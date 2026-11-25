# realpawns — research notes

Goal (revised 2026-11-24, see "Design" below): official offline (rift) pawns
keep their progress. While hired they gain Discipline (DP) and vocation ranks,
and the player can change their gear, learn and set skills, and (later)
change their vocation. On save, that progress is stored per pawn and
re-applied whenever the pawn is listed or summoned again. **Pawn level never
changes**: no XP, no level-ups. What the rift search shows must match what
gets hired.

pawnlib-offline is assumed *not* loaded; only official offline pawns
(romNoraPawnData.arc) are handled.

## Design

### Identity: `mRomPawnUniqueID`

Offline pawns are synthesized from template + level (GOG addresses):

- Search listing `FUN_00792510(mgr, out, EAX=level)`: picks the level bracket
  (table `DAT_014eb954`), a random template index inside it, calls the
  synthesizer `FUN_00792c10(mgr, out, template, level)`, then stamps
  `out+0x1478 = ((level + 0x1000) << 16) | index` (`0x10LLIIII`).
- Favorites / re-summon dispatcher `FUN_00792610(mgr, out, key)`: for
  `0x1…` keys it decodes `level = (key>>16)&0xFF` and `index = key&0xFFFF`,
  re-synthesizes, and re-stamps the same key. For `0x2…` keys (authored story
  pawns) it copies a record from a table.
- Recommended-pawns builder `FUN_007927c0` synthesizes the same way into the
  save's record region (see open items).

The key is therefore a deterministic, re-synthesizable identity:
(template, level). Because the level is fixed, the key never changes for a
given pawn.

### Capture (on save)

Detour the save-write state machine (pawnlib's `pawnhook`; GOG write
`0x004b7830`). From the inflated save XML, read the two hired-pawn
`cSAVE_DATA_CMC` blocks (pawnlib's `pawnsave.read_hired`). For each occupied
slot whose key is `0x1…` or `0x2…`, write the mutable fields to
`<dll_dir>/realpawns/<key as 8 hex>.xml`. Each save overwrites that pawn's
file. The store lives outside the save, so it survives new playthroughs and
is shared across save files.

### Apply (when the pawn is built)

Post-call detours on `FUN_00792510` (search) and `FUN_00792610` (dispatch).
Both stamp the key last and then return. After the original returns, look up
`out+0x1478`. On a hit, overwrite **only the mutable fields** in the 0x14E8
record, using the clone-then-overwrite approach of pawnlib's
`nora_pawn_to_record`. This happens before the list UI reads the record, so
the preview, filtering by skills, the hire cost and the hired pawn all agree.

### Field policy

| Persisted (overlaid) | Never touched (from the game) |
|---|---|
| gear (`mEquipItem`) | name, appearance, voice, gender |
| learned + equipped skills, augments | level, exp, nextExp |
| current vocation, jobLevel / jobExp / jobNext | stats (HP, stamina, str/def/mag/mdef) |
| Discipline (`mJobPoint`) | inclinations |
| bestiary (`mStudyFlag`, `mLocalStudyFlag`, `mStudyData`) | the key `+0x1478` |

Vocation rank doesn't affect stats (stats only grow at level-up), so a
level-20 pawn with rank-9 vocations is still a consistent level-20 pawn.

### In-session progress: Discipline only, no XP

Discipline is a single gain that the game stores in two counters.
`FUN_00780380` computes one amount from the XP and adds it to both:

- `mJobPoint` (+0x344): the spendable pool shared by all vocations. Learning
  skills and unlocking vocations reduce it.
- `mJobExp[voc]` (+0x2F4): rank progress for the current vocation. Spending
  doesn't reduce it; `FUN_00b59890` turns it into `jobLevel` rank-ups.

Both must be persisted. Support pawns currently get no Discipline at all (see
section 1). The patch must grant **the Discipline gain but skip the exp add**
for slots 2/3. Otherwise they would level up mid-session and drop back on the
next load.

### Coexistence with pawnlib-offline

Both mods detour the same rift functions. With pawnitems-style signature
checks, whichever loads second refuses to patch and logs it. Chaining them
later is possible: realpawns only acts on `0x10`/`0x20` keys, and pawnlib uses
`0x11`.

### Research results (2026-11-24)

#### R1. 0x14E8 rift-record layout: resolved

The record's `mParam` block starts at **record+0x58**. It is the class
registered by `FUN_00782e00` (property names confirmed from its string refs)
and initialized by `FUN_00783bb0` (record init = `FUN_00785430`, which also
memsets the payload). Record offset = 0x58 + p:

| field | p | record | type |
|---|---|---|---|
| mLevel / mJob | +0x04 / +0x05 | 0x05C / 0x05D | u8 / u8 (mJob = current vocation) |
| mJobLevel | +0x06 | 0x05E | u8[10] |
| **mJobExp** | +0x10 | **0x068** | u32[10] |
| **mJobNextExp** | +0x38 | **0x090** | u32[10] |
| **mJobPoint** | +0x60 | **0x0B8** | s32, single pool (getter `0x00779c70`) |
| mWeaponSkill | +0x70 | 0x0C8 | s16[12][6] (equipped skills per weapon) |
| mAbility | +0x100 | 0x158 | s8[6] (equipped augments) |
| mSkillLv1 / mSkillLv2 | +0x108 / +0x140 | 0x160 / 0x198 | u32[14] (learned / upgraded skill bitfields) |
| mAbilityInfo | +0x178 | 0x1D0 | u32[4] (learned augments) |
| stats | +0x188 | 0x1E0 | f32[10] |
| mExp / mNextExp | +0x1B0 / +0x1B4 | 0x208 / 0x20C | u32 |
| mGold | +0x1B8 | 0x210 | s32 |

"Learned skills" are mSkillLv1/mSkillLv2 + mAbilityInfo, which pawnlib
already pins. The save XML writes mJobPoint as `s32[10]` with the same value
in every element (it is a scalar exposed as a 10-element property; checked on
the main pawn: 367276 ×10).

In-memory party param (for comparison): jobExp +0x2F4, jobNext +0x31C,
jobPoint +0x344, mGold +0x348, **OnlinePoint +0x34C** (the 10% of XP that
slots 2/3 bank is the owner's rift-point reward, not DP).

Hired pawns in the current save have mJobExp = mJobPoint = 0, which matches
"support pawns get no Discipline".

#### R2. Record region `pBase+0x16E0`: needs a third apply point

120 records × 0x14E8, in memory only (the save has no such array; its pawn
lists `mFavoritePawn` / `mBorrowPawn` / `mLentPawn` / `mPawnHistory`
(100 each) are `cSAVE_DATA_ONLINE_PAWN` entries holding **only the key** plus
counters such as mWithTime, mEmployCount and mClearQuestCount).

- Slots 0..19: story pawns (`0x2…` dispatch copies from here).
- Slots 20..119: the rift's pool of 100 pawns, built by `FUN_0047d0d0`:
  1. for each key in the in-memory list at `pBase+0x9E3FC` (entry key at
     +0x94) that isn't already in the pool: `FUN_00792610` dispatch into a
     local record, then copy header/param (`FUN_007851e0`, `FUN_00783db0`)
     and the 0x680..0x13D8 payload into the slot. **Covered** by the dispatch
     overlay, since the copy happens after it.
  2. fill the rest with `FUN_007927c0(mgr, 100-n)`, which calls the
     synthesizer `FUN_00792c10` directly and stamps the key inline.
     **Not covered** by the two planned hooks.
- Fix: after `FUN_007927c0` (or `FUN_0047d0d0`) returns, run a post-pass over
  slots 20..119 that overlays every slot whose +0x1478 key is in the store.
  The many readers of this region (rift code `FUN_0047d3b0`/`FUN_0047da00`,
  actor code around 0x00BA8xxx/0x00BBxxxx) then see the overlaid data.

Also noted: `mCloseFriendPawn[4]` and `mLastClearCloseFriendPawnData[4]` in
the save carry **full** cSAVE_DATA_CMC blocks. Their purpose is unknown
(possibly the "pawns you adventured with" / last-clear list). If the game
restores a pawn from them, it would use that snapshot instead of our store.
Low priority; check if it ever matters in-game.

#### R3. Key on hire: confirmed

Tested in-game 2026-11-24: an official pawn (Wynne, level 179, Sorcerer) was
hired and the game saved. Her `mCmc[2].mRomPawnUniqueID = 0x10B302C4`, i.e.
level 0xB3 = 179 (matches her mLevel) and template index 0x2C4 = 708 (so the
bracket banks hold 700+ templates). `mPawnType` = 0 for hired pawns, 1 for the
main pawn.

Earlier indirect evidence:

- The hired slots of the current save hold `0x11000066` / `0x11000065`,
  exactly the keys pawnlib stamps into record+0x1478. So hiring copies the
  record key into `mCmc[s].mRomPawnUniqueID` unchanged.
- `mBorrowPawn` / `mPawnHistory` contain official keys in the expected form,
  e.g. `0x1012002F` (level 18, index 0x2F) and `0x104501B5` (level 69).

#### R4. Rift pawn data files: resolved

There is no `romNoraPawn.arc`. Everything is inside
`nativePC/rom/etc/romNoraPawnData.arc`:

- `etc\editPattern\romNoraPawn00` … `romNoraPawn32`: **33 template banks**.
  This matches the 0x21 (33) level brackets of table `DAT_014eb954`, so
  presumably one bank per bracket, with the key's index selecting a template
  inside it (inferred from the count, not traced to the loader).
- `etc\editPattern\romPawnInfo` (story pawns), `romPawnNameAlphabet`,
  `etc\skill\skill`, `param\pl\level\Lv*` (per-vocation level tables),
  `param\pl\stamina\PlStaminaMax`, `AI\preset_charainfo`, `AI\preset_study`.
- Template class `rRomNoraPawn::cRomNoraPawn` fields: mRomPawnEdit,
  WeightType, Job, AiNo, CharaNo, NameNo, JobLevel, NormalSkill, CustomSkill,
  Ability, EquipItemId. Only identity plus a starting kit; level-dependent
  stats come from the level tables at synthesis time.

#### Updated apply points

1. `FUN_00792510` (search listing), post-call.
2. `FUN_00792610` (favorites / reload / pool fill from lists), post-call.
3. Rift pool post-pass after `FUN_007927c0` (random pool fill).

All VAs are **GOG DDDA.exe** (image base 0x00400000), same build pawnitems
targets (pawnitems' give signature at 0x00461172 matches the Ghidra project).

## Shared layout

Persistent party data lives at `pBase = *DAT_018db49c`:

    slot 0 Arisen      param = pBase + 0xA76D0
    slot s=1..3 pawns  param = pBase + 0xA6860 + s*0x1660
                       (1 = main pawn 0xA7EC0, 2 = 0xA9520, 3 = 0xAAB80)

`FUN_00442570(base, EAX=slot)` is the canonical slot -> param resolver (and
it is inlined in many places as `slot*0x1660 + 0xA6860`).  The param block is
`cmc + 0x6D0` in pawnlib-offline terms, so dinput8's offsets = param + 0x6D0.

Param block fields (offsets from param):

    +0x004 u8   slot alive
    +0x008 u32  party slot index
    +0x010 u32  current vocation (dinput8 0x6E0)
    +0x2C4 u32  exp          +0x2C8 u32 nextExp
    +0x2CC u32[10] jobLevel  +0x2F4 u32[10] jobExp  +0x31C u32[10] jobNext
    +0x344 u32  mJobPoint (Discipline, cap 999999)
    +0x34C u32  slot 2/3 only: 10% of XP banked for the owner (likely RC reward)
    +0x700 u32  level
    +0x7F4 u32  pawn kind: 1 = main pawn, 2 = support pawn (set to 2 in FUN_0043fca0)

Live actor: `actor+0x3DEC` -> param block; `actor+0x206D` flags
(2 = Arisen, 4 = pawn, 8 = NPC); `actor+0x5854` pawn index.

## 1. XP / DP for support pawns — small job

(Scope note: per the design above, only the Discipline gain (mJobPoint + mJobExp) is wanted.
The level-up machinery described here stays untouched for slots 2/3.)

`FUN_00442f20(mgr, xp, mult, raw)` distributes kill XP over slots 0..3:

- slot 0: `FUN_00780380(xp)` (exp + DP + jobExp) + `FUN_0044b190` (player-only total)
- slot 1: catch-up formula vs. the Arisen's exp, then `FUN_00780380(xp')`
- slot 2/3: **only** `param+0x34C += xp*0.1` (cap 99,999,999). No XP.

`FUN_00780380` (EDI = param, stack arg = xp) has a second gate:

    007803ae  83 BF F4 07 00 00 01   cmp dword [edi+7f4], 1   ; main pawn?
    007803b5  0F 85 D5 01 00 00      jnz 00780590             ; return

(taken only when the param's DTI is the pawn class `DAT_0197413c`).
Only caller is FUN_00442f20, so relaxing it is safe.

Everything downstream is already generic:

- Level-up: `FUN_00b59380(actor,1,0)` is called every frame from the human
  update `FUN_00b5f080` for every non-NPC actor (exp >= nextExp -> level++,
  stats from the per-vocation table at `DAT_018db4cc+0x86C`, new nextExp).
- Vocation rank-up: `FUN_00b59890(actor)` per frame, same pattern on
  jobExp/jobNext/jobLevel.
- DP item multipliers read per-slot equipment (`DAT_018db4b4 + slot*0x11E8`).

Patches: (a) shim in FUN_00442f20 so slots 2/3 also call FUN_00780380 (optionally
keep the 0x34C banking); (b) NOP/relax the jnz at 0x007803b5.

## 2. Vocation / skills for support pawns

### Skills (learn + set) — small/medium

Guild/inn skill screens are `uGUISkillLearn` (DTI 0x01973090, size 0x370,
ctor base `uGUISkillBase` 0x006F75B0), mode `gui+0x2D4` (0/1), created by
`FUN_00512190` / `FUN_00512460`.

- Target character is `gui+0x270` = party slot (0..3, 4 = none); all lookups
  use the generic slot formula (e.g. DP check in `FUN_006fc790`).
- Tab change callback `FUN_006ffc70` copies the tab value into `gui+0x270`.
- On close, `FUN_00512460` already loops slots 0..3 and refreshes actors for
  slots flagged in `gui+0x33C[slot]`.
- **Only limit:** init `FUN_006f8fd0` builds the character tab list with
  count = 1 (+1 if main pawn exists via `FUN_0077da70`), then hides tab widgets
  [count..3] — the layout has 4 tabs.
- `uGUISkill` init `FUN_006f2730` (0x006F2AC0..0x006F2BE5) already builds a
  4-slot tab list (Arisen, main, 0xA9520, 0xAAB80) — copy that pattern.

Open: confirm no further "slot >= 2" / `+0x7F4 == 1` gate inside the learn/set
flow (none found in a first pass; remaining `+0x7F4==1` sites are
FUN_006df310, FUN_006e98a0, FUN_0071f770, FUN_00645b30, FUN_00410b50,
FUN_00416c90, FUN_00b74e40 — the 0x0057Exxx cluster is inclination AI).

### Vocation change — medium/hard

`uGUIJobChange` (DTI 0x019726C4, size 0x1300, vtable 0x01563728, ctor
0x0066FD30, init `FUN_00670130`, state machine `FUN_006711d0`, apply
`FUN_00674360`). Opened by `FUN_00510d80` (town) / `FUN_00512740` (game start).

- Row index == party slot, resolved with the generic formula, and the apply
  path is generic (`param+0x10 = job`, `FUN_00671d00`, `FUN_0077dd00`,
  `FUN_0077dfb0`, flag `pBase+0xAC3C0+slot*4 = 1`, a 4-entry array).
  `FUN_00673d40` already gives non-Arisen slots the 6 pawn vocations.
- **But** it is built for exactly 2 rows: init hardcodes 0xA76D0 / 0xA7EC0,
  loops are `< 2`, and per-row arrays are packed in the 0x1300 object:
  +0x270 short[2][10] unlock cost, +0x29C u32[2] chosen job, +0x2A4 u8[2]
  changed, +0x2F0 2 x 0x7F0 param working copies (ends at +0x12D0, which is
  the next field).
- Option A (recommended): keep 2 rows, remap row 1 -> a chosen pawn slot
  (1/2/3); needs a picker (e.g. cycle on reopen / hotkey) and patching each
  inline slot computation in the class (~10-15 sites).
- Option B: grow to 4 rows — bump allocation size and relocate every row
  array access. Much larger.

## Tooling

Project skills live in `.claude/skills/`:

- `arctool`: inspect/extract `.arc` files (e.g. `romNoraPawnData.arc`).
- `ddsavetool`: decode saves to XML (hired-pawn blocks, `mRomPawnUniqueID`).

The `.exe` binaries are git-ignored and supplied by the user.

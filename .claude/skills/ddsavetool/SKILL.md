---
name: ddsavetool
description: >-
  Unpack, inspect, edit, repack, or convert Dragon's Dogma: Dark Arisen save
  files (.sav, e.g. DDDA.sav). Use whenever the user wants to decode a DDDA
  save to XML (e.g. to inspect hired-pawn cSAVE_DATA_CMC blocks or
  mRomPawnUniqueID), edit save data, repack XML into a .sav, convert a console
  save to PC, or back up saves. Runs the bundled ddsavetool.exe via wine.
---

# ddsavetool: editing DDDA save files

This skill wraps FluffyQuack's `ddsavetool.exe` in a Linux helper, `ddsave`,
that runs it through `wine` and always backs up a `.sav` before overwriting
it. Both files sit in this skill directory:

    SKILL=/home/istvan/repos/realpawns/.claude/skills/ddsavetool
    $SKILL/ddsave          # wrapper (tracked)
    $SKILL/ddsavetool.exe  # third-party binary (git-ignored; the user supplies it)

The live save is in the GOG build's save folder (PC format, 524288 bytes;
the path contains "Galaxy", but the Galaxy client isn't installed, so nothing syncs it):

    SAVEDIR="/home/istvan/.wine/drive_c/users/istvan/AppData/Local/GOG.com/Galaxy/Applications/49987265717041704/Storage/Shared/Files"
    $SAVEDIR/DDDA.sav           # active save
    $SAVEDIR/DDDA.sav.001.backup

## Never work on the live save in place

That folder is the live save, and `unpack` writes `<save>.xml` next to its
input. For research, **copy the save into the session scratchpad first** and
work on the copy:

```sh
cp "$SAVEDIR/DDDA.sav" "<scratchpad>/DDDA.sav"
$SKILL/ddsave unpack "<scratchpad>/DDDA.sav"    # -> <scratchpad>/DDDA.sav.xml
```

Only write back to `$SAVEDIR` when the user explicitly asks for it. Even then
`repack` backs up the target first (see below).

## Always use the `ddsave` wrapper; never call the .exe directly

`ddsavetool.exe` returns exit code **1 on success and 0 on a missing-file
error**, so `$?` is misleading. The wrapper checks the tool's output and the
file it produced instead.

| Command | Effect |
|---|---|
| `ddsave unpack <savefile> [out.xml]` | Decode a `.sav` to editable XML (the `.sav` is not modified) |
| `ddsave repack <xml\|sav> [out.sav] [--console]` | Rebuild a `.sav` from XML (PC format by default) |
| `ddsave convert <console_savedata> <pc.sav>` | Convert an Xbox360/PS3 save to PC format |
| `ddsave roundtrip <savefile>` | Check that unpack/repack is lossless for a save (read-only) |

`repack` takes either the `.xml` or the `.sav` name and finds the matching
`.xml` itself.

## Backups (mandatory; never skip)

Before `repack` or `convert` overwrites an existing `.sav`, the wrapper copies
it to `<dir>/backups/<name>.<YYYYmmdd-HHMMSS>.bak`. This happens
automatically; don't bypass it. `unpack` and `roundtrip` never modify the
original save.

## Useful targets in the XML

The XML is large (~20 MB). Grep for specific elements instead of reading the
whole file:

- `mRomPawnUniqueID`: the offline-pawn key (`0x10LLIIII` generated,
  `0x2000xxxx` story pawns).
- The hired-pawn `cSAVE_DATA_CMC` blocks (party slots 2/3), and fields such as
  `mJobPoint`, `mJobExp`, `mNextExp`, `mEquipItem`, `mStudyFlag`.

## Notes

- Requires `wine`. The wrapper errors clearly if `wine` or the `.exe` is missing.
- Unpack → repack → unpack is lossless: the decompressed XML comes out
  byte-identical (only the compressed-size header field changes).
- If `ddsavetool.exe` is missing, ask the user to put it in this directory.
- Sibling skill: `arctool` (for `.arc` archives).

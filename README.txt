realpawns
=========

A rift pawn progression mod for Dragon's Dogma: Dark Arisen.

Pawns you hire from the rift normally stay as they were made: they earn
no Discipline, you can't teach them skills, and whatever you change on
them is gone the next time you hire them. realpawns turns every official
offline pawn you hire into your own copy of that pawn, which grows while
it travels with you, levels up, and remembers all of it.


FEATURES
--------

- Hired pawns become yours. Hiring an official rift pawn makes a clone
  of it with its own rift id. The original stays in the rift exactly as
  it was, so others can still find it, and you can even hire it again
  (which makes another clone).
- Clones remember. Each time the game saves, a hired clone's level, XP,
  stats, gear, learned and equipped skills, augments, vocation ranks,
  Discipline, inclination and bestiary knowledge are stored. When you
  meet it again, in the level search, your favorites, your history or
  the rift's own selection, it has all of that. That holds in a new
  playthrough too, and across save files.
- Clones level up. Hired clones gain XP the way your main pawn does:
  quickly while they are behind you, slowly or not at all once they are
  far ahead. Their stats grow with their current vocation, as the main
  pawn's do.
- Found at their own level. Searching the rift by level lists your
  clones saved at that level first; the rest of the list is the usual
  rift pawns. A clone that has levelled up turns up under its new level.
- What you see is what you hire. Stored progress is applied when the
  rift builds its pawn list, so the preview, searching by skills, the
  hire cost and the pawn you hire all agree.
- Discipline for support pawns. Hired pawns earn Discipline and vocation
  rank from kills, just like your main pawn.
- Skill lessons for support pawns. The inn's skill screen gets tabs for
  your hired pawns, so you can spend their Discipline on new skills and
  choose which skills they use.
- Starting skills. Rift pawns are created without the free skills a
  vocation starts with (such as the Fighter's Blink Strike), so their
  upgrades could never be learned. realpawns gives every rift pawn the
  starting skills of every vocation, like a character who has tried
  them all. Skills you normally buy with Discipline are not given away.
- Vocation change for support pawns. Give a support pawn a Neutralizing
  Elixir (configurable), then open the vocation screen: that pawn takes
  the main pawn's row and can change vocation with its own rank and
  Discipline. The usual level requirement applies (advanced vocations
  from level 10). Optionally the elixir is used up on each change.
- Inclination elixirs for any pawn. The nine inclination elixirs
  (Scather's to Acquisitor's) and the Neutralizing Elixir can be used
  from your main pawn's and your support pawns' inventories. They act on
  the pawn that carries them, so you can steer a support pawn's
  inclination too. Used from your own inventory they still go to your
  main pawn, as before.
- Gear for support pawns. Gear you take off a support pawn stays in its
  inventory instead of being sent away when you close the equipment
  menu, and the "Removing this equipment will send it away" prompt is
  skipped. Support pawns can equip, be given, and withdraw from storage
  gear they normally refuse: upgraded gear (such as 3-star or
  dragonforged) and restricted item types, which include the elixirs
  above. Vocation rules still apply: a pawn can't equip gear its
  vocation can't use. Barroch's upgrade screen gets pages for your
  support pawns too, as the smiths' does.
- Configurable. Four switches in realpawns.ini: persist, pawn
  progression, inclination elixirs and pawn gear.
- Reversible. All changes are made in memory while the game runs. Remove
  realpawns.dll and the game goes back to its stock behaviour. Progress
  already earned stays in your save, as it would after a normal hire.


REQUIREMENTS
------------

- Dragon's Dogma: Dark Arisen on PC, GOG release. Linux with Wine /
  Proton works too.

  The Steam release isn't supported yet. realpawns checks the game code
  before changing anything; on another build it changes nothing and
  logs why in realpawns.log.

- dinput8, which loads add-on DLLs into the game:
  https://github.com/istvan-sipos/dinput8

- Not together with pawnlib-offline or pawnitems. realpawns changes the
  same rift code as pawnlib-offline, and pawnitems' gear features are
  built into realpawns. Whichever mod loads second refuses to patch and
  says so in its log.


INSTALLATION
------------

1. Install dinput8 first. Its dinput8.dll and dinput8.ini should sit
   next to DDDA.exe in your game folder.

2. Copy realpawns.dll and realpawns.ini into the same folder, next to
   DDDA.exe.

3. Open dinput8.ini in a text editor. Under the [main] section, add
   realpawns.dll to the addons line:

       addons = realpawns.dll

   If you already load other addons, separate them with semicolons:

       addons = other.dll;realpawns.dll

4. Launch the game. A realpawns.log file appears next to DDDA.exe. Near
   the top it should say:

       clones: ready
       persist: on
       pawn progression: ready (marker item 1529, kept)
       inclination elixirs: ready
       pawn gear: ready

   A realpawns folder is created next to the DLL. It holds one file per
   clone (named after its rift id, 12000001.xml and up) and
   next_id.txt, the next clone number. Keep it if you reinstall.
   Deleting a clone's file removes that clone: it no longer shows up in
   the rift, and its favorites entry can't be summoned.


CONFIGURATION
-------------

realpawns.ini sits next to realpawns.dll. Every switch defaults to on;
set it to 0 to turn that feature off. Cloning and the starting skills
are always on.

    [main]
    logging = truncate     ; disabled, truncate (reset each launch),
                           ; or append

    [features]
    persist = 1            ; hired clones' progress is saved for rehire
    pawn_progression = 1   ; Discipline, leveling, skill tabs and
                           ; vocation change for support pawns
    inclination_elixirs = 1 ; elixirs usable from any pawn's inventory
    pawn_gear = 1          ; keep unequipped gear, equip restricted and
                           ; upgraded gear, Barroch's upgrade pages

    [vocation]
    marker_item = 1529     ; item that picks the pawn (Neutralizing Elixir)
    consume_marker = 0     ; 1 = one is used up per vocation change

Giving support pawns restricted items (with [Give] or from storage) is
on while any of pawn_progression, inclination_elixirs or pawn_gear is
on: the elixirs and the vocation marker are restricted items.


NOTES
-----

- persist = 0 still makes a clone on every hire, and it still earns
  Discipline and levels up, but only for as long as it is in your party.
  Nothing is written to the realpawns folder, so once it leaves it can't
  be hired again. Clones saved earlier still show up in the rift as they
  were saved, and their files aren't updated.
- Progress is stored when the game saves. A clone you dismiss before
  the next save forgets what it learned since the last one; one you
  hire and dismiss before any save is never stored at all.
- Only official rift pawns are cloned. Story pawns stay as the game made
  them (no XP, nothing stored), and pawns from other mods (such as
  pawnlib-offline's archive) are left alone. Neither gains levels.
- Up to 65,535 clones. Numbers are never reused, even after a clone's
  file is deleted.
- Without realpawns the game can't build a clone, so clones in your
  favorites or history simply don't show up. A clone in your party stays
  as it is: the save holds all of it.
- Upgrading from realpawns 1.2 or older: the old files in the
  realpawns folder (named 10xxxxxx.xml / 2xxxxxxx.xml) are no longer
  used and can be deleted. A rift pawn already in your party becomes
  a clone at the next save; save once more afterwards so the save file
  carries its new id too.
- Vocation change: the first support pawn that is present (not dead or
  away) and carries the marker item is picked, slot 2 before slot 3. While
  one carries it, the vocation screen shows that pawn instead of your main
  pawn; take the item back to change your main pawn. Cancelling costs
  nothing. Rift pawns come with every vocation unlocked, so they pay no
  Discipline to unlock one.
- Inclination: support pawns' inclinations still never drift on their
  own; only elixirs change them. The Neutralizing Elixir is also the
  default vocation marker, so using it from a pawn's inventory uses up
  that pawn's marker.
- Gear: a few specific items are blocked for support pawns by the
  game's vocation check rather than the rules above, and stay blocked.
  dinput8's ignoreEquipVocation option lifts them along with every
  other vocation restriction.
- Upgrading from pawnitems: remove pawnitems.dll from the addons line.
  Its settings are now the pawn_gear switch in realpawns.ini.


BUILDING
--------

The mod is plain C, built with the MinGW-w64 32-bit cross compiler and
its static zlib. On Debian / Ubuntu:

    sudo apt-get install gcc-mingw-w64-i686-win32 libz-mingw-w64-dev

Then, from the repository root:

    make                   build realpawns.dll
    make deploy            build and copy it to DDDA_DIR (see Makefile)
    make deploy-ini        copy the default realpawns.ini to DDDA_DIR

Edit DDDA_DIR at the top of the Makefile to point at your game folder.
deploy-ini is separate so a customised ini isn't overwritten on every
build.

Source layout:

    src/realpawns.c   DllMain, ini parsing, logging
    src/patch.c       signature-checked code patching helpers
    src/savehook.c    save-write hook (captures hired pawns)
    src/pawnxml.c     reads a pawn's progress from the save data
    src/store.c       the realpawns\ folder: one file per clone
    src/rift.c        clones: made on hire, rebuilt in rift lists
    src/discipline.c  Discipline for support pawns, XP for clones
    src/skilltabs.c   support pawn tabs on the inn skill screen
    src/innate.c      vocation starting skills
    src/vocation.c    vocation change for support pawns
    src/inclination.c inclination elixirs from any pawn's inventory
    src/unequip.c     keep gear unequipped from support pawns
    src/restrict.c    equip / give / withdraw restrictions

realpawns.md has the reverse-engineering notes: every game address
used, and why.

realpawns
=========

A rift pawn progression mod for Dragon's Dogma: Dark Arisen.

Pawns you hire from the rift normally stay as they were made: they earn
no Discipline, you can't teach them skills, and whatever you change on
them is gone the next time you hire them. realpawns lets the official
offline pawns grow while they travel with you and remember it, without
changing their level.


FEATURES
--------

- Pawns remember. Each time the game saves, the gear, learned and
  equipped skills, augments, vocation ranks, Discipline and bestiary
  knowledge of your hired pawns are stored. The next time the same pawn
  turns up, in the level search, your favorites, your history or the
  rift's own selection, it has all of that again. That holds in a new
  playthrough too, and across save files.
- What you see is what you hire. The stored progress is applied when the
  rift builds its pawn list, so the preview, searching by skills, the
  hire cost and the pawn you hire all agree.
- Levels never change. A pawn you found at level 20 stays level 20, with
  level-20 stats. Support pawns gain no XP; only their vocation grows.
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
- Configurable. Each feature can be switched off in realpawns.ini.
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

- Not together with pawnlib-offline. Both mods change the same rift
  code; whichever loads second refuses to patch and says so in its log.
  pawnitems works fine alongside realpawns.


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

       addons = pawnitems.dll;realpawns.dll

4. Launch the game. A realpawns.log file appears next to DDDA.exe. Near
   the top it should say:

       persist: ready
       discipline: ready
       skill tabs: ready

   A realpawns folder is created next to the DLL. It holds one file per
   pawn you have hired (named after the pawn's rift id). Keep it if you
   reinstall; deleting a file makes that pawn forget its progress.


CONFIGURATION
-------------

realpawns.ini sits next to realpawns.dll. Every option defaults to on;
set it to 0 to turn that feature off.

    [main]
    logging = truncate     ; disabled, truncate (reset each launch),
                           ; or append

    [features]
    persist = 1            ; hired pawns keep their progress
    discipline = 1         ; support pawns earn Discipline, no XP
    skill_tabs = 1         ; inn skill screen has tabs for them
    innate_skills = 1      ; rift pawns know the vocation starting skills


NOTES
-----

- Progress is stored when the game saves. A pawn you dismiss before the
  next save forgets what it learned since the last one.
- Only official rift pawns are stored. Pawns from other mods (such as
  pawnlib-offline's archive) are left alone.
- The same rift pawn found at a different level is a different pawn to
  the game, so it has its own progress.
- Support pawns still can't change vocation.


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
    src/store.c       the realpawns\ folder: one file per pawn
    src/rift.c        applies stored progress to rift pawns
    src/discipline.c  Discipline without XP for support pawns
    src/skilltabs.c   support pawn tabs on the inn skill screen
    src/innate.c      vocation starting skills

realpawns.md has the reverse-engineering notes: every game address
used, and why.

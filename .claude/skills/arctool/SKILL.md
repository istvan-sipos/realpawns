---
name: arctool
description: >-
  Extract, list, edit, repack, or restore Dragon's Dogma: Dark Arisen .arc
  archives (romNoraPawnData.arc, npc*.arc, shop lists, GMD text, etc.). Use
  whenever the user wants to unpack an .arc, inspect its contents (e.g. the
  offline-pawn templates), repack it back into the game, or revert a modded
  .arc to vanilla. Runs the bundled arctool.exe via wine on Linux.
---

# arctool: editing DDDA .arc archives

This skill wraps FluffyQuack's `arctool.exe` in a Linux helper, `arc`, that
runs it through `wine`. Both files sit in this skill directory:

    SKILL=/home/istvan/repos/realpawns/.claude/skills/arctool
    $SKILL/arc          # wrapper (tracked)
    $SKILL/arctool.exe  # third-party binary (git-ignored; the user supplies it)

The game is the **GOG** install under wine:

    GAME=/home/istvan/.wine/drive_c/DDDA
    $GAME/nativePC/rom/...           # .arc files
    $GAME/nativePC/rom/etc/romNoraPawnData.arc   # offline (rift) pawn data

## Always use the `arc` wrapper; never call the .exe directly

`arctool.exe` returns exit code **0 both on success and on a missing-file
error**, so `$?` tells you nothing. The wrapper checks the tool's output and
the files it produced instead. It also always passes the flag set known to
round-trip DDDA archives (`-dd -texRE6 -alwayscomp -pc -txt -v 7 -xfs`).

| Command | Effect |
|---|---|
| `arc extract <archive.arc> [workdir] [--force]` | Unpack for editing (XFS files converted to XML) |
| `arc repack <archive.arc> [workdir]` | Rebuild the archive and install it over `<archive.arc>` |
| `arc restore <archive.arc>` | Copy `<archive.arc>.original` back over the archive (revert to vanilla) |
| `arc list <archive.arc>` | Print archive contents (read-only, uses a temp dir) |

## Read-only research (the usual case for realpawns)

For reverse engineering we only *read* archives. Pass an explicit workdir in
the session scratchpad so the game folder isn't cluttered:

```sh
$SKILL/arc list "$GAME/nativePC/rom/etc/romNoraPawnData.arc"
$SKILL/arc extract "$GAME/nativePC/rom/etc/romNoraPawnData.arc" "<scratchpad>/romNoraPawnData.work"
```

Note: `extract` still creates the one-time `<archive.arc>.original` backup
next to the game file, which is harmless.

## Pristine backup (mandatory; never skip)

The first time `extract` or `repack` touches an archive, it creates a
**one-time** pristine copy next to it: `<archive.arc>.original`. That copy is
only made if it doesn't already exist, so the vanilla file survives any
number of later repacks. Never delete or overwrite a `.original` file;
`arc restore` depends on it.

## Edit workflow (only when the user asks to modify game files)

```sh
ARC=$GAME/nativePC/rom/npc/npc117_01.arc
$SKILL/arc extract "$ARC"     # -> <arc-dir>/npc117_01.work/npc117_01/ + .original
# edit files in the .work tree (XFS files are converted to *.xml)
$SKILL/arc repack "$ARC"      # rebuilds and installs over $ARC
$SKILL/arc restore "$ARC"     # back to vanilla if something breaks
```

`extract` refuses to overwrite a non-empty workdir unless you pass `--force`,
which discards any edits there and extracts fresh.

## Notes

- Requires `wine`. The wrapper errors clearly if `wine` or the `.exe` is missing.
- Round-trips are byte-faithful: repacking an unmodified extraction
  reproduces the same archive size and file order.
- If `arctool.exe` is missing, ask the user to put it in this directory.
- Sibling skill: `ddsavetool` (for `.sav` save files).

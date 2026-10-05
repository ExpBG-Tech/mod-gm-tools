# EXPBG GM Tools

One Arma Reforger Game Master modpack by M.Pac and K.Edgar. It bundles seven
modules into a single Workshop item and engine project
(`FC1402F65B2F4A45`, Unlisted). Each module keeps its own source folder under
[`addon/`](addon) so it can be maintained on its own.

| Module | Folder | Origin |
|---|---|---|
| Garrison | `addon/garrison` | Developed here |
| Unit Caching (formerly GM Optimizer) | `addon/unit-caching` | `mod-gamemaster-optimizer` 0.1.31 |
| Intel Items | `addon/intel-items` | `mod-intel-items` 0.0.4 |
| Ambient Civilians | `addon/ambient-civilians` | `mod-ambient-civilians` 0.0.23 |
| Ambient Destruction | `addon/ambient-destruction` | `mod-ambient-destruction` 0.0.9 |
| Ambient Sounds | `addon/ambient-sounds` | `mod-ambient-sounds` 0.2.9 |
| Persistent Battlefield | `addon/persistent-battlefield` | `mod-persistent-battlefield` 1.0.5 |

Exact source commits and former Workshop IDs are recorded in
[`tools/pack.json`](tools/pack.json). Module folders keep the original class
names, resource GUIDs, prefab paths, serialized keys and artwork, so module docs
in the original repositories still describe their behavior. User-visible
"GM Optimizer" labels are renamed to "Unit Caching"; its classes still use the
`EBG_` prefix.

## Use the pack or the standalone mods, not both

The pack contains the full implementations. Do not enable it together with the
standalone EXPBG GM Optimizer, Intel Items, Ambient Civilians, Ambient
Destruction, Ambient Sounds or Persistent Battlefield items, or their CDF
companions: duplicate classes and resources will not load. The standalone
Workshop listings remain available for existing modsets.

The only dependency is the base game (`58D0FB3206B6F859`).

## Modules

- **Garrison**: right-click a building, choose **Add Garrison** and select an
  infantry squad. One squad takes reachable fixed guard posts and patrols
  indoors. A freshly spawned squad larger than the safe capacity is trimmed;
  existing squads and casualties are never refilled. Caching offers Off,
  Simulation and Full (default) with per-garrison wake/sleep distances. Full
  recreates survivors at their captured world transforms with their posts in the
  original group, using prefab-default kits and health. Force Move releases the
  garrison. Use Unit Caching **Prepare for Save** before saving; assignments are
  mission-only.
- **Unit Caching**: AI Cache Zones and the EXPBG UNIT CACHING CONTROLLER.
  Simulation pauses existing AI; Full removes supported groups and restores only
  survivors.
- **Intel Items**: placeable intel with GM-authored title and text.
- **Ambient Civilians**: civilian population module and exclusion zones.
- **Ambient Destruction**: permanent building damage, rubble and road wrecks.
- **Ambient Sounds**: war ambience, radios, crowds and emergency-alert TVs; new
  modules start OFF.
- **Persistent Battlefield**: body/wreck lifetime rules, reconnect retention
  and spawn weapon safety. It overrides the vanilla `Character_Base` prefab and
  systems config.

## CDF Game Master Save

The existing CDF companions depend on the standalone mods and are not
compatible with the pack. The guards in Unit Caching and Garrison check for a
reserved pack companion identity (`07BC942D90324CD9`, not yet built). Until it
exists, Full caching refuses new removals while CDF is loaded and Garrison
caching stays off; Simulation caching and restoration still work.

## Building

`addon/EXPBG_GM_Tools.gproj` is the pack project. Opening a module folder in
Workbench directly does not work; [`tools/Assemble-Pack.ps1`](tools/Assemble-Pack.ps1)
copies every module's runtime files into one project and merges the three shared
vanilla overrides (`Edit.conf`, `Systems.conf`, `GameMaster.conf`). Undeclared
path collisions, duplicate resource GUIDs and Workbench-only scripts fail the
assembly. Build, tests and release all use it.

With PowerShell 7:

- Portable checks: `./tests/Test-Tools.ps1`
- Native build and local install: `./build.ps1 -NonInteractive`
- Workbench editing: `./tools/Assemble-Pack.ps1 -Destination <empty folder>`,
  then copy intended changes back into the module folder.

See [architecture](docs/ARCHITECTURE.md), [testing](docs/TESTING.md),
[artwork](docs/ASSETS.md) and [module licenses](docs/licenses).

## License

Repository tooling and Garrison: Arma Public License Share Alike (APL-SA).
Module source keeps its original terms (APL-SA, APL, MIT), and third-party
audio and assets keep their notices in the module `Credits`, `Licenses` and
`NOTICE.md` files. Because Ambient Sounds content is internal-use only, the
Workshop item uses a Custom license: `INTERNAL USE ONLY - DO NOT RE-DISTRIBUTE OR RE-UPLOAD`.

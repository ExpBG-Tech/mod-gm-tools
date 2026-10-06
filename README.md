# EXPBG GM Tools

One Arma Reforger Game Master modpack by M.Pac and K.Edgar. It bundles its
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
| No Game Master Budget | `addon/no-gm-budget` | Rebuilt here; same behaviour as Disable Game Master Budgets by ceo_of_bacon (no code reused) |
| Unit Scripts | `addon/unit-scripts` | Developed here |
| Unit Dialog | `addon/unit-dialog` | Developed here |
| Advanced Briefing Map | `addon/advanced-briefing-map` | Developed here |
| AI Surrender | `addon/ai-surrender` | Developed here |
| AI Global Skills | `addon/ai-global-skills` | Developed here |
| Ambient Unrest | `addon/ambient-unrest` | Developed here |

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

The pack checks the loaded addon list once per mission start for these items:
GM Optimizer `F3B7C6FB18AB1F79` and its CDF companion `8C5A6D9E73B241F0`,
Intel Items `E110000000000001` and CDF `E110000000000002`, Ambient Civilians
`A9C45E82D6710B3F`, Ambient Sounds `A93E9F6271894A3C`, Ambient Destruction
`E2A47D19C8B6503F` and CDF `D7A82F4139C60BE5`, and Persistent Battlefield
`6A32DB878B264D05`. When any is loaded, the server and every client log one
error line, `[EXPBG GM TOOLS] Conflicting standalone mods loaded: <names>`, and
each Game Master sees a persistent hint (also written to chat) the first time
the full editor opens: disable the old standalone EXPBG mods, GM Tools already
contains them (EXPBG CDF Compat replaces the old CDF companions). If the
duplicate scripts stop the game from compiling, the engine's script error
appears instead and this warning cannot run.

The only dependency is the base game (`58D0FB3206B6F859`).

## Modules

- **Garrison**: right-click a building, choose **EXPBG Add Garrison** and select an
  infantry squad. One squad takes reachable fixed guard posts and patrols
  indoors: window posts first, then just inside the outer doors, spread over
  every floor the stairs reach and at least 1.5 m apart, never on porches or
  entrance steps. A freshly spawned squad larger than the safe capacity is trimmed;
  existing squads and casualties are never refilled. Add Garrison again on the
  same building, as often as needed, to add squads: each deploys in full as its
  own garrison on free posts first, then elsewhere in and around the building.
  Caching offers Off,
  Simulation and Full (default) with per-garrison wake/sleep distances. Full
  recreates survivors at their captured world transforms with their posts in the
  original group, using prefab-default kits and health. Force Move releases the
  garrison. Use Unit Caching **Prepare for Save** before saving; assignments are
  mission-only.
- **Unit Caching**: AI Cache Zones and the EXPBG UNIT CACHING CONTROLLER.
  Simulation pauses existing AI; Full removes supported groups and restores only
  survivors. With Group cleanup on, each AI casualty's body (with everything it
  carries) and its dropped weapon are deleted together once it reaches the Minimum corpse age and no player has come within
  the wake radius of the zone, the squad or the remains for the clear delay, also
  while the rest of its squad is awake, Simulation-cached or Full-cached.
  Survivors are never deleted or refilled. A group caches once no player
  character (a Game Master's own character included, not the free GM camera)
  is within the sleep radius for the clear delay; locally hosted and
  single-player GM missions behave like a server. The zone status (selected
  module on the GM map) and a notice after saving the zone say why groups are
  not enrolled or not caching.
- **Intel Items**: placeable intel with GM-authored title and text, plus server
  racks whose intel players download onto an EXPBG USB Drive (timed, 3 m range,
  readable by hovering the drive). Rack and drive text are not yet saved by CDF.
- **Ambient Civilians**: civilian population module and exclusion zones.
- **Ambient Destruction**: permanent building damage, rubble and road wrecks.
- **Ambient Sounds**: war ambience, radios, crowds and emergency-alert TVs, plus
  28 placeable EXPBG Sound modules from Vinny - Sounds (radio chatter, Hanoi
  Hannah, firefights, shelling, jets, drone, market, prayer, traffic and sound
  effects); new modules start OFF.
- **Persistent Battlefield**: body/wreck lifetime rules, reconnect retention
  and spawn weapon safety. It overrides the vanilla `Character_Base` prefab and
  systems config. Bodies owned by Unit Caching cleanup are removed by that
  cleanup; Persistent Battlefield lifetimes apply to other bodies and to remains
  Unit Caching hands back when a native delete is refused. Bodies stay while a
  player is within 80 m. The `Character_Base` override also carries the Unit
  Dialog "Speak" action.
- **No Game Master Budget**: Game Settings switch "EXPBG Enable Game Master Budgets"
  (ON by default). OFF lifts the Game Master placement budgets for props, AI,
  vehicles, waypoints and systems and raises their displayed limits; switching
  back ON restores them at once. Campaign building budgets are unchanged.
- **Unit Scripts**: right-click AI soldiers or squads for EXPBG Hold Position,
  Freeze or Release Unit Scripts; the EXPBG Unit Scripts tab also offers ambient
  animations, and "EXPBG Night discipline" in a group's Group tab sets Light
  discipline or Terror tactics. Scripts end when the unit is hurt or possessed;
  mission-only.
- **Unit Dialog**: GM-authored speaker name and up to ten dialog lines on AI
  units; players read them with "Speak to <Name>" (Continue, Restart, End).
- **Advanced Briefing Map**: a briefing board that shows one player's map view,
  markers and drawn lines live to everyone nearby while they brief.
- **AI Surrender**: place "EXPBG AI Surrender" from Systems; broken AI squads
  may surrender (weapons dropped, sitting), and players interrogate prisoners
  ("Interrogate" on the prisoner's face, clear of medical actions) for a nearby
  squad's position or identity intel.
- **AI Global Skills**: place "EXPBG AI Global Skills" from Systems for per-faction
  AI skill and aim with Rifleman, MG/LMG, Marksman and Leader overrides (modded
  factions detected at runtime), rules of engagement (Return Fire Only, Fire on
  Sight, Warning Shots First; per group with "EXPBG ROE" in the group's Group
  tab) and AI ammunition (unlimited or N refills). Everything starts on vanilla.
- **Ambient Unrest**: "EXPBG Civil Protest Zone", a static crowd of 10-15
  unarmed protesting civilians with crowd audio from Ambient Sounds; the "Crowd
  sound" setting picks Angry crowd, Rioting crowd or Alternate (the default,
  switching between the two on every loop). Protesters turn to face a player
  they can see within 40 m. Crowd and sound exist only while a player character
  is within the "Wake distance" (default 300 m; Game Masters count by their
  character); farther away the zone sleeps and brings the same civilians back
  when a player returns.

## CDF Game Master Save

The standalone CDF companions depend on the standalone mods and are not
compatible with the pack. Use [EXPBG CDF Compat](https://reforger.armaplatform.com/workshop/07BC942D90324CD9)
([source](https://github.com/ExpBG-Tech/mod-cdf-compat), `07BC942D90324CD9`)
together with CDF Game Master Save instead. The guards in Unit Caching and
Garrison check for that identity: without it, Full caching refuses new removals
while CDF is loaded and Garrison caching stays off. With it, Unit Caching Full
snapshots, Intel Items and Ambient Destruction are saved in CDF files. A
garrison set to Full caches in Simulation while CDF is loaded (status
"Simulation cached (CDF loaded)"): its soldiers stay on their posts with AI
paused, because Full survivors cannot survive a CDF load. Simulation caching and
restoration always work.

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

Vinny - Sounds by Vinuesa (Workshop 61D358A07E15C5FE, APL-SA)

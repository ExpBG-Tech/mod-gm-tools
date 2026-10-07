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
| Advanced Briefing Map | `addon/advanced-briefing-map` | Developed here; projector screen model from Structures For GM byHeine (APL-SA) |
| AI Surrender | `addon/ai-surrender` | Developed here |
| AI Global Skills | `addon/ai-global-skills` | Developed here |
| Ambient Unrest | `addon/ambient-unrest` | Developed here |
| Time and Weather | `addon/time-weather` | Developed here |

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
  infantry squad. On a building not analysed yet the squad picker opens by
  itself once the structure analysis is done; until then a hint shows its
  progress, and EXPBG Add Garrison on the same building again stops waiting.
  One squad takes reachable fixed guard posts and patrols
  indoors: window posts first, then 2-4 m inside the outer doors, spread over
  every floor the stairs reach and at least 1.5 m apart, never on porches or
  entrance steps and never in a door's swing or doorway, so every door still
  opens. Soldiers beyond the posts patrol inside: they walk between free
  indoor stops, watch a hallway or door for 10-30 s at each and never bunch up;
  when a firefight starts each takes the nearest free window (or watches a door
  or stairs nearby) and returns to patrol a minute after it calms down.
  The whole squad always deploys and nobody is removed: soldiers beyond the
  building's posts follow the same order as an added squad (free posts, then
  patrolling inside, then other watch positions, then close around the
  building, last where they spawned); casualties are never refilled. Add Garrison again on the
  same building, as often as needed, to add squads: each deploys in full as its
  own garrison on free posts first, then patrolling inside while there is room,
  then on other watch positions inside, then close around the building.
  Caching offers Off,
  Simulation and Full (default) with per-garrison wake/sleep distances. Full
  recreates survivors at their captured world transforms with their posts in the
  squad, using prefab-default kits and health (the squad itself is recreated:
  while Full cached a garrison has no squad in the world). Force Move releases
  the garrison. Garrisons are saved with the mission: native saves and, with
  EXPBG CDF Compat 0.1.6 or later, CDF saves keep every garrison (building,
  posts and patrol stops, cache state, settings, casualties, soldier and squad
  overrides) and loading restores them in place, Full ones still cached;
  loadouts and wounds restore as prefab defaults. "EXPBG Release All Garrisons"
  (right-click a garrison squad) returns every garrison to normal AI, for a
  mission that will be saved without EXPBG GM Tools.
- **Unit Caching**: AI Cache Zones and the UNIT CACHING CONTROLLER.
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
  racks whose intel players download onto a USB Drive (timed, 3 m range,
  readable by hovering the drive). Rack and drive text are not yet saved by CDF.
- **Ambient Civilians**: civilian population module and exclusion zones.
- **Ambient Destruction**: permanent building damage, rubble and road wrecks.
  Each zone's "Vehicle types" setting picks its wrecks: Civilian (cars, vans,
  buses, civilian trucks), Military (armour, military trucks and jeeps) or Both
  (the default, as before). Changing it regenerates that zone's scenery and is
  saved with the zone.
- **Ambient Sounds**: war ambience, crowds, emergency-alert TVs and three
  radios: Radio Black (civilian), Radio AN/GRC-160 and Radio R123M (military).
  A radio's Recording offers radio chatter and broadcasts only (static, Apache,
  US battlefield, Russian, Chinese and Arab chatter, Hanoi Hannah, or Random per
  language). Plus 28 placeable "Sound: ..." modules from Vinny - Sounds (radio
  chatter, Hanoi Hannah, firefights, shelling, jets, drone, market, prayer,
  traffic and sound effects). New modules start OFF.
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
- **Advanced Briefing Map**: "Briefing Projector Screen" (Systems), a projector
  screen (model from Structures For GM byHeine) that shows one player's map
  view, markers and drawn lines live to everyone nearby while they brief
  ("EXPBG: Brief on map"). The map is drawn by the game's own map renderer
  (roads, buildings, names, contours, grid) on one screen per player while that
  player's own map is closed, otherwise from the world map image. The picture
  is on the side the roller case sticks out to. The older wall-map "Briefing
  Board" stays for existing saves but is no longer in the Game Master list.
- **AI Surrender**: place "AI Surrender" from Systems; broken AI squads
  may surrender (weapons dropped, sitting), and players interrogate prisoners
  ("Interrogate" on the prisoner's face, clear of medical actions) for a nearby
  squad's position or identity intel. "Interrogation: reveal intel items (%)"
  (default 30) is rolled once with his first answer: he also points out up to
  three unclaimed EXPBG Intel Items within the reveal search radius (loose, in a
  crate or vehicle, or on a body; not carried by a player), each marked on the
  interrogator's map. "Commander: grenade suicide instead of
  surrender (%)" (default 0, off) gives a breaking squad's leader that chance to
  kill himself with a fragmentation grenade at his feet instead of surrendering.
  The blast also hurts anyone nearby; the rest of the squad surrenders as usual.
  "Commander must carry a grenade" (default ON) uses one of his own grenades,
  and a leader without one surrenders; OFF gives him a vanilla M67 (US) or
  RGD-5 (other sides). Per squad and per soldier: the "EXPBG Surrender & Intel"
  tab of an AI squad's or AI soldier's Edit properties sets the surrender chance
  and the reveal squad, identity and reveal intel items chances (0-100%, default
  "Use module setting"). The soldier's value wins over his squad's, the squad's
  over the module; a set surrender chance is exact (no random factor). The
  module's casualty threshold still decides when a squad breaks. A prisoner
  keeps the interrogation values his squad had when he surrendered; his own can
  be changed while he is a prisoner. The values survive caching and saves.
- **AI Global Skills**: place "AI Global Skills" from Systems for per-faction
  AI skill and aim with Rifleman, MG/LMG, Marksman and Leader overrides (modded
  factions detected at runtime), rules of engagement (Return Fire Only, Fire on
  Sight, Warning Shots First; per squad and per soldier in the "EXPBG Rules of
  Engagement" tab of their Edit properties, soldier over squad over module) and
  AI ammunition (unlimited or N refills). Everything starts on vanilla.
- **Ambient Unrest**: "Civil Protest Zone", a static crowd of 10-15
  unarmed protesting civilians with crowd audio from Ambient Sounds; the "Crowd
  sound" setting picks Angry crowd, Rioting crowd or Alternate (the default,
  switching between the two on every loop). Protesters turn to face a player
  they can see within 40 m. Crowd and sound exist only while a player character
  is within the "Wake distance" (default 300 m; Game Masters count by their
  character); farther away the zone sleeps and brings the same civilians back
  when a player returns.
- **Time and Weather**: two Systems modules.
  - "Weather Transition" blends the weather to a target (preset buttons like
    Scenario Properties, plus optional rain, fog, wind speed and direction) over
    1-120 real minutes (default 10) instead of switching at once. Changing any
    of its settings and pressing Save starts the transition from the current
    weather; a new one takes over smoothly from the values reached. Clouds use
    the game's own weather blend; rain, fog and wind move every half second.
    Afterwards the weather holds (default) or runs automatically again. The
    Action setting offers Start again, Stop here and hold, and Return to
    automatic weather. A read-only progress row shows the status.
  - While a Weather Transition module exists, a weather picked in Scenario
    Properties also blends over its transition time ("Smooth Scenario
    Properties weather", ON by default). Changing the weather or wind in
    Scenario Properties during a transition stops it, or stops it moving the
    wind.
  - "Time Skip": choose "Skip time now" and every screen (players, and Game
    Masters unless switched off) fades to black, shows "6 hours later" (editable
    text with {hours}, {minutes}, {time} and {date}) and the new time, the clock
    moves forward with the date rolling over, and the screens fade back in.
    Defaults: 6 h, fades 2/3/2 s. One skip at a time; a running weather
    transition completes under the black screen.
  - Module settings are saved with native mission saves. A running transition
    is not: after loading, the weather reached so far stays.

## CDF Game Master Save

The standalone CDF companions depend on the standalone mods and are not
compatible with the pack. Use [EXPBG CDF Compat](https://reforger.armaplatform.com/workshop/07BC942D90324CD9)
([source](https://github.com/ExpBG-Tech/mod-cdf-compat), `07BC942D90324CD9`)
together with CDF Game Master Save instead. The guards in Unit Caching and
Garrison check for that identity: without it, Full caching refuses new removals
while CDF is loaded and Garrison caching stays off. With it, Unit Caching Full
snapshots, Intel Items and Ambient Destruction are saved in CDF files, and with
EXPBG CDF Compat 0.1.6 or later garrisons too: the CDF file carries the garrison
ledger, Full caching works under CDF, and a load with clearBeforeLoad replaces
the scene's garrisons with the saved ones (no duplicates; an append load of a
save with garrisons is refused). With an older EXPBG CDF Compat a garrison set
to Full caches in Simulation while CDF is loaded (status "Simulation cached (CDF
loaded)"), CDF saves are refused while a garrison is active and Unit Caching
Prepare for Save releases garrisons, as in 0.1.8. Native saves always keep the
garrisons.

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

Projector Screen model from Structures For GM byHeine by Heine.CRV (Workshop 628EDA2ABC937159, APL-SA)

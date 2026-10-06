# EXPBG GM Tools changelog

## 0.1.9

- AI Surrender: a prisoner who gave away squads could answer "He insists there
  is nobody else out here." although a squad of his side stood a few hundred
  metres away (seen in Morton with a large mod set). The search walked every
  moving object in the radius (props, wrecks, vehicles, dropped weapons and the
  gear every soldier and civilian carries) and gave up after 2048 of them, in
  no particular order, so in a busy town it could stop before reaching the
  squad. It now reads the squads directly from the AI, once per answer, so the
  nearest living squad of his side (or of a friendly military side) within the
  radius is always found however crowded the area; civilians of a friendly
  side never count, and his own remnants are still named only when nothing
  else is near. Squads that Unit Caching or Garrison holds in Full cache have
  no soldiers in the world while they sleep and are still not revealed.
- Garrison: EXPBG Add Garrison on a building that has not been analysed yet
  no longer opens the squad picker straight away, only to refuse the squad
  choice ("Structure analysis is still running"; about 90 s on a large
  two-storey house). The picker now opens by itself once the structure
  analysis is done; until then a hint (and a chat line) shows "Analysing
  building structure... NN%" with the real progress. EXPBG Add Garrison on the
  same building again stops waiting; the analysis keeps running, so asking
  again later continues from there. A failed analysis says why. A second
  request or a second Game Master on the same building joins the running
  analysis instead of starting another; while one Game Master is choosing a
  squad for the building, the next request waits (it was refused before) and
  its picker opens when the first is done, oldest request first. If the
  analysis finishes while the Game Master is placing something or another
  window or menu is open, the picker stays closed and EXPBG Add Garrison opens
  it at once. Analysed buildings open the picker at once, as before. Buildings
  already analysed or garrisoned no longer take turns with a new analysis:
  each one stretched it (the likely cause of the 90 s; a native fixture
  analyses a similar house in about 10 s). The analysis results are unchanged;
  a door destroyed while its building is analysed is now skipped instead of
  stopping the analysis with a script error.
  Results stay cached per building, not per building type: they depend on the
  terrain, neighbouring objects, door states and damage around each house.

## 0.1.8

- Ambient Destruction: road wrecks no longer stand in a line on the road's
  centreline, all facing the road direction. Each wreck now gets a random spot
  across the carriageway or on either shoulder (its turned footprint is kept
  on the road plus a 2.5 m shoulder where it fits), a heading 8-35 degrees
  off the road, about one in six lies across the road, and wrecks on the left
  side mostly face the other way. About three in ten pile up just ahead of or
  behind an earlier wreck; the others keep a random gap, so spacing is
  irregular. The existing ground, water, building and clearance checks still
  apply. Layouts still follow the zone's seed, and layouts saved through CDF
  Game Master Save (and sent to joining clients) keep their exact wreck
  positions; only newly generated layouts change.
- Garrison: with CDF Game Master Save loaded, garrisons set to Full (the
  default) were never cached ("Full cache held: CDF saves cannot keep Garrison
  Full survivors") and the Unit Caching zone listed them as "held by another
  EXPBG module". They now cache in Simulation while CDF is loaded: the same
  soldiers stay on their posts with AI paused and wake where they slept, with
  the garrison's own wake and sleep distances. The status (and one server log
  line per sleep) reads "Simulation cached (CDF loaded)"; the Full choice is
  kept and works again without CDF. Full survivors cannot survive a CDF load,
  so Full stays off under CDF.
- Garrison: posts are spread over the whole house. Before, a garrison bunched
  up in the ground-floor hall by the front door, some soldiers stood outside
  on the entrance steps, and none went upstairs. Stair treads and ramps are
  now sampled and linked (up to 45 degrees), so upper floors the stairs reach
  can get posts. Soldiers take window posts first (one per window, nearest to
  it), then posts just inside the outer doors (never on the outer step;
  interior doors are no longer watched), then the rest; the floors take
  turns and each post goes to the part of the floor farthest from the posts
  already taken. Posts keep 1.5 m apart on a floor (planned posts were 1.2 m
  apart), also for squads added later, so a small building may hold fewer
  posts and a fresh squad may be trimmed further. In a house (a building
  mostly walled in) a post needs walls on six of eight sides, so porches,
  entrance steps and ground under the eaves are not used; open sheds and
  barns keep their roofed floor. Only soldiers beyond the building's room
  stand around it, as before.
- Unit Caching: the zone status and notice now say which nearby squads a
  garrison caches itself, with their state ("Cached by EXPBG Garrison itself,
  with its own wake and sleep distances: 4 groups Simulation cached (CDF
  loaded)"), instead of counting them as not enrolled.
- AI Surrender: "Interrogate" now sits on the prisoner's face instead of his
  chest, so ACE Medical's actions on a bleeding prisoner no longer overlap or
  hide it. Look at his face (from the front, within 3 m) to interrogate; his
  torso, arms and legs keep the medical actions. The prompt follows his head
  whether he sits (vanilla) or stands with raised hands (ACE), and when he is
  moved or carried. Its target is also smaller (0.15 m radius instead of
  0.45 m), so it no longer covers his chest.

## 0.1.7

0.1.6 was prepared but never uploaded: the Workshop refused its 231-character
summary (limit 200). 0.1.7 ships the same code with a shorter summary and a
portable check for the limit.

- Compatibility: GM Tools now warns when the old standalone EXPBG mods are
  loaded next to it (GM Optimizer, Intel Items, Ambient Civilians, Ambient
  Sounds, Ambient Destruction, Persistent Battlefield and their CDF companions).
  One check of the loaded addon list at mission start; the server and every
  client log `[EXPBG GM TOOLS] Conflicting standalone mods loaded: <names>` as
  an error, and each Game Master gets a persistent hint plus a chat line the
  first time the full editor opens. GM Tools already contains those mods;
  disable them (EXPBG CDF Compat replaces the old CDF companions). If their
  duplicate scripts stop the game from compiling, only the engine's script
  error appears.
- AI Surrender / AI Global Skills: fixed the script error "No suppression
  volume provided!" (vanilla SuppressBehavior.bt), seen in a GM session seconds
  after AI Surrender took a soldier. Any vanilla suppress tree running without
  its suppression volume now ends quietly with a log warning naming the
  soldier's current action (at most ten per game run) instead of a script
  exception. A surrendering soldier also drops every suppress behaviour before
  his AI is switched off, and again whenever upkeep finds his AI back on.
- AI Global Skills: warning shots could make the vanilla suppress code compute
  an invalid aim height (NaN) at or beyond the edge of their 1 m aim sphere,
  which asserts on diagnostic servers. Vanilla sweeps every suppression line at
  least 2 degrees sideways, often past the sphere's edge, so this happened at
  any range and on every burst fired from more than about 30 m; fixed. Aim
  points inside the sphere are unchanged. The burst is also aimed further to
  the side of the target at range (at least 1 m + 2 degrees of the distance
  + 2 m, so about 4.7 m at 50 m and 13.5 m at 300 m), so that sweep never
  carries a warning round onto the target.
- AI Global Skills: the four "EXPBG ROE" right-click entries are removed; with
  ACE, GME and loadout-editor entries the soldier context menu grew taller than
  the screen. A group's rules of engagement are now set with "EXPBG ROE" in the
  group's Group tab, next to Set combat mode (Module default, Return Fire Only,
  Fire on Sight, Warning Shots First, Exempt), shown while an AI Global Skills
  module exists. Saved overrides are unchanged; the server logs each group's
  override once per change.
- Unit Scripts: EXPBG Hold Position and EXPBG Freeze are no longer listed for a
  soldier who already runs that script.
- Unit Scripts: the EXPBG Light Discipline and EXPBG Terror Tactics right-click
  entries are removed to shorten the context menu. Night discipline is now set
  with "EXPBG Night discipline" in the group's Group tab (None, Light
  discipline, Terror tactics), replacing the former entry in the EXPBG Unit
  Scripts tab, which keeps the unit scripts and animations. Hold Position,
  Freeze and Release stay on right-click; Release on a group still ends night
  discipline. The server logs each change once; still mission-only.
- Unit Scripts: a squad that a Unit Caching zone already manages now stays awake
  while any member runs Hold Position, Freeze or an animation, or while the squad
  has night discipline (zone status "Held awake by EXPBG Unit Scripts"). Before,
  Full caching could delete those soldiers and silently drop their scripts.
  Normal caching resumes after Release.
- Ambient Sounds: debug rings and the sound legend also hide while a GM context
  menu (right-click actions or waypoint commands) is open and return after it
  closes.
- Ambient Sounds / Ambient Unrest: new "Rioting crowd" recording (Mixkit,
  prepared as a seamless mono loop) in the EXPBG Ambient Crowd Sound recording
  list and in Random crowd. The EXPBG Civil Protest Zone has a new "Crowd sound"
  setting: Angry crowd, Rioting crowd or Alternate (switches between the two on
  every loop). New zones and zones from older saves use Alternate.
- Ambient Unrest: the EXPBG Civil Protest Zone caches itself like Full caching.
  New "Wake distance (m)" setting (50-3000, default 300): while no player
  character is within it (Game Masters count by their character only), an On
  zone sleeps; once every player is 50 m beyond it for 10 seconds, the crowd,
  its group and its sound are removed a few entities at a time. A returning
  player brings the same civilians back to their spots; casualties are not
  replaced. The zone stays On; the new read-only "Status" line and the debug
  log show Sleeping. Protesters also turn to face a player they can see within
  40 m (random among several, kept 8-15 seconds) and turn back to the protest
  direction when nobody is in sight. Older saves load with 300 m.
- Unit Dialog: the talking gesture now plays once, when a player starts the
  conversation; Continue and Restart no longer repeat it. Ending the
  conversation and speaking to the unit again plays it again.
- Garrison: EXPBG Add Garrison works again on a building that already has a
  garrison, as often as you like (0.1.5 refused with "This building already has
  a garrison"). Every added squad deploys in full as its own garrison: first on
  building posts no other garrison holds, then on other verified positions in
  the building, then on standing places around it, facing outward. Existing
  garrisons are untouched; each garrison keeps its own cache settings and
  sleeps, wakes and is released by Force Move on its own. The server logs one
  `[EXPG Garrison] group=... added to building ...: placed N of N soldiers`
  line per add, with the count per kind of position. An added squad spawns on
  open ground beside the building, so it never pushes the existing guards.
- Unit Caching: a cache zone now says why it enrolls or caches nothing. Its
  status (selected module on the GM map, debug panel, `[EBG DEBUG ZONE]` log)
  names each refusal of a group inside the affected radius (held by Unit
  Scripts, Garrison or ambient crowds, marked Exclude, civilian faction with
  Soldiers only, containing a player, still spawning, managed by another zone),
  counts AI Surrender prisoners and other soldiers who left their squad (never
  cached), reports how many groups a player character keeps awake inside the
  sleep radius (a Game Master's own character counts, the free GM camera does
  not; the notice and debug panel also give its distance), why Full cache
  cannot run in this session (for example no GameMasterSystems systems config
  in Workbench play), and old
  standalone EXPBG mods loaded next to GM Tools. A zone with no AI nearby says
  so. The Game Master who saves a zone or uses a global zone switch gets the
  same explanation once as a hint and chat line (`[EBG ZONE NOTICE]` in the
  server log). Locally hosted and single-player GM missions cache exactly like
  a server; caching itself is unchanged.
- Intel Items: the EXPBG server racks and the EXPBG USB Drive use real models
  (Heine's server rack and hard drive, APL-SA, credited in the Licenses folder)
  instead of a vanilla electrical cabinet and debris. Server Rack A/B, the USB
  Drive and the new modules (AI Global Skills, Briefing Board, Civil Protest
  Zone, AI Surrender) now show up when searching the Game Master entity browser.
- Intel Items / AI Surrender: while the intel reader or the interrogation window
  is open, the interaction prompt is hidden and no action behind it can be used;
  both return when the window closes.
- AI Surrender: with ACE loaded (optional, never a dependency), prisoners take
  ACE Captives' own surrender state (hands up), so ACE's Take prisoner, Escort
  and Release work on them. Without ACE they sit down as before.
- AI Surrender / Unit Caching: a prisoner leaves his squad's cache record at
  once; he is never cached, respawned or deleted by Unit Caching.
- Unit Dialog: a unit's speaker name and dialog lines survive Full caching
  (Unit Caching and Garrison), so the restored soldier still offers Speak to.
  Session memory only; CDF Full snapshots do not hold it.
- Ambient Civilians: indoor residents are placed again. The spawner now finds
  the ground-storey floor (houses stand on raised slabs) instead of testing
  0.1 m above the terrain, which never fit in 0.1.5. A home that keeps failing
  indoors is filled outdoors, and an Indoor spawn distance of 0 makes every home
  outdoor. Ruined houses are skipped. Civilians and their groups are kept out of
  native saves (0.1.5 restored five leftover civilians at every server start).
- Unit Caching: casualty cleanup no longer stalls on RHS: Status Quo gear. RHS
  preset vests, weapon parts and the Dovetail mount RHS adds to the vanilla
  AK-74N are left out of the save with the body instead of raising a save Issue
  and holding the group; headgear accessories in clothing slots (Comtac on a
  boonie hat) are removed with their wearer.
- AI Global Skills: AI ammunition refill works for weapons without a magazine
  template (RHS M40A5); they are refilled with the magazine they carry.
- Unit Scripts: Light Discipline switches on worn RHS helmet night vision and
  switches it off again on release or Terror Tactics; gear the soldier already
  had on stays untouched.
- Tooling: `tools/Invoke-ReleaseHousekeeping.ps1` (run by `release.ps1`) moves
  superseded build, gameplay and release folders to the configured archive;
  it deletes only with an explicit `-Purge`.

## 0.1.5

New modules:
- Unit Scripts: right-click AI soldiers or squads for EXPBG Hold Position,
  EXPBG Freeze (static, head follows nearby players) and EXPBG Release Unit
  Scripts; an EXPBG Unit Scripts attribute tab adds seven vanilla ambient
  animations. Squads get EXPBG Night Discipline: Light Discipline (flashlights
  off, worn night vision on) or Terror Tactics (flashlights on, facing nearby
  players). Scripts end when the unit is hurt or a player takes control.
  Mission-only; scripted squads are not enrolled by Unit Caching.
- Advanced Briefing Map: a placeable EXPBG briefing board. One player at a time
  uses "EXPBG: Brief on map"; their map opens and the board shows the world map
  with their view, markers and drawn lines live for everyone nearby. Closing the
  map, walking away or disconnecting frees the board.
- Ambient Unrest: "EXPBG Civil Protest Zone" gathers 10-15 unarmed civilians in
  one group inside the zone; they stand still and protest with vanilla raised-
  hand, pointing and arm-sweep gestures while an angry-crowd loop from Ambient
  Sounds plays. Off, delete and world end remove the crowd; saves keep only the
  zone settings.
- Unit Dialog: Game Masters give an AI unit a speaker name and up to ten dialog
  lines in its "EXPBG Unit Dialog" attributes; players get "Speak to <Name>",
  which opens a conversation window (Continue, then Restart or End
  conversation) with an optional short gesture. The name defaults to the unit's
  own identity name; dialog reaches join-in-progress players and is kept in
  native mission saves.
- AI Surrender: "EXPBG AI Surrender" system module. AI squads that lose a set
  share of their members may surrender: prisoners drop their weapons, leave their
  squad, turn civilian and sit down (vanilla has no hands-up animation). Players
  "Interrogate" them for one nearby squad's position (a removable map marker for
  their faction) or the prisoner's name, bio and squad leader.
- AI Global Skills: "EXPBG AI Global Skills" system module. Per-faction AI skill
  and aim accuracy with Rifleman, MG/LMG, Marksman and Leader overrides (roles
  from the actual weapons; modded factions detected at runtime), default rules
  of engagement (Return Fire Only, Fire on Sight, Warning Shots First) with
  right-click "EXPBG ROE" per group, and AI ammunition (unlimited magazines or N
  refills, primary magazines only). Everything starts on vanilla.
- Intel Items: EXPBG server racks (A/B) with GM-set intel title, text and
  download time (5-900 s), and a placeable EXPBG USB Drive. A player carrying a
  drive uses "Download intel"; a progress bar shows to that player only; moving
  more than 3 m away, dying or pressing again stops it. The intel is stored on
  the drive, travels with it and shows when hovering the drive. Rack and drive
  text are not yet kept by CDF or native saves.

Fixes and changes:
- Unit Caching: the game's own autosave and saves work while cache zones are on,
  as long as no group is cached or being restored; after every Full-cached group
  has woken, normal saving is allowed again on its own.
- Unit Caching: every Game Master, including voted ones, sees cache zone icons,
  rings, map labels and Full-cached group icons; the testing monitor and group
  ids are for logged-in admins only.
- Unit Caching: groups waking at the same moment come back a few at a time (up
  to 12 soldiers at once, then about 4 per second); a single group still wakes
  immediately.
- Unit Caching: new controller action "Release blocked groups (escape)" for a
  Prepare for save that stays Blocked; soldiers already brought back are kept
  and never doubled. A zone stuck on "Restoring saved module settings" logs what
  it waits for after 10 s and resumes after 60 s if the game's save system never
  started.
- Ambient Civilians: deleting the last civilians module no longer leaves its
  traffic cars and drivers behind (removed out of sight and far from players,
  never a car a player used). A traffic slot is no longer lost when a driver
  dies and the game later cleans up the car. The debug summary sits below the
  Game Master compass.
- Ambient Sounds: debug rings and the sound legend hide while the entity
  browser, an attributes window or the pause menu is open.
- Persistent Battlefield: bodies stay while a player is within 80 m (was 50 m),
  as in the last standalone release.

## 0.1.4

Fixes from the first full client test on a dedicated server with the production
modset (ACE, RHS, CDF GameMaster Save, Game Master Enhanced and 110+ other mods).

- Unit Caching: casualty cleanup now simply deletes the body, with everything
  it carries, and its dropped weapon in the same tick. There are no per-item
  checks any more, so modded kit (entrenching tool, ACE Overheating weapons,
  character-mod heads) never blocks a body. Before, kit was removed piece by
  piece and a body that failed a check was left stripped. Items a player picked
  up or stored are never touched; bodies with an EBG keep component or valuable
  intel are still kept. ACE's overheating helper attachment and character-mod
  heads are accepted in Full cache snapshots. The zone status shows the native-save pause only while Full
  groups are actually absent, the admin cache monitor is hidden outside the GM
  editor, the GM entity list no longer throws when entities are deleted while
  paging, and a blocked Prepare for save names the group, its position and its
  zone.
- Garrison: "EXPBG Add Garrison" (renamed so it is distinct from Game Master
  Enhanced's "Add Garrison") now opens its squad picker on dedicated-server
  clients; the access check used a server-only editor lookup and always failed on
  clients. Every refusal now logs one `[EXPG GARRISON]` reason, and feedback also
  appears in chat for players with hints disabled. Window guards stand directly
  at the window (nearest window posts are chosen first).
- Intel Items: reading opens a dedicated window (title, scrollable text, Close /
  Escape) instead of a hint, so it works with hints disabled. One read per press
  (repeats within 1 s are ignored). `[br]` in the intel text makes a line break.
- Ambient Destruction: a CDF save of destroyed buildings loads after a server
  restart even when native mission persistence already removed the building;
  ambiguous matches are still refused. Building collapses are paced at least 3 s
  apart across all zones to avoid client freezes. Diagnostics is an Off /
  Lifecycle / Counts selector and sliders show whole numbers. Debug rings no
  longer draw huge wedges near the camera. Removed an invalid material remap
  from the Afghan truck wreck.
- Ambient Sounds: the crowd is now "EXPBG Ambient Crowd Sound". Radio, TV, crowd,
  war and sound modules share the same "On/Off" first and "Debug" last attribute
  order; saved settings are unaffected. All sound items use one speaker mini icon
  and the EXPBG badge. The debug legend moved to the right edge and is removed
  when no debug module remains and when the world ends. Failed-start retries for
  long-range sounds need at most 50 m of approach. Removed an unused sample.
- Ambient Civilians: traffic works again. A car's destination town had to be
  500-5000 m away and also inside the 400 m module radius, which no town can be;
  journeys may now leave the module radius (exclusion zones still apply), the car
  is freed from the radius once it departs and is removed out on the road, far
  from players. Neighbours visit each other's tables: the join range is 150 m, a
  nearby idle resident is called when a spot opens, and visitors walk back home.
  Deleting the module no longer leaves an empty civilian group. The debug summary
  sits at the top centre and the legend on the right, clear of the GM panels and
  the sound legend, and both are removed when the world ends. Civilian entities
  are excluded from CDF and vanilla saves with the editor's "not saved" flag
  instead of the obsolete `Serialize` override.
- No Game Master Budget: the switch is now "EXPBG Enable Game Master Budgets".
- Persistent Battlefield: weapon safety on spawn is applied only by the owning
  peer once the character is registered, removing the RPC errors on mission load
  and on clients.

## 0.1.3

- Ambient Sounds: 28 new placeable sounds under the EXPBG Sounds browser category,
  covering every sound from Vinny - Sounds by Vinuesa (Workshop 61D358A07E15C5FE,
  APL-SA): radio static, Apache, Russian, Chinese and Arab radio chatter, Hanoi
  Hannah, close and distant firefight, distant shelling, two jet flybys, a drone,
  market, market seller, street singing, Muslim prayer, traffic, church bell, car
  alarm, police car, Nokia ringtone, distant barking and distant sheep. They are
  labelled Radio transmissions, War sound effects, Crowd or Sound effects.
- Each sound is a new invisible EXPBG Ambient Sound module with Game Master
  On/Off, Recording, Volume, Audible distance (30-1500 m), Loop, Pause between
  repeats and Debug. New placements start Off at 50% volume; looping sounds
  default to Loop on, one-shots (jet flybys, drone, church bell, car alarm,
  police car, ringtone, barking) to Loop off. Settings are saved with the session.
- The sounds share the radio/crowd/TV playback limits (at most four of these
  voices at once, 32 active sources) and wake within 1000 m or their audible
  distance plus 50 m, whichever is larger. Radios, crowds, TVs and the war module
  are unchanged.
- Vinny - Sounds is not required and Game Master FX is not used. Thirteen
  recordings are included unchanged; the other fifteen reuse the radio, jet,
  firefight and shelling recordings Ambient Sounds already ships.

## 0.1.2

- New module: No Game Master Budget. A Game Settings switch, "Enable Game Master
  Budgets" (ON by default), lifts the Game Master placement budgets for props,
  AI, vehicles, waypoints and systems when OFF; campaign building budgets are
  unchanged. Same behaviour as the Disable Game Master Budgets mod, rebuilt from
  scratch (no code reused): toggling now applies and restores the budget limits
  immediately, and the switch is read from the replicated game mode so late
  joiners and new missions never act on a stale value.

- Unit Caching Group cleanup now works per casualty. A managed AI soldier's body
  and owned kit are deleted once that casualty reaches the Minimum corpse age and
  no player has come within the wake radius of the zone, the squad or the remains
  for the clear delay. The rest of the squad no longer has to die, and cleanup
  also runs while survivors are Simulation-cached or Full-cached. Survivors are
  never deleted or refilled; caching and restoration are unchanged. Bodies of an
  eliminated squad now go one by one as each reaches its own corpse age.
- A soldier killed by script while his group is Simulation-cached (hidden and
  untargetable) is not confirmed dead while the group sleeps; his body is kept
  and never deleted by the cleanup. Known limit; normal play cannot reach it.
- Remains that cannot be verified as safe to delete (unregistered or transferred
  contents, unapproved modded items, or a refused deletion) are retried at most
  three times, a minute apart, then handed back to the game's body cleanup
  (vanilla or Persistent Battlefield) with one `[EBG CLEANUP RELEASE]` log line.
  Remains protected by an EBG cleanup keep component or holding valuable intel
  stay protected as before (`[EBG CLEANUP KEEP]`), also when the same body or
  container holds other unverifiable items. Other casualties keep
  draining meanwhile.
- Debug group lines and zone markers show casualty cleanup progress for groups
  with survivors.

## 0.1.1

- First Workshop publication of the EXPBG GM Tools pack; content identical to
  0.1.0. The 0.1.0 publish attempt ended before any upload (the Workbench
  publishing window closed; the Workshop item did not exist afterwards), and
  that version stays retired under the release guard.

## 0.1.0

- EXPBG GM Tools becomes one modpack: Garrison, Unit Caching (formerly GM Optimizer
  0.1.31), Intel Items 0.0.4, Ambient Civilians 0.0.23, Ambient Destruction 0.0.9,
  Ambient Sounds 0.2.9 and Persistent Battlefield 1.0.5. Each module keeps its own
  folder; one assembled project, base game the only dependency.
- Do not combine with the standalone items or their CDF companions.
- Garrison: Full caching is the default and restores survivors at exact world
  transforms with their posts; casualties are never refilled. Freshly spawned
  squads are trimmed to the safe indoor capacity. Eight-neighbour interior
  planning, door-aware entrances and a native speed cap hold guards and blocked
  patrols in place.
- Unit Caching save guards now recognise the pack identity and a reserved pack
  CDF companion.

## 0.0.1

- Initial Garrison feature: one-squad building garrisons, reachable fixed posts,
  interior overflow patrol and independent activation policy using GM Optimizer.
- First-version caching is Off or Simulation with adjustable wake/sleep distances.
  Original actors are retained; Full caching and cold garrison save/load are unsupported.
- Native and multiplayer acceptance are pending; this is not a released version.

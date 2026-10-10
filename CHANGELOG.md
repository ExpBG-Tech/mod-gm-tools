# EXPBG GM Tools changelog

## 0.1.21

- Unit Caching: the Hold/Freeze script and the Unit Dialog of a Full-cached soldier are now written
  into the portable (CDF) Full snapshot ("eusScript", "eudDialog"), so a Full-cached squad keeps
  them through a CDF save, a server restart and a load. Snapshots without them still load.
- Unit Scripts: a pose entry keeps the soldier's AI out of its maximum LOD for 15 s (the same pin
  Ambient Civilians uses), so a soldier far from every player starts his animation instead of
  waiting for someone to come near.

## 0.1.20

- New Mission Intro module (Systems): every player, late joiners too, sees it once when he first
  spawns in a game session. The screen is black, the title and a "time | date | location" line fade
  in, then the view fades back in (Time Skip black screen). Title and location are set in its
  attributes; empty uses the mission name and the map grid of the spawn. On and the text duration
  are saved with CDF; the texts are session settings like the Time Skip text.
- Unit Caching: Hold Position and Freeze soldiers now Full cache. Their script (code, held spot and
  heading) rides the Full survivor carry and binds again on the respawned soldier, like Unit
  Dialog. Only squads with a Unit Scripts animation still fall back to Simulation in a Full zone.
- Unit Scripts: an animation the server does not start (production 2026-10-10: 14 poses far from
  every player after a CDF load) is no longer dropped after 4 attempts. The soldier keeps his spot
  and the pose is tried again every 30 s.
- Unit Caching: new "Never cache this squad" switch in the EXPBG Unit Caching tab of a squad's or
  soldier's Edit properties (saved with CDF). Squads in helicopters and planes are never cached;
  ground vehicles and static weapons keep Simulation caching.
- Global controller: zones paused by Prepare for save now count as disabled, and the action
  description explains Prepare for save (pause, AI restored, zones resume with Enable) versus
  Disable all zones.
- AI Surrender: a soldier with his own surrender chance rolls it once when he first comes under
  threat (danger or combat), whatever the size of his squad. A lone soldier at 100% now gives up
  when fired at; squad breaks still roll as before.
- Garrison: a squad added to a building whose spawn never reports complete within 45 s no longer
  stays outside as a normal squad; the soldiers that exist take their posts.
- Log noise: per-unit Unit Scripts lines, Prepare progress, AI Skills override lines, Garrison
  status lines and settings-hold warnings print only while a cache zone has Debug messages on.

## 0.1.19

- Unit Caching: a Full cache zone now falls back to Simulation for every squad Full cannot take,
  instead of leaving it awake:
  - squads whose soldiers run Unit Scripts (Hold, Freeze or an ambient animation): a Full cycle
    would respawn them without their script; in Simulation they keep the same actors, scripts and
    poses and wake as in a Simulation zone;
  - squads with a soldier in a static weapon seat: CDF cannot save a Full-cached gunner and
    refused the whole save ("Mounted Full survivors require restoration"); in Simulation the gunner
    stays seated and CDF saves him with his seat. Gunners Full cached by an earlier version wake
    once and sleep again in Simulation;
  - a squad whose Full attempt is refused tries Simulation at once (no cooldown) and tries Full
    again after it next wakes;
  - every squad of the zone while Full is unavailable in the session.
  Squads that neither mode can take stay awake with their reason. A Simulation-cached squad in a
  Full zone names why in its status; each refused Full attempt logs one `[EBG FULL FALLBACK]` line.
- Unit Caching: a soldier a player controlled once (Game Master "Take control") no longer keeps his
  squad out of caching forever. Once the player has left him, his squad enrolls again and caches
  in Simulation (Full still refuses him, and cleanup still never deletes his things). Only a
  soldier a player controls right now holds his squad awake.

## 0.1.18

- Ambient Civilians: every placed module now populates its own area. Until 0.1.17 a second module
  was treated as a duplicate and spawned nobody. Each module brings its own radius, civilian limit,
  neighbourhood limits, residents per house, theme and faction, while the first placed module runs
  the shared scheduler and its activity, traffic, sound and diagnostic settings apply mission-wide.
  The mission total is the sum of the module limits, at most 200; when modules ask for more, each
  gets a proportional share and the log reports the area as capped. A full area no longer holds the
  others back, and the console summary and GM overlay list each area's civilians and limit.
- Unit Scripts: Freeze and Hold now hold until the Game Master releases them; damage,
  unconsciousness and ragdoll no longer end them (corrections pause while the unit is down).
  Ambient animations loop inside the same animation without the gap that let units step and drift,
  and every restart begins on the held spot. Only entries that never start count as failed
  attempts, and a Unit Caching pause no longer uses them up. Moving a unit held in a compartment
  without an editable vehicle (for example ACE Captives helpers) no longer throws in the editor
  transform. Running unit scripts are saved with native mission saves and restored on the saved spot
  and heading; a versioned state API lets EXPBG CDF Compat do the same for CDF saves. Releases record
  a precise reason, and Game Master replies say scripts are saved (night discipline stays
  mission-only).
- Unit Caching: Simulation zones now cache squads whose soldiers run Unit Scripts (Hold, Freeze,
  ambient animations). They are paused and resumed in place with their script still bound; an
  animation that ended while paused is started again at the same spot. Night discipline and other
  modules' reservations still keep a squad awake. Full zones still refuse these squads (a Full cycle
  would respawn the soldiers without their scripts); the zone note and the hold reason say so. The
  hold reason "Unsupported vehicle, medical or movement state" is split to name the actual
  compartment, medical or movement state.
- AI Global Skills / AI Surrender: the squad and soldier settings now show for every AI squad and AI
  soldier of any faction or mod (vanilla, RHS and others), with or without a module; in 0.1.17 they
  stayed hidden until a module was placed. Squad values also show when you edit one of the squad's
  soldiers, like the vanilla Group tab. New "AI skill (squad)" and "AI skill (soldier)" (Novice to
  Expert) sit with the rules of engagement in the renamed "EXPBG AI Skill & ROE" tab; the soldier's
  value wins over the squad's, and the squad's over the module. A squad's or soldier's own skill and
  rules of engagement apply without a module too, and stay when the module is deleted. Skills are kept
  by native saves, CDF saves, Unit Caching and Garrison caching; earlier saves load unchanged.
  Surrender and interrogation chances read "Squad ..." and "Soldier ..."; they can be set before the
  AI Surrender module is placed, but surrenders still need the module. Both modules also add these
  settings in script, so another mod's attribute list override cannot hide them.
- Garrison: a patroller whose walk fails beside a standing guard no longer sleeps or wakes metres
  from his stop; he takes the nearest free stop where he stands (failed walk, forced settle,
  Simulation wake) or walks back to his stop on waking.

## 0.1.17

- New entity browser art: every placed sound (28), both server racks, the USB
  drive, the briefing board and projector, the AI Cache Zone and the Unit Caching
  controller have their own card and their own gold icon instead of a shared
  card and the generic speaker icon. No behaviour change; saves and missions
  are unaffected.
- tools/art/Import-PackArt.ps1 cooks any number of textures in one Workbench run
  and can write the results into another addon folder (-TargetRoot).

## 0.1.16

- The sounds of Ambient Sounds, Ambient Civilians and Intel Items moved to the
  new dependency EXPBG Audio Data (198987BE7BAC4C84), byte for byte at the same
  paths with the same resource IDs: everything plays exactly as in 0.1.15.
  Servers must load EXPBG Audio Data (0.1.3 or later) with GM Tools; clients get
  it as a Workshop dependency. Saves and missions are unaffected.
- The download of this item is about 590 MB smaller.
- Workshop listing: a short feature list, licence Arma Public License Share
  Alike (APL-SA). Credits stay in the addon's Credits and Licenses files.

## 0.1.15

Fixes from the 2026-10-07 op and a performance pass: less server and client
work per frame and per second, mainly on long sessions with many players and
AI. Saves from 0.1.14 load unchanged. Behaviour is unchanged except where a
bullet below says otherwise.

- Random Garrison: "Exclude support squads" (default on) let ammo teams and
  helicopter crews through, because only squads labelled medical, logistics or
  essential on the group were skipped. It now also skips a squad when every
  soldier is a medic, ammo bearer, vehicle or helicopter crewman or driver
  (vanilla ammo teams, FIA's medical section) and, for squads of other mods
  without such labels, when the prefab's file name says medic, ammo,
  ammunition, supply or supplies, logistic, crew or pilot (for example
  REAPER_USSR_HelicopterCrew). Transport and guard teams (labelled essential)
  are skipped as before; rifle squads, machine gun, AT, sniper, recon and
  special forces squads stay. Each squad is classified once when the faction's
  catalog is read; the server log line "squad catalog" now also counts the
  support squads. Saved settings are unchanged.
- Unit Scripts: Freeze now really keeps a soldier on his spot, squad leaders
  and lone soldiers included. He no longer drifts off while his squad has a
  waypoint or while he stands idle, and he no longer turns round to glance
  about; only his head follows a nearby player. If anything other than a Game
  Master move shifts him more than about a third of a metre, or turns him far
  away, he is put back. Hold position does the same once a soldier is pushed
  more than 1.5 m.
- Unit Scripts: moving a soldier who runs Hold, Freeze or an animation with
  the editor (dragging him, moving his squad, or the position field) sets his
  new spot. The server log says "moved by the Game Master" only for real
  editor moves. Other displacements show as "held on its spot", at most once
  per soldier every 5 minutes, instead of the misleading "re-anchored".
- Unit Scripts: "Sit on a chair" now needs room. A soldier standing at or in
  furniture (for example at a desk) is refused with "no room for 'Sit on a
  chair' here: <object> is in the way" and keeps the script he had. In the
  2026-10-07 op, one officer sitting in a table cut the server from 240 to 70
  FPS for an hour. If furniture is placed into a seated soldier later, or
  anything other than the Game Master pushes an animating soldier more than
  1.5 m, the animation ends at once instead of retrying four times with server
  stutters.
- Random Garrison: when a building is too small for the chosen squads or has
  no usable rooms, the zone now keeps trying other buildings, up to three
  times the number you asked for (and at least 16 more). Buildings of a type
  that just failed are tried last. Villages full of sheds and one-room houses
  (for example on Chernarus Minus) now get the number of buildings you set,
  and the same seed still gives the same buildings.
- Random Garrison: when a generation finishes or is stopped, the Game Master
  who started it gets a message with the buildings and squads placed. If fewer
  buildings than asked for were garrisoned, the message also says how many
  were tried and why the others were skipped.
- Random Garrison: if a Game Master stops another Game Master's generation,
  both of them get the Stopped message.
- Random Garrison: when a generation stops because the AI limit was reached,
  the zone now stays Stopped and shows that reason. Before, it immediately
  showed Done instead, which hid why fewer buildings were garrisoned.
- Random Garrison: the status now lists failed buildings by reason with a
  count for each, instead of only the first reason. It also says how many
  buildings took fewer squads than drawn.
- Random Garrison: pressing Generate while the zone is still working now shows
  how far it has got. It says to wait or use Stop, or only to wait while the
  zone is clearing.
- Time and Weather: the Weather Transition's clouds now really blend over the
  chosen time. Before, the sky did not move during a transition and jumped to
  the new weather at the end. Rain, fog and wind now move with the clouds, so
  everything arrives at the same moment and the weather stays there.
- Time and Weather: when the weather changed only a short while ago (for
  example a weather picked in Scenario Properties a few minutes earlier), the
  clouds start as soon as the game allows, instead of waiting another 10
  in-game minutes.
- Time and Weather: picking a target weather, in the Weather Transition module
  or in Scenario Properties while smoothing is on, no longer shows the new sky
  at once on the Game Master's screen and then jumps back on Save. The sky
  only changes by blending from where it is.
- Time and Weather: the game needs at least 10 in-game minutes to change the
  clouds (10 real minutes at the normal day length). Right after another
  weather change, it may first hold the current sky that long. A shorter
  transition then takes longer than set; the status says when the clouds
  start, and says so when the transition takes longer.
- Time and Weather: if another weather mod takes over the weather during a
  transition, the transition now leaves the clouds to that mod until the end.
  Rain, fog and wind still arrive, the other mod's weather is no longer frozen
  at the end, and the status says the clouds were left to another weather
  source.
- Ambient Sounds: placed radios keep playing their chatter when lots of other
  sounds are playing. When the game cut a radio short to make room for other
  sounds, the radio stayed silent for the rest of its recording (up to 10
  minutes). It now starts again after about 3 seconds. If it keeps being cut,
  it waits a little longer each time (up to 30 seconds), but never longer than
  it would have waited before. Radio and emergency-alert TV recordings now
  have a higher sound priority, so the game cuts other sounds before them, and
  being quiet no longer makes them the first to go.
- Ambient Sounds: radios, TVs, crowds and placed sounds no longer fail to
  start, or switch themselves off after three tries, when you are at the very
  edge of their audible distance. They now start once you are within 90% of it
  (27 m for a 30 m radio) and keep playing out to the full distance. If the
  game refuses a start, the source tries again later instead of staying quiet
  until it is placed again.
- Ambient Sounds: up to four radios, TVs, crowds or placed sounds near you
  still play at the same time. A radio that was cut or refused now comes back
  on its own.
- Ambient Sounds: the "[EAS] Radio start failed" log line now appears at most
  twice per placed source until its settings change. It shows the retry delay
  and whether the sound was too quiet to start, instead of "parked". With
  Debug on, the log also shows 'evicted', 'refused' and 'inaudible' events,
  and the summary line counts evictions.
- Unit Caching: when a mission ends, a last CDF save made during shutdown no
  longer logs the misleading "[EBG CDF HOLD] Optimizer loading or native
  restoration is in progress". If the mission has cache zones, that late save
  is still skipped, and the log now says "The mission is ending, changing or
  not running; capture skipped and the existing save kept". A mission without
  cache zones is no longer held back by Unit Caching at that moment.
- Unit Caching: removed an unused hook on every Game Master placement and
  every squad member spawn. GM Tools no longer shows up in the script stack of
  placement errors. Nothing read the flag it set, so gameplay is unchanged.
- Persistent Battlefield: reconnect log lines now show the disconnect cause by
  name, for example "cause=REPLICATION/SHUTDOWN (1/9)" instead of
  "cause=0x0x10009 {}". A refused reservation now says why: no controlled
  entity, character is dead, and so on.
- AI Global Skills: Return Fire Only and Warning Shots First squads now open
  fire only when actually fired upon, then return fire until the contact is
  over. That means a member hit or killed by an enemy, an enemy round within
  about 4 m of any member, or an enemy explosion within about 10 m. Before,
  the vanilla rule fired whenever an enemy shot passed within 13 m of the
  squad leader or a gunshot went off within 15 m of him, even at other
  targets. It also ignored hits on members other than the leader; any member
  hit now counts.
- AI Global Skills: setting vanilla Set combat mode on a squad with EXPBG
  rules of engagement switches it to Exempt (vanilla), so both tabs agree.
  After a Unit Caching Full wake, a native load or a CDF load, a squad keeps
  its own vanilla combat mode, and a woken squad takes its rules of engagement
  at once. Native saves and Full cache snapshots made before this version
  store the EXPBG mode as the squad's own. On such a squad, set Set combat
  mode once and then the EXPBG rules of engagement again.
- AI Global Skills: a soldier with his own Return Fire Only no longer joins
  his squad's suppressive fire.
- AI Global Skills: Warning Shots First also warns a player in a vehicle. If
  the spotter cannot fire, the next member who sees the target fires the
  warning. No warning is given once the squad is already being fired upon, and
  the switch to lethal and the re-arm are logged.
- AI Surrender: the surrender chance texts say that nothing happens until the
  squad has lost the casualty threshold (default 50%). A casualty taken while
  a squad's cache state is held is now rolled once the squad is awake.
- AI Surrender: a soldier whose death the game reports only after taking him
  out of his squad still counts as that squad's casualty (the death of a squad
  member could go uncounted, so the squad never rolled to surrender). With
  surrender diagnostics on, a casualty that cannot be matched to a squad is
  logged.
- Garrison: a guard pushed off his post no longer leaves a patrol claim within
  1.5 m of his new spot: nearby patrollers move on to another free stop, and
  posts off the building plan count for claim spacing. A patroller whose walk
  ends or fails beside a standing soldier moves on instead of dwelling pressed
  against him.
- Tests: the fixture runners delete their copy of the pack after each run
  (each kept about 0.8 GB).

Performance:

- Unit Caching (server, except where noted): the 500 ms possession safety net
  no longer re-checks every player against the whole cleanup ledger on every
  tick. A player is re-checked when his controlled character changes, after
  a cache record, member or ledger binding change, and otherwise in turn (one
  player per tick); taking control of a unit is still handled at once. The
  ledger's part of that check uses a per-member row index. When a tracked
  item changes inventory slot (reload, pickup, loot), and in the other
  per-group cleanup checks, the ledger visits only that group's rows through
  a per-group index instead of scanning every row. Player-history and saved
  release-id checks use hash lookups instead of list scans. The bayonet,
  cloth-blade and RHS rail samplers share one timer per kind (each item is
  still sampled about once a second) instead of one 1 s timer per item.
  Between the 500 ms ticks, wake protection is reused unless a record, member
  or Full Cache change happened. Zone enrolment reads each AI group's members
  once per pass instead of once per zone. Inventory events, on server and
  clients, find a cached part's owner through a map instead of scanning every
  cached character (this was quadratic when a group was suspended). "Soldiers
  only" now names only the first civilian group each zone skips; later ones
  are counted in one summary line per zone at most every 300 s.
- Garrison: the costly Unit Caching support check on each guard now runs
  only when the garrison could go to sleep, not on every garrison tick while
  players are near (up to 40 garrison ticks a second, each over every living
  guard); the near-player test reads each player's and guard's position once
  per call. Posted and patrolling guards keep their per-frame movement gate,
  but it no longer looks up the guard's replication component every frame
  and a patrol tests ownership once per frame instead of twice; a calm
  guard's look request is re-sent once a second instead of on every garrison
  tick. The Unit Caching reservation and status lookups match a garrison's
  own squad before checking its guards. The save-exclusion sync runs every
  5 s instead of every second, with one pass over each garrison's guards
  (every save and load still syncs first). Building analysis checks its 4 ms
  slice after every step instead of every 8, and a Full-cached garrison
  skips its floor check until it wakes.
- Random Garrison: while every zone is idle, the director ticks every 2 s
  instead of every 100 ms and returns to 100 ms as soon as a zone has work
  (an idle zone's status text refreshes within 2 s instead of 0.5 s). While
  the AI limit leaves no room for any squad a building could take, the squad
  already drawn is kept instead of being redrawn every tick, so the length
  of such a wait no longer changes which squads a seed produces; once any
  squad has room, a squad is drawn afresh as before. A seed that hit the AI
  limit can therefore give different squads than in 0.1.14. The building
  census makes cheaper checks and keeps its eligibility checks within the
  director's per-tick time budget.
- Time and Weather: during a transition, the rain, fog and wind overrides
  are written (and sent to clients) only when their value changes, instead
  of every 0.5 s; a channel that stays at its current value is written once.
  The final values are still set when the transition ends.
- Ambient Civilians: where a mission has a transit-blocking exclusion zone,
  a resident's route (walk, activity approach, shelter and emerge) is first
  checked against the route's bounding box, and the per-segment exclusion
  test runs only when a zone touches that box. A resident's components are
  looked up once when it is bound; the shelter check no longer repeats the
  possession query the danger check has just made; home discovery stops
  probing a ring of cells that is already fully indexed; residents far from
  players have their 2 s checks spread over four passes. Clients (not a
  listen-server host) no longer run the module's frame handlers, and the GM
  debug view asks the server for snapshots only while civilian debug is on.
  Traffic remembers its hidden-from-players test for the rest of each step.
  The scene-prop memo grows from 512 to 4096 paths (DebugLevel 3 shows how
  full it is).
- AI Surrender: the interrogation point used to follow the prisoner's face
  ten times a second on every machine. It now does so only within 20 m of
  the local player and once a second otherwise, which includes a dedicated
  server (the server's 5 s upkeep also moves it). At most 128 squad and
  intel map markers are kept per session: each marker published past that
  removes the oldest one, if it is still on the map (also when markers are
  set to stay forever).
- Ambient Destruction: a zone with no player near but still occupied by
  characters or vehicles checks occupancy every 10 s instead of every
  second, so it can go to sleep up to 10 s later; the suppressed-prop scan
  runs only while a suppression is pending; prefab path checks are
  remembered (up to 4096 paths); and sleeping zones skip the wall-support
  step.
- Small fixes: the AI "proper fire mode was not found" warning is printed
  once per weapon prefab and requested mode (the first 256 pairs per server
  run; later new pairs are not logged); Ambient Unrest looks up each
  protester's AI control once, at spawn; Unit Scripts no longer logs a
  routine loiter re-issue (the first loiter of each script, real retries
  and the final failure still log).
- Portable guards for this work (`tests/Test-PerformanceGuards.ps1`,
  `tests/Test-Perf*.ps1` and additions to `tests/Test-RandomGarrison.ps1`
  and `tests/Test-TimeWeather.ps1`), `tools/Measure-LogRate.ps1`, debug
  cross-checks of the new unit-caching indexes against the old full scans
  and of the new civilian route check against the old per-segment loop
  (`tests/EAC_RouteEquivalenceTest.c`), and a Unit Caching A/B soak fixture
  (`tests/EBG_PerfSoakGameplay.c`).

## 0.1.14

- Garrison: the "EXPBG Garrison" tab in a garrison squad's Edit properties
  (cache mode, wake and sleep distance, status, Release Garrison) was there but
  invisible: the dialog shows tabs as icons only and the category had no icon.
  It now shows the EXPBG badge. The same fix makes the Ambient Civilians
  (Ambient, Traffic, Diagnostics, Exclusion) and Ambient Destruction tabs
  visible.
- Time and Weather: the vanilla weather setting of Scenario Properties was
  missing ("Unknown class 'SCR_WeatherInstantEditorAttribute'": the modded class
  lacked its container decorator), so smooth Scenario Properties weather
  changes never ran. Restored.
- Workshop description without the version number.
- Portable guard: every attribute category has a pack icon and every modded
  editor attribute keeps [BaseContainerProps()].

## 0.1.13

- Fix for Windows clients failing to join with "Can't compile 'Game' script
  module ... Too many instructions per function" in large modsets (seen with
  GM Tools + EXPBG CDF Compat + about 130 other mods; Linux servers were not
  affected). Every static initializer of every loaded mod runs in one engine
  function with an instruction limit; GM Tools' 122 collection statics
  (arrays, maps, helpers) are now created on first use instead, which frees
  most of GM Tools' share of that limit. No behaviour change.

## 0.1.12

- Time and Weather: "Weather Transition" and "Time Skip" show EXPBG preview
  cards in the entity browser instead of the missing-image sign (0.1.11 left
  the cards out until the textures were cooked).
- New module Random Garrison (Systems): place "Random Garrison", set it up and
  choose Generate. It garrisons random buildings within its radius (default
  150 m): a number of buildings (1-32, default 4) or a share of those that can
  be garrisoned, each with one to four random squads (default 1-2) from the Game
  Master squad list of one faction (default USSR), or of two factions with each
  building drawing one, so one building never holds both. Squad sizes go by the
  number of soldiers (fire teams, squads, large, small teams); a squad only goes
  where it fits the building's posts, and medical, logistics and essential squads
  are skipped by default. No building closer than 200 m (adjustable) to a player
  character is used; Game Master cameras do not count. A seed repeats a
  generation (0, the default, draws a new one and shows it). Every squad is an
  ordinary EXPBG garrison: posts, indoor patrols, caching with the zone's cache
  mode and distances (a change applies to the zone's garrisons at once), Force
  Move and Add Garrison work as usual, casualties are never refilled and the zone
  never generates again by itself. Regenerate clears the zone's garrisons and
  generates again, Clear generated garrisons deletes them, Stop ends the work in
  progress and keeps what has deployed. Deleting the zone keeps its garrisons
  (default) or deletes them. A read-only status row shows the progress (reopen
  the attributes to refresh). Garrisons and zone settings are saved with the
  mission (native saves; CDF saves with EXPBG CDF Compat) and after a load the
  zone finds its garrisons again; it never resumes by itself. Building analyses
  run after any waiting EXPBG Add Garrison request.
- Garrison: EXPBG Add Garrison now refuses a squad whose prefab does not spawn its
  soldiers by itself (it used to wait 45 s and leave an empty squad). The garrison
  save ledger keeps which Random Garrison zone made each garrison; saves from
  0.1.11 still load.
- Garrison: a guard who came to rest 0.5 to 1 m from his post on a spot that
  could not become his post was sent back to it about ten times a second for as
  long as he stood there: the log filled with "came to rest ... not a plausible
  post; sent back to it" and his garrison could not cache. Within 1 m he counts
  as on his post (as before) and now holds where he stands.

## 0.1.11

- Garrison: garrisons are saved with the mission. Build a mission, save it and
  load it: every garrison comes back in its building with its posts and patrol
  stops, cache state (awake, Simulation or Full, the Full ones still with nobody
  in the world), wake and sleep distances, casualties (never respawned) and the
  AI Surrender and AI Global Skills squad and soldier overrides. Loadouts and
  wounds restore as prefab defaults, as Full caching does. Native saves carry a
  garrison ledger of their own; CDF Game Master Save carries the same ledger
  with EXPBG CDF Compat 0.1.6 or later. Garrison squads and soldiers are no
  longer saved as ordinary squads: the native autosave no longer fails while a
  garrison is active ("[EXPG SAVE] Refused active garrison ownership" and
  "[PERSISTENCE] Save failed" are gone) and Unit Caching Prepare for Save no
  longer releases garrisons. On load each garrison waits for its building's
  analysis, takes its posts back by position and wakes with its soldiers' AI
  held until every guard is bound; a garrison whose building is gone restores
  as an ordinary squad. Add Garrison waits while garrisons load. A save whose
  garrison ledger cannot be read loads without garrisons and tells every Game
  Master. With an older EXPBG CDF Compat, CDF keeps the 0.1.8 rules (Full runs
  as Simulation, CDF saves wait for Prepare for Save).
- Garrison: Full caching now captures and removes the squad itself too and
  recreates it at wake (as Unit Caching Full does), so no empty squad is left
  behind for a save, a load or a Game Master to delete. A Full-cached garrison
  has no squad in the world until it wakes. A squad that cannot be captured
  (orders or AI settings it cannot own) caches in Simulation instead, with the
  reason in its status, and tries Full again ten minutes later.
- Garrison: EXPBG Add Garrison never deletes soldiers. The 0.1.10 live test
  placed 2 of a 9-man rifle squad on a small house and deleted the other 7.
  Now the whole squad deploys: soldiers beyond the building's posts take free
  posts, then patrol inside, then other watch positions, then places close
  around the building, last where they spawned (the order for added squads).
- Garrison: "EXPBG Release All Garrisons" (right-click a garrison squad) wakes
  every garrison and returns its squad to normal AI control, for a mission that
  will be saved and used without EXPBG GM Tools.
- Unit Caching: a native save's group that was eliminated (no living AI) no
  longer holds every later save with "Original member has retained UUID-less
  transfer lineage"; its record is forgotten on load.
- Briefing Projector Screen (and the wall-map board): the screen shows the map
  again, now with roads, buildings, names and contours. In the 0.1.10 live
  test it stayed plain white on both sides, before and after "EXPBG: Brief on
  map". 0.1.10 drew the board in a second render target and showed it on the
  screen through an image; that image never received the board picture, so its
  plain white colour covered the screen. Both screens map their picture a
  quarter turn round, and the game's own map view cannot be turned, so each
  player's game now places a flat panel with an upright picture just in front
  of the screen (a base-game pane, covering about 96% of the projector and 98%
  of the wall map; the rest shows plain paper) and draws the board on it
  unturned: the game map view with roads and buildings over the world map
  image, drawn lines, markers and the title, north-up. If the map view cannot
  draw, the map image, markers and lines still show; if the panel cannot be
  created, the board is drawn on the screen itself with every element turned
  so it still reads north-up (the single render target path 0.1.9 drew live).
  The panel follows the board when a Game Master moves it. The board no longer
  resizes its layout root (the "EBM_Root: Position/Size works only when min and
  max anchor is the same" log error), and it binds the render target again if
  the screen's model object changes. Client option -ebmDiagnostics 1 (or the
  board's "Debug trace" attribute) logs board creation, the panel, the render
  target bindings with the models' materials and the drawing path
  (path=engine or path=raster) as "[EBM DIAG]" lines; -ebmDirect 1 skips the
  panel for a comparison. Not yet tested in the game.
## 0.1.10

- New module Time and Weather (Systems): "Weather Transition" blends the
  weather to a chosen target (clouds, rain, fog, wind) over 1-120 real minutes
  (default 10) instead of switching at once. Saving its settings starts the
  transition; a new one takes over smoothly from the current values. While the
  module exists, weather picked in Scenario Properties blends too (switchable).
  "Time Skip" fades every screen to black, shows "6 hours later" (editable) and
  the new time, moves the clock forward (the date rolls over) and fades back in
  (defaults 6 h, fades 2/3/2 s). Not yet compiled or tested in the game; the
  card art follows.
- Game Master entity browser: pack items no longer start with "EXPBG", so the
  item name is readable at a glance; they still carry the EXPBG logo and EXPBG
  faction. For example "Radio Black", "Sound: Church bell", "Ambient
  Civilians", "Intel - Server Rack A", "AI Cache Zone" and "Briefing
  Projector Screen". A browser search for "EXPBG" no longer finds them by
  name. Context actions (EXPBG Add Garrison, EXPBG: Brief on map), attribute
  tabs and the EXPBG label keep their names. A portable guard now rejects
  entity names with the prefix.
- Garrison: guards no longer stand where they block a door. In a live test 17
  guards held a two-storey Morton house through four firefights, but one stood
  right behind the front door, so it could not be opened (a guard on a post
  cannot step aside). Every door leaf of the building now has a keep-out zone:
  its full swing on both sides (native doors open either way) and the doorway
  1.5 m deep on each side. No post, patrol stop or alarm position lies in it,
  including entrance posts, extra positions of added squads and places around
  the building (nobody stands on the step in front of a door); soldiers still
  walk through. Door guards now stand 2-4 m inside the outer door, at least
  1.2 m deep, preferably diagonal to the doorway, facing it. Small houses may
  offer a few fewer posts.
- Garrison: soldiers beyond the window, door and stair posts now patrol inside
  the building. Each walks at walking pace to a free stop of the same indoor
  area (stairs included, never outside, never in a doorway), stays 10-30 s
  watching a hallway or doorway, then moves on; stops are claimed, at least
  1.5 m from every other guard, so patrollers never bunch up, and enough stops
  stay free that they can always move on. Added squads take free posts first,
  then patrol inside while there is room, then other window, door and stair
  positions that leave every patrol stop free, then stand around the building
  (now at most about 25 m from its walls). When a firefight starts
  (any guard of the building is shot at, injured or sees an enemy) each
  patroller runs to the nearest free window, preferably facing the enemy; if
  none is free he watches a door or stairs from a free spot nearby, or holds
  his stop. A window post whose guard was killed can be taken during an alarm
  (unless the body lies on it); posts are never refilled in peace. A minute after
  the last alarm they return to patrol one by one. Caching waits until every
  patroller stands at a stop (at most about 20 s), and Full and Simulation
  caching bring patrollers back as patrollers at their stop; casualties are
  never replaced. A patroller whose walk fails steps onto a free stop next to
  him or walks back to the stop he came from, so he never stands in a hallway
  beside another guard, and after Simulation caching each patroller first
  stands at his stop again. Patrol movement passed the native interior test on
  a two-storey Everon town house.
- Garrison log: the "added to building" line also counts soldiers patrolling
  inside.
- Garrison: a guard on a post now stays there. Live report: guards left their
  spots and ran around, and their garrison was no longer cached. One guard
  knocked more than 1.5 m off his spot (ACE ragdoll or unconsciousness, a blast,
  a push through a doorway, an ACE carry or drag, a Game Master move), possessed,
  moved to another squad or deleted released the whole squad without a word, and
  released soldiers are ordinary AI (EXPBG RO AI sends them to cover). Now each
  case concerns that guard alone: knocked off his spot, he holds where he came to
  rest (he never walks back and keeps the plan position if one is within 0.5 m)
  when that spot is plausible: within 6 m of his post and, for a post in the
  building, on its indoor floor (never on the roof or outside); otherwise, once
  he is conscious and still, he is put back on his post. A newly placed soldier
  is bound only once he stands on his post (the move is sent again if it has not
  landed), so nobody takes his spawn point as his post;
  possessed, he holds where the player leaves him; moved to another squad, he is
  forgotten; deleted, he is never respawned. The others keep their posts and
  their caching. Guards on posts still crouch, go prone, turn, aim and fire in
  place; only patrollers move, and only inside (one who cannot patrol for 10 s
  holds where he stands). A whole garrison is released only by Force Move, the
  Release attribute, Unit Caching (regroup, Prepare for Save), a deleted squad, a
  moved or replaced building or the last guard's death. The server log then
  prints "[EXPG Garrison] group=... released (reason)" and the garrison's status
  shows "Released: reason".
- Garrison caching: a garrison that should cache but cannot says why in its
  status and the server log, for example "Cache held: guard 3: Unsupported life
  or movement state" (unconscious), a bleeding or possessed guard, a soldier who
  is not one of its guards, or a combat alarm. Full cache refusals name the guard
  or the squad reason. A Full restore no longer stays frozen when one survivor's
  spot fails its safety check (after 10 s he wakes where he stands) or a survivor
  is missing (he counts as dead and is never respawned). A soldier who was ever
  possessed is never cached (Unit Caching rule), so his garrison stays awake
  while he lives; the status says so.
- AI Surrender: new setting "Commander: grenade suicide instead of surrender
  (%)". It defaults to 0, so nothing changes until a Game Master raises it.
  When a broken squad's leader would surrender, this chance is rolled once for
  him. If it hits, he does not surrender: he stops fighting, crouches, places a
  fragmentation grenade at his feet 1-2.5 s later, and it goes live a second
  after that. The grenade's own fuse (about 4 s) and explosion do the rest, so
  the blast also hurts anyone close by, including his surrendering men. The
  rest of the squad surrenders as usual. The leader is the squad's leader; if
  the squad has none, its highest-ranking able soldier. "Commander must carry a
  grenade" (default ON) means he uses one of his own grenades, and a leader
  without one surrenders; with it OFF, such a leader gets a vanilla M67 (US) or
  RGD-5 (other sides). Players, possessed soldiers, soldiers in vehicles and
  cached squads are never affected; a grenade that someone picks up, or whose
  leader a Game Master possesses, before it goes live is never set live. He
  does not shoot himself, because vanilla has no animation for that. Saves now
  store the commander settings; older saves still load, with the two new
  settings at their defaults, but a save made with this version does not load
  in 0.1.9. The native fixture `tests/ESR_CommanderGrenadeGameplay.c` is
  written but has not been run yet.
- AI Surrender: new setting "Interrogation: reveal intel items (%)" (default
  30). The first time a prisoner answers anything but a refusal, this chance is
  rolled once, on its own; the squad and identity chances are unchanged. If it
  hits, he also points out the nearest intel items (EXPBG Intel Items, at most
  three) within the "Reveal search radius (m)": lying on the ground, in a crate
  or vehicle, or on a body. Items a player carries, items he carries himself,
  and laptops or tablets a player has already read or picked up are left out
  (Intel Items keeps no read state for notebooks and manuals, so those count
  until a player carries them). Each item gets a map marker "Intel
  (interrogation)" for the interrogating player's faction, kept or removed like
  the squad marker ("Intel marker lifetime"), and his answer ends with a line
  such as "He also points out 2 intel items: about 75 m north-east, about 150 m
  south." Asking again repeats it without new markers. Saves now store
  thirteen AI Surrender settings; older saves still load, with the new setting
  at 30%. Intel Items now keeps a read-only server list of the intel items in
  play for this search. The native fixture `tests/ESR_IntelRevealGameplay.c` is
  written but has not been run yet.
- AI Surrender: surrender and interrogation chances per squad and per soldier.
  A new "EXPBG Surrender & Intel" tab in the Edit properties of an AI squad and
  of an AI soldier (shown while an AI Surrender module is placed) has "Surrender
  chance (%)", "Interrogation: reveal squad (%)", "Interrogation: identity (%)"
  and "Interrogation: reveal intel items (%)", 0-100% in steps of 5. Each starts
  on "Use module setting" (squad) or "Use squad or module setting" (soldier), so
  nothing changes until a Game Master sets one. A soldier's own value wins over
  his squad's, and the squad's over the module. A squad's values apply to all
  its soldiers, including later reinforcements. Whether a squad breaks is still
  decided by the module's casualty threshold; then every able soldier rolls his
  own surrender chance, and a set chance is exact (the random factor only
  spreads the module chance), so 0% never surrenders and 100% always does. A
  prisoner takes his squad's interrogation values with him when he surrenders,
  so later changes to the squad do not affect prisoners already taken; his own
  values can still be changed while he is a prisoner. The values stay with the
  squad and soldier through Unit Caching and Garrison caching (Full and
  Simulation) and are kept by native saves and by CDF Game Master Save; missions
  and cache snapshots saved before this version load with no overrides. With
  Diagnostics on, the server logs each change, each squad's chance and source,
  and each prisoner's effective chances. The native fixture
  `tests/ESR_OverrideGameplay.c` is written but has not been run yet.
- AI Global Skills: rules of engagement per soldier. The squad's rules of
  engagement move from "EXPBG ROE" in the vanilla Group tab to "Rules of
  engagement (squad)" in a new "EXPBG Rules of Engagement" tab, next to a new
  "Rules of engagement (soldier)" on AI soldiers with the same choices (Return
  Fire Only, Fire on Sight, Warning Shots First, Exempt) and "Use squad setting"
  as the default. A soldier's own choice wins over his squad's, and the squad's
  over the module default. With his own Return Fire Only he holds fire until his
  squad is fired upon, even in a Fire on Sight squad; with Warning Shots First he
  fires the warning rounds himself and turns lethal 5 s later. Player squads keep
  vanilla behaviour. The soldier's choice survives Unit Caching and Garrison
  caching and is kept by native saves (earlier saves still load, and a save
  without soldier choices still loads in 0.1.9) and CDF Game Master Save. Squad
  rules of engagement now also survive Unit Caching Full caching, which
  recreates the squad.
- Advanced Briefing Map: the map on the board was a quarter turn round (north
  pointed left, the title ran up the left edge), squeezed and cut off on one
  side. Cause: the wall-map model shows its render texture turned 90 degrees
  and uses only three quarters of it. The board is now drawn upright in its own
  canvas, sized to the screen surface's texture area, and handed to the
  model's render texture turned the other way, so north is up, the title reads
  left to right and nothing is cut off or stretched. Markers and drawn lines
  turn with the map and stay on their places.
- Advanced Briefing Map: the board now shows roads, buildings, place names,
  contours and the grid like the in-game map. Before, it showed only the
  world's map image (terrain shading). It now borrows the game's own map
  renderer, styled by the scenario's map settings, for the briefer's view. One
  board per player draws it, and only while that player's own map is closed: a
  second board nearby, or the board while your own map is open, shows the plain
  map image as before. Your own map then opens at your own zoom and position.
- Advanced Briefing Map: the Game Master item is now "Briefing Projector
  Screen" (Systems): the Prop - Projector Screen model from Structures For GM
  byHeine by Heine.CRV (Workshop 628EDA2ABC937159, APL-SA; credited in
  `docs/licenses/advanced-briefing-map` and the module's Credits folder), about
  4 x 2.3 m and floor-standing, with the live map on its screen. The picture is
  on the side the roller case sticks out to; turn that side toward the
  audience. Heine's screen image and prefab are not included. The wall-map
  "Briefing Board" stays for existing saves (with the orientation fix) but is no
  longer offered in the Game Master list. None of this has been checked in the
  game yet: the turned canvas, the game map on the board, the projector's look
  and its collision still need a native test.
- Ambient Destruction: new zone setting "Vehicle types" (after Wreck
  intensity): Civilian, Military or Both. Both is the default and keeps the
  previous mix, so existing seeds still give the same layouts. Civilian zones
  spawn only car, van, bus, taxi, police, ambulance, pickup and civilian truck
  wrecks; Military zones only BRDM-2, BMP-1, BTR-70, M113, T-62, M151A2, HMMWV,
  UAZ-469, UAZ-452, M923A1 and Ural-4320 wrecks. Judgement calls: the UAZ-452
  van and the UAZ-469 count as military (Soviet army vehicles in the base
  game); police car and ambulance count as civilian. The category of each
  wreck is data in the wreck catalog, so a mod that adds wrecks can classify
  them too. Changing the setting regenerates the zone's scenery (destroyed
  buildings stay destroyed). It is saved with the zone: CDF saves now write
  zone snapshot schema 3, and older saves still load, as Both, with their
  wrecks unchanged (0.1.9 and older cannot load the new saves). The native
  fixture `tests/EAD_VehicleCategoryGameplay.c` is written but has not been
  run yet.
- Ambient Sounds: the entity browser offers exactly three radios: Radio Black
  (civilian) and the military Radio AN/GRC-160 and Radio R123M. Their Recording
  holds radio chatter and broadcasts only: radio static, Apache, US
  battlefield, Russian, Chinese and Arab chatter, the per-language Random
  choices, and now also the Hanoi Hannah broadcast. Nokia ringtone, church
  bell, firefights and the other non-radio sounds stay on the placed
  "Sound: ..." modules, which keep all 28 recordings. The eleven radio-chatter
  "Sound: ..." items now show the Sound effects picture instead of a radio, so
  the browser no longer looks as if every track had its own radio. Radio Red
  stays out of the browser and still loads from saves. Saved radios keep their
  recording (radios store a permanent recording ID, not a list position); a
  save with a radio set to Hanoi Hannah does not restore that radio in 0.1.9.
  The Credits now list the thirteen unchanged Vinny - Sounds recordings.
  Portable guard `tests/Test-Radios.ps1`; Hanoi Hannah on a radio has not been
  heard in the game yet.

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

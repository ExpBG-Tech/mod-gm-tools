# EXPBG GM Tools validation gates

## Unit Scripts: Freeze, Hold and the chair pose (Unreleased)

Portable: `tests/Test-UnitScriptsHold.ps1` (run by `tests/Test-Tools.ps1`)
checks the following:

- a displacement is never taken for a Game Master move: only the editor's
  `SCR_EditableCharacterComponent.SetTransform` sets a new spot;
- Freeze and Hold put the soldier back the way the editor's owner teleport moves
  him, and keep a new spot only for a drop, a moving platform or an object on
  his spot; Freeze keeps its look claimed;
- the chair pose needs room where it is issued, and a pushed pose ends;
- every log line stays bounded, the Enforce gotchas, and the wiring of
  `tests/EUS_FreezeLeaderGameplay.c`.

Pending gates:

- build.ps1 compile. Most likely to break: the modded
  `SCR_EditableCharacterComponent.SetTransform` next to Unit Dialog's modded copy
  of the same class, `PoseAt` with an out static array, the `TraceOBB` with
  `TracePosition` and a member filter callback, and
  `CharacterAnimationComponent.PhysicsIsLinked()` / `GetLinkedEntity()`;
- native fixture `tests/EUS_FreezeLeaderGameplay.c` (command in
  `tests/GAMEPLAY.md`), then the existing `tests/EUS_UnitScriptsGameplay.c`,
  `tests/EUS_CacheHoldGameplay.c` and `tests/EUS_DisciplineRhsGameplay.c`;
- manual chair FPS A/B before release: a local dedicated server with
  `-maxFPS 240 -logStats 10000`, the op modset including Heine
  Structures/Placeables, and a client in GM:
  - place `MilTable_Open_GM` and an RHS USMC Officer about 0.1 m and about
    0.5 m from the table origin, then apply "Sit on a chair": a refusal naming
    the table, and FPS stays at 240. If it binds instead, the trace does not see
    this table: watch FPS for 5 minutes (a drop means the room check misses this
    furniture; no drop means the inferred cause is wrong);
  - move him 3 m into the open and apply the chair again: it binds, FPS at 240
    for 5 minutes;
  - place the table where he sits: released with "no room" within 10 s, no FPS
    drop;
  - optional: "Sit on the ground", "Stand at ease" and "Push-ups" against the
    table; if FPS drops, extend `EUS_AnimationCatalog.NeedsRoom`. If the chair
    costs FPS even in the open, remove "Sit on a chair" from
    `addon/unit-scripts/Configs/Editor/AttributeLists/Edit.conf`;
- GM session on a local server and client:
  - Freeze a lone officer and walk 5-8 m around him for 2 minutes: his head
    follows you inside the cone, his body does not step or turn round;
  - drag him with the editor: he stays at the new spot with one "moved by the
    Game Master" line; a second drag within 10 s shows as a count on the next
    line; the rotation attribute and a squad drag also log it;
  - Freeze a fire team leader and give the squad a Move waypoint: he stays on
    his spot facing the same way;
  - Freeze an officer at a desk for 2 minutes: no correction loop, at most one
    "held" line every 300 s; then "Sit on a chair" gets the "no room ... is in
    the way" reply and he stays frozen;
  - drag a seated chair-pose soldier: he stands up and sits down again at the
    new spot, and no chair is left behind on the client;
  - another AI walking into a frozen soldier: he is put back, with bounded log
    lines;
  - Push-ups and Lean are not ended as "pushed".

## AI Global Skills and AI Surrender: squad attributes (Unreleased)

Portable: `tests/Test-SquadRoe.ps1` (run by `tests/Test-Tools.ps1`) checks the
following:

- Return Fire Only and armed Warning Shots First follow the strict fired-upon
  rule (`EGS_Provocation.c`: a hit, a kill, a near miss within 4 m, an explosion
  within 10 m) at the actual combat-mode decision point, from the attribute
  through the override and the applied ROE; one log line per contact and the
  calm-down on the shared tick; a PowerShell model of the 4 m rule against
  vanilla's 13 m rule;
- vanilla Set combat mode and the EXPBG squad ROE agree (the Exempt switch, the
  order within one save, the re-assert), and the squad's own combat mode
  survives Unit Caching Full, native saves and session loads;
- a soldier holding fire under his own ROE stays out of his squad's suppressive
  fire; warning shots at player vehicles, a second shooter and vehicle
  clearance;
- the AI Surrender chance descriptions name the casualty threshold, and squads
  whose cache state was held are evaluated once awake;
- the fixture's runner command and RESULT regex.

`tests/Test-PerfSurrender.ps1` now also checks that a soldier the native chain
takes out of his squad before his death is reported still counts for that squad
(a bounded, lazily created list kept for 2 s).

Pending gates:

- build.ps1 compile;
- native fixture `tests/ESR_SquadAttributesGameplay.c` (command in
  `tests/GAMEPLAY.md`), then the existing `tests/ESR_OverrideGameplay.c`,
  `tests/ESR_SuppressGameplay.c`, `tests/ESR_SurrenderGameplay.c` and
  `tests/EGS_SkillsTest.c`;
- a GM session on a dedicated-server client: a Return Fire Only squad ignores a
  firefight next to it until it is fired upon; Set combat mode on an EXPBG squad
  shows Exempt (vanilla) in the EXPBG tab after reopening; warning shots at a
  player in a vehicle; EXPBG RO AI loaded;
- a native save and load and a CDF save and load of a squad with its own
  vanilla combat mode and an EXPBG ROE.

Open decision: Return Fire Only ignores an enemy firing point-blank at someone
else unless a round passes within 4 m, and explosions with no enemy behind them
(for example a GM-placed blast). The limits are constants in
`EGS_Provocation.c`.

## Ambient Sounds: radio playback in busy scenes (Unreleased)

Portable: `tests/Test-RadioPlayback.ps1` (run by `tests/Test-Tools.ps1`) checks
the following in `EAS_RadioRuntime` and `EAS_RadioState`:

- an engine end at least 2 s before the recording's end, heard in range and
  confirmed by the real clock, is an eviction; it retries in 3 s, doubling to
  30 s while evictions follow each other, never later than the recording's own
  end plus its pause; a complete play or a 60 s voice ends the streak; a
  one-shot is never re-armed;
- starts keep a 10% margin inside the audible range (27 of 30 m) and playing
  voices keep the full range; a refusal below audibility retries in 3 s, other
  refusals back off 5-30 s, and only invalid metadata parks a module;
- refused starts log two normal lines per placement until a settings change;
- up to four finite sources play at once;
- both native fixtures are present.

`tests/Test-Radios.ps1` now also requires every radio and TV event to keep
priority 80-100 and `bypassVolumeTest 1` in Workbench key order (no
`noInAudible`); crowd events are unchanged.

Pending gates:

- build.ps1 compile (Windows and the Linux DS);
- native fixtures `tests/EAS_RadioStateGameplay.c` (22 checks) and
  `tests/EAS_RadioRetryGameplay.c` (12 checks) (commands in `tests/GAMEPLAY.md`);
- Workbench Audio Editor: the 15 radio and TV nodes in `EXPBG_Radio.acp` show
  Priority 85 and Bypass Volume Test ticked, the crowd nodes are unchanged. Do
  not commit a Workbench re-save: it rewrites the one-class-per-line layout that
  `Test-Radios.ps1` and `Test-CrowdAudio.ps1` parse;
- GM session on a local retail server and client started with
  `-easDiagnostics 1`:
  - setup: Radio Black, AN/GRC-160 and R-123M 5-10 m apart (On, Debug on,
    Volume 20, different recordings), an emergency-alert TV (Loop on, pause
    600), two running `E_GeneratorFloodlight_US_01`, a few placed sounds (close
    firefight, shelling) and running vehicles; stand 10-20 m away;
  - expect `action=play` for each radio (voices=1..4) and the radios audible
    together, no `release ... reason=finished` with a large `remaining=`; a cut
    voice shows `release ... reason=evicted`, then `action=evicted ...
    retry_s=3` (later 6, 12 ... at most 30), then a new `action=play`. For the
    TV, `retry_s` is never above its remaining length plus 1 s; after a complete
    play the next cut shows `retry_s=3` again. Summary lines end with
    `evicted=N`;
  - edge: walk out to 28-29 m: playing sources keep playing until 30 m
    (`reason=inaudible`) and restart only at 27 m or closer; no
    `[EAS] Radio start failed ... distance=29.x`;
  - log bound: after about 30 minutes in the busy scene, at most two
    `[EAS] Radio start failed` lines per placed source; with Debug on, later
    refusals appear only as `action=refused` or `action=inaudible`;
  - optional: a client stall (long alt-tab while a TV plays near its end) still
    ends with `reason=finished` and keeps the 600 s pause;
- EXPBG Ambient Radio vehicle chatter (same `EXPBG_Radio.acp` events) still
  plays, now with priority 85, next to a running generator.

## Unit Caching, Persistent Battlefield and Garrison patrol spacing (Unreleased)

Portable:

- `tests/Test-SaveGateReconnect.ps1`: no GM-created latch (no placement or
  squad-member wrappers in EXPBG stacks); `EBG_CacheSnapshot.CanSave` names a
  mission end or change as such and refuses it only while the Optimizer holds
  state or this world's cleanup has begun; EXPBG Reconnect prints the disconnect
  cause as group/reason and names the check that refused a reservation;
- `tests/Test-GarrisonPatrolSpacing.ps1`: a re-anchored guard makes patrol
  claims within 1.5 m yield to another free stop (never in an alarm or a cache
  settle, never moving a guard), off-plan posts are seen by every claim,
  arrivals and failed walks never dwell within 1.2 m of a standing soldier,
  event-driven work only, and `tests/EXPG_InteriorGameplay.c` still fails on
  soldiers within 1.2 m or claims within 1.5 m. A self-test proves every check
  fires on a regressed copy.

Pending gates:

- build.ps1 compile on Windows and the Linux DS (the removed modded
  constructors and override, `EBG_CacheManager.IsCleaningUpCurrentWorld`,
  `EBG_CacheSnapshot.HasOptimizerState`, `GetGame().GetFullKickReason`);
- native fixtures `tests/EBG_CacheRecoveryGameplay.c` (its `CanSave` checks with
  zones present must still pass), `tests/EBG_LocalCacheGameplay.c` and
  `tests/EXPG_InteriorGameplay.c` (it failed the spacing check in 5 of 5 runs
  before this fix);
- reconnect on a local retail server and client: kill the client process; the
  server logs `[EXPBG Reconnect] Reserved living character for playerId=N
  cause=REPLICATION/<NAME> (1/<n>)` with no `0x0x`; rejoin with the same body.
  Dead, or on the deploy screen, then quit: `Reservation rejected: character is
  dead ...` or `Reservation rejected: no controlled entity ...`;
- GM placement of a character, a group and a vehicle behaves as before; a
  placement script error no longer has `EBG_PlayerHistory.c` in its stack;
- CDF at mission end (GM Tools, CDF Compat and CDF with `saveOnGameEnd=true`):
  without cache zones a shutdown logs no `[EBG CDF HOLD]` and the endgame slot
  loads complete; with a Unit Caching zone (Simulation and Full groups) the
  second capture logs `[EBG CDF HOLD] The mission is ending, changing or not
  running; capture skipped and the existing save kept` and the first endgame
  save loads with the zone and its cached groups. Also run EXPBG CDF Compat
  `tests/Run-CdfRoundTrip.ps1`.

## Random Garrison (Unreleased)

Portable: `tests/Test-RandomGarrison.ps1` (run by `tests/Test-Tools.ps1`) checks
the following:

- the module is registered last in `tools/pack.json` with its own `Edit.conf`,
  `Systems.conf` and `GameMaster.conf` (vanilla identities), the prefab, the
  browser name without EXPBG and the native zone serializer registration;
- the attribute list order, each class's `m_Key` against the module's `KEY_*`
  constants, the preset rows, the restore-only saved state and the dialog filter
  (attribute class names are permanent CDF save keys);
- the zone never spawns, steps a plan or reserves a budget itself: it uses the
  shared spawner (`EXPG_GarrisonSpawn.c`) with its token, background analyses
  (`EXPG_PlanWaiter.Background`) and the manager's additive views
  (`PlanCount`, `CollectGenerated`, `AssignedSoldiers`, `Discard`);
- EXPBG Add Garrison uses the same validator and spawner with its budget checks,
  ticket and vanilla placement callbacks in their order;
- the garrison ledger always writes `generatedBy` and reads it with a default
  (ledgers without it still load; schema stays 1);
- the census, catalog and director bounds, the rules (with a PowerShell model of
  the building target), the Enforce gotchas, ASCII/LF, the contract and fixture
  wiring and the docs.

Native (pending, run by the orchestrator):

- build.ps1 compile;
- Run-Contracts (`[EXPG RANDOM CONTRACT RESULT] rules=1 shuffle=1 packing=1`
  from `tests/EXPG_RandomGarrisonTest.c`);
- fixture `tests/EXPG_RandomGarrisonGameplay.c` (command in `tests/GAMEPLAY.md`);
- regressions, because Add Garrison now goes through the shared spawner: the
  default fixture, `-FreshTrim`, RepeatGarrison, PostSpread, ScanProgress,
  Interior, Ledger.

Pending gates:

- GM session (retail server and client): place the zone, the radius mesh, the
  dialog (factions list, presets, status after reopening), Generate, Stop, Clear,
  Regenerate, a client joining during a generation;
- a native save and load with generated garrisons: the zone comes back Stopped
  ("Loaded: ...") and Clear or Regenerate finds its garrisons again;
- a CDF save and load with EXPBG CDF Compat (attribute restore of the token);
- a modded faction (RHS) catalog: squads that spawn no members or carry stale
  size labels are rejected or bucketed by their roster.

## Random Garrison: building target, stops and notices (Unreleased)

Portable: `tests/Test-RandomGarrisonFill.ps1` (run by `tests/Test-Tools.ps1`)
checks the following:

- up to `EXPG_RGRules.AttemptCap` tries (three times the target, at least the
  target plus 16, never more than the eligible buildings: 20 for 4 of 49, 96 for
  32), with a model of the try limit;
- building types that failed for their posts or rooms are put back for a second
  pass, the first squad is judged as soon as the analysis is ready, and a
  behaviour model of the queue on the 49 eligible buildings around Krasnostav
  (0.1.14 against the new rule) with the event order shuffled, so a seed
  repeats its generation whatever analysis finishes first;
- the status, the log and the Game Master notice carry the failures by reason;
- an AI-limit stop stays Stopped with one notice instead of turning into Done, a
  Generate while clearing is told to wait, and a Game Master who stops another
  one's generation is told too;
- the wiring of the fill and stop fixtures.

Pending gates:

- build.ps1 compile, or Run-Contracts on a snapshot with these changes (the
  module, `EXPG_RGRules.AttemptCap` and `ReasonLabel` have not been compiled);
- the existing fixture `tests/EXPG_RandomGarrisonGameplay.c` with its existing
  regex (buildings=4, squads=[4-8], analysingSeen=1, sameBuildings=1,
  samePrefabs=1, orphans=0);
- native fixtures `tests/EXPG_RandomGarrisonFillGameplay.c` and
  `tests/EXPG_RandomGarrisonStopGameplay.c` (commands in `tests/GAMEPLAY.md`);
- Krasnostav on the local production-replica server and client (`.local/e2e`:
  GM_Cherno, Chernarus Minus, RHS, retail server and client). As GM, place
  Random Garrison at about 11100,12300: radius 150, RHS_AFRF, fire teams and
  squads, 4 buildings, 1-2 squads.
  - Generate; pressing it again during the run shows "Busy (Analysing ...): wait
    for Done or use Stop first".
  - At the end, "Done: 4 buildings, N squads (seed S)" plus any failures by
    reason, in both the status and the hint/chat.
  - Server console: the `selection: ... tries=20` line, the per-building
    `failed (posts P, tries a/b): <reason>` and `deployed in ... posts P` lines,
    and `done: buildings=4/4 ... tried=x/49 tries=20`.
  - Regenerate with the same seed: the same buildings.
- the other cases in game: Generate during a Clear ("Busy (Clearing ...): wait
  until it has finished"); with two GMs, GM A generates and GM B stops, and both
  get "Stopped (stopped by the Game Master): ..."; with a low AI limit
  (`operating.aiLimit`, for example 10), Generate gives exactly one notice
  "Stopped (the AI limit was reached (AI limit X/Y)): ..." after about 60 s, the
  status stays Stopped and the console shows one `stopped` line and no `done`
  line for that run;
- confirm the original cause: grep the production server log of 2026-10-07
  (about 16:00 to 18:00 UTC) for `[EXPG RANDOM] zone`, especially an AI-limit
  `stopped (the AI limit was reached` followed by `done:` for the same zone (in
  0.1.14 that pair showed the Game Master a plain "Done: 1 buildings"), and for
  `failed (...)` lines and manual Stops.

## Random Garrison: support squads (Unreleased)

Portable: `tests/Test-RandomGarrisonSupport.ps1` (run by `tests/Test-Tools.ps1`)
checks the following:

- a catalog squad is a support squad by the labels of its group, else by the
  labels of every soldier, else by its file name (`EXPG_RGRules.SupportName`),
  classified once while the faction's catalog is read, never per pick; a mission
  maker's Squad prefabs list is used as given;
- the wiring in `EXPG_SquadPool.c` and the `squad catalog` log line;
- the keyword list against false positives (vanilla and installed mod squad
  names and folders);
- a PowerShell model of the whole rule over the vanilla USSR, US and FIA group
  catalogs plus the REAPER helicopter crews: ammo teams, crews and medical
  sections are support squads; rifle, machine gun, AT, sniper, recon, special
  forces, sapper and engineer squads stay;
- "Exclude support squads" keeps key 2, default on and its saved 0/1 value; the
  attribute text, README and CHANGELOG name the kinds; the native Rules contract
  runs `SupportNames`.

Pending gates:

- Run-Contracts (`tests/EXPG_RandomGarrisonTest.c` Rules runs `SupportNames`);
- a GM session with the RHS catalog: the server's `squad catalog` line counts
  the support squads, and no ammo team, crew or medical squad is placed.

## Garrison save bridge and no trimming (Unreleased)

Portable: `tests/Test-GarrisonLedger.ps1` (run by `tests/Test-Tools.ps1`) checks
the ledger format and limits, the building identity rule, the native and CDF
exclusion with hand-back, the native state registration (own GUIDs, merged
GameMaster.conf), the public API and bridge handshake, the legacy-only release,
durable Full with spawn-call AI pins and squad rebind, import by token with
plan-first remap, no trimming, the Unit Caching eliminated-record fix, the
override seams, the Enforce gotchas and the ledger fixture wiring.
`tests/Test-GameplayEvidence.ps1` now requires the twelve-man `-FreshTrim` case
to keep all twelve.

Native fixtures (commands in `tests/GAMEPLAY.md`): `tests/EXPG_LedgerGameplay.c`
(new), and the garrison fixtures adapted to durable Full (default, `-FreshTrim`,
hold, interior, repeat, CDF fallback).

Pending gates:

- a cold native round trip (save, restart the server, load the save) with awake,
  Simulation and Full garrisons;
- live CDF save and load with EXPBG CDF Compat 0.1.6 (GM UI, dialogs), including
  an autosave with garrisons active and no `[EBG CDF HOLD]`;
- multiplayer: a client joining after a load sees the garrison squads and their
  attributes;
- Full caching of garrison squads with Game Master orders or AI settings (the
  Simulation fallback when the squad cannot be captured).

## Time and Weather (Unreleased)

Portable: `tests/Test-TimeWeather.ps1` (run by `tests/Test-Tools.ps1`) checks
the following:

- the pack.json registration, module files, metadata GUID references (Systems,
  persistence, layout, fixture) and browser names without EXPBG;
- the defaults (10 min; 6 h, fades 2/3/2 s, "{hours} hours later"), clamps and
  the attribute list (order, keys, choices, local read-only statuses, actions
  never saved);
- clouds through the engine queue (direct start after the hold left over, else
  pin, at most one start request) with no `ForceWeatherTo` or
  `RemoveStateTransition`, no empty weather names, rain, fog and wind eased with
  the clouds, pins measured for rain and fog jumps, deferral that leaves another
  source alone, no instant previews;
- the foreign-change hooks, the Scenario Properties smoothing guard;
- the broadcast fade RPC, the overlap refusal and the clock change at full
  black, plus a PowerShell model of the date rollover;
- the Enforce gotchas, ASCII/LF, and the runner commands and RESULT regexes of
  `tests/ETW_TimeWeatherGameplay.c`, `tests/ETW_CloudProbeGameplay.c` and
  `tests/ETW_CloudBlendGameplay.c` (all three at a 1440 s day).

Pending gates:

- build.ps1 compile (watch `ETW_WeatherRunner.c`: the new helpers and the
  `HOLD_ACROSS_PIN` const used in a condition; also `ETW_Hooks.c` and
  `ETW_WeatherModule.c`);
- native fixtures (commands in `tests/GAMEPLAY.md`), in this order:
  - the cloud probe `tests/ETW_CloudProbeGameplay.c` (evidence; its values set
    `DIRECT_WAITS_HOLD`, `START_REQUEST` and whether the direct path works);
  - the cloud blend gate `tests/ETW_CloudBlendGameplay.c` (pinJump below 0.05,
    otherwise set `HOLD_ACROSS_PIN`; check the elapsed time against its 520 s
    deadline);
  - the existing fixture `tests/ETW_TimeWeatherGameplay.c`, now with its weather
    phases at a 1440 s day starting 12 s past the hold, and smooth=1 required;
- GM session:
  - both modules in the Systems list with names and descriptions;
  - preset buttons, with no preview: picking a weather does not change the sky
    before Save, and Save does not reset it;
  - status rows refresh on reopen;
  - Scenario Properties weather blends with the module present and is instant
    without it;
- dedicated server with a client and the production modset (Cherno, normal day
  length, Atmospheric Weather Mod):
  - a Weather Transition to Overcast Rain at 5 and at 10 minutes: no flash when
    picking, no reset on Save, the status shows "Clouds start changing ...", the
    clouds thicken gradually with the rain, no jump at the end; server log
    `[ETW] clouds are blending to ...` and `transition done ...` with snapped and
    deferred off and clouds `direct` (or `pinned`);
  - let a transition finish, then about 3 minutes later pick another target
    with 5 minutes: the clouds take at least 5 real minutes, never about 1 s,
    and the "clouds are blending ..., N s to go" line shows N of 300 or more;
  - pick a weather in Scenario Properties, then within a few minutes start a
    module transition: the status says "Clouds start changing in about N", and
    the clouds start after at most the rest of the 10 in-game minutes, not 10
    more;
  - during a run: Start again with the same target, a different target, Stop
    here and hold (settles on the nearer weather), Return to automatic weather
    mid-blend (finishes, no jump), time paused, and smoothing OFF plus a
    Scenario Properties change (the status names a Game Master);
  - watch the client for a sudden rain or fog change when the log shows "clouds
    settled on" (a pin), and compare it with the "across the immediate weather
    change" line;
  - clouds, rain, fog and wind change smoothly on the client;
  - the black screen covers the player view, the deploy menu and the GM editor
    (Z order);
  - text and time are readable;
  - a player joining mid-fade sees the normal view;
- a native save and load of both modules' settings, and a save during a
  transition (the weather reached so far remains);
- card art (`tools/art/New-TitleCard.ps1`, `Import-PackArt.ps1`, then `m_Image`);
- CDF Compat support (separate repository).

## Ambient Destruction: vehicle types (Unreleased)

Portable: `tests/Test-VehicleCategory.ps1` (run by `tests/Test-Tools.ps1`)
checks the following:

- every wreck in `EAD_Catalog` has a category, matching the reviewed table
  (11 military and 14 civilian assets; the four old-car paints share one bag
  family, so each category has 11 families);
- key 12 with default Both, replication, clamping and regeneration;
- for Both, the wreck bag keeps the pre-setting order and random draws, so
  existing seeds give the same layouts;
- schema 3 snapshots with 13 settings, while schema 1/2 still read, as Both;
- the Civilian / Military / Both spinbox after Wreck intensity;
- the fixture's runner command and RESULT regex.

Pending gates:

- build.ps1 compile;
- native fixture `tests/EAD_VehicleCategoryGameplay.c` (command in
  `tests/GAMEPLAY.md`) and the existing `tests/EAD_RoadWrecksGameplay.c`;
- a GM session: Military and Civilian zones show only their wrecks on a
  dedicated-server client, and the spinbox reads back after reopening the
  attributes;
- a CDF save and load of a Military zone, and a CDF save from 0.1.9 (schema 2)
  loading as Both with its wrecks unchanged.

## AI Surrender: commander grenade (Unreleased)

Portable: `tests/Test-CommanderGrenade.ps1` (run by `tests/Test-Tools.ps1`)
checks the following:

- keys 10/11, the 0% / ON defaults, replication, esrVersion 1 and 2 saves
  loading, and the attribute order;
- the leader-only roll after a successful surrender roll, the guards for
  players, prisoners, vehicles and caches, and the vanilla `SetLive()` arming;
- the grenade placed on the traced floor or ground in front of him (else at his
  feet), and never set live once picked up or under a possessed leader;
- the fixture's runner command and RESULT regex.

Pending gates:

- build.ps1 compile;
- native fixture `tests/ESR_CommanderGrenadeGameplay.c` (command in
  `tests/GAMEPLAY.md`);
- a GM session in which a breaking squad's leader (setting at 100%) crouches,
  places a frag and dies in the blast while his men surrender, on flat ground,
  on a slope and on an upper building floor (the grenade lies on the surface);
- a client view of the grenade and explosion;
- ACE medical (the leader should be killed or downed);
- a Garrison squad.

## AI Surrender: intel items from interrogation (Unreleased)

Portable: `tests/Test-IntelReveal.ps1` (run by `tests/Test-Tools.ps1`)
checks the following:

- key 12 "Interrogation: reveal intel items (%)", default 30, its own attribute
  class after the identity chance, replication, the esrVersion 3 save (thirteen
  values) with esrVersion 1 and 2 loading;
- the unchanged squad/identity roll, then one intel roll per prisoner, only with
  an answer other than a refusal;
- the bounded pass over the Intel Items registry inside the reveal search
  radius: spent computer items and items carried by a player or by the prisoner
  excluded, holders' positions, nearest three;
- markers through the squad marker's publisher (faction, owner, lifetime), the
  dialog line with the 25 m and compass helpers, and the read-only registry in
  `EII_IntelComponent`;
- the fixture's runner command and RESULT regex.

Pending gates:

- build.ps1 compile;
- native fixture `tests/ESR_IntelRevealGameplay.c` (command in
  `tests/GAMEPLAY.md`) and the existing `tests/ESR_RevealGameplay.c`,
  `tests/ESR_SurrenderGameplay.c` and `tests/ESR_CommanderGrenadeGameplay.c`;
- a GM session: intel items placed loose, in a crate and on a body are pointed
  out and marked for the interrogator's faction on a dedicated-server client,
  an item in a player's inventory is not, and the marker lifetime removes the
  intel markers;
- a native or CDF save and load of the new setting, and an older save loading
  with 30%.

## Per-squad and per-soldier surrender, intel and ROE overrides (Unreleased)

Portable: `tests/Test-SquadOverrides.ps1` (run by `tests/Test-Tools.ps1`)
checks the following:

- the "EXPBG Surrender & Intel" and "EXPBG Rules of Engagement" categories, the
  eight AI Surrender override classes (one per slot and target) after Prisoners,
  the squad ROE moved to the new tab and the soldier ROE beside it;
- attribute behaviour: spinbox entries (module, then 0-100 % in steps of 5),
  serializable, set values only in saves, writes only from the server-side
  session-load contract or the editing Game Master, squad and soldier targets
  kept apart;
- resolution soldier > squad > module at decision time, the per-group cache
  keyed on the module settings revision, the surrender roll (threshold first,
  then each soldier's exact override; random factor only for the module chance),
  prisoners capturing their squad's values before leaving it, interrogation and
  intel rolls through the prisoner's effective values, diagnostics lines;
- no replication of the new values; the native save state (DEFAULT when empty,
  bounded, ordered binding) and its persistence config; the AI Global Skills
  state version 2, written as version 1 without soldier overrides;
- Unit Caching and Garrison Full caching: survivor carry, group snapshot and
  portable snapshot keys, which old snapshots may lack;
- the soldier's own combat mode (vanilla unless he has his own ROE in a managed
  AI squad), his own warning-shot timers on the shared tick, bounded loops;
- Enforce gotchas, ASCII and LF in the new files, and the fixture's runner
  command and RESULT regex.

`tests/Test-CommanderGrenade.ps1` now expects the eight override classes after
Prisoners and the `rolled < 100` surrender roll.

Pending gates:

- build.ps1 compile;
- native fixture `tests/ESR_OverrideGameplay.c` (command in
  `tests/GAMEPLAY.md`) and the existing `tests/ESR_SurrenderGameplay.c`,
  `tests/ESR_RevealGameplay.c`, `tests/ESR_IntelRevealGameplay.c`,
  `tests/ESR_CommanderGrenadeGameplay.c`, `tests/ESR_SuppressGameplay.c` and
  `tests/EUD_DialogCacheGameplay.c` (it shares the survivor carry);
- a GM session on a dedicated-server client: both tabs appear on an AI squad
  and an AI soldier (and not on player squads), multi-selected squads, values
  read back after reopening, a soldier with Return Fire Only holding fire in a
  Fire on Sight squad until fired upon, his own warning shots at a player;
- Garrison Full and Simulation caching of a squad with overrides;
- a native save and reload and a CDF save and load of squad and soldier
  overrides, and a 0.1.9 save loading with none.

## Ambient Sounds: Vinny sound modules (0.1.3, GM UI)

Build `local-20261005-123008-430` compiled with no script errors. Workbench play
on GM_Eden (2026-10-05): all 28 "EXPBG Sound: ..." entries appear on System
browser pages 8-10 with radio, war, crowd and EXPBG cards. Muslim prayer
(imported Vinny recording, 1000 m) played `SOUND_EAS_VINNY_MUSLIM_PRAYER_R1000`,
finished after 165.7 s and looped; Russian radio 3 (reused converted recording,
50 m) started once the camera was inside its 50 m range
(`SOUND_EAS_VINNY_RUSSIAN_3_R50`). Finite sources start only for listeners inside
their audible range (existing radio behaviour). Not covered: the other 26 sounds
individually, dedicated server/JIP, save/load.

## No Game Master Budget (0.1.2, GM UI)

Workbench play on GM_Eden with the installed `local-20261005-111422-291` build
(2026-10-05, computer use): Scenario properties -> Game -> "Enable Game Master
Budgets" defaults to Yes. Set to No: log `[EXPBG NO BUDGET] Game Master budgets
enabled=0`; a placed US rifle squad left the AI budget at 0% (limits raised 500x)
and the panel read back No. Set to Yes: `enabled=1` and the AI budget showed 4%
immediately. No script errors. Not covered: dedicated server/JIP replication,
campaign building, save/load of the switch.

## Unit Caching per-casualty cleanup (0.1.2, native)

Fixture `tests/EBG_UnitCleanupGameplay.c`, selected with the runner switch
`-UnitCleanup` (build the pack with `build.ps1 -NonInteractive` first):

```powershell
pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -UnitCleanup -TimeoutSeconds 540 -OrchestratorSlotGranted
```

Add `-Rhs` to also load RHS: Status Quo and both content packs, linked by GUID
from the installed addons (never copied); without it the two RHS cases only log
`available=0`. `-Rhs` also works with a custom fixture run with `-FixturePath`
and `-ExpectResult` (AI Global Skills ammo refill, Unit Scripts RHS discipline).

Cases (letters used below; the fixture runs 14):

| | Case |
| --- | --- |
| A | `sim-partial-awake` |
| B | `sim-partial-cached` |
| C | `full-partial-cached` |
| D | `full-partial-awake-then-cache` |
| G | `sim-partial-wakeband` |
| H | `foreign-item` |
| J | `us-etool-atomic` |
| K | `identityless-gear` |
| E | `sim-all-control` |
| F | `full-all-control` |
| L | `rhs-mg-team` (needs `-Rhs`) |
| M | `cloth-slot-accessory` |
| N | `rhs-usmc-recon` (needs `-Rhs`) |
| I | `sim-death-while-cached` |

The letters match the fixture source. Each case spawns a real USSR rifle squad
with its own cache zone (Affected 40, Wake 60, Sleep 200, clear delay 5), kills
one, two or all six soldiers with native damage, and moves an injected player
presence away. Case I kills one soldier only after his squad is Simulation
cached. A scripted kill of a suspended character does not complete native death
while the squad sleeps, so the fixture gates only safety for I: the body is never
deleted by EBG and survivors stay alive (known limit; sleeping units are hidden
and untargetable in normal play). Deletions are attributed to `EBG_CacheCleanup.Tick` together
with the cache state at deletion (0 awake, 1 Simulation cached, 2 Full cached).
K gives one casualty a worn vest stripped of its native identity and an
unregistered WeaponPart_Base stock on his weapon (the RHS preset-vest and
weapon-part situation without RHS). L is the RHS AFRF machine-gun team (PKP,
AK-74M parts and the 6B45 preset vest without native identity). M is the
vanilla stand-in for B1: the first non-leader whose worn cloth carries an
accessory in a storage-less `LoadoutSlotInfo` (a stock Lifchik or 6B3 canteen)
dies, and a test seam reports that cloth to `NativeVestAccessoryOwner` as
unlisted. N is the RHS USMC MEF recon team (scout and scout RTO, both in
`Hat_USMC_Boonie_Comtac` with the Peltor headset in its storage-less Comtacs
slot). M and N must show, just before eligibility and after an explicit
transfer check, every such accessory still held, owned by its wearer and not a
protected ownership chain, then the bodies deleted with cloth and accessory and
no UUID-less lineage.
The runner's `Test-UnitCleanupEvidence` requires one distinct
`[EBG CLEANUP TEST RESULT] ... failures=0 cases=14 reason=complete` line (stdout,
`console.log` and `script.log` each repeat it), `state=0 whileCached=0` for A and
D, `state=1 whileCached=1` for B, `state=2 whileCached=1` for C, the I
`[EBG CLEANUP TEST SNAPSHOT]` line (`heldWhileCached=1 restored=1 deletedByEbg=1`,
or `knownLimitation=death-not-confirmed-while-suspended deletedByEbg=0`), the H
`[EBG CLEANUP TEST FOREIGN] ... present=0 firstByEbg=1 secondByEbg=1 foreignGone=1`
line (an unregistered magazine inside the body is deleted with it), the J
`[EBG CLEANUP TEST ATOMIC] ... sameTick=1 partialStrips=0` line, the K
`[EBG CLEANUP TEST IDENTITYLESS] ... blocking=0` and
`[EBG CLEANUP TEST IDENTITYLESS DELETED] ... lineage=0` lines, the M
`[EBG CLEANUP TEST CLOTH SLOT] case=cloth-slot-accessory available=1
accessories=n unlisted=n held=n wearerHolder=n protectedChain=0 blocking=0` and
`[EBG CLEANUP TEST CLOTH SLOT DELETED] ... accessoriesGone=1 clothGone=1
present=0 proven=1 lineage=0` lines, L and N passing with `-Rhs` (otherwise
their `available=0` lines), no `[EBG CLEANUP KEEP]`, no
`[EBG CLEANUP TEST SURVIVOR DELETE]` or `[EBG CLEANUP TEST PARTIAL STRIP]`, no
script errors and a clean shutdown. The fixture deadline is 400 s of world time; `-UnitCleanup` defaults
`-TimeoutSeconds` to 540 and refuses less than 480. Portable verifier cases:
`tests/Test-GameplayEvidence.ps1`.

The Full cases need `-worldSystemsConfig
{8DDC2A311929D52F}Configs/Systems/GameMasterSystems.conf`, which
`Run-Gameplay.ps1` passes. Workbench World Editor play does not use that
systems config, so Full caching cannot be exercised there.

Expected on 0.1.1 source: A, B, C, D, the second half of G, H and the final
deletion of I fail; E and F pass. If I fails only its "(precondition)" check,
the squad woke by itself after the cached death; that run is inconclusive for I,
not a cleanup result. Expected after the per-casualty change: all pass.

Results (2026-10-05):

- Before, on the 0.1.1 cleanup code (`build/pack-review-20261005-0715`, run
  `build/gameplay-20261005-104816-409`, first eight cases): E/F whole-squad
  controls deleted all six bodies; every partial case (A, B, C, D, G, H) ended
  with `remaining=1`, H with `held=1 garbageProtected=1 nativeInserted=0`.
- After (`build/local-20261005-111422-291`, run
  `build/gameplay-20261005-112252-503`): `checks=83 failures=0 cases=9`, runner
  verifier PASS. A deleted 32.5 s after leaving, D 33.6 s, G after the wake band
  was left, B while Simulation cached (`state=1`) and C while Full cached
  (`state=2`) after their 150 s corpse age, E and F six bodies each, H released
  to native garbage after three attempts (`[EBG CLEANUP RELEASE]`), I known limit
  as above. No survivor was deleted.
- 0.1.4 rule (user decision 2026-10-06): a casualty is its body plus its own
  loose items. The body is deleted in one native call that takes everything
  inside it, its dropped weapon in the same tick; no per-item checks. Only a
  keep component or valuable intel parks a casualty (`[EBG CLEANUP KEEP]`), and
  an unconfirmed native delete is retried three times before
  `[EBG CLEANUP RELEASE]`. H now expects both casualties deleted whole.
- The fixture must keep the spawned entity in a local `IEntity` before casting;
  the inline `Cast(SpawnEntityPrefab(...))` form returned null in native runs.

Separate open gates, each reported on its own:

1. A real GM session on a server launched with GameMasterSystems, in both
   Simulation and Full modes: kill one AI, walk beyond the yellow Wake ring, see
   the body disappear and "bodies cleaned" increment, return, and see the
   survivors restore with no refill. Not yet run.
2. Prepare for Save, save and reload with a living record whose casualty was
   cleaned: no PersistenceIssue, the casualty stays dead, no respawn. Not yet run.
3. Portable tests and `build.ps1` passed (`local-20261005-111422-291`); Garrison
   contracts passed (`build/contracts-20261005-112707-872`); the Garrison default
   world run passed 59/0 with four guards and both Full cycles
   (`build/gameplay-20261005-112714-928`).

## Pack 0.1.1 published Unlisted (2026-10-05)

Workshop `FC1402F65B2F4A45` 0.1.1 (tag `v0.1.1`, commit `08d29f3`) was
registered through Workbench **Publish Project**; backend processing succeeded.
The UI bundle's `data.pak`, project and preview matched the prepared package
and the live listing matched the prepared metadata. The receipt keeps
`packageVerified=false` only because the first-publication dialog has no
change-note field. Custom license needs a root `license.txt`; Workbench copied
`addon/ambient-sounds/license.txt` into the frozen stage (not into `data.pak`).

GM UI session on the installed 0.1.1 build (GM_Eden, Workbench play, computer
use; no script errors or VM exceptions in the session log):

- Ambient Civilians: placed from the browser, attributes open; theme prepared
  and a civilian group appeared after a player avatar was placed nearby.
- Ambient Destruction: placed, enabled in attributes; a burnt BTR road wreck
  appeared inside the zone.
- Unit Caching: zone tab reads "EXPBG Unit Caching"; enabled Simulation cache
  hid a fire team while the player was teleported about 2 km away and restored
  it at the same positions on return.
- Ambient Sounds: War module enabled with Debug; range rings shown and `[EAS
  DIAG]` lines show firefight/explosion clips scheduled, played and released.
- Garrison: Add Garrison -> picker -> Fire Team on a Le Moule house; the
  4-man team spawned and initialized inside.
- Not exercised in this UI: Intel Items (GM_Eden's browser offers no Object
  tab and its type filters did not respond to mouse input) and Persistent
  Battlefield body/wreck lifetimes (only its reconnect identity cache logged).

## Pack 0.1.0 status (2026-10-05)

GM UI and building coverage (Workbench play with computer use, then native survey):

- Real GM flow: right-click building -> EXPBG Add Garrison -> "Choose Garrison Squad"
  picker -> squad selection. Two defects found and fixed: the picker was never
  bound (`GetTopMenu()` never returns dialogs; now `FindMenuByPreset`), and the
  server refused every vanilla squad (no vanilla group prefab has
  `GROUPTYPE_INFANTRY`; the roster is now checked for editable characters).
  The selection then reached analysis and capacity, and on a sloped row house
  the editor placement/budget check refused. The placement check now skips the
  preview transform (the building origin can fail its terrain-height test; the
  building plan validates positions) and logs which check refused.
- `tests/EXPG_BuildingSurvey.c` runs the production planner on one building of
  each distinct prefab around Montignac, Saint-Philippe, Levie and Morton
  (`tests/Run-Gameplay.ps1 -FixturePath tests/EXPG_BuildingSurvey.c`). Before:
  most houses had floor samples but no entrance (only axis probes with terrain
  within 0.45 m). Door entrances (straight doorway sweep, porch walk-out to
  terrain), door glass excluded with its leaf, terrain floors inside authored
  interiors, open gateways and larger bounds raised coverage to 91 of 160
  prefabs; nearly all remaining ones are non-enterable (graves, walls, bridges,
  tanks). `Angles[1]` is now used as yaw (it was pitch). Building analysis gets
  about 4 ms per 100 ms pump instead of a fixed 32 steps.
- Open: `Barn_E_01` (open barn) and the open `ShedMetal_02` have no positions;
  the castle keep at Montfort has none.

- Portable: `tests/Test-Tools.ps1` passes with the assembled pack (843 files,
  three merged overrides, no duplicate GUIDs).
- Native compile: `build/pack-floorfix-20261005-0130/` compiled all seven modules as one
  Game module with no script errors (stock obsolete warnings only, plus the
  existing `EAC_SessionLifecycle` Serialize warning).
- Workbench via Enfusion MCP (installed pack, 2026-10-05): project and NET
  handlers compiled without script errors; GM_Eden loaded with every module
  prefab placed; play mode initialized Ambient Sounds, Ambient Destruction and
  the Unit Caching controller/zone with no script errors. Interactive GM use was
  not exercised (no UI control).
- Garrison contracts in the pack: `build/contracts-20261005-011102-720/` passed.
- Garrison default world run in the pack: `build/gameplay-20261005-013118-511/`
  59 checks, 0 failures, runner passed: four guards, Simulation sleep/wake, two Full cycles
  (4 then 3 survivors, exact world transforms and assignments), real casualty not
  refilled, crouch/stand, body turn and head aim, Force Move release.
- Fixed: `build/gameplay-20261005-012655-973/` released a garrison after the
  casualty's helmet landed on a neighbouring post and the floor-support trace
  hit it. `Supported` now uses the native CharacterAI collision mask, like
  `ClearBody`; both world runs above use that build.
- The runner tolerates only the stock `SCR_BaseResupplySupportStationComponent`
  catalog error when GM_Eden tears down after the fixture result; any other
  error, or that line earlier, still fails.
- Garrison 12->9 fresh-trim world run in the pack:
  `build/gameplay-20261005-013453-823/` 101 checks, 0 failures, runner passed: twelve real
  native members reduced to nine originals in the same group with the leader,
  three fresh members deleted, both Full cycles (9 then 8), casualty not
  refilled, stance/look and Force Move release. Earlier attempts exposed two
  fixture timing bugs, now fixed: the motion probe waits for native activation,
  and recreated actors are compared with the original awake presentation instead
  of release-frame flags (`build/gameplay-20261005-011903-728/` shows every
  released member visible, traceable and unowned).
- Open: the body-clearance negative control (`tests/EXPG_BodyClearance.c`) has
  never identified its spawned wall (`build/gameplay-20261005-010839-185/`); GM
  right-click/picker UI, combat and patrol movement, multiplayer/JIP, CDF with
  the pack, and gameplay of the other six modules inside the pack.

Older Garrison evidence follows.

Earlier portable tooling, GitHub CI, native compile and scalar contracts passed
on 2026-10-05. Native artwork import and Resource Browser preview passed.
The original real house fixture failed capacity before spawning actors: closed
native doors isolated the interior, leaving two reachable exterior porch samples.
That evidence remains under `build/gameplay-20261004-181506-740`. The revised
door-aware planner, standing/interior checks, fresh-squad trimming and Full-cache
integration compiled in `build/root-garrison-full-20261004-1824/`; contracts passed
in `build/contracts-20261004-182429-417/`. The house still had only two slots in
`build/gameplay-20261004-182511-833/`. Boundary diagnostics in
`build/gameplay-20261004-182839-199/` proved an omitted diagonal edge clear of both
body and floor obstacles. The resulting eight-neighbor correction, exact fresh
roster tracking, interior patrol checks and Full failure-path fixes compiled in
`build/root-garrison-full-20261004-1840/`; contracts passed in
`build/contracts-20261004-183804-041/`. World run
`build/gameplay-20261004-183828-944/` reached nine safe slots, four actors in one
group and five seconds of fixed-post retention, then failed on premature release
before Simulation sleep. Later native build
`build/root-garrison-posthold-20261004-1855/` passed compilation, but native run
`build/gameplay-20261004-185751-652/` proved that the controller's zero movement
input and IDLE wanted/override values did not prevent displacement. GuardHouse
run `build/gameplay-20261004-190234-938/` measured 23 slots and admitted four fresh
actors in one group, then failed the same retention check. It supplies no actual
roster-reduction evidence. A labeled test-only native maximum-speed hypothesis
in `build/gameplay-20261004-190538-307/` passed awake retention, Simulation sleep
and wake, and one Full restore of all four survivors: world transforms matched
within 0.001 at each observed Poll and before release, and assignments were bound
before resuming AI. The run stopped at the fixture's undocumented Boolean-return
assumption about `SetHealthScaled(0)`, before observing actual death. It exited
normally but did not pass the fixture. Production speed-cap build
`build/root-garrison-speedcap-20261004-1913/` and contracts passed. Its world run
`build/gameplay-20261004-191609-524/` passed Simulation and the first Full restore,
then held the second restore at the position-safety gate. Death-order build
`build/root-garrison-deathorder-20261004-1934/` also compiled successfully; it
recognizes the native destroyed damage state before group-transfer checks.
Diagnostic run `build/gameplay-20261004-194816-251/` uses the supported damage
`Kill` path and observes actual controller death, ledger retirement and three
native survivors. The second restore recreates those three at their captured
world transforms. Its broad body query hits the casualty's helmet and vest,
while otherwise identical CharacterAI and Character collision queries are clear.
ClearBody now uses the native CharacterAI interaction matrix; structural support
and geometry checks remain required. Build
`build/root-garrison-charcollision-20261004-1956/` compiled successfully.
`build/gameplay-20261004-195632-008/` then passed the complete default fixture:
native exit 0, receipt `passed=true`, 57 checks and zero failures, normal shutdown
and no SCRIPT (E) entries. The real house still measured nine positions; four
actors retained one group and their fixed posts through Simulation and both Full
cycles (four survivors, then three after native death). Captured world matrices
and assignments matched. Native crouch/stand state, head aim and separate body
rotation passed while held; Force Move removed controls and stayed released for
ten seconds. This does not establish arrival at the Force Move destination.
The same diagnostic shows deferred LOD activation settling on the next frame;
no LOD-policy correction is justified by that evidence. Real combat/firing,
fresh-roster reduction, blocked patrols, other buildings, interactive GM actions,
distance-triggered wake, multiplayer/JIP and publication remain open. Portable
structural checks passed. A separate real-wall collision control is prepared.
Actual gameplay acceptance remains open. Do not publish an
unvalidated payload as a completed release.

| Gate | Required evidence |
| --- | --- |
| Portable tooling | Test-Tools.ps1 exit 0; staging contains only runtime inputs |
| Native compile/import | build.ps1 exit 0, owned script/resource errors absent |
| GM transaction | Right-click building; on a new building the analysis progress hint, then the picker by itself (at once on an analysed one); cancel by a second Add Garrison, retry; a second GM on the same building joins the analysis and waits for the first one's picker; exactly one squad |
| Placement | Windows/doors/stairs, rotations, disconnected floor and roof rejection |
| Behavior | Guards hold during combat while aiming/firing/crouching; overflow stays inside |
| Orders | Real Force Move releases active/cached/restoring squads permanently |
| Caching | Simulation retains original actors; two Full cycles preserve captured transforms/assignments and exclude casualties, no duplicate owner |
| Lifecycle | Collapse, deletion, possession, transfer, GM disconnect during the analysis wait or the picker |
| Ownership transfer | Both directions between Optimizer and Garrison while sleeping; pending regroup cannot commit until original snapshots and controls release |
| Save admission | No intermediate Ready publication; Enable/Resume stays blocked until release; native GM and Optimizer-CDF export refusal |
| Multiplayer | Dedicated server/client and JIP agree on one group and state |
| Assets | Native DDS and actual browser/banner rendering |
| Publication | Immutable Git tag, Unlisted receipt/listing and exact package hashes |

Use disposable profiles and fixtures. Obtain native ownership from MOD ISSUES
Orchestrator before any Workbench/client/server launch. Existing GME or Optimizer
fixtures prove only their tested contract, not this new garrison behavior.

`tests/Run-Contracts.ps1` runs graph connectivity/reservation geometry, editor
ticket replay/expiry, attribute bounds, safe-capacity selection, CREATED-slot
no-replenishment and route corridor contracts in an isolated
ResourceManager copy. It requires an indexed source snapshot and an explicit
`-OrchestratorSlotGranted` acknowledgement. This does not launch a gameplay world.

Cold restoration of garrison assignments is not supported. Check the native
save refusal and Optimizer-CDF refusal while active, then Prepare for Save,
await survivor restoration and ownership release, and save ordinary squads.
CDF without its Optimizer companion bypasses the save hook: caching must refuse
to suspend actors in that configuration.

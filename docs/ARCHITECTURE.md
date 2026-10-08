# EXPBG GM Tools design

## Pack assembly

EXPBG GM Tools is one engine project (`addon/EXPBG_GM_Tools.gproj`,
`FC1402F65B2F4A45`) built from seven module folders listed in `tools/pack.json`.
`tools/Assemble-Pack.ps1` copies every module file to its original runtime path,
so resource GUIDs and `{GUID}path` references are unchanged. Three vanilla
overrides are shared and merged in module order: the `Edit.conf` attribute list,
the `Systems.conf` placeable registry and the persistence `GameMaster.conf`
(both `Configurations` and `PersistentStates`). Only the Ambient Sounds banner
PNG and two `license.txt` files are relocated. Anything else that collides,
duplicate metadata GUIDs, module `.gproj` files and Workbench-only scripts stop
the assembly. The pack project registers all five string tables.

Unit Caching (former GM Optimizer) keeps its `EBG_` classes. Its Full save gate
accepts the pack identity, and Unit Caching and Garrison check the reserved pack
CDF companion `07BC942D90324CD9` instead of the standalone Optimizer companion.

The contracts below describe Garrison. The other modules' contracts live in
their original repositories at the commits recorded in `tools/pack.json`.

## Product contract

One native SCR_AIGroup per garrison. All retained infantry remain members of it.
The GM context action starts (or joins) the building's one bounded analysis and
opens the native squad picker only when the plan is ready; until then the server
sends that Game Master the analysis progress (owner RPCs). Plans are cached per
building, never per building type (the result depends on terrain, neighbours,
doors and damage). Server validation owns the final selection, placement and
roster. Keep no more than one open squad picker per building (later requests
wait for it, oldest first). Each EXPBG Add Garrison on a garrisoned building adds
another independent garrison that shares the building's plan.

Prefer authored sentinel positions, then physically checked interior positions:
window watchers (the nearest clear position 0.4-3 m from each window), then
positions 2-4 m from outer doors and at least 1.2 m deep on the room side (a
door with an enclosed room on one side only; interior doors are not watched),
diagonal to the doorway first, facing it, then entrance approaches and stair
ends, then patrol starts. Each fixed kind is taken one per window or door and
2.5 m apart first, then 1.5 m apart; storeys take turns and each post goes where
it is farthest from the posts already chosen, so a squad spreads over every floor.
Every accepted position must be on
the building floor, have character clearance and a connected entrance route.
Nearby navmesh alone does not prove entrance connectivity. Reject uncertain
locations, disconnected floors and roof surfaces without a usable entrance path.
Closed native doors with Soldier navigation links are traversable for planning;
their frames, walls and furniture still block traces. Spawn occupancy never
ignores a door leaf. Standing-volume overlap, structural support and interior
volume checks prevent using furniture tops as posts. An authored interior volume
can cover porches and entrance steps, so in a building whose indoor floor is
mostly enclosed a post, patrol stop or added position also needs building walls
on six of eight bearings at eye height; open sheds keep their roofed floor.
The grid checks eight neighbors, including diagonals around furniture, with the
same body sweep and intermediate floor checks on each connection. Stair treads
and ramps, where the floor itself steps and a body fits above step height, are
transit-only nodes; neighbouring stair nodes link up to 45 degrees when the floor
between them climbs in steps of at most 0.3 m. Patrols stop only at interior
roam stops and walk only on the indoor floor; reachable exterior entrance
nodes establish access but cannot become patrol destinations.
Cancellation and unsupported buildings create no orphaned squads or reservations.

Every door leaf of the building has one keep-out zone (`InDoorZone`): its swing
disc, leaf width plus 0.6 m around the native hinge on both sides, and the
doorway passage, 0.3 m beyond the leaf and 1.5 m deep on each side, on the
leaf's floor. No fixed post, entrance post, added building position, position
around the building, patrol stop, alarm window or watch point lies in a zone;
walking through stays allowed. A guard on a post cannot step aside for a door.

Overflow soldiers patrol inside. An added squad takes free fixed posts, then
patrol starts under the free-room rule, then extra window, door, entrance and
stair positions that keep 1.5 m from every roam stop, then places on rings 1 m
to 24.75 m around the walls, then its spawn point. Indoor walking nodes are enclosed interior
nodes plus stair and aside nodes whose stair-connected group touches only such
nodes; their connected areas are walk groups. Up to 96 roam stops (not stairs,
not door zones, 1.5 m from every fixed slot and from each other, farthest first,
storeys taking turns) each face the longest indoor view (a hallway or doorway).
A patroller's single claim (his stop, or his destination while walking) keeps
1.5 m from every other claim, post and parked post of a Full-cached garrison;
the first claimant wins. Patrollers join a walk group only while at least
max(2, a third) of its stops stay free. A patroller dwells 10-30 s, then walks
(walk speed, normal priority) to a free stop of his walk group 5-20 steps away,
mostly on his storey. The native path is admitted only when every point and the
line between points stays on the indoor floor (within 0.7 m of an indoor
walking node) and clear of parked posts; each input frame re-checks the next
step and the momentum with O(1) lookups. A failed walk stops at a nearby stop
and skips that destination for two minutes.

Alarm: per-guard threat state reaching ALERTED (`SCR_AIThreatSystem` invoker)
and the squad's filtered enemy detection (`SCR_AIGroupPerception`) only raise a
flag; the shared scheduler polls at most 32 threat states per garrison and
keeps one alarm per building. While it is under 60 s old, at most two
patrollers per garrison and Tick claim the best of three free windows (steps,
then facing the known threat), else a free door or stairs watch point within
12 steps, else hold their stop, and run there at player priority to hold it
like a post; holders without a window retry every 5 s. Posts freed by
casualties are claimable during an alarm (body clearance permitting). After
60 s without an alarm one holder every 3-8 s returns to patrol.

Fixed guards stay at their assigned position, retaining aim, shooting, rotation
and stance changes. Overflow patrols walk between claimed interior stops; native
paths that leave the building are not acceptable. Dwelling and holding
patrollers are capped like posts (combat moves become stance changes). No soldier
is ever deleted to fit a building (0.1.11): the first squad takes the planned
posts and patrol starts, and soldiers beyond them follow the reinforcement order
(free fixed posts, interior patrollers under the free-room rule, more watch
positions, close rings around the building, the spawn point). The fresh-roster
transaction still tracks exact native-spawned actor identities before editor
callbacks; foreign additions, removals or same-count substitutions release the
squad as ordinary AI. Zero safe positions still produce an honest refusal.
Force Move ends garrison enforcement once; ordinary simulation resumption is not
a GM order. One guard's problem never releases the others; it is handled for that
guard alone. A guard displaced more than 0.5 m (ragdoll, unconsciousness, blast,
push, carry, a Game Master move) holds where he comes to rest once still when the
spot is plausible (`EXPG_GarrisonMember.Plausible`: within 6 m of the post he was
given; for a building post on the indoor floor, inside the walls, with a floor
under him; for a post outside on walkable ground at about its height): his post
becomes that spot (`EXPG_GarrisonMember.Anchor`: the nearest free standing node
within 0.5 m keeps a reservation, otherwise an off-plan post). Any other spot (the
roof, the yard, far away) never becomes a post: once he is conscious and still he
is teleported back to his post. He is never released for it. Placement moves each
soldier with the editor's transform, which lands a frame or more later: a guard is
bound only once he stands on his post or stop (within 1 m), the move is sent again
every second (five times, then every 5 s), and nobody is anchored on the way.
Binding accepts a guard within 1.5 m of his post and anchors one farther away (if
plausible) or whose node another guard holds. A possessed guard
is free while the player has him and holds where he is left; a living guard in
another squad is forgotten; a deleted guard (or one a Full restore did not
recreate) is dead and never respawned; a lost control is rebound for that guard;
a floor lost under a post only holds caching. A patroller whose control cannot
start for 10 s becomes a fixed guard where he stands. A whole garrison is released
only through `RequestRelease(reason)`: Force Move, the Release attribute, Release
All Garrisons, Unit Caching regroup or Prepare for Save (CDF legacy mode only), a
deleted squad (also a recreated one deleted during a Full restore), a moved or replaced
building, no surviving guard, or a refused initialization. `FinishRelease` logs
`[EXPG Garrison] group=... released (reason)` and the group status keeps
`Released: reason`, which the Game Master's status attribute still shows.
Each fixed post owns a native character maximum-speed limit in addition to its
AI setting. The character combines limits from all sources; release removes only
the post's entry. Player input releases the post in the first controller callback.
No repeated position correction or disabling of the whole AI is used.

## Runtime and caching

One shared bounded scheduler; no per-soldier timers or repeated world scanning.
Each building record retains roster identity, local post transforms, route state,
cache lifecycle and release intent. Defaults: wake 300 m, sleep 400 m, adjustable
per garrison; these are starting values, not measured optimal distances.
Active combat, a building alarm under 60 s old or player possession prevents
sleeping. When sleep is due but held (a guard Unit Caching cannot suspend, such
as an unconscious, bleeding, possessed or once-possessed one; a guard not under
garrison control; a lost floor; a soldier who is not a guard; an alarm), the
status and the server log say `Cache held: <reason>` once caching has been held
for 30 s (when it would have slept; a changed reason at most every 10 s); Full
refusals name the guard or the squad reason. Sleep also waits (silently) until every patroller dwells at his stop;
after 20 s a walker stops at a stop near him. Full captures a patroller's claimed
stop only; he wakes there as a patroller (within 1.5 m, or the nearest free stop
within 1 m). Simulation keeps his control and claim. Exactly one cache owner
may manage a garrison, including overlapping Optimizer zones.

Version 0.0.1 supports Off, Simulation and Full (default), using each garrison's
wake and sleep distances. Simulation retains the native group and original actors,
pauses their simulation and resumes those same actors; no soldiers are deleted
or recreated. Equipment and casualty state remain on the retained actors.
Full reuses Optimizer's `EBG_PrefabFullCache` transaction in its durable form
(0.1.11): the squad is captured (`EBG_CacheGroupSnapshot`, with its orders and
the squad overrides) and deleted with the survivors, and recreated at wake; the
recreated squad is rebound to the record in its spawning call
(`AdoptRestoredGroup`: settings, status, Force Move subscription, save
exclusion). Settings live in the record (`CacheMode`, `WakeDistance`,
`SleepDistance`), mirrored from the squad while it exists. A squad that cannot
be captured (unsupported orders or AI settings, editor protection) is cached in
Simulation instead, and Full is retried ten minutes later. Only current
survivors are captured; recreated actors use prefab-default kits and health.
Post identity and patrol route state survive actor replacement, including a
guard's anchored or off-plan post. New actors receive an owned LOD hold, then
their controls before native AI release. The wake stays gated by binding the
controls; a survivor whose restored spot fails the floor or body check is
accepted where he stands after 10 s, and a survivor that was not recreated counts
as dead. Actual spawn
timing, world-transform parity and client behavior require native acceptance.
Partial deletion/restoration retains its recovery transaction; a previously
created slot or retained original is never spawned again after external removal.
An ungrouped newly created survivor whose first native group admission failed
remains held for recovery, distinct from an actor transferred externally. Original-group
deletion or foreign membership during restoration remains a recovery hold,
including save refusal, rather than silently creating another group.
If safe ownership cannot be established, retain live AI and report the refusal.

## Garrison persistence (0.1.11)

The garrison owns its soldiers in every save. A Ready garrison's squad, waypoints
and living guards are kept out of native persistence (`StopTracking`, repeated in
`SCR_PersistenceSystem.GetOnBeforeSave`) and, with the EXPBG CDF Compat bridge,
out of CDF (`NON_SERIALIZABLE`; the bridge's `IsManaged` makes CDF clear them on
load) by `EXPG_SaveExclusion`, the Ambient Civilians seam. Whatever stops being
owned (possessed, surrendered, regrouped, dead, released) is handed back within a
second; only flags set there are cleared. A squad the editor protects from
deletion is not portable and saves as an ordinary squad.

The ledger (`EXPG_Snapshot.c`, schema 1) records per garrison: token,
`GeneratedBy` (the Random Garrison zone token; read with a default, so ledgers
without it load), record order, building
(prefab and transform, found again with the Ambient Destruction identity rule),
cache mode and distances, cache state (awake, Simulation, Full), release request,
leader, the squad (`EBG_CacheGroupSnapshot.CaptureForLedger`, tolerant: orders it
cannot own are left out) and one row per member, casualties included: post kind,
fixed or patrolling, saved node (a hint), post, look and last position in
building-local coordinates (world fallback), prefab, author and the AI Surrender
and AI Global Skills soldier overrides (module seams `CaptureCarry`/`FillCarry`).
Not saved: controls, reservations, parks, holds, Simulation snapshots, timers,
status, creator. Limits: 32 members, 64 buildings, 1024 garrisons, 16 MB.

Carriers: `EXPG_GarrisonPersistenceState` with `EXPG_GarrisonPersistenceSerializer`
(merged `GameMaster.conf`; an unreadable ledger never fails the native load, Game
Masters are told) and the EXPBG CDF Compat bridge (world-state envelope
`expgGarrisons`). `EXPG_GarrisonPersistence` is the API: `CanExport`, `Export`,
`ExportJson`, `OwnsForSave`, `SyncSaveExclusion`, `BeginImport`,
`DiscardForImport`, `QueueImport`, `FinishImport`, `EndImport`, `ParseJson`,
`Notify`. Modes (`PersistenceMode`): native (no CDF), CDF bridged
(`CdfBridgeVersion() == BRIDGE_API`, overridden by the bridge) and CDF legacy
(0.1.8 rules for CDF; the native ledger still works).

Save: nothing is spawned or woken; awake and Simulation garrisons are read from
their actors, Full ones from their transaction (rows not yet recreated from the
transaction); a guard far from his post is saved on it. A queued import is written
back verbatim. Saves are refused only while a load replaces the garrisons (and in
legacy mode for CDF while one is active).

Load: Add Garrison is refused meanwhile (`IsImporting`, also while a native load
waits for persistence to become active). A CDF load with clearBeforeLoad discards
every old garrison without waking or respawning anyone. Each saved garrison
becomes a record with a Full CACHED transaction made from the ledger (dead rows
stay dead) and nothing in the world; it waits for its building's analysis (first
when it must wake), remaps posts and stops by position (saved node within 0.1 m,
else the nearest standing node within 0.25 m, else an off-plan post; a stop that
cannot be found becomes a fixed post) and then wakes (saved awake, or Simulation,
which caches again at once when no player is near) or stays Full cached until a
player comes within wake distance. Survivors are pinned (maximum AI LOD) in their
spawning call before they join the squad and released only after they are bound
to their posts. A missing or ambiguous building restores the survivors as an
ordinary squad at their saved world positions.

## Random Garrison

`addon/random-garrison` (`EXPGR` folder, `EXPG_RandomGarrison*`/`EXPG_RG*` classes,
log prefix `[EXPG RANDOM]`). One Systems entity per zone; all work runs on the
server; clients only receive the radius (GM area mesh) and the status line.

Ownership. Every squad is an ordinary garrison created through the shared spawn
API (`EXPG_GarrisonSpawn.c`: `EXPG_SquadPrefab.Validate`, then
`EXPG_GarrisonSpawner.Spawn`: CanFit, spawn, fresh roster, AdoptFresh), the same
path EXPBG Add Garrison takes. The zone writes its token (`rg:<hi>-<lo>`, two
24-bit halves) into the record's `GeneratedBy` and keeps no list of its squads:
`EXPG_GarrisonManager.CollectGenerated(token)` finds them, also after a load (the
ledger saves `GeneratedBy`; ledgers without it load with an empty value). Caching,
settings, Force Move, alarms and interior behaviour are those of any garrison;
casualties are never refilled and a zone never generates again by itself.

Flow. A static director (100 ms, at most 64 zones round robin, about 2 ms of zone
script per tick) runs census, catalog and generation. Census: 64 m cells over the
circle's bounding square, one spatial query per cell (512 callbacks, quadrant split
on saturation), then at most 32 eligibility checks per tick (whole intact building,
not a ruin or part, not an Ambient Destruction collapse, inside the planner's
limits, no rejected prefab path, doors or an interior volume). Eligible buildings
are sorted by position and prefab, shuffled with the seed; buildings near players
(bounds plus 5 m) are left out after the shuffle, the first Target are queued, the
rest are reserves. A building that fails is replaced by the next reserve, up to
`EXPG_RGRules.AttemptCap` tries (three times the target, at least the target plus
16, never more than the eligible buildings). A reserve whose type (prefab and drawn
faction) failed in this run for its posts or rooms, and never took a squad, is put
back for a second pass after the first one. While an earlier building of its type
is still analysed the choice waits, so every choice depends only on the buildings
before it in the order and a seed repeats its generation whatever analysis
finishes first. Each building draws its faction and squad count from its own
generator (seed and building key), so results do not depend on analysis order.
A generation runs with the settings it started with (Generate copies them; later
edits wait for Regenerate, cache settings apply at once). Before any analysis every
faction a building can draw must have a squad of an enabled size. One faction per
building: with Allow garrisoned a building keeps the faction already in it, and one
held by another faction (or by several) is skipped.
Analyses are background plan waiters: the zone never steps a plan, the manager's
pump keeps its 4 ms budget, Game Masters' requests go first, and no analysis starts
above 56 plans. Squads: one spawn every 1.5 s across zones, at most two spawning at
once, one at a time per building; the squad must fit the building's planned posts
minus the soldiers already there, so fresh squads are never trimmed. A squad that
does not take its posts within 60 s is deleted (one retry with a smaller size).
A drawn squad waits while the AI limit leaves no room for it; after 60 s the
generation stops (Stopped with the reason, never Done afterwards). When a
generation ends or stops, the status counts the failed buildings by reason, the
server log line counts the buildings tried, and the Game Master who started it
gets one notice (`EXPG_Notice`, hint and chat); a Game Master who stops another
one's generation is told too.
Clear discards the zone's garrisons one per tick (`EXPG_GarrisonManager.Discard`,
nobody woken or respawned) and deletes their squads and soldiers 8 entities per
tick; soldiers who left a squad (surrender, possession) are kept.

Saves. Squads that have not taken their posts, and every squad or soldier waiting
to be deleted (Clear, Stop, a deleted zone: `EXPG_RetireList`, handed over from
`EXPG_SaveExclusion` in the same call), are kept out of every save until deleted
(flag and native tracking, repeated before each native save); a Ready garrison
saves itself in the garrison ledger. The zone is saved by
`EXPG_RandomGarrisonSerializer` (`rgVersion` 1: settings, faction keys, token,
generated, last seed, outcomes; generated is true once a squad of the zone has
taken its posts, also in a save made during a run) and,
for CDF, by its attributes (durable values, faction key hashes, a hidden saved-state
attribute with the token). A loaded zone comes back Stopped (or Idle) and never
resumes. With CDF but without the EXPBG CDF Compat bridge, Prepare for Save stops a
generation first and the released garrisons are no longer the zone's.

## Boundaries

Production depends on base game 58D0FB3206B6F859 and Optimizer F3B7C6FB18AB1F79.
Project identity FC1402F65B2F4A45 is newly allocated, not yet Workshop registered.
Runtime namespace EXPG; all new resources get independent GUIDs and unique paths.
Use native editor selection and server-authenticated requests. Treat delayed
selection/building references as stale until revalidated by the server.

Use the existing EXPBG black/antique-gold screenprint style, small top-left emblem,
small module title, separate square browser card and wide Workshop banner.
Import textures through native Workbench and verify the packaged pixels.

## Reference analysis

Installed Game Master Enhanced 1.3.8 (5964E0B3BB7410CE) was inspected read-only.
Its AddGarrison code creates a separate group per member, assigns a native Defend
order, and deletes members after slots run out. Its random navmesh/floor sampling
does not verify entrance-to-post connectivity. These observations motivate this
original design; no GME scripts, resources or generated positions are imported.

The Garrison ledger supplies post/patrol ownership around Optimizer's existing
Full survivor transaction. It is not a portable CDF snapshot bridge. Prepare for
Save restores surviving soldiers and releases ordinary squads before admission.
While CDF Game Master Save is loaded, `EXPG_GarrisonManager.CacheModeInUse` runs
a garrison set to Full in Simulation (the GM's choice is kept): a CDF
clear-before-load would delete a Full garrison's retained empty group (the
record could never wake and would block every later save) or respawn its
survivors into the loaded scene, while Simulation originals are deleted like any
squad and the record releases. Unit Caching's zone status names squads a
garrison caches itself through `EBG_CacheManager.DescribeExternalCache`.

## Unit Caching casualty cleanup
Cleanup is per casualty. A dead, death-confirmed, never-possessed member's corpse
and only that member's owned rows are deletable after Minimum corpse age, player
clearance (Wake radius around the module, every member and every held remain) and
the continuous clear delay. The same check runs for awake, Simulation-suspended
and Full-cached records, never during Full transitions or recovery. Every node of
a deleted tree must belong to the same dead member, and the root must still be on
the ground, on that body or in that member's own container. One root is deleted
per 0.5 s scan across the server. A root that fails verification three times is
released to native garbage handling, unless its tree holds a keep component, a
mission-protected item or valuable intel anywhere (checked over the whole tree,
not only the first blocker met), which stays protected. A record with nothing
owned left settles and costs O(members) per tick; a casualty whose body is gone
and who has nothing left to delete no longer counts as mature, so a younger
casualty waits on the O(members) corpse-age hold without player-distance scans.

## Time and Weather

`addon/time-weather` (`EXPTW`, `ETW_` classes, log prefix `[ETW]`). Two
always-relevant Systems entities; all state changes run on the server.

Weather Transition. One transition per session (`ETW_WeatherRunner`, statics with
a weak reference to the world's `TimeAndWeatherManagerEntity`, so nothing leaks
into the next mission).

Clouds only change through the engine's weather state queue. It raises every
node's blend and hold to at least 10 in-game minutes, and starts a queued node
only when the node in place stops looping and its hold is over. It cannot blend
from the middle of a running blend. `RequestStateTransition()` on its own
restarted the weather in place (0.1.14 fixtures).

Every cloud change pins (below): `DIRECT_START` is off because the native cloud
probe (2026-10-08) reported `direct=misdirected` in every direct case, the engine
moving to another weather instead of ours. With `DIRECT_START` on, a clean start
puts our node right behind the node in place, which stops looping and gets the
shortest hold (direct). The hold left over comes from the engine
(time left until the next weather, minus our blend). It is capped by the hold
read back from that node (at most 0.2 in-game hours), and the start waits it out
(`DIRECT_WAITS_HOLD`; off, a hold left over of more than 2 s pins at once). A
hold left over that the engine reported counts toward the requested time.
Without a reading the latest start is planned, so rain, fog and wind never lead
the clouds.

The clouds pin instead when something else blends (on the nearer weather), when
our node is no longer next, when the direct start has not taken 2 s after the
hold left over (`DIRECT_MARGIN_S`), or when a blend is running at the start. A
pin node aimed at the current (or nearer) weather and our node go to the back of
the queue, and the pin is set at once with `RequestStateTransitionImmediately`.
That drops everything ahead of it, as vanilla `ForceWeatherTo` and the vanilla
looping toggle do. Our node then blends after the pin's hold. When our node is
first in the queue, `RequestStateTransition()` is asked once (`START_REQUEST`);
if that moves anything else, one more pin undoes it. A transition makes at most
one direct start, two pins and one start request.

Empty weather names are never sent to the engine. Node durations are read back.
A queued node's blend only changes through `SetBlend`, never below the engine's
minimum (or the requested time when that is shorter), so no setter can ask for an
instant switch whether the engine raises it or not. Rain, fog and wind ease from
the clouds' start to their arrival, never ending before the requested time; the
transition takes longer than set when the minimum requires it, and the status
says so. Progress is tracked on the 0.5 s tick with bounded queue scans (eight
entries) and at most three queue dumps per transition. Nodes are never removed
(`RemoveStateTransition` crashed the server).

Rain and fog left to the weather are measured across each pin and logged a tick
later. `HOLD_ACROSS_PIN` (off) would hold them and hand them back at the end. If
our node vanishes and nothing heads to the target for four ticks, the clouds are
left to whatever rebuilt the queue: the end and Stop then leave its queue and
looping alone.

Rain, fog and wind use the replicated overrides, written on the 0.5 s tick only
when the value changed, along an ease-in-out curve. Wind direction turns the
short way round. A value left to the weather that is overridden now blends to
the target state's typical value and is released at the end. At the end, clouds
that have not arrived within 30 s of grace (for example while time is paused) are
set through our own immediate node (counted, logged). The vanilla looping flag
and the automated-wind flag are kept in step through two
`modded TimeAndWeatherManagerEntity` methods, without forcing the state. A new request
replaces the running one from the live values. A cloud blend still running
completes at once on the nearer weather unless it already goes to the same
target (then our node keeps going, resized if it has not started). Keeping the
clouds (no target, "Return to automatic weather") never jumps: a blend of ours
goes on and one of the weather's own is left to run.

Foreign changes. The runner never calls `ForceWeatherTo`, so every call
(Scenario Properties weather or automated weather, mission load, other mods)
stops the transition and releases the overrides it set. The vanilla delayed wind
setters drop only the wind channels. Smoothing of Scenario Properties weather
replaces the instant `ForceWeatherTo` in `SCR_WeatherInstantEditorAttribute.WriteVariable`
with a transition over the newest smoothing module's duration. It applies only to
interactive writes (item, manager, player above 0) and starts one call-queue tick
later, after the rest of the Save. Restores stay vanilla; with smoothing on, the
instant local preview is skipped (the smoothing switch is replicated). The Weather
Transition's own target attribute never previews; it only clears a preview
something else left.

Time Skip. `ETW_TimeSkip.Start` refuses overlap and empty skips, then sends one
reliable broadcast RPC from the module: the host runs the handler itself, and a
dedicated server ignores it (`System.IsConsoleApp`). Clients draw their own
layout at the workspace root (Z order 10000, no cursor or focus), timed by the
local world clock. At fade-out plus half the hold, the server completes a running
weather transition, then sets date (Gregorian rollover in `ETW_TimeMath`) and
time with `immediateChange`. Calls carry a serial. Joiners during a fade see the
normal view.

Persistence. Module settings and the skip text use entity serializers
(`etwVersion` 1). The clock is saved by vanilla persistence. A running
transition is not saved; CDF Compat support is a separate addon change.

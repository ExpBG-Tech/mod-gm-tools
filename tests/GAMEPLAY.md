# Native Garrison smoke fixture

Status: production fixed-post retention **FAILED** on 2026-10-05 during phase 4,
`build/gameplay-20261004-185751-652/`. The eight-neighbor planner found nine slots
and 31 reachable nodes and spawned four actors in one group. Controller callbacks
were bound and repeatedly received zero movement input; native wanted/override
speed was IDLE. Actors nevertheless drifted beyond the fixture's 0.5-metre limit.
`SCR_AIIdleBehavior` alone does not establish immobility: its native tree includes
formation/cover movement. Simulation/Full restoration and Force Move remain
unreached. The separate GuardHouse run `build/gameplay-20261004-190234-938/`
measured 23 slots, admitted four fresh actors in one group and failed the same
retention check. It does not establish reduced-roster trimming.

The labeled test-only `SetSpeedLimit` hypothesis in
`build/gameplay-20261004-190538-307/` passed awake retention, Simulation sleep/wake
and the first Full cycle with four restored survivors and verified captured world
transforms/assignments. It then stopped at an undocumented assumption about the
damage setter's Boolean return. Its result remains failed. Production now owns
the tested speed cap; the fixture override has been removed. Fresh native build
and production verification are pending. The casualty phase now observes actual
death, member retirement and native group count before starting the second cycle.

After a successful indexed candidate build and explicit orchestrator slot handoff:

```powershell
pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot '<indexed addon directory>' -TimeoutSeconds 600 -OrchestratorSlotGranted
```

The runner refuses to launch if an Arma/Workbench process exists. It copies the
candidate and configured Optimizer dependency into a private run directory, records
their hashes, runs the diagnostic server for at most 600 seconds with the command
above (360 by default), and retains logs
and a result receipt under `build/gameplay-*`. Timeout termination is limited to
the launched process after checking its PID, creation time and executable. It never
edits an installed addon. There is no automatic retry or publication.

The derived world uses base GM Eden at `4773.46 0 7094.57`. It spawns the real
`House_Village_E_1I01` building and four-member US fireteam. The production planner
must accept that actual placement and assign a fixed post; there is no fallback
mock graph or actor teleporting by the fixture. If terrain/collision makes that
placement unsafe, the test fails and retains the refusal evidence.

Assertions observe production `Prepare`/`CanFit`/`Adopt`, duplicate-adoption
refusal, all four original entity IDs in the same group, fixed guards within
0.5 metres of their assigned posts, actual Simulation cache state and hidden
presentation, a five-second stationary cache interval, restoration of the same
actors and presentation, and ten seconds of restored post retention. The fixture
then requests two Full-cache cycles on the same group. It observes each recreated
actor in the spawn callback and compares all four world-transform vectors with
the captured transform (0.001 tolerance), plus member identity and assigned post
or patrol state before AI is released. It kills one actor between Full cycles
using native damage and checks that the second restore has three survivors.
After the second Full cycle, a held survivor must crouch and stand through the
native stance helper. A four-second native look action must produce measured
head aiming and a separate body-forward change while all posts remain held.
These actuator checks do not establish spontaneous combat reactions or firing.
Finally native Force Move must release owned controls without reapplying the
garrison. The fixture's own deadline is 420 seconds.

The terminal `actors=4` counts original logical member records, including the
record of the casualty. It does not mean four actors survived the second cycle.
The four-position gate remains explicit even though fresh squad trimming exists;
this test must not hide a planner regression by reducing its requested roster.

The separate `-FreshTrim` fixture enables the production fresh-roster transaction
immediately after native spawning. A test-only prefab derives from the native US
RifleSquad and requests twelve real actors (one squad leader and eleven riflemen).
The real Village house must independently measure nine safe slots. Read-only
hooks record native admissions, the full roster and leader before trimming, and
acknowledged native removals. Acceptance requires nine retained original actors,
the same leader and three deleted original actors absent from the world. Full
cycles must then restore nine and eight survivors. The ordinary case continues
to require four positions. Both cases retain one native group.

Earlier GuardHouse and Shed candidates measured 23 and zero slots respectively;
neither proves roster reduction. The custom prefab's inherited editor budget
metadata still describes the parent squad; this is a manager/trim fixture, not
proof of Game Master budget handling. The runner must copy its `.et` and `.meta`
into the fixture before recording input hashes. Native trim acceptance is pending.

Full observations compare all materialized survivors on every adapter Poll and
immediately before release, including the effective per-agent native LOD hold.
These observations do not prove continuous timing between callbacks, failed
native deletions/admissions, or possession/regrouping during partial restoration.

A native observer keeps ordinary AI materialized. No connected player is injected.
Sleep uses the unmodified production no-player scheduler and timers; wake uses the
supported cache-Off setting. This does **not** test player-distance crossing,
interactive GM building selection/picker/RPC permissions, combat and firing,
movement to the Force Move destination, multiplayer/JIP, save/load, patrol route
coverage, performance, or other buildings. Those remain separate acceptance gates.

Installed base-game resource identities used:

- House: `{EDBC0E94793BA9F1}Prefabs/Structures/Houses/Village/House_Village_E_1I01/House_Village_E_1I01.et`
- Trim parent squad: `{DDF3799FA1387848}Prefabs/Groups/BLUFOR/Group_US_RifleSquad.et`
- Fireteam: `{84E5BBAB25EA23E5}Prefabs/Groups/BLUFOR/Group_US_FireTeam.et`
- Force Move: `{06E1B6EBD480C6E0}Prefabs/AI/Waypoints/AIWaypoint_ForcedMove.et`
- Parent world: `{BEF094A5F7F3211B}worlds/GameMaster/GM_Eden.ent`
- Systems: `{8DDC2A311929D52F}Configs/Systems/GameMasterSystems.conf`

The launch convention follows this repository family's Optimizer native fixture
runner. The fixture addon/world have independent identities and are test-only.

Default fixture evidence: `build/gameplay-20261004-195632-008/result.json`
records native exit 0 and `passed=true`; 57 checks completed with zero failures.
It covers Simulation, both Full cycles, native stance/look and Force Move control
release. The limitations above still apply. All nine selected Village positions
have positive guard scores, so this house does not cover overflow patrols.

`EXPG_BodyClearance.c` is a separate fixture using the same generated-world driver
name. It spawns no actors: an initially clear real plan node must be blocked by
an actual concrete wall, identify that wall in CharacterAI overlap and sweep
queries, fail production ClearBody, and clear again after native wall deletion.
It has a 120-second deadline and its own `[EXPG BODY RESULT]` marker. Select
`-BodyClearance` to use that fixture and its dedicated verifier; it is mutually
exclusive with `-FreshTrim`. Require its checks, clean shutdown and exit status
separately. Source reviewed; native execution pending.

`EXPG_RepeatGarrisonGameplay.c` (custom fixture, same driver name and house)
repeats the production Add Garrison server calls (CanFit, fresh roster,
AdoptFresh) with US fire teams on one building: at least three adds (the third
while the first garrison is Full cached) and more until some soldiers stand
around the house (at most eight). Every add must be accepted and placed in full
(no trim, no refusal) on valid posts that keep clear of every fixed post, and
earlier garrisons stay untouched. Overflow soldiers may patrol inside (kind 4,
`roam=`, not fixed, on an indoor stop outside every door zone) before any stands
around the house. Then the garrisons sleep (one Simulation, the rest Full), the
second wakes alone, all wake on their posts, and Force Move releases the second
alone. Deadline 330 s. Source reviewed; native execution pending.

```powershell
pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/EXPG_RepeatGarrisonGameplay.c -ExpectResult '\[EXPG REPEAT RESULT\] checks=[1-9]\d* failures=0 adds=[3-8] guards=[1-9]\d* capacity=[1-9]\d* posts=[1-9]\d* building=\d+ roam=\d+ around=[1-9]\d* spawn=0 reason=completed' -TimeoutSeconds 480 -OrchestratorSlotGranted
```

`EXPG_InteriorGameplay.c` (custom fixture, same driver name, a House_Town_E_2I01
of the Everon map, whose floors are in the baked navmesh; a house spawned at run
time has navmesh only on the terrain under it) covers the 2026-10-07 live test: 17 guards held a two-storey Morton house, but
one stood right behind the front door so it could not be opened, and the Game
Master asked for overflow soldiers to patrol inside, take free windows (or watch
a hallway or door) in a firefight and not bunch up. Door clearance: no fixed
slot, patrol stop, alarm window or watch point lies in a door zone (the
planner's `InDoorZone` and the fixture's own subset: leaf width + 0.5 m around
the hinge or 0.8 m around the closed leaf's centre, on its floor); every door
that opens fully on the empty house (`SetControlValue(1)`, 4 s) opens fully again
with the guards in place (three tries), and no standing guard is in a door zone.
US fire teams are added (CanFit, fresh roster, AdoptFresh, caching Off) until
at least three soldiers patrol inside (at most twelve adds; each add is checked
once every soldier is bound on his post or stop). Patrol, 60 s sampled
every 0.5 s: every patroller walks at least 3 m over two claimed stops, never
outside (roof and sixteen-bearing test) or off the indoor floor; standing
soldiers stay 1.2 m apart and any two 0.5 m; claims stay unique and 1.5 m apart.
Alarm: `ThreatBulletImpact(5)` on one fixed guard drives the real threat-state
event; within 30 s every patroller holds a free window, a distinct watch point
or his stop, none outside or stacked, at least one window when one was free.
Calm: all back on patrol within 150 s and one walking again. One patroller is
killed; a Full cycle and a Simulation cycle must bring every survivor back as a
patroller within 1.5 m of his claimed stop, the casualty stays dead, nobody is
replenished. Deadline 540 s. No players, GM UI, real combat or save/load.
Source reviewed; native execution pending. `tests/Test-Interior.ps1` guards the
source invariants and this fixture's wiring portably.

```powershell
pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/EXPG_InteriorGameplay.c -ExpectResult '\[EXPG INTERIOR RESULT\] checks=[1-9]\d* failures=0 leaves=[1-9]\d* zoned=0 doorGuards=0 doorsOpened=\d+ doorsBlocked=0 guards=[1-9]\d* rovers=[3-9]\d* roamMoved=[3-9]\d* roamOutside=0 stacked=0 overlap=0 alertWindows=\d+ alertWatch=\d+ alertOutside=0 alertStacked=0 calmReturned=1 killed=1 fullRovers=[2-9]\d* simRovers=[2-9]\d* replenished=0 reason=completed' -TimeoutSeconds 600 -OrchestratorSlotGranted
```

`EXPG_HoldGameplay.c` (custom fixture, same driver name, House_Town_E_2I01)
covers the 2026-10-07 live report (0.1.9, ACE, EXPBG RO AI, dedicated server):
guards left their spots and ran around, and the garrison was no longer cached.
One US fire team garrisons the house (CanFit, fresh roster, AdoptFresh; caching
Off at first) and needs at least three fixed guards. Case 1: one fixed guard is
moved 2-4 m by a Game Master transform (editable `SetTransform`) and another is
knocked unconscious (`SetUnconscious`, as the Scenario Framework does) for 10 s.
No release; the moved guard holds where he came to rest (his post moves to him,
controls bound, he never walks back), the knocked guard is bound again within
0.6 m of his post after waking and stays within 2 m of where he stood, and every
other fixed guard keeps his exact post (same node, never re-anchored, within
0.5 m). Case 2: a third fixed guard is deleted (editable `Delete`); he is marked
dead and never respawned, the others keep their posts. Case 3: cache mode Full
(no CDF in the fixture modset) then Off, then Simulation then Off; every survivor
is restored within 0.5 m of where he slept and, if fixed, within 0.6 m of his
(anchored) post with controls bound, three soldiers in the squad. Case 4: the
Game Master's Release is the only release and names its reason (status
"Released: Released by the Game Master"). Any earlier release or a refused or
retained status fails the run; "Cache held" statuses are logged as
`[EXPG HOLD STATUS]`. Deadline 540 s. No players, GM UI, ACE, real blasts or
possession. Source reviewed; native execution pending. `tests/Test-PostHold.ps1`
guards the source invariants and this fixture's wiring portably.

```powershell
pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/EXPG_HoldGameplay.c -ExpectResult '\[EXPG HOLD RESULT\] checks=[1-9]\d* failures=0 guards=4 fixed=[34] pushHeld=1 knockedOut=1 knockHeld=1 othersKept=1 deleted=1 respawned=0 fullRestored=3 simRestored=3 premature=0 released=1 reasons=1 reason=completed' -TimeoutSeconds 600 -OrchestratorSlotGranted
```

`EXPG_PostSpreadGameplay.c` (custom fixture, same driver name) covers the live
report of bunched posts on a two-storey town house: it spawns the vanilla
`{38A5F3E4578087AB}Prefabs/Structures/Houses/Town/House_Town_E_2I01/House_Town_E_2I01.et`
at the fixture point, waits up to 150 s for the production analysis and logs the
plan (`[EXPG SPREAD PLAN]`), one `[EXPG SPREAD LEVEL]` line per metre of height
(sampled, reachable, stair, indoor and enclosed nodes: shows whether an upper
floor was sampled but not connected) and one `[EXPG SPREAD SLOT]` line per slot
(storey, height above the house origin, opening 1 window / 2 door, score, range,
fixed, inside). It then adds three US fire teams through CanFit, the fresh
roster and AdoptFresh (caching Off) and logs each `[EXPG SPREAD POST]`. Checks:
slots on at least two storeys; window slots before door slots before the rest;
the first squad on window posts only and on two storeys; no slot or building
post outside (its own test: no roof overhead or six of sixteen bearings open at
1.2 m and 1.8 m); every post 1.5 m from the others on its floor; after 5 s all
twelve alive on their posts, no two within 0.8 m, posts on at least two storeys,
none around the building or at the spawn point. Deadline 300 s. No players, GM
UI, caching or combat. Source reviewed; native execution pending. The planner
change behind it also alters slot counts, so the `-FreshTrim` expectation of
nine Village slots must be re-measured. The interior change (door posts set back
2-4 m, door zones, patrol starts instead of score-0 slots) alters them again; a
patroller counts as holding his post, and `around=0` assumes the town house has
room inside for twelve soldiers.

```powershell
pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/EXPG_PostSpreadGameplay.c -ExpectResult '\[EXPG SPREAD RESULT\] checks=[1-9]\d* failures=0 slots=[1-9]\d* storeys=[2-9] windowSlots=[1-9]\d* doorSlots=\d+ guards=12 postStoreys=[2-9] outside=0 around=0 crowded=0 reason=completed' -TimeoutSeconds 420 -OrchestratorSlotGranted
```

`EXPG_ScanProgressGameplay.c` (custom fixture, same driver name and house) covers
the 0.1.8 live report of squad choices refused for about 90 s while a two-storey
house was still being analysed. It drives the server seam of the deferred squad
picker, `EXPG_GarrisonManager.Wait` with test waiters (the editor's waiter turns
the same events into owner RPCs: progress hint, picker). Request A starts the
analysis, B (a second Game Master) joins it, C cancels at once, D joins past
30 %. Checks: one plan for the building; the plan's progress (sampled every
0.5 s) and every reported percentage never decrease and reach 100; the ready
event fires exactly once per request (B is held twice as if another Game Master
were choosing, reports the queued state, then fires once); C hears nothing; D
starts at the running percentage; a request on the analysed building (E) is
ready inside `Wait` without progress; a request whose building is deleted (F)
fails once with a reason. Plans stay per building (no per-type cache), so there
is no type-cache case. Deadline 300 s. No players, GM UI or squads: the owner
RPCs, the hint and the picker opening need GM acceptance. Source reviewed;
native execution pending. `tests/Test-ScanProgress.ps1` guards the deferred
picker flow and this fixture's wiring portably.

```powershell
pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/EXPG_ScanProgressGameplay.c -ExpectResult '\[EXPG SCAN RESULT\] checks=[1-9]\d* failures=0 samples=[1-9]\d* decreases=0 plans=1 joined=1 readyA=1 readyB=1 queuedB=1 readyD=1 cancelled=0 cachedReady=1 cachedProgress=0 failedF=1 reason=completed' -TimeoutSeconds 420 -OrchestratorSlotGranted
```

`EXPG_CdfFallbackGameplay.c` (custom fixture, same driver name and house) covers
Garrison caching with CDF loaded. The fixture modset has no CDF, so its one seam
overrides `EXPG_GarrisonManager.CdfLoaded()`; player presence is one injected
entity in the garrison's player list. One US fire team (cache mode Full, wake
300 m, sleep 400 m): without CDF it Full-caches with the presence 1000 m away and
restores onto its posts. With CDF it keeps the Full choice but uses Simulation:
a Unit Caching Full zone is placed over the house, the presence waits 350 m away
for 50 s (the garrison must stay awake on its posts), then 1000 m away (it must
Simulation-cache with status "Simulation cached (CDF loaded)", logged once, no
Full transaction, the same four soldiers on their posts, and stay cached), then
340 m (still cached) and 250 m (the same soldiers wake within 0.5 m of where they
slept, controls bound). The zone status must name the squad as "Cached by EXPBG
Garrison itself, with its own wake and sleep distances" with its state, never
"held by another EXPBG module". Deadline 360 s. No real CDF save/load (see the
CDF Compat round trips). Source reviewed; native execution pending.

```powershell
pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/EXPG_CdfFallbackGameplay.c -ExpectResult '\[EXPG CDF FALLBACK RESULT\] checks=[1-9]\d* failures=0 guards=4 fullCycle=1 simulationCycle=1 cdfLines=1 zoneNote=1 reason=completed' -TimeoutSeconds 480 -OrchestratorSlotGranted
```

## Unit Caching per-casualty cleanup fixture (-UnitCleanup)

`tests/EBG_UnitCleanupGameplay.c` reuses the generated-world driver name and is
selected by `-UnitCleanup`, which is mutually exclusive with `-FreshTrim` and
`-BodyClearance`. `-Rhs` (unit cleanup only) also loads RHS: Status Quo and both
content packs, linked by GUID from the installed addons, for the two RHS cases;
without it they only log `available=0`. It spawns real USSR rifle squads, one US
fire team and, with `-Rhs`, two RHS teams, each with its own cache
zone (Affected 40, Wake 60, Sleep 200, clear delay 5), injects player presence
through `EBG_CacheManager.UpdatePlayers`, and kills with native damage. It
attributes deletions to `EBG_CacheCleanup.Tick` and records the cache state at
deletion (0 awake, 1 Simulation cached, 2 Full cached).

Cases: `sim-partial-awake`, `sim-partial-cached`, `full-partial-cached`,
`full-partial-awake-then-cache`, `sim-partial-wakeband`, `foreign-item` (an
unregistered magazine inside the body goes with it), `us-etool-atomic`,
`identityless-gear` (a worn vest without native identity and an unregistered
WeaponPart_Base stock are left out of the save, block nothing and go with the
body), `sim-all-control`, `full-all-control`, `rhs-mg-team` (`-Rhs`: the RHS AFRF
machine-gun team with identity-less PKP, AK-74M and 6B45 gear),
`cloth-slot-accessory` (B1 vanilla stand-in: a stock Lifchik/6B3 canteen in a
storage-less `LoadoutSlotInfo` whose cloth a test seam reports as unlisted to
`NativeVestAccessoryOwner`), `rhs-usmc-recon` (`-Rhs`: the USMC MEF recon team in
`Hat_USMC_Boonie_Comtac`, Peltor headset in the storage-less Comtacs slot) and
`sim-death-while-cached` (a soldier killed while his squad is Simulation cached
keeps his body until the survivors are restored, then cleanup deletes it): 14
cases. Simulation and Full are both covered,
including deletion while Full cached and Full restoration of the survivors
without refill. The wake-band case asserts the hold directly (owned rows intact,
no clear-delay timer, production `PlayerNear` true), and every deleted case
checks that deletion did not precede corpse age plus the clear delay. Full needs the GameMasterSystems world-systems config, which
the runner passes.

`Test-UnitCleanupEvidence` requires one distinct complete RESULT line (stdout,
`console.log` and `script.log` each repeat it) with `failures=0` and `cases=14`;
`state=0 whileCached=0` for `sim-partial-awake` and
`full-partial-awake-then-cache`, `state=1 whileCached=1` for `sim-partial-cached`,
`state=2 whileCached=1` for `full-partial-cached`; the
`[EBG CLEANUP TEST SNAPSHOT] ... heldWhileCached=1 restored=1 deletedByEbg=1`
line; the `[EBG CLEANUP TEST FOREIGN] ... present=0 firstByEbg=1 secondByEbg=1
foreignGone=1` line; the `[EBG CLEANUP TEST ATOMIC] ... sameTick=1
partialStrips=0` line; the `identityless-gear` IDENTITYLESS lines
(`blocking=0`, `lineage=0`); the `cloth-slot-accessory`
`[EBG CLEANUP TEST CLOTH SLOT] ... accessories=n unlisted=n held=n wearerHolder=n
protectedChain=0 blocking=0` and `[EBG CLEANUP TEST CLOTH SLOT DELETED] ... accessoriesGone=1
clothGone=1 present=0 proven=1 lineage=0` lines; with `-Rhs` the passing
`rhs-mg-team` and `rhs-usmc-recon` lines (`bodies=2 byEbg=2`, at least two
accessories), otherwise their `available=0` lines; no `[EBG CLEANUP KEEP]`, no
`[EBG CLEANUP TEST SURVIVOR DELETE]` or `[EBG CLEANUP TEST PARTIAL STRIP]`; and a
clean shutdown. The fixture
deadline is 400 s of world time; `-UnitCleanup` defaults `-TimeoutSeconds` to 540
and refuses less than 480.

```powershell
pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -UnitCleanup -TimeoutSeconds 540 -OrchestratorSlotGranted
pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -UnitCleanup -Rhs -TimeoutSeconds 540 -OrchestratorSlotGranted
```

It proves no GM UI, real player movement, multiplayer or save/load. Status:
source reviewed; native execution pending.

## Unit Caching local-host fixture

`tests/EBG_LocalCacheGameplay.c` (custom fixture, same driver name) models a
locally hosted or single-player GM session: one host character injected through
`EBG_CacheManager.UpdatePlayers` and two USSR rifle squads next to a Full zone
with the module radii (affected 300, wake 700, sleep 900, clear delay 5). One
squad enrolls; the other is marked Exclude and loses a soldier through AI
Surrender's prisoner path (RemoveAgent, AI off, `EBG_MarkLeftSquad`); the zone
status must name both. With the host 780 m away the squad must stay awake for
20 s and the status and notice must say a player character keeps it awake inside
the 900 m sleep radius; at 1200 m it must Full-cache (GameMasterSystems present)
and the note must clear; back at the squad it must wake with six soldiers. An
empty Simulation zone must report that no AI group is inside its affected
radius. Deadline 260 s. The engine is the dedicated diagnostic server, so it
does not prove listen-host or single-player replication, GM hint rendering or
save/load. `tests/Test-CacheZoneFeedback.ps1` guards the same wiring portably.
Source reviewed; native execution pending.

```powershell
pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/EBG_LocalCacheGameplay.c -ExpectResult '\[EBG LOCAL CACHE RESULT\] checks=[1-9]\d* failures=0 reason=complete' -OrchestratorSlotGranted
```

## Ambient Destruction road wreck fixture

`tests/EAD_RoadWrecksGameplay.c` (custom fixture, same driver name) spawns the
production `EAD_Zone` prefab in Morton at the origin and radius of the 0.1.7 live
report (radius 100, Wrecks 100, Bodies 0, Destruction 0) and lets the real
scheduler generate. It measures each wreck against the native road network
independently of the production helpers and requires: at least six wrecks; a
lateral offset standard deviation above 0.8 m; at most 40 percent within 0.25 m of
the centreline; mean heading skew above 10 degrees and median above 8; at most a
quarter within 2 degrees of the road direction; every centre within the road or
its shoulder; no overlapping oriented footprints; centre and four inset corners
on a real surface within 0.35 m of the wreck base (not water, not a destructible
building, live props cached first); nearest-neighbour spacing CV above 0.15. The
`EAD_Snapshot` round trip must keep every transform, another seed must give
another layout and the first seed must regenerate it exactly. Deadline 240 s.
No players, GM UI, CDF or real save/load. Source reviewed; native execution
pending.

```powershell
pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/EAD_RoadWrecksGameplay.c -TimeoutSeconds 300 -OrchestratorSlotGranted -ExpectResult '\[EXPG EAD ROAD RESULT\] checks=[1-9]\d* failures=0 wrecks=([6-9]|[1-9]\d+) reason=complete'
```

## Ambient Destruction vehicle types fixture

`tests/EAD_VehicleCategoryGameplay.c` (custom fixture, same driver name) spawns
the production `EAD_Zone` prefab at the road wreck fixture's Morton point
(radius 150, Wrecks 100, Bodies 0, Destruction 0, one seed). A new zone must
default to Both, and out-of-range values must clamp. The packed `Edit.conf`
entry `EAD_VehicleTypesAttribute` (key 12, Civilian 0 / Military 1 / Both 2)
switches the zone through the server-only attribute write that CDF restore
uses: Military, Civilian, Both, then Military again. Every wreck of the
Military and Civilian layouts must carry that `EAD_Catalog.Category` (at
least four each). Both must place at least six wrecks and mix the two
categories. The second Military layout must equal the first exactly. A
schema 3 snapshot must keep the setting. A schema 2 payload (written before
the setting existed) must read back as Both with every record, and importing
it (the CDF bridge path) must keep those records without regenerating.
Deadline 270 s. No players, GM UI, CDF or real save/load. Source reviewed;
native execution pending. `tests/Test-VehicleCategory.ps1` guards the
classification and wiring portably.

```powershell
pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/EAD_VehicleCategoryGameplay.c -TimeoutSeconds 420 -OrchestratorSlotGranted -ExpectResult '\[EXPG EAD VEHICLE RESULT\] checks=[1-9]\d* failures=0 military=([4-9]|[1-9]\d+) civilian=([4-9]|[1-9]\d+) both=([6-9]|[1-9]\d+) mixed=1 replay=1 legacy=1 reason=complete'
```

## AI Surrender commander grenade fixture

`tests/ESR_CommanderGrenadeGameplay.c` (custom fixture, same driver name) spawns
the real AI Surrender module prefab and four native USSR rifle squads at least
140 m apart, then kills two non-leaders per squad through the native damage
manager (2 of 6 is above the 10% threshold; surrender 100%, random 0). The
production evaluation decides every case. Settings checks come first: the
prefab defaults (grenade 0%, must carry ON), the clamp, a ten-value save from
before the commander settings (restored with their defaults), a twelve-value
save (restored and mirrored), and a save with more values than settings
(fourteen since the intel setting; refused).

- own: grenade 100%, must carry ON. If his loadout has no frag, the fixture puts
  one in his inventory. He does not surrender and stays in his squad with his AI
  off. Exactly one of his grenades leaves his inventory, a timed vanilla frag of
  the same prefab lies within 1 m of him and goes live, and he is dead or
  unconscious after the fuse. The other three able soldiers surrendered, and his
  record ends with "blast".
- spawned: must carry OFF, his frags removed. A vanilla RGD-5 (USSR side) is
  placed live within 1 m of him, and he is down after the fuse.
- mustCarry: must carry ON, his frags removed. He surrenders with the others.
- control: grenade 0%. The leader surrenders as before, and no commander record
  is left.

Deadline 240 s. No players, GM UI, ACE, CDF or real save/load. Source reviewed;
native execution pending.

```powershell
pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/ESR_CommanderGrenadeGameplay.c -TimeoutSeconds 300 -OrchestratorSlotGranted -ExpectResult '\[ESR GRENADE RESULT\] checks=[1-9]\d* failures=0 own=1 spawned=1 mustCarry=1 control=1 reason=complete'
```

## AI Surrender intel items fixture

`tests/ESR_IntelRevealGameplay.c` (custom fixture, same driver name) spawns the
real AI Surrender module prefab and one native USSR rifle squad. Settings checks
come first: the prefab default (30%), the clamp, ten- and twelve-value saves
(restored with the intel default), a thirteen-value save (restored and
mirrored) and a fourteen-value save (refused). Then reveal 0%, identity 100%,
intel 100%, radius 300 m. One soldier carries an Intel Items notebook and
surrenders through the production call; a squad mate carrying a manual is
killed. Around the prisoner: a notebook 50 m east, a manual 100 m north, a
tablet 200 m south, a laptop 20 m west whose startup token is spent, and a
notebook 330 m east.

- query: the production `ESR_IntelQuery` alone counts four unclaimed items
  inside 300 m (three listed) and five inside 400 m.
- first answer (production Interrogate): identity, and he points out exactly
  the body's manual, the east notebook and the north manual, nearest first,
  with the 25 m distances and compass sectors computed by the fixture from the
  holders' positions. The tablet is fourth (at most three); the spent laptop,
  his own notebook and the item outside the radius are left out. Three static
  "Intel (interrogation)" placed markers stand at those positions, and the
  dialog text lists the three. Asking again repeats it without new markers.
- refused: a second prisoner with identity 0% refuses; no intel is rolled.
- chanceZero: intel 0%, identity 100%; he answers with no intel line and no
  marker.

Deadline 180 s. No players (an item in a player's inventory is not covered), GM
UI, client dialog, CDF or real save/load. Source reviewed; native execution
pending.

```powershell
pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/ESR_IntelRevealGameplay.c -TimeoutSeconds 300 -OrchestratorSlotGranted -ExpectResult '\[ESR INTEL RESULT\] checks=[1-9]\d* failures=0 listed=3 markers=3 refused=1 chanceZero=1 reason=complete'
```

## Per-squad and per-soldier override fixture

`tests/ESR_OverrideGameplay.c` (custom fixture, same driver name) spawns the
real AI Surrender and AI Global Skills module prefabs, two native USSR rifle
squads (A, and control B 150 m east), one Intel Items notebook and, later, a
real Unit Caching zone in Full mode over A. Module settings: surrender 0%,
threshold 10%, random 0, reveal, identity and intel 0%, radius 300 m, grenade
0%, default ROE Return Fire Only. Presence is injected through
`EBG_CacheManager.UpdatePlayers`; the server has no players.

- overrides: through the production attribute classes on the attribute-saver
  path (null manager, playerID -1): A surrender 100%, reveal 100%, intel 100%
  (identity on the module), one soldier of A (the holdout) surrender 0%; A ROE
  Fire on Sight, the holdout's own ROE Return Fire Only. Saved reads return set
  values as spinbox entries and nothing for unset ones; resolution gives a plain
  A soldier 100% (squad), the holdout 0% (soldier) and B 0% (module); a module
  change reaches B through the per-squad cache.
- roe: A FIRE_AT_WILL, B RETURN_FIRE; the holdout answers HOLD_FIRE himself
  while a squad mate gives FIRE_AT_WILL, before caching and after the wake. B's
  ROE override set mid-mission applies at once and clears back to the default.
- cached: A Full caches (squad deleted, survivors respawned later). The squad
  snapshot holds A's values and ROE, exactly one survivor carry holds the
  holdout's, and a portable snapshot written and read back by production keeps
  both. After the wake the recreated squad and the respawned holdout have every
  value back and no other soldier took one.
- save path: exported squad overrides bind back by persistence id at once after
  being cleared; the holdout's exported ROE binds back on the AI Global Skills
  tick (skipped with a log line if the world has no persistence id).
- control: one casualty in B (module 0%): nobody surrenders.
- surrendered / heldOut: one casualty in A: the four able soldiers surrender
  (100%, exact), the holdout stays in his squad; each prisoner carries A's
  values.
- interrogation / intel: A's reveal is then changed to 0%. Prisoner 1 still
  reveals B (his squad's 100% at surrender) and points out the notebook;
  prisoner 2, given his own reveal 0% and identity 100% as a prisoner, gives his
  identity; prisoner 3, given his own reveal and identity 0%, refuses.

Deadline 300 s. No GM UI, clients or JIP (nothing replicates), Garrison Full
caching (same survivor carry, squad entity kept) or real native or CDF
save/load. Source reviewed; native execution pending.

```powershell
pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/ESR_OverrideGameplay.c -TimeoutSeconds 420 -OrchestratorSlotGranted -ExpectResult '\[ESR OVERRIDE RESULT\] checks=[1-9]\d* failures=0 surrendered=4 heldOut=1 control=0 interrogation=3 intel=1 roe=1 cached=1 reason=complete'
```

## Time and Weather fixture

`tests/ETW_TimeWeatherGameplay.c` (custom fixture, same driver name) spawns the
real Weather Transition and Time Skip module prefabs and drives the public server
entry points the Game Master attributes call (`ETW_TimeSkip.Start`,
`ETW_WeatherRunner.StartFromModule`).

- rollover: calendar arithmetic (year end, leap day, non-leap February, 30-day
  month, century rule), the module defaults (10 min; 6 h, fades 2/3/2 s,
  "{hours} hours later"), clamps, native-save restore, choice values and texts.
- skip: with day auto-advance off, +26 h 30 min from 2026-12-31 22:00 lands on
  2027-01-02 00:30 and +2 h from 2028-02-28 23:00 on 2028-02-29 01:00; a second
  skip during the first is refused; the dedicated server draws no black screen.
- broadcasts: three skips send three fade broadcasts and the local handler runs
  three times.
- gradual / finished: a 1-minute transition to another weather (Rainy unless
  the start is Rainy) with rain to 100% (0% if it starts at 50% or more) and
  wind 8 m/s. Rain moves monotonically and is about half way after 30 s; at the
  end rain and wind are at target, the target state is reached and held.
- interrupt: a new transition 20 s into another one continues from the rain
  reached (no jump a second later) with a restarted clock.
- skipFinish: a time skip during a transition completes it at full black.
- foreign: `ForceWeatherTo` (what Scenario Properties weather does) stops the
  transition and hands rain back to the weather.
- smooth (evidence only, 0 or 1): 1 when the clouds reached the target through
  the engine's own blend without the end-of-transition snap. Judge it from the
  `[ETW SAMPLE]` overcast column as well; 0 means the smooth engine transition
  did not run from script and needs a fix before release.

Deadline 300 s. No players, GM UI, client black screen, JIP or real save/load.
Source reviewed; native execution pending.

```powershell
pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/ETW_TimeWeatherGameplay.c -TimeoutSeconds 420 -OrchestratorSlotGranted -ExpectResult '\[ETW RESULT\] checks=[1-9]\d* failures=0 rollover=1 skip=1 broadcasts=3 gradual=1 finished=1 interrupt=1 skipFinish=1 foreign=1 smooth=[01] reason=complete'
```

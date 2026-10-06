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
earlier garrisons stay untouched. Then the garrisons sleep (one Simulation, the
rest Full), the second wakes alone, all wake on their posts, and Force Move
releases the second alone. Deadline 330 s. Source reviewed; native execution
pending.

```powershell
pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/EXPG_RepeatGarrisonGameplay.c -ExpectResult '\[EXPG REPEAT RESULT\] checks=[1-9]\d* failures=0 adds=[3-8] guards=[1-9]\d* capacity=[1-9]\d* posts=[1-9]\d* building=\d+ around=[1-9]\d* spawn=0 reason=completed' -TimeoutSeconds 480 -OrchestratorSlotGranted
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

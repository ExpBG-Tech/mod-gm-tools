# EXPBG GM Tools validation gates

## Pack 0.1.0 status (2026-10-05)

GM UI and building coverage (Workbench play with computer use, then native survey):

- Real GM flow: right-click building -> Add Garrison -> "Choose Garrison Squad"
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
| GM transaction | Right-click building, picker, cancel/retry, exactly one squad |
| Placement | Windows/doors/stairs, rotations, disconnected floor and roof rejection |
| Behavior | Guards hold during combat while aiming/firing/crouching; overflow stays inside |
| Orders | Real Force Move releases active/cached/restoring squads permanently |
| Caching | Simulation retains original actors; two Full cycles preserve captured transforms/assignments and exclude casualties, no duplicate owner |
| Lifecycle | Collapse, deletion, possession, transfer, GM disconnect during picker |
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

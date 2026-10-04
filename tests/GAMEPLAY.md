# Native Garrison smoke fixture

Status: native run **FAILED** on 2026-10-05 at building capacity: the planner
accepted one slot for the requested four-person squad. No actors were spawned.
Roster, post-hold, cache and Force Move checks were not reached. Diagnostic node
logging was added after that run and awaits a rerun.

After a successful indexed candidate build and explicit orchestrator slot handoff:

```powershell
pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot '<indexed addon directory>' -OrchestratorSlotGranted
```

The runner refuses to launch if an Arma/Workbench process exists. It copies the
candidate and configured Optimizer dependency into a private run directory, records
their hashes, runs the diagnostic server for at most 360 seconds, and retains logs
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
actors and presentation, ten seconds of restored post retention, and native Force
Move releasing owned post/patrol controls without reapplying the garrison.

A native observer keeps ordinary AI materialized. No connected player is injected.
Sleep uses the unmodified production no-player scheduler and timers; wake uses the
supported cache-Off setting. This does **not** test player-distance crossing,
interactive GM building selection/picker/RPC permissions, combat and firing,
movement to the Force Move destination, multiplayer/JIP, save/load, patrol route
coverage, performance, or other buildings. Those remain separate acceptance gates.

Installed base-game resource identities used (source inspected, runtime unproven):

- House: `{EDBC0E94793BA9F1}Prefabs/Structures/Houses/Village/House_Village_E_1I01/House_Village_E_1I01.et`
- Fireteam: `{84E5BBAB25EA23E5}Prefabs/Groups/BLUFOR/Group_US_FireTeam.et`
- Force Move: `{06E1B6EBD480C6E0}Prefabs/AI/Waypoints/AIWaypoint_ForcedMove.et`
- Parent world: `{BEF094A5F7F3211B}worlds/GameMaster/GM_Eden.ent`
- Systems: `{8DDC2A311929D52F}Configs/Systems/GameMasterSystems.conf`

The launch convention follows this repository family's Optimizer native fixture
runner. The fixture addon/world have independent identities and are test-only.

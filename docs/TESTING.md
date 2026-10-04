# EXPBG GM Tools validation gates

Portable tooling passed on 2026-10-05. Native gates remain pending. Do not publish
a broken or uncompiled candidate.

| Gate | Required evidence |
| --- | --- |
| Portable tooling | Test-Tools.ps1 exit 0; staging contains only runtime inputs |
| Native compile/import | build.ps1 exit 0, owned script/resource errors absent |
| GM transaction | Right-click building, picker, cancel/retry, exactly one squad |
| Placement | Windows/doors/stairs, rotations, disconnected floor and roof rejection |
| Behavior | Guards hold during combat while aiming/firing/crouching; overflow stays inside |
| Orders | Real Force Move releases active/cached/restoring squads permanently |
| Caching | Two Simulation cycles, same actors/equipment/casualties, post resumption, no duplicate owner |
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
ticket replay/expiry, attribute bounds and route corridor contracts in an isolated
ResourceManager copy. It requires an indexed source snapshot and an explicit
`-OrchestratorSlotGranted` acknowledgement. This does not launch a gameplay world.

Cold restoration of garrison assignments is not supported. Check the native
save refusal and Optimizer-CDF refusal while active, then Prepare for Save,
await original actor restoration and ownership release, and save ordinary squads.
CDF without its Optimizer companion bypasses the save hook: caching must refuse
to suspend actors in that configuration.

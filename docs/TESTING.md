# EXPBG GM Tools validation gates

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
- clouds through `RequestStateTransition` with no `ForceWeatherTo` in the
  runner, the foreign-change hooks, the Scenario Properties smoothing guard;
- the broadcast fade RPC, the overlap refusal and the clock change at full
  black, plus a PowerShell model of the date rollover;
- the Enforce gotchas, ASCII/LF, and the fixture's runner command and RESULT
  regex.

Pending gates:

- build.ps1 compile;
- native fixture `tests/ETW_TimeWeatherGameplay.c` (command in
  `tests/GAMEPLAY.md`), including the `smooth` evidence. If clouds only snap at
  the end, the state machine route needs a fix: path through adjacent states,
  or day auto-advance;
- GM session:
  - both modules in the Systems list with names and descriptions;
  - preset buttons and their host preview;
  - status rows refresh on reopen;
  - Scenario Properties weather blends with the module present and is instant
    without it;
- dedicated server with a client:
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

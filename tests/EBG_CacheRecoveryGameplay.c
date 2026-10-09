// TEST ONLY. Unit Caching static-seat wake matrix (#14) and the Release blocked groups escape (#15).
// pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/EBG_CacheRecoveryGameplay.c -ExpectResult '\[EBG RECOVERY TEST RESULT\] checks=[1-9]\d* failures=0 cases=5 reason=complete mismatches=0' -TimeoutSeconds 480 -OrchestratorSlotGranted
// Real zone prefab, real tripod M2 and USSR squad, production Full sleep/wake, Prepare for
// save and controller commands. Ported from the standalone Optimizer static-seat matrix.
// Cases run one after another (Prepare for save is global): seat untouched, seat occupied by
// another AI, gun deleted, accepted entry that never completes (timeout -> on foot), and a
// native transition that never settles (negative: Blocked), released with the escape.
// Fault injection changes only the native-entry result/transition observation; actors,
// ownership, clock, restoration and release stay real. No player, no GM UI, no save/load.
// The runner copies this file to EXPG_GarrisonGameplay.c; the class names are fixed.
class EBGSeatCase
{
 string Name;
 string Fault;
 vector Point;
 SCR_AIGroup Group;
 SCR_AIGroup IntruderGroup;
 IEntity Mount;
 EntityID MountId;
 EBG_CacheZone Zone;
 EBG_CacheGroup Record;
 EBG_CacheMember GunnerMember;
 SCR_ChimeraCharacter Original;
 SCR_ChimeraCharacter CreatedGunner;
 SCR_ChimeraCharacter Intruder;
 ref EBG_StaticEmplacement OriginalSeat;
 int Phase;
 float PhaseAt;
 float CachedAt;
 bool Done;
}
class EXPG_GarrisonGameplayClass : GenericEntityClass {}
class EXPG_GarrisonGameplay : GenericEntity
{
 static const float FIXTURE_SECONDS = 360;
 static const string SQUAD = "{E552DABF3636C2AD}Prefabs/Groups/OPFOR/Group_USSR_RifleSquad.et";
 static const string TRIPOD = "{73530808E4B455BA}Prefabs/Weapons/Tripods/Tripod_M3_M2HB.et";
 static const string ZONE = "{7E1080ED8F0633FD}PrefabsEditable/EXPBG/EBG_CacheZone.et";
 ref array<ref EBGSeatCase> Cases = {};
 int Current;
 int Checks;
 int Failures;
 float Started;
 float Next;
 bool Finished;
 bool Isolated;
 vector Origin = "4773.46 0 7094.57";
 void EXPG_GarrisonGameplay(IEntitySource src, IEntity parent) { SetEventMask(EntityEvent.INIT | EntityEvent.FRAME); }
 float Now() { return GetGame().GetWorld().GetWorldTime() * 0.001; }
 override void EOnInit(IEntity owner)
 {
  if (!Replication.IsServer()) { ClearEventMask(EntityEvent.FRAME); return; }
  EBG_DebugChecks.Enabled = true; EBG_DebugChecks.Mismatches = 0; // indexes also run their old full scans
  ClearHooks();
  Started = Now(); Next = Started + 15;
  // Dry points proven by the unit-cleanup fixture; one case at a time.
  AddCase("seat-untouched", "", 0, 0);
  AddCase("seat-occupied", "occupied", 600, 0);
  AddCase("seat-missing", "missing", 0, 600);
  AddCase("seat-timeout", "timeout", 600, 600);
  AddCase("seat-transition-release", "transition", -600, 0);
  PrintFormat("[EBG RECOVERY TEST BEGIN] cases=%1 mode=Full strategy=group affected=30 deadline=%2", Cases.Count(), FIXTURE_SECONDS);
 }
 void AddCase(string name, string fault, float x, float z)
 {
  EBGSeatCase added = new EBGSeatCase();
  added.Name = name; added.Fault = fault; added.Point = Origin + Vector(x, 0, z);
  Cases.Insert(added);
 }
 bool Check(bool ok, string label)
 {
  Checks++;
  if (!ok) Failures++;
  PrintFormat("[EBG RECOVERY TEST CHECK] pass=%1 %2", ok, label);
  return ok;
 }
 void Finish(string reason)
 {
  if (Finished) return;
  Finished = true;
  ClearHooks();
  ClearEventMask(EntityEvent.FRAME);
  Check(EBG_DebugChecks.Mismatches == 0, "index cross-checks matched their old full scans");
  PrintFormat("[EBG RECOVERY TEST RESULT] checks=%1 failures=%2 cases=%3 reason=%4 mismatches=%5", Checks, Failures, Cases.Count(), reason, EBG_DebugChecks.Mismatches);
  GetGame().RequestClose();
 }
 void ClearHooks()
 {
  // Seated gunners go to Simulation in play (CDF cannot export them Full); this matrix
  // exercises the Full static-seat wake that caches from earlier versions still use.
  EBG_CacheManager.EBG_TestFullMounted = true;
  EBG_StaticEmplacement.EBG_TestAcceptWithoutMount = false;
  EBG_StaticEmplacement.EBG_TestHoldTransition = false;
  EBG_StaticEmplacement.EBG_TestTransition = null;
 }
 EntitySpawnParams Params(vector point)
 {
  EntitySpawnParams params = new EntitySpawnParams();
  params.TransformMode = ETransformMode.WORLD;
  Math3D.MatrixIdentity4(params.Transform);
  point[1] = GetGame().GetWorld().GetSurfaceY(point[0], point[2]) + 0.2;
  params.Transform[3] = point;
  return params;
 }
 IEntity Spawn(string prefab, vector point)
 {
  // Keep the Resource in a local before spawning (inline temporaries returned null spawns).
  Resource resource = Resource.Load(prefab);
  return GetGame().SpawnEntityPrefab(resource, GetGame().GetWorld(), Params(point));
 }
 bool Seated(EBGSeatCase c, SCR_ChimeraCharacter character, bool request)
 {
  if (!character || !c.Mount) return false;
  SCR_CompartmentAccessComponent access = SCR_CompartmentAccessComponent.Cast(character.GetCompartmentAccessComponent());
  if (!access) return false;
  BaseCompartmentSlot slot = access.GetCompartment();
  if (slot && !access.IsGettingIn() && !access.IsGettingOut()) return slot.GetType() == ECompartmentType.TURRET && slot.GetVehicle() == c.Mount;
  if (request && !access.IsGettingIn() && !slot) access.MoveInVehicle(c.Mount, ECompartmentType.TURRET);
  return false;
 }
 bool OnFoot(SCR_ChimeraCharacter character)
 {
  if (!character || character.IsInVehicle()) return false;
  CompartmentAccessComponent access = character.GetCompartmentAccessComponent();
  return access && !access.GetCompartment() && !access.IsInCompartment() && !access.IsGettingIn() && !access.IsGettingOut() && !access.IsSwitchingSeatsAnim();
 }
 int Agents(SCR_AIGroup group)
 {
  if (!group) return -1;
  return group.GetAgentsCount();
 }
 override void EOnFrame(IEntity owner, float timeSlice)
 {
  if (Finished || Now() < Next) return;
  Next = Now() + 0.25;
  if (!Isolated)
  {
   Isolated = true;
   string modeName = "none";
   if (GetGame().GetGameMode()) modeName = GetGame().GetGameMode().Type().ToString();
   PrintFormat("[EBG RECOVERY TEST RUNTIME] systems='%1' gameMode=%2 players=%3 zones=%4", GetGame().GetSystemsConfig(), modeName, GetGame().GetPlayerManager().GetPlayerCount(), EBG_CacheZone.Zones.Count());
   if (!Check(GetGame().GetPlayerManager().GetPlayerCount() == 0 && EBG_CacheZone.Zones.IsEmpty(), "isolated server: no players, no zones")) { Finish("setup"); return; }
  }
  if (Now() - Started > FIXTURE_SECONDS)
  {
   if (Current < Cases.Count()) Report(Cases[Current]);
   Check(false, "all cases finished before the fixture deadline");
   Finish("timeout");
   return;
  }
  if (Current >= Cases.Count()) { Finish("complete"); return; }
  EBGSeatCase c = Cases[Current];
  Step(c);
  if (c.Done) EndCase(c);
 }
 void EndCase(EBGSeatCase c)
 {
  ClearHooks();
  // Leave the native AI; remove the module so the next case starts alone.
  if (c.Zone) SCR_EntityHelper.DeleteEntityAndChildren(c.Zone);
  PrintFormat("[EBG RECOVERY TEST CASE END] case=%1 checks=%2 failures=%3", c.Name, Checks, Failures);
  Current++;
  Next = Now() + 2;
 }
 void Fail(EBGSeatCase c, string label)
 {
  Report(c);
  Check(false, c.Name + " " + label);
  c.Done = true;
 }
 void Report(EBGSeatCase c)
 {
  string state = "none";
  string error = "";
  if (c.Record && c.Record.Full) { state = typename.EnumToString(EBG_FullGroupPhase, c.Record.Full.GetState()); error = c.Record.Full.GetError(); }
  string recovery = "";
  if (c.Record) recovery = c.Record.Recovery;
  PrintFormat("[EBG RECOVERY TEST STATE] case=%1 phase=%2 full=%3 error='%4' recovery='%5' global=%6 pending=%7 blocked=%8", c.Name, c.Phase, state, error, recovery, EBG_OptimizerControl.State, EBG_OptimizerControl.Pending, EBG_OptimizerControl.Blocked);
 }
 void Step(EBGSeatCase c)
 {
  EBG_CacheManager manager = EBG_CacheManager.Get();
  if (c.Phase == 0)
  {
   if (!PersistenceSystem.GetInstance() || !GetGame().GetGameMode()) return;
   EBG_StaticEmplacement.EBG_TestRequests = 0;
   c.Mount = Spawn(TRIPOD, c.Point + Vector(8, 0, 8));
   c.Group = SCR_AIGroup.Cast(Spawn(SQUAD, c.Point));
   if (c.Fault == "occupied")
   {
    // Another native AI outside the zone; never enrolled by any cache zone.
    c.IntruderGroup = SCR_AIGroup.Cast(Spawn(SQUAD, c.Point + Vector(60, 0, 0)));
    if (c.IntruderGroup) c.IntruderGroup.EBG_Exclude = true;
   }
   c.Zone = EBG_CacheZone.Cast(Spawn(ZONE, c.Point));
   if (!Check(c.Mount && c.Group && c.Zone && (c.Fault != "occupied" || c.IntruderGroup), c.Name + " tripod, six-man squad and cache zone spawned")) { c.Done = true; return; }
   c.MountId = c.Mount.GetID();
   // Full, per group, small enrollment radius; a long clear delay while the gunner is seated.
   c.Zone.SetValue(1, 1); c.Zone.SetValue(2, 1); c.Zone.SetValue(3, 30);
   c.Zone.SetValue(4, 60); c.Zone.SetValue(5, 200); c.Zone.SetValue(12, 300);
   c.Zone.SetValue(15, 0); c.Zone.SetValue(18, 0); c.Zone.SetValue(21, 1);
   c.Zone.SetValue(0, 1);
   c.Phase = 1; c.PhaseAt = Now(); return;
  }
  if (c.Phase == 1)
  {
   c.Record = manager.FindGroup(c.Group);
   bool intruderReady = c.Fault != "occupied" || Agents(c.IntruderGroup) > 0;
   if (!c.Record || c.Record.Members.Count() != 6 || !c.Group.EBG_HasCompletedInitialSpawn() || !intruderReady || c.Zone.Editing)
   {
    if (Now() - c.PhaseAt > 40) Fail(c, "manager enrolled the six-man squad");
    return;
   }
   c.GunnerMember = c.Record.Members[0];
   c.Original = c.GunnerMember.Entity;
   c.Phase = 2; c.PhaseAt = Now(); return;
  }
  if (c.Phase == 2)
  {
   if (!Seated(c, c.Original, true))
   {
    if (Now() - c.PhaseAt > 30) Fail(c, "gunner seated in the native static tripod");
    return;
   }
   c.OriginalSeat = new EBG_StaticEmplacement();
   Check(c.OriginalSeat.Capture(c.Original) && c.OriginalSeat.HadMount && c.OriginalSeat.Matches(c.Original), c.Name + " native static tripod occupant admitted and exact seat captured");
   if (c.Fault == "")
   {
    // Guards of the timeout fallback, on fresh copies so production state is untouched.
    EBG_StaticEmplacement seatedGuard = new EBG_StaticEmplacement();
    seatedGuard.Capture(c.Original);
    Check(!seatedGuard.RestoreOnFootIfUnavailable(c.Original, c.Group, true) && seatedGuard.Matches(c.Original), c.Name + " timeout fallback never unmounts a seated survivor");
    SCR_ChimeraCharacter foot = c.Record.Members[1].Entity;
    EBG_StaticEmplacement footGuard = new EBG_StaticEmplacement();
    Check(footGuard.Capture(foot) && !footGuard.HadMount && !footGuard.RestoreOnFootIfUnavailable(foot, c.Group, true), c.Name + " timeout fallback requires a captured mount");
    EBG_StaticEmplacement groupGuard = new EBG_StaticEmplacement();
    groupGuard.Capture(c.Original);
    Check(!groupGuard.RestoreOnFootIfUnavailable(foot, null, true) && groupGuard.HadMount, c.Name + " timeout fallback requires the survivor's own group");
   }
   // Only a seat/compartment refusal is a regression here; a transient AI state is not.
   string admission = EBG_SimulationCache.Unsupported(c.Original, false);
   if (!Check(!admission.Contains("tatic") && !admission.Contains("ompartment") && !admission.Contains("ehicle"), c.Name + " cache admission accepts the verified static seat '" + admission + "'")) { c.Done = true; return; }
   c.Zone.SetValue(12, 1);
   c.Phase = 3; c.PhaseAt = Now(); return;
  }
  if (c.Phase == 3)
  {
   if (!c.Record.Full || c.Record.Full.GetState() != EBG_FullGroupPhase.CACHED)
   {
    if (Now() - c.PhaseAt > 60) Fail(c, "squad Full cached with the gunner seated");
    return;
   }
   Check(!c.Original && !c.Record.Group && c.Record.Full.GetMemberCount() == 6 && c.Mount, c.Name + " Full retains six snapshots, no native group or originals, and the tripod");
   c.CachedAt = Now(); c.Phase = 4; return;
  }
  if (c.Phase == 4)
  {
   if (Now() - c.CachedAt < 5) return;
   if (c.Fault == "occupied")
   {
    c.Intruder = SCR_ChimeraCharacter.Cast(c.IntruderGroup.GetLeaderEntity());
    string occupiedReason;
    if (!Check(c.Intruder && c.OriginalSeat.RequestMount(c.Intruder, occupiedReason), c.Name + " another native AI requested the empty static seat " + occupiedReason)) { c.Done = true; return; }
    c.Phase = 9; c.PhaseAt = Now(); return;
   }
   if (c.Fault == "missing")
   {
    SCR_EntityHelper.DeleteEntityAndChildren(c.Mount);
    c.Phase = 9; c.PhaseAt = Now(); return;
   }
   if (c.Fault == "timeout" || c.Fault == "transition")
   {
    EBG_StaticEmplacement.EBG_TestAcceptWithoutMount = true;
    EBG_StaticEmplacement.EBG_TestHoldTransition = c.Fault == "transition";
    manager.RestoreAllForSave();
    c.Phase = 5; c.PhaseAt = Now(); return;
   }
   c.Zone.SetValue(101, 1);
   c.Phase = 5; c.PhaseAt = Now(); return;
  }
  if (c.Phase == 9)
  {
   bool injected;
   if (c.Fault == "occupied") injected = c.OriginalSeat.Matches(c.Intruder);
   if (c.Fault == "missing") injected = !c.Mount && !c.OriginalSeat.Resolve();
   if (!injected)
   {
    if (Now() - c.PhaseAt > 30) Fail(c, "static seat fault applied before Full wake");
    return;
   }
   Check(true, c.Name + " native static seat fault verified before Full wake: " + c.Fault);
   c.Zone.SetValue(101, 1);
   c.Phase = 5; c.PhaseAt = Now(); return;
  }
  if (c.Phase == 5)
  {
   if (!c.CreatedGunner && c.GunnerMember.Entity) c.CreatedGunner = c.GunnerMember.Entity;
   bool failed = c.Record.Full && c.Record.Full.GetState() == EBG_FullGroupPhase.FAILED;
   if (c.Fault == "transition")
   {
    if (!failed || EBG_OptimizerControl.Blocked == 0)
    {
     if (Now() - c.PhaseAt > 60) Fail(c, "unsettled transition reached the retained Blocked state");
     return;
    }
    string heldReason;
    string error = c.Record.Full.GetError();
    Check(c.GunnerMember.Entity == c.CreatedGunner && c.CreatedGunner && c.CreatedGunner.GetCharacterGroup() == c.Record.Group && EBG_StaticEmplacement.EBG_TestRequests == 1, c.Name + " timed-out transition retains the exact created survivor without retry or replacement");
    Check(Now() - EBG_StaticEmplacement.EBG_TestRequestedAt >= 10 && error.Contains("Survivor compartment state did not settle") && EBG_StaticEmplacement.InTransition(c.CreatedGunner.GetCompartmentAccessComponent()), c.Name + " transition held through the actual ten-second deadline");
    Check(EBG_OptimizerControl.State == 3 && !EBG_CacheSnapshot.CanSave(heldReason) && !c.Record.Recovery.IsEmpty(), c.Name + " unsafe transition keeps global and CDF save protection (Blocked)");
    Check(error.Contains("capturedMount=") && error.Contains("transition="), c.Name + " terminal refusal carries bounded native-state diagnostics");
    PrintFormat("[EBG RECOVERY TEST BLOCKED] case=%1 agentsBeforeRelease=%2 error='%3'", c.Name, Agents(c.Record.Group), error);
    // #15: the explicit operator escape, exactly as the controller attribute runs it.
    EBG_OptimizerControl.ActingPlayer = 0;
    EBG_OptimizerControl.Execute(4);
    Check(c.Record.Recovery.IsEmpty() && EBG_OptimizerControl.Blocked == 0, c.Name + " Release blocked groups clears the recovery hold at once");
    c.Phase = 7; c.PhaseAt = Now(); return;
   }
   if (failed) { Fail(c, "unavailable or timed-out static seat must not strand surviving AI: " + c.Record.Full.GetError()); return; }
   if (c.Record.Full)
   {
    if (Now() - c.PhaseAt > 60) Fail(c, "Full wake completed");
    return;
   }
   if (c.Fault == "timeout" && EBG_OptimizerControl.State != 2)
   {
    if (Now() - c.PhaseAt > 60) Fail(c, "Prepare for save reached Ready");
    return;
   }
   SCR_ChimeraCharacter restored = c.GunnerMember.Entity;
   Check(c.Record.Group && Agents(c.Record.Group) == 6 && c.Record.Alive == 6 && c.Record.Dead == 0, c.Name + " all six survivors restored without duplication or casualty replenishment");
   if (c.Fault == "") Check(restored && c.OriginalSeat.Matches(restored), c.Name + " restored gunner is mounted in the exact retained static seat");
   else Check(restored == c.CreatedGunner && OnFoot(restored) && restored.GetCharacterGroup() == c.Record.Group, c.Name + " unavailable seat restores the already-created gunner on foot in its group");
   if (c.Fault == "timeout")
   {
    BaseCompartmentSlot available = c.OriginalSeat.Resolve();
    Check(available && !available.GetOccupant() && EBG_StaticEmplacement.EBG_TestRequests == 1 && Now() - EBG_StaticEmplacement.EBG_TestRequestedAt >= 10, c.Name + " accepted entry that never completes falls back on foot after one request and the bounded wait");
    string saveReason;
    Check(EBG_OptimizerControl.Preparing && EBG_OptimizerControl.Pending == 0 && EBG_OptimizerControl.Blocked == 0 && EBG_CacheSnapshot.CanSave(saveReason), c.Name + " Prepare for save reaches Ready and CDF capture readiness accepts the survivors " + saveReason);
   }
   if (c.Fault == "missing") Check(!c.Mount && !GetGame().GetWorld().FindEntityByID(c.MountId) && !c.OriginalSeat.Resolve(), c.Name + " deleted gun is not recreated");
   else Check(c.Mount != null, c.Name + " tripod entity retained through the wake");
   if (c.Fault == "occupied") Check(c.OriginalSeat.Matches(c.Intruder) && c.Intruder.GetCharacterGroup() == c.IntruderGroup, c.Name + " the other native occupant keeps the seat and its group");
   if (c.Fault == "") { c.Done = true; return; }
   c.CachedAt = Now(); c.Phase = 6; return;
  }
  if (c.Phase == 6)
  {
   if (Now() - c.CachedAt < 11) return;
   Check(!c.Record.Full && !c.Record.Simulation && Agents(c.Record.Group) == 6 && c.GunnerMember.Entity == c.CreatedGunner && OnFoot(c.CreatedGunner), c.Name + " on-foot fallback holds beyond the mount deadline with no delayed duplicate or remount");
   if (c.Fault == "occupied") Check(c.OriginalSeat.Matches(c.Intruder), c.Name + " other occupant still seated");
   if (c.Fault == "timeout")
   {
    Check(c.OriginalSeat.Resolve() && !c.OriginalSeat.Resolve().GetOccupant() && EBG_StaticEmplacement.EBG_TestRequests == 1, c.Name + " timed-out seat stays intact without repeated entry requests");
    // Leave the save pause the documented way before the next case caches again.
    EBG_OptimizerControl.Execute(1);
    Check(!EBG_OptimizerControl.Preparing, c.Name + " Enable all zones accepted after Ready");
   }
   c.Done = true; return;
  }
  if (c.Phase == 7)
  {
   if (c.Record.Full || EBG_OptimizerControl.State != 2)
   {
    if (c.Record.Full && c.Record.Full.GetState() == EBG_FullGroupPhase.FAILED && !c.Record.Recovery.IsEmpty()) { Fail(c, "released group failed again: " + c.Record.Full.GetError()); return; }
    if (Now() - c.PhaseAt > 60) Fail(c, "released group finished its wake and Prepare reached Ready");
    return;
   }
   string releasedReason;
   int created = 0;
   foreach (EBG_CacheMember member : c.Record.Members) { if (member.Entity && !member.Dead) created++; }
   PrintFormat("[EBG RECOVERY TEST RELEASED] case=%1 agents=%2 members=%3 alive=%4 gunnerSame=%5 state=%6", c.Name, Agents(c.Record.Group), c.Record.Members.Count(), created, c.GunnerMember.Entity == c.CreatedGunner, EBG_OptimizerControl.State);
   Check(c.Record.Group && Agents(c.Record.Group) == 6 && c.Record.Members.Count() == 6 && created == 6, c.Name + " escape completes the wake: six survivors, none duplicated, none refilled");
   Check(c.GunnerMember.Entity == c.CreatedGunner && c.CreatedGunner.GetCharacterGroup() == c.Record.Group, c.Name + " the stuck gunner is kept, never spawned a second time");
   Check(c.Record.Recovery.IsEmpty() && EBG_OptimizerControl.Blocked == 0 && EBG_OptimizerControl.Pending == 0 && EBG_CacheSnapshot.CanSave(releasedReason), c.Name + " Prepare reaches Ready and CDF capture readiness accepts the released group " + releasedReason);
   Check(manager.Records.Contains(c.Record) && !c.Record.ReleaseRequested, c.Name + " released group stays an ordinary managed record");
   c.Done = true; return;
  }
 }
}

// Fault injection changes only the native-entry result/transition observation.
// Actors, ownership, clock, cache restoration and final release remain real.
modded class EBG_StaticEmplacement
{
 static bool EBG_TestAcceptWithoutMount;
 static bool EBG_TestHoldTransition;
 static int EBG_TestRequests;
 static float EBG_TestRequestedAt;
 static CompartmentAccessComponent EBG_TestTransition;
 override static bool InTransition(CompartmentAccessComponent access)
 {
  if (access && access == EBG_TestTransition) return true;
  return super.InTransition(access);
 }
 override bool RequestMount(SCR_ChimeraCharacter character, out string reason)
 {
  if (!EBG_TestAcceptWithoutMount) return super.RequestMount(character, reason);
  EBG_TestRequests++;
  EBG_TestRequestedAt = GetGame().GetWorld().GetWorldTime() * 0.001;
  if (EBG_TestHoldTransition) EBG_TestTransition = character.GetCompartmentAccessComponent();
  reason = "Fixture: native request accepted without completing entry";
  return true;
 }
}

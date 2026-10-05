// A temporary action point exists only for its assigned ambient participant.
class EAC_ActivityPointClass : GenericEntityClass {}
class EAC_ActivityPoint : GenericEntity
{
 EAC_CivilianActivity Activity;
 // Set for a lean stop: where the pose is played, as opposed to where this point
 // stands and the resident walks to. See EAC_RoutineAnchors.LeanAgainst.
 bool HasPose;
 vector PoseTransform[4];
}

class EAC_ActivityAction : SCR_LoiterUserAction
{
 // Actions execute on clients too, where the server-owned Activity is absent.
 [Attribute("0", UIWidgets.CheckBox, "Use the vanilla seated loop on existing furniture")]
 protected bool m_bExistingFurniture;
 override bool CanBeShownScript(IEntity user) { return false; }
 override bool CanBePerformedScript(IEntity user)
 {
  EAC_ActivityPoint point = EAC_ActivityPoint.Cast(GetOwner());
  return Replication.IsServer() && point && point.Activity && point.Activity.CanStart(user) && super.CanBePerformedScript(user);
 }
 // Vanilla derives the pose transform from the point's own transform. A lean is
 // played against the wall, 0.35 m from where the point can stand on the navmesh.
 override protected void GetLoiteringPosition(SCR_AISmartActionSentinelComponent smartAction, out vector loiteringPosition[4])
 {
  super.GetLoiteringPosition(smartAction, loiteringPosition);
  EAC_ActivityPoint point = EAC_ActivityPoint.Cast(GetOwner());
  if (!point || !point.HasPose) return;
  loiteringPosition[0] = point.PoseTransform[0]; loiteringPosition[1] = point.PoseTransform[1];
  loiteringPosition[2] = point.PoseTransform[2]; loiteringPosition[3] = point.PoseTransform[3];
 }

 override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
 {
  if (Replication.IsServer() && !CanBePerformedScript(pUserEntity)) return;
  super.PerformAction(pOwnerEntity, pUserEntity);
 }
 override void StartAction(IEntity pUserEntity)
 {
  if (Replication.IsServer() && !CanBePerformedScript(pUserEntity)) return;
  // Vanilla's StartAction starts every loiter with the default animation data, and
  // the default carries no item: the smoking pose then plays with an empty hand and
  // no smoke (owner report 2026-09-19). The cigarette and both of its particle
  // emitters belong to the game's own CIGAR loiter item preset - the one its
  // "Emote_Smoke" command passes - so a smoking stop passes the same preset. Every
  // other pose still goes through vanilla unchanged.
  bool furniture = m_bExistingFurniture;
  if (m_eLoiteringType != ELoiteringType.SMOKING && !furniture) { super.StartAction(pUserEntity); return; }
  SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(pUserEntity);
  if (!character) return;
  SCR_CharacterControllerComponent controller = SCR_CharacterControllerComponent.Cast(character.GetCharacterController());
  if (!controller || !m_isValid) return;
  m_isValid = false;
  if (furniture)
  {
   controller.StartLoitering(GetOwner(), EAC_FurnitureSeat.LOITER_TYPE, m_bHolsterWeapon, true, true, m_targetPosition);
   return;
  }
  SCR_LoiterCustomAnimData cigarette = SCR_LoiterCustomAnimData.CreateInstance(-1, -1, SCR_CustomAnimData_Properties.BINDING_COMMAND_NAME, SCR_CustomAnimData_Properties.BINDING_NAME_NPC, ResourceName.Empty, ResourceName.Empty, -1, -1, string.Empty, SCR_ELoiterItemID.CIGAR);
  controller.StartLoitering(GetOwner(), m_eLoiteringType, m_bHolsterWeapon, true, true, m_targetPosition, false, cigarette);
 }
}

class EAC_CivilianActivity
{
 // Straight-line obstruction sweep applies to hops up to this length (m).
 static const float BODY_SWEEP_METRES = 12;
 // The approach finished and the loiter action was performed: how long the native
 // entry may take before the request is re-issued once and then given up on.
 // Vanilla queues a loiter behind alignment and equipment, which is seconds; the
 // 90-240 s entry deadline was never meant to cover a pose that simply never
 // started (audit A8).
 static const float POSE_WINDOW_SECONDS = 15;
 // A stop that was told to end and whose loiter never reports stopped holds its
 // slot and blocks caching for ever (audit item 9). This is how long after the
 // drain began - and never later than m_EndAt + this - the slot is released
 // anyway, under ABORT_DRAIN.
 static const float DRAIN_WINDOW_SECONDS = 60;
 // The anchor sweep is bounded by EAC_RoutineAnchors.TakeProbe, a budget shared
 // across every resident and spent per scheduler tick. It used to be a per-call
 // cap of three, which bounded the wrong axis and cost a routine start up to six
 // retries; see the note on EAC_RoutineAnchors.TICK_BUDGET.
 // Campaign 118. m_Point doubles as the ANIMATION ROOT for a seat leg, and
 // vanilla's Behavior_Animate.bt resolves that root on its own tick. The seat
 // object knows when the root is still borrowed, but the seat is a ref this class
 // nulls on several paths - so the deadline is mirrored HERE, on the object that
 // actually owns the point, and survives m_VanillaSeat going away. Stamped while
 // a seat holds the root and consulted immediately before every deletion.
 protected float m_RootHoldUntil;
 static const ResourceName SMOKE_POINT = "{CA1A000000000040}Prefabs/EXPAC/EAC_SmokePoint.et";
 protected EAC_ResidentClaim m_Claim;
 protected SCR_ChimeraCharacter m_Actor;
 protected EAC_ActivityPoint m_Point;
 protected EAC_ActivityAction m_Action;
 protected EAC_RoutineDefinition m_Routine;
 protected ref EAC_ActivityStation m_Station;
 protected ref EAC_VanillaSeat m_VanillaSeat;
 protected EAC_SceneSpot m_FurnitureSeat;
 protected AIWaypoint m_Approach;
 // The group's move-failure guard (plain pointer; the guard registry owns it).
 protected EAC_MoveFailureGuard m_Guard;
 protected float m_EnterDeadline, m_EndAt, m_Dwell, m_ApproachTravel;
 // A8: when the performed loiter action must have produced a pose by, and whether
 // the single re-issue has already been spent. Per stop; cleared by ResetLeg.
 protected float m_PoseDeadline;
 protected bool m_PoseRetried;
 // Item 9: when this stop's drain gives up waiting for the loiter to report
 // stopped, and whether the abort has already been counted. Per stop.
 protected float m_DrainDeadline;
 protected bool m_DrainCounted;
 protected vector m_LastApproachPosition;
 protected bool m_Entered, m_Stopping, m_StopIssued, m_FastStopIssued, m_HadApproach, m_FailedAttemptRecorded;
 protected bool m_SceneHeld;
 // A routine is a chain of occupied stops inside one object. Weak on purpose:
 // EAC_PedestrianActivation holds `ref EAC_CivilianActivity Activity`, so a
 // 'ref' back-pointer here is a retain cycle and no resident is ever freed.
 // Same shape as EAC_ActivityPoint.Activity above, which is also plain.
 protected EAC_PedestrianActivation m_Activation;
 protected int m_Leg;        // 0-based index of the stop being served
 protected int m_Legs;       // stops this routine intends, 1..4
 protected int m_LongLeg;    // the one stop that carries the authored dwell
 protected int m_Stops;      // stops genuinely entered so far
 protected bool m_LegDone;   // this stop's dwell expired normally (chain candidate)
 protected bool m_SeatLeg;   // this stop used the station/seat path; terminates the chain
 protected bool m_Finished;  // FinishRoutine ran once
 // Routine-level, like m_Stops: this routine's supply produced something real -
 // a station was acquired or an activity point was built. A routine that got
 // that far and still entered no stop is a supply/geometry failure, not an
 // idling resident, and FinishRoutine must not charge it ActivityInterval.
 protected bool m_Supplied;
 protected float m_LegSpacing; // minimum separation demanded of THIS stop, metres
 protected float m_RoutineStartedAt, m_RoutineTravel;
 // Audit S3. Monitor runs at 2 Hz for every active resident; GetWaypoints and
 // GetCurrentPath each wanted a fresh array per resident per tick. One scratch
 // array per object, cleared before every fill. Never used by BeginLeg, which
 // can run from inside Monitor through Continue.
 protected ref array<AIWaypoint> m_ScratchOrders = {};
 protected ref array<vector> m_ScratchRoute = {};

 string GetProfileName()
 {
  if (m_FurnitureSeat) return "Rest on existing furniture";
  if (m_VanillaSeat) return "At the shared seat";
  return EAC_ActivityProfiles.Name(m_Routine);
 }
 // One '+=' per term. A single long '+' chain fails Enforce compilation with
 // "Formula too complex", the way every existing Describe* in this addon does it.
 string GetPurpose()
 {
  if (!m_Claim) return GetProfileName();
  string purpose = EAC_ActivityProfiles.Role(m_Claim.Resident);
  purpose += ": ";
  purpose += GetProfileName();
  if (m_Legs > 1)
  {
   purpose += " (stop ";
   purpose += (m_Leg + 1).ToString();
   purpose += "/";
   purpose += m_Legs.ToString();
   purpose += ")";
  }
  return purpose;
 }

 bool OwnsActor()
 {
  EAC_ResidentClaims ledger = EAC_AmbientModule.GetMissionClaims();
  if (!ledger || !m_Claim || ledger.Find(m_Claim.Home, m_Claim.Resident) != m_Claim || m_Claim.Character != m_Actor || !m_Actor || m_Claim.Cache) return false;
  if (!m_Claim.Committed || !m_Claim.OptimizerMember || m_Actor.EBG_WasPlayerControlled() || m_Claim.OptimizerMember.WasPlayer || m_Actor.GetCharacterGroup() != m_Claim.Group) return false;
  CharacterControllerComponent controller = m_Actor.GetCharacterController();
  return controller && !controller.IsPlayerControlled() && !controller.IsDead();
 }

 bool CanStart(IEntity actor)
 {
  return !m_Stopping && !m_VanillaSeat && actor == m_Actor && m_Point && m_Point.Activity == this && OwnsActor();
 }

 // True while the resident is actually in its pose - leaning, smoking, sitting,
 // reading the board - as opposed to walking to it or leaving it.
 bool IsPosed() { return m_Entered && !m_Stopping; }
 // Set when a start was refused for one reason only: the group still carried this
 // resident's own wander leg, which has just been taken off. Nothing else was wrong,
 // so the caller retries in two seconds instead of twenty. Since 0.0.7 residents
 // wander almost continuously, which made this the usual case rather than the rare
 // one: every routine cost twenty seconds of standing still first (Danger fixture
 // 25/1, campaign 0919-151).
 protected bool m_ClearedOwnLeg;
 protected float m_PoseReportAt;
 bool ClearedOwnLeg() { return m_ClearedOwnLeg; }

 bool BlocksWalking()
 {
  if (!m_Stopping || m_Approach) return true;
  // A chain continuation is pending. In that window the pose has exited and the
  // point delete is not yet confirmed, so the group holds no waypoint; without
  // this clause MaintainNext injects a wander leg and the next BeginLeg refuses
  // on orders.IsEmpty(), silently collapsing every chain to a single stop.
  if (m_LegDone && !m_Finished) return true;
  if (!m_Actor) return false;
  SCR_CharacterControllerComponent controller = SCR_CharacterControllerComponent.Cast(m_Actor.GetCharacterController());
  if (controller && (controller.IsLoitering() || (controller.GetScrInputContext() && controller.GetScrInputContext().m_iLoiteringType >= 0))) return true;
  CompartmentAccessComponent access = m_Actor.GetCompartmentAccessComponent();
  return access && (access.IsInCompartment() || access.IsGettingIn() || access.IsGettingOut());
 }

 bool CanInterrupt(IEntity actor, EAC_ActivityPoint point)
 {
  return Replication.IsServer() && actor == m_Actor && point && point == m_Point && point.Activity == this && OwnsActor();
 }

 bool Start(EAC_AmbientModule module, EAC_PedestrianActivation activation, float now)
 {
  // Every gate here sits ABOVE BeginLeg and was invisible: TryActivity re-arms
  // NextActivity before calling Start, so a refusal on any of these lines looks
  // exactly like "the retry has not come round yet" from the idle ladder. The
  // Danger 23/1 and campaign 127 diagnoses both stalled on that. Named now.
  if (!Replication.IsServer() || module != EAC_AmbientModule.GetActive()) { RefuseStart(activation, EAC_RoutineStats.BLG_START_MODULE); return false; }
  if (activation.Activity || activation.PlayerTouched) { RefuseStart(activation, EAC_RoutineStats.BLG_START_HELD); return false; }
  if (activation.Claim.AlarmUntil > now) { RefuseStart(activation, EAC_RoutineStats.BLG_START_ALARM); return false; }
  m_Claim = activation.Claim; m_Actor = SCR_ChimeraCharacter.Cast(m_Claim.Character);
  if (now < m_Claim.Resident.NextRoutineAt) { RefuseStart(activation, EAC_RoutineStats.BLG_START_COOLDOWN); return false; }
  // Once per world, against a real civilian: does any loaded ACE animation
  // qualify as a pose? Creates nothing, a no-op when ACE is absent; the result
  // is the probe counters and the compatibility line, not a new routine. It
  // lives in this preamble so it runs once per routine, not once per stop.
  if (m_Actor) { EAC_AceAnimations.Detect(m_Actor); }
  m_Activation = activation;
  m_RoutineStartedAt = now;
  m_Leg = 0; m_Stops = 0; m_Supplied = false;
  // The plan is a pure function of (Id, ActivityStep) and is never stored on the
  // resident record, so a cached resident resumes at the correct next place in
  // its excursion with nothing new to migrate.
  int span = 1;
  if (module) span = module.RoutineStops;
  m_Legs = EAC_ActivityProfiles.RoutineStops(m_Claim.Resident, span);
  m_LongLeg = EAC_ActivityProfiles.LongStop(m_Claim.Resident, m_Legs);
  // Counted only once the first stop actually opened. A routine that never
  // created anything is not a started routine, and counting it there made
  // planned>0 with legs==0 unreadable.
  bool started = BeginLeg(module, now);
  int plannedLegs = m_Legs;
  if (started) EAC_RoutineStats.RecordRoutineStarted(plannedLegs);
  return started;
 }

 // Per-stop state only. The routine-level fields (m_Leg, m_Legs, m_LongLeg,
 // m_Stops, m_Finished, m_RoutineStartedAt, m_RoutineTravel) are untouched.
 protected void ResetLeg()
 {
  m_Point = null; m_Action = null; m_Approach = null;
  m_Station = null; m_VanillaSeat = null;
  m_FurnitureSeat = null;
  // Audit S9. The guard belongs to EAC_MoveFailureGuard's registry, which may
  // rebind the slot once this group is gone. Holding the pointer past the order
  // it was attached for is how a stale guard reaches a later stop.
  m_Guard = null;
  m_Entered = false; m_Stopping = false; m_StopIssued = false; m_FastStopIssued = false;
  m_HadApproach = false; m_FailedAttemptRecorded = false;
  m_LegDone = false; m_SeatLeg = false;
  // The minimum-travel proof is per stop, not per routine. Forgetting this makes
  // every stop after the first abort in BeginOccupation and silently collapses
  // the whole feature back to single-occupation routines.
  m_ApproachTravel = 0; m_SceneHeld = false;
  m_RootHoldUntil = 0;
  m_PoseDeadline = 0; m_PoseRetried = false;
  m_DrainDeadline = 0; m_DrainCounted = false;
 }

 // Expected refusals belong in the bounded periodic counters, not a line for
 // every cooldown/held/alarm retry while detailed diagnostics are enabled.
 protected void RefuseStart(EAC_PedestrianActivation activation, int gate)
 {
  EAC_RoutineStats.RecordBeginLegGate(gate);
 }

 protected void RefuseLeg(int gate)
 {
  EAC_RoutineStats.RecordBeginLegGate(gate);
 }

 // Today's Start body, opening one stop of the chain. Return semantics are
 // unchanged: true means "this object owns something, keep monitoring", false
 // means "nothing was created, discard me", and every post-point failure path
 // keeps the "m_Stopping = true; return true;" shape so a half-built stop can
 // never leak a point.
 protected bool BeginLeg(EAC_AmbientModule module, float now)
 {
  ResetLeg();
  m_Routine = EAC_ActivityProfiles.Select(m_Claim.Resident);
  if (!m_Routine) { RefuseLeg(EAC_RoutineStats.BLG_ROUTINE); return false; }
  // Someone nearby is already sitting at a shared spot with a free position.
  // Joining them is the social behaviour the station exists for, and the only
  // path by which its second position is ever taken.
  bool invited = EAC_ActivityStation.InviteOpen(m_Actor.GetOrigin(), now);
  if (invited && !EAC_ActivityProfiles.NeedsTable(m_Routine))
  {
   EAC_RoutineDefinition joined = EAC_RoutineCatalog.SelectTable(m_Claim.Resident);
   if (joined) { m_Routine = joined; EAC_RoutineStats.RecordTable(0, 0, 1); }
  }
  // Record what the catalog chose before any substitution below, otherwise a
  // corner routine that loses its animation is miscounted as an open-yard one.
  EAC_RoutineStats.RecordWanted(EAC_ActivityProfiles.Anchor(m_Routine));
  if (EAC_ActivityProfiles.NeedsTable(m_Routine)) EAC_RoutineStats.RecordTableWanted();
  EAC_RoutineStats.RecordStep(m_Claim.Resident.ActivityStep);
  if (!OwnsActor() || EAC_CivilianDanger.HasCombatThreat(m_Actor) || !EAC_PedestrianSpawner.IsCivilian(m_Actor, m_Claim.Group) || !m_Claim.Home.BuildingEntity || vector.Distance(m_Actor.GetOrigin(), m_Claim.Home.BuildingEntity.GetOrigin()) > module.RoutineRange) { RefuseLeg(EAC_RoutineStats.BLG_OWNERSHIP); return false; }
  SCR_CharacterControllerComponent controller = SCR_CharacterControllerComponent.Cast(m_Actor.GetCharacterController());
  if (!controller) { RefuseLeg(EAC_RoutineStats.BLG_CONTROLLER); return false; }
  // Audit S2. BlocksWalking at :134 already null-checks this; these two sites did
  // not, and a character whose scripted input context has not been created yet
  // (a freshly spawned or possession-restored actor) crashed the whole scheduler
  // tick rather than refusing one routine start.
  SCR_ScriptedCharacterInputContext startInput = controller.GetScrInputContext();
  if (!startInput) { RefuseLeg(EAC_RoutineStats.BLG_CONTROLLER); return false; }
  if (controller.IsLoitering() || startInput.m_iLoiteringType >= 0 || CompartmentAccessComponent.GetVehicleIn(m_Actor) || !EBG_SimulationCache.Unsupported(m_Actor, false).IsEmpty()) { RefuseLeg(EAC_RoutineStats.BLG_CONTROLLER); return false; }
  // An unsupported animation degrades to the open-ground idle slot rather
  // than substituting an arbitrary command on this character.
  if (!controller.CanPlayLoiterAnimation(EAC_ActivityProfiles.Animation(m_Routine))) m_Routine = EAC_RoutineCatalog.Fallback();
  // Counted, and nothing else: this returns false before anything is created, so
  // Start returns false and TryActivity charges START_RETRY_SECONDS (20 s), not a
  // rest. That is exactly the cost a resident should pay while a shelter stand-up
  // debt is still keeping it out of STAND (audit item 7).
  if (!m_Routine || !controller.CanPlayLoiterAnimation(EAC_ActivityProfiles.Animation(m_Routine)))
  {
   RefuseLeg(EAC_RoutineStats.BLG_ANIM);
   return false;
  }
  // The actor's own standing position must be outside exclusion zones and
  // out of water, nothing more: the body-box test in ClearFloor is a rule for
  // placing NEW points, and applied here it refused every routine start for a
  // resident standing beside a fence or on a doorstep (campaign 86, probe
  // gate=beginleg.clear_floor, ten free surveyed spots at its home).
  vector standing = m_Actor.GetOrigin();
  if (!EAC_ExclusionZone.IsPopulationAllowed(standing)) { RefuseLeg(EAC_RoutineStats.BLG_STANDING); return false; }
  if (ChimeraWorldUtils.TryGetWaterSurfaceSimple(module.GetWorld(), standing)) { RefuseLeg(EAC_RoutineStats.BLG_STANDING); return false; }
  ActionsPerformerComponent performer = ActionsPerformerComponent.Cast(m_Actor.FindComponent(ActionsPerformerComponent));
  if (!performer) { RefuseLeg(EAC_RoutineStats.BLG_PERFORMER); return false; }
  // Never remove an order and add one in the same script call. Stopping a live
  // wander leg here and adding the approach below used to happen in one tick;
  // the group's next tick then reported the wander move as EMoveError.UNKNOWN,
  // the guard stamped the fresh approach as the failed order, Monitor matched
  // it and retired a good stop (run 93 diagnosis, cause b).
  //
  // The live leg is no longer stopped here either (audit item 8). Stopping it
  // and then refusing threw away real movement and left the resident standing
  // for the whole hold with no counter; the leg is bounded by its own 30 s
  // deadline and EAC_PedestrianWalk.Monitor clears it, so the group empties on
  // a later tick than any add - which is the only property the two-tick rule
  // actually needs. The hold keeps the walker from issuing a FRESH leg past the
  // retry, so the retry finds the group empty. Bounded: one hold per refusal.
  array<AIWaypoint> orders = {}; m_Claim.Group.GetWaypoints(orders);
  if (!orders.IsEmpty())
  {
   float hold = now + EAC_PedestrianSpawner.START_RETRY_SECONDS + 10;
   // YieldTo, not HoldUntil: HoldUntil writes the walker's backoff field, which
   // the walker's own Monitor and ResetBackoff overwrite within seconds, so the
   // refusal never actually reserved the group (Danger fixture 23/1).
   m_Activation.Walking.YieldTo(hold);
   // When the ONLY thing on the group is the walker's own wander leg, take it off
   // now. Nothing is added in this call - the start still refuses this tick - so
   // the two-tick rule that run 93 established is intact, and the yield above
   // keeps a replacement leg from appearing before the retry. Without this the
   // retry and the wander leg simply chase each other for ever.
   if (m_Activation.Walking.OwnsOnly(orders))
   {
    m_Activation.Walking.Stop();
    EAC_RoutineStats.RecordBeginLegGate(EAC_RoutineStats.BLG_ORDERS_CLEARED);
    m_ClearedOwnLeg = true;
    return false;
   }
   EAC_RoutineStats.RecordBeginLegGate(EAC_RoutineStats.BLG_ORDERS);
   return false;
  }
  EntitySpawnParams params = new EntitySpawnParams(); params.TransformMode = ETransformMode.WORLD;
  m_Actor.GetWorldTransform(params.Transform);
  // Stop 0 keeps the historic 8 m rule so a single-stop routine behaves exactly
  // as it did. Later stops in a chain must be visibly further apart, which is
  // what makes the walk between them readable rather than a shuffle.
  m_LegSpacing = 8;
  if (m_Leg > 0 && module) { m_LegSpacing = module.RoutineLegSpacing; }
  // An open invitation in reach goes to the waiting neighbour's shared spot.
  // Taking a furniture seat somewhere else instead left the host sitting alone.
  if (EAC_ActivityProfiles.NeedsTable(m_Routine) && (invited || !FurnitureAnchor(module, params.Transform)))
  {
   int slot;
   // Audit B2. `deferred` means the interior probe budget for this tick ran out
   // before the ladder finished, not that the household has no shared spot. It
   // must not be charged as a failed table attempt, or a budget stop would
   // silently retire the resident's table purpose after three ticks.
   bool stationDeferred;
   m_Station = EAC_ActivityStation.Acquire(m_Claim, slot, stationDeferred);
   // A seat or station stop always terminates the routine: chaining past it
   // would subject EAC_VanillaSeat's cancel and ROOT_GRACE hold to a follow-on
   // the fourteen-campaign-proven path has never run.
   m_SeatLeg = m_Station != null;
   if (m_Station)
   {
    // Real supply was produced for this routine, whatever happens to it now.
    m_Supplied = true;
    m_Activation.Activity = this;
    m_Station.Position(slot, params.Transform);
    EAC_RoutineStats.RecordSeatGate(EAC_RoutineStats.SEAT_ACQUIRED);
    // The shared spot sits inside the resident's own home and most residents
    // spawn indoors, so the minimum-travel rule is the prime suspect. Counted
    // apart from the other approach conditions so the log can say so outright.
    if (m_Claim.Resident.HasActivityPosition && vector.DistanceXZ(m_Actor.GetOrigin(), params.Transform[3]) < 9)
     EAC_RoutineStats.RecordSeatGate(EAC_RoutineStats.SEAT_TOONEAR);
    if (!CanApproach(module, params.Transform[3]))
    {
     EAC_RoutineStats.RecordSeatGate(EAC_RoutineStats.SEAT_APPROACH);
     RequestStop(); return true;
    }
   }
   else
   {
    if (!stationDeferred) RecordFailedAttempt();
    RefuseLeg(EAC_RoutineStats.BLG_STATION);
    return false;
   }
  }
  else if (!m_FurnitureSeat && !OutdoorAnchor(module, params.Transform)) { RefuseLeg(EAC_RoutineStats.BLG_ANCHOR); return false; }
  if (!controller.CanPlayLoiterAnimation(EAC_ActivityProfiles.Animation(m_Routine)))
  {
   // OutdoorAnchor may already hold a surveyed position. Returning without
   // releasing it leaks the lease until the world ends.
   if (!m_Station) { ReleaseScene(); RefuseLeg(EAC_RoutineStats.BLG_ANIM); return false; }
   EAC_RoutineStats.RecordSeatGate(EAC_RoutineStats.SEAT_ANIM);
   RequestStop(); return true;
  }
  bool hasPose; vector poseTransform[4];
  // A lean needs a wall under the shoulder; without one the stop keeps its place
  // and plays the standing idle instead (see EAC_RoutineAnchors.LeanAgainst).
  if (!m_Station && m_Routine && (m_Routine.Anim == EAC_ERoutineAnim.LEAN_LEFT || m_Routine.Anim == EAC_ERoutineAnim.LEAN_RIGHT))
  {
   bool leftShoulder = m_Routine.Anim == EAC_ERoutineAnim.LEAN_LEFT;
   vector pose[4]; pose[0] = params.Transform[0]; pose[1] = params.Transform[1]; pose[2] = params.Transform[2]; pose[3] = params.Transform[3];
   vector approach, reachable;
   AIPathfindingComponent leanPath = AIPathfindingComponent.Cast(m_Claim.Group.FindComponent(AIPathfindingComponent));
   hasPose = EAC_RoutineAnchors.LeanAgainst(module.GetWorld(), m_Actor, leftShoulder, pose, approach);
   // The resident walks to a point it can actually reach: the one just off the
   // wall when the navmesh carries it, otherwise the anchor itself if the pose is
   // within a short side-step of it.
   if (hasPose && leanPath && leanPath.GetClosestPositionOnNavmesh(approach, "0.5 1 0.5", reachable) && vector.Distance(approach, reachable) <= 0.3)
   {
    params.Transform[0] = pose[0]; params.Transform[1] = pose[1]; params.Transform[2] = pose[2]; params.Transform[3] = reachable;
   }
   else if (hasPose && vector.DistanceXZ(params.Transform[3], pose[3]) > 1.0) hasPose = false;
   if (hasPose) { poseTransform[0] = pose[0]; poseTransform[1] = pose[1]; poseTransform[2] = pose[2]; poseTransform[3] = pose[3]; }
   else
   {
    EAC_RoutineDefinition upright = EAC_RoutineCatalog.Fallback();
    if (upright && controller.CanPlayLoiterAnimation(EAC_ActivityProfiles.Animation(upright))) m_Routine = upright;
   }
  }
  ResourceName pointPrefab = EAC_ActivityProfiles.Point(m_Routine);
  if (m_FurnitureSeat)
  {
   hasPose = true; m_SeatLeg = true;
   Math3D.MatrixCopy(m_FurnitureSeat.PoseTransform, poseTransform);
   pointPrefab = "{C61573B2EA0F408D}Prefabs/EXPAC/EAC_FurniturePoint.et";
  }
  m_Point = EAC_ActivityPoint.Cast(GetGame().SpawnEntityPrefab(Resource.Load(pointPrefab), module.GetWorld(), params));
  if (!m_Point)
  {
   // Same lease as above: nothing owns the surveyed position once this object
   // is discarded, so hand it back before returning false.
   if (!m_Station) { ReleaseScene(); RefuseLeg(EAC_RoutineStats.BLG_ROUTINE); return false; }
   EAC_RoutineStats.RecordSeatGate(EAC_RoutineStats.SEAT_POINT);
   m_Stopping = true; return true;
  }
  // Retain cleanup immediately after creation, including partial setup failure.
  // The point exists: this routine produced supply and can never be charged the
  // no-supply rest again, whichever way the stop ends (audit item 1).
  m_Supplied = true;
  m_Activation.Activity = this; m_Point.Activity = this;
  if (hasPose)
  {
   m_Point.HasPose = true;
   m_Point.PoseTransform[0] = poseTransform[0]; m_Point.PoseTransform[1] = poseTransform[1]; m_Point.PoseTransform[2] = poseTransform[2]; m_Point.PoseTransform[3] = poseTransform[3];
  }
  ActionsManagerComponent manager = ActionsManagerComponent.Cast(m_Point.FindComponent(ActionsManagerComponent));
  array<BaseUserAction> actions = {};
  if (manager) manager.GetActionsList(actions);
  foreach (BaseUserAction action : actions)
  {
   m_Action = EAC_ActivityAction.Cast(action);
   if (m_Action) break;
  }
  if (!m_Action) { if (m_Station) EAC_RoutineStats.RecordSeatGate(EAC_RoutineStats.SEAT_ACTION); m_Stopping = true; return true; }
  AIControlComponent control = AIControlComponent.Cast(m_Actor.FindComponent(AIControlComponent));
  AIAgent agent = control.GetAIAgent();
  // Exactly one stop in a routine carries the catalog's authored dwell; the rest
  // are short, so a routine reads as "went out, did a few things, settled
  // somewhere". A table slot always keeps its authored band.
  m_Dwell = EAC_ActivityProfiles.StopDwell(m_Routine, m_Claim.Resident, m_Leg, m_LongLeg);
  // Hoisted so the counter argument is a local, not an instance read inside a
  // static call, and so both pins provably ask for the same span.
  float pinSeconds = m_Dwell + 90;
  agent.PreventMaxLOD(pinSeconds); agent.SetLOD(0); agent.ActivateAI();
  m_Claim.Group.PreventMaxLOD(pinSeconds); m_Claim.Group.SetLOD(0); m_Claim.Group.ActivateAI();
  // Two issues, agent and group. Counted only; the pin itself is unchanged and
  // stays unchanged until a campaign says what it costs.
  EAC_RoutineStats.RecordPinnedSeconds(pinSeconds);
  EAC_RoutineStats.RecordPinnedSeconds(pinSeconds);
  // A walk to a neighbour's scene legitimately takes longer than one across
  // the garden. A fixed 90 s would abort the routine with the resident metres
  // short, which reads worse than the under-varied loitering it replaced.
  m_EnterDeadline = now + Math.Clamp(60 + vector.Distance(m_Actor.GetOrigin(), params.Transform[3]) * 1.6, 90, 240);
  m_EndAt = m_EnterDeadline + m_Dwell;
  m_LastApproachPosition = m_Actor.GetOrigin();
  // All-vanilla, and the only seat machine. The legacy compartment path and its
  // third-party helper prefab were removed once campaigns 46 and 47 measured the
  // native route posing every seat it issued.
  if (m_Station && EAC_VanillaSeat.Enabled(module.VanillaSeating))
  {
   m_VanillaSeat = new EAC_VanillaSeat();
   vector nativeTransform[4]; m_Point.GetWorldTransform(nativeTransform);
   if (!m_VanillaSeat.Prepare(m_Actor, m_Claim.Group, m_Point, nativeTransform, module.VanillaSeating, m_Dwell))
   {
    m_VanillaSeat = null;
    EAC_RoutineStats.RecordSeatGate(EAC_RoutineStats.SEAT_PREPARE); RequestStop(); return true;
   }
  }
  if (vector.Distance(m_Actor.GetOrigin(), m_Point.GetOrigin()) > 1)
  {
   EntitySpawnParams orderParams = new EntitySpawnParams(); orderParams.TransformMode = ETransformMode.WORLD;
   Math3D.MatrixIdentity4(orderParams.Transform); orderParams.Transform[3] = m_Point.GetOrigin();
   m_Approach = AIWaypoint.Cast(GetGame().SpawnEntityPrefab(Resource.Load("{750A8D1695BD6998}Prefabs/AI/Waypoints/AIWaypoint_Move.et"), module.GetWorld(), orderParams));
   if (!m_Approach) { m_Stopping = true; return true; }
   m_HadApproach = true;
   m_Approach.SetCompletionRadius(0.6); m_Claim.Group.AddWaypoint(m_Approach);
   // Once per group, before the order can fail: a native path failure on this
   // approach then ends the stop instead of the mission's scripts.
   m_Guard = EAC_MoveFailureGuard.Attach(m_Claim.Group, "approach");
  }
  else BeginOccupation();
  return true;
 }

 protected void BeginOccupation()
 {
  if (m_Stopping || !m_Point || !OwnsActor()) { RequestStop(); return; }
  if (m_FurnitureSeat && !EAC_FurnitureSeat.SurfaceClear(m_FurnitureSeat, m_Actor)) { RequestStop(); return; }
  if (!EAC_ActivityStation.OffRoad(m_Point.GetOrigin()) || !EAC_ExclusionZone.IsPopulationAllowed(m_Point.GetOrigin()) || (m_Claim.Resident.HasActivityPosition && m_ApproachTravel < 8)) { RequestStop(); return; }
  if (m_Station)
  {
   if (!m_Station.Contains(m_Actor)) { EAC_RoutineStats.RecordSeatGate(EAC_RoutineStats.SEAT_CONTAINS); RequestStop(); return; }
  }
  if (m_VanillaSeat)
  {
   float began = GetGame().GetWorld().GetWorldTime() * 0.001;
   if (!m_VanillaSeat.Begin(began)) { EAC_RoutineStats.RecordSeatGate(EAC_RoutineStats.SEAT_ENTER); RequestStop(); return; }
   EAC_RoutineStats.RecordSeatGate(EAC_RoutineStats.SEAT_OCCUPIED);
   // The three native rungs each own a window. A long approach must not consume
   // the entry allowance and retire the ladder before its last rung was tried.
   float ladder = began + EAC_VanillaSeat.RUNG_SECONDS * 3 + 6;
   if (ladder > m_EnterDeadline) m_EnterDeadline = ladder;
   if (m_EndAt < m_EnterDeadline + m_Dwell) m_EndAt = m_EnterDeadline + m_Dwell;
   return;
  }
  if (!CanStart(m_Actor)) { RequestStop(); return; }
  ActionsPerformerComponent performer = ActionsPerformerComponent.Cast(m_Actor.FindComponent(ActionsPerformerComponent));
  if (!performer || !m_Action) { RequestStop(); return; }
  performer.PerformAction(m_Action);
  // A8. The request is in; native entry is seconds, not minutes. Monitor re-issues
  // this once at the window and then aborts, instead of leaving the resident stood
  // at its own stop until the 90-240 s entry deadline.
  float performedAt = GetGame().GetWorld().GetWorldTime() * 0.001;
  m_PoseDeadline = performedAt + POSE_WINDOW_SECONDS;
 }

 protected bool CanApproach(EAC_AmbientModule module, vector destination)
 {
  vector start = m_Actor.GetOrigin();
  // Split so a table occupation records which clause refused it. Outdoor
  // routines are not counted; they have their own anchor counters.
  bool table = m_Station != null;
  // Taking the free position at another household's shared spot is a visit, and
  // may reach EAC_ActivityStation.JOIN_RANGE (see the note there). The walker
  // brings a visitor back inside its own leash afterwards (EAC_PedestrianWalk).
  float reach = module.RoutineRange;
  if (table && m_Station.GetHome() != m_Claim.Home) reach = Math.Max(reach, EAC_ActivityStation.JOIN_RANGE);
  if (vector.Distance(start, destination) > reach) { EAC_RoutineStats.RecordApproach(EAC_RoutineStats.APR_RANGE); return false; }
  // Both rules apply per stop, against the spacing this stop demands: 8 m for
  // stop 0, RoutineLegSpacing for every later stop in a chain.
  if (!EAC_ActivityProfiles.SeparatedBy(m_Claim.Resident, destination, m_LegSpacing)) { EAC_RoutineStats.RecordApproach(EAC_RoutineStats.APR_ANCHOR); return false; }
  if (m_Claim.Resident.HasActivityPosition && vector.DistanceXZ(start, destination) < m_LegSpacing + 1) { EAC_RoutineStats.RecordApproach(EAC_RoutineStats.APR_NEAR); return false; }
  if (!EAC_ActivityStation.OffRoad(destination)) { EAC_RoutineStats.RecordApproach(EAC_RoutineStats.APR_ROAD); return false; }
  if (!EAC_ExclusionZone.IsTransitAllowed(start, destination)) { EAC_RoutineStats.RecordApproach(EAC_RoutineStats.APR_TRANSIT); return false; }
  if (!EAC_ExclusionZone.IsPopulationAllowed(destination)) { EAC_RoutineStats.RecordApproach(EAC_RoutineStats.APR_ZONE); return false; }
  // The leash is a disc around the resident's own home. Chained scenes live in
  // neighbours' records, whose positions sit beyond that record's centre, so a
  // hard 60 reserved spots and then rejected them - the beat was wasted and the
  // resident went inert. One configurable radius now governs both.
  if (!m_Claim.Home.BuildingEntity || vector.Distance(destination, m_Claim.Home.BuildingEntity.GetOrigin()) > reach) { EAC_RoutineStats.RecordApproach(EAC_RoutineStats.APR_LEASH); return false; }
  // A place the native pathing already failed to reach this mission. Every
  // destination passes through here, so one check covers the station slot, the
  // surveyed spot, the anchor probes and the legacy ring alike.
  if (EAC_MoveFailureGuard.IsRetired(destination)) { EAC_RoutineStats.RecordApproach(EAC_RoutineStats.APR_RETIRED); return false; }
  AIPathfindingComponent path = AIPathfindingComponent.Cast(m_Claim.Group.FindComponent(AIPathfindingComponent));
  vector projected;
  if (!path) { EAC_RoutineStats.RecordApproach(EAC_RoutineStats.APR_NAV); return false; }
  // The move starts where the actor stands. A standing position more than half
  // a metre off the navmesh (a pose exit, a doorstep, furniture, an indoor floor)
  // is counted but no longer refused: campaign 96 refused 129 starts this way
  // and two residents stood 90+ s indoors being refused every 20 s, while every
  // native UNKNOWN in campaigns 95/96 was a waypoint the mod had itself removed.
  // A start that really cannot path is defused by EAC_MoveFailureGuard.
  if (!path.GetClosestPositionOnNavmesh(start, "0.5 1 0.5", projected) || vector.Distance(start, projected) > 0.5) EAC_RoutineStats.RecordApproach(EAC_RoutineStats.APR_NAV_START);
  if (!path.GetClosestPositionOnNavmesh(destination, "0.5 1 0.5", projected) || vector.Distance(destination, projected) > 0.5) { EAC_RoutineStats.RecordApproach(EAC_RoutineStats.APR_NAV); return false; }
  // Indoor waypoints must reach a doorway through native navigation; a straight
  // body trace from the garden would incorrectly reject the house's outer wall.
  // The same holds in reverse: an actor standing inside the house would fail the
  // sweep against its own wall in every direction and get no routine at all.
  if (m_Station || EAC_PedestrianSpawner.InsideHouse(module.GetWorld(), m_Claim.Home.BuildingEntity, start, m_Actor) || EAC_PedestrianSpawner.InsideHouse(module.GetWorld(), m_Claim.Home.BuildingEntity, destination, m_Actor))
  {
   EAC_RoutineStats.RecordApproach(EAC_RoutineStats.APR_OK);
   return true;
  }
  // The straight body sweep only makes sense for a short hop: a wall between
  // the actor and a point 8 m away means a wasted stop. A continuation is
  // placed 15 m or further and almost always has a fence or a house corner on
  // the straight line, which native pathing simply walks around; campaign 77
  // measured body=269 against ok=31 with chains capped at two stops. Longer
  // legs are already bounded by the navmesh projection above, the 64-segment
  // route cap in Monitor and the entry deadline.
  float straight = vector.Distance(start, destination);
  if (straight <= BODY_SWEEP_METRES)
  {
   TraceBox trace = new TraceBox(); trace.Start = start; trace.End = destination;
   trace.Mins = "-0.3 0.2 -0.3"; trace.Maxs = "0.3 1.8 0.3";
   trace.Flags = TraceFlags.WORLD | TraceFlags.ENTS; trace.Exclude = m_Actor;
   float swept = module.GetWorld().TraceMove(trace, null);
   if (swept < 1) { EAC_RoutineStats.RecordApproach(EAC_RoutineStats.APR_BODY); return false; }
  }
  EAC_RoutineStats.RecordApproach(EAC_RoutineStats.APR_OK);
  return true;
 }

 // Sweep the routine's own anchor kind, then open ground, so an unavailable
 // wall, corner or canopy degrades the routine instead of cancelling it.
 protected bool FurnitureAnchor(EAC_AmbientModule module, out vector transform[4])
 {
  if (!module || module.SceneSurvey <= 0) return false;
  SCR_CharacterControllerComponent controller = SCR_CharacterControllerComponent.Cast(m_Actor.GetCharacterController());
  if (!controller || !controller.CanPlayLoiterAnimation(EAC_FurnitureSeat.LOITER_TYPE)) return false;
  float now = module.GetWorld().GetWorldTime() * 0.001;
  for (int attempt = 0; attempt < 3; attempt++)
  {
   int homeId;
   if (!EAC_SceneItinerary.Reserve(module, m_Claim, m_Routine, now, transform, homeId)) return false;
   EAC_SceneSpot seat = EAC_SceneIndex.HeldSeat(m_Claim.Resident.Id);
   if (seat && CanApproach(module, transform[3]))
   {
    m_FurnitureSeat = seat; m_SceneHeld = true;
    EAC_SceneItinerary.Commit(m_Claim.Resident, homeId);
    return true;
   }
   EAC_SceneIndex.ReleaseAllFor(m_Claim.Resident.Id);
  }
  return false;
 }

 protected bool OutdoorAnchor(EAC_AmbientModule module, out vector transform[4])
 {
  AIPathfindingComponent path = AIPathfindingComponent.Cast(m_Claim.Group.FindComponent(AIPathfindingComponent));
  if (!path) return false;
  IEntity home = m_Claim.Home.BuildingEntity;
  // Pass 0: a position surveyed and verified ahead of demand. Falls through to
  // the existing probe sweeps whenever the home is unsurveyed, barren or full,
  // so behaviour with an empty index is exactly what it was before.
  if (module.SceneSurvey == 1 && !m_SceneHeld)
  {
   // Up to three surveyed candidates. A released spot carries a short
   // re-reservation cooldown, so each attempt is offered a different one; this
   // is the only supply that can place a continuation on a neighbour's plot
   // (campaign 72: the single second stop came from here).
   float sceneNow = module.GetWorld().GetWorldTime() * 0.001;
   // The previous attempt's position, so a retry that is handed the same spot
   // again is counted rather than merely suspected. EAC_SceneIndex.Release and
   // ReleaseAllFor now leave a 20 s anti-repeat stamp behind instead of zeroing
   // it, which is what makes the three attempts three different offers.
   // Every previous offer of THIS attempt loop, not merely the last one: with
   // three attempts, comparing n against n-1 alone reports "different" for the
   // A,B,A sequence the anti-repeat stamp is specifically supposed to prevent.
   vector offeredA = vector.Zero;
   vector offeredB = vector.Zero;
   int offers = 0;
   for (int attempt = 0; attempt < 3; attempt++)
   {
    vector reserved[4];
    int sceneHome;
    if (!EAC_SceneItinerary.Reserve(module, m_Claim, m_Routine, sceneNow, reserved, sceneHome)) break;
    if (offers > 0)
    {
     bool repeated = vector.DistanceXZ(offeredA, reserved[3]) < 1;
     if (!repeated && offers > 1) repeated = vector.DistanceXZ(offeredB, reserved[3]) < 1;
     EAC_RoutineStats.RecordSceneRetry(repeated);
    }
    if (offers == 0) offeredA = reserved[3];
    else if (offers == 1) offeredB = reserved[3];
    offers++;
    if (CanApproach(module, reserved[3]))
    {
     for (int axis = 0; axis < 4; axis++) transform[axis] = reserved[axis];
     // Audit S1. The itinerary's retained chain moves on only for a reservation
     // this stop actually keeps. Committing inside Reserve advanced SceneHomeId
     // for every offer CanApproach then rejected, so a resident whose first two
     // offers were unreachable had its excursion retargeted by a refusal.
     EAC_SceneItinerary.Commit(m_Claim.Resident, sceneHome);
     m_SceneHeld = true;
     return true;
    }
    EAC_SceneIndex.ReleaseAllFor(m_Claim.Resident.Id);
   }
  }
  EAC_EAnchorKind wanted = EAC_ActivityProfiles.Anchor(m_Routine);
  // Audit B2. This used to run the whole two-pass sweep in one call: sixteen
  // EAC_RoutineAnchors.Find calls, each up to roughly fifteen short traces plus a
  // road query, for every idle resident every twenty seconds. The sweep is now
  // spent ANCHOR_BUDGET probes at a time and resumes where it stopped, with the
  // cursor on the retained resident record rather than on this object - a refused
  // start returns false and the activity is discarded, so an object-local cursor
  // would restart the sweep from the same bearings for ever.
  int steps = EAC_RoutineAnchors.PROBES * 2;
  if (wanted == EAC_EAnchorKind.YARD_OPEN) steps = EAC_RoutineAnchors.PROBES;
  int cursor = m_Claim.Resident.AnchorCursor;
  if (cursor < 0 || cursor >= steps) cursor = 0;
  bool swept = false;
  float probeNow = module.GetWorld().GetWorldTime() * 0.001;
  for (int spent = 0; spent < steps; spent++)
  {
   // Stop as soon as this tick's shared budget is gone; the cursor is saved
   // below and the next attempt resumes exactly here.
   if (!EAC_RoutineAnchors.TakeProbe(module.GetWorld(), probeNow)) break;
   int pass = cursor / EAC_RoutineAnchors.PROBES;
   int probe = cursor % EAC_RoutineAnchors.PROBES;
   cursor++;
   if (cursor >= steps) { cursor = 0; swept = true; }
   EAC_EAnchorKind kind = wanted;
   if (pass == 1) kind = EAC_EAnchorKind.YARD_OPEN;
   // Each leg starts three bearings further round, so a continuation does not
   // re-walk the sequence that chose the previous stop (leg 0 unchanged).
   int index = (probe + m_Claim.Resident.Id + m_Leg * 3) % EAC_RoutineAnchors.PROBES;
   vector candidate[4]; string reason;
   if (!EAC_RoutineAnchors.Find(kind, module.GetWorld(), m_Actor, home, path, index, candidate, reason)) continue;
   if (!CanApproach(module, candidate[3])) continue;
   for (int axis = 0; axis < 4; axis++) transform[axis] = candidate[axis];
   m_Claim.Resident.AnchorCursor = cursor;
   return true;
  }
  m_Claim.Resident.AnchorCursor = cursor;
  // The budget stopped this call part-way through the sweep. Refusing here costs
  // the caller START_RETRY_SECONDS and the next retry resumes at `cursor`, so the
  // full sweep still completes - across ticks instead of inside one.
  if (!swept)
  {
   EAC_RoutineStats.RecordAnchorBudget(false);
   return false;
  }
  EAC_RoutineStats.RecordAnchorBudget(true);
  return LegacyAnchor(module, transform);
 }

 // Last resort, and the behaviour this system had before the anchor catalog: a
 // ring around the actor itself. A home whose geometry supports no anchor at all
 // must still allow an ordinary open-ground occupation rather than starving the
 // resident of every routine.
 protected bool LegacyAnchor(EAC_AmbientModule module, out vector transform[4])
 {
  for (int probe = 0; probe < 8; probe++)
  {
   float angle = (probe + m_Claim.Resident.Id) * Math.PI / 4;
   // The ring must clear the spacing this stop demands or CanApproach rejects
   // every candidate it produces (campaign 72: 37 routines, one second stop).
   // Leg 0 keeps the historic 12 m exactly.
   float ring = m_LegSpacing + 4;
   if (ring < 12) ring = 12;
   vector point = m_Actor.GetOrigin() + Vector(Math.Cos(angle) * ring, 0, Math.Sin(angle) * ring);
   point[1] = module.GetWorld().GetSurfaceY(point[0], point[2]) + 0.1;
   if (!EAC_PedestrianSpawner.IsClear(module.GetWorld(), point, m_Actor) || !CanApproach(module, point)) continue;
   vector face = m_Claim.Home.BuildingEntity.GetOrigin() - point; face[1] = 0;
   if (face.LengthSq() < 0.0001) face = "0 0 1";
   Math3D.DirectionAndUpMatrix(face.Normalized(), "0 1 0", transform); transform[3] = point;
   EAC_RoutineStats.RecordFallback();
   return true;
  }
  return false;
 }

 protected void StopApproach()
 {
  m_HadApproach = false;
  if (!m_Approach) return;
  if (m_Claim && m_Claim.Group) m_Claim.Group.RemoveWaypoint(m_Approach);
  SCR_EntityHelper.DeleteEntityAndChildren(m_Approach);
 }

 // Retire this stop's position everywhere it could be offered from again: the
 // point itself (CanApproach), the surveyed spot (index Failures) and, for a
 // shared sitting spot, the home that hosts it. Runs before RequestStop so the
 // scene lease still names this resident when the index is told.
 protected void RetireDestination()
 {
  if (!m_Point) return;
  EAC_MoveFailureGuard.Retire(m_Point.GetOrigin());
  if (m_SceneHeld && m_Claim && m_Claim.Resident) EAC_SceneIndex.FailHeld(m_Claim.Resident.Id);
  if (m_Station && m_Claim) EAC_ActivityStation.RetireHome(m_Claim.Home);
 }

 protected void RecordFailedAttempt()
 {
  if (m_Entered || m_FailedAttemptRecorded || !m_Claim || !EAC_ActivityProfiles.NeedsTable(m_Routine)) return;
  m_FailedAttemptRecorded = true;
  EAC_ActivityProfiles.TableAttemptFailed(m_Claim.Resident);
 }

 // Idempotent: a surveyed position is returned once, and only by the resident
 // that holds it.
 protected void ReleaseScene()
 {
  if (!m_SceneHeld || !m_Claim || !m_Claim.Home || !m_Claim.Resident) return;
  m_SceneHeld = false;
  m_FurnitureSeat = null;
  // The position may belong to a neighbour's record, so release by identity
  // rather than by assuming the resident's own home holds it.
  EAC_SceneIndex.ReleaseAllFor(m_Claim.Resident.Id);
 }

 // True: this object still owns the resident, because a next stop is open or a
 // transition is deferred by one tick. False: the routine is over and the
 // spawner may drop this object. Every guard is braced - a brace-less nested
 // return in a one-line block can fall through in Enforce.
 protected bool Continue(EAC_AmbientModule module, float now)
 {
  if (!m_LegDone) { FinishRoutine(module, now); return false; }
  if (m_SeatLeg) { FinishRoutine(module, now); return false; }
  if (!module || module != EAC_AmbientModule.GetActive() || module.ActivityLimit <= 0) { FinishRoutine(module, now); return false; }
  if (!m_Activation || m_Activation.PlayerTouched || !OwnsActor()) { FinishRoutine(module, now); return false; }
  if (m_Claim.Cache || m_Claim.AlarmUntil > now || EAC_CivilianDanger.HasCombatThreat(m_Actor)) { FinishRoutine(module, now); return false; }
  if (m_Leg + 1 >= m_Legs) { FinishRoutine(module, now); return false; }
  EAC_PedestrianSpawner spawner = module.GetSpawner();
  if (!spawner) { FinishRoutine(module, now); return false; }
  // One leg transition per shared tick across the whole town. Deferring keeps
  // ownership: m_LegDone and m_Stopping stay set, the point is already gone, and
  // the early tail re-enters here on the next 0.5 s monitor tick.
  if (!spawner.TakeLegToken(now)) return true;
  m_Leg++;
  if (BeginLeg(module, now)) return true;
  FinishRoutine(module, now);
  return false;
 }

 // One place closes a routine. A chain has already delivered two to four
 // occupations; charging the full per-occupation cooldown again is exactly what
 // produced the measured dead air between routines.
 protected void FinishRoutine(EAC_AmbientModule module, float now)
 {
  if (m_Finished) return;
  m_Finished = true;
  int closedStops = m_Stops;
  float closedTravel = m_RoutineTravel;
  float closedSeconds = now - m_RoutineStartedAt;
  EAC_RoutineStats.RecordRoutineFinished(closedStops, closedTravel, closedSeconds);
  if (!m_Claim || !m_Claim.Resident) return;
  float interval = 120;
  float gap = 30;
  if (module) { interval = module.ActivityInterval; gap = module.RoutineGap; }
  // Audit item 1. A routine that PRODUCED something - an entered occupation, a
  // built point, an acquired station - pays RoutineGap. Run 74 and campaigns
  // 106-113 both measured the other rule charging 120-180 s of standing for a
  // continuation the supply refused, which is a geometry failure billed to the
  // resident; 32-46 % of admitted routines took that path.
  //
  // ActivityInterval is now reserved for "no supply at all", and even there the
  // rest is clamped: no closing routine may hand out a rest longer than
  // max(gap, 20) seconds, because the owner rule is that no active resident near
  // a player stands with no order for more than 120 s and the Random(1, 1.5)
  // multiplier below turns a 120 s interval into 180. A start that produced
  // nothing at all does not reach here at all - BeginLeg returns false, Start
  // returns false and TryActivity charges its own 20 s retry.
  float floorRest = gap;
  if (floorRest < 20) floorRest = 20;
  float rest = interval;
  bool supplied = m_Stops >= 1 || m_Supplied;
  if (supplied) rest = gap;
  else if (rest > floorRest) rest = floorRest;
  float restSeconds = rest * Math.RandomFloat(1, 1.5);
  EAC_RoutineStats.RecordRoutineRest(supplied, restSeconds);
  m_Claim.Resident.NextRoutineAt = now + restSeconds;
  // The routine is over: the bounded sweeps start fresh next time rather than
  // resuming mid-circle, which is also what makes a resident returning from
  // cache/wake begin at a known place instead of at whatever probe the routine it
  // never finished had reached.
  m_Claim.Resident.AnchorCursor = 0;
  m_Claim.Resident.StationCursor = 0;
  // The walker was yielded so this routine could take the group; give it back.
  if (m_Activation && m_Activation.Walking) m_Activation.Walking.ClearYield();
  if (!m_Activation) return;
  if (m_Activation.NextActivity > m_Claim.Resident.NextRoutineAt) m_Activation.NextActivity = m_Claim.Resident.NextRoutineAt;
 }

 // RequestStop with a counted cause. Only the first cause of a stop is counted:
 // Monitor keeps calling RequestStop while an occupation drains, and those
 // repeats are not new aborts. The counters exist because run 93's stopless
 // routines left no line at all behind them.
 protected void AbortStop(int reason, bool fast = false)
 {
  if (!m_Stopping) EAC_RoutineStats.RecordAbort(reason);
  RequestStop(fast);
 }

 void RequestStop(bool fast = false)
 {
  if (!Replication.IsServer()) return;
  RecordFailedAttempt();
  m_Stopping = true;
  StopApproach();
  if (m_VanillaSeat) m_VanillaSeat.RequestStop(fast);
  if (!m_Actor || !m_Point) return;
  SCR_CharacterControllerComponent controller = SCR_CharacterControllerComponent.Cast(m_Actor.GetCharacterController());
  // Regrouping revokes new orders, but this exact activity still needs cancellation.
  // Native possession handles active player loiter; never change player input here.
  if (!controller || controller.IsPlayerControlled()) return;
  RplComponent replication = m_Actor.GetRplComponent();
  if (replication && !replication.IsOwner()) return;
  SCR_ScriptedCharacterInputContext input = controller.GetScrInputContext();
  if (!input || input.GetLoiterEntity() != m_Point) return;
  if (!controller.IsLoitering())
  {
   // StartLoitering can queue entry behind alignment/equipment; StopLoitering
   // only stops an existing command and does not clear that pending request.
   input.m_iLoiteringType = -1;
   input.SetLoiteringEntity(null);
  }
  else if (!m_StopIssued || (fast && !m_FastStopIssued))
  {
   controller.StopLoitering(fast); m_StopIssued = true;
   if (fast) m_FastStopIssued = true;
  }
 }

 // Returns true only after confirmed helper absence. Occupied points remain owned.
 bool Monitor(EAC_AmbientModule module, float now)
 {
  if (!Replication.IsServer()) return false;
  // Reached when the point vanished, and again after a deferred leg transition.
  // A deliberate leg boundary must not be recorded as a failed attempt.
  if (!m_Point)
  {
   if (!m_LegDone) { RecordFailedAttempt(); }
   StopApproach();
   // The point went away with a native pose still running (item 9's other half).
   // Same deadline rule as the drain tail below, so this branch cannot hold the
   // slot for ever either.
   if (m_VanillaSeat)
   {
    if (m_DrainDeadline <= 0)
    {
     m_DrainDeadline = now + DRAIN_WINDOW_SECONDS;
     float lostDrain = m_EndAt + DRAIN_WINDOW_SECONDS;
     if (lostDrain < m_DrainDeadline) m_DrainDeadline = lostDrain;
    }
    bool lostSeatClear;
    if (now >= m_DrainDeadline)
    {
     if (!m_DrainCounted) { m_DrainCounted = true; EAC_RoutineStats.RecordAbort(EAC_RoutineStats.ABORT_DRAIN); }
     lostSeatClear = m_VanillaSeat.Drain(now);
    }
    else lostSeatClear = m_VanillaSeat.Cleanup(now);
    if (!lostSeatClear) return false;
   }
   m_VanillaSeat = null;
   ReleaseScene();
   if (m_Station && !m_Station.Release(m_Claim)) return false;
   m_Station = null;
   return !Continue(module, now);
  }
  SCR_CharacterControllerComponent controller;
  if (m_Actor) controller = SCR_CharacterControllerComponent.Cast(m_Actor.GetCharacterController());
  bool loitering = controller && controller.IsLoiteringOnEntity(m_Point);
  bool danger;
  if (OwnsActor())
  {
   danger = EAC_CivilianDanger.HasCombatThreat(m_Actor);
  }
  // Routine gates use the combat threat (native threat measure above VIGILANT, or
  // bleeding), not HasThreat: that one also returns true for any queued vanilla
  // danger event - a passing car, a horn, a door - because it is the cache
  // manager's safety veto. Campaign 103 (calm phase 2, one car driving through)
  // counted 22 danger aborts and four residents idle 90 s on beginleg.threat.
  // Same conditions and the same fast flag as before, split so the danger stop
  // is counted: it was run 93's silent routine killer (gate=beginleg.threat with
  // no [EAC ALARM] line, because HasThreat is also the cache manager's blocking
  // danger, which prints nothing).
  bool halt = !OwnsActor() || !module || module.ActivityLimit == 0 || !m_Claim.Resident.Wanted || !EAC_ExclusionZone.IsPopulationAllowed(m_Point.GetOrigin());
  if (danger) { AbortStop(EAC_RoutineStats.ABORT_DANGER, true); }
  else if (halt) RequestStop(false);
  // Presence is the station record, not a prop: a retired station, or one left
  // behind by a previous mission, ends this occupation at once.
  if (m_Station && !m_Station.IsPresent()) RequestStop(true);
  if (!m_Stopping && m_HadApproach)
  {
   vector current = m_Actor.GetOrigin();
   float travelled = vector.Distance(current, m_LastApproachPosition);
   // Reject teleport jumps as routine walking evidence.
   if (travelled <= 3) m_ApproachTravel += travelled;
   m_LastApproachPosition = current;
   // The native move to this stop failed on the AI's own tick. The guard has
   // already kept vanilla off its NodeError line; this side removes the order
   // and, for a destination that has no path, strikes it off for the mission.
   vector failedAt; bool failedForGood;
   if (m_Guard && m_Approach && m_Guard.TakeFailure(m_Approach, failedAt, failedForGood))
   {
    if (failedForGood) RetireDestination();
    RequestStop();
   }
   m_ScratchOrders.Clear(); m_Claim.Group.GetWaypoints(m_ScratchOrders);
   foreach (AIWaypoint order : m_ScratchOrders)
   {
    if (order != m_Approach) { AbortStop(EAC_RoutineStats.ABORT_FOREIGN_WP); }
   }
   // Audit B1. Both the straight-line test and the per-segment sweep below exist
   // only to keep a resident out of a transit-blocking zone. With no such zone in
   // the mission - the ordinary case - the whole scan is skipped rather than run
   // against an empty list once per route point per resident per tick.
   bool sweepRoute = EAC_ExclusionZone.AnyTransitBlocked();
   if (sweepRoute && !EAC_ExclusionZone.IsTransitAllowed(m_Actor.GetOrigin(), m_Point.GetOrigin())) { AbortStop(EAC_RoutineStats.ABORT_ROUTE); }
   AICharacterMovementComponent movement = AICharacterMovementComponent.Cast(m_Actor.FindComponent(AICharacterMovementComponent));
   m_ScratchRoute.Clear(); if (movement) movement.GetCurrentPath(m_ScratchRoute);
   // The 64-segment cap is a distance rule, not an exclusion rule, so it is still
   // evaluated whether or not any zone blocks transit.
   if (!movement || m_ScratchRoute.Count() > 64) { AbortStop(EAC_RoutineStats.ABORT_DISTANT); }
   if (sweepRoute)
   {
    vector previous = m_Actor.GetOrigin();
    foreach (vector next : m_ScratchRoute)
    {
     if (m_Stopping) break;
     if (!EAC_ExclusionZone.IsTransitAllowed(previous, next)) { AbortStop(EAC_RoutineStats.ABORT_ROUTE); }
     previous = next;
    }
   }
   if (!m_Stopping && vector.Distance(m_Actor.GetOrigin(), m_Point.GetOrigin()) <= 1)
   {
    StopApproach();
    BeginOccupation();
   }
   else if (!m_Stopping && (!m_Approach || m_Claim.Group.GetCurrentWaypoint() != m_Approach)) { AbortStop(EAC_RoutineStats.ABORT_WP_CHANGED); }
  }
  if (m_FurnitureSeat && !EAC_FurnitureSeat.Present(m_FurnitureSeat)) RequestStop(true);
  if (m_VanillaSeat)
  {
   // The rung ladder advances here, so a native route that builds but never
   // poses is retired inside this occupation rather than across campaigns.
   if (!m_Stopping) m_VanillaSeat.Tick(now);
   loitering = m_VanillaSeat.IsPosed();
   // Mirror the root obligation onto this object while it is still true, so the
   // deletion guard below holds even on a tick where m_VanillaSeat has already
   // been nulled.
   if (m_VanillaSeat.HoldsRoot(now)) m_RootHoldUntil = now + EAC_VanillaSeat.ROOT_GRACE;
  }
  if (!m_Entered && loitering && (m_VanillaSeat || (m_Action && m_Action.IsOccupiedBy(m_Actor))))
  {
   m_Entered = true; m_EndAt = now + m_Dwell;
   if (m_Point && m_Point.HasPose) m_PoseReportAt = now + 6;
   m_Claim.Resident.FailedTableAttempts = 0;
   m_Claim.Resident.LastActivityPosition = m_Point.GetOrigin(); m_Claim.Resident.HasActivityPosition = true;
   m_Stops++; m_RoutineTravel += m_ApproachTravel;
   // Both reads hoisted into locals before the static call's argument list.
   int enteredLeg = m_Leg;
   float enteredTravel = m_ApproachTravel;
   EAC_RoutineStats.RecordStop(enteredLeg, enteredTravel);
   // NextRoutineAt is deliberately not stamped here any more. FinishRoutine is
   // the single authority for the between-routine cooldown, so a chain can never
   // be gated against a clock its own first stop set.
   // Choose a different supported purpose while retaining it over cache/wake.
   EAC_ActivityProfiles.Advance(m_Claim.Resident);
  }
  // A8. The approach finished, the loiter action was performed and no pose ever
  // began. Re-issue once, then abort under a counted reason: standing at one's
  // own stop until the 90-240 s entry deadline is the exact shape the owner rule
  // forbids. The native seat has its own rung ladder with its own windows and
  // extends m_EnterDeadline to cover them, so it is excluded here.
  if (!m_Stopping && !m_Entered && !m_VanillaSeat && m_PoseDeadline > 0 && now >= m_PoseDeadline)
  {
   if (!m_PoseRetried)
   {
    m_PoseRetried = true;
    m_PoseDeadline = now + POSE_WINDOW_SECONDS;
    BeginOccupation();
   }
   else
   {
    m_PoseDeadline = 0;
    AbortStop(EAC_RoutineStats.ABORT_POSE);
   }
  }
  // The only new branch. A dwell that expired normally after a genuine entry is
  // a chain candidate; every other RequestStop site leaves m_LegDone false and
  // therefore ends the routine exactly as it ends an occupation today.
  if (now >= m_EndAt)
  {
   if (m_Entered) { m_LegDone = true; }
   RequestStop();
  }
  else if (!m_Entered && now >= m_EnterDeadline) { AbortStop(EAC_RoutineStats.ABORT_DEADLINE); }
  // Six seconds into a lean: where the resident actually stands against where the
  // pose was meant to be played. A picture cannot settle a 25 cm question; this can.
  if (m_PoseReportAt > 0 && now >= m_PoseReportAt && m_Entered && !m_Stopping && m_Point && m_Point.HasPose && m_Actor)
  {
   m_PoseReportAt = 0;
   if (EAC_AmbientModule.GetDebugLevelMirror() >= 2)
   {
    float poseOffset = vector.DistanceXZ(m_Actor.GetOrigin(), m_Point.PoseTransform[3]);
    float pointOffset = vector.DistanceXZ(m_Actor.GetOrigin(), m_Point.GetOrigin());
    vector facing = m_Actor.GetWorldTransformAxis(2);
    float agreement = vector.Dot(facing, m_Point.PoseTransform[2]);
    vector shift = m_Actor.GetOrigin() - m_Point.PoseTransform[3]; shift[1] = 0;
    float shiftForward = vector.Dot(shift, m_Point.PoseTransform[2]);
    float shiftRight = vector.Dot(shift, m_Point.PoseTransform[0]);
    float facingRight = vector.Dot(facing, m_Point.PoseTransform[0]);
    int leanKind = m_Routine.Anim;
    if (m_FurnitureSeat)
     PrintFormat("[EAC furniture] resident=%1 from_pose_m=%2 from_point_m=%3 facing_dot=%4", m_Claim.Resident.Id, poseOffset, pointOffset, agreement);
    else
    {
    PrintFormat("[EAC lean] resident=%1 from_pose_m=%2 from_point_m=%3 facing_dot=%4", m_Claim.Resident.Id, poseOffset, pointOffset, agreement);
    PrintFormat("[EAC lean frame] anim=%1 shift_forward_m=%2 shift_right_m=%3 facing_right_dot=%4", leanKind, shiftForward, shiftRight, facingRight);
    // The number that matters: how far the settled resident's feet are from the wall
    // the pose was started toward.
    IEntity wallHit;
    float wallGap = EAC_RoutineAnchors.HitDistance(m_Actor.GetWorld(), m_Actor.GetOrigin() + "0 1.2 0", m_Point.PoseTransform[2], 2, m_Actor, wallHit);
    PrintFormat("[EAC lean wall] resident=%1 feet_to_wall_m=%2", m_Claim.Resident.Id, wallGap);
    }
   }
  }
  else if (m_Entered && !loitering) { RequestStop(); }
  if (!m_Stopping) return false;
  // Audit item 9. Every branch below returns false while the pose still reports,
  // so a loiter that never reports stopped holds the activity slot - and blocks
  // caching - for the rest of the mission by construction. Stamp a deadline the
  // first time this stop drains and release anyway when it passes: 60 s from the
  // drain, and never later than m_EndAt + 60, so a stop aborted long before its
  // dwell cannot buy itself a longer grace than one that ran its course.
  if (m_DrainDeadline <= 0)
  {
   m_DrainDeadline = now + DRAIN_WINDOW_SECONDS;
   float dwellDrain = m_EndAt + DRAIN_WINDOW_SECONDS;
   if (dwellDrain < m_DrainDeadline) m_DrainDeadline = dwellDrain;
  }
  bool drained = now >= m_DrainDeadline;
  if (drained && !m_DrainCounted)
  {
   m_DrainCounted = true;
   EAC_RoutineStats.RecordAbort(EAC_RoutineStats.ABORT_DRAIN);
   // One fast cancel on the way out; the point is released this tick regardless.
   RequestStop(true);
  }
  if (m_VanillaSeat)
  {
   bool seatClear;
   if (drained) seatClear = m_VanillaSeat.Drain(now);
   else seatClear = m_VanillaSeat.Cleanup(now);
   if (!seatClear) return false;
   loitering = false;
  }
  if (loitering && !drained) { RequestStop(); return false; }
  // Audit S2. Hoisted into one local and tested: the drain tail runs on a
  // character that may have lost its scripted input context (possession, a
  // pending despawn), and the two chained calls dereferenced it unguarded.
  SCR_ScriptedCharacterInputContext drainInput;
  if (controller) drainInput = controller.GetScrInputContext();
  if (!drained && drainInput && drainInput.GetLoiterEntity() == m_Point && drainInput.m_iLoiteringType >= 0) { RequestStop(); return false; }
  if (m_Action && m_Action.IsOccupied())
  {
   if (!m_Action.IsOccupiedBy(m_Actor) && !drained) return false;
   m_Action.ReleaseOccupant();
  }
  // Campaign 118's fatal NodeError. The point is the animation root for a seat
  // leg and vanilla re-reads it on the behaviour tree's own tick, which is a
  // different clock from IsLoitering and from the queued loiter type. Refuse the
  // deletion while either the live seat or this object's mirrored deadline still
  // says the root is borrowed. Bounded by ROOT_GRACE (3 s), so a stop can be
  // delayed by at most that - the drain window is sixty.
  bool rootHeld = m_VanillaSeat && m_VanillaSeat.HoldsRoot(now);
  if (!rootHeld && now < m_RootHoldUntil) rootHeld = true;
  if (rootHeld)
  {
   EAC_RoutineStats.RecordRootDeferred();
   return false;
  }
  SCR_EntityHelper.DeleteEntityAndChildren(m_Point);
  if (m_Point) return false;
  // The action lived on the point that has just gone. Leaving the pointer set
  // lets the !m_Point branch at the top of Monitor - and Continue's next stop -
  // see an action belonging to a deleted entity.
  m_Action = null;
  m_VanillaSeat = null;
  ReleaseScene();
  if (m_Station && !m_Station.Release(m_Claim)) return false;
  m_Station = null;
  // Drain completely, confirm the point is gone, release the scene lease, and
  // only then advance and open the next stop. A routine holds at most one
  // surveyed spot at a time.
  return !Continue(module, now);
 }

 bool EAC_ContainsSessionHelper(SCR_EditableEntityComponent candidate)
 {
  if (EAC_SessionLifecycle.ContainsOwned(candidate, m_Point) || EAC_SessionLifecycle.ContainsOwned(candidate, m_Approach)) return true;
  // The world furniture and the seat's borrowed root are not owned helpers.
  return m_VanillaSeat && m_VanillaSeat.EAC_ContainsSessionHelper(candidate);
 }

 // The same helpers, for EAC_SessionLifecycle.Sync.
 void EAC_KeepSessionHelpers()
 {
  EAC_SessionLifecycle.Keep(m_Point);
  EAC_SessionLifecycle.Keep(m_Approach);
  if (m_VanillaSeat) m_VanillaSeat.EAC_KeepSessionHelpers();
 }
}

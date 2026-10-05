// One retained response per owned resident; expensive probes share one mission gate.
class EAC_CivilianShelter
{
 // How long after the cover pose was physically commanded the calm stand-up may
 // still be re-armed from the retained debt. It was 300 s, which is a long time
 // to keep a resident out of STAND - and therefore out of every routine, because
 // CanPlayLoiterAnimation refuses a non-standing character (audit item 7). The
 // stand-up itself needs seconds: the debt exists to survive a Stop() teardown
 // and a couple of refused attempts, not to run for five minutes.
 static const float STAND_UP_DEBT_SECONDS = 60;
 // Campaign 127, phase 3: the threatened resident (response 3, prone-then-flee)
 // stood at STAND for the whole 120 s under fire, with
 // "waiting for native cover stance, requests=4" on every sample. RequestProne
 // caps at four requests spaced one second apart, so the ENTIRE cover attempt was
 // four requests inside four seconds, and nothing renewed the counter: Pose() is
 // the only reset, and for a fleeing response Pose() is not reached again until
 // the shelter order is issued, which is itself gated behind the cover pose.
 // Circular, so Monitor looped on "waiting" for the remaining 116 s.
 //
 // Campaign 125 drew the SAME response 3 on the same build and had cover in
 // ~4 s (first_seeking=4.02), which is why this is intermittent: the pose simply
 // has to land inside that one four-second window, and in 127 native dropped it.
 //
 // Two bounds now. The batch renews every COVER_RETRY_SECONDS, so a character
 // that was busy - still exiting a loiter pose, mid behaviour - gets further
 // chances; and the whole attempt is bounded by COVER_WINDOW_SECONDS, after which
 // the response proceeds WITHOUT the pose rather than waiting for ever. Twelve
 // seconds is three times what campaign 125 needed.
 static const float COVER_WINDOW_SECONDS = 12;
 static const float COVER_RETRY_SECONDS = 3;
 // Audit S13. The shelter probes used to share ONE 0.5 s gate across the whole
 // mission, so twenty-four sheltering residents each got a probe every twelve
 // seconds and a town-wide alarm took minutes to resolve. The gate is now a
 // per-tick budget: at most PROBE_BUDGET interior probes are spent in any one
 // scheduler tick, whoever asks for them, which is four times the old throughput
 // and still a hard ceiling on the trace cost of a single tick.
 static const int PROBE_BUDGET = 4;
 protected static BaseWorld s_World;
 protected static float s_ProbeTick;
 protected static int s_ProbeSpent;
 protected SCR_AIGroup m_Group;
 // Plain pointer into EAC_MoveFailureGuard's own registry, which holds the ref.
 protected EAC_MoveFailureGuard m_Guard;
 protected AIWaypoint m_Order;
 protected vector m_Destination;
 protected bool m_HadOrder, m_OrderCompleted, m_InitialCoverObserved;
 protected float m_CoverSince, m_NextPoseCommand;
 protected int m_PoseCommands;
 protected ref SCR_AISettingBase m_OriginalStance, m_OriginalSpeed, m_OriginalGroupSpeed;
 protected ref SCR_AISettingBase m_Stance, m_Speed, m_GroupSpeed;
 protected float m_Started, m_Deadline, m_NextAttempt;
 protected int m_Probe;
 protected bool m_Responding, m_Hiding, m_Yielded;
 // Verified arrival time. Deliberately NOT cleared by Stop(): a teardown that
 // rebuilds the response object's settings must not erase the measured fact that
 // this resident is inside its house. Only the alarm lapsing, or a re-probe that
 // shows the actor outside the volume, clears it.
 protected float m_ShelteredAt;
 // The stance setting to return to, the actor that owes it and when the cover pose
 // was physically commanded. Retained across Stop() for the same reason as
 // m_ShelteredAt: a teardown must not lose the fact that this resident was commanded
 // out of standing and still owes a stand-up. Cleared only once it is upright again.
 protected ref SCR_AISettingBase m_CoverOriginal;
 protected IEntity m_CoverActor;
 protected float m_CoverCommandedAt;
 protected ECharacterStance m_CoverStance = ECharacterStance.PRONE;
 protected bool m_SeekShelter = true;
 protected string m_LastProbe;
 protected IEntity m_CalmActor;
 protected SCR_AIGroup m_CalmGroup;
 protected ref SCR_AISettingBase m_CalmStance;
 protected float m_CalmDeadline, m_CalmNext;
 protected int m_CalmRequests;
 // Audit S3. One scratch array each, cleared before every fill, rather than two
 // fresh allocations per sheltering resident per 2 Hz monitor tick.
 protected ref array<AIWaypoint> m_ScratchOrders = {};
 protected ref array<vector> m_ScratchRoute = {};
 string GetProbeDetails() { return m_LastProbe; }

 // Audit S4. m_LastProbe is a diagnostic read only by the GM snapshot and the QA
 // fixtures, both of which print at DebugLevel >= 2, but every assignment below
 // built its string unconditionally - on the success path too, on every tick of
 // every sheltering resident. Bare literals stay unconditional (they cost
 // nothing); anything that concatenates, formats or calls ToString is guarded by
 // this. The level comes from the static mirror: this class holds no module.
 protected static bool Verbose()
 {
  return EAC_AmbientModule.GetDebugLevelMirror() >= 2;
 }

 // Audit S13. The shared world-reset and the per-tick probe budget in one place,
 // replacing four copies of the same block. True means this caller may spend one
 // interior probe on this tick. `now` is the scheduler's shared tick clock, so
 // every resident visited in one tick draws from the same window.
 protected static bool ProbeGate(IEntity actor, float now)
 {
  if (s_World != actor.GetWorld()) { s_World = actor.GetWorld(); s_ProbeTick = now; s_ProbeSpent = 0; }
  if (now > s_ProbeTick) { s_ProbeTick = now; s_ProbeSpent = 0; }
  if (s_ProbeSpent >= PROBE_BUDGET) { EAC_RoutineStats.RecordShelterProbe(false); return false; }
  s_ProbeSpent++;
  EAC_RoutineStats.RecordShelterProbe(true);
  return true;
 }

 string GetState()
 {
  if (m_CalmStance) return "returning upright";
  // Reported before the "calm" fallthrough below. Compat set ii: once the 5 s
  // calm deadline lapsed with the agent deactivated at far LOD, ClearCalm nulled
  // m_CalmStance and this fell through to "calm" while the body was still prone
  // and a stand-up debt was outstanding - an honest-state bug that made
  // calm_prone read 0 on every sample.
  if (m_CoverOriginal && !m_Responding && !m_Hiding && m_ShelteredAt == 0 && !m_HadOrder) return "standing up";
  if (m_Hiding || m_ShelteredAt != 0) return "sheltered";
  if (m_HadOrder) return "seeking shelter";
  if (m_Responding)
  {
   if (m_CoverStance == ECharacterStance.CROUCH) return "ducking";
   return "taking cover";
  }
  return "calm";
 }

 // Not everyone reacts the same way. The choice is retained per resident, so a
 // given person is consistent while the crowd is varied.
 // 1 drop flat and hold, 2 duck and hold, 3 drop flat then run for shelter,
 // 4 duck then run for shelter. There is no native surrender state in this
 // build, so no surrender response is offered; see docs/ROUTINES.md.
 protected void ChooseResponse(EAC_ResidentClaim claim)
 {
  m_CoverStance = ECharacterStance.PRONE; m_SeekShelter = true;
  if (!claim || !claim.Resident) return;
  if (claim.Resident.DangerResponse < 1 || claim.Resident.DangerResponse > 4) claim.Resident.DangerResponse = Math.RandomInt(1, 5);
  int response = claim.Resident.DangerResponse;
  if (response == 2 || response == 4) m_CoverStance = ECharacterStance.CROUCH;
  m_SeekShelter = response >= 3;
 }

 protected void ClearOrder()
 {
  if (m_Order)
  {
   if (m_Group) m_Group.RemoveWaypoint(m_Order);
   SCR_EntityHelper.DeleteEntityAndChildren(m_Order);
  }
  m_Order = null;
  m_HadOrder = false; m_OrderCompleted = false;
 }

 protected void OnWaypointCompleted(AIWaypoint waypoint)
 {
  if (waypoint && waypoint == m_Order) m_OrderCompleted = true;
 }

 protected void Restore(SCR_AIGroupSettingsComponent settings, typename kind, SCR_AISettingBase ours, SCR_AISettingBase original)
 {
  // Remove our exact object even when a higher-priority setting hides it.
  // Replaced settings remain authoritative; never reinsert an old same-priority
  // value on top of a GM change.
  if (!ours) return;
  SCR_AISettingBase current = settings.GetCurrentSetting(kind);
  bool restore = current == ours || !current || (original && current.GetPriority() > original.GetPriority());
  settings.RemoveSetting(ours);
  if (restore && original) settings.AddSetting(original, false, false);
 }

 // A caller that tears the response down while the resident is actually inside
 // its house must say why: silently losing the sheltered state is the defect the
 // probe details are read for. Callers that never shelter keep the default.
 void Stop(string reason = "")
 {
  if (m_Hiding && reason != "" && Verbose()) m_LastProbe = "stopped while sheltered: " + reason;
  ClearCalm();
  if (m_Group) m_Group.GetOnWaypointCompleted().Remove(OnWaypointCompleted);
  ClearOrder();
  if (m_Group)
  {
   SCR_AIGroupSettingsComponent settings = SCR_AIGroupSettingsComponent.Cast(m_Group.FindComponent(SCR_AIGroupSettingsComponent));
   if (settings)
   {
    Restore(settings, SCR_AICharacterStanceSettingBase, m_Stance, m_OriginalStance);
    Restore(settings, SCR_AICharacterMovementSpeedSettingBase, m_Speed, m_OriginalSpeed);
    Restore(settings, SCR_AIGroupCharactersMovementSpeedSettingBase, m_GroupSpeed, m_OriginalGroupSpeed);
   }
  }
  m_Group = null; m_Guard = null; m_Stance = null; m_Speed = null; m_GroupSpeed = null;
  m_OriginalStance = null; m_OriginalSpeed = null; m_OriginalGroupSpeed = null;
  m_Responding = false; m_Hiding = false; m_Probe = 0;
  m_InitialCoverObserved = false; m_CoverSince = 0; m_PoseCommands = 0; m_NextPoseCommand = 0;
 }

 protected void ClearCalm()
 {
  m_CalmActor = null; m_CalmGroup = null; m_CalmStance = null;
  m_CalmDeadline = 0; m_CalmNext = 0; m_CalmRequests = 0;
 }

 static bool OwnsCalmStanding(SCR_AISettingBase original, SCR_AISettingBase groupSetting, SCR_AISettingBase characterSetting)
 {
  SCR_AICharacterStanceSettingBase stance = SCR_AICharacterStanceSettingBase.Cast(characterSetting);
  return original && groupSetting == original && stance && stance.GetParentSetting() == original && stance.GetStance(ECharacterStance.PRONE) == ECharacterStance.STAND;
 }

 protected void FinishCalm(EAC_AmbientModule module, EAC_PedestrianActivation activation, CharacterControllerComponent controller, float now)
 {
  EAC_ResidentClaim claim = activation.Claim;
  // The debt expires. Nothing cleared these three fields when the window lapsed -
  // the only clear sits below the "if (!m_CalmStance) return;" line and so runs
  // only while a calm attempt is live - so a resident that never made it upright
  // carried m_CoverOriginal for the rest of the session and GetState could not
  // tell "owes a stand-up" from "calm".
  // The stand-up falls due when the alarm lapses, not when cover was commanded: a
  // resident that went prone early in a two-minute firefight had spent its whole
  // window before the shooting stopped, so the debt was dropped on the first calm
  // tick and the shelter reported "calm" while the body was still prone (campaign
  // 0918-133 phase 4: agent deactivated at max LOD, prone, state calm from 8 s after
  // the lapse). Count the window from whichever came later.
  float debtSince = Math.Max(m_CoverCommandedAt, claim.AlarmUntil);
  if (m_CoverOriginal && now - debtSince >= STAND_UP_DEBT_SECONDS)
  { m_CoverOriginal = null; m_CoverActor = null; m_CoverCommandedAt = 0; }
  // A single armed attempt used to be abandoned by any ClearCalm branch below (the
  // 5 s deadline lapsing while the agent sat deactivated at max LOD, or a gate
  // refusing once) and, m_Responding being false after Stop(), nothing ever asked
  // again: the resident walked its whole routine prone and no loiter animation
  // could start (Danger fixture phase 54, 2026-09-17). Re-arm from the retained
  // debt until the character is actually upright, for at most
  // STAND_UP_DEBT_SECONDS.
  bool owed = m_CoverOriginal && !m_CalmStance && controller && m_CoverActor == claim.Character;
  owed = owed && controller.GetStance() != ECharacterStance.STAND && now - debtSince < STAND_UP_DEBT_SECONDS;
  if (m_Responding || owed)
  {
   SCR_AISettingBase original = m_OriginalStance;
   if (!original) original = m_CoverOriginal;
   SCR_AIGroup group = m_Group;
   if (!group) group = claim.Group;
   IEntity originalActor = claim.Character;
   if (m_Responding) Stop();
   if (!original || !group || !originalActor || originalActor != claim.Character || group != claim.Group) return;
   m_CalmStance = original; m_CalmGroup = group; m_CalmActor = claim.Character;
   m_CalmDeadline = now + 5;
  }
  if (!m_CalmStance) return;
  if (controller && controller.GetStance() == ECharacterStance.STAND) { m_CoverOriginal = null; m_CoverActor = null; m_CoverCommandedAt = 0; }
  if (!m_CalmActor || !m_CalmGroup || !controller || claim.Character != m_CalmActor || claim.Group != m_CalmGroup || now >= m_CalmDeadline || controller.GetStance() == ECharacterStance.STAND)
  { ClearCalm(); return; }
  SCR_AIGroupSettingsComponent groupSettings = SCR_AIGroupSettingsComponent.Cast(m_CalmGroup.FindComponent(SCR_AIGroupSettingsComponent));
  SCR_AICharacterSettingsComponent characterSettings = SCR_AICharacterSettingsComponent.FindOnControlledEntity(m_CalmActor);
  SCR_AIGroupInfoComponent info = SCR_AIGroupInfoComponent.Cast(m_CalmGroup.FindComponent(SCR_AIGroupInfoComponent));
  if (!groupSettings || !characterSettings || !OwnsCalmStanding(m_CalmStance, groupSettings.GetCurrentSetting(SCR_AICharacterStanceSettingBase), characterSettings.GetCurrentSetting(SCR_AICharacterStanceSettingBase)) || (info && info.GetAllowedStance(ECharacterStance.STAND) != ECharacterStance.STAND))
  { ClearCalm(); return; }
  // The resumed routine's own approach order used to veto the stand-up here; the
  // group stance setting is already STAND again after Stop(), so an upright walk
  // is exactly the intended state and the order is no reason to stay prone.
  if (now < m_CalmNext || m_CalmRequests >= 4) return;
  // Stop restores settings through native callbacks. Revalidate the claim and
  // current character immediately before issuing any physical stance request.
  if (!module || module != EAC_AmbientModule.GetActive() || activation.PlayerTouched || claim.Cache || !claim.Committed || claim.AlarmUntil > now || module.GetResidentActivation(claim.Home, claim.Resident) != claim || !EAC_PedestrianSpawner.HasCivilianControl(m_CalmActor, m_CalmGroup) || !claim.OptimizerMember || claim.OptimizerMember.WasPlayer)
  { ClearCalm(); return; }
  RplComponent actorRpl = RplComponent.Cast(m_CalmActor.FindComponent(RplComponent));
  RplComponent groupRpl = RplComponent.Cast(m_CalmGroup.FindComponent(RplComponent));
  if (!actorRpl || actorRpl.IsProxy() || !actorRpl.IsOwner() || !groupRpl || groupRpl.IsProxy() || !groupRpl.IsOwner() || m_CalmGroup.GetPlayerCount() != 0)
  { ClearCalm(); return; }
  controller = CharacterControllerComponent.Cast(m_CalmActor.FindComponent(CharacterControllerComponent));
  SCR_CharacterControllerComponent loiter = SCR_CharacterControllerComponent.Cast(controller);
  CompartmentAccessComponent access = CompartmentAccessComponent.Cast(m_CalmActor.FindComponent(CompartmentAccessComponent));
  AIControlComponent control = AIControlComponent.Cast(m_CalmActor.FindComponent(AIControlComponent));
  if (!controller || controller.IsUnconscious() || controller.IsPlayerControlled() || SCR_PossessingManagerComponent.GetPlayerIdFromControlledEntity(m_CalmActor) != 0 || !control || !control.GetAIAgent() || control.GetAIAgent().GetPermanentLOD() >= 0 || (loiter && (loiter.IsLoitering() || (loiter.GetScrInputContext() && loiter.GetScrInputContext().m_iLoiteringType >= 0))) || (access && (access.IsInCompartment() || access.IsGettingIn() || access.IsGettingOut())))
  { ClearCalm(); return; }
  m_CalmNext = now + 1; m_CalmRequests++;
  // Settings constrain future native nodes; idle AI needs an explicit request.
  SCR_AIStanceHandling.SetStance(controller, ECharacterStance.STAND);
 }

 protected bool OwnsPose(SCR_AIGroupSettingsComponent settings)
 {
  return settings && settings.GetCurrentSetting(SCR_AICharacterStanceSettingBase) == m_Stance && settings.GetCurrentSetting(SCR_AICharacterMovementSpeedSettingBase) == m_Speed && settings.GetCurrentSetting(SCR_AIGroupCharactersMovementSpeedSettingBase) == m_GroupSpeed;
 }

 protected void YieldResponse()
 {
  Stop(); m_Yielded = true;
 }

 protected bool Pose(bool moving)
 {
  if (!m_Group) return false;
  SCR_AIGroupSettingsComponent settings = SCR_AIGroupSettingsComponent.Cast(m_Group.FindComponent(SCR_AIGroupSettingsComponent));
  if (!settings || (m_Stance && !OwnsPose(settings))) { YieldResponse(); return false; }
  // Remove only captured/owned settings, never all entries of a category/origin.
  if (m_Stance) settings.RemoveSetting(m_Stance);
  else if (m_OriginalStance) settings.RemoveSetting(m_OriginalStance);
  if (m_Speed) settings.RemoveSetting(m_Speed);
  else if (m_OriginalSpeed) settings.RemoveSetting(m_OriginalSpeed);
  if (m_GroupSpeed) settings.RemoveSetting(m_GroupSpeed);
  else if (m_OriginalGroupSpeed) settings.RemoveSetting(m_OriginalGroupSpeed);
  ECharacterStance stance = m_CoverStance;
  EMovementType speed = EMovementType.WALK;
  if (moving) { stance = ECharacterStance.STAND; speed = EMovementType.RUN; }
  // Threat-driven native behaviors use DANGER_LOW/COMBAT, above GROUP_GOAL.
  // Apply our temporary response there too, then restore the captured settings.
  m_Stance = SCR_AICharacterStanceSetting.Create(SCR_EAISettingOrigin.SCENARIO, SCR_EAIBehaviorCause.ALWAYS, stance);
  m_Speed = SCR_AICharacterMovementSpeedSetting.Create(SCR_EAISettingOrigin.SCENARIO, SCR_EAIBehaviorCause.ALWAYS, speed);
  m_GroupSpeed = SCR_AIGroupCharactersMovementSpeedSetting.Create(SCR_EAISettingOrigin.SCENARIO, speed);
  bool stanceAdded = settings.AddSetting(m_Stance, false, false);
  bool speedAdded = settings.AddSetting(m_Speed, false, false);
  bool groupSpeedAdded = settings.AddSetting(m_GroupSpeed, false, false);
  if (!stanceAdded || !speedAdded || !groupSpeedAdded || !OwnsPose(settings)) { YieldResponse(); return false; }
  m_PoseCommands = 0; m_NextPoseCommand = 0;
  return true;
 }

 // Use the same native stance request as SCR_AISetStance, only while the
 // character's effective setting is the exact child of our group setting.
 protected void RequestProne(IEntity actor, CharacterControllerComponent controller, float now)
 {
  if (controller.GetStance() == m_CoverStance || now < m_NextPoseCommand) return;
  // Renew the batch instead of dead-ending on it; the overall attempt is bounded
  // by COVER_WINDOW_SECONDS in Monitor, not by this counter.
  if (m_PoseCommands >= 4)
  {
   if (now < m_NextPoseCommand + COVER_RETRY_SECONDS) return;
   m_PoseCommands = 0;
   EAC_RoutineStats.RecordCoverRetry();
  }
  SCR_AICharacterSettingsComponent settings = SCR_AICharacterSettingsComponent.FindOnControlledEntity(actor);
  SCR_AISettingBase stance;
  if (settings) stance = settings.GetCurrentSetting(SCR_AICharacterStanceSettingBase);
  SCR_AIGroupInfoComponent info = SCR_AIGroupInfoComponent.Cast(m_Group.FindComponent(SCR_AIGroupInfoComponent));
  if (!stance || stance.GetParentSetting() != m_Stance || (info && info.GetAllowedStance(m_CoverStance) != m_CoverStance))
  { m_LastProbe = "cover stance yielded to character/group setting"; YieldResponse(); return; }
  m_NextPoseCommand = now + 1; m_PoseCommands++;
  if (m_OriginalStance) m_CoverOriginal = m_OriginalStance;
  m_CoverActor = actor; m_CoverCommandedAt = now;
  SCR_AIStanceHandling.SetStance(controller, m_CoverStance);
 }

 protected bool BeginResponse(SCR_AIGroup group, float now)
 {
  if (!group) return false;
  array<AIWaypoint> orders = {}; group.GetWaypoints(orders);
  if (!orders.IsEmpty()) return false;
  SCR_AIGroupSettingsComponent settings = SCR_AIGroupSettingsComponent.Cast(group.FindComponent(SCR_AIGroupSettingsComponent));
  if (!settings) return false;
  m_Group = group;
  m_OriginalStance = settings.GetCurrentSetting(SCR_AICharacterStanceSettingBase);
  m_OriginalSpeed = settings.GetCurrentSetting(SCR_AICharacterMovementSpeedSettingBase);
  m_OriginalGroupSpeed = settings.GetCurrentSetting(SCR_AIGroupCharactersMovementSpeedSettingBase);
  if (!Replaceable(m_OriginalStance) || !Replaceable(m_OriginalSpeed) || !Replaceable(m_OriginalGroupSpeed)) { m_Group = null; return false; }
  m_Group.GetOnWaypointCompleted().Insert(OnWaypointCompleted);
  m_Responding = true; m_Started = now; m_NextAttempt = now + 2;
  return Pose(false);
 }

 protected bool Replaceable(SCR_AISettingBase setting)
 {
  return !setting || setting.GetOrigin() == SCR_EAISettingOrigin.SCENARIO || setting.GetOrigin() == SCR_EAISettingOrigin.DEFAULT;
 }

 static bool RouteAllowed(vector from, vector destination, array<vector> route)
 {
  if (!route || route.Count() > 64) return false;
  if (!EAC_ExclusionZone.IsPopulationAllowed(destination) || !EAC_ExclusionZone.IsTransitAllowed(from, destination)) return false;
  // Audit B1. The point-by-point sweep exists only for transit-blocking zones.
  // With none in the mission it is skipped whole rather than walked for up to 64
  // points per sheltering or emerging resident per tick; EAC_RoutineEmerge reaches
  // this same function, so both callers get the skip from here.
  if (!EAC_ExclusionZone.AnyTransitBlocked()) return true;
  vector previous = from;
  foreach (vector point : route)
  {
   if (!EAC_ExclusionZone.IsTransitAllowed(previous, point)) return false;
   previous = point;
  }
  return true;
 }

 // Footprint candidates are accepted only with native navmesh, clear body volume
 // and a roof belonging to this house. The native waypoint determines reachability.
 static bool InteriorPoint(IEntity house, IEntity actor, AIPathfindingComponent path, int index, out vector destination, out string reason)
 {
  reason = "missing house, actor or path";
  if (!house || !actor || !path || index < 0 || index >= 9) return false;
  vector mins, maxs, transform[4]; house.GetBounds(mins, maxs); house.GetWorldTransform(transform);
  vector local = (mins + maxs) * 0.5;
  int column = index % 3;
  local[0] = mins[0] + (maxs[0] - mins[0]) * (0.25 + column * 0.25);
  local[2] = mins[2] + (maxs[2] - mins[2]) * (0.25 + Math.Floor(index / 3.0) * 0.25);
  local[1] = mins[1] + 1;
  vector candidate = local.Multiply4(transform);
  // Model bounds may include foundations well below the usable ground floor.
  // Search near the actual terrain here; navmesh and the house roof still prove
  // a usable interior before any order is issued.
  candidate[1] = actor.GetWorld().GetSurfaceY(candidate[0], candidate[2]) + 0.2;
  reason = "no navmesh";
  NavmeshWorldComponent mesh = path.GetNavmeshComponent();
  if (!mesh) return false;
  // Audit S4. Three ToString calls ran on every probe of every sheltering
  // resident, including the ones that go on to succeed. The bare reason is always
  // set; the position is appended only when a reader exists.
  bool verbose = Verbose();
  reason = "tile pending";
  if (verbose) reason = "tile pending at " + candidate.ToString();
  if (!mesh.IsTileLoaded(candidate)) { if (!mesh.IsTileRequested(candidate)) mesh.LoadTileIn(candidate); return false; }
  reason = "invalid interior tile";
  if (verbose) reason = "invalid interior tile at " + candidate.ToString();
  if (!mesh.IsTileValid(candidate)) return false;
  reason = "no interior navmesh";
  if (verbose) reason = "no interior navmesh at " + candidate.ToString();
  if (!path.GetClosestPositionOnNavmesh(candidate, "1 3 1", destination)) return false;
  reason = "projected tile unavailable";
  if (!mesh.IsTileLoaded(destination) || !mesh.IsTileValid(destination)) return false;
  reason = "distance or exclusion";
  if (vector.Distance(actor.GetOrigin(), destination) > 80 || !EAC_ExclusionZone.IsPopulationAllowed(destination) || !EAC_ExclusionZone.IsTransitAllowed(actor.GetOrigin(), destination)) return false;
  return InteriorVolume(house, actor, destination, reason);
 }

 static bool NearDestination(vector position, vector destination)
 {
  // Native completion has been measured 3.22 m from the probed point while the
  // actor was already inside the assigned house (campaign run 80), so the XZ
  // tolerance is wider than the waypoint's own completion radius. The interior
  // volume probe, not this distance, is what actually decides arrival.
  return vector.DistanceXZ(position, destination) <= 3.5 && Math.AbsFloat(position[1] - destination[1]) <= 1;
 }

 // Recheck the actor's actual interior at arrival, not only the target selected
 // earlier. Native completion/deletion alone is never proof of shelter.
 static bool InteriorVolume(IEntity house, IEntity actor, vector destination, out string reason)
 {
  if (!house || !actor) { reason = "house or actor removed"; return false; }
  vector mins, maxs; house.GetBounds(mins, maxs);
  vector projectedLocal = house.CoordToLocal(destination);
  reason = "projection outside footprint";
  if (projectedLocal[0] <= mins[0] || projectedLocal[0] >= maxs[0] || projectedLocal[2] <= mins[2] || projectedLocal[2] >= maxs[2]) return false;
  BaseWorld world = actor.GetWorld();
  TraceBox body = new TraceBox(); body.Start = destination; body.End = destination;
  body.Mins = "-0.35 0.15 -0.35"; body.Maxs = "0.35 1.7 0.35";
  body.Flags = TraceFlags.WORLD | TraceFlags.ENTS; body.Exclude = actor;
  bool verbose = Verbose();
  reason = "interior body clearance";
  if (verbose) reason = "interior body clearance at " + destination.ToString();
  if (world.TracePosition(body, null) < 0)
  {
   if (verbose) reason += string.Format(" blocker=%1", body.TraceEnt);
   return false;
  }
  TraceParam roof = new TraceParam(); roof.Start = destination + "0 1.7 0"; roof.End = destination + "0 12 0";
  roof.Flags = TraceFlags.WORLD | TraceFlags.ENTS; roof.Exclude = actor;
  reason = "no assigned house roof";
  if (verbose) reason = "no assigned house roof at " + destination.ToString();
  bool roofHit = world.TraceMove(roof, null) < 1;
  if (!roofHit || roof.TraceEnt != house)
  {
   if (verbose) reason += string.Format(" blocker=%1", roof.TraceEnt);
   return false;
  }
  return true;
 }

 void Monitor(EAC_AmbientModule module, EAC_PedestrianActivation activation, float now)
 {
  if (!Replication.IsServer()) return;
  if (!module || module != EAC_AmbientModule.GetActive() || !activation) { Stop("module guard"); return; }
  EAC_ResidentClaim claim = activation.Claim;
  if (!claim || !claim.Character || claim.Cache || activation.PlayerTouched || !claim.Committed || module.GetResidentActivation(claim.Home, claim.Resident) != claim || !EAC_PedestrianSpawner.HasCivilianControl(claim.Character, claim.Group)) { Stop("claim guard"); return; }
  IEntity actor = claim.Character;
  CharacterControllerComponent controller = CharacterControllerComponent.Cast(actor.FindComponent(CharacterControllerComponent));
  if (!controller || controller.IsPlayerControlled() || SCR_PossessingManagerComponent.GetPlayerIdFromControlledEntity(actor) != 0 || !claim.OptimizerMember || claim.OptimizerMember.WasPlayer || CompartmentAccessComponent.GetVehicleIn(actor)) { Stop("actor guard"); return; }
  // Retained furniture cleanup is not an actor command. Wait only for native
  // occupation/entry/exit, then allow danger response while props stay protected.
  SCR_CharacterControllerComponent loiterController = SCR_CharacterControllerComponent.Cast(controller);
  CompartmentAccessComponent access = CompartmentAccessComponent.Cast(actor.FindComponent(CompartmentAccessComponent));
  if (claim.AlarmUntil <= now)
  {
   // The alarm is over: the resident is no longer sheltering from anything, so
   // the verified-interior latch ends with it.
   m_ShelteredAt = 0;
   bool occupied = (loiterController && (loiterController.IsLoitering() || (loiterController.GetScrInputContext() && loiterController.GetScrInputContext().m_iLoiteringType >= 0))) || (access && (access.IsInCompartment() || access.IsGettingIn() || access.IsGettingOut()));
   if (occupied) Stop();
   else FinishCalm(module, activation, controller, now);
   m_Yielded = false; return;
  }
  ClearCalm();
  if (m_Yielded) return;
  if (activation.Activity && ((loiterController && (loiterController.IsLoitering() || (loiterController.GetScrInputContext() && loiterController.GetScrInputContext().m_iLoiteringType >= 0))) || (access && (access.IsInCompartment() || access.IsGettingIn() || access.IsGettingOut())))) return;
  if (!m_Responding)
  {
   ChooseResponse(claim);
   if (!BeginResponse(claim.Group, now)) return;
  }
  if (claim.Group != m_Group) { Stop(); return; }
  SCR_AIGroupSettingsComponent currentSettings = SCR_AIGroupSettingsComponent.Cast(m_Group.FindComponent(SCR_AIGroupSettingsComponent));
  if (!OwnsPose(currentSettings)) { YieldResponse(); return; }
  m_ScratchOrders.Clear(); m_Group.GetWaypoints(m_ScratchOrders);
  if (m_ScratchOrders.Count() > 1 || (m_ScratchOrders.Count() == 1 && m_ScratchOrders[0] != m_Order)) { m_LastProbe = "foreign waypoint"; YieldResponse(); return; }
  if (!m_InitialCoverObserved)
  {
   RequestProne(actor, controller, now);
   if (!m_Responding) return;
   if (controller.GetStance() != m_CoverStance)
   {
    m_CoverSince = 0;
    // A pose that never applies must not cost the whole alarm (campaign 127).
    // After the window the response proceeds without it: a fleeing response goes
    // on to the shelter search, which is the more valuable half of the behaviour
    // anyway - running indoors beats lying down - and a hold-in-place response
    // stops claiming to be taking cover. RequestProne is still called from both
    // paths below, so a late pose is still taken, and Pose() on the shelter order
    // renews the batch.
    if (m_Started > 0 && now - m_Started >= COVER_WINDOW_SECONDS)
    {
     m_InitialCoverObserved = true;
     m_LastProbe = "cover pose never applied, proceeding";
     EAC_RoutineStats.RecordCoverTimeout();
    }
    else
    {
     m_LastProbe = "waiting for native cover stance";
     if (Verbose()) m_LastProbe = "waiting for native cover stance, requests=" + m_PoseCommands.ToString();
     return;
    }
   }
   else
   {
    if (m_CoverSince == 0) m_CoverSince = now;
    // Confirm native cover before travel; elapsed setup time is not pose proof.
    if (now - m_CoverSince < 1) return;
    m_InitialCoverObserved = true; m_LastProbe = "native cover stance observed";
   }
  }
  if (m_HadOrder)
  {
   // A shelter walk the native pathing could not complete. The guard already kept
   // vanilla off its NodeError line; drop the order here and seek again, exactly
   // as for an order that ended without a verified arrival.
   vector failedAt; bool failedForGood;
   if (m_Guard && m_Order && m_Guard.TakeFailure(m_Order, failedAt, failedForGood))
   {
    m_LastProbe = "shelter move failed";
    if (Verbose()) m_LastProbe = "shelter move failed at " + failedAt.ToString() + " permanent=" + failedForGood.ToString();
    ClearOrder(); if (!Pose(false)) return; RequestProne(actor, controller, now); m_NextAttempt = now + 5; return;
   }
   AICharacterMovementComponent movement = AICharacterMovementComponent.Cast(actor.FindComponent(AICharacterMovementComponent));
   m_ScratchRoute.Clear(); if (movement) movement.GetCurrentPath(m_ScratchRoute);
   bool allowed = movement && RouteAllowed(actor.GetOrigin(), m_Destination, m_ScratchRoute);
   // Reject an invalid/excluded route before arrival can mark it sheltered.
   if (!allowed || now >= m_Deadline)
   {
    m_LastProbe = "shelter route invalid"; if (allowed) m_LastProbe = "shelter travel deadline";
    ClearOrder(); if (!Pose(false)) return; RequestProne(actor, controller, now); m_NextAttempt = now + 5; return;
   }
   string arrivalFailure = "outside arrival distance";
   if (NearDestination(actor.GetOrigin(), m_Destination))
   {
    if (!ProbeGate(actor, now)) return;
    string arrivalReason;
    if (InteriorVolume(claim.Home.BuildingEntity, actor, actor.GetOrigin(), arrivalReason))
    {
     m_LastProbe = "arrival verified";
     if (Verbose()) m_LastProbe = "arrival verified at " + actor.GetOrigin().ToString() + " native_completed=" + m_OrderCompleted.ToString();
     m_ShelteredAt = now;
     ClearOrder(); if (!Pose(false)) return; m_Hiding = true; RequestProne(actor, controller, now); return;
    }
    arrivalFailure = arrivalReason;
    m_LastProbe = "arrival rejected";
    if (Verbose()) m_LastProbe = "arrival rejected: " + arrivalReason;
   }
   if (!m_Order || m_OrderCompleted || m_ScratchOrders.IsEmpty())
   {
    // The native move can report completion short of the probed point while the
    // actor already stands inside the assigned house. Test the actual volume
    // where it stopped before throwing the order away and walking out again.
    if (m_OrderCompleted)
    {
     if (ProbeGate(actor, now))
     {
      string endedReason;
      if (InteriorVolume(claim.Home.BuildingEntity, actor, actor.GetOrigin(), endedReason))
      {
       m_LastProbe = "arrival verified off-target";
       if (Verbose()) m_LastProbe = "arrival verified off-target at " + actor.GetOrigin().ToString();
       m_ShelteredAt = now;
       ClearOrder(); if (!Pose(false)) return; m_Hiding = true; RequestProne(actor, controller, now); return;
      }
      arrivalFailure = endedReason;
     }
    }
    m_LastProbe = "native order ended before verified arrival";
    if (Verbose())
    {
     // One '+=' per term: this is the longest probe string in the file and a
     // single '+' chain of six terms fails Enforce with "Formula too complex".
     string ended = "native order ended before verified arrival, completed=";
     ended += m_OrderCompleted.ToString();
     ended += " target=" + m_Destination.ToString();
     ended += " actor=" + actor.GetOrigin().ToString();
     ended += " reason=" + arrivalFailure;
     m_LastProbe = ended;
    }
    ClearOrder(); if (!Pose(false)) return; RequestProne(actor, controller, now); m_NextAttempt = now + 5; return;
   }
   if (m_Group.GetCurrentWaypoint() != m_Order) { m_LastProbe = "current waypoint replaced"; YieldResponse(); }
   return;
  }
  // Hold-in-place reactions stay where they are; only the fleeing responses
  // search for a reachable interior.
  if (!m_SeekShelter) { m_LastProbe = "holding cover in place"; RequestProne(actor, controller, now); return; }
  // Already verified inside. Re-probe the actual volume on the shared gate, so a
  // resident that wandered or was pushed out resumes seeking, and one that is
  // still indoors keeps the sheltered state through any settings teardown.
  if (m_ShelteredAt != 0)
  {
   if (ProbeGate(actor, now))
   {
    string stayReason;
    if (!InteriorVolume(claim.Home.BuildingEntity, actor, actor.GetOrigin(), stayReason))
    {
     m_LastProbe = "left shelter";
     if (Verbose()) m_LastProbe = "left shelter: " + stayReason;
     m_ShelteredAt = 0; m_Hiding = false;
    }
   }
   if (m_ShelteredAt != 0) { m_Hiding = true; RequestProne(actor, controller, now); return; }
  }
  if (m_Hiding) { RequestProne(actor, controller, now); return; }
  if (now < m_NextAttempt || !claim.Home.BuildingEntity) return;
  if (!ProbeGate(actor, now)) return;
  AIPathfindingComponent path = AIPathfindingComponent.Cast(m_Group.FindComponent(AIPathfindingComponent));
  vector destination;
  int probe = m_Probe++;
  if (m_Probe >= 9) { m_Probe = 0; m_NextAttempt = now + 10; }
  string reason;
  if (!InteriorPoint(claim.Home.BuildingEntity, actor, path, probe, destination, reason))
  {
   m_LastProbe = "probe refused";
   if (Verbose()) m_LastProbe = "probe=" + probe.ToString() + " " + reason;
   return;
  }
  m_LastProbe = "accepted";
  if (Verbose()) m_LastProbe = "accepted " + destination.ToString();
  m_ScratchOrders.Clear(); m_Group.GetWaypoints(m_ScratchOrders);
  if (!m_ScratchOrders.IsEmpty()) { Stop(); return; }
  EntitySpawnParams params = new EntitySpawnParams(); params.TransformMode = ETransformMode.WORLD;
  Math3D.MatrixIdentity4(params.Transform); params.Transform[3] = destination;
  m_Order = AIWaypoint.Cast(GetGame().SpawnEntityPrefab(Resource.Load("{750A8D1695BD6998}Prefabs/AI/Waypoints/AIWaypoint_Move.et"), actor.GetWorld(), params));
  if (!m_Order) return;
  if (!Pose(true)) return;
  m_Destination = destination; m_HadOrder = true; m_OrderCompleted = false;
  // A broad radius can complete outside a doorway while the target is indoors.
  // Reach the validated interior point closely; actual volume still decides arrival.
  m_Order.SetCompletionRadius(0.6); m_Group.AddWaypoint(m_Order);
  // Once per group; the same guard serves this group's walk and activity orders.
  // Attach() also drops any failure left over from the previous order.
  m_Guard = EAC_MoveFailureGuard.Attach(m_Group, "shelter");
  m_Group.ActivateAI(); m_Group.PreventMaxLOD(30); m_Deadline = now + 25;
 }

 bool EAC_ContainsSessionHelper(SCR_EditableEntityComponent candidate) { return EAC_SessionLifecycle.ContainsOwned(candidate, m_Order); }
}

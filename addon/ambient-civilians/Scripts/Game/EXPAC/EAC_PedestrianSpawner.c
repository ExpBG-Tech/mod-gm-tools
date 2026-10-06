// One mission-owned pending transaction, advanced by the existing shared tick.
class EAC_PedestrianSpawner
{
 // Seconds before a resident whose routine start was refused may try again.
 static const float START_RETRY_SECONDS = 20;
 protected BaseWorld m_World;
 protected ref EAC_PedestrianActivation m_Pending;
 protected ref array<ref EAC_PedestrianActivation> m_Tracked = {};
 protected float m_NextCacheStep;
 protected float m_BlockAdmissionUntil;
 protected int m_NextMaintenance;
 protected float m_NextOrderMonitor;
 // MonitorOrders was the only unbounded per-resident sweep left: five calls per
 // tracked resident per half second, two of which are not cheap. It is now split
 // by urgency - full rate inside REACTIVE_RADIUS of a player, 0.5 Hz outside it.
 // The result is measured by EAC_SchedulerStats.STEP_MONITOR, which wraps this
 // exact call in EAC_PedestrianSpawner.Step; the spawner used to keep a second
 // set of accumulators around the same call, and two ledgers for one number is
 // one ledger too many (audit item 14).
 protected float m_NextLegToken;
 // One routine START per second across the whole town, shared by the maintenance
 // batch and the forced idle intervention (audit items 3 and 4). A start runs the
 // anchor search, the scene reservation and the first approach; a forced wander
 // leg creates a waypoint and runs the walker's geometry sweep. Both are the
 // expensive half of this file, and ForceIdle used to be able to run four of them
 // per half-second tick beside the batch's one. A refusal never consumes it.
 protected float m_NextStartToken;
 // The 1 Hz gate on the forced-movement watchdog. It used to run at the full 2 Hz
 // beside a 1 Hz ServiceBatch, so the two "make somebody move" passes were paced
 // differently for no stated reason.
 protected float m_NextForce;
 // Residents currently holding a routine. Maintained where this file assigns or
 // clears activation.Activity and re-synced from the truth once per MonitorOrders
 // pass, which already touches every tracked record; TryActivity used to run a
 // full m_Tracked scan for the ActivityLimit compare on every offered turn, up to
 // twelve times a second at the city benchmark (audit item 18). GetActivityCount()
 // below stays the authoritative scan and is what the fixtures assert against.
 protected int m_ActivityCount;
 // Which gate last refused TryActivity, so a forced intervention that produced
 // nothing can name the cause instead of adding to one undifferentiated total.
 // Written by TryActivity on every refusal, read by ForceOne immediately after.
 protected int m_LastActivityGate;
 // One REMOVAL per scheduler tick, the mirror of the one-start-per-second rule.
 // Campaign 125 read max_despawn_per_tick=2 on a steady-state tick in phase 2 with
 // only three despawns in the whole run: Rollback deletes the actor and then, on
 // the same call, the group the actor's deletion just emptied, and the pending
 // ladder's own cleanup can land on the same tick as the maintenance rotation's.
 // Reset at the top of Step, which is the tick boundary for everything here
 // including SelectNext, because the module runs Step before admission.
 protected bool m_RemovalSpent;
 // Walk/routine maintenance rotation (audit item 3) and the idle-watchdog
 // rotation (the forced-movement guarantee). Both are plain cursors into
 // m_Tracked: a resident that refuses its turn is not revisited until the cursor
 // comes round again, which is the rate limit, so neither needs per-resident
 // state and neither can hammer one stuck record.
 protected int m_ServiceCursor, m_ForceCursor;
 // The 1 Hz gate on the maintenance batch and the 0.5 Hz half of the split order
 // sweep.
 protected float m_NextService, m_NextFarSweep;
 // Reused by every waypoint read in the hot sweeps. GetWaypoints inserts, so each
 // borrower clears it first. One array for the life of the spawner instead of one
 // allocation per tracked resident per half second (188 per second at the city
 // benchmark's 94 residents).
 protected ref array<AIWaypoint> m_OrderScratch = {};
 // GetHiddenReason allocated a TraceParam and an exclusion array PER OBSERVER on
 // every call, and it is called from the admission ladder, the pending ladder,
 // rollback and the cache gate several times a tick. Statics because the method
 // is static and is never re-entered: it only calls world.TraceMove. Both are
 // refilled at the top of each observer's turn, so nothing survives a call except
 // plain entity pointers.
 protected static ref TraceParam s_HiddenTrace = new TraceParam();
 protected static ref array<IEntity> s_HiddenExcluded = {};

 // Residents this near any observer are monitored at the full 2 Hz: a gunshot
 // reaction a player can watch must not wait two seconds. Everyone else is swept
 // at 0.5 Hz, which is already the granularity their own danger cooldown
 // (CalmDelay >= 5 s) and walk deadline (30 s) work at. Deliberately not a
 // setting: an owner has no way to judge it and it is a consequence of the danger
 // model, not a preference.
 static const float REACTIVE_RADIUS = 200;

 protected int m_HomeCursor, m_SlotCursor, m_IndoorProbe;
 // Population-floor diagnostics. Written by SelectNext for the candidate it last
 // evaluated; read only by BuildDebugSummary. Neither is consulted for admission.
 protected int m_LastLocalCount;
 protected bool m_CatchingUp;
 protected ref EAC_Diagnostics m_Diagnostics = new EAC_Diagnostics();

 EAC_Diagnostics GetDiagnostics() { return m_Diagnostics; }
 // Indoor distance is a maximum wake radius. Walls and LOS hide the placement;
 // the fixed safety gap and house footprint protect approaching observers.
 static const float INDOOR_MINIMUM_DISTANCE = 5;
 static bool IndoorPlacementAllowed(EAC_AmbientModule module, EAC_HouseholdRecord home, array<IEntity> observers)
 {
  if (!module || module.IndoorSpawnDistance <= 0 || !home || !home.BuildingEntity) return false;
  if (!IsRelevant(module, home.BuildingEntity.GetOrigin(), observers, true)) return false;
  IEntity house = home.BuildingEntity;
  vector mins, maxs; house.GetBounds(mins, maxs);
  foreach (IEntity observer : observers)
  {
   if (!observer) return false;
    if (vector.Distance(observer.GetOrigin(), house.GetOrigin()) < INDOOR_MINIMUM_DISTANCE) return false;
   vector local = house.CoordToLocal(observer.GetOrigin());
   if (local[0] > mins[0] && local[0] < maxs[0] && local[2] > mins[2] && local[2] < maxs[2]) return false;
  }
  return true;
 }

 // Stable per logical home slot; an indoor slot does not fall back to outdoor
 // admission merely because no player is inside its smaller wake radius.
 // Two exceptions, both so that a slot is never permanently empty: a zero
 // IndoorSpawnDistance makes every slot outdoor (it used to leave 60% of the
 // town unspawnable), and a slot that lost INDOOR_FALLBACK_FAILURES placements
 // in a row to geometry is admitted outdoors under the ordinary outdoor gates.
 static const int INDOOR_FALLBACK_FAILURES = 6;
 static bool PrefersIndoor(EAC_AmbientModule module, EAC_ResidentRecord resident)
 {
  if (!module || !resident || module.IndoorSpawnShare <= 0 || module.IndoorSpawnDistance <= 0) return false;
  if (resident.IndoorFailures >= INDOOR_FALLBACK_FAILURES) return false;
  if (module.IndoorSpawnShare >= 100) return true;
  int share = (resident.Id * 37) % 100;
  return share < module.IndoorSpawnShare;
 }

 // One more indoor placement this slot lost to geometry or navmesh.
 static void NoteIndoorFailure(EAC_ResidentRecord resident)
 {
  if (!resident || resident.IndoorFailures >= INDOOR_FALLBACK_FAILURES) return;
  resident.IndoorFailures++;
  if (resident.IndoorFailures == INDOOR_FALLBACK_FAILURES) EAC_RoutineStats.RecordIndoorToOutdoor();
 }

 // Bounded interior candidate sampled from the house footprint. The assigned
 // roof overhead is what makes the placement unobservable; navmesh projection
 // and every ordinary placement gate still run in the pending step.
 static bool IndoorCandidate(BaseWorld world, IEntity house, int index, out vector candidate)
 {
  if (!world || !house || index < 0 || index >= 9) return false;
  vector mins, maxs, transform[4];
  house.GetBounds(mins, maxs); house.GetWorldTransform(transform);
  vector local = (mins + maxs) * 0.5;
  int column = index % 3;
  local[0] = mins[0] + (maxs[0] - mins[0]) * (0.25 + column * 0.25);
  local[2] = mins[2] + (maxs[2] - mins[2]) * (0.25 + Math.Floor(index / 3.0) * 0.25);
  local[1] = mins[1] + 1;
  // A plain local for the inout floor probe; the out parameter is written once.
  vector floorPoint = local.Multiply4(transform);
  if (!FindIndoorFloor(world, floorPoint)) return false;
  candidate = floorPoint;
  return InsideHouse(world, house, candidate, null);
 }

 // The ground-storey floor under a footprint sample. Houses stand on raised
 // slabs and foundations, so the terrain under the footprint is commonly
 // 0.3-1.5 m below the floor. The old candidate sat at terrain + 0.1 m and the
 // 0.15-1.9 m body box in GetClearReason went through the floor slab or a
 // footing every time (retail-1: indoor candidates 2887/3248 passed the roof
 // test, blocked_geometry 3256, emerged 0 in 45 minutes - no indoor resident was
 // ever placed). One ray from just under a ground-storey ceiling down to below
 // the terrain; the first surface it meets is the floor (or furniture, which the
 // body box then refuses like any other obstruction).
 static const float INDOOR_FLOOR_PROBE = 2.2;
 static const float INDOOR_FLOOR_MAX_RISE = 2;
 // Floor height above the terrain heightmap accepted for an indoor placement:
 // the probe's own ceiling plus the navmesh projection's vertical reach.
 static const float INDOOR_HEIGHT_TOLERANCE = 2.5;
 static bool FindIndoorFloor(BaseWorld world, inout vector point)
 {
  if (!world) return false;
  float ground = world.GetSurfaceY(point[0], point[2]);
  float top = ground + INDOOR_FLOOR_PROBE;
  float bottom = ground - 0.5;
  TraceParam probe = new TraceParam();
  probe.Start = Vector(point[0], top, point[2]);
  probe.End = Vector(point[0], bottom, point[2]);
  probe.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
  float fraction = world.TraceMove(probe, null);
  if (fraction >= 1) return false;
  float floorY = top + (bottom - top) * fraction;
  if (floorY - ground > INDOOR_FLOOR_MAX_RISE) return false;
  point[1] = floorY + 0.05;
  return true;
 }

 // The assigned roof must be directly overhead and the footprint must contain
 // the point. Used for the candidate and again after navmesh projection.
 static bool InsideHouse(BaseWorld world, IEntity house, vector position, IEntity exclude)
 {
  if (!world || !house) return false;
  vector mins, maxs; house.GetBounds(mins, maxs);
  vector local = house.CoordToLocal(position);
  if (local[0] <= mins[0] || local[0] >= maxs[0] || local[2] <= mins[2] || local[2] >= maxs[2]) return false;
  TraceParam roof = new TraceParam();
  roof.Start = position + "0 1.7 0"; roof.End = position + "0 12 0";
  roof.Flags = TraceFlags.WORLD | TraceFlags.ENTS; roof.Exclude = exclude;
  return world.TraceMove(roof, null) < 1 && roof.TraceEnt == house;
 }

 protected bool Reject(int reason, vector position)
 {
  m_Diagnostics.Record(reason);
  m_Diagnostics.RememberRejectedPosition(position);
  return false;
 }

 void EAC_PedestrianSpawner(BaseWorld world) { m_World = world; }
 protected bool IsOwner(EAC_AmbientModule module)
 {
  return Replication.IsServer() && module && module == EAC_AmbientModule.GetActive() && module.GetWorld() == m_World && module.GetSpawner() == this;
 }

 // Terrain-height agreement, and the single most expensive line in this file to
 // have got wrong. GetSurfaceY reads the TERRAIN HEIGHTMAP only: road slabs,
 // kerbs, porches, doorsteps, jetties and bridge decks are meshes, and the
 // navmesh follows those meshes. So a perfectly good navmesh point on a road sits
 // up to a couple of metres above "the terrain" and was refused as GEOMETRY -
 // campaign 121 lost 28 of 97 walk destinations and 31 of 97 clearance checks
 // here, and campaign 118's anchor sweep lost three of eight bearings that had
 // valid navmesh under them. A GetSurfaceY-derived candidate still has to sit on
 // the heightmap, because for those the comparison is exact by construction.
 // 2.5 m covers a road embankment or a porch step and is not a licence to float:
 // the body box below still has to fit, and the water test is unchanged.
 static const float SURFACE_HEIGHT_TOLERANCE = 0.75;
 static const float NAVMESH_HEIGHT_TOLERANCE = 2.5;

 static bool IsClear(BaseWorld world, vector position, IEntity exclude = null, bool navmeshProjected = false)
 {
  return GetClearReason(world, position, exclude, navmeshProjected) == EAC_ESpawnReason.NONE;
 }

 // Siblings for callers that always hand in a navmesh-projected position, so the
 // call site says what it means instead of trailing a bare boolean.
 static bool IsNavmeshClear(BaseWorld world, vector position, IEntity exclude = null)
 {
  return GetClearReason(world, position, exclude, true) == EAC_ESpawnReason.NONE;
 }

 static int GetNavmeshClearReason(BaseWorld world, vector position, IEntity exclude = null)
 {
  return GetClearReason(world, position, exclude, true);
 }

 // indoors: a ground-storey floor point inside the assigned house. Its height
 // above the heightmap is the house's floor, not a float, so it gets
 // INDOOR_HEIGHT_TOLERANCE; the body box, water and every later gate are the same.
 static int GetClearReason(BaseWorld world, vector position, IEntity exclude = null, bool navmeshProjected = false, bool indoors = false)
 {
  if (!world) return EAC_ESpawnReason.GEOMETRY;
  float tolerance = SURFACE_HEIGHT_TOLERANCE;
  if (navmeshProjected) tolerance = NAVMESH_HEIGHT_TOLERANCE;
  if (indoors) tolerance = Math.Max(tolerance, INDOOR_HEIGHT_TOLERANCE);
  float rise = Math.AbsFloat(position[1] - world.GetSurfaceY(position[0], position[2]));
  if (rise > tolerance) return EAC_ESpawnReason.GEOMETRY;
  if (ChimeraWorldUtils.TryGetWaterSurfaceSimple(world, position - "0 0.1 0")) return EAC_ESpawnReason.WATER;
  TraceBox box = new TraceBox();
  box.Start = position; box.Mins = "-0.45 0.15 -0.45"; box.Maxs = "0.45 1.9 0.45";
  box.Flags = TraceFlags.WORLD | TraceFlags.ENTS; box.Exclude = exclude;
  if (world.TracePosition(box, null) < 0) return EAC_ESpawnReason.GEOMETRY;
  // Only reachable when navmeshProjected is true: a position the old rule would
  // have refused outright and that passed every other gate. This is the number
  // that says whether the tolerance did anything.
  if (rise > SURFACE_HEIGHT_TOLERANCE && !indoors) EAC_SchedulerStats.RecordSurfaceRecovered();
  return EAC_ESpawnReason.NONE;
 }

 static int GetObserverReason(BaseWorld world, array<IEntity> observers)
 {
  if (!observers || observers.IsEmpty()) return EAC_ESpawnReason.NO_CONTROLLED_CHARACTERS;
  if (observers.Count() > 64) return EAC_ESpawnReason.OBSERVER_LIMIT;
  int controlled, unknown, unsupported;
  foreach (IEntity observer : observers)
  {
   if (!observer) { unknown++; continue; }
   if (!ChimeraCharacter.Cast(observer) || observer.GetWorld() != world) unsupported++;
   else controlled++;
  }
  if (unsupported > 0) return EAC_ESpawnReason.UNKNOWN_OBSERVER;
  if (controlled == 0) return EAC_ESpawnReason.NO_CONTROLLED_CHARACTERS;
  if (unknown > 0) return EAC_ESpawnReason.UNKNOWN_OBSERVER;
  return EAC_ESpawnReason.NONE;
 }

 // Conservative collision LOS, independent of camera direction. No observer bypass.
 static bool IsHidden(BaseWorld world, vector position, array<IEntity> observers, IEntity exclude = null, float minimumDistance = 50)
 {
  return GetHiddenReason(world, position, observers, exclude, minimumDistance) == EAC_ESpawnReason.NONE;
 }

 static int GetHiddenReason(BaseWorld world, vector position, array<IEntity> observers, IEntity exclude = null, float minimumDistance = 50)
 {
  if (!world) return EAC_ESpawnReason.UNKNOWN_OBSERVER;
  int reason = GetObserverReason(world, observers);
  if (reason != EAC_ESpawnReason.NONE) return reason;
  foreach (IEntity observer : observers)
  {
   ChimeraCharacter character = ChimeraCharacter.Cast(observer);
   if (vector.Distance(character.GetOrigin(), position) < minimumDistance) return EAC_ESpawnReason.TOO_NEAR;
   s_HiddenTrace.Start = character.EyePosition();
   s_HiddenTrace.Flags = TraceFlags.WORLD | TraceFlags.ENTS | TraceFlags.VISIBILITY | TraceFlags.ANY_CONTACT;
   s_HiddenExcluded.Clear(); s_HiddenExcluded.Insert(observer);
   if (exclude) s_HiddenExcluded.Insert(exclude);
   IEntity vehicle = CompartmentAccessComponent.GetVehicleIn(observer);
   if (vehicle) s_HiddenExcluded.Insert(vehicle);
   s_HiddenTrace.ExcludeArray = s_HiddenExcluded;
   for (int sample = 0; sample < 3; sample++)
   {
    s_HiddenTrace.End = position + Vector(0, 0.2 + sample * 0.75, 0);
    if (world.TraceMove(s_HiddenTrace, null) >= 1) return EAC_ESpawnReason.VISIBLE;
   }
  }
  return EAC_ESpawnReason.NONE;
 }

 static bool IsRelevant(EAC_AmbientModule module, vector position, array<IEntity> observers, bool indoors = false)
 {
  if (!module || GetObserverReason(module.GetWorld(), observers) != EAC_ESpawnReason.NONE) return false;
  int distance = module.WakeDistance;
  if (indoors) distance = module.IndoorSpawnDistance;
  if (distance <= 0) return false;
  foreach (IEntity observer : observers)
   if (vector.Distance(observer.GetOrigin(), position) <= distance) return true;
  return false;
 }

 protected bool ValidPosition(EAC_AmbientModule module, EAC_HouseholdRecord home, vector position, array<IEntity> observers, IEntity exclude = null, float minimumDistance = 50, bool indoors = false)
 {
  if (!home || !home.BuildingEntity) return Reject(EAC_ESpawnReason.BAD_HOME, position);
  if (vector.Distance(home.BuildingEntity.GetOrigin(), position) > 40) return Reject(EAC_ESpawnReason.OUTSIDE_DISTANCE, position);
  int reason = GetObserverReason(m_World, observers);
  if (reason != EAC_ESpawnReason.NONE) return Reject(reason, position);
  if (!IsRelevant(module, home.BuildingEntity.GetOrigin(), observers, indoors)) return Reject(EAC_ESpawnReason.OUTSIDE_DISTANCE, position);
  if (!EAC_ExclusionZone.IsPopulationAllowed(home.BuildingEntity.GetOrigin()) || !EAC_ExclusionZone.IsPopulationAllowed(position)) return Reject(EAC_ESpawnReason.EXCLUSION, position);
  // Indoor positions are validated against the floor they stand on, at every
  // call site: Begin, the pending re-check, the navmesh projection and the
  // settled body. Outdoor positions keep the heightmap tolerance.
  reason = GetClearReason(m_World, position, exclude, false, indoors);
  if (reason == EAC_ESpawnReason.NONE) reason = GetHiddenReason(m_World, position, observers, exclude, minimumDistance);
  if (reason != EAC_ESpawnReason.NONE) return Reject(reason, position);
  return true;
 }

 protected Resource QualifiedResource(ResourceName prefab)
 {
  Resource resource = Resource.Load(prefab);
  if (!resource || !resource.IsValid()) return null;
  IEntitySource source = SCR_BaseContainerTools.FindEntitySource(resource);
  if (!source) return null;
  IEntityComponentSource affiliation = SCR_BaseContainerTools.FindComponentSource(source, SCR_CharacterFactionAffiliationComponent);
  string faction;
  if (!affiliation || !affiliation.Get("faction affiliation", faction)) return null;
  EAC_ThemeAsset asset = new EAC_ThemeAsset(); asset.Prefab = prefab; asset.Weight = 1; asset.SourceFaction = faction;
  ResourceName canonical; string reason;
  if (!EAC_ThemeSelection.ValidateAsset(asset, false, canonical, reason)) return null;
  return resource;
 }

 protected IEntity Spawn(Resource resource, vector position)
 {
  if (!resource || !resource.IsValid()) return null;
  EntitySpawnParams params = new EntitySpawnParams(); params.TransformMode = ETransformMode.WORLD;
  Math3D.MatrixIdentity4(params.Transform); params.Transform[3] = position;
  IEntity created = GetGame().SpawnEntityPrefab(resource, m_World, params);
  // Gradual admission evidence (deliverable 5). The single-slot pending ladder
  // already makes two groups in one tick structurally impossible; this counts
  // what actually happened so the claim is measured rather than argued.
  if (created) EAC_SchedulerStats.RecordSpawn();
  return created;
 }

 bool Begin(EAC_AmbientModule module, EAC_HouseholdRecord home, EAC_ResidentRecord resident, vector position, array<IEntity> observers, bool indoors = false)
 {
  if (!IsOwner(module)) return Reject(EAC_ESpawnReason.BAD_OWNER, position);
  if (m_Pending) return Reject(EAC_ESpawnReason.PENDING, position);
  if (m_World.GetWorldTime() * 0.001 < m_BlockAdmissionUntil) return Reject(EAC_ESpawnReason.CACHE_RECOVERY, position);
  if (m_Tracked.Count() >= 200) return Reject(EAC_ESpawnReason.CAPACITY, position);
  int reason = module.GetAdmissionReason(home, resident, position);
  if (reason != EAC_ESpawnReason.NONE) return Reject(reason, position);
  float minimum = 50;
  if (indoors)
  {
   if (!IndoorPlacementAllowed(module, home, observers) || !InsideHouse(m_World, home.BuildingEntity, position, null)) return Reject(EAC_ESpawnReason.TOO_NEAR, position);
   minimum = INDOOR_MINIMUM_DISTANCE;
  }
  if (!ValidPosition(module, home, position, observers, null, minimum, indoors)) return false;
  ResourceName freshPrefab = module.GetHomeIndex().GetRegistry().PickCharacter(resident.CharacterPrefab);
  if (!QualifiedResource(freshPrefab)) return Reject(EAC_ESpawnReason.BAD_PREFAB, position);
  if (!GetGame().GetFactionManager() || !GetGame().GetFactionManager().GetFactionByKey(EAC_AmbientModule.CIV_FACTION)) return Reject(EAC_ESpawnReason.BAD_FACTION, position);
  if (module.GetReservedPopulation() >= module.PopulationLimit) return Reject(EAC_ESpawnReason.BUDGET, position);
  if (!HasEngineAiHeadroom()) return Reject(EAC_ESpawnReason.AI_LIMIT, position);
  if (!module.CanAdmitNewWork()) return Reject(EAC_ESpawnReason.LOAD_PAUSED, position);
  if (!HasLocalCapacity(module, position)) return Reject(EAC_ESpawnReason.LOCAL_BUDGET, position);
  EAC_ResidentClaim claim = module.BeginResidentActivation(home, resident, position);
  if (!claim) return Reject(EAC_ESpawnReason.RESIDENT_INELIGIBLE, position);
  resident.CharacterPrefab = freshPrefab;
  m_Pending = new EAC_PedestrianActivation(); m_Pending.Claim = claim; m_Tracked.Insert(m_Pending);
  m_Pending.Position = position; m_Pending.Aborting = false; m_Pending.CleanupRequested = false; m_Pending.SettleUntil = 0;
  m_Pending.SpawnedIndoors = indoors;
  m_Pending.Deadline = m_World.GetWorldTime() * 0.001 + 10;
  SCR_AIGroup group = SCR_AIGroup.Cast(Spawn(Resource.Load("{000CD338713F2B5A}Prefabs/AI/Groups/Group_Base.et"), position));
  m_Diagnostics.Record(EAC_ESpawnReason.STARTED);
  if (!group) { m_Pending.Fail("group prefab spawn failed"); return true; }
  group.SetDeleteWhenEmpty(false);
  if (!module.TrackResidentGroup(m_Pending.Claim, group))
  {
   // Nothing owns this group yet, so deferring its deletion would lose it
   // outright. It is counted as a creation rollback rather than as a removal.
   EAC_SchedulerStats.RecordCreationRollback();
   SCR_EntityHelper.DeleteEntityAndChildren(group); m_Pending.Fail("group ownership registration failed"); return true;
  }
  EAC_SessionLifecycle.Keep(group);
  group.SetFaction(GetGame().GetFactionManager().GetFactionByKey(EAC_AmbientModule.CIV_FACTION));
  SCR_AIGroupSettingsComponent settings = SCR_AIGroupSettingsComponent.Cast(group.FindComponent(SCR_AIGroupSettingsComponent));
  if (!settings) { m_Pending.Fail("group settings component missing"); return true; }
  // Ambient owns calm/danger stance transitions. Native danger behaviors have
  // higher causes than GROUP_GOAL and otherwise ignore this setting on recovery.
  bool stance = settings.AddSetting(SCR_AICharacterStanceSetting.Create(SCR_EAISettingOrigin.SCENARIO, SCR_EAIBehaviorCause.ALWAYS, ECharacterStance.STAND), false, true);
  bool speed = settings.AddSetting(SCR_AICharacterMovementSpeedSetting.Create(SCR_EAISettingOrigin.SCENARIO, SCR_EAIBehaviorCause.GROUP_GOAL, EMovementType.WALK), false, true);
  bool groupSpeed = settings.AddSetting(SCR_AIGroupCharactersMovementSpeedSetting.Create(SCR_EAISettingOrigin.SCENARIO, EMovementType.WALK), false, true);
  bool combat = settings.AddSetting(SCR_AIGroupCombatModeSetting.Create(SCR_EAISettingOrigin.SCENARIO, EAIGroupCombatMode.HOLD_FIRE), false, true);
  if (!stance || !speed || !groupSpeed || !combat) m_Pending.Fail("civilian group settings rejected");
  return true;
 }

 // Cheap live control/affiliation check; carried equipment is checked separately.
 static bool HasCivilianControl(IEntity actor, SCR_AIGroup group)
 {
  if (!actor || !group || group.GetAgentsCount() != 1 || !group.GetFaction() || group.GetFaction().GetFactionKey() != EAC_AmbientModule.CIV_FACTION) return false;
  CharacterControllerComponent controller = CharacterControllerComponent.Cast(actor.FindComponent(CharacterControllerComponent));
  FactionAffiliationComponent affiliation = FactionAffiliationComponent.Cast(actor.FindComponent(FactionAffiliationComponent));
  AIControlComponent control = AIControlComponent.Cast(actor.FindComponent(AIControlComponent));
  if (!controller || controller.GetLifeState() != ECharacterLifeState.ALIVE || !affiliation || !affiliation.GetAffiliatedFaction() || affiliation.GetAffiliatedFaction().GetFactionKey() != EAC_AmbientModule.CIV_FACTION || !control || !control.GetAIAgent() || control.GetAIAgent().GetParentGroup() != group) return false;
  if (!SCR_AICharacterSettingsComponent.FindOnControlledEntity(actor)) return false;
  return true;
 }

 static bool IsCivilian(IEntity actor, SCR_AIGroup group)
 {
  if (!HasCivilianControl(actor, group)) return false;
  BaseWeaponManagerComponent weapons = BaseWeaponManagerComponent.Cast(actor.FindComponent(BaseWeaponManagerComponent));
  InventoryStorageManagerComponent inventory = InventoryStorageManagerComponent.Cast(actor.FindComponent(InventoryStorageManagerComponent));
  if (!weapons || !inventory || inventory.GetGrenadesCount() != 0) return false;
  array<IEntity> carried = {}; weapons.GetWeaponsList(carried);
  if (!carried.IsEmpty()) return false;
  inventory.GetItems(carried, EStoragePurpose.PURPOSE_ANY);
  if (carried.Count() > 128) return false;
  foreach (IEntity item : carried)
   if (item && item.FindComponent(BaseWeaponComponent)) return false;
  return true;
 }

 protected void ReleaseTracking(EAC_PedestrianActivation activation)
 {
  if (activation.Activity && !activation.Activity.Monitor(EAC_AmbientModule.GetActive(), m_World.GetWorldTime() * 0.001)) { m_NextMaintenance++; return; }
  if (activation.Activity && m_ActivityCount > 0) m_ActivityCount--;
  activation.Activity = null;
  activation.Detach();
  m_Tracked.RemoveItem(activation);
 }

 protected bool Rollback(EAC_PedestrianActivation activation, EAC_AmbientModule module, array<IEntity> observers)
 {
  EAC_ResidentClaim claim = activation.Claim;
  IEntity actor = claim.Character;
  SCR_AIGroup group = claim.Group;
  if (actor)
  {
   if (activation.PlayerTouched || SCR_PossessingManagerComponent.GetPlayerIdFromControlledEntity(actor) != 0 || CompartmentAccessComponent.GetVehicleIn(actor)) return false;
   if (!observers || (!observers.IsEmpty() && !IsHidden(m_World, actor.GetOrigin(), observers, actor))) return false;
   if (!TakeRemoval()) return false;
   activation.CleanupRequested = true;
   EAC_SchedulerStats.RecordDespawn();
   SCR_EntityHelper.DeleteEntityAndChildren(actor);
  }
  if (claim.Character) return false;
  if (group)
  {
   if (group.GetAgentsCount() > 0) return false;
   // The actor's own deletion emptied this group moments ago on this same tick,
   // which is exactly how one rollback used to remove two entities at once. The
   // group waits for the next tick; the claim is retained until it goes.
   if (!TakeRemoval()) return false;
   EAC_SchedulerStats.RecordDespawn();
   SCR_EntityHelper.DeleteEntityAndChildren(group);
  }
  return module.CancelResidentActivation(claim);
 }

 // Only our bounded active list (<=200), never a world scan or the dormant cache.
 // One actor/group deletion per shared step; a protected resident cannot stall
 // cleanup of other residents. Live zone geometry also covers moves and resizes.
 protected bool ClearExcluded(EAC_AmbientModule module)
 {
  foreach (EAC_PedestrianActivation activation : m_Tracked)
  {
   EAC_ResidentClaim claim = activation.Claim;
   if (!claim || module.GetResidentActivation(claim.Home, claim.Resident) != claim) continue;
   SCR_ChimeraCharacter actor = SCR_ChimeraCharacter.Cast(claim.Character);
   SCR_AIGroup group = claim.Group;
   vector position = activation.Position;
   if (actor) position = actor.GetOrigin();
   bool outsideArea = !module.ContainsPopulationPosition(position) || (claim.Home.BuildingEntity && !module.ContainsPopulationPosition(claim.Home.BuildingEntity.GetOrigin()));
   if (!activation.ExclusionRemoval && !activation.SessionRemoval && !outsideArea && !EAC_ExclusionZone.IsManualPopulationExcluded(position))
   {
    // A now-blocked home also cancels an unfinished owned cleanup.
    if (actor || !claim.Cache || !claim.Home.BuildingEntity || !EAC_ExclusionZone.IsManualPopulationExcluded(claim.Home.BuildingEntity.GetOrigin())) continue;
   }
   activation.Observe();
   if (actor && actor.EBG_WasPlayerControlled()) activation.PlayerTouched = true;
   if ((activation.PlayerTouched || (claim.OptimizerMember && claim.OptimizerMember.WasPlayer)) && (actor || (!activation.SessionRemoval && group) || (claim.OptimizerMember && claim.OptimizerMember.Entity)))
   {
    // A takeover during cleanup returns to ordinary protected maintenance.
    activation.ExclusionRemoval = false; activation.CleanupRequested = false;
    continue;
   }
   if (group)
   {
    RplComponent groupRpl = RplComponent.Cast(group.FindComponent(RplComponent));
    if (!groupRpl || groupRpl.IsProxy() || !groupRpl.IsOwner() || group.GetPlayerCount() != 0 || group.GetAgentsCount() > 1 || !EBG_PrefabFullCache.CanDeleteFullEntity(group)) continue;
    // No authored/native children belong to an Ambient Group_Base. Do not erase
    // something attached by another system along with the group's editor icon.
    if (group.GetChildren()) continue;
   }
   if (actor)
   {
    RplComponent actorRpl = RplComponent.Cast(actor.FindComponent(RplComponent));
    if (!actorRpl || actorRpl.IsProxy() || !actorRpl.IsOwner() || actor.EBG_WasPlayerControlled() || SCR_PossessingManagerComponent.GetPlayerIdFromControlledEntity(actor) != 0) continue;
    CharacterControllerComponent controller = actor.GetCharacterController();
    if (!controller || controller.IsPlayerControlled() || controller.IsDead() || CompartmentAccessComponent.GetVehicleIn(actor) || !EBG_PrefabFullCache.CanDeleteFullEntity(actor)) continue;
    AIControlComponent control = AIControlComponent.Cast(actor.FindComponent(AIControlComponent));
    if (!group || !control || !control.GetAIAgent() || control.GetAIAgent().GetParentGroup() != group) continue;
   }
   if (!activation.ExclusionRemoval && (!activation.SessionRemoval || !activation.CleanupRequested))
   {
    if (!activation.SessionRemoval) activation.ExclusionRemoval = true;
    activation.CleanupRequested = true;
    if (!claim.Committed)
    {
     if (activation.SessionRemoval) activation.Fail("session load");
     else activation.Fail("manual exclusion");
    }
    if (activation == m_Pending) m_Pending = null;
    activation.Walking.Stop(); activation.Shelter.Stop(); activation.Emerge.Stop();
    activation.IdleSince = 0;
    if (activation.Activity) activation.Activity.RequestStop(true);
    // Stop callbacks may transfer ownership. Recheck everything next tick
    // before any destructive operation, including when no activity was running.
    continue;
   }
   // Native loiter exit must finish before its actor, helpers or props go away.
   if (activation.Activity) continue;
   if (actor)
   {
    if (!TakeRemoval()) return false;
    EAC_SchedulerStats.RecordDespawn();
    SCR_EntityHelper.DeleteEntityAndChildren(actor);
    return true;
   }
   if (group)
   {
    if (group.GetAgentsCount() != 0 || !TakeRemoval()) continue;
    EAC_SchedulerStats.RecordDespawn();
    SCR_EntityHelper.DeleteEntityAndChildren(group);
    return true;
   }
   EAC_ResidentClaims ledger = EAC_AmbientModule.GetMissionClaims();
   if (ledger && ledger.FinishExclusionRemoval(claim, activation.SessionRemoval))
   {
    EAC_SceneIndex.ReleaseAllFor(claim.Resident.Id);
    ReleaseTracking(activation);
    return true;
   }
  }
  return false;
 }

 // One retained record per shared tick; protected recovery never owns m_Pending.
 protected bool MaintainNext(EAC_AmbientModule module, array<IEntity> observers, bool cacheTick)
 {
  if (m_Tracked.IsEmpty()) return false;
  m_NextMaintenance = m_NextMaintenance % m_Tracked.Count();
  EAC_PedestrianActivation activation = m_Tracked[m_NextMaintenance];
   if (activation.ExclusionRemoval || activation.SessionRemoval || activation == m_Pending) { m_NextMaintenance++; return false; }
  EAC_ResidentClaim claim = activation.Claim;
  // A tracked record with no claim owns nothing and can never be reconciled; it
  // used to be dereferenced on the very next line (audit item 16).
  if (!claim) { ReleaseTracking(activation); return false; }
  if (module.GetResidentActivation(claim.Home, claim.Resident) != claim) { ReleaseTracking(activation); return false; }
  activation.Observe();
   // The cleanup transaction remains only until its actor and group are absent.
  if (claim.Cache)
  {
    bool worked = false;
    if (cacheTick && !m_Pending) worked = EAC_PedestrianCaching.Advance(module, activation, observers, m_World.GetWorldTime() * 0.001);
    if (module.GetResidentActivation(claim.Home, claim.Resident) != claim) ReleaseTracking(activation);
    else m_NextMaintenance++;
    return worked;
  }
  bool released;
  if (claim.Committed)
  {
    if (cacheTick && !m_Pending && EAC_PedestrianCaching.TrySleep(module, activation, observers, m_World.GetWorldTime() * 0.001))
    {
     m_NextMaintenance++;
    return true;
   }
   // Routine admission and the wander leg used to live here, one resident per
   // 0.5 s tick: 12 s between retries at 24 tracked and 47 s at the city
   // benchmark (audit item 3). They are now ServiceBatch's job, which visits
   // several residents per tick. This rotation keeps the cache and removal
   // duties, which must stay at one record per tick.
   // Preserve living actors and corpses; only clean an empty orphan group.
   if (!claim.Character)
   {
    // The orphan group goes only if this tick's removal is still unspent; the
    // capacity release waits with it, because releasing a claim whose group is
    // still standing is how an empty group outlives its record.
    if (claim.Group && claim.Group.GetAgentsCount() == 0)
    {
     if (!TakeRemoval()) { m_NextMaintenance++; return false; }
     EAC_SchedulerStats.RecordDespawn();
     SCR_EntityHelper.DeleteEntityAndChildren(claim.Group);
    }
    released = module.FinishResidentRemoval(claim, claim.Resident.Dead);
   }
  }
  else if (activation.Aborting) released = Rollback(activation, module, observers);
  if (released) ReleaseTracking(activation);
  else m_NextMaintenance++;
  return false;
 }

 // Walk and routine maintenance for several residents per second (audit item 3).
 //
 // One resident per 0.5 s tick meant a refused routine or a refused wander leg
 // waited tracked * 0.5 s for its next attempt: 12 s at 24 tracked, 47 s at the
 // city benchmark's 94, which is the mechanism behind that benchmark's
 // peak_idle_gap 416. The batch is Math.Clamp(tracked/8, 1, 8) records once per
 // second, so the per-resident revisit is tracked/budget seconds - 8 s at 24 and
 // 11.75 s at 94 - with a hard per-tick ceiling of eight records whatever the
 // population is. The expensive call inside is EAC_PedestrianWalk.Step, whose own
 // per-resident backoff (m_NextAttempt = now + 1) still limits a single resident
 // to one geometry sweep per second however often the batch reaches it.
 protected void ServiceBatch(EAC_AmbientModule module, array<IEntity> observers, float now)
 {
  if (!IsOwner(module) || m_Tracked.IsEmpty()) return;
  if (now < m_NextService) return;
  m_NextService = now + 1;
  int count = m_Tracked.Count();
  int budget = Math.Clamp(count / 8, 1, 8);
  if (budget > count) budget = count;
  // At most one routine START per second, whatever the batch size. A start runs
  // the anchor search, the scene reservation and the first approach, which is by
  // far the most expensive thing in this file; eight of them landing on one tick
  // is exactly the spike the old one-record rotation prevented by accident. The
  // batch exists to raise the RETRY rate for refused residents, and a refusal
  // does not consume this token. It is the SAME token the forced idle watchdog
  // spends, so the two passes cannot each start a routine on the same tick.
  for (int i = 0; i < budget; i++)
  {
   m_ServiceCursor = m_ServiceCursor % m_Tracked.Count();
   EAC_PedestrianActivation activation = m_Tracked[m_ServiceCursor++];
   ServiceOne(module, activation, observers, now, AllowStart(now));
  }
 }

 // The shared one-start-per-second budget. Read it before the work and spend it
 // only when something was actually created, so a refusal still leaves the next
 // resident in the batch a turn.
 // True at most once per scheduler tick. A refused caller leaves the entity alone
 // and is revisited by the retained-cleanup rotation on a later tick, which is the
 // same mechanism that already handles a protected or observed removal.
 bool TakeRemoval()
 {
  if (m_RemovalSpent) { EAC_SchedulerStats.RecordRemovalDeferred(); return false; }
  m_RemovalSpent = true;
  return true;
 }

 protected bool AllowStart(float now) { return now >= m_NextStartToken; }
 protected void ConsumeStart(float now) { m_NextStartToken = now + 1; }

 // One resident's ordinary progress: try to start a routine, otherwise keep a
 // wander leg running toward the next scene. Every gate inside TryActivity and
 // EAC_PedestrianWalk.Step is unchanged; only the rate at which they are offered
 // a turn moved. Returns whether a routine actually started.
 protected bool ServiceOne(EAC_AmbientModule module, EAC_PedestrianActivation activation, array<IEntity> observers, float now, bool allowStart)
 {
  if (activation.ExclusionRemoval || activation.SessionRemoval || activation == m_Pending) return false;
  EAC_ResidentClaim claim = activation.Claim;
  if (!claim || !claim.Committed || claim.Cache || !claim.Character) return false;
  // The forced watchdog runs immediately before this batch on the same tick. Its
  // refusals arm the walker's backoff deliberately, and SetGoal below clears that
  // backoff whenever the itinerary's next scene moved more than two metres, so
  // the ordinary batch used to undo the brake the forced pass had just applied
  // (audit item 19). A resident that was just given an intervention keeps it.
  if (activation.ForcedAt != 0 && now - activation.ForcedAt < 1) return false;
  if (allowStart && !m_Pending && TryActivity(module, activation, observers))
  {
   // Same brake the maintenance rotation applied when it owned this work: a
   // routine start and a fresh admission never land on the same tick.
   ConsumeStart(now);
   m_BlockAdmissionUntil = now + 0.5;
   return true;
  }
  // Point the wander at the next scene while no reservation is held; the
  // activity owns its own approach once one is. A resident outside its own leash
  // (back from a neighbour's shared spot, EAC_ActivityStation.JOIN_RANGE) aims
  // home first: no routine starts out there, and the walker only accepts legs
  // that bring it nearer.
  vector goal;
  bool outsideLeash = !activation.Activity && claim.Home.BuildingEntity != null && vector.Distance(claim.Character.GetOrigin(), claim.Home.BuildingEntity.GetOrigin()) > module.RoutineRange;
  if (outsideLeash) activation.Walking.SetGoal(claim.Home.BuildingEntity.GetOrigin());
  else if (!activation.Activity && EAC_SceneItinerary.Peek(module, claim, goal)) activation.Walking.SetGoal(goal);
  else activation.Walking.ClearGoal();
  if (!activation.Emerge.IsActive()) activation.Walking.Step(module, activation);
  return false;
 }

 void ~EAC_PedestrianSpawner()
 {
  if (GetGame()) GetGame().GetCallqueue().Remove(DrainActivities);
  foreach (EAC_PedestrianActivation activation : m_Tracked) activation.Detach();
 }

 void StopOwnedWalks()
 {
  if (!Replication.IsServer()) return;
  foreach (EAC_PedestrianActivation activation : m_Tracked) { activation.Walking.Stop(); activation.Shelter.Stop(); activation.ClearSince = 0; if (activation.Activity) activation.Activity.RequestStop(); }
  GetGame().GetCallqueue().Remove(DrainActivities);
  if (GetActivityCount() > 0 || HasOwnedGroup()) GetGame().GetCallqueue().CallLater(DrainActivities, 500, true);
 }

 // One shared cleanup pump survives removal of the last configuration module.
 // It stops once helpers and owned groups are gone or a module resumes ordinary
 // monitoring. While owned groups remain it keeps running (one pass over at most
 // 200 records every half second) so a group emptied later in the gap - a
 // resident deleted or killed by someone else - is removed as well.
 protected void DrainActivities()
 {
  if (!GetGame() || GetGame().GetWorld() != m_World || !Replication.IsServer() || EAC_AmbientModule.GetActive())
  {
   if (GetGame()) GetGame().GetCallqueue().Remove(DrainActivities);
   return;
  }
  float now = m_World.GetWorldTime() * 0.001;
  foreach (EAC_PedestrianActivation activation : m_Tracked)
  {
   if (!activation.Activity || !activation.Activity.Monitor(null, now)) continue;
   activation.Activity = null;
   if (m_ActivityCount > 0) m_ActivityCount--;
  }
  RemoveEmptyGroup();
  EAC_SessionLifecycle.Sync(now);
  if (GetActivityCount() == 0 && !HasOwnedGroup()) GetGame().GetCallqueue().Remove(DrainActivities);
 }

 protected bool HasOwnedGroup()
 {
  foreach (EAC_PedestrianActivation activation : m_Tracked)
   if (activation.Claim && activation.Claim.Group) return true;
  return false;
 }

 // A controller gap must not strand an empty "Non-Combatant(s)" group in the GM
 // entity list. Live run 2026-10-06: the module was deleted with reserved=3 and an
 // empty civilian group stayed for the rest of the session, because every path
 // that deletes an emptied group (MaintainNext, Rollback, ClearExcluded and the
 // sleep cache) only runs under an active controller. Same owned-group guards as
 // ClearExcluded: this claim's own group, server authority, no player, no agents,
 // no children, deletable. A living actor keeps its group (an unfinished pending
 // spawn may not have joined it yet); a corpse does not. One deletion per pump
 // tick. The claim keeps its reservation; a replacement controller's ordinary
 // maintenance then finds actor and group absent and releases it.
 protected bool RemoveEmptyGroup()
 {
  foreach (EAC_PedestrianActivation activation : m_Tracked)
  {
   EAC_ResidentClaim claim = activation.Claim;
   if (!claim || !claim.Group) continue;
   SCR_AIGroup group = claim.Group;
   if (group.GetAgentsCount() != 0 || group.GetPlayerCount() != 0 || group.GetChildren()) continue;
   ChimeraCharacter actor = ChimeraCharacter.Cast(claim.Character);
   if (actor)
   {
    CharacterControllerComponent controller = actor.GetCharacterController();
    if (controller && !controller.IsDead()) continue;
   }
   RplComponent groupRpl = RplComponent.Cast(group.FindComponent(RplComponent));
   if (!groupRpl || groupRpl.IsProxy() || !groupRpl.IsOwner() || !EBG_PrefabFullCache.CanDeleteFullEntity(group)) continue;
   EAC_SchedulerStats.RecordDespawn();
   SCR_EntityHelper.DeleteEntityAndChildren(group);
   return true;
  }
  return false;
 }

 // The authoritative scan. Debug lines and every fixture assert against this;
 // only the hot ActivityLimit compare inside TryActivity reads the maintained
 // count instead (audit item 18).
 int GetActivityCount()
 {
  int count;
  foreach (EAC_PedestrianActivation activation : m_Tracked) if (activation.Activity) count++;
  return count;
 }

 // The maintained count, exposed so a fixture can prove it agrees with the scan.
 int GetTrackedActivityCount() { return m_ActivityCount; }

 // A resident has just opened a shared indoor spot (EAC_ActivityStation.Acquire).
 // Offer its free position to the nearest idle resident in reach by clearing that
 // resident's routine cooldown, exactly as RecoverIdle does: its ordinary start
 // ladder (TryActivity -> Start -> BeginLeg -> InviteOpen) then takes the
 // invitation within seconds instead of whenever its own rest happens to end.
 // One bounded pass over the tracked list (<= 200) per spot opened. Nothing is
 // created here and no placement, ownership, budget or approach gate is skipped.
 bool CallToStation(EAC_AmbientModule module, EAC_ResidentClaim host, vector spot, float now)
 {
  if (!IsOwner(module) || !host) return false;
  float reach = EAC_ActivityStation.JOIN_RANGE;
  EAC_PedestrianActivation best;
  float bestDistance = reach;
  foreach (EAC_PedestrianActivation activation : m_Tracked)
  {
   if (activation == m_Pending || activation.ExclusionRemoval || activation.SessionRemoval || activation.PlayerTouched || activation.Activity) continue;
   EAC_ResidentClaim claim = activation.Claim;
   if (!claim || claim == host || !claim.Committed || claim.Cache || !claim.Character || !claim.Group || !claim.Home || !claim.Home.BuildingEntity) continue;
   if (claim.AlarmUntil > now || claim.Resident.Dead || CompartmentAccessComponent.GetVehicleIn(claim.Character)) continue;
   // The two leashes the start will apply anyway: BeginLeg needs the resident
   // near its own home, and CanApproach the spot within visiting reach of it.
   vector home = claim.Home.BuildingEntity.GetOrigin();
   vector position = claim.Character.GetOrigin();
   if (vector.Distance(position, home) > module.RoutineRange || vector.Distance(spot, home) > reach) continue;
   float distance = vector.Distance(position, spot);
   if (distance >= bestDistance) continue;
   best = activation; bestDistance = distance;
  }
  if (!best) return false;
  best.Claim.Resident.NextRoutineAt = 0;
  // Zero is TryActivity's "never armed" sentinel and would add its ten-second
  // arming wait; a time already past is what "no cooldown" means (ForceOne).
  if (best.NextActivity == 0 || best.NextActivity > now) best.NextActivity = now - 1;
  best.Walking.ResetBackoff();
  return true;
 }

 protected bool TryActivity(EAC_AmbientModule module, EAC_PedestrianActivation activation, array<IEntity> observers)
 {
  // The gate splits below are for the refusal breakdown only; every condition and
  // its order of evaluation is unchanged except that the ActivityLimit read moved
  // one place earlier, and both it and CanAdmitNewWork are pure reads.
  m_LastActivityGate = EAC_SchedulerStats.GATE_ACTIVITY_OTHER;
  float now = m_World.GetWorldTime() * 0.001;
  if (activation.Activity || activation.PlayerTouched || !module.CanAdmitNewWork()) { activation.RecordRefusal(EAC_PedestrianActivation.REFUSED_BUSY, now); return false; }
  // Emergence blocks a routine only while it actually HOLDS an order. Armed but
  // orderless emergence used to refuse here, and the wander fallbacks refuse on
  // the same test, while RecoverIdle re-armed it every IdleRecoverySeconds for
  // any resident whose doorstep sits under its own roof - a resident with no path
  // to any order at all (danger fixture v6d-danger-005152: refused every 20 s for
  // 90 s with activities=0).
  if (EmergenceHoldsOrder(activation, activation.Claim)) { activation.RecordRefusal(EAC_PedestrianActivation.REFUSED_EMERGE, now); return false; }
  if (module.ActivityLimit <= 0)
  {
   m_LastActivityGate = EAC_SchedulerStats.GATE_ACTIVITY_LIMIT;
   activation.RecordRefusal(EAC_PedestrianActivation.REFUSED_LIMIT, now);
   return false;
  }
  if (activation.Claim.AlarmUntil > now) { activation.RecordRefusal(EAC_PedestrianActivation.REFUSED_ALARM, now); return false; }
  int stagger = activation.Claim.Resident.Id % 5;
  if (activation.NextActivity == 0)
  {
   activation.NextActivity = now + 10 + stagger;
   activation.RecordRefusal(EAC_PedestrianActivation.REFUSED_ARMING, now);
   return false;
  }
  if (now < activation.NextActivity) { activation.RecordRefusal(EAC_PedestrianActivation.REFUSED_COOLDOWN, now); return false; }
  if (m_ActivityCount >= module.ActivityLimit)
  {
   m_LastActivityGate = EAC_SchedulerStats.GATE_ACTIVITY_LIMIT;
   activation.RecordRefusal(EAC_PedestrianActivation.REFUSED_LIMIT, now);
   return false;
  }
  if (!EAC_PedestrianCaching.Relevant(module, activation, observers)) { activation.RecordRefusal(EAC_PedestrianActivation.REFUSED_RELEVANCE, now); return false; }
  activation.NextActivity = now + module.ActivityInterval;
  EAC_CivilianActivity activity = new EAC_CivilianActivity();
  // A start refused by anchor supply or geometry used to cost the whole
  // ActivityInterval (run 84: a resident with ten free surveyed spots stood
  // 90+ s because every refused start re-armed a 30-120 s wait). A refused
  // start now retries after START_RETRY_SECONDS; one anchor search per retry
  // per idle resident is bounded work, and a routine that DID start keeps the
  // full interval it was charged above.
  bool started = activity.Start(module, activation, now);
  // Start is the only path that sets activation.Activity, so the maintained count
  // moves with it; the next MonitorOrders pass re-syncs from the tracked list
  // whatever happened in between.
  if (started && activation.Activity) m_ActivityCount++;
  if (started) activation.RecordRefusal(EAC_PedestrianActivation.REFUSED_NONE, now);
  if (!started)
  {
   m_LastActivityGate = EAC_SchedulerStats.GATE_START;
   activation.RecordRefusal(EAC_PedestrianActivation.REFUSED_START, now);
   float retry = now + START_RETRY_SECONDS;
   // Two seconds still leaves the group empty on a later tick than the removal,
   // which is all the two-tick rule asks.
   if (activity.ClearedOwnLeg()) retry = now + 2;
   if (retry < activation.NextActivity) activation.NextActivity = retry;
  }
  return started;
 }

 // One leg transition per shared tick across the whole town. A ceiling, not a
 // throttle: eight concurrent routines transition about once per hundred seconds
 // each, so leg_deferred comparable with legs means chains are being starved by
 // geometry, not by this token.
 bool TakeLegToken(float now)
 {
  if (now < m_NextLegToken) { EAC_RoutineStats.RecordLegDeferred(); return false; }
  m_NextLegToken = now + 0.5;
  return true;
 }

 // Is any observer inside the reactive radius of this position? Pure arithmetic:
 // one squared XZ compare per observer, no trace and no world query.
 protected bool NearObserver(array<IEntity> observers, vector position)
 {
  if (!observers || observers.IsEmpty() || observers.Count() > 64) return false;
  float limit = REACTIVE_RADIUS * REACTIVE_RADIUS;
  foreach (IEntity observer : observers)
  {
   if (!observer) continue;
   vector delta = position - observer.GetOrigin(); delta[1] = 0;
   if (delta.LengthSq() <= limit) return true;
  }
  return false;
 }

 // Separate fast order safety from staggered geometry, inventory and recovery work.
 void MonitorOrders(EAC_AmbientModule module, array<IEntity> observers)
 {
  if (!IsOwner(module)) return;
  float now = m_World.GetWorldTime() * 0.001;
  if (now < m_NextOrderMonitor) return;
  m_NextOrderMonitor = now + 0.5;
  // The 0.5 Hz half of the split. A far resident is visited on this tick and
  // skipped on the next three; a near one is visited every tick.
  bool farSweep = now >= m_NextFarSweep;
  if (farSweep) m_NextFarSweep = now + 2;
  // Re-synced from the tracked list on every pass, so the maintained count this
  // sweep hands to TryActivity is the truth as of this tick however the routine
  // side changed it (audit item 18). One null compare per tracked record.
  int activities;
  foreach (EAC_PedestrianActivation activation : m_Tracked)
  {
   if (activation.Activity) activities++;
   if (activation.ExclusionRemoval || activation.SessionRemoval)
   {
    if (activation.Activity && activation.Activity.Monitor(module, now)) { activation.Activity = null; activities--; }
    continue;
   }
   // Urgency split. The skip costs one squared distance per observer; the work
   // it skips is five calls, two of which read native AI state. A skipped
   // resident keeps its IdleSince untouched, so the idle clock loses granularity
   // (two seconds instead of half a second) and nothing else.
   IEntity monitored;
   if (activation.Claim) monitored = activation.Claim.Character;
   if (!farSweep && monitored && !NearObserver(observers, monitored.GetOrigin())) continue;
   EAC_CivilianDanger.Monitor(module, activation, now);
   activation.Emerge.Monitor(module, activation, now);
   if (activation.Activity && activation.Activity.Monitor(module, now))
   {
    activation.Activity = null;
    if (activities > 0) activities--;
   }
   activation.Walking.Monitor(module, activation, now);
   // The honest idle clock. LastBusyAt belongs to the watchdog and is re-stamped
   // on both of RecoverIdle's branches, so it cannot measure a gap longer than
   // IdleRecoverySeconds; this can. One boolean, one bounded GetWaypoints on a
   // group that holds at most one waypoint, and one counter bump, inside the
   // existing 2 Hz loop: no new loop and no world scan.
   // This is also RecoverIdle's population guard, which is why the nudge is now
   // called BELOW it rather than above: RecoverIdle re-arms emergence, which used
   // to flip this resident to "ordered" on the very tick the nudge fired, so
   // IdleSince was zeroed every IdleRecoverySeconds and the IdleForceSeconds
   // deadline could never be reached by an indoor resident at all (audit item 1).
   EAC_ResidentClaim idleClaim = activation.Claim;
   if (!idleClaim || !idleClaim.Committed || idleClaim.Cache || activation.PlayerTouched || idleClaim.Resident.Dead || !idleClaim.Character || !idleClaim.Group)
   {
    // Close the span before dropping it. A span still open when the resident is
    // cached, taken by a player or removed is still a span the town stood
    // through; discarding it silently is why campaign 73 reported idle_max=86
    // while the sampler measured 293.
    if (activation.IdleSince != 0) EAC_RoutineStats.RecordIdleSpan(now - activation.IdleSince);
    activation.IdleSince = 0;
    continue;
   }
   bool ordered = HasOrder(activation, idleClaim, now);
   // One waypoint read per near resident per tick, shared by the idle clock and
   // the recovery nudge below; RecoverIdle used to run a second identical
   // GetWaypoints on the same group in the same iteration (audit item 5).
   RecoverIdle(module, activation, now, ordered);
   if (ordered)
   {
    if (activation.IdleSince != 0) EAC_RoutineStats.RecordIdleSpan(now - activation.IdleSince);
    activation.IdleSince = 0;
   }
   else if (activation.IdleSince == 0) activation.IdleSince = now;
  }
  m_ActivityCount = activities;
 }

 // Does this resident hold an order RIGHT NOW? Emergence counts only while an
 // actual order waypoint stands on the group, which the waypoint read below
 // already reports: EAC_RoutineEmerge adds its move order to this very group, so
 // asking Emerge.IsActive() instead counted a resident as ordered from the moment
 // emergence was armed or re-armed, before any order existed (audit item 1).
 // Does emergence currently own this actor? Only an emergence that has issued an
 // order does: EAC_RoutineEmerge adds its doorstep order to this same group.
 // ServiceOne still gates the ordinary walker on IsActive, so a waypoint standing
 // here is emergence's own or a forced leg issued while emergence was probing;
 // reading the second case as "owned" is conservative and clears itself when the
 // leg completes. During the probe window a routine may start - the probe refuses
 // to issue while the group already holds waypoints, and a routine walks the
 // resident out of the house, which is what emergence wanted anyway.
 protected bool EmergenceHoldsOrder(EAC_PedestrianActivation activation, EAC_ResidentClaim claim)
 {
  if (!activation || !activation.Emerge.IsActive()) return false;
  if (!claim || !claim.Group) return false;
  m_OrderScratch.Clear(); claim.Group.GetWaypoints(m_OrderScratch);
  return !m_OrderScratch.IsEmpty();
 }

 protected bool HasOrder(EAC_PedestrianActivation activation, EAC_ResidentClaim claim, float now)
 {
  if (!activation || !claim || !claim.Group) return false;
  if (activation.Activity || claim.AlarmUntil > now) return true;
  m_OrderScratch.Clear(); claim.Group.GetWaypoints(m_OrderScratch);
  return !m_OrderScratch.IsEmpty();
 }

 // DebugLevel >= 3 only. Read-only accumulators; nothing here is consulted for
 // admission and nothing new is scanned to produce it.
 // Read straight out of the one scheduler ledger that already measures this exact
 // call (STEP_MONITOR wraps MonitorOrders in Step), rather than from a second set
 // of accumulators kept beside it (audit item 14).
 string DescribeMonitor()
 {
  int samples = EAC_SchedulerStats.GetSamples(EAC_SchedulerStats.STEP_MONITOR);
  int average = EAC_SchedulerStats.GetAverage(EAC_SchedulerStats.STEP_MONITOR);
  int peak = EAC_SchedulerStats.GetMax(EAC_SchedulerStats.STEP_MONITOR);
  int tracked = m_Tracked.Count();
  int total = average * samples;
  string result = "monitor_ms total=" + total.ToString();
  result += " peak=" + peak.ToString();
  result += " avg=" + average.ToString();
  result += " samples=" + samples.ToString();
  result += " tracked=" + tracked.ToString();
  return result;
 }

 // A committed resident must never stand still indefinitely. This detects
 // inertness and re-opens the existing fallbacks; it bypasses no placement,
 // exclusion, ownership or budget gate, and issues no order itself.
 // `ordered` is computed by the caller BEFORE this runs, because the re-arm below
 // makes Emerge.IsActive() true without creating any order: computing it after
 // the nudge made every nudged indoor resident look ordered on the same tick.
 protected void RecoverIdle(EAC_AmbientModule module, EAC_PedestrianActivation activation, float now, bool ordered)
 {
  if (module.IdleRecoverySeconds <= 0) return;
  EAC_ResidentClaim claim = activation.Claim;
  if (!claim || !claim.Committed || claim.Cache || activation.PlayerTouched) return;
  if (claim.Resident.Dead || !claim.Character || !claim.Group) return;
  bool busy = ordered;
  if (busy || activation.LastBusyAt == 0) { activation.LastBusyAt = now; return; }
  if (now - activation.LastBusyAt < module.IdleRecoverySeconds) return;
  activation.LastBusyAt = now;
  EAC_RoutineStats.RecordIdleRecovery();
  // Cheapest first: let walking retry now instead of sitting out its backoff.
  activation.Walking.ResetBackoff();
  // A resident that gave up leaving its house is the known permanent case.
  if (activation.Emerge.HasFinished() && claim.Home.BuildingEntity && InsideHouse(m_World, claim.Home.BuildingEntity, claim.Character.GetOrigin(), claim.Character))
   activation.Emerge.Rearm();
  // Allow a routine attempt now. Clearing activation.NextActivity alone was a
  // no-op for the case this watchdog exists for: EAC_CivilianActivity.Start
  // refuses while now < Resident.NextRoutineAt, which FinishRoutine stamps for
  // RoutineGap * RandomFloat(1, 1.5) - 30-45 s at the shipped default and up to
  // 300 s at the clamp - so a resident resting out a routine gap was nudged and
  // then refused, every IdleRecoverySeconds, for as long as the gap lasted.
  // A resident that has held no order at all for IdleRecoverySeconds has already
  // spent its rest standing still, which is exactly what the rest was not for.
  claim.Resident.NextRoutineAt = 0;
  if (activation.NextActivity > now) activation.NextActivity = now;
 }

 // The hard guarantee: nobody stands with no order for longer than
 // IdleForceSeconds. RecoverIdle above re-opens the ordinary fallbacks and then
 // waits for the rotation to reach the resident; this pass issues the movement
 // itself, on the tick the threshold is crossed, bypassing the walk backoff.
 //
 // Bounded like every other sweep here: the scan is one float compare per tracked
 // resident, and only Math.Clamp(tracked/16, 1, 4) residents per tick pay for an
 // actual intervention. m_ForceCursor is the rate limit - a resident that refuses
 // is not revisited until the cursor comes round, about twelve seconds at both 24
 // and 94 tracked - so no per-resident timer is needed and one stuck record can
 // never own the budget.
 protected void ForceIdle(EAC_AmbientModule module, array<IEntity> observers, float now)
 {
  if (!IsOwner(module) || module.IdleForceSeconds <= 0 || m_Tracked.IsEmpty()) return;
  // The same 1 Hz gate ServiceBatch runs at. This pass used to run at the full
  // 2 Hz with four interventions per tick - eight forced starts a second beside
  // a batch that was allowed one - which is not a watchdog, it is a second
  // scheduler running at eight times the rate (audit item 4).
  if (now < m_NextForce) return;
  m_NextForce = now + 1;
  int count = m_Tracked.Count();
  int budget = Math.Clamp(count / 16, 1, 4);
  int forced;
  for (int visited = 0; visited < count && forced < budget; visited++)
  {
   m_ForceCursor = m_ForceCursor % m_Tracked.Count();
   EAC_PedestrianActivation activation = m_Tracked[m_ForceCursor++];
   if (!IsForceCandidate(module, activation, now)) continue;
   forced++;
   // Only the first candidate of the second may spend the shared start token; the
   // rest still get the cheap re-open (cooldown cleared, backoff reset), which is
   // what makes them reachable by the next batch.
   ForceOne(module, activation, observers, now, AllowStart(now));
  }
 }

 // Exactly the population MonitorOrders counts as idle, plus the compartment
 // exclusion: a passenger in a civilian car holds no waypoint of its own and is
 // not standing anywhere.
 protected bool IsForceCandidate(EAC_AmbientModule module, EAC_PedestrianActivation activation, float now)
 {
  if (activation.ExclusionRemoval || activation.SessionRemoval || activation == m_Pending) return false;
  if (activation.IdleSince == 0) return false;
  if (now - activation.IdleSince < module.IdleForceSeconds) return false;
  EAC_ResidentClaim claim = activation.Claim;
  if (!claim || !claim.Committed || claim.Cache || activation.PlayerTouched) return false;
  if (claim.Resident.Dead || !claim.Character || !claim.Group) return false;
  if (claim.AlarmUntil > now) return false;
  if (CompartmentAccessComponent.GetVehicleIn(claim.Character)) return false;
  return true;
 }

 // Issues movement now. It bypasses the routine cooldown and the walk backoff -
 // both are pacing, and a resident that has stood still for IdleForceSeconds has
 // already been paced. It bypasses no placement, exclusion, ownership, budget or
 // visibility gate: TryActivity and EAC_PedestrianWalk.Step run their own ladders
 // unchanged, which is also why a refusal is counted rather than worked around.
 protected void ForceOne(EAC_AmbientModule module, EAC_PedestrianActivation activation, array<IEntity> observers, float now, bool allowStart)
 {
  EAC_ResidentClaim claim = activation.Claim;
  float idleFor = now - activation.IdleSince;
  EAC_SchedulerStats.RecordForceAttempt(idleFor);
  // Deliberately NOT EAC_RoutineStats.RecordIdleRecovery(): that counter means
  // "the 45 s nudge fired" and eight campaigns are recorded against that meaning.
  // The forced intervention has its own counters in EAC_SchedulerStats.
  claim.Resident.NextRoutineAt = 0;
  // Zero is TryActivity's "never armed" sentinel and buys a further ten-second
  // wait, which is the opposite of what this pass is for; a value already in the
  // past is what "no cooldown" actually means here.
  activation.NextActivity = now - 1;
  activation.LastBusyAt = now;
  activation.ForcedAt = now;
  activation.Walking.ResetBackoff();
  // Everything above is free. Everything below creates something, and the town
  // gets one of those per second whichever pass asks for it.
  if (!allowStart) { EAC_SchedulerStats.RecordForceDeferred(); return; }
  // A pending or restoring activation means TryActivity is never consulted, so it
  // would leave last tick's verdict standing.
  m_LastActivityGate = EAC_SchedulerStats.GATE_ACTIVITY_OTHER;
  if (!m_Pending && TryActivity(module, activation, observers))
  {
   ConsumeStart(now);
   EAC_SchedulerStats.RecordForcedStart();
   m_BlockAdmissionUntil = now + 0.5;
   CloseIdleSpan(activation, now);
   return;
  }
  // The routine refused. Force a wander leg instead, past the backoff the walker
  // arms on every refusal, so "move somewhere" does not depend on a routine slot,
  // an anchor or a surveyed scene being available.
  // Same rule as TryActivity: only an emergence that holds an order owns the
  // actor. Armed-but-orderless emergence used to block the forced wander leg too,
  // which is what made the refusal loop permanent rather than merely slow.
  if (EmergenceHoldsOrder(activation, claim)) { EAC_SchedulerStats.RecordForceRefusedAt(EAC_SchedulerStats.GATE_ACTIVITY_OTHER); return; }
  activation.Walking.Step(module, activation);
  m_OrderScratch.Clear(); claim.Group.GetWaypoints(m_OrderScratch);
  if (!m_OrderScratch.IsEmpty())
  {
   ConsumeStart(now);
   EAC_SchedulerStats.RecordForcedWalk();
   CloseIdleSpan(activation, now);
   return;
  }
  // Last resort, and only now. Re-arming emergence makes Emerge.IsActive() true,
  // which TryActivity refuses on and which the wander fallback above returns on,
  // so doing it FIRST - as this method used to - guaranteed that the one pass
  // written to move an indoor-stuck resident moved nobody at all (audit item 2).
  // Reached only once both ordinary interventions have already been refused.
  if (activation.Emerge.HasFinished() && claim.Home.BuildingEntity && InsideHouse(m_World, claim.Home.BuildingEntity, claim.Character.GetOrigin(), claim.Character))
  {
   activation.Emerge.Rearm();
   EAC_SchedulerStats.RecordForcedRearm();
  }
  // Exactly one bucket per refused intervention, so the four always sum to
  // `refused`. An upstream cause wins over "the wander also failed": a town whose
  // routine slots are all occupied, or whose starts keep refusing, is a different
  // defect from one whose walker cannot find a destination, and city benchmark 06
  // could not tell those apart from a single total of 675.
  int refusedGate = EAC_SchedulerStats.GATE_WALK;
  if (m_LastActivityGate == EAC_SchedulerStats.GATE_ACTIVITY_LIMIT) refusedGate = EAC_SchedulerStats.GATE_ACTIVITY_LIMIT;
  else if (m_LastActivityGate == EAC_SchedulerStats.GATE_START) refusedGate = EAC_SchedulerStats.GATE_START;
  EAC_SchedulerStats.RecordForceRefusedAt(refusedGate);
 }

 // The resident now holds an order, so close its no-order span here rather than
 // leaving it open for up to two seconds until the 0.5 Hz half of the order sweep
 // notices. A stale IdleSince is a second forced intervention on a resident that
 // is already moving. Identical to what MonitorOrders does on its `ordered`
 // branch, including the span record, so the measurement is unchanged.
 protected void CloseIdleSpan(EAC_PedestrianActivation activation, float now)
 {
  if (activation.IdleSince == 0) return;
  EAC_RoutineStats.RecordIdleSpan(now - activation.IdleSince);
  activation.IdleSince = 0;
 }

 void Step(EAC_AmbientModule module, array<IEntity> observers)
 {
  if (!IsOwner(module)) return;
  m_RemovalSpent = false;
  bool cacheWork = ClearExcluded(module);
  int monitorTick = System.GetTickCount();
  MonitorOrders(module, observers);
  EAC_SchedulerStats.Record(EAC_SchedulerStats.STEP_MONITOR, monitorTick);
  float cacheNow = m_World.GetWorldTime() * 0.001;
  // The idle watchdog reads the clock MonitorOrders just refreshed and issues
  // movement for anyone past IdleForceSeconds. Measured apart from the ordinary
  // batch (audit item 15): the two do comparable work on different populations,
  // and one figure covering both cannot say which of them grew.
  int forceTick = System.GetTickCount();
  ForceIdle(module, observers, cacheNow);
  EAC_SchedulerStats.Record(EAC_SchedulerStats.STEP_FORCE, forceTick);
  int serviceTick = System.GetTickCount();
  ServiceBatch(module, observers, cacheNow);
  EAC_SchedulerStats.Record(EAC_SchedulerStats.STEP_SERVICE, serviceTick);
  int maintainTick = System.GetTickCount();
  bool cacheTick = cacheNow >= m_NextCacheStep;
  if (cacheTick)
  {
   m_NextCacheStep = cacheNow + 0.5;
   foreach (EAC_PedestrianActivation active : m_Tracked)
   {
    // One distance sweep per tracked resident per tick, shared by the cache
    // sampler and the routine's own distance stop; it used to be computed twice,
    // each one a distance per observer (audit item 17).
    bool distant;
    if (active.Claim) distant = EAC_PedestrianCaching.Distant(module, active.Claim.Character, observers);
    EAC_PedestrianCaching.Sample(module, active, observers, cacheNow, distant);
    if (active.Activity && distant) active.Activity.RequestStop();
   }
  }
  if (MaintainNext(module, observers, cacheTick && !cacheWork)) cacheWork = true;
  if (cacheWork) m_BlockAdmissionUntil = cacheNow + 0.5;
  EAC_SchedulerStats.Record(EAC_SchedulerStats.STEP_MAINTAIN, maintainTick);
  int voiceTick = System.GetTickCount();
  EAC_CivilianVoice.Step(module, m_Tracked, observers, cacheNow);
  EAC_SchedulerStats.Record(EAC_SchedulerStats.STEP_VOICE, voiceTick);
  if (!m_Pending) return;
  int pendingTick = System.GetTickCount();
  StepPending(module, observers);
  EAC_SchedulerStats.Record(EAC_SchedulerStats.STEP_PENDING, pendingTick);
 }

 // Placement/exclusion/control changes are expected cancellations. Only native
 // infrastructure failures/deadlines warrant a quiet-server warning.
 static bool IsActivationError(string reason)
 {
  if (reason == "group prefab spawn failed" || reason == "group ownership registration failed" || reason == "group settings component missing") return true;
  if (reason == "civilian group settings rejected" || reason == "activation deadline" || reason == "missing navmesh component") return true;
  if (reason == "qualified character prefab spawn failed" || reason == "character ownership registration failed") return true;
  return reason == "missing lifecycle component" || reason == "character affiliation or AI control missing";
 }

 // The pending activation ladder is measured separately and keeps Step's local
 // count below the native 64-local limit.
 protected void StepPending(EAC_AmbientModule module, array<IEntity> observers)
 {
  if (m_Pending.SessionRemoval) return;
  float now = m_World.GetWorldTime() * 0.001;
  m_Pending.Observe();
  if (now > m_Pending.Deadline) m_Pending.Fail("activation deadline");
  if (m_Pending.Aborting)
  {
   m_Diagnostics.Record(EAC_ESpawnReason.ACTIVATION_FAILED, m_Pending.FailureReason);
   if (IsActivationError(m_Pending.FailureReason) && m_Diagnostics.TakeFailureLog("activation: " + m_Pending.FailureReason))
    Print("[EAC activation] " + m_Pending.FailureReason, LogLevel.WARNING);
   m_Diagnostics.RememberRejectedPosition(m_Pending.Position);
   m_Pending = null; return;
  }
  float pendingMinimum = 50;
  if (m_Pending.SpawnedIndoors)
  {
   if (!IndoorPlacementAllowed(module, m_Pending.Claim.Home, observers)) { m_Pending.Fail("indoor wake range or observer safety changed"); return; }
   pendingMinimum = INDOOR_MINIMUM_DISTANCE;
  }
  if (!m_Pending.Claim.Group || !m_Pending.Claim.Resident.Wanted || m_Pending.Claim.Resident.Dead) { m_Pending.Fail("eligibility or placement changed"); return; }
  if (!ValidPosition(module, m_Pending.Claim.Home, m_Pending.Position, observers, m_Pending.Claim.Character, pendingMinimum, m_Pending.SpawnedIndoors)) { NotePendingGeometry(); m_Pending.Fail("eligibility or placement changed"); return; }
  if (!m_Pending.Claim.Character)
  {
   AIPathfindingComponent path = AIPathfindingComponent.Cast(m_Pending.Claim.Group.FindComponent(AIPathfindingComponent));
   if (!path || !path.GetNavmeshComponent()) { m_Diagnostics.Record(EAC_ESpawnReason.NAVMESH_REJECTED); m_Pending.Fail("missing navmesh component"); return; }
   NavmeshWorldComponent mesh = path.GetNavmeshComponent();
   if (!mesh.IsTileLoaded(m_Pending.Position)) { m_Diagnostics.Record(EAC_ESpawnReason.NAVMESH_PENDING); if (!mesh.IsTileRequested(m_Pending.Position)) mesh.LoadTileIn(m_Pending.Position); return; }
   vector projected;
   if (!mesh.IsTileValid(m_Pending.Position) || !path.GetClosestPositionOnNavmesh(m_Pending.Position, "2 2 2", projected)) { m_Diagnostics.Record(EAC_ESpawnReason.NAVMESH_REJECTED); NotePendingNavmesh(); m_Pending.Fail("navmesh projection failed"); return; }
   if (vector.Distance(projected, m_Pending.Position) > 2 || !mesh.IsTileLoaded(projected) || !mesh.IsTileValid(projected)) { m_Diagnostics.Record(EAC_ESpawnReason.NAVMESH_REJECTED); NotePendingNavmesh(); m_Pending.Fail("projected navmesh tile rejected"); return; }
   if (!ValidPosition(module, m_Pending.Claim.Home, projected, observers, null, pendingMinimum, m_Pending.SpawnedIndoors)) { NotePendingGeometry(); m_Pending.Fail("projected position: " + EAC_Diagnostics.ReasonName(m_Diagnostics.GetLastReason())); return; }
   // Navmesh projection can leave the building; an indoor placement that is no
   // longer indoors loses the only thing that was hiding it.
   if (m_Pending.SpawnedIndoors && !InsideHouse(m_World, m_Pending.Claim.Home.BuildingEntity, projected, m_Pending.Claim.Group)) { NotePendingNavmesh(); m_Pending.Fail("projected indoor position left the house"); return; }
   m_Pending.Position = projected;
   IEntity actor = Spawn(QualifiedResource(m_Pending.Claim.Resident.CharacterPrefab), projected);
   if (!actor) { m_Pending.Fail("qualified character prefab spawn failed"); return; }
   // Same as the group above: untracked, so it cannot wait for another tick.
   if (!module.TrackResidentCharacter(m_Pending.Claim, actor)) { EAC_SchedulerStats.RecordCreationRollback(); SCR_EntityHelper.DeleteEntityAndChildren(actor); m_Pending.Fail("character ownership registration failed"); return; }
   // Out of GM saves and vanilla persistence from the first frame it is owned.
   EAC_SessionLifecycle.Keep(actor);
   m_Pending.SettleUntil = now + 1;
   if (!m_Pending.Bind()) { m_Pending.Fail("missing lifecycle component"); return; }
   if (m_Pending.Aborting) return;
   FactionAffiliationComponent affiliation = FactionAffiliationComponent.Cast(actor.FindComponent(FactionAffiliationComponent));
   AIControlComponent control = AIControlComponent.Cast(actor.FindComponent(AIControlComponent));
   if (!affiliation || !control || !control.GetAIAgent()) { m_Pending.Fail("character affiliation or AI control missing"); return; }
   affiliation.SetAffiliatedFactionByKey(EAC_AmbientModule.CIV_FACTION);
   m_Pending.Claim.Group.AddAgent(control.GetAIAgent());
   return;
  }
  if (now < m_Pending.SettleUntil) return;
  if (!IsCivilian(m_Pending.Claim.Character, m_Pending.Claim.Group)) { m_Diagnostics.Record(EAC_ESpawnReason.LIVE_QUALIFICATION); m_Pending.Fail("live civilian control, faction or unarmed qualification failed"); return; }
  if (!ValidPosition(module, m_Pending.Claim.Home, m_Pending.Claim.Character.GetOrigin(), observers, m_Pending.Claim.Character, pendingMinimum, m_Pending.SpawnedIndoors)) { NotePendingGeometry(); m_Pending.Fail("settled position: " + EAC_Diagnostics.ReasonName(m_Diagnostics.GetLastReason())); return; }
  if (!module.CommitResidentActivation(m_Pending.Claim)) { m_Pending.Fail("activation commit ownership rejected"); return; }
  // A civilian may have no valid first walking leg or routine slot yet. Start
  // its native thinking at admission so threat evaluation does not depend on
  // eventually receiving an order (a nearby idle actor otherwise stays inert).
  EAC_ResidentClaim committed = m_Pending.Claim;
  committed.Resident.IndoorFailures = 0;
  if (m_Pending.SpawnedIndoors)
  {
   m_Pending.Emerge.Require();
   EAC_RoutineStats.RecordIndoorLive();
  }
  if (CanStartPendingAI(module, committed))
  {
   committed.Group.ActivateAI();
   if (CanStartPendingAI(module, committed))
   {
    AIControlComponent committedControl = AIControlComponent.Cast(committed.Character.FindComponent(AIControlComponent));
    if (committedControl) committedControl.ActivateAI();
   }
  }
  m_Diagnostics.Record(EAC_ESpawnReason.ACTIVATED);
  m_Pending = null;
 }

 // An indoor pending placement refused by the body box (the reason Reject just
 // recorded) counts toward that slot's outdoor fallback. Observer reasons -
 // visible, too near, out of range - do not: they say nothing about the house.
 protected void NotePendingGeometry()
 {
  if (!m_Pending || !m_Pending.SpawnedIndoors || !m_Pending.Claim) return;
  if (m_Diagnostics.GetLastReason() == EAC_ESpawnReason.GEOMETRY) NoteIndoorFailure(m_Pending.Claim.Resident);
 }

 // No usable interior navmesh under an indoor placement.
 protected void NotePendingNavmesh()
 {
  if (m_Pending && m_Pending.SpawnedIndoors && m_Pending.Claim) NoteIndoorFailure(m_Pending.Claim.Resident);
 }

 protected bool CanStartPendingAI(EAC_AmbientModule module, EAC_ResidentClaim claim)
 {
  if (!IsOwner(module) || !m_Pending || m_Pending.Claim != claim || m_Pending.PlayerTouched || !claim || !claim.Committed || !claim.OptimizerMember || claim.OptimizerMember.WasPlayer || !HasCivilianControl(claim.Character, claim.Group)) return false;
  CharacterControllerComponent controller = CharacterControllerComponent.Cast(claim.Character.FindComponent(CharacterControllerComponent));
  if (controller.IsPlayerControlled() || SCR_PossessingManagerComponent.GetPlayerIdFromControlledEntity(claim.Character) != 0) return false;
  RplComponent actorRpl = RplComponent.Cast(claim.Character.FindComponent(RplComponent));
  RplComponent groupRpl = RplComponent.Cast(claim.Group.FindComponent(RplComponent));
  return actorRpl && groupRpl && !actorRpl.IsProxy() && actorRpl.IsOwner() && !groupRpl.IsProxy() && groupRpl.IsOwner();
 }

 void SelectNext(EAC_AmbientModule module, array<IEntity> observers)
 {
  if (!IsOwner(module)) { m_Diagnostics.Record(EAC_ESpawnReason.BAD_OWNER); return; }
  if (!module.CanAdmitNewWork()) { m_Diagnostics.Record(EAC_ESpawnReason.LOAD_PAUSED); return; }
  if (m_Pending) { m_Diagnostics.Record(EAC_ESpawnReason.PENDING); return; }
  if (m_World.GetWorldTime() * 0.001 < m_BlockAdmissionUntil) { m_Diagnostics.Record(EAC_ESpawnReason.CACHE_RECOVERY); return; }
  int observerReason = GetObserverReason(m_World, observers);
  if (observerReason != EAC_ESpawnReason.NONE) { m_Diagnostics.Record(observerReason); return; }
  if (module.GetReservedPopulation() >= module.PopulationLimit) { m_Diagnostics.Record(EAC_ESpawnReason.BUDGET); return; }
  if (!HasEngineAiHeadroom()) { m_Diagnostics.Record(EAC_ESpawnReason.AI_LIMIT); return; }
  EAC_HouseholdRegistry registry = module.GetHomeIndex().GetRegistry();
  if (registry.GetHomeCount() == 0) { m_Diagnostics.Record(EAC_ESpawnReason.NO_HOMES); return; }
  // Retained old/distant/full households must not delay a newly approached town
  // by one scheduler tick per slot. Only cheap eligibility advances are batched;
  // a physical candidate consumes one unit of the admission budget below.
  int admissions;
  int budget = Math.Clamp(module.AdmissionsPerTick, 1, 4);
  m_CatchingUp = false;
  for (int selection = 0; selection < 32; selection++)
  {
   m_HomeCursor = m_HomeCursor % registry.GetHomeCount();
   EAC_HouseholdRecord home = registry.GetHome(m_HomeCursor);
   if (!home || !home.BuildingEntity) { m_SlotCursor = 0; m_HomeCursor++; m_Diagnostics.Record(EAC_ESpawnReason.BAD_HOME); continue; }
   if (m_SlotCursor >= home.Residents.Count()) { m_SlotCursor = 0; m_HomeCursor++; m_Diagnostics.Record(EAC_ESpawnReason.SLOT_ADVANCED); continue; }
   EAC_ResidentRecord resident = home.Residents[m_SlotCursor++];
   if (!IsRelevant(module, home.BuildingEntity.GetOrigin(), observers))
   { m_SlotCursor = 0; m_HomeCursor++; m_Diagnostics.Record(EAC_ESpawnReason.OUTSIDE_DISTANCE); continue; }
   int reason = module.GetAdmissionReason(home, resident, home.BuildingEntity.GetOrigin());
   // A ruin refuses every slot; step past the whole household at once.
   if (reason == EAC_ESpawnReason.RUINED_HOME) { m_Diagnostics.Record(reason); m_SlotCursor = 0; m_HomeCursor++; continue; }
   if (reason != EAC_ESpawnReason.NONE) { m_Diagnostics.Record(reason); continue; }
   // One count per candidate, shared by the ceiling and the floor.
   vector neighbourhood = home.BuildingEntity.GetOrigin();
   int localUsed = CountLocalOccupants(module, neighbourhood);
   m_LastLocalCount = localUsed;
   if (!HasLocalCapacityFor(module, localUsed, 1))
   {
    // S4. A saturated neighbourhood used to end the whole scheduler tick, so one
    // full town square could starve every other street in the city. Record the
    // reason, step past this household and keep scanning - exactly what the
    // sibling rejections above already do.
    m_Diagnostics.Record(EAC_ESpawnReason.LOCAL_BUDGET);
    m_SlotCursor = 0; m_HomeCursor++;
    continue;
   }
   // The floor only raises the attempt budget for this tick. It relaxes no gate:
   // CanAdmitNewWork, PopulationLimit, ValidPosition and GetHiddenReason all ran
   // above or run inside Begin, unchanged.
   if (IsBelowLocalFloor(module, localUsed))
   {
    m_CatchingUp = true;
    int catchUpBudget = Math.Clamp(module.CatchUpAdmissionsPerTick, 1, 8);
    if (catchUpBudget > budget) budget = catchUpBudget;
   }
   // An indoor-designated slot waits for its own smaller radius. A slot whose
   // indoor placements keep failing on geometry is handed to the outdoor branch
   // below by PrefersIndoor, under the outdoor wake distance, the 50 m minimum
   // and the line-of-sight test; it never appears near a player indoors-style.
   vector candidate; bool indoors;
   if (PrefersIndoor(module, resident))
   {
    if (!IndoorPlacementAllowed(module, home, observers)) { m_Diagnostics.Record(EAC_ESpawnReason.OUTSIDE_DISTANCE); continue; }
    int interior = (m_IndoorProbe++ + resident.Id) % 9;
    indoors = IndoorCandidate(m_World, home.BuildingEntity, interior, candidate);
    EAC_RoutineStats.RecordIndoorSpawn(indoors);
    if (!indoors) { NoteIndoorFailure(resident); m_Diagnostics.Record(EAC_ESpawnReason.GEOMETRY); admissions++; if (admissions >= budget) return; continue; }
   }
   if (!indoors)
   {
    float angle = Math.RandomFloat(0, Math.PI2); float radius = Math.RandomFloat(8, 25);
    candidate = home.BuildingEntity.GetOrigin() + Vector(Math.Cos(angle) * radius, 0, Math.Sin(angle) * radius);
    candidate[1] = m_World.GetSurfaceY(candidate[0], candidate[2]) + 0.1;
   }
   bool begun = Begin(module, home, resident, candidate, observers, indoors);
   if (indoors && !begun && m_Diagnostics.GetLastReason() == EAC_ESpawnReason.GEOMETRY) NoteIndoorFailure(resident);
   admissions++;
   // The pending single-slot ladder and its ten-second deadline are untouched:
   // once an activation is in flight nothing else can start this tick whatever
   // the budget says. A budget above one therefore buys a retry of a refused
   // placement inside the same tick, never a second concurrent activation.
   if (m_Pending || admissions >= budget) return;
  }
 }

 // The one count of a neighbourhood, read once and shared by the ceiling
 // (HasLocalCapacity) and the floor (IsBelowLocalFloor) so the two can never
 // disagree about how many people are already there. At most 200 retained active
 // records; the cached count is O(1) and this is never a world scan.
 int CountLocalOccupants(EAC_AmbientModule module, vector position)
 {
  if (!module) return 0;
  int used;
  EAC_TrafficDirector traffic = EAC_TrafficDirector.Get();
  if (traffic) used = traffic.CountNearbyOccupants(position, module.LocalPopulationRadius);
  float radiusSquared = module.LocalPopulationRadius * module.LocalPopulationRadius;
  foreach (EAC_PedestrianActivation activation : m_Tracked)
  {
   if (!activation.Claim || activation.Claim.Resident.Dead) continue;
   vector existing = activation.Position;
   if (activation.Claim.Character) existing = activation.Claim.Character.GetOrigin();
   vector delta = position - existing; delta[1] = 0;
   if (delta.LengthSq() <= radiusSquared) used++;
  }
  return used;
 }

 bool HasLocalCapacity(EAC_AmbientModule module, vector position, int needed = 1)
 {
  if (!module || needed < 1) return false;
  int used = CountLocalOccupants(module, position);
  return HasLocalCapacityFor(module, used, needed);
 }

 // The same ceiling expressed over a count the caller already has, so the
 // scheduler never counts one neighbourhood twice in a single decision.
 static bool HasLocalCapacityFor(EAC_AmbientModule module, int used, int needed)
 {
  if (!module || needed < 1) return false;
  return used + needed <= module.LocalPopulationLimit;
 }

 // Purely a rate signal. It grants nothing: every placement, exclusion, budget,
 // load and visibility gate still runs unchanged, and MinLocalPopulation is
 // clamped to LocalPopulationLimit so the floor can never outrank the ceiling.
 static bool IsBelowLocalFloor(EAC_AmbientModule module, int used)
 {
  if (!module || module.MinLocalPopulation <= 0) return false;
  return used < module.MinLocalPopulation;
 }

 // Engine per-faction AI headroom, read-only. SetLimitOfActiveAIs is a
 // scenario-owned global and is never called here. The cast is null-guarded on
 // every path, exactly as the four existing ChimeraAIWorld call sites are, and a
 // base game that configures no CIV entry answers true, so the gate never fires
 // rather than refusing everything.
 static bool HasEngineAiHeadroom()
 {
  if (!GetGame()) return true;
  ChimeraAIWorld ai = ChimeraAIWorld.Cast(GetGame().GetAIWorld());
  if (!ai) return true;
  return ai.CanLimitedAIBeAddedForFaction(EAC_AmbientModule.CIV_FACTION);
 }

 // Diagnostics only: the neighbourhood count the scheduler last read for a real
 // candidate, and whether that candidate was under the floor.
 int GetLastLocalCount() { return m_LastLocalCount; }
 bool IsCatchingUp() { return m_CatchingUp; }

 // The survey needs a navmesh-capable pathfinding component to run the shared
 // acceptance ladder. Borrow one from a live civilian group rather than creating
 // anything. The honest consequence is that nothing is verified while no civilian
 // exists, which is self-correcting: it ties survey effort to populated areas.
 AIPathfindingComponent BorrowPathfinding()
 {
  foreach (EAC_PedestrianActivation activation : m_Tracked)
  {
   EAC_ResidentClaim claim = activation.Claim;
   if (!claim || !claim.Committed || claim.Cache || !claim.Group) continue;
   AIPathfindingComponent path = AIPathfindingComponent.Cast(claim.Group.FindComponent(AIPathfindingComponent));
   if (path && path.GetNavmeshComponent()) return path;
  }
  return null;
 }

 string BuildDebugStats()
 {
  int active, pending, recovery, dead, sheltering, emerging, ducking;
  foreach (EAC_PedestrianActivation activation : m_Tracked)
  {
   if (!activation.Claim.Committed) pending++;
   else if (activation.Claim.Cache) recovery++;
   else if (activation.Claim.Resident.Dead) dead++;
   else if (activation.Claim.Character) active++;
   string shelterState = activation.Shelter.GetState();
   if (shelterState != "calm") sheltering++;
   if (shelterState == "ducking") ducking++;
   if (activation.Emerge.IsActive()) emerging++;
  }
  string stats = "active=" + active.ToString() + " pending=" + pending.ToString();
  stats += " recovery=" + recovery.ToString() + " dead=" + dead.ToString() + " activities=" + GetActivityCount().ToString();
  stats += " sheltering=" + sheltering.ToString() + " ducking=" + ducking.ToString() + " emerging=" + emerging.ToString();
  EAC_ResidentClaims claims = EAC_AmbientModule.GetMissionClaims();
  if (claims)
  {
   stats += " cache_completed=" + claims.GetCacheCompleted().ToString();
   stats += " exclusion_completed=" + claims.GetExclusionCompleted().ToString();
   stats += " session_completed=" + claims.GetSessionCompleted().ToString();
  }
  return stats;
 }

 void AppendDebugDrawData(array<vector> positions, array<int> kinds)
 {
  m_Diagnostics.AppendRejectedPoint(positions, kinds);
  if (m_Pending) EAC_Diagnostics.AddDrawPoint(positions, kinds, m_Pending.Position, EAC_Diagnostics.PENDING);
  int shown;
  foreach (EAC_PedestrianActivation activation : m_Tracked)
  {
   if (shown >= 24) break;
   if (activation == m_Pending) continue;
   if (activation.Claim.Cache) continue;
   vector position = activation.Position;
   if (activation.Claim.Character) position = activation.Claim.Character.GetOrigin();
   int kind = EAC_Diagnostics.ACTIVE;
   if (!activation.Claim.Committed) kind = EAC_Diagnostics.PENDING;
   EAC_Diagnostics.AddDrawPoint(positions, kinds, position, kind); shown++;
  }
 }

 void EAC_RetireSessionPopulation()
 {
  // Keep claims, reservations and protected survivors in the mission ledger.
  // ClearExcluded advances this distinct retirement flag at its normal pace.
  foreach (EAC_PedestrianActivation activation : m_Tracked)
  {
   activation.SessionRemoval = true;
   if (activation.Claim && (activation.PlayerTouched || activation.Claim.EAC_SessionTransferred()))
   {
    activation.PlayerTouched = true;
    activation.Claim.Resident.Removed = true;
   }
  }
  // Retired activations remain tracked, but cannot block fresh admission.
  m_Pending = null;
 }

 bool EAC_ContainsSessionHelper(SCR_EditableEntityComponent candidate, EAC_ResidentClaim ownerClaim = null)
 {
  foreach (EAC_PedestrianActivation activation : m_Tracked)
  {
   if (ownerClaim && activation.Claim != ownerClaim) continue;
   if (activation.Activity && activation.Activity.EAC_ContainsSessionHelper(candidate)) return true;
   if (activation.Walking.EAC_ContainsSessionHelper(candidate) || activation.Shelter.EAC_ContainsSessionHelper(candidate) || activation.Emerge.EAC_ContainsSessionHelper(candidate)) return true;
  }
  return false;
 }

 // Every helper EAC_ContainsSessionHelper answers for, for EAC_SessionLifecycle.Sync.
 void EAC_KeepSessionHelpers()
 {
  foreach (EAC_PedestrianActivation activation : m_Tracked)
  {
   if (activation.Activity) activation.Activity.EAC_KeepSessionHelpers();
   activation.Walking.EAC_KeepSessionHelpers();
   activation.Shelter.EAC_KeepSessionHelpers();
   activation.Emerge.EAC_KeepSessionHelpers();
  }
 }
}

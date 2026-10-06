// TEST ONLY. EXPBG Ambient Civilians (addon/ambient-civilians) native server fixture:
// indoor floor placement, ruined homes and native-save exclusion.
// pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/EAC_AmbientGameplay.c -TimeoutSeconds 300 -OrchestratorSlotGranted -ExpectResult '\[EXPG EAC RESULT\] checks=[1-9]\d* failures=0 houses=[1-9]\d* indoorHouses=[1-9]\d* indoorPlaced=1 reason=complete'
// The runner copies this file to EXPG_GarrisonGameplay.c, so the driver class name is fixed.
// The runner's own verdict checks Garrison evidence; accept on exactly one
// "[EXPG EAC RESULT] ... failures=0 ... reason=complete" line, no script errors and
// "Game destroyed" in the retained logs.
// Real GM_Eden houses in Morton, the real spawner placement statics (IndoorCandidate,
// GetClearReason, InsideHouse), the real group navmesh projection, native Kill on a
// real building, real EAC_SessionLifecycle and the real 1.8 before-save event and
// IsTracked (no save is written or loaded). No players and no scheduler: the
// admission ladder, emergence and the GM view need the separate client test.
modded class EAC_SessionLifecycle
{
 // One Sync now, whatever the 1 s throttle says. No module runs here, so no
 // ledger answers for the fixture's entities and Sync releases them.
 static void EAC_FixtureSync(float now) { s_NextSync = 0; Sync(now); }
}
class EXPG_GarrisonGameplayClass : GenericEntityClass {}
class EXPG_GarrisonGameplay : GenericEntity
{
 static const float FIXTURE_SECONDS = 240;
 static const float INDOOR_DEADLINE = 60;
 static const ResourceName GROUP_PREFAB = "{000CD338713F2B5A}Prefabs/AI/Groups/Group_Base.et";
 static const ResourceName MODULE_PREFAB = "{CA1A000000000010}PrefabsEditable/EXPAC/EAC_AmbientModule.et";
 // Morton, Everon: the town of the 0.1.5 leftovers and ruins.
 static const vector TOWN = "5080 0 4000";
 int m_Phase;
 int m_Checks;
 int m_Failures;
 float m_Started;
 float m_PhaseAt;
 float m_SurveyAt;
 bool m_Finished;
 bool m_HandbackPending;
 ref array<IEntity> m_Found = {};
 ref array<IEntity> m_SpotHouses = {};
 ref array<vector> m_Spots = {};
 int m_Houses;
 int m_IndoorHouses;
 int m_OldHouses;
 int m_IndoorPlaced;
 int m_Try;
 float m_IndoorSeconds;
 IEntity m_House;
 vector m_Projected;
 SCR_AIGroup m_Group;
 IEntity m_Resident;
 IEntity m_Foreign;
 IEntity m_Ruin;

 void EXPG_GarrisonGameplay(IEntitySource src, IEntity parent) { SetEventMask(EntityEvent.INIT | EntityEvent.FRAME); }
 float Now() { return GetGame().GetWorld().GetWorldTime() * 0.001; }

 override void EOnInit(IEntity owner)
 {
  if (!Replication.IsServer()) { ClearEventMask(EntityEvent.FRAME); return; }
  m_Started = Now();
  m_PhaseAt = m_Started + 5;
  PrintFormat("[EXPG EAC BEGIN] deadline=%1 town=%2", FIXTURE_SECONDS, TOWN);
 }

 bool Check(bool ok, string label)
 {
  m_Checks++;
  if (!ok) m_Failures++;
  PrintFormat("[EXPG EAC CHECK] pass=%1 %2", ok, label);
  return ok;
 }

 void Finish(string reason)
 {
  if (m_Finished) return;
  m_Finished = true;
  ClearEventMask(EntityEvent.FRAME);
  PrintFormat("[EXPG EAC RESULT] checks=%1 failures=%2 houses=%3 indoorHouses=%4 indoorPlaced=%5 reason=%6", m_Checks, m_Failures, m_Houses, m_IndoorHouses, m_IndoorPlaced, reason);
  GetGame().RequestClose();
 }

 void Advance(int phase, float delay = 0)
 {
  m_Phase = phase;
  m_PhaseAt = Now() + delay;
 }

 IEntity SpawnAt(ResourceName prefab, vector position)
 {
  // Keep the Resource and the spawned entity in locals.
  Resource resource = Resource.Load(prefab);
  if (!resource || !resource.IsValid()) return null;
  EntitySpawnParams params = new EntitySpawnParams();
  params.TransformMode = ETransformMode.WORLD;
  Math3D.MatrixIdentity4(params.Transform);
  params.Transform[3] = position;
  IEntity spawned = GetGame().SpawnEntityPrefab(resource, GetGame().GetWorld(), params);
  return spawned;
 }

 bool AddHouse(IEntity entity)
 {
  if (!entity || m_Found.Count() >= 16 || m_Found.Contains(entity)) return true;
  if (EAC_HomeIndex.ResolveEntityHouseClass(entity) == 0) return true;
  if (EAC_HomeIndex.IsRuined(entity)) return true;
  m_Found.Insert(entity);
  return true;
 }

 // The pre-fix candidate: the same footprint sample at terrain + 0.1 m.
 bool OldCandidate(BaseWorld world, IEntity house, int index, out vector candidate)
 {
  vector mins, maxs, transform[4];
  house.GetBounds(mins, maxs); house.GetWorldTransform(transform);
  vector local = (mins + maxs) * 0.5;
  int column = index % 3;
  local[0] = mins[0] + (maxs[0] - mins[0]) * (0.25 + column * 0.25);
  local[2] = mins[2] + (maxs[2] - mins[2]) * (0.25 + Math.Floor(index / 3.0) * 0.25);
  local[1] = mins[1] + 1;
  candidate = local.Multiply4(transform);
  candidate[1] = world.GetSurfaceY(candidate[0], candidate[2]) + 0.1;
  return EAC_PedestrianSpawner.InsideHouse(world, house, candidate, null);
 }

 override void EOnFrame(IEntity owner, float timeSlice)
 {
  if (m_Finished) return;
  float now = Now();
  if (now - m_Started > FIXTURE_SECONDS) { Check(false, string.Format("phase %1 finished before the fixture deadline", m_Phase)); Finish("timeout"); return; }
  if (now < m_PhaseAt) return;
  if (m_Phase == 0) { Survey(now); return; }
  if (m_Phase == 1) { Project(now); return; }
  if (m_Phase == 2) { Settled(now); return; }
  if (m_Phase == 3) { Ruin(); return; }
  if (m_Phase == 4) { Ruined(); return; }
  if (m_Phase == 5) { SaveContract(now); return; }
  if (m_Phase == 6) { ModuleContract(); return; }
 }

 // Every supported intact house in Morton, all nine footprint samples, old and new.
 void Survey(float now)
 {
  BaseWorld world = GetGame().GetWorld();
  vector mins = Vector(TOWN[0] - 200, -100, TOWN[2] - 200);
  vector maxs = Vector(TOWN[0] + 200, 1000, TOWN[2] + 200);
  world.QueryEntitiesByAABB(mins, maxs, AddHouse, null, EQueryEntitiesFlags.ALL);
  m_Houses = m_Found.Count();
  int oldClear, newClear;
  foreach (IEntity house : m_Found)
  {
   // Enforce does not re-initialise an uninitialised loop local per iteration.
   bool oldAny = false;
   bool newAny = false;
   for (int index = 0; index < 9; index++)
   {
    vector previous;
    if (OldCandidate(world, house, index, previous) && EAC_PedestrianSpawner.GetClearReason(world, previous) == EAC_ESpawnReason.NONE) { oldAny = true; oldClear++; }
    vector candidate;
    if (!EAC_PedestrianSpawner.IndoorCandidate(world, house, index, candidate)) continue;
    if (EAC_PedestrianSpawner.GetClearReason(world, candidate, null, false, true) != EAC_ESpawnReason.NONE) continue;
    newClear++;
    if (!newAny) { m_SpotHouses.Insert(house); m_Spots.Insert(candidate); }
    newAny = true;
   }
   if (oldAny) m_OldHouses++;
   if (newAny) m_IndoorHouses++;
  }
  PrintFormat("[EXPG EAC INDOOR SURVEY] houses=%1 old_houses=%2 old_points=%3 new_houses=%4 new_points=%5", m_Houses, m_OldHouses, oldClear, m_IndoorHouses, newClear);
  if (!Check(m_Houses >= 3, string.Format("intact vanilla houses found in Morton (%1)", m_Houses))) { Finish("survey"); return; }
  Check(m_IndoorHouses * 2 >= m_Houses, string.Format("at least half the houses have a clear floor-level indoor point (%1/%2; terrain + 0.1 m points: %3)", m_IndoorHouses, m_Houses, m_OldHouses));
  Check(newClear >= oldClear, string.Format("floor placement clears no fewer points than the old terrain placement (%1 vs %2)", newClear, oldClear));
  if (m_Spots.IsEmpty()) { Finish("survey"); return; }
  m_SurveyAt = now;
  m_Try = 0;
  Advance(1);
 }

 // The pending ladder's own projection: group navmesh, 2 m reach, still inside,
 // still clear at the indoor tolerance. Next candidate house on a refusal.
 void Project(float now)
 {
  if (now - m_SurveyAt > INDOOR_DEADLINE) { Check(false, "an indoor point projected onto interior navmesh before the deadline"); Finish("navmesh"); return; }
  if (m_Try >= m_Spots.Count()) { Check(false, string.Format("an indoor point projected onto interior navmesh (%1 houses tried)", m_Spots.Count())); Finish("navmesh"); return; }
  BaseWorld world = GetGame().GetWorld();
  vector spot = m_Spots[m_Try];
  if (!m_Group)
  {
   m_Group = SCR_AIGroup.Cast(SpawnAt(GROUP_PREFAB, spot));
   if (!Check(m_Group != null, "Group_Base spawned for the navmesh projection")) { Finish("group"); return; }
   m_Group.SetDeleteWhenEmpty(false);
  }
  AIPathfindingComponent path = AIPathfindingComponent.Cast(m_Group.FindComponent(AIPathfindingComponent));
  if (!path || !path.GetNavmeshComponent()) { Check(false, "group navmesh component present"); Finish("navmesh"); return; }
  NavmeshWorldComponent mesh = path.GetNavmeshComponent();
  if (!mesh.IsTileLoaded(spot)) { if (!mesh.IsTileRequested(spot)) mesh.LoadTileIn(spot); return; }
  IEntity house = m_SpotHouses[m_Try];
  vector projected;
  bool onMesh = mesh.IsTileValid(spot) && path.GetClosestPositionOnNavmesh(spot, "2 2 2", projected) && vector.Distance(projected, spot) <= 2;
  bool inside = onMesh && EAC_PedestrianSpawner.InsideHouse(world, house, projected, m_Group);
  int reason = EAC_ESpawnReason.GEOMETRY;
  if (inside) reason = EAC_PedestrianSpawner.GetClearReason(world, projected, null, false, true);
  PrintFormat("[EXPG EAC PROJECT] try=%1 spot=%2 projected=%3 onMesh=%4 inside=%5 clear=%6", m_Try, spot, projected, onMesh, inside, EAC_Diagnostics.ReasonName(reason));
  if (!inside || reason != EAC_ESpawnReason.NONE) { m_Try++; return; }
  m_House = house;
  m_Projected = projected;
  m_Resident = SpawnAt(EAC_HomeIndex.DEFAULT_CHARACTER, projected);
  if (!Check(m_Resident != null, "civilian spawned at the projected indoor point")) { Finish("spawn"); return; }
  AIControlComponent control = AIControlComponent.Cast(m_Resident.FindComponent(AIControlComponent));
  if (control && control.GetAIAgent()) m_Group.AddAgent(control.GetAIAgent());
  Advance(2, 3);
 }

 void Settled(float now)
 {
  BaseWorld world = GetGame().GetWorld();
  ChimeraCharacter actor = ChimeraCharacter.Cast(m_Resident);
  bool alive = actor && actor.GetCharacterController() && !actor.GetCharacterController().IsDead();
  vector origin = vector.Zero;
  if (m_Resident) origin = m_Resident.GetOrigin();
  float drop = Math.AbsFloat(origin[1] - m_Projected[1]);
  bool inside = m_Resident && EAC_PedestrianSpawner.InsideHouse(world, m_House, origin, m_Resident);
  m_IndoorSeconds = now - m_SurveyAt;
  PrintFormat("[EXPG EAC INDOOR] house=%1 projected=%2 settled=%3 drop=%4 inside=%5 alive=%6 seconds=%7", m_House.GetPrefabData().GetPrefabName(), m_Projected, origin, drop, inside, alive, m_IndoorSeconds);
  if (Check(alive && inside && drop < 0.6, string.Format("resident stands inside the vanilla house 3 s after spawning (drop %1 m)", drop)) && m_IndoorSeconds <= INDOOR_DEADLINE) m_IndoorPlaced = 1;
  Check(m_IndoorSeconds <= INDOOR_DEADLINE, string.Format("indoor resident placed within %1 s of the survey (%2 s)", INDOOR_DEADLINE, m_IndoorSeconds));
  Advance(3);
 }

 // A house collapsed by native damage (the Ambient Destruction path) is a ruin.
 void Ruin()
 {
  foreach (IEntity house : m_Found)
  {
   if (house == m_House) continue;
   if (!house.FindComponent(SCR_DestructibleBuildingComponent)) continue;
   m_Ruin = house;
   break;
  }
  if (!Check(m_Ruin != null, "a second destructible house to collapse")) { Advance(5); return; }
  Check(!EAC_HomeIndex.IsRuined(m_Ruin), "intact house is not a ruin");
  SCR_DestructibleBuildingComponent collapse = SCR_DestructibleBuildingComponent.Cast(m_Ruin.FindComponent(SCR_DestructibleBuildingComponent));
  collapse.Kill(Instigator.CreateInstigator(null));
  Check(EAC_HomeIndex.IsRuined(m_Ruin), "house is a ruin as soon as native damage destroys it");
  Advance(4, 8);
 }

 void Ruined()
 {
  bool ruined = m_Ruin && EAC_HomeIndex.IsRuined(m_Ruin);
  bool collapsed;
  if (m_Ruin)
  {
   SCR_DestructibleBuildingComponent collapse = SCR_DestructibleBuildingComponent.Cast(m_Ruin.FindComponent(SCR_DestructibleBuildingComponent));
   collapsed = collapse && collapse.EAC_IsCollapsed();
  }
  PrintFormat("[EXPG EAC RUIN] present=%1 ruined=%2 collapsed=%3", m_Ruin != null, ruined, collapsed);
  Check(!m_Ruin || ruined, "collapsed house stays a ruin (or was deleted by the native collapse)");
  Advance(5);
 }

 bool Flagged(IEntity entity)
 {
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(entity);
  return editable && editable.HasEntityFlag(EEditableEntityFlag.NON_SERIALIZABLE);
 }

 // Vanilla 1.8 saves what IsTracked reports. Owned entities are kept out at
 // creation, tracked again late (as a lazy registration, another system or a
 // regroup would), and must be out again when the real before-save event runs.
 // A foreign NON_SERIALIZABLE flag must not keep an entity in native tracking.
 void SaveContract(float now)
 {
  SCR_PersistenceSystem persistence = SCR_PersistenceSystem.GetByEntityWorld(this);
  EPersistenceSystemState state = EPersistenceSystemState.FAILURE;
  if (persistence) state = persistence.GetState();
  if (!Check(state == EPersistenceSystemState.ACTIVE, string.Format("native persistence active in the fixture world (state %1)", typename.EnumToString(EPersistenceSystemState, state)))) { Advance(6); return; }
  if (!m_Resident || !m_Group) { Check(false, "resident and group available for the save contract"); Advance(6); return; }
  m_Foreign = SpawnAt(EAC_HomeIndex.DEFAULT_CHARACTER, m_Projected + "3 0 0");
  if (!Check(m_Foreign != null, "second civilian spawned")) { Advance(6); return; }
  SCR_EditableEntityComponent foreignEditable = SCR_EditableEntityComponent.GetEditableEntity(m_Foreign);
  if (foreignEditable) foreignEditable.SetEntityFlag(EEditableEntityFlag.NON_SERIALIZABLE, true);
  persistence.StartTracking(m_Foreign, false);
  bool foreignTracked = persistence.IsTracked(m_Foreign);
  EAC_SessionLifecycle.Keep(m_Resident);
  EAC_SessionLifecycle.Keep(m_Group);
  EAC_SessionLifecycle.Keep(m_Foreign);
  Check(EAC_SessionLifecycle.IsSaveHooked(), "before-save hook installed by the first owned entity");
  Check(Flagged(m_Resident) && Flagged(m_Group), "owned resident and group flagged NON_SERIALIZABLE for GM saves");
  Check(!persistence.IsTracked(m_Resident) && !persistence.IsTracked(m_Group), "owned resident and group out of native tracking at creation");
  Check(!foreignTracked || !persistence.IsTracked(m_Foreign), "an entity with someone else's NON_SERIALIZABLE flag is still taken out of native tracking");
  // Late tracking: StartTracking may answer false for a tracked entity; IsTracked decides.
  persistence.StartTracking(m_Resident, false);
  persistence.StartTracking(m_Group, false);
  persistence.StartTracking(m_Foreign, false);
  int lateTracked;
  if (persistence.IsTracked(m_Resident)) lateTracked++;
  if (persistence.IsTracked(m_Group)) lateTracked++;
  if (persistence.IsTracked(m_Foreign)) lateTracked++;
  int stoppedBefore = EAC_SessionLifecycle.GetStoppedCount();
  persistence.GetOnBeforeSave().Invoke(ESaveGameType.MANUAL);
  int atSave;
  if (persistence.IsTracked(m_Resident)) atSave++;
  if (persistence.IsTracked(m_Group)) atSave++;
  if (persistence.IsTracked(m_Foreign)) atSave++;
  int stopped = EAC_SessionLifecycle.GetStoppedCount() - stoppedBefore;
  PrintFormat("[EXPG EAC PERSISTENCE] foreignTracked=%1 lateTracked=%2 atSave=%3 stoppedAtSave=%4 marked=%5", foreignTracked, lateTracked, atSave, stopped, EAC_SessionLifecycle.GetMarkedCount());
  Check(lateTracked > 0 && atSave == 0, string.Format("no ambient entity tracked when a save reads its data (%1/3 tracked going into the save event, %2 after)", lateTracked, atSave));
  // Released by the next Sync once no ledger answers for them: our flag goes,
  // the foreign flag stays, and native tracking is handed back.
  EAC_SessionLifecycle.EAC_FixtureSync(now);
  Check(!Flagged(m_Resident) && !Flagged(m_Group), "released resident and group lose the flag this module set");
  Check(Flagged(m_Foreign), "released entity keeps someone else's NON_SERIALIZABLE flag");
  // Release hands back with a lazy StartTracking; read IsTracked 2 s later.
  m_HandbackPending = true;
  Advance(6, 2);
 }

 // The module itself is never a ledger entity: the before-save sweep must not
 // change whether native persistence tracks it.
 void ModuleContract()
 {
  SCR_PersistenceSystem persistence = SCR_PersistenceSystem.GetByEntityWorld(this);
  if (m_HandbackPending)
  {
   m_HandbackPending = false;
   bool residentBack = persistence && m_Resident && persistence.IsTracked(m_Resident);
   bool foreignBack = persistence && m_Foreign && persistence.IsTracked(m_Foreign);
   PrintFormat("[EXPG EAC HANDBACK] resident=%1 foreign=%2", residentBack, foreignBack);
   Check(residentBack && foreignBack, "released entities are handed back to native tracking");
  }
  IEntity spawned = SpawnAt(MODULE_PREFAB, GetOrigin());
  EAC_AmbientModule module = EAC_AmbientModule.Cast(spawned);
  if (!Check(module != null, "Ambient Civilians module spawned")) { Finish("complete"); return; }
  if (!persistence) { Finish("complete"); return; }
  bool before = persistence.IsTracked(module);
  persistence.GetOnBeforeSave().Invoke(ESaveGameType.MANUAL);
  bool after = persistence.IsTracked(module);
  PrintFormat("[EXPG EAC MODULE] trackedBefore=%1 trackedAfter=%2 flagged=%3", before, after, Flagged(module));
  Check(before == after, "before-save sweep leaves the module's native tracking unchanged");
  Check(!Flagged(module), "module is not flagged NON_SERIALIZABLE");
  Finish("complete");
 }
}

// TEST ONLY. Garrison ledger round trip (GM Tools 0.1.11, EXPG_Snapshot.c).
// pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/EXPG_LedgerGameplay.c -ExpectResult '\[EXPG LEDGER RESULT\] checks=[1-9]\d* failures=0 garrisons=3 buildings=2 awake=1 simulation=1 full=1 posts=1 patrollers=[1-9]\d* respawnedDead=0 duplicates=0 aiBeforeBind=0 overrides=1 excluded=1 nativeLedger=1 nativeSave=(1|na) fullWoke=1 reason=completed' -TimeoutSeconds 600 -OrchestratorSlotGranted
// The runner copies this file to EXPG_GarrisonGameplay.c; the class names are fixed.
// Production Add Garrison server calls (CanFit, fresh roster, AdoptFresh) on two real
// houses: a two-storey map town house (patrollers) holds garrison A (caching Off, so
// awake) and garrison C (Full; added second, so it takes the reinforcement order), a
// spawned village house holds garrison B (Simulation). One soldier of A and one of C
// are killed; A's first soldier gets an AI Global Skills ROE override and B's squad an
// AI Surrender override. With no player connected B and C cache. Then:
//  1. Save: every owned entity (squads and living guards of A and B) is out of native
//     tracking; the ledger is exported (JSON as the CDF bridge writes it) and the native
//     state is serialized through PersistenceSystem.Serialize with the production
//     serializer; a native save point is requested when saving is enabled here.
//  2. Clear, as a load does: BeginImport, every garrison squad deleted with its
//     soldiers, DiscardForImport (no record left, nobody woken or respawned).
//  3. Load: the native ledger is read by the serializer's own read path (the
//     production DeserializeNative); the garrison pump imports it once native persistence
//     is active. Each frame, a restored soldier whose AI is not pinned must already be
//     bound to his post or patrol (aiBeforeBind).
//  4. Same tokens, buildings, alive members (dead never respawned), posts and stops
//     within 0.3 m in building-local coordinates, patrollers, cache states (A awake,
//     B Simulation again, C Full with nothing in the world), overrides, exclusion.
//  5. C woken (its mode set Off): survivors on their posts, casualty not respawned.
// No GM UI, no CDF (see EXPBG CDF Compat), no multiplayer, no cold restart.
modded class EXPG_GarrisonManager
{
 EXPG_GarrisonRecord EXPG_TestByToken(string token)
 {
  foreach (EXPG_GarrisonRecord record : m_Records)
  {
   if (!record.Finished && record.Token == token) return record;
  }
  return null;
 }

 int EXPG_TestTokenCount(string token)
 {
  int count = 0;
  foreach (EXPG_GarrisonRecord record : m_Records)
  {
   if (!record.Finished && record.Token == token) count++;
  }
  return count;
 }

 int EXPG_TestActive()
 {
  int count = 0;
  foreach (EXPG_GarrisonRecord record : m_Records)
  {
   if (!record.Finished) count++;
  }
  return count;
 }
}

modded class EXPG_GarrisonRecord
{
 static int EXPG_TestReleases;
 static string EXPG_TestReleaseReason;
 override void RequestRelease(string reason)
 {
  if (!ReleaseRequested && reason != "fixture scene clear")
  {
   EXPG_TestReleases++;
   EXPG_TestReleaseReason = reason;
   PrintFormat("[EXPG LEDGER RELEASE] group=%1 reason=%2", Group, reason);
  }
  super.RequestRelease(reason);
 }
}

// One saved garrison as the fixture saw it before the clear.
class EXPG_LedgerExpect
{
 string Name;
 string Token;
 IEntity Structure;
 int State;
 int Alive;
 int Patrollers;
 ref array<int> Ids = {};
 ref array<bool> Dead = {};
 ref array<bool> Fixed = {};
 ref array<vector> LocalPosts = {};
}

class EXPG_GarrisonGameplayClass : GenericEntityClass {}
class EXPG_GarrisonGameplay : GenericEntity
{
 static const float FIXTURE_SECONDS = 575;
 static const int SQUAD = 4;
 static const int ROE_VALUE = 2;
 static const int ESR_VALUE = 35;
 IEntity HouseA;
 IEntity HouseB;
 IEntity MapHouse;
 bool Baked;
 vector PointA;
 vector PointB = "4773.46 0 7094.57";
 EXPG_GarrisonManager Manager;
 SCR_AIGroup GroupA;
 SCR_AIGroup GroupB;
 SCR_AIGroup GroupC;
 EXPG_GarrisonRecord RecordA;
 EXPG_GarrisonRecord RecordB;
 EXPG_GarrisonRecord RecordC;
 ref array<ref EXPG_LedgerExpect> Expect = {};
 ref array<IEntity> BoundOnce = {};
 ref array<IEntity> Violations = {};
 // Test seam: stand-ins holding the town house's free fixed posts while C is placed,
 // so C takes the reinforcement order's interior patrols (as if other guards held them).
 ref array<IEntity> Holders = {};
 ref SaveGameOperationCallback SaveCallback;
 ObserversSystem Observers;
 int ObserverKeyA;
 int ObserverKeyB;
 string NativeJson;
 int RoeMemberId;
 int Phase;
 int Checks;
 int Failures;
 int Garrisons;
 int Buildings;
 int AwakeCount;
 int SimulationCount;
 int FullCount;
 int PostsOk;
 int PatrollersSaved;
 int PatrollersRestored;
 int RespawnedDead;
 int Duplicates;
 int OverridesOk;
 int ExcludedOk;
 int NativeLedger;
 int NativeSave = -1;
 int FullWoke;
 int SerializeBefore;
 bool SaveDone;
 bool SaveOk;
 bool Watching;
 float Started;
 float Next;
 float PhaseStarted;
 bool Finished;

 void EXPG_GarrisonGameplay(IEntitySource src, IEntity parent) { SetEventMask(EntityEvent.INIT | EntityEvent.FRAME); }
 float Now() { return GetGame().GetWorld().GetWorldTime() * 0.001; }
 override void EOnInit(IEntity owner)
 {
  if (!Replication.IsServer()) { ClearEventMask(EntityEvent.FRAME); return; }
  Started = Now();
  Next = Started + 15;
  ObserverKeyA = "EXPG_Ledger.ObserverA".Hash();
  ObserverKeyB = "EXPG_Ledger.ObserverB".Hash();
  PrintFormat("[EXPG LEDGER BEGIN] squads=3 size=%1 deadline=%2 connectedPlayer=0", SQUAD, FIXTURE_SECONDS);
 }
 bool Check(bool value, string label)
 {
  Checks++;
  if (!value) Failures++;
  PrintFormat("[EXPG LEDGER CHECK] pass=%1 %2", value, label);
  return value;
 }
 void RemoveObservers()
 {
  if (Observers && GetGame() && GetGame().GetWorld() && GetGame().GetWorld().FindSystem(ObserversSystem) == Observers)
  {
   Observers.RemoveObserverSP(ObserverKeyA);
   Observers.RemoveObserverSP(ObserverKeyB);
  }
 }
 void ~EXPG_GarrisonGameplay() { RemoveObservers(); }
 void Finish(string reason)
 {
  if (Finished) return;
  Finished = true;
  RemoveObservers();
  ClearEventMask(EntityEvent.FRAME);
  string nativeSave = "na";
  if (NativeSave >= 0) nativeSave = NativeSave.ToString();
  string first = string.Format("checks=%1 failures=%2 garrisons=%3 buildings=%4 awake=%5 simulation=%6 full=%7 posts=%8 patrollers=%9", Checks, Failures, Garrisons, Buildings, AwakeCount, SimulationCount, FullCount, PostsOk, PatrollersRestored);
  string second = string.Format("respawnedDead=%1 duplicates=%2 aiBeforeBind=%3 overrides=%4 excluded=%5 nativeLedger=%6 nativeSave=%7 fullWoke=%8 reason=%9", RespawnedDead, Duplicates, Violations.Count(), OverridesOk, ExcludedOk, NativeLedger, nativeSave, FullWoke, reason);
  PrintFormat("[EXPG LEDGER RESULT] %1 %2", first, second);
  GetGame().RequestClose();
 }
 void Advance(int phase)
 {
  Phase = phase;
  PhaseStarted = Now();
  PrintFormat("[EXPG LEDGER PHASE] phase=%1 elapsed=%2", Phase, Now() - Started);
 }
 bool Waited(float seconds, string label)
 {
  if (Now() - PhaseStarted <= seconds) return false;
  Check(false, label);
  Finish("phase " + Phase.ToString());
  return true;
 }
 EntitySpawnParams Params(vector point, float elevation = 0)
 {
  EntitySpawnParams spawn = new EntitySpawnParams();
  spawn.TransformMode = ETransformMode.WORLD;
  Math3D.AnglesToMatrix(vector.Zero, spawn.Transform);
  point[1] = GetGame().GetWorld().GetSurfaceY(point[0], point[2]) + elevation;
  spawn.Transform[3] = point;
  return spawn;
 }
 bool AddMapHouse(IEntity entity)
 {
  if (MapHouse || !SCR_DestructibleBuildingEntity.Cast(entity)) return true;
  ResourceName prefab = SCR_ResourceNameUtils.GetPrefabName(entity);
  if (!prefab.Contains("House_Town_E_2I01")) return true;
  MapHouse = entity;
  return false;
 }
 void OnSaved(bool success, Managed context = null)
 {
  SaveDone = true;
  SaveOk = success;
  PrintFormat("[EXPG LEDGER NATIVE SAVE] completed=1 success=%1", success);
 }
 // Every living guard in his squad and under garrison control.
 bool Bound(EXPG_GarrisonRecord record)
 {
  if (!record || record.Finished || !record.Ready || record.Full || record.Simulation || !record.Group) return false;
  foreach (EXPG_GarrisonMember member : record.Members)
  {
   if (member.CacheMember.Dead) continue;
   SCR_ChimeraCharacter actor = member.CacheMember.Entity;
   if (!actor || actor.GetCharacterGroup() != record.Group) return false;
   if (member.Fixed && !member.Post) return false;
   if (!member.Fixed && !member.Patrol) return false;
  }
  return true;
 }
 bool SimulationAsleep(EXPG_GarrisonRecord record)
 {
  return record && !record.Full && record.Simulation && record.Simulation.Suspended;
 }
 bool FullAsleep(EXPG_GarrisonRecord record)
 {
  return record && record.Full && record.Full.GetState() == EBG_FullGroupPhase.CACHED && !record.Group;
 }
 SCR_AIGroup AddSquad(IEntity house, vector point, int mode, out EXPG_GarrisonRecord record)
 {
  string reason;
  bool fits = Manager.CanFit(house, SQUAD, reason);
  if (!Check(fits && reason.IsEmpty(), "Add Garrison accepted by CanFit: " + reason)) return null;
  ResourceName team = "{84E5BBAB25EA23E5}Prefabs/Groups/BLUFOR/Group_US_FireTeam.et";
  Resource teamResource = Resource.Load(team);
  SCR_AIGroup group = SCR_AIGroup.Cast(GetGame().SpawnEntityPrefab(teamResource, GetGame().GetWorld(), Params(point, 0.3)));
  bool fresh = group && group.EXPG_BeginFreshRoster(SQUAD);
  bool adopted = fresh && Manager.AdoptFresh(group, house, 0, SQUAD);
  if (!Check(adopted, "freshly spawned fire team adopted")) return null;
  group.EXPG_CacheMode = mode;
  record = Manager.Find(group);
  if (!Check(record != null, "garrison record created")) return null;
  return group;
 }
 void Kill(EXPG_GarrisonRecord record)
 {
  EXPG_GarrisonMember victim = record.Members[record.Members.Count() - 1];
  SCR_ChimeraCharacter actor = victim.CacheMember.Entity;
  if (actor) actor.GetDamageManager().Kill(Instigator.CreateInstigator(actor));
 }
 bool DeadSettled(EXPG_GarrisonRecord record)
 {
  return record.Members[record.Members.Count() - 1].CacheMember.Dead;
 }
 vector LocalPost(EXPG_GarrisonRecord record, EXPG_GarrisonMember member)
 {
  return record.Plan.Structure.CoordToLocal(member.PostPoint());
 }
 EXPG_LedgerExpect Remember(string name, EXPG_GarrisonRecord record, int state)
 {
  EXPG_LedgerExpect saved = new EXPG_LedgerExpect();
  saved.Name = name;
  saved.Token = record.Token;
  saved.Structure = record.Plan.Structure;
  saved.State = state;
  foreach (EXPG_GarrisonMember member : record.Members)
  {
   saved.Ids.Insert(member.CacheMember.Id);
   saved.Dead.Insert(member.CacheMember.Dead);
   saved.Fixed.Insert(member.Fixed);
   saved.LocalPosts.Insert(LocalPost(record, member));
   if (!member.CacheMember.Dead) saved.Alive++;
   if (!member.CacheMember.Dead && !member.Fixed) saved.Patrollers++;
  }
  PatrollersSaved += saved.Patrollers;
  PrintFormat("[EXPG LEDGER SAVED] garrison=%1 token=%2 state=%3 alive=%4 members=%5 patrollers=%6", name, saved.Token, state, saved.Alive, saved.Ids.Count(), saved.Patrollers);
  Expect.Insert(saved);
  return saved;
 }
 // The squad and living guards of an awake or Simulation garrison are out of native
 // tracking; in native mode no CDF flag is set.
 bool Excluded(EXPG_GarrisonRecord record)
 {
  if (!record.Group || !EXPG_GarrisonPersistence.OwnsForSave(record.Group) || EXPG_SaveExclusion.IsNativelyTracked(record.Group) || EXPG_SaveExclusion.IsFlagged(record.Group)) return false;
  foreach (EXPG_GarrisonMember member : record.Members)
  {
   SCR_ChimeraCharacter actor = member.CacheMember.Entity;
   if (member.CacheMember.Dead || !actor) continue;
   if (!EXPG_GarrisonPersistence.OwnsForSave(actor) || EXPG_SaveExclusion.IsNativelyTracked(actor) || EXPG_SaveExclusion.IsFlagged(actor)) return false;
  }
  return true;
 }
 int Agents(EXPG_GarrisonRecord record)
 {
  if (!record || !record.Group) return 0;
  return record.Group.GetAgentsCount();
 }
 // Each frame from the load: a restored soldier whose AI runs must be bound already.
 void SampleHolds()
 {
  foreach (EXPG_LedgerExpect saved : Expect)
  {
   EXPG_GarrisonRecord record = Manager.EXPG_TestByToken(saved.Token);
   if (!record || record.ReleaseRequested) continue;
   foreach (EXPG_GarrisonMember member : record.Members)
   {
    SCR_ChimeraCharacter actor = member.CacheMember.Entity;
    if (!actor || member.CacheMember.Dead || member.CacheMember.WasPlayer) continue;
    if (member.Post || member.Patrol)
    {
     if (!BoundOnce.Contains(actor)) BoundOnce.Insert(actor);
     continue;
    }
    if (BoundOnce.Contains(actor) || Violations.Contains(actor)) continue;
    AIControlComponent control = actor.GetAIControlComponent();
    if (!control || !control.GetAIAgent()) continue;
    if (control.GetAIAgent().GetPermanentLOD() == AIAgent.GetMaxLOD()) continue;
    Violations.Insert(actor);
    PrintFormat("[EXPG LEDGER AI BEFORE BIND] garrison=%1 member=%2 actor=%3 permanentLOD=%4", saved.Name, member.CacheMember.Id, actor, control.GetAIAgent().GetPermanentLOD());
   }
  }
 }
 // Same building, members, posts and patrollers as saved; dead never respawned.
 bool VerifyRestored(EXPG_LedgerExpect saved, EXPG_GarrisonRecord record, bool awakeExpected, bool counted = true)
 {
  bool ok = Check(record.Plan.Structure == saved.Structure, saved.Name + ": the same building holds the restored garrison");
  ok = Check(record.Members.Count() == saved.Ids.Count(), string.Format("%1: %2 member rows restored (saved %3)", saved.Name, record.Members.Count(), saved.Ids.Count())) && ok;
  if (!ok) return false;
  int alive = 0;
  int patrollers = 0;
  bool posts = true;
  foreach (int i, EXPG_GarrisonMember member : record.Members)
  {
   bool dead = member.CacheMember.Dead;
   if (member.CacheMember.Id != saved.Ids[i] || dead != saved.Dead[i]) { posts = false; PrintFormat("[EXPG LEDGER ROW] garrison=%1 row=%2 id=%3/%4 dead=%5/%6", saved.Name, i, member.CacheMember.Id, saved.Ids[i], dead, saved.Dead[i]); continue; }
   if (dead)
   {
    if (member.CacheMember.Entity) RespawnedDead++;
    continue;
   }
   alive++;
   if (!member.Fixed) patrollers++;
   float drift = vector.Distance(LocalPost(record, member), saved.LocalPosts[i]);
   if (drift > 0.3 || member.Fixed != saved.Fixed[i]) posts = false;
   PrintFormat("[EXPG LEDGER POST] garrison=%1 member=%2 fixed=%3/%4 localDrift=%5 node=%6 kind=%7", saved.Name, member.CacheMember.Id, member.Fixed, saved.Fixed[i], drift, member.NodeIndex, member.PostKind);
  }
  ok = Check(alive == saved.Alive, string.Format("%1: %2 living guards restored (saved %3)", saved.Name, alive, saved.Alive)) && ok;
  ok = Check(posts, saved.Name + ": every post and patrol stop within 0.3 m in building-local coordinates, same role") && ok;
  ok = Check(patrollers == saved.Patrollers, string.Format("%1: %2 patrollers restored (saved %3)", saved.Name, patrollers, saved.Patrollers)) && ok;
  if (awakeExpected) ok = Check(Agents(record) == saved.Alive, string.Format("%1: the squad holds exactly the survivors (agents %2)", saved.Name, Agents(record))) && ok;
  if (counted) PatrollersRestored += patrollers;
  return ok;
 }
 override void EOnFrame(IEntity owner, float timeSlice)
 {
  if (Finished) return;
  if (Watching) SampleHolds();
  if (Now() < Next) return;
  Next = Now() + 0.5;
  if (Now() - Started > FIXTURE_SECONDS) { Check(false, string.Format("%1 second deadline; last phase %2", FIXTURE_SECONDS, Phase)); Finish("timeout"); return; }
  if (EXPG_GarrisonRecord.EXPG_TestReleases > 0) { Check(false, "no garrison was released: " + EXPG_GarrisonRecord.EXPG_TestReleaseReason); Finish("release"); return; }
  if (Phase == 0)
  {
   array<int> players = {};
   GetGame().GetPlayerManager().GetPlayers(players);
   Manager = EXPG_GarrisonManager.Get();
   Observers = ObserversSystem.Cast(GetGame().GetWorld().FindSystem(ObserversSystem));
   if (!Check(Manager && Observers && players.IsEmpty(), "isolated server with no connected players")) { Finish("setup"); return; }
   Check(Manager.PersistenceMode() == EXPG_GarrisonPersistence.MODE_NATIVE, "native persistence mode (no CDF in the fixture modset)");
   GetGame().GetWorld().QueryEntitiesBySphere("4570 0 10700", 700, AddMapHouse);
   if (!MapHouse) GetGame().GetWorld().QueryEntitiesBySphere("7270 0 4700", 500, AddMapHouse);
   if (!MapHouse) GetGame().GetWorld().QueryEntitiesBySphere("5040 0 3950", 500, AddMapHouse);
   ResourceName townHouse = "{38A5F3E4578087AB}Prefabs/Structures/Houses/Town/House_Town_E_2I01/House_Town_E_2I01.et";
   Resource townResource = Resource.Load(townHouse);
   if (MapHouse)
   {
    HouseA = MapHouse;
    Baked = true;
    PointA = MapHouse.GetOrigin();
   }
   else
   {
    PointA = PointB + "0 0 80";
    HouseA = GetGame().SpawnEntityPrefab(townResource, GetGame().GetWorld(), Params(PointA));
   }
   ResourceName village = "{EDBC0E94793BA9F1}Prefabs/Structures/Houses/Village/House_Village_E_1I01/House_Village_E_1I01.et";
   Resource villageResource = Resource.Load(village);
   HouseB = GetGame().SpawnEntityPrefab(villageResource, GetGame().GetWorld(), Params(PointB));
   if (!Check(SCR_DestructibleBuildingEntity.Cast(HouseA) != null && SCR_DestructibleBuildingEntity.Cast(HouseB) != null, "two enterable houses")) { Finish("buildings"); return; }
   PrintFormat("[EXPG LEDGER HOUSES] mapTownHouse=%1 a=%2 b=%3", Baked, PointA, PointB);
   Observers.InsertObserverSP(ObserverKeyA, PointA[0], PointA[2], null);
   Observers.InsertObserverSP(ObserverKeyB, PointB[0], PointB[2], null);
   SCR_AIWorld aiWorld = SCR_AIWorld.Cast(GetGame().GetAIWorld());
   if (aiWorld && !Baked) aiWorld.RequestNavmeshRebuildEntity(HouseA);
   Manager.Prepare(HouseA);
   Manager.Prepare(HouseB);
   Advance(1);
   return;
  }
  if (Phase == 1)
  {
   EXPG_BuildingPlan planA = Manager.FindPlan(HouseA);
   EXPG_BuildingPlan planB = Manager.FindPlan(HouseB);
   if (!planA || !planA.Done || !planB || !planB.Done) { Waited(150, "both building analyses finished within 150 seconds"); return; }
   PrintFormat("[EXPG LEDGER PLANS] a.nodes=%1 a.slots=%2 a.roam=%3 b.nodes=%4 b.slots=%5", planA.Nodes.Count(), planA.Slots.Count(), planA.RoamStops.Count(), planB.Nodes.Count(), planB.Slots.Count());
   GroupA = AddSquad(HouseA, PointA + "12 0 0", 0, RecordA);
   if (!GroupA) { Finish("add A"); return; }
   GroupB = AddSquad(HouseB, PointB + "12 0 0", 1, RecordB);
   if (!GroupB) { Finish("add B"); return; }
   Advance(2);
   return;
  }
  if (Phase == 2)
  {
   if (!Bound(RecordA) || !Bound(RecordB)) { Waited(60, "garrisons A and B placed and bound within 60 seconds"); return; }
   EXPG_BuildingPlan townPlan = Manager.FindPlan(HouseA);
   foreach (int order, int slot : townPlan.Slots)
   {
    if (!townPlan.FixedSlots[order]) continue;
    IEntity holder = GetGame().SpawnEntity(GenericEntity, GetGame().GetWorld(), Params(PointA + "0 60 0"));
    if (holder && townPlan.ReserveNode(holder, slot)) Holders.Insert(holder);
    else if (holder) SCR_EntityHelper.DeleteEntityAndChildren(holder);
   }
   PrintFormat("[EXPG LEDGER SEAM] free fixed posts held while C is placed: %1", Holders.Count());
   GroupC = AddSquad(HouseA, PointA + "-12 0 0", 2, RecordC);
   if (!GroupC) { Finish("add C"); return; }
   Advance(3);
   return;
  }
  if (Phase == 3)
  {
   if (!Bound(RecordC)) { Waited(60, "garrison C placed and bound within 60 seconds"); return; }
   EXPG_BuildingPlan heldPlan = Manager.FindPlan(HouseA);
   foreach (IEntity held : Holders)
   {
    if (heldPlan) heldPlan.ReleaseReservation(held);
    SCR_EntityHelper.DeleteEntityAndChildren(held);
   }
   Holders.Clear();
   int patrolling = 0;
   foreach (EXPG_GarrisonMember placed : RecordC.Members) if (!placed.Fixed) patrolling++;
   if (!Check(patrolling > 0, string.Format("C took the reinforcement order's interior patrols (%1 patrollers)", patrolling))) { Finish("no patrollers"); return; }
   Kill(RecordA);
   Kill(RecordC);
   foreach (EXPG_GarrisonMember member : RecordA.Members)
   {
    if (member.CacheMember.Dead || !member.CacheMember.Entity) continue;
    EGS_Manager.SetUnitRoe(member.CacheMember.Entity, ROE_VALUE);
    RoeMemberId = member.CacheMember.Id;
    break;
   }
   array<int> squadValues = {ESR_VALUE, -1, -1, -1};
   ESR_Overrides.RestoreGroup(GroupB, squadValues, "ledger fixture");
   Advance(4);
   return;
  }
  if (Phase == 4)
  {
   if (!DeadSettled(RecordA) || !DeadSettled(RecordC)) { Waited(20, "both casualties registered within 20 seconds"); return; }
   bool states = Bound(RecordA) && SimulationAsleep(RecordB) && FullAsleep(RecordC);
   if (!states) { Waited(150, string.Format("A awake, B Simulation and C Full cached within 150 seconds: A='%1' B='%2' C='%3'", RecordA.Status, RecordB.Status, RecordC.Status)); return; }
   Advance(5);
   return;
  }
  if (Phase == 5)
  {
   // 1. Save.
   EXPG_GarrisonPersistence.SyncSaveExclusion();
   bool excluded = Check(Excluded(RecordA) && Excluded(RecordB), "squads and living guards of A and B are out of native tracking, no CDF flag in native mode");
   Check(EXPG_SaveExclusion.IsSaveHooked() || !SCR_PersistenceSystem.GetByCurrentWorld(), "the before-save exclusion hook is registered");
   string allowedReason;
   bool allowed = EXPG_GarrisonPersistence.CanExport(allowedReason);
   Check(EXPG_GarrisonManager.HasActive() && allowed, "a save is allowed with three active garrisons: " + allowedReason);
   if (excluded) ExcludedOk = 1;
   Remember("A", RecordA, 0);
   Remember("B", RecordB, 1);
   Remember("C", RecordC, 2);
   string json, reason;
   bool exported = EXPG_GarrisonPersistence.ExportJson(json, reason);
   array<ref EXPG_GarrisonSnapshot> parsed = {};
   bool parsedOk = exported && EXPG_GarrisonPersistence.ParseJson(json, parsed, reason);
   Check(parsedOk && parsed.Count() == 3, string.Format("CDF-format ledger written and read back: %1 garrisons, %2 bytes, reason '%3'", parsed.Count(), json.Length(), reason));
   foreach (EXPG_GarrisonSnapshot row : parsed) PrintFormat("[EXPG LEDGER ROW SNAPSHOT] token=%1 state=%2 members=%3 alive=%4 squadOrders=%5", row.Token, row.CacheState, row.Members.Count(), row.AliveCount(), row.Squad.Orders.Count());
   PersistenceSystem system = PersistenceSystem.GetInstance();
   EXPG_GarrisonPersistenceState state = EXPG_GarrisonPersistenceState.Get();
   if (system && state)
   {
    // The registered state goes through the production serializer.
    SCR_PersistenceJsonSaveContext systemContext = new SCR_PersistenceJsonSaveContext();
    ESerializeResult result = system.Serialize(state, systemContext);
    Check(result == ESerializeResult.OK && state.SerializeCalls > 0, string.Format("native state serialized through PersistenceSystem with garrisons active: result=%1 calls=%2", result, state.SerializeCalls));
    SerializeBefore = state.SerializeCalls;
   }
   else Check(false, string.Format("native garrison state is registered (system=%1 state=%2)", system != null, state != null));
   // The ledger text the serializer writes, kept for the load below (a live
   // PersistenceSystem.DeserializeLoad of a registered state is not a supported
   // mid-session call; the serializer's own read path is used instead).
   SCR_PersistenceJsonSaveContext nativeContext = new SCR_PersistenceJsonSaveContext();
   ESerializeResult written = EXPG_GarrisonPersistence.SerializeNative(nativeContext);
   NativeJson = nativeContext.SaveToString();
   if (Check(written == ESerializeResult.OK && !NativeJson.IsEmpty(), string.Format("native ledger written by the serializer's path: result=%1 bytes=%2", written, NativeJson.Length()))) NativeLedger = 1;
   SaveGameManager saving = GetGame().GetSaveGameManager();
   if (saving && saving.IsSavingEnabled())
   {
    SaveCallback = new SaveGameOperationCallback(OnSaved);
    saving.RequestSavePoint(ESaveGameType.MANUAL, "expg-ledger", ESaveGameRequestFlags.BLOCKING, SaveCallback);
    NativeSave = 0;
   }
   else Print("[EXPG LEDGER NATIVE SAVE] saving is not enabled in this fixture world; the in-process state round trip stands in for it");
   Advance(6);
   return;
  }
  if (Phase == 6)
  {
   if (NativeSave == 0 && !SaveDone) { Waited(30, "native save point completed within 30 seconds"); return; }
   if (NativeSave == 0)
   {
    EXPG_GarrisonPersistenceState saveState = EXPG_GarrisonPersistenceState.Get();
    bool written = saveState && saveState.SerializeCalls > SerializeBefore;
    if (Check(SaveOk && written, string.Format("native save succeeded with garrisons active and wrote the ledger (calls %1)", saveState.SerializeCalls))) NativeSave = 1;
   }
   // 2. Clear, as a load does: nobody woken or respawned.
   string why;
   if (!Check(EXPG_GarrisonPersistence.BeginImport(why), "load started: " + why)) { Finish("begin import"); return; }
   array<SCR_AIGroup> squads = {GroupA, GroupB};
   foreach (SCR_AIGroup squad : squads)
   {
    SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(squad);
    if (editable) editable.Delete(false, false);
   }
   EXPG_GarrisonPersistence.DiscardForImport("fixture scene clear");
   EXPG_GarrisonPersistence.EndImport();
   Check(Manager.EXPG_TestActive() == 0 && EXPG_SaveExclusion.Count() == 0, "the clear left no garrison record or exclusion");
   // 3. Load through the production native serializer.
   bool deserialized = false;
   if (NativeLedger == 1)
   {
    SCR_PersistenceJsonLoadContext load = new SCR_PersistenceJsonLoadContext();
    deserialized = load.LoadFromString(NativeJson) && EXPG_GarrisonPersistence.DeserializeNative(load);
   }
   if (!Check(deserialized, "native ledger read back by the serializer's path; the pump imports it")) NativeLedger = 0;
   Watching = true;
   Advance(7);
   return;
  }
  if (Phase == 7)
  {
   if (Manager.EXPG_TestActive() < 3) { Waited(20, string.Format("the native ledger imported 3 garrisons within 20 seconds (%1)", Manager.EXPG_TestActive())); return; }
   if (Now() - PhaseStarted < 1 && (GroupA || GroupB)) return;
   EXPG_GarrisonRecord a = Manager.EXPG_TestByToken(Expect[0].Token);
   EXPG_GarrisonRecord b = Manager.EXPG_TestByToken(Expect[1].Token);
   EXPG_GarrisonRecord c = Manager.EXPG_TestByToken(Expect[2].Token);
   if (!a || !b || !c) { Check(false, "every saved token is loaded"); Finish("tokens"); return; }
   bool ready = Bound(a) && SimulationAsleep(b) && FullAsleep(c) && !a.RemapPending && !c.RemapPending;
   if (!ready) { Waited(240, string.Format("loaded A awake and bound, B Simulation cached again, C Full cached within 240 seconds: A='%1' B='%2' C='%3'", a.Status, b.Status, c.Status)); return; }
   // 4. Same garrisons.
   Check(!GroupA && !GroupB, "the squads deleted by the clear are gone; the restored ones are new");
   foreach (EXPG_LedgerExpect saved : Expect) if (Manager.EXPG_TestTokenCount(saved.Token) != 1) Duplicates++;
   Garrisons = Manager.EXPG_TestActive();
   if (a.Plan.Structure == HouseA && c.Plan.Structure == HouseA && b.Plan.Structure == HouseB) Buildings = 2;
   bool restoredA = VerifyRestored(Expect[0], a, true);
   bool restoredB = VerifyRestored(Expect[1], b, true);
   bool restoredC = VerifyRestored(Expect[2], c, false);
   if (restoredA && restoredB && restoredC) PostsOk = 1;
   if (Check(!a.Full && !a.Simulation && a.CacheMode == 0, "A awake (caching Off) as saved")) AwakeCount = 1;
   if (Check(SimulationAsleep(b) && b.CacheMode == 1, "B Simulation cached again as saved")) SimulationCount = 1;
   if (Check(FullAsleep(c) && c.CacheMode == 2 && Agents(c) == 0, "C Full cached with nothing in the world")) FullCount = 1;
   Check(Garrisons == 3 && Duplicates == 0, string.Format("exactly three garrisons, no duplicate (%1, duplicates %2)", Garrisons, Duplicates));
   Check(RespawnedDead == 0, "no casualty was respawned");
   bool roe = false;
   foreach (EXPG_GarrisonMember member : a.Members)
   {
    if (member.CacheMember.Id == RoeMemberId && member.CacheMember.Entity) roe = member.CacheMember.Entity.EGS_GetRoeOverride() == ROE_VALUE;
   }
   bool esr = b.Group && b.Group.ESR_GetOverride(0) == ESR_VALUE;
   if (Check(roe && esr, string.Format("soldier ROE override (%1) and squad surrender override (%2) restored", roe, esr))) OverridesOk = 1;
   EXPG_GarrisonPersistence.SyncSaveExclusion();
   if (!Check(Excluded(a) && Excluded(b), "restored squads and guards are out of native tracking")) ExcludedOk = 0;
   // 5. Wake C.
   c.CacheMode = 0;
   RecordC = c;
   Advance(8);
   return;
  }
  if (Phase == 8)
  {
   if (!Bound(RecordC)) { Waited(60, "C woke from its loaded Full state onto its posts within 60 seconds: " + RecordC.Status); return; }
   if (Check(VerifyRestored(Expect[2], RecordC, true, false) && RespawnedDead == 0, "C woke with its survivors on their posts; its casualty stays dead")) FullWoke = 1;
   Check(EXPG_FullCache.s_SpawnLate == 0, string.Format("every survivor was pinned in his spawning call (held %1, late %2)", EXPG_FullCache.s_SpawnHeld, EXPG_FullCache.s_SpawnLate));
   Advance(9);
   return;
  }
  if (Phase == 9)
  {
   if (Now() - PhaseStarted < 3) return;
   Check(Violations.IsEmpty(), string.Format("no restored soldier ran AI before he was bound (%1 seen)", Violations.Count()));
   Finish("completed");
  }
 }
}

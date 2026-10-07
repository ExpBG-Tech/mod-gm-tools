// TEST ONLY. Random Garrison module (addon/random-garrison) on a real GM_Eden town.
// pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/EXPG_RandomGarrisonGameplay.c -ExpectResult '\[EXPG RANDOM RESULT\] checks=[1-9]\d* failures=0 eligible=([6-9]|[1-9]\d+) excludedPlayers=[1-9]\d* buildings=4 squads=[4-8] distinct=1 inside=1 settings=1 nearPlayer=0 analysingSeen=1 cleared=1 leftovers=0 sameBuildings=1 samePrefabs=1 keptOnDelete=1 orphans=0 reason=completed' -TimeoutSeconds 600 -OrchestratorSlotGranted
// The runner copies this file to EXPG_GarrisonGameplay.c; the class names are fixed.
// The driver point 4773 169 7094 is a GM_Eden town centre (municipal office, houses,
// villa, fire station and pub within 90 m). The fixture spawns no houses.
//  1. A fake player stands at the town centre (the module's ObserverPositions seam);
//     player distance 60 m.
//  2. The real Random Garrison prefab: radius 150, 4 buildings, share 100, squads 1-2,
//     seed 4242, faction US and no second faction, fire teams, cache Off, wake 350,
//     sleep 450. Generate.
//  3. The status is sampled every 0.5 s: "Analysing x/4" is seen and x never decreases.
//     While the zone runs and a squad has taken its posts, both zone saves (native
//     IsGenerated, CDF SavedState) already say generated.
//  4. Done: 4 distinct buildings, each structurally eligible, within 150 m and at least
//     60 m (nearest bounds point) from the player; at least one building excluded near
//     the player; 4-8 squads, as many as the zone's Ready garrisons; every soldier
//     alive, fixed ones within 1.5 m of their posts, all inside their building, posts
//     only planned, building or patrol (none around or at the spawn), checked twice 20 s
//     apart; garrison settings 0/350/450; no generated soldier ever within 59.5 m of the
//     player; no squad deleted as never deployed and no refusal.
//  5. Clear: every squad and soldier is gone, no garrison is left, no building has a
//     garrison, the zone is Idle; every entity waiting in the zone's retire list was
//     kept out of saves (NON_SERIALIZABLE) until it was deleted.
//  6. Regenerate: the same buildings in the same order with the same squads.
//  7. The module is deleted (Keep garrisons): 10 s later every garrison is still active.
// No GM UI, no save/load, no multiplayer.
modded class EXPG_RandomGarrisonModule
{
 static bool EXPG_TestObserverOn;
 static vector EXPG_TestObserver;
 static int EXPG_TestRetireSeen;
 static int EXPG_TestRetireExposed;

 override protected void ObserverPositions(notnull array<vector> positions)
 {
  super.ObserverPositions(positions);
  if (EXPG_TestObserverOn) { positions.Insert(EXPG_TestObserver); }
 }

 // Every tick, before the deletes: what waits in the retire list must be out of saves.
 override protected void RetireSome(EXPG_RGBudget budget)
 {
  EXPG_TestRetireSeen += m_Retire.Count();
  EXPG_TestRetireExposed += m_Retire.EXPG_TestExposed();
  super.RetireSome(budget);
 }
}

modded class EXPG_RetireList
{
 // Entries a CDF capture could still take (no NON_SERIALIZABLE flag).
 int EXPG_TestExposed()
 {
  int exposed;
  foreach (IEntity entity : m_aEntities)
  {
   if (!entity || entity.IsDeleted()) { continue; }
   SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(entity);
   if (editable && !editable.HasEntityFlag(EEditableEntityFlag.NON_SERIALIZABLE))
   {
    exposed++;
    PrintFormat("[EXPG RANDOM EXPOSED] %1 waits for deletion without NON_SERIALIZABLE", entity);
   }
  }
  return exposed;
 }
}

// Any refusal or normal-AI retention of a generated garrison fails the run.
modded class EXPG_GarrisonRecord
{
 static int EXPG_TestRefusals;

 override void Report(string message)
 {
  bool changed = Status != message;
  super.Report(message);
  if (!changed || GeneratedBy.IsEmpty()) { return; }
  if (message.Contains("retained") || message.Contains("refused") || message.Contains("releasing survivors") || message.Contains("cannot") || message.Contains("unavailable"))
  {
   EXPG_TestRefusals++;
   PrintFormat("[EXPG RANDOM REFUSAL] group=%1 status=%2", Group, message);
  }
 }
}

class EXPG_GarrisonGameplayClass : GenericEntityClass {}
class EXPG_GarrisonGameplay : GenericEntity
{
 static const float FIXTURE_SECONDS = 540;
 static const float PHASE_SECONDS = 360;
 static const ResourceName ZONE_PREFAB = "{1B71589F70C61F8E}PrefabsEditable/EXPGR/EXPG_RandomGarrison.et";
 static const int PLAYER_DISTANCE = 60;
 static const int SEED = 4242;
 static const int TARGET = 4;
 vector Center = "4773 0 7094";
 vector Observer;
 EXPG_RandomGarrisonModule Zone;
 EXPG_GarrisonManager Manager;
 string Token;
 int Phase;
 int Checks;
 int Failures;
 int Eligible;
 int ExcludedPlayers;
 int Buildings;
 int Squads;
 int Distinct;
 int InsideOk = 1;
 int SettingsOk;
 int NearPlayer;
 int LastAnalysing = -1;
 bool AnalysingDecreased;
 bool MidRunSampled;
 bool MidRunGenerated = true;
 int Cleared;
 int Leftovers;
 int SameBuildings;
 int SamePrefabs;
 int KeptOnDelete;
 int Orphans;
 float Started;
 float Next;
 float PhaseStarted;
 bool Finished;
 ref array<IEntity> FirstBuildings = {};
 ref array<string> FirstPrefabs = {};
 ref array<IEntity> BeforeClear = {};
 ref array<ref EXPG_GarrisonRecord> Kept = {};

 void EXPG_GarrisonGameplay(IEntitySource src, IEntity parent)
 {
  SetEventMask(EntityEvent.INIT | EntityEvent.FRAME);
 }

 float Now()
 {
  return GetGame().GetWorld().GetWorldTime() * 0.001;
 }

 override void EOnInit(IEntity owner)
 {
  if (!Replication.IsServer())
  {
   ClearEventMask(EntityEvent.FRAME);
   return;
  }
  Started = Now();
  Next = Started + 15;
  PrintFormat("[EXPG RANDOM BEGIN] center=%1 radius=150 target=%2 seed=%3 playerDistance=%4 deadline=%5 connectedPlayer=0", Center, TARGET, SEED, PLAYER_DISTANCE, FIXTURE_SECONDS);
 }

 bool Check(bool value, string label)
 {
  Checks++;
  if (!value) { Failures++; }
  PrintFormat("[EXPG RANDOM CHECK] pass=%1 %2", value, label);
  return value;
 }

 void Finish(string reason)
 {
  if (Finished) { return; }
  Finished = true;
  EXPG_RandomGarrisonModule.EXPG_TestObserverOn = false;
  ClearEventMask(EntityEvent.FRAME);
  int analysingSeen = 0;
  if (LastAnalysing >= 0 && !AnalysingDecreased) { analysingSeen = 1; }
  string first = string.Format("checks=%1 failures=%2 eligible=%3 excludedPlayers=%4 buildings=%5 squads=%6 distinct=%7", Checks, Failures, Eligible, ExcludedPlayers, Buildings, Squads, Distinct);
  string second = string.Format("inside=%1 settings=%2 nearPlayer=%3 analysingSeen=%4 cleared=%5 leftovers=%6", InsideOk, SettingsOk, NearPlayer, analysingSeen, Cleared, Leftovers);
  string third = string.Format("sameBuildings=%1 samePrefabs=%2 keptOnDelete=%3 orphans=%4 reason=%5", SameBuildings, SamePrefabs, KeptOnDelete, Orphans, reason);
  PrintFormat("[EXPG RANDOM RESULT] %1 %2 %3", first, second, third);
  GetGame().RequestClose();
 }

 void Advance(int phase)
 {
  Phase = phase;
  PhaseStarted = Now();
  string status;
  if (Zone) { status = Zone.GetStatus(); }
  PrintFormat("[EXPG RANDOM PHASE] phase=%1 elapsed=%2 status=%3", Phase, Now() - Started, status);
 }

 bool Waited(float seconds, string label)
 {
  if (Now() - PhaseStarted <= seconds)
  {
   return false;
  }
  Check(false, label);
  Finish("phase " + Phase.ToString());
  return true;
 }

 // "Analysing x/4 buildings ...": x never goes down.
 void SampleStatus()
 {
  if (!Zone) { return; }
  string status = Zone.GetStatus();
  if (!status.StartsWith("Analysing ")) { return; }
  int slash = status.IndexOf("/");
  if (slash <= 10) { return; }
  int current = status.Substring(10, slash - 10).ToInt();
  if (current < LastAnalysing)
  {
   AnalysingDecreased = true;
   PrintFormat("[EXPG RANDOM STATUS] analysing went down from %1: %2", LastAnalysing, status);
  }
  if (current > LastAnalysing) { PrintFormat("[EXPG RANDOM STATUS] %1", status); }
  if (current > LastAnalysing) { LastAnalysing = current; }
 }

 // A save made while the zone runs, after a squad has taken its posts (the ledger
 // saves that garrison): the zone must say generated in both saves.
 void SampleGenerated()
 {
  if (!Zone || Zone.State() != EXPG_RandomGarrisonModule.STATE_RUNNING) { return; }
  array<ref EXPG_GarrisonRecord> records = {};
  Generated(records);
  bool ready;
  foreach (EXPG_GarrisonRecord record : records)
  {
   if (record.Ready) { ready = true; }
  }
  if (!ready) { return; }
  vector saved = Zone.SavedState();
  int flags = Math.Round(saved[2]);
  bool saysGenerated = Zone.IsGenerated() && (flags & 1) != 0;
  if (!MidRunSampled || !saysGenerated) { PrintFormat("[EXPG RANDOM MIDRUN] generated=%1 flags=%2 status=%3", saysGenerated, flags, Zone.GetStatus()); }
  MidRunSampled = true;
  if (!saysGenerated) { MidRunGenerated = false; }
 }

 int Generated(notnull array<ref EXPG_GarrisonRecord> records)
 {
  if (!Manager)
  {
   return 0;
  }
  return Manager.CollectGenerated(Token, records);
 }

 // No generated soldier (spawning or deployed) ever near the player.
 void SampleSoldiers()
 {
  array<ref EXPG_GarrisonRecord> records = {};
  Generated(records);
  foreach (EXPG_GarrisonRecord record : records)
  {
   if (!record.Group) { continue; }
   array<AIAgent> agents = {};
   record.Group.GetAgents(agents);
   foreach (AIAgent agent : agents)
   {
    IEntity actor;
    if (agent) { actor = agent.GetControlledEntity(); }
    if (!actor || vector.Distance(actor.GetOrigin(), Observer) >= PLAYER_DISTANCE - 0.5) { continue; }
    NearPlayer++;
    PrintFormat("[EXPG RANDOM NEAR] soldier %1 at %2 is %3 m from the player", actor, actor.GetOrigin(), vector.Distance(actor.GetOrigin(), Observer));
   }
  }
 }

 // Every guard alive in his squad, fixed ones on their posts, everyone inside the
 // building, posts only planned, building or patrol.
 bool Deployed(EXPG_GarrisonRecord record, bool report)
 {
  if (!record || record.Finished || !record.Ready || !record.Group || record.Members.IsEmpty())
  {
   if (report) { PrintFormat("[EXPG RANDOM HELD] record=%1 not ready", record); }
   return false;
  }
  foreach (EXPG_GarrisonMember member : record.Members)
  {
   SCR_ChimeraCharacter actor = member.CacheMember.Entity;
   bool alive = !member.CacheMember.Dead && actor && actor.GetCharacterGroup() == record.Group && !EXPG_GarrisonManager.IsDeadActor(actor);
   bool onPost = alive && (!member.Fixed || vector.DistanceSq(actor.GetOrigin(), member.PostPoint()) <= 2.25);
   bool inside = alive && record.Plan.Inside(actor.GetOrigin(), 0.25);
   bool kind = member.PostKind == EXPG_Placement.PLANNED || member.PostKind == EXPG_Placement.BUILDING || member.PostKind == EXPG_Placement.ROAM;
   if (alive && onPost && inside && kind) { continue; }
   if (report) { PrintFormat("[EXPG RANDOM HELD] group=%1 member=%2 alive=%3 onPost=%4 inside=%5 kind=%6", record.Group, member.CacheMember.Id, alive, onPost, inside, member.PostKind); }
   return false;
  }
  return true;
 }

 void CheckDeployment(string label)
 {
  array<ref EXPG_GarrisonRecord> records = {};
  Generated(records);
  bool all = !records.IsEmpty();
  foreach (EXPG_GarrisonRecord record : records)
  {
   if (!Deployed(record, true)) { all = false; }
  }
  if (!Check(all, label + ": every generated soldier alive, on his post and inside his building (planned, building or patrol posts)")) { InsideOk = 0; }
 }

 void CheckDone()
 {
  Eligible = Zone.EligibleCount();
  ExcludedPlayers = Zone.ExcludedByPlayers();
  array<IEntity> structures = {};
  array<string> prefabs = {};
  Zone.PlacedSites(structures, prefabs);
  Buildings = structures.Count();
  FirstBuildings.Copy(structures);
  FirstPrefabs.Copy(prefabs);
  Check(Buildings == TARGET, string.Format("%1 buildings garrisoned (expected %2); status %3", Buildings, TARGET, Zone.GetStatus()));
  Check(ExcludedPlayers >= 1, string.Format("%1 eligible buildings left out near the player", ExcludedPlayers));
  Distinct = 1;
  int placedSquads;
  foreach (int i, IEntity structure : structures)
  {
   string reason;
   if (!structure)
   {
    Distinct = 0;
    continue;
   }
   for (int j = i + 1; j < structures.Count(); j++)
   {
    if (structures[j] == structure) { Distinct = 0; }
   }
   bool eligible = EXPG_RandomGarrisonCensus.Structural(structure, reason);
   vector mins, maxs;
   structure.GetWorldBounds(mins, maxs);
   float fromCenter = vector.DistanceXZ(structure.GetOrigin(), Center);
   float fromPlayer = EXPG_RGRules.BoxDistance(Observer, mins, maxs);
   Check(eligible && fromCenter <= 150 && fromPlayer >= PLAYER_DISTANCE, string.Format("building %1 eligible=%2 (%3), %4 m from the centre, %5 m from the player", structure, eligible, reason, fromCenter, fromPlayer));
   array<string> parts = {};
   prefabs[i].Split(";", parts, true);
   placedSquads += parts.Count();
  }
  array<ref EXPG_GarrisonRecord> records = {};
  Squads = Generated(records);
  Check(Squads >= TARGET && Squads <= 2 * TARGET && Squads == placedSquads, string.Format("%1 generated garrisons, %2 squads placed", Squads, placedSquads));
  SettingsOk = 1;
  foreach (EXPG_GarrisonRecord record : records)
  {
   if (record.CacheMode != 0 || Math.AbsFloat(record.WakeDistance - 350) > 0.01 || Math.AbsFloat(record.SleepDistance - 450) > 0.01) { SettingsOk = 0; }
  }
  Check(SettingsOk == 1, "every generated garrison has the zone's settings (Off, 350 m, 450 m)");
  CheckDeployment("first check");
 }

 // Squads and soldiers of the zone, captured before Clear.
 void CaptureEntities()
 {
  BeforeClear.Clear();
  array<ref EXPG_GarrisonRecord> records = {};
  Generated(records);
  foreach (EXPG_GarrisonRecord record : records)
  {
   if (record.Group) { BeforeClear.Insert(record.Group); }
   foreach (EXPG_GarrisonMember member : record.Members)
   {
    if (member.CacheMember.Entity && !member.CacheMember.Dead) { BeforeClear.Insert(member.CacheMember.Entity); }
   }
  }
 }

 override void EOnFrame(IEntity owner, float timeSlice)
 {
  if (Finished || Now() < Next) { return; }
  Next = Now() + 0.5;
  if (Now() - Started > FIXTURE_SECONDS)
  {
   Check(false, string.Format("%1 second deadline; last phase %2", FIXTURE_SECONDS, Phase));
   Finish("timeout");
   return;
  }
  if (EXPG_GarrisonRecord.EXPG_TestRefusals > 0)
  {
   Check(false, "no generated garrison refused or retained as normal AI");
   Finish("refusal");
   return;
  }
  if (Zone && Phase >= 2 && Phase <= 4) { SampleStatus(); }
  if (Phase == 2) { SampleGenerated(); }
  if (Phase >= 2 && Phase <= 4) { SampleSoldiers(); }
  if (Phase == 0)
  {
   Manager = EXPG_GarrisonManager.Get();
   if (!Check(Manager != null, "garrison manager on the server")) { Finish("manager"); return; }
   Observer = Center;
   Observer[1] = GetGame().GetWorld().GetSurfaceY(Center[0], Center[2]) + 1.7;
   EXPG_RandomGarrisonModule.EXPG_TestObserver = Observer;
   EXPG_RandomGarrisonModule.EXPG_TestObserverOn = true;
   EntitySpawnParams spawn = new EntitySpawnParams();
   spawn.TransformMode = ETransformMode.WORLD;
   Math3D.AnglesToMatrix(vector.Zero, spawn.Transform);
   vector point = Center;
   point[1] = GetGame().GetWorld().GetSurfaceY(point[0], point[2]);
   spawn.Transform[3] = point;
   Resource zoneResource = Resource.Load(ZONE_PREFAB);
   Zone = EXPG_RandomGarrisonModule.Cast(GetGame().SpawnEntityPrefab(zoneResource, GetGame().GetWorld(), spawn));
   if (!Check(Zone != null, "Random Garrison prefab spawned")) { Finish("spawn"); return; }
   Advance(1);
   return;
  }
  if (Phase == 1)
  {
   Token = Zone.Token();
   if (Token.IsEmpty())
   {
    Waited(10, "zone registered with a token");
    return;
   }
   Zone.ApplySetting(EXPG_RandomGarrisonModule.KEY_RADIUS, 150, false);
   Zone.ApplySetting(EXPG_RandomGarrisonModule.KEY_BUILDINGS, TARGET, false);
   Zone.ApplySetting(EXPG_RandomGarrisonModule.KEY_SHARE, 100, false);
   Zone.ApplySetting(EXPG_RandomGarrisonModule.KEY_SQUADS_MIN, 1, false);
   Zone.ApplySetting(EXPG_RandomGarrisonModule.KEY_SQUADS_MAX, 2, false);
   Zone.ApplySetting(EXPG_RandomGarrisonModule.KEY_SEED, SEED, false);
   Zone.ApplySetting(EXPG_RandomGarrisonModule.KEY_SIZES, EXPG_RGRules.BUCKET_FIRETEAM, false);
   Zone.ApplySetting(EXPG_RandomGarrisonModule.KEY_PLAYER_DISTANCE, PLAYER_DISTANCE, false);
   Zone.ApplySetting(EXPG_RandomGarrisonModule.KEY_CACHE_MODE, 0, false);
   Zone.ApplySetting(EXPG_RandomGarrisonModule.KEY_WAKE, 350, false);
   Zone.ApplySetting(EXPG_RandomGarrisonModule.KEY_SLEEP, 450, false);
   Zone.SetFactionSetting(0, "US", false);
   Zone.SetFactionSetting(1, "", false);
   Check(Zone.ResolveFaction(0) == "US" && Zone.ResolveFaction(1).IsEmpty(), "faction US, no second faction");
   Check(Zone.Run(EXPG_RandomGarrisonModule.ACTION_GENERATE, -1), "Generate accepted");
   Advance(2);
   return;
  }
  if (Phase == 2)
  {
   if (Zone.State() == EXPG_RandomGarrisonModule.STATE_DONE)
   {
    Check(Zone.PendingCount() == 0, "nothing pending at Done");
    Check(MidRunSampled && MidRunGenerated, string.Format("a save during the run says generated once a squad took its posts (sampled %1)", MidRunSampled));
    CheckDone();
    Advance(3);
    return;
   }
   if (Zone.State() == EXPG_RandomGarrisonModule.STATE_STOPPED)
   {
    Check(false, "the generation stopped: " + Zone.GetStatus());
    Finish("stopped");
    return;
   }
   Waited(PHASE_SECONDS, "generation done; status " + Zone.GetStatus());
   return;
  }
  if (Phase == 3)
  {
   if (Now() - PhaseStarted < 20) { return; }
   CheckDeployment("second check 20 s later");
   Check(!Zone.Run(EXPG_RandomGarrisonModule.ACTION_GENERATE, -1) && Zone.GetStatus().Length() > 0, "a second Generate is refused (Already generated)");
   Advance(4);
   return;
  }
  if (Phase == 4)
  {
   Orphans = Zone.OrphansDeleted();
   Check(Orphans == 0, "no squad deleted as never deployed");
   CaptureEntities();
   Check(!BeforeClear.IsEmpty(), string.Format("%1 squads and soldiers before Clear", BeforeClear.Count()));
   Check(Zone.Run(EXPG_RandomGarrisonModule.ACTION_CLEAR, -1), "Clear accepted");
   Advance(5);
   return;
  }
  if (Phase == 5)
  {
   bool idle = Zone.State() == EXPG_RandomGarrisonModule.STATE_IDLE && Zone.RetireCount() == 0 && EXPG_RandomGarrisonDirector.JanitorCount() == 0;
   if (!idle)
   {
    Waited(60, "Clear finished; status " + Zone.GetStatus());
    return;
   }
   Leftovers = 0;
   foreach (IEntity entity : BeforeClear)
   {
    if (entity && !entity.IsDeleted()) { Leftovers++; }
   }
   array<ref EXPG_GarrisonRecord> records = {};
   int remaining = Generated(records);
   bool garrisoned;
   foreach (IEntity structure : FirstBuildings)
   {
    if (Manager.HasGarrison(structure)) { garrisoned = true; }
   }
   if (Check(Leftovers == 0 && remaining == 0 && !garrisoned && !Zone.IsGenerated(), string.Format("Clear: %1 leftovers, %2 garrisons, garrisoned=%3", Leftovers, remaining, garrisoned))) { Cleared = 1; }
   int seen = EXPG_RandomGarrisonModule.EXPG_TestRetireSeen;
   int exposed = EXPG_RandomGarrisonModule.EXPG_TestRetireExposed;
   Check(seen > 0 && exposed == 0, string.Format("Clear: what waited for deletion stayed out of saves (%1 waiting samples, %2 exposed)", seen, exposed));
   Check(Zone.Run(EXPG_RandomGarrisonModule.ACTION_REGENERATE, -1), "Regenerate accepted");
   Advance(6);
   return;
  }
  if (Phase == 6)
  {
   if (Zone.State() != EXPG_RandomGarrisonModule.STATE_DONE)
   {
    Waited(PHASE_SECONDS, "regeneration done; status " + Zone.GetStatus());
    return;
   }
   array<IEntity> structures = {};
   array<string> prefabs = {};
   Zone.PlacedSites(structures, prefabs);
   SameBuildings = 1;
   SamePrefabs = 1;
   if (structures.Count() != FirstBuildings.Count()) { SameBuildings = 0; }
   if (prefabs.Count() != FirstPrefabs.Count()) { SamePrefabs = 0; }
   for (int i = 0; i < structures.Count() && i < FirstBuildings.Count(); i++)
   {
    if (structures[i] != FirstBuildings[i]) { SameBuildings = 0; }
    if (i < prefabs.Count() && i < FirstPrefabs.Count() && prefabs[i] != FirstPrefabs[i]) { SamePrefabs = 0; }
   }
   Check(SameBuildings == 1, "Regenerate with the same seed picks the same buildings");
   Check(SamePrefabs == 1, "Regenerate with the same seed picks the same squads per building");
   if (Zone.OrphansDeleted() > Orphans) { Orphans = Zone.OrphansDeleted(); }
   Kept.Clear();
   Generated(Kept);
   Check(!Kept.IsEmpty(), string.Format("%1 garrisons before the module is deleted", Kept.Count()));
   SCR_EntityHelper.DeleteEntityAndChildren(Zone);
   Advance(7);
   return;
  }
  if (Phase == 7)
  {
   if (Now() - PhaseStarted < 10) { return; }
   KeptOnDelete = 1;
   foreach (EXPG_GarrisonRecord record : Kept)
   {
    if (!record || record.Finished || !record.Ready || record.ReleaseRequested) { KeptOnDelete = 0; }
   }
   Check(KeptOnDelete == 1 && !Kept.IsEmpty(), "deleting the module (Keep garrisons) keeps every garrison active");
   Finish("completed");
  }
 }
}

// TEST ONLY. Garrison caching while CDF Game Master Save is loaded (0.1.7 live test:
// every garrison logged "Full cache held: CDF saves cannot keep Garrison Full
// survivors" and nothing cached it; the Unit Caching zone said "4 groups held by
// another EXPBG module").
// pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/EXPG_CdfFallbackGameplay.c -ExpectResult '\[EXPG CDF FALLBACK RESULT\] checks=[1-9]\d* failures=0 guards=4 fullCycle=1 simulationCycle=1 cdfLines=1 zoneNote=1 reason=completed' -TimeoutSeconds 480 -OrchestratorSlotGranted
// The runner copies this file to EXPG_GarrisonGameplay.c; the class names are fixed.
// Same generated GM_Eden world, house and US fire team as the repeat fixture, through
// the production Add Garrison server calls (CanFit, fresh roster, AdoptFresh). The
// fixture modset has no CDF, so the one test seam is CdfLoaded() (production reads the
// loaded addon list for 6A1876F37D65AB09 once per mission); player presence is one
// injected entity, the garrison's Players() list. Cases, in order:
//  1. CDF absent, cache mode Full (the default): the garrison Full-caches (soldiers
//     removed) with the presence 1000 m away and restores every survivor onto his
//     post when the presence returns. The Full path is unchanged.
//  2. CDF loaded, cache mode still Full: CacheModeInUse is Simulation, the GM's Full
//     choice is kept. A Unit Caching zone (Full) is placed over the house. Presence
//     350 m away (inside the 400 m sleep distance, outside the 300 m wake distance)
//     for 50 s: the garrison stays awake on its posts (its own sleep distance holds).
//     The zone status says "Cached by EXPBG Garrison itself ... 1 group awake on their
//     posts" and never "held by another EXPBG module".
//  3. Presence 1000 m away: the garrison Simulation-caches with status "Simulation
//     cached (CDF loaded)" (logged once), no Full transaction ever starts, the same
//     four soldiers stay in the group on their posts, and it stays cached (no
//     wake/sleep flip from the Full choice). The zone status says "1 group Simulation
//     cached (CDF loaded)".
//  4. Presence 340 m away (inside sleep, outside wake): still cached. Presence 250 m
//     away (inside wake): the same four soldiers wake where they slept, on their posts,
//     controls bound; the zone status says "awake on their posts" again.
// Any "cache held", refusal or survivor release fails the run. No GM UI, no real CDF
// save or load (see the CDF Compat round trips), no multiplayer.
modded class EXPG_GarrisonManager
{
 override protected bool CdfLoaded()
 {
  return EXPG_GarrisonGameplay.s_FakeCdf;
 }

 override protected void Players()
 {
  super.Players();
  if (EXPG_GarrisonGameplay.s_Presence) { m_Players.Insert(EXPG_GarrisonGameplay.s_Presence); }
 }
}

modded class EXPG_GarrisonRecord
{
 static int EXPG_TestRefusals;
 static int EXPG_TestCdfLines;
 override void Report(string message)
 {
  bool changed = Status != message;
  super.Report(message);
  if (!changed) return;
  if (message == "Simulation cached (CDF loaded)") EXPG_TestCdfLines++;
  if (message.Contains("ache held") || message.Contains("recovery held") || message.Contains("retained") || message.Contains("refused") || message.Contains("releasing survivors") || message.Contains("cannot") || message.Contains("unavailable") || message.Contains("timed out"))
  {
   EXPG_TestRefusals++;
   PrintFormat("[EXPG CDF FALLBACK REFUSAL] group=%1 status=%2", Group, message);
  }
 }
}

class EXPG_GarrisonGameplayClass : GenericEntityClass {}
class EXPG_GarrisonGameplay : GenericEntity
{
 static const float FIXTURE_SECONDS = 360;
 static const int SQUAD = 4;
 static const float AWAKE_HOLD = 50;
 static const string ZONE_PREFAB = "{7E1080ED8F0633FD}PrefabsEditable/EXPBG/EBG_CacheZone.et";
 static const string GARRISON_NOTE = "Cached by EXPBG Garrison itself, with its own wake and sleep distances: ";
 static bool s_FakeCdf;
 static IEntity s_Presence;
 IEntity Structure;
 EXPG_GarrisonManager Manager;
 EXPG_BuildingPlan Plan;
 SCR_AIGroup Group;
 EXPG_GarrisonRecord Record;
 EBG_CacheZone Zone;
 ref array<EntityID> Ids = {};
 ref array<vector> SleepPositions = {};
 ObserversSystem Observers;
 int ObserverKey;
 int Phase;
 int Checks;
 int Failures;
 int FullCycle;
 int SimulationCycle;
 int ZoneNote;
 bool FullUnderCdf;
 float Started;
 float Next;
 float PhaseStarted;
 bool Finished;
 vector Point = "4773.46 0 7094.57";

 void EXPG_GarrisonGameplay(IEntitySource src, IEntity parent) { SetEventMask(EntityEvent.INIT | EntityEvent.FRAME); }
 float Now() { return GetGame().GetWorld().GetWorldTime() * 0.001; }
 override void EOnInit(IEntity owner)
 {
  if (!Replication.IsServer()) { ClearEventMask(EntityEvent.FRAME); return; }
  s_FakeCdf = false;
  s_Presence = null;
  Started = Now();
  Next = Started + 15;
  ObserverKey = "EXPG_CdfFallback.Observer".Hash();
  PrintFormat("[EXPG CDF FALLBACK BEGIN] squad=%1 awakeHold=%2 deadline=%3 connectedPlayer=0 presence=injected cdf=seam", SQUAD, AWAKE_HOLD, FIXTURE_SECONDS);
 }
 bool Check(bool value, string label)
 {
  Checks++;
  if (!value) Failures++;
  PrintFormat("[EXPG CDF FALLBACK CHECK] pass=%1 %2", value, label);
  return value;
 }
 void RemoveObserver()
 {
  if (Observers && GetGame() && GetGame().GetWorld() && GetGame().GetWorld().FindSystem(ObserversSystem) == Observers)
   Observers.RemoveObserverSP(ObserverKey);
 }
 void ~EXPG_GarrisonGameplay() { RemoveObserver(); }
 void Finish(string reason)
 {
  if (Finished) return;
  Finished = true;
  RemoveObserver();
  s_FakeCdf = false;
  s_Presence = null;
  ClearEventMask(EntityEvent.FRAME);
  int guards = 0;
  if (Record) guards = Record.Members.Count();
  PrintFormat("[EXPG CDF FALLBACK RESULT] checks=%1 failures=%2 guards=%3 fullCycle=%4 simulationCycle=%5 cdfLines=%6 zoneNote=%7 reason=%8", Checks, Failures, guards, FullCycle, SimulationCycle, EXPG_GarrisonRecord.EXPG_TestCdfLines, ZoneNote, reason);
  GetGame().RequestClose();
 }
 void Advance(int phase)
 {
  Phase = phase;
  PhaseStarted = Now();
  PrintFormat("[EXPG CDF FALLBACK PHASE] phase=%1 elapsed=%2 status=%3", Phase, Now() - Started, EXPG_StatusText());
 }
 string EXPG_StatusText()
 {
  if (!Record) return "none";
  return Record.Status;
 }
 bool Waited(float seconds, string label)
 {
  if (Now() - PhaseStarted <= seconds) return false;
  Check(false, label);
  PostsHeld(true);
  LogZone("timeout");
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
 // The injected player presence, east of the house at standing height.
 void PlacePresence(float east)
 {
  vector point = Point + Vector(east, 0, 0);
  point[1] = GetGame().GetWorld().GetSurfaceY(point[0], point[2]) + 1.8;
  s_Presence.SetOrigin(point);
  PrintFormat("[EXPG CDF FALLBACK PRESENCE] east=%1 at=%2", east, point);
 }
 bool Active()
 {
  return Group && Group.EXPG_Active && Record && Manager.Find(Group) == Record && !Record.ReleaseRequested;
 }
 bool Awake() { return Record && !Record.Full && !Record.Simulation; }
 bool SimulationAsleep() { return Record && !Record.Full && Record.Simulation && Record.Simulation.Suspended; }
 bool FullAsleep() { return Record && Record.Full && Record.Full.GetState() == EBG_FullGroupPhase.CACHED; }
 vector EXPG_Origin(IEntity entity)
 {
  vector origin;
  if (entity) origin = entity.GetOrigin();
  return origin;
 }
 // Awake garrison: every soldier alive in its own group, on his post, controls bound.
 bool PostsHeld(bool report = false)
 {
  if (!Active() || !Awake() || Record.Members.Count() != SQUAD || Group.GetAgentsCount() != SQUAD)
  {
   if (report && Record) PrintFormat("[EXPG CDF FALLBACK HELD] group=%1 active=%2 members=%3 agents=%4 status=%5", Group, Active(), Record.Members.Count(), Group.GetAgentsCount(), Record.Status);
   return false;
  }
  foreach (EXPG_GarrisonMember member : Record.Members)
  {
   SCR_ChimeraCharacter actor = member.CacheMember.Entity;
   bool alive = !member.CacheMember.Dead && actor && actor.GetCharacterGroup() == Group && !EXPG_GarrisonManager.IsDeadActor(actor);
   bool bound = (member.Fixed && member.Post) || (!member.Fixed && member.Patrol);
   // Bind tolerance: a guard knocked off his post holds where he came to rest (his post moves to him).
   bool onPost = alive && (!member.Fixed || vector.DistanceSq(actor.GetOrigin(), member.PostPoint()) <= 2.25);
   if (alive && bound && onPost) continue;
   if (report) PrintFormat("[EXPG CDF FALLBACK HELD] group=%1 member=%2 alive=%3 bound=%4 onPost=%5 kind=%6 post=%7 actor=%8", Group, member.CacheMember.Id, alive, bound, onPost, member.PostKind, member.PostPoint(), EXPG_Origin(actor));
   return false;
  }
  return true;
 }
 // The very same actors as captured, in the group, each within 0.5 m of where he
 // slept (when positions were captured) and within 1.5 m of his fixed post.
 bool SameSoldiers(bool comparePositions, string stage)
 {
  if (!Active() || Record.Members.Count() != Ids.Count() || Group.GetAgentsCount() != SQUAD) return false;
  foreach (int i, EXPG_GarrisonMember member : Record.Members)
  {
   SCR_ChimeraCharacter actor = member.CacheMember.Entity;
   bool same = actor && !member.CacheMember.Dead && actor.GetID() == Ids[i] && actor.GetCharacterGroup() == Group;
   bool spot = same && (!member.Fixed || vector.DistanceSq(actor.GetOrigin(), member.PostPoint()) <= 2.25);
   if (same && comparePositions && i < SleepPositions.Count() && vector.DistanceSq(actor.GetOrigin(), SleepPositions[i]) > 0.25) spot = false;
   if (same && spot) continue;
   PrintFormat("[EXPG CDF FALLBACK SOLDIER] stage=%1 member=%2 same=%3 spot=%4 post=%5 slept=%6 actor=%7", stage, member.CacheMember.Id, same, spot, member.PostPoint(), SleepPositions.Count() > i, EXPG_Origin(actor));
   return false;
  }
  return true;
 }
 void CaptureIds()
 {
  Ids.Clear();
  // Called only while PostsHeld: every member has a live actor. A missing one
  // leaves Ids short and SameSoldiers fails on the count.
  foreach (EXPG_GarrisonMember member : Record.Members)
  {
   if (member.CacheMember.Entity) Ids.Insert(member.CacheMember.Entity.GetID());
  }
 }
 void CapturePositions()
 {
  SleepPositions.Clear();
  foreach (EXPG_GarrisonMember member : Record.Members) SleepPositions.Insert(EXPG_Origin(member.CacheMember.Entity));
 }
 string Note()
 {
  if (!Zone) return string.Empty;
  return Zone.EnrollmentNote;
 }
 void LogZone(string stage)
 {
  if (!Zone) return;
  PrintFormat("[EXPG CDF FALLBACK ZONE] stage=%1 managed=%2 passes=%3 note='%4' status='%5'", stage, Zone.ManagedCount, Zone.EnrollmentPasses, Zone.EnrollmentNote, Zone.Status);
  PrintFormat("[EXPG CDF FALLBACK NOTICE] stage=%1 text='%2'", stage, EBG_CacheManager.Get().ZoneNoticeText(Zone));
 }
 // Module defaults (affected 300, wake 700, sleep 900), Full, per-group activation,
 // 5 s clear delay, no cleanup or overlays, debug log on (as the local-cache fixture).
 EBG_CacheZone SpawnZone()
 {
  Resource zoneResource = Resource.Load(ZONE_PREFAB);
  IEntity spawned = GetGame().SpawnEntityPrefab(zoneResource, GetGame().GetWorld(), Params(Point, 0.3));
  EBG_CacheZone zone = EBG_CacheZone.Cast(spawned);
  if (!zone) return null;
  zone.SetValue(1, 1); zone.SetValue(2, 1); zone.SetValue(3, 300);
  zone.SetValue(4, 700); zone.SetValue(5, 900); zone.SetValue(12, 5);
  zone.SetValue(15, 0); zone.SetValue(18, 0); zone.SetValue(21, 1);
  zone.SetValue(0, 1);
  return zone;
 }
 bool AddSquad()
 {
  string reason;
  bool fits = Manager.CanFit(Structure, SQUAD, reason);
  if (!Check(fits && reason.IsEmpty() && !Manager.HasGarrison(Structure), "Add Garrison accepted by CanFit on an empty house: " + reason)) return false;
  ResourceName team = "{84E5BBAB25EA23E5}Prefabs/Groups/BLUFOR/Group_US_FireTeam.et";
  Resource teamResource = Resource.Load(team);
  IEntity spawnedTeam = GetGame().SpawnEntityPrefab(teamResource, GetGame().GetWorld(), Params(Point + "14 0 4", 0.3));
  Group = SCR_AIGroup.Cast(spawnedTeam);
  bool fresh = Group && Group.EXPG_BeginFreshRoster(SQUAD);
  bool adopted = fresh && Manager.AdoptFresh(Group, Structure, 0, SQUAD);
  if (!Check(adopted, "freshly spawned fire team adopted by the house")) return false;
  Record = Manager.Find(Group);
  return Check(Record != null, "garrison record created");
 }
 override void EOnFrame(IEntity owner, float timeSlice)
 {
  if (Finished || Now() < Next) return;
  Next = Now() + 0.5;
  if (Now() - Started > FIXTURE_SECONDS) { Check(false, string.Format("%1 second deadline; last phase %2", FIXTURE_SECONDS, Phase)); PostsHeld(true); LogZone("deadline"); Finish("timeout"); return; }
  if (EXPG_GarrisonRecord.EXPG_TestRefusals > 0) { Check(false, "no garrison status said cache held, refused, retained or released its survivors"); Finish("refusal"); return; }
  if (Record && Record.Ready && !Active()) { Check(false, "garrison stays assigned for the whole run: " + Record.Status); Finish("premature release"); return; }
  if (s_FakeCdf && Record && Record.Full && !FullUnderCdf)
  {
   FullUnderCdf = true;
   Check(false, "no Full transaction starts while CDF is loaded");
   Finish("full under cdf");
   return;
  }
  if (Phase == 0)
  {
   array<int> players = {};
   GetGame().GetPlayerManager().GetPlayers(players);
   Manager = EXPG_GarrisonManager.Get();
   Observers = ObserversSystem.Cast(GetGame().GetWorld().FindSystem(ObserversSystem));
   if (!Check(Manager && Observers && players.IsEmpty() && EBG_CacheZone.Zones.IsEmpty(), "isolated server with no connected players or Unit Caching zones")) { Finish("setup"); return; }
   Observers.InsertObserverSP(ObserverKey, Point[0], Point[2], null);
   ResourceName house = "{EDBC0E94793BA9F1}Prefabs/Structures/Houses/Village/House_Village_E_1I01/House_Village_E_1I01.et";
   Resource houseResource = Resource.Load(house);
   Structure = GetGame().SpawnEntityPrefab(houseResource, GetGame().GetWorld(), Params(Point));
   if (!Check(SCR_DestructibleBuildingEntity.Cast(Structure) != null, "native enterable house spawned")) { Finish("building"); return; }
   IEntity presence = GetGame().SpawnEntity(GenericEntity, GetGame().GetWorld(), Params(Point + "1000 0 0", 1.8));
   s_Presence = presence;
   if (!Check(s_Presence != null, "player presence entity spawned 1000 m east")) { Finish("presence"); return; }
   Manager.Prepare(Structure);
   Advance(1);
   return;
  }
  if (Phase == 1)
  {
   Plan = Manager.FindPlan(Structure);
   if (!Plan || !Plan.Done) { Waited(60, "production plan finished within 60 seconds"); return; }
   PrintFormat("[EXPG CDF FALLBACK PLAN] nodes=%1 slots=%2 error=%3", Plan.Nodes.Count(), Plan.Slots.Count(), Plan.Error);
   if (!Check(Plan.Error.IsEmpty() && Plan.Slots.Count() >= SQUAD, "production plan has room for the fire team")) { Finish("plan"); return; }
   if (!AddSquad()) { Finish("add"); return; }
   Advance(2);
   return;
  }
  if (Phase == 2)
  {
   if (!Record.Ready) { Waited(30, "fire team placed within 30 seconds: " + Record.Status); return; }
   if (!Check(PostsHeld(true), "all four soldiers placed on their posts in their own group")) { Finish("placement"); return; }
   Check(Group.EXPG_CacheMode == 2 && Manager.CacheModeInUse(Group) == 2, "CDF absent: Full (the default) is the cache mode in use");
   if (!Check(Group.EXPG_WakeDistance == 300 && Group.EXPG_SleepDistance == 400, string.Format("garrison default distances wake 300 m and sleep 400 m (wake=%1 sleep=%2)", Group.EXPG_WakeDistance, Group.EXPG_SleepDistance))) { Finish("distances"); return; }
   Advance(3);
   return;
  }
  if (Phase == 3)
  {
   // Case 1: CDF absent, presence 1000 m away (beyond the 400 m sleep distance).
   if (!FullAsleep()) { Waited(100, "garrison Full-cached within 100 seconds without CDF: " + Record.Status); return; }
   if (!Check(!Record.Simulation && Group.GetAgentsCount() == 0 && Record.Status.Contains("Full cached"), "without CDF the garrison Full-caches and its soldiers are removed until wake")) { Finish("full sleep"); return; }
   Check(EXPG_GarrisonManager.DescribeCache(Group) == "Full cached", "Garrison describes its squad as Full cached: " + EXPG_GarrisonManager.DescribeCache(Group));
   PlacePresence(0);
   Advance(4);
   return;
  }
  if (Phase == 4)
  {
   if (!PostsHeld()) { Waited(45, "Full survivors restored onto their posts within 45 seconds"); return; }
   Check(Record.Status == "Garrison restored", "Full restore reports Garrison restored");
   FullCycle = 1;
   Advance(5);
   return;
  }
  if (Phase == 5)
  {
   if (!PostsHeld(true)) { Check(false, "restored garrison holds its posts for five seconds"); Finish("full hold"); return; }
   if (Now() - PhaseStarted < 5) return;
   // Case 2: CDF loaded; the GM's Full choice stays, Simulation is used.
   s_FakeCdf = true;
   CaptureIds();
   Check(Group.EXPG_CacheMode == 2 && Manager.CacheModeInUse(Group) == 1, "CDF loaded: the Full choice is kept and Simulation is the cache mode in use");
   Zone = SpawnZone();
   if (!Check(Zone != null, "Unit Caching Full zone placed over the house")) { Finish("zone"); return; }
   PlacePresence(350);
   Advance(6);
   return;
  }
  if (Phase == 6)
  {
   if (!Awake() || !PostsHeld(true)) { Check(false, "garrison stays awake on its posts while the presence is inside its 400 m sleep distance"); Finish("sleep distance"); return; }
   if (Now() - PhaseStarted < AWAKE_HOLD) return;
   if (Zone.EnrollmentPasses < 1 || !Note().Contains(GARRISON_NOTE + "1 group awake on their posts")) { Waited(AWAKE_HOLD + 30, "zone status says Garrison caches the awake squad itself: '" + Note() + "'"); return; }
   LogZone("awake");
   Check(!Note().Contains("held by another EXPBG module") && Zone.ManagedCount == 0, "zone status no longer implies the garrison squad is uncached, and the zone enrolls none");
   Check(EXPG_GarrisonManager.DescribeCache(Group) == "awake on their posts", "Garrison describes its awake squad: " + EXPG_GarrisonManager.DescribeCache(Group));
   // Case 3: presence beyond the sleep distance.
   PlacePresence(1000);
   Advance(7);
   return;
  }
  if (Phase == 7)
  {
   if (!SimulationAsleep()) { Waited(60, "garrison Simulation-cached within 60 seconds with CDF loaded: " + Record.Status); return; }
   CapturePositions();
   Check(Record.Status == "Simulation cached (CDF loaded)", "status names the CDF fallback: " + Record.Status);
   Check(SameSoldiers(false, "cached"), "the same four soldiers stay in their group on their posts while cached");
   bool suspended = true;
   foreach (EXPG_GarrisonMember sleeper : Record.Members) if (!sleeper.CacheMember.Entity || !sleeper.CacheMember.Entity.EBG_IsSimulationCached()) suspended = false;
   Check(suspended, "every soldier is Simulation cached (AI paused in place)");
   Check(EBG_CacheManager.IsCacheHeld(Group), "Unit Caching sees the squad's cache held by Garrison");
   Check(EXPG_GarrisonManager.DescribeCache(Group) == "Simulation cached (CDF loaded)", "Garrison describes its squad as Simulation cached (CDF loaded): " + EXPG_GarrisonManager.DescribeCache(Group));
   Advance(8);
   return;
  }
  if (Phase == 8)
  {
   if (!SimulationAsleep() || Record.Status != "Simulation cached (CDF loaded)") { Check(false, "garrison stays Simulation cached without a wake cause (no flip from the Full choice): " + Record.Status); Finish("simulation hold"); return; }
   if (Now() - PhaseStarted < 10) return;
   if (!Note().Contains(GARRISON_NOTE + "1 group Simulation cached (CDF loaded)")) { Waited(40, "zone status says Garrison Simulation-caches the squad: '" + Note() + "'"); return; }
   LogZone("cached");
   string notice = EBG_CacheManager.Get().ZoneNoticeText(Zone);
   bool noted = Check(notice.Contains("0 groups enrolled") && notice.Contains(GARRISON_NOTE + "1 group Simulation cached (CDF loaded)") && !notice.Contains("held by another EXPBG module"), "zone notice says the garrison squad is cached by Garrison");
   if (noted) ZoneNote = 1;
   Check(SameSoldiers(true, "held"), "soldiers have not moved while cached");
   Check(EXPG_GarrisonRecord.EXPG_TestCdfLines == 1, string.Format("one status line for the fallback sleep (lines=%1)", EXPG_GarrisonRecord.EXPG_TestCdfLines));
   // Case 4: inside the sleep distance but outside the wake distance.
   PlacePresence(340);
   Advance(9);
   return;
  }
  if (Phase == 9)
  {
   if (!SimulationAsleep()) { Check(false, "garrison stays cached while the presence is outside its 300 m wake distance"); Finish("wake distance"); return; }
   if (Now() - PhaseStarted < 6) return;
   PlacePresence(250);
   Advance(10);
   return;
  }
  if (Phase == 10)
  {
   if (!PostsHeld()) { Waited(20, "garrison woke onto its posts within 20 seconds of the presence entering its wake distance"); return; }
   Check(Record.Status == "Garrison restored", "Simulation restore reports Garrison restored: " + Record.Status);
   Check(SameSoldiers(true, "restored"), "the same four soldiers woke where they slept, on their posts");
   Check(!EBG_CacheManager.IsCacheHeld(Group), "cache hold released once the guards are awake");
   SimulationCycle = 1;
   Advance(11);
   return;
  }
  if (Phase == 11)
  {
   if (!PostsHeld(true)) { Check(false, "woken garrison holds its posts"); Finish("restored hold"); return; }
   if (Now() - PhaseStarted < 5) return;
   if (!Note().Contains(GARRISON_NOTE + "1 group awake on their posts")) { Waited(40, "zone status says the garrison squad is awake again: '" + Note() + "'"); return; }
   LogZone("woken");
   Check(EXPG_GarrisonRecord.EXPG_TestCdfLines == 1 && !FullUnderCdf, "one fallback line and no Full transaction under CDF for the whole run");
   Finish("completed");
  }
 }
}

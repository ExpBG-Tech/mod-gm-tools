// TEST ONLY. Repeated EXPBG Add Garrison on one building (0.1.5 report: Add Garrison
// did nothing once the building had a garrison; "This building already has a garrison").
// pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/EXPG_RepeatGarrisonGameplay.c -ExpectResult '\[EXPG REPEAT RESULT\] checks=[1-9]\d* failures=0 adds=[3-8] guards=[1-9]\d* capacity=[1-9]\d* posts=[1-9]\d* building=\d+ roam=\d+ around=[1-9]\d* spawn=0 reason=completed' -TimeoutSeconds 480 -OrchestratorSlotGranted
// The runner copies this file to EXPG_GarrisonGameplay.c; the class names are fixed.
// Same generated GM_Eden world, house and US fire team as the default fixture, through
// the production calls the editor makes after its picker: CanFit, then the fresh roster
// and AdoptFresh on the newly spawned squad. Fire teams are added to the one house until
// at least three adds were made and some soldiers had to stand around the building (at
// most eight adds); the third add waits until the first garrison is Full-cached. Every
// add must be accepted and fully placed (free posts first), earlier garrisons stay
// untouched, posts never overlap; overflow soldiers may patrol inside (kind 4, not
// fixed) before any stands around the house. Then every garrison sleeps (the third in Simulation,
// the rest Full), the second wakes alone while the others stay cached, then all wake on
// their posts, and Force Move releases the second garrison alone. No players, no GM UI,
// no save/load. Server log: one "[EXPG Garrison] group=... added to building ..." line
// per add with the placed count per kind of position.
class EXPG_RepeatAdd
{
 SCR_AIGroup Group;
 ref EXPG_GarrisonRecord Record;
 ref array<EntityID> Ids = {};
 ref array<vector> Posts = {};
 ref array<bool> FixedPosts = {};
}

// Any refusal, normal-AI retention or survivor release fails the run.
modded class EXPG_GarrisonRecord
{
 static int EXPG_TestRefusals;
 override void Report(string message)
 {
  bool changed = Status != message;
  super.Report(message);
  if (!changed) return;
  if (message.Contains("retained") || message.Contains("refused") || message.Contains("releasing survivors") || message.Contains("cannot") || message.Contains("unavailable"))
  {
   EXPG_TestRefusals++;
   PrintFormat("[EXPG REPEAT REFUSAL] group=%1 status=%2", Group, message);
  }
 }
}

class EXPG_GarrisonGameplayClass : GenericEntityClass {}
class EXPG_GarrisonGameplay : GenericEntity
{
 static const float FIXTURE_SECONDS = 330;
 static const int MAX_ADDS = 8;
 static const int SQUAD = 4;
 IEntity Structure;
 EXPG_GarrisonManager Manager;
 EXPG_BuildingPlan Plan;
 ref array<ref EXPG_RepeatAdd> Adds = {};
 AIWaypoint ForceMove;
 ObserversSystem Observers;
 int ObserverKey;
 int Phase;
 int Checks;
 int Failures;
 int Capacity;
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
  Started = Now();
  Next = Started + 15;
  ObserverKey = "EXPG_RepeatGarrison.Observer".Hash();
  PrintFormat("[EXPG REPEAT BEGIN] squad=%1 maxAdds=%2 deadline=%3 connectedPlayer=0", SQUAD, MAX_ADDS, FIXTURE_SECONDS);
 }
 bool Check(bool value, string label)
 {
  Checks++;
  if (!value) Failures++;
  PrintFormat("[EXPG REPEAT CHECK] pass=%1 %2", value, label);
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
  ClearEventMask(EntityEvent.FRAME);
  int guards = 0;
  int posts = 0;
  int building = 0;
  int around = 0;
  int roam = 0;
  int spawn = 0;
  foreach (EXPG_RepeatAdd add : Adds)
  {
   if (!add.Record) continue;
   foreach (EXPG_GarrisonMember member : add.Record.Members)
   {
    guards++;
    if (member.PostKind == EXPG_Placement.PLANNED) posts++;
    else if (member.PostKind == EXPG_Placement.BUILDING) building++;
    else if (member.PostKind == EXPG_Placement.AROUND) around++;
    else if (member.PostKind == EXPG_Placement.ROAM) roam++;
    else spawn++;
   }
  }
  string placedAt = string.Format("posts=%1 building=%2 roam=%3 around=%4 spawn=%5", posts, building, roam, around, spawn);
  PrintFormat("[EXPG REPEAT RESULT] checks=%1 failures=%2 adds=%3 guards=%4 capacity=%5 %6 reason=%7", Checks, Failures, Adds.Count(), guards, Capacity, placedAt, reason);
  GetGame().RequestClose();
 }
 void Advance(int phase)
 {
  Phase = phase;
  PhaseStarted = Now();
  PrintFormat("[EXPG REPEAT PHASE] phase=%1 elapsed=%2 adds=%3", Phase, Now() - Started, Adds.Count());
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
 int AroundCount()
 {
  int around = 0;
  foreach (EXPG_RepeatAdd add : Adds)
  {
   if (!add.Record) continue;
   foreach (EXPG_GarrisonMember member : add.Record.Members) if (member.PostKind == EXPG_Placement.AROUND) around++;
  }
  return around;
 }
 bool Active(EXPG_RepeatAdd add)
 {
  return add.Group && add.Group.EXPG_Active && add.Record && Manager.Find(add.Group) == add.Record && !add.Record.ReleaseRequested;
 }
 bool Asleep(EXPG_GarrisonRecord record)
 {
  if (record.Full) return record.Full.GetState() == EBG_FullGroupPhase.CACHED;
  return record.Simulation && record.Simulation.Suspended;
 }
 bool Awake(EXPG_GarrisonRecord record) { return !record.Full && !record.Simulation; }
 vector EXPG_RepeatOrigin(IEntity entity)
 {
  vector origin;
  if (entity) origin = entity.GetOrigin();
  return origin;
 }
 // Awake garrison: every soldier alive in its own group, on his post, controls bound.
 bool PostsHeld(EXPG_RepeatAdd add, bool report = false)
 {
  if (!Active(add) || !Awake(add.Record) || add.Record.Members.Count() != SQUAD || add.Group.GetAgentsCount() != SQUAD)
  {
   if (report) PrintFormat("[EXPG REPEAT HELD] group=%1 active=%2 members=%3 agents=%4 status=%5", add.Group, Active(add), add.Record.Members.Count(), add.Group.GetAgentsCount(), add.Record.Status);
   return false;
  }
  foreach (EXPG_GarrisonMember member : add.Record.Members)
  {
   SCR_ChimeraCharacter actor = member.CacheMember.Entity;
   bool alive = !member.CacheMember.Dead && actor && actor.GetCharacterGroup() == add.Group && !EXPG_GarrisonManager.IsDeadActor(actor);
   bool bound = (member.Fixed && member.Post) || (!member.Fixed && member.Patrol);
   // Bind tolerance: a guard knocked off his post holds where he came to rest (his post moves to him).
   bool onPost = alive && (!member.Fixed || vector.DistanceSq(actor.GetOrigin(), member.PostPoint()) <= 2.25);
   if (alive && bound && onPost) continue;
   if (report) PrintFormat("[EXPG REPEAT HELD] group=%1 member=%2 alive=%3 bound=%4 onPost=%5 kind=%6 post=%7 actor=%8", add.Group, member.CacheMember.Id, alive, bound, onPost, member.PostKind, member.PostPoint(), EXPG_RepeatOrigin(actor));
   return false;
  }
  return true;
 }
 // Post, roster and (while not Full-cached) the very same actors as when placed.
 bool Untouched(EXPG_RepeatAdd add)
 {
  if (!Active(add) || add.Record.Members.Count() != add.Posts.Count()) return false;
  foreach (int i, EXPG_GarrisonMember member : add.Record.Members)
  {
   if (member.CacheMember.Dead) return false;
   if (member.Fixed && vector.DistanceSq(member.PostPoint(), add.Posts[i]) > 0.0001) return false;
   if (!add.Record.Full && (!member.CacheMember.Entity || member.CacheMember.Entity.GetID() != add.Ids[i])) return false;
  }
  return true;
 }
 bool AddSquad()
 {
  int number = Adds.Count() + 1;
  string reason;
  bool garrisoned = Manager.HasGarrison(Structure);
  bool fits = Manager.CanFit(Structure, SQUAD, reason);
  if (!Check(fits && reason.IsEmpty() && garrisoned == (number > 1), string.Format("add %1 accepted by CanFit, building garrisoned=%2: %3", number, garrisoned, reason))) return false;
  ResourceName team = "{84E5BBAB25EA23E5}Prefabs/Groups/BLUFOR/Group_US_FireTeam.et";
  Resource teamResource = Resource.Load(team);
  SCR_AIGroup group = SCR_AIGroup.Cast(GetGame().SpawnEntityPrefab(teamResource, GetGame().GetWorld(), Params(Point + Vector(14, 0, 4 * number), 0.3)));
  bool fresh = group && group.EXPG_BeginFreshRoster(SQUAD);
  bool adopted = fresh && Manager.AdoptFresh(group, Structure, 0, SQUAD);
  if (!Check(adopted, string.Format("add %1: freshly spawned fire team adopted by the building", number))) return false;
  // Later adds stay awake until the caching phase; the first one Full-caches early.
  if (number > 1) group.EXPG_CacheMode = 0;
  EXPG_RepeatAdd add = new EXPG_RepeatAdd();
  add.Group = group;
  add.Record = Manager.Find(group);
  Adds.Insert(add);
  return Check(add.Record != null, string.Format("add %1: garrison record created", number));
 }
 bool VerifyAdd(int index)
 {
  EXPG_RepeatAdd add = Adds[index];
  EXPG_GarrisonRecord record = add.Record;
  int number = index + 1;
  if (!Check(Active(add) && record.Members.Count() == SQUAD && add.Group.GetAgentsCount() == SQUAD, string.Format("add %1: all %2 soldiers placed in their own group, none trimmed", number, SQUAD))) return false;
  array<int> kinds = {0, 0, 0, 0, 0};
  foreach (EXPG_GarrisonMember member : record.Members)
  {
   SCR_ChimeraCharacter actor = member.CacheMember.Entity;
   vector post = member.PostPoint();
   PrintFormat("[EXPG REPEAT POST] add=%1 member=%2 kind=%3 node=%4 fixed=%5 post=%6 actor=%7", number, member.CacheMember.Id, member.PostKind, member.NodeIndex, member.Fixed, post, actor.GetOrigin());
   bool valid = false;
   if (member.PostKind == EXPG_Placement.PLANNED) valid = member.NodeIndex >= 0 && Plan.Slots.Contains(member.NodeIndex);
   else if (member.PostKind == EXPG_Placement.BUILDING) valid = member.NodeIndex >= 0 && !Plan.Slots.Contains(member.NodeIndex) && Plan.Nodes[member.NodeIndex].Reachable && Plan.Inside(post, 0.25) && Plan.Supported(post, 0.2, actor);
   else if (member.PostKind == EXPG_Placement.AROUND) valid = member.NodeIndex < 0 && !Plan.Inside(post) && Plan.GroundSupported(post, 0.2, actor) && vector.DistanceXZ(post, Plan.Origin) < 60;
   else if (member.PostKind == EXPG_Placement.ROAM) valid = !member.Fixed && member.NodeIndex >= 0 && Plan.Nodes[member.NodeIndex].IndoorWalk && !Plan.Nodes[member.NodeIndex].DoorBlock && Plan.Inside(post, 0.25);
   if (!Check(valid && (member.Fixed == (member.PostKind != EXPG_Placement.ROAM)), string.Format("add %1 member %2: valid kind %3 post", number, member.CacheMember.Id, member.PostKind))) return false;
   if (index == 0 && !Check(member.PostKind == EXPG_Placement.PLANNED || member.PostKind == EXPG_Placement.ROAM, "the sole garrison uses the planned building posts and patrol starts")) return false;
   kinds[member.PostKind] = kinds[member.PostKind] + 1;
   // An added post never overlaps a fixed post of any garrison on the building.
   if (index > 0)
   {
    bool spaced = true;
    for (int other = 0; other <= index; other++)
    {
     foreach (int j, vector taken : Adds[other].Posts)
     {
      if (Adds[other].FixedPosts[j] && EXPG_BuildingPlan.RoutesConflict(post, post, taken, taken)) spaced = false;
     }
    }
    if (!Check(spaced, string.Format("add %1 member %2: post keeps clear of every fixed post", number, member.CacheMember.Id))) return false;
   }
   add.Ids.Insert(actor.GetID());
   add.Posts.Insert(post);
   add.FixedPosts.Insert(member.Fixed);
  }
  PrintFormat("[EXPG REPEAT ADD] add=%1 placed=%2 posts=%3 building=%4 around=%5 spawn=%6 roam=%7", number, record.Members.Count(), kinds[0], kinds[1], kinds[2], kinds[3], kinds[4]);
  for (int earlier = 0; earlier < index; earlier++)
  {
   if (!Check(Untouched(Adds[earlier]), string.Format("add %1 left garrison %2 untouched", number, earlier + 1))) return false;
  }
  return true;
 }
 override void EOnFrame(IEntity owner, float timeSlice)
 {
  if (Finished || Now() < Next) return;
  Next = Now() + 0.5;
  if (Now() - Started > FIXTURE_SECONDS) { Check(false, string.Format("%1 second deadline; last phase %2", FIXTURE_SECONDS, Phase)); foreach (EXPG_RepeatAdd late : Adds) PostsHeld(late, true); Finish("timeout"); return; }
  if (EXPG_GarrisonRecord.EXPG_TestRefusals > 0) { Check(false, "no garrison refused, retained as normal AI or released its survivors"); Finish("refusal"); return; }
  foreach (int i, EXPG_RepeatAdd add : Adds)
  {
   if (!add.Record || !add.Record.Ready || Active(add) || (Phase >= 11 && i == 1)) continue;
   Check(false, string.Format("garrison %1 released before the final Force Move: %2", i + 1, add.Record.Status));
   Finish("premature release");
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
   Manager.Prepare(Structure);
   Advance(1);
   return;
  }
  if (Phase == 1)
  {
   Plan = Manager.FindPlan(Structure);
   if (!Plan || !Plan.Done) return;
   Capacity = Plan.Slots.Count();
   PrintFormat("[EXPG REPEAT PLAN] nodes=%1 slots=%2 error=%3", Plan.Nodes.Count(), Capacity, Plan.Error);
   if (!Check(Plan.Error.IsEmpty() && Capacity >= SQUAD, "production plan has room for the first fire team")) { Finish("plan"); return; }
   if (!AddSquad()) { Finish("add"); return; }
   Advance(2);
   return;
  }
  if (Phase == 2)
  {
   EXPG_RepeatAdd last = Adds[Adds.Count() - 1];
   if (!last.Record.Ready)
   {
    if (Now() - PhaseStarted > 30) { Check(false, string.Format("add %1 placed within 30 seconds: %2", Adds.Count(), last.Record.Status)); Finish("placement timeout"); }
    return;
   }
   if (!VerifyAdd(Adds.Count() - 1)) { Finish("placement"); return; }
   if (Adds.Count() == 2) { Advance(3); return; }
   if (Adds.Count() < 3 || (AroundCount() == 0 && Adds.Count() < MAX_ADDS))
   {
    if (!AddSquad()) { Finish("add"); return; }
    Advance(2);
    return;
   }
   Check(AroundCount() > 0, string.Format("soldiers beyond the building's room stand around it after %1 adds", Adds.Count()));
   Advance(4);
   return;
  }
  if (Phase == 3)
  {
   // Add the third squad while the first garrison is Full-cached (no bodies).
   if (!Asleep(Adds[0].Record))
   {
    if (Now() - PhaseStarted > 90) { Check(false, "first garrison Full-cached within 90 seconds: " + Adds[0].Record.Status); Finish("first sleep"); }
    return;
   }
   if (!Check(Adds[0].Record.Full != null && Adds[0].Group.GetAgentsCount() == 0, "first garrison Full-cached; its soldiers are gone until wake")) { Finish("first sleep state"); return; }
   if (!AddSquad()) { Finish("add"); return; }
   Advance(2);
   return;
  }
  if (Phase == 4)
  {
   if (Now() - PhaseStarted < 5) return;
   bool held = Asleep(Adds[0].Record);
   for (int a = 1; a < Adds.Count(); a++) if (!PostsHeld(Adds[a], true)) held = false;
   if (!Check(held, "every added garrison holds its posts for five seconds while the first stays cached")) { Finish("posts"); return; }
   for (int m = 1; m < Adds.Count(); m++)
   {
    int mode = 2;
    if (m == 2) mode = 1;
    Adds[m].Group.EXPG_SetSetting(0, mode);
   }
   Advance(5);
   return;
  }
  if (Phase == 5)
  {
   bool asleep = true;
   foreach (EXPG_RepeatAdd sleeper : Adds) if (!Asleep(sleeper.Record)) asleep = false;
   if (!asleep)
   {
    if (Now() - PhaseStarted > 90) { Check(false, "every garrison on the building cached within 90 seconds"); foreach (EXPG_RepeatAdd sleepy : Adds) PrintFormat("[EXPG REPEAT SLEEP] group=%1 status=%2", sleepy.Group, sleepy.Record.Status); Finish("sleep"); }
    return;
   }
   bool modes = Adds[2].Record.Simulation != null;
   foreach (int s, EXPG_RepeatAdd full : Adds) if (s != 2 && (!full.Record.Full || full.Group.GetAgentsCount() != 0)) modes = false;
   if (!Check(modes, "third garrison Simulation-cached, every other garrison Full-cached with its soldiers removed")) { Finish("sleep modes"); return; }
   Advance(6);
   return;
  }
  if (Phase == 6)
  {
   foreach (EXPG_RepeatAdd held6 : Adds) if (!Asleep(held6.Record)) { Check(false, "garrisons stay cached without a wake cause"); Finish("sleep hold"); return; }
   if (Now() - PhaseStarted < 5) return;
   Adds[1].Group.EXPG_SetSetting(0, 0);
   Advance(7);
   return;
  }
  if (Phase == 7)
  {
   foreach (int c, EXPG_RepeatAdd other7 : Adds) if (c != 1 && !Asleep(other7.Record)) { Check(false, "waking the second garrison leaves every other garrison cached"); Finish("independent wake"); return; }
   if (!PostsHeld(Adds[1]))
   {
    if (Now() - PhaseStarted > 30) { PostsHeld(Adds[1], true); Check(false, "second garrison woke onto its posts within 30 seconds"); Finish("second wake"); }
    return;
   }
   Check(true, "second garrison woke alone onto its posts; the others stay cached");
   Advance(8);
   return;
  }
  if (Phase == 8)
  {
   foreach (int d, EXPG_RepeatAdd other8 : Adds) if (d != 1 && !Asleep(other8.Record)) { Check(false, "other garrisons stay cached while the second is awake"); Finish("independent hold"); return; }
   if (!PostsHeld(Adds[1], true)) { Check(false, "second garrison holds its posts while the others sleep"); Finish("second hold"); return; }
   if (Now() - PhaseStarted < 5) return;
   foreach (int w, EXPG_RepeatAdd waking : Adds) if (w != 1) waking.Group.EXPG_SetSetting(0, 0);
   Advance(9);
   return;
  }
  if (Phase == 9)
  {
   bool restored = true;
   foreach (EXPG_RepeatAdd wakeCheck : Adds) if (!PostsHeld(wakeCheck)) restored = false;
   if (!restored)
   {
    if (Now() - PhaseStarted > 60) { foreach (EXPG_RepeatAdd slow : Adds) PostsHeld(slow, true); Check(false, "every garrison woke onto its posts within 60 seconds"); Finish("wake all"); }
    return;
   }
   Check(true, "every garrison woke with all soldiers on their posts in their own groups");
   Advance(10);
   return;
  }
  if (Phase == 10)
  {
   foreach (EXPG_RepeatAdd held10 : Adds) if (!PostsHeld(held10, true)) { Check(false, "every garrison holds its posts after waking"); Finish("post hold"); return; }
   if (Now() - PhaseStarted < 5) return;
   ResourceName waypoint = "{06E1B6EBD480C6E0}Prefabs/AI/Waypoints/AIWaypoint_ForcedMove.et";
   Resource waypointResource = Resource.Load(waypoint);
   ForceMove = AIWaypoint.Cast(GetGame().SpawnEntityPrefab(waypointResource, GetGame().GetWorld(), Params(Point + "50 0 0")));
   if (!Check(ForceMove != null, "native Force Move waypoint spawned")) { Finish("force move setup"); return; }
   Adds[1].Group.AddWaypoint(ForceMove);
   Advance(11);
   return;
  }
  if (Phase == 11)
  {
   if (Manager.Find(Adds[1].Group) || Adds[1].Group.EXPG_Active)
   {
    if (Now() - PhaseStarted > 30) { Check(false, "Force Move released the second garrison within 30 seconds"); Finish("force move"); }
    return;
   }
   bool others = true;
   foreach (int f, EXPG_RepeatAdd kept : Adds) if (f != 1 && !PostsHeld(kept, true)) others = false;
   if (!Check(others, "Force Move released the second garrison alone; the others keep their posts")) { Finish("force move scope"); return; }
   Advance(12);
   return;
  }
  if (Phase == 12)
  {
   if (Now() - PhaseStarted < 5) return;
   bool kept12 = !Manager.Find(Adds[1].Group) && !Adds[1].Group.EXPG_Active;
   foreach (int k, EXPG_RepeatAdd still : Adds) if (k != 1 && !PostsHeld(still, true)) kept12 = false;
   Check(kept12, "five seconds later only the second garrison is released");
   Finish("completed");
  }
 }
}

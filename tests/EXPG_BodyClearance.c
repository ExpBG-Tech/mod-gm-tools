// Standalone native fixture; source authored, native execution NOT RUN.
// Run separately through Run-Gameplay -FixturePath. Its ordinary cache-result
// verifier does not apply: require the BODY markers and a clean native exit.
class EXPG_GarrisonGameplayClass : GenericEntityClass {}
class EXPG_GarrisonGameplay : GenericEntity
{
 static const ResourceName HOUSE = "{EDBC0E94793BA9F1}Prefabs/Structures/Houses/Village/House_Village_E_1I01/House_Village_E_1I01.et";
 static const ResourceName WALL = "{9A25B3AF92191E1F}Prefabs/Structures/Walls/Concrete/ConcreteWall_USSR_01/ConcreteWall_USSR_01_single.et";
 EXPG_GarrisonManager Manager;
 EXPG_BuildingPlan Plan;
 IEntity Structure;
 IEntity Wall;
 EntityID WallId;
 ref array<vector> Points = {};
 vector Point;
 int Phase, Attempt, Checks, Failures;
 float Started, Next, DeleteStarted;
 bool Finished, VerifiedBlock;

 void EXPG_GarrisonGameplay(IEntitySource src, IEntity parent)
 {
  SetEventMask(EntityEvent.INIT | EntityEvent.FRAME);
 }
 float Now() { return GetGame().GetWorld().GetWorldTime() * 0.001; }
 override void EOnInit(IEntity owner)
 {
  if (!Replication.IsServer()) { ClearEventMask(EntityEvent.FRAME); return; }
  Started = Now(); Next = Started + 15;
  Print("[EXPG BODY BEGIN] realPlan=1 actorsSpawned=0 deadlineSeconds=120");
 }
 bool Check(bool value, string description)
 {
  Checks++;
  if (!value) { Failures++; }
  PrintFormat("[EXPG BODY CHECK] pass=%1 %2", value, description);
  return value;
 }
 void Finish(string reason)
 {
  if (Finished) { return; }
  Finished = true;
  ClearEventMask(EntityEvent.FRAME);
  if (Wall) { SCR_EntityHelper.DeleteEntityAndChildren(Wall); }
  PrintFormat("[EXPG BODY RESULT] checks=%1 failures=%2 attempts=%3 verifiedBlock=%4 reason=%5", Checks, Failures, Attempt, VerifiedBlock, reason);
  GetGame().RequestClose();
 }
 EntitySpawnParams Params(vector position, float yaw = 0)
 {
  EntitySpawnParams spawn = new EntitySpawnParams();
  spawn.TransformMode = ETransformMode.WORLD;
  Math3D.AnglesToMatrix(Vector(yaw, 0, 0), spawn.Transform);
  spawn.Transform[3] = position;
  return spawn;
 }
 TraceBox Body(vector position)
 {
  // Exact stationary-volume geometry used by Plan.ClearBody(p,p+0.01y).
  TraceBox body = new TraceBox();
  body.Start = position + Vector(0, 0.05, 0);
  body.End = position + Vector(0, 0.06, 0);
  body.Mins = Vector(-0.23, 0, -0.23);
  body.Maxs = Vector(0.23, 1.75, 0.23);
  body.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
  body.LayerMask = EPhysicsLayerDefs.CharacterAI;
  return body;
 }
 bool Probe(vector position, bool blocked, bool report)
 {
  TraceBox overlap = Body(position);
  TraceBox sweep = Body(position);
  float occupied = GetGame().GetWorld().TracePosition(overlap, null);
  float fraction = GetGame().GetWorld().TraceMove(sweep, null);
  bool exactOverlap = Wall && overlap.TraceEnt == Wall && overlap.ColliderName != string.Empty && overlap.ColliderIndex >= 0;
  bool exactSweep = Wall && sweep.TraceEnt == Wall && sweep.ColliderName != string.Empty && sweep.ColliderIndex >= 0;
  if (report)
  {
   PrintFormat("[EXPG BODY OVERLAP] attempt=%1 result=%2 exactWall=%3 collider=%4 index=%5 entity=%6", Attempt, occupied, exactOverlap, overlap.ColliderName, overlap.ColliderIndex, overlap.TraceEnt);
   PrintFormat("[EXPG BODY SWEEP] attempt=%1 result=%2 exactWall=%3 collider=%4 index=%5 entity=%6", Attempt, fraction, exactSweep, sweep.ColliderName, sweep.ColliderIndex, sweep.TraceEnt);
  }
  if (blocked) { return occupied < 0 && fraction < 0.999 && exactOverlap && exactSweep; }
  return occupied >= 0 && fraction >= 0.999;
 }
 override void EOnFrame(IEntity owner, float timeSlice)
 {
  if (Finished || Now() < Next) { return; }
  Next = Now() + 0.25;
  if (Now() - Started > 120) { Check(false, "bounded native clearance deadline"); Finish("timeout"); return; }
  if (Phase == 0)
  {
   array<int> players = {};
   GetGame().GetPlayerManager().GetPlayers(players);
   Manager = EXPG_GarrisonManager.Get();
   if (!Check(Manager && players.IsEmpty(), "isolated server without connected players")) { Finish("setup"); return; }
   vector origin = "4773.46 0 7094.57";
   origin[1] = GetGame().GetWorld().GetSurfaceY(origin[0], origin[2]);
   Structure = GetGame().SpawnEntityPrefab(Resource.Load(HOUSE), GetGame().GetWorld(), Params(origin));
   if (!Check(SCR_DestructibleBuildingEntity.Cast(Structure) != null, "native Village house spawned")) { Finish("house"); return; }
   Manager.Prepare(Structure);
   Phase = 1;
   return;
  }
  if (Phase == 1)
  {
   Plan = Manager.FindPlan(Structure);
   if (!Plan || !Plan.Done) { return; }
   if (!Check(Plan.Valid() && Plan.Error == string.Empty, "production building analysis completed")) { Finish("plan"); return; }
   foreach (EXPG_BuildingNode node : Plan.Nodes)
   {
    if (!node.Reachable || !node.Interior || !Plan.Inside(node.Position, 0.25) || !Plan.Supported(node.Position)) { continue; }
    if (!Plan.ClearBody(node.Position, node.Position + Vector(0, 0.01, 0)) || !Probe(node.Position, false, false)) { continue; }
    Points.Insert(node.Position);
    if (Points.Count() == 8) { break; }
   }
   if (!Check(!Points.IsEmpty(), "real unoccupied nodes clear before obstruction")) { Finish("no clear node"); return; }
   Phase = 2;
  }
  if (Phase == 2)
  {
   if (Attempt >= Points.Count() * 6) { Check(false, "native wall overlap and sweep must identify actual wall colliders"); Finish("no verified obstruction"); return; }
   Point = Points[Attempt / 6];
   int variant = Attempt % 6;
   int rotation = variant % 2;
   int widthStep = variant / 2;
   float yaw = rotation * 90.0;
   float offset = (widthStep - 1) * 0.5;
   Attempt++;
   if (!Probe(Point, false, false)) { return; }
   Wall = GetGame().SpawnEntityPrefab(Resource.Load(WALL), GetGame().GetWorld(), Params(Point, yaw));
   if (!Check(Wall != null, "native concrete wall spawned")) { Finish("wall"); return; }
   WallId = Wall.GetID();
   // Centre the real wall's bounds at the query; vary along its own width to
   // accommodate panel seams. There are no actors to displace or fabricated shapes.
   vector mins, maxs;
   vector transform[4];
   Wall.GetBounds(mins, maxs);
   Wall.GetWorldTransform(transform);
   transform[3] = Point - transform[0] * ((mins[0] + maxs[0]) * 0.5 + offset) - transform[2] * ((mins[2] + maxs[2]) * 0.5) - Vector(0, mins[1], 0);
   Wall.SetWorldTransform(transform);
   Phase = 3;
   Next = Now() + 0.5;
   return;
  }
  if (Phase == 3)
  {
   VerifiedBlock = Probe(Point, true, true);
   if (VerifiedBlock)
   {
    Check(true, "CharacterAI stationary overlap and sweep identify the concrete wall");
    if (!Check(!Plan.ClearBody(Point, Point + Vector(0, 0.01, 0)), "production body clearance rejects real solid wall")) { Finish("clearance mismatch"); return; }
   }
   SCR_EntityHelper.DeleteEntityAndChildren(Wall);
   DeleteStarted = Now();
   Phase = 4;
   return;
  }
  if (Phase == 4)
  {
   if (GetGame().GetWorld().FindEntityByID(WallId))
   {
    if (Now() - DeleteStarted > 2) { Check(false, "native wall deletion acknowledged"); Finish("deletion"); }
    return;
   }
   Wall = null;
   if (!VerifiedBlock) { Phase = 2; return; }
   Check(true, "native wall deletion acknowledged");
   bool clear = Probe(Point, false, true) && Plan.ClearBody(Point, Point + Vector(0, 0.01, 0));
   if (!Check(clear, "same point and geometry clear after wall deletion")) { Finish("clear after removal"); return; }
   Finish("completed");
  }
 }
}

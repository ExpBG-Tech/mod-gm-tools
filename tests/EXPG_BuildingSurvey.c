// Diagnostic survey: run the production Garrison planner on one building of
// every distinct prefab near the driver and report where planning stops.
// No actors are spawned and nothing is changed. Run with:
//   tests/Run-Gameplay.ps1 -FixturePath tests/EXPG_BuildingSurvey.c
// The cache verifier rejects this run by design; read [EXPG SURVEY] lines.
modded class EXPG_BuildingPlan
{
 int EXPG_SurveyDoors() { return m_TraversableDoors.Count(); }
 int EXPG_SurveyOpenings() { return m_Openings.Count(); }
 bool EXPG_SurveyInteriorBounds() { return m_InteriorBounds && !m_InteriorBounds.IsEmpty(); }

 // Why no door became an entrance: nearest same-floor node per door and each check.
 void EXPG_SurveyDoorReport(string prefab)
 {
  foreach (int d, EXPG_BuildingOpening opening : m_Openings)
  {
   if (!opening.Door) continue;
   vector door = opening.Position;
   int best = -1; float bestDistance = 99;
   foreach (int n, EXPG_BuildingNode node : Nodes)
   {
    if (door[1] < node.Position[1] + 0.2 || door[1] > node.Position[1] + 2.5) continue;
    float distance = vector.DistanceXZ(door, node.Position);
    if (distance < bestDistance) { bestDistance = distance; best = n; }
   }
   if (best < 0) { PrintFormat("[EXPG SURVEY DOOR] %1 door=%2 at=%3 nearest=none (no node on its floor)", prefab, d, door); continue; }
   vector point = Nodes[best].Position;
   vector flat = Vector(door[0] - point[0], 0, door[2] - point[2]);
   vector beyond = Vector(door[0], point[1], door[2]) + flat * (1.25 / Math.Max(flat.Length(), 0.1));
   bool interior = Inside(beyond, 0.1) && InteriorPoint(beyond, 0);
   float drop = point[1] - GetGame().GetWorld().GetSurfaceY(beyond[0], beyond[2]);
   bool clear = ClearBody(point, beyond, null, null, true);
   PrintFormat("[EXPG SURVEY DOOR] %1 door=%2 at=%3 nearest=%4 beyondInterior=%5 drop=%6 clear=%7", prefab, d, door, bestDistance, interior, drop, clear);
   // Replicate the doorway sweep (frame thin axis) and name the first blocker.
   vector frame[4]; opening.Part.GetWorldTransform(frame);
   vector mins, maxs; opening.Part.GetBounds(mins, maxs);
   vector normal = frame[2];
   if (maxs[0] - mins[0] < maxs[2] - mins[2]) normal = frame[0];
   normal[1] = 0; normal.Normalize();
   vector threshold = Vector(door[0], point[1], door[2]);
   if (vector.Dot(point - threshold, normal) < 0) normal = normal * -1;
   TraceBox sweep = new TraceBox();
   sweep.Start = threshold + normal * 0.7 + "0 0.45 0";
   sweep.End = threshold - normal * 0.8 + "0 0.45 0";
   sweep.Mins = "-0.23 0 -0.23"; sweep.Maxs = "0.23 1.35 0.23";
   sweep.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
   sweep.LayerMask = EPhysicsLayerDefs.CharacterAI;
   array<IEntity> exclusions = {}; exclusions.Copy(m_TraversableDoors); sweep.ExcludeArray = exclusions;
   float fraction = GetGame().GetWorld().TraceMove(sweep, null);
   bool listed;
   foreach (IEntity traversable : m_TraversableDoors) if (traversable && vector.Distance(traversable.GetOrigin(), door) < 1.5) listed = true;
   string blocker = "none";
   if (sweep.TraceEnt) blocker = SCR_ResourceNameUtils.GetPrefabName(sweep.TraceEnt) + " " + sweep.TraceEnt.ClassName();
   PrintFormat("[EXPG SURVEY SWEEP] %1 door=%2 fraction=%3 traversableNear=%4 blocker=%5", prefab, d, fraction, listed, blocker);
  }
 }
}

class EXPG_GarrisonGameplayClass : GenericEntityClass {}
class EXPG_GarrisonGameplay : GenericEntity
{
 static const float RADIUS = 1500;
 static const int MAX_PREFABS = 160;
 protected ref array<IEntity> m_Buildings = {};
 protected ref array<string> m_Prefabs = {};
 protected ref EXPG_BuildingPlan m_Plan;
 protected int m_Index = -1;
 protected float m_Started;
 protected float m_PlanStarted;
 protected int m_WithSlots;
 protected int m_NoNodes;
 protected int m_NoEntrance;
 protected int m_NoReachable;
 protected int m_NoInterior;
 protected int m_Other;

 void EXPG_GarrisonGameplay(IEntitySource src, IEntity parent) { SetEventMask(EntityEvent.INIT | EntityEvent.FRAME); }

 override void EOnInit(IEntity owner) { m_Started = GetGame().GetWorld().GetWorldTime() * 0.001; }

 protected bool AddBuilding(IEntity entity)
 {
  SCR_DestructibleBuildingEntity building = SCR_DestructibleBuildingEntity.Cast(entity);
  if (!building || building.GetParent()) return true;
  string prefab = SCR_ResourceNameUtils.GetPrefabName(building);
  if (prefab.IsEmpty() || m_Prefabs.Contains(prefab) || m_Prefabs.Count() >= MAX_PREFABS) return true;
  m_Prefabs.Insert(prefab);
  m_Buildings.Insert(building);
  return true;
 }

 protected void Next()
 {
  m_Index++;
  if (m_Index >= m_Buildings.Count())
  {
   PrintFormat("[EXPG SURVEY RESULT] buildings=%1 withSlots=%2 noNodes=%3 noEntrance=%4 noReachable=%5 noInterior=%6 other=%7", m_Buildings.Count(), m_WithSlots, m_NoNodes, m_NoEntrance, m_NoReachable, m_NoInterior, m_Other);
   GetGame().RequestClose();
   m_Plan = null;
   return;
  }
  m_Plan = new EXPG_BuildingPlan();
  m_Plan.Begin(m_Buildings[m_Index]);
  m_PlanStarted = GetGame().GetWorld().GetWorldTime() * 0.001;
 }

 protected void Report()
 {
  int entrances, reachable, interior, interiorReachable, fixedCount;
  foreach (EXPG_BuildingNode node : m_Plan.Nodes)
  {
   if (node.Entrance) entrances++;
   if (node.Reachable) reachable++;
   if (node.Interior) interior++;
   if (node.Reachable && node.Interior) interiorReachable++;
  }
  foreach (bool isFixed : m_Plan.FixedSlots) if (isFixed) fixedCount++;
  string stage = "ok";
  if (!m_Plan.Slots.IsEmpty()) m_WithSlots++;
  else if (m_Plan.Nodes.IsEmpty()) { stage = "noNodes"; m_NoNodes++; }
  else if (entrances == 0) { stage = "noEntrance"; m_NoEntrance++; }
  else if (reachable == 0) { stage = "noReachable"; m_NoReachable++; }
  else if (interiorReachable == 0) { stage = "noInterior"; m_NoInterior++; }
  else { stage = "other"; m_Other++; }
  IEntity building = m_Buildings[m_Index];
  PrintFormat("[EXPG SURVEY] %1 stage=%2 slots=%3 fixed=%4 nodes=%5 entrances=%6 reachable=%7", m_Prefabs[m_Index], stage, m_Plan.Slots.Count(), fixedCount, m_Plan.Nodes.Count(), entrances, reachable);
  PrintFormat("[EXPG SURVEY DETAIL] %1 interior=%2 interiorReachable=%3 authoredInterior=%4 doors=%5 openings=%6 origin=%7", m_Prefabs[m_Index], interior, interiorReachable, m_Plan.EXPG_SurveyInteriorBounds(), m_Plan.EXPG_SurveyDoors(), m_Plan.EXPG_SurveyOpenings(), building.GetOrigin());
  if (!m_Plan.Error.IsEmpty()) PrintFormat("[EXPG SURVEY ERROR] %1 %2", m_Prefabs[m_Index], m_Plan.Error);
  if (stage == "noEntrance" || (!m_Plan.Slots.IsEmpty() && m_Plan.Slots.Count() < 3 && m_Plan.Nodes.Count() > 40)) m_Plan.EXPG_SurveyDoorReport(m_Prefabs[m_Index]);
 }

 override void EOnFrame(IEntity owner, float timeSlice)
 {
  float now = GetGame().GetWorld().GetWorldTime() * 0.001;
  if (m_Index < 0)
  {
   if (now - m_Started < 10) return;
   GetGame().GetWorld().QueryEntitiesBySphere(GetOrigin(), RADIUS, AddBuilding);
   // Town centres read from the GM map grid: Saint-Philippe, Levie, Morton.
   GetGame().GetWorld().QueryEntitiesBySphere("4570 0 10700", 700, AddBuilding);
   GetGame().GetWorld().QueryEntitiesBySphere("7270 0 4700", 500, AddBuilding);
   GetGame().GetWorld().QueryEntitiesBySphere("5040 0 3950", 500, AddBuilding);
   PrintFormat("[EXPG SURVEY START] prefabs=%1 radius=%2 center=%3", m_Buildings.Count(), RADIUS, GetOrigin());
   Next();
   return;
  }
  if (!m_Plan) return;
  if (!m_Plan.Done && now - m_PlanStarted < 20)
  {
   m_Plan.Step(512);
   if (!m_Plan.Done) return;
  }
  if (!m_Plan.Done) m_Plan.Fail("survey timeout");
  Report();
  Next();
 }
}

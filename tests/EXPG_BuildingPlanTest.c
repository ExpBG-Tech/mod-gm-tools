// Native fixture only; never ship in the production addon.
// Run EXPG_BuildingPlanTest.Run() from a disposable Workbench test plugin.
class EXPG_BuildingPlanTest
{
 // Native fixtures authored before reservation implementation. NOT RUN.
 static bool ReservationGeometry()
 {
  if (!EXPG_BuildingPlan.RoutesConflict("0 0 0", "0.75 0 0", "0.75 0 0", "0 0 0")) { return false; }
  if (!EXPG_BuildingPlan.RoutesConflict("0 0 0", "0.75 0 0", "0.375 0 -0.75", "0.375 0 0.75")) { return false; }
  if (!EXPG_BuildingPlan.RoutesConflict("0 0 0", "0.75 0 0", "0.75 0 0", "0.75 0 0")) { return false; }
  if (EXPG_BuildingPlan.RoutesConflict("0 0 0", "0.75 0 0", "0 3 0", "0.75 3 0")) { return false; }
  return !EXPG_BuildingPlan.RoutesConflict("0 0 0", "0.75 0 0", "0 0 2", "0.75 0 2");
 }

 // Pass two isolated live fixture entities as reservation identities.
 static bool ReservationOwnership(IEntity first, IEntity second)
 {
  if (!first || !second || first == second) { return false; }
  EXPG_BuildingPlan plan = new EXPG_BuildingPlan();
  for (int i = 0; i < 4; i++)
  {
   EXPG_BuildingNode node = new EXPG_BuildingNode();
   node.Position = Vector(i * 0.75, 0, 0);
   node.Reachable = true;
   if (i > 0) { node.Links.Insert(i - 1); }
   if (i < 3) { node.Links.Insert(i + 1); }
   plan.Nodes.Insert(node);
  }
  if (!plan.ReserveNode(first, 0) || !plan.ReserveNode(second, 3)) { return false; }
  if (!plan.TryReserveEdge(first, 0, 1)) { return false; }
  if (plan.TryReserveEdge(second, 3, 2)) { return false; }
  // Failed second update must retain its previous node occupancy.
  if (plan.ReserveNode(first, 3)) { return false; }
  plan.ReleaseReservation(first);
  if (!plan.TryReserveEdge(second, 3, 2)) { return false; }
  if (plan.TryReserveEdge(first, 2, 3)) { return false; }
  plan.ReleaseReservation(second);
  return plan.TryReserveEdge(first, 2, 3);
 }

 static bool Run()
 {
  EXPG_BuildingPlan plan = new EXPG_BuildingPlan();
  for (int i = 0; i < 5; i++)
  {
   EXPG_BuildingNode node = new EXPG_BuildingNode();
   node.Position = Vector(i, 0, 0);
   plan.Nodes.Insert(node);
  }
  // Entrance -> ground floor -> stair landing; disconnected roof stays excluded.
  plan.Nodes[0].Entrance = true;
  plan.Nodes[0].Links.Insert(1);
  plan.Nodes[1].Links.Insert(0);
  plan.Nodes[1].Links.Insert(2);
  plan.Nodes[2].Links.Insert(1);
  plan.Nodes[3].Links.Insert(4);
  plan.Nodes[4].Links.Insert(3);
  plan.MarkReachable();
  bool passed = plan.Nodes[0].Reachable && plan.Nodes[1].Reachable && plan.Nodes[2].Reachable;
  passed = passed && ReservationGeometry();
  passed = passed && !plan.Nodes[3].Reachable && !plan.Nodes[4].Reachable;
  array<int> route = {};
  passed = passed && plan.Route(0, 2, route) && route.Count() == 3;
  passed = passed && route[0] == 0 && route[1] == 1 && route[2] == 2;
  passed = passed && !plan.Route(0, 4, route) && route.IsEmpty();
  plan.Nodes[0].Entrance = false;
  plan.MarkReachable();
  passed = passed && !plan.Nodes[0].Reachable && !plan.Nodes[2].Reachable;
  PrintFormat("[EXPG GRAPH RESULT] pass=%1", passed);
  return passed;
 }
}

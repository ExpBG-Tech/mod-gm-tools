// Native fixture only; never ship in the production addon.
// Run EXPG_BuildingPlanTest.Run() from a disposable Workbench test plugin.
class EXPG_SlotSelectionFixture : EXPG_BuildingPlan
{
 void Select() { SelectSlots(); }
}

class EXPG_BuildingPlanTest
{
 static bool NeighborColumns()
 {
  // Three columns x three rows. Diagonal links must not wrap across a row.
  array<int> middle = {5, 7, 8, 6};
  for (int edge = 0; edge < 4; edge++)
   if (EXPG_BuildingPlan.ForwardColumn(4, 3, 9, edge) != middle[edge]) return false;
  if (EXPG_BuildingPlan.ForwardColumn(2, 3, 9, 0) != -1 || EXPG_BuildingPlan.ForwardColumn(2, 3, 9, 2) != -1) return false;
  if (EXPG_BuildingPlan.ForwardColumn(3, 3, 9, 3) != -1) return false;
  return EXPG_BuildingPlan.ForwardColumn(8, 3, 9, 1) == -1 && EXPG_BuildingPlan.ForwardColumn(8, 3, 9, 3) == -1;
 }
 // Rejecting a nearby candidate must not discard later, well-spaced candidates.
 // The wide pass (2.5 m) takes 0 and 3 first; the 1.5 m pass then adds 2.
 static bool SlotSelection()
 {
  EXPG_SlotSelectionFixture plan = new EXPG_SlotSelectionFixture();
  array<vector> positions = {"0 0 0", "0.75 0 0", "1.5 0 0", "3 0 0"};
  foreach (vector position : positions)
  {
   EXPG_BuildingNode node = new EXPG_BuildingNode();
   node.Position = position;
   node.Reachable = true;
   node.Interior = true;
   plan.Nodes.Insert(node);
  }
  plan.Select();
  bool passed = plan.Slots.Count() == 3;
  if (passed) { passed = plan.Slots[0] == 0 && plan.Slots[1] == 3 && plan.Slots[2] == 2; }
  PrintFormat("[EXPG SLOT RESULT] pass=%1 expected=3 actual=%2", passed, plan.Slots.Count());
  // A roofed entrance/porch may connect the graph, but is not an indoor post.
  plan.Slots.Clear(); plan.FixedSlots.Clear(); plan.Nodes[0].Interior = false;
  plan.Select();
  passed = passed && plan.Slots.Count() == 2 && !plan.Slots.Contains(0);
  PrintFormat("[EXPG INTERIOR SLOT RESULT] pass=%1 expected=2 actual=%2", passed, plan.Slots.Count());
  return passed;
 }

 // A window post nearer its window wins over an earlier-scanned one behind it.
 static bool WindowPreference()
 {
  EXPG_SlotSelectionFixture plan = new EXPG_SlotSelectionFixture();
  array<vector> positions = {"0 0 0", "0.75 0 0", "5 0 0"};
  array<float> ranges = {2.5, 0.6, 1.0};
  foreach (int i, vector position : positions)
  {
   EXPG_BuildingNode node = new EXPG_BuildingNode();
   node.Position = position;
   node.Reachable = true;
   node.Interior = true;
   node.Score = EXPG_BuildingPlan.SCORE_WINDOW;
   node.Range = ranges[i];
   plan.Nodes.Insert(node);
  }
  plan.Select();
  bool passed = plan.Slots.Count() == 2 && plan.Slots[0] == 1 && plan.Slots[1] == 2 && plan.FixedSlots[0] && plan.FixedSlots[1];
  PrintFormat("[EXPG WINDOW SLOT RESULT] pass=%1 expected=2 actual=%2", passed, plan.Slots.Count());
  return passed;
 }

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
  passed = passed && ReservationGeometry() && NeighborColumns();
  passed = SlotSelection() && passed;
  passed = WindowPreference() && passed;
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

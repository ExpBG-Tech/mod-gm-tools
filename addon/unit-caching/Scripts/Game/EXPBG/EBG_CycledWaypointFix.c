// Reforger 1.8 can re-enter its waypoint-added invoker when editor parenting
// rebuilds a cycle. Defer cycle updates and keep the active native queue intact.
modded class SCR_EditableGroupComponent
{
 override void OnChildEntityChanged(SCR_EditableEntityComponent child, bool isAdded)
 {
  if (IsServer() && m_CycleWaypoint && isAdded && child && child.GetEntityType() == EEditableEntityType.WAYPOINT)
  {
   AIWaypoint waypoint = AIWaypoint.Cast(child.GetOwner());
   if (!waypoint) return;
   array<AIWaypoint> cycle = {}; m_CycleWaypoint.GetWaypoints(cycle);
   if (cycle.Contains(waypoint)) return;
   // Publish the ordered membership synchronously so immediate editor exports
   // include this waypoint. Only the reentrant native queue update is deferred.
   cycle.Insert(waypoint); m_CycleWaypoint.SetWaypoints(cycle);
   GetGame().GetCallqueue().CallLater(EBG_ApplyCycleChild, 1, false, child);
   return;
  }
  super.OnChildEntityChanged(child, isAdded);
 }
 protected void EBG_ApplyCycleChild(SCR_EditableEntityComponent child)
 {
  if (!GetGame() || !GetGame().InPlayMode() || !IsServer() || !GetOwner() || GetOwner().GetWorld() != GetGame().GetWorld()) return;
  if (!child || child.GetParentEntity() != this || !m_Group) return;
  if (!m_CycleWaypoint) { super.OnChildEntityChanged(child, true); return; }
  AIWaypoint waypoint = AIWaypoint.Cast(child.GetOwner());
  if (!waypoint) return;
  array<AIWaypoint> cycle = {}; m_CycleWaypoint.GetWaypoints(cycle);
  if (!cycle.Contains(waypoint)) return;
  array<AIWaypoint> queued = {}; m_Group.GetWaypoints(queued);
  // Move the cycle sentinel behind appended orders. Native removal by count
  // also removes the active order, skipping it when a GM appends a waypoint.
  if (queued.Contains(m_CycleWaypoint)) m_Group.RemoveWaypoint(m_CycleWaypoint);
  if (!queued.Contains(waypoint)) m_Group.AddWaypoint(waypoint);
  m_Group.AddWaypoint(m_CycleWaypoint);
  ReindexWaypoints();
 }
 override protected void AddWaypoints(array<AIWaypoint> waypoints)
 {
  // Native cycle-disable calls this while m_CycleWaypoint still exists. Keep
  // the current/first occurrence and pending order; never rebuild from index 0.
  if (!m_CycleWaypoint || !m_Group || !IsServer()) { super.AddWaypoints(waypoints); return; }
  array<AIWaypoint> queued = {}; m_Group.GetWaypoints(queued);
  // Keep existing native duplicates: even indexed removal also removes the
  // current occurrence of the same entity. This hook adds missing orders only.
  array<AIWaypoint> missing = {};
  foreach (AIWaypoint order : waypoints)
  {
   if (!order || queued.Contains(order)) continue;
   missing.Insert(order); queued.Insert(order);
  }
  // Preserve native toggle state, sentinel removal/deletion, reindex and RPC.
  super.AddWaypoints(missing);
 }
 override void OnDelete(IEntity owner)
 {
  if (GetGame()) GetGame().GetCallqueue().Remove(EBG_ApplyCycleChild);
  super.OnDelete(owner);
 }
}

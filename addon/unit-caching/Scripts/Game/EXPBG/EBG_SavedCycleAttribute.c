// Session-only attribute: generic GM exporters can retain cycle intent without
// a dependency on a particular save addon or another visible editor setting.
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EBG_SavedCycleAttribute : SCR_BaseEditorAttribute
{
 override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
 {
  if (manager) return null;
  SCR_EditableGroupComponent group = SCR_EditableGroupComponent.Cast(item);
  if (!group) return null;
  return SCR_BaseEditorAttributeVar.CreateBool(group.EBG_SavedCycleEnabled());
 }
 override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var, SCR_AttributesManagerEditorComponent manager, int playerID)
 {
  if (manager || playerID != -1 || !var || !Replication.IsServer() || !GetGame()) return;
  SCR_EditableGroupComponent group = SCR_EditableGroupComponent.Cast(item);
  if (group) group.EBG_QueueSavedCycle(var.GetBool());
 }
}

modded class SCR_EditableGroupComponent
{
 protected bool m_EBGHasSavedCycle, m_EBGSavedCycle;

 bool EBG_SavedCycleEnabled()
 {
  if (m_EBGHasSavedCycle) return m_EBGSavedCycle;
  return AreCycledWaypointsEnabled();
 }
 void EBG_QueueSavedCycle(bool enabled)
 {
  // Preserve immediate re-export intent while the synchronous importer adds
  // the group's child orders. Repeated writes replace this instance's callback.
  m_EBGHasSavedCycle = true; m_EBGSavedCycle = enabled;
  GetGame().GetCallqueue().Remove(EBG_ApplySavedCycle);
  GetGame().GetCallqueue().CallLater(EBG_ApplySavedCycle, 1, false);
 }
 protected void EBG_ApplySavedCycle()
 {
  if (!GetGame() || !GetGame().InPlayMode() || !IsServer() || !GetOwner() || GetOwner().GetWorld() != GetGame().GetWorld()) return;
  m_EBGHasSavedCycle = false;
  EnableCycledWaypoints(m_EBGSavedCycle);
 }
 override void OnDelete(IEntity owner)
 {
  if (GetGame()) GetGame().GetCallqueue().Remove(EBG_ApplySavedCycle);
  super.OnDelete(owner);
 }
}

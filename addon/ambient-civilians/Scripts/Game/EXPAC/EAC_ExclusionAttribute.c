[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAC_ExclusionAttribute : SCR_BaseValueListEditorAttribute
{
 [Attribute("0")] int m_Key;

 override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
 {
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(item);
  if (!editable) return null;
  EAC_ExclusionZone zone = EAC_ExclusionZone.Cast(editable.GetOwner());
  if (!zone) return null;
  return SCR_BaseEditorAttributeVar.CreateFloat(zone.GetSetting(m_Key));
 }

 override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var, SCR_AttributesManagerEditorComponent manager, int playerID)
 {
  if (!Replication.IsServer() || !var) return;
  if (!manager)
  {
   if (playerID != -1) return;
  }
  else
  {
   SCR_EditorManagerEntity editor = manager.GetManager();
   if (!editor || editor.GetPlayerID() != playerID || editor.IsLimited() || editor.GetCurrentMode() != EEditorMode.EDIT) return;
  }
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(item);
  if (!editable) return;
  EAC_ExclusionZone zone = EAC_ExclusionZone.Cast(editable.GetOwner());
  if (zone) zone.SetSetting(m_Key, Math.Round(var.GetFloat()));
 }
}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAC_ExclusionRadiusAttribute : EAC_ExclusionAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAC_ExclusionTransitAttribute : EAC_ExclusionAttribute {}

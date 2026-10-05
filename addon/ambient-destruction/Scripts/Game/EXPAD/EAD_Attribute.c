[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAD_Attribute : SCR_BaseValueListEditorAttribute
{
 [Attribute("0")] int m_Key;
 [Attribute()] ref array<ref SCR_EditorAttributeFloatStringValueHolder> m_aValues;
 override int GetEntries(notnull array<ref SCR_BaseEditorAttributeEntry> outEntries)
 {
  if (m_Key == 8 || m_Key == 10) return outEntries.Count();
  if (m_Key != 11) return super.GetEntries(outEntries);
  outEntries.Insert(new SCR_BaseEditorAttributeFloatStringValues(m_aValues));
  return outEntries.Count();
 }
 override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
 {
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(item);
  if (!editable) return null;
  EAD_Zone zone = EAD_Zone.Cast(editable.GetOwner());
  if (!zone) return null;
  // Native bool/int/float attribute values share the same serialized vector slot.
  if (m_Key == 8 || m_Key == 10) return SCR_BaseEditorAttributeVar.CreateBool(zone.GetSetting(m_Key) != 0);
  if (m_Key == 11) return SCR_BaseEditorAttributeVar.CreateInt(zone.GetSetting(m_Key));
  return SCR_BaseEditorAttributeVar.CreateFloat(zone.GetSetting(m_Key));
 }
 override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var, SCR_AttributesManagerEditorComponent manager, int playerID)
 {
  if (!Replication.IsServer() || !var) return;
  // CDF and native session restoration use this server-only system-write contract.
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
  EAD_Zone zone = EAD_Zone.Cast(editable.GetOwner());
  if (zone) zone.SetSetting(m_Key, Math.Round(var.GetFloat()));
 }
}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAD_RadiusAttribute : EAD_Attribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAD_DestructionAttribute : EAD_Attribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAD_ClutterAttribute : EAD_Attribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAD_WrecksAttribute : EAD_Attribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAD_BodiesAttribute : EAD_Attribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAD_WakeAttribute : EAD_Attribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAD_SleepAttribute : EAD_Attribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAD_SeedAttribute : EAD_Attribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAD_EnabledAttribute : EAD_Attribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAD_DebugAttribute : EAD_Attribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAD_DebugDrawAttribute : EAD_Attribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAD_BodyModeAttribute : EAD_Attribute {}

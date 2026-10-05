// One adapter for every protest zone setting; concrete types keep the editor's
// duplicate-type check intact. Keys match EAU_ProtestZone.KEY_*.
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAU_ZoneAttribute : SCR_BaseValueListEditorAttribute
{
 [Attribute("0")] int m_Key;

 protected bool IsSwitch() { return m_Key == EAU_ProtestZone.KEY_ENABLED || m_Key == EAU_ProtestZone.KEY_DEBUG; }

 protected static EAU_ProtestZone Zone(Managed item)
 {
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(item);
  if (!editable) return null;
  return EAU_ProtestZone.Cast(editable.GetOwner());
 }

 override int GetEntries(notnull array<ref SCR_BaseEditorAttributeEntry> outEntries)
 {
  if (IsSwitch()) return outEntries.Count();
  return super.GetEntries(outEntries);
 }

 override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
 {
  EAU_ProtestZone zone = Zone(item);
  if (!zone || m_Key < 0 || m_Key >= EAU_ProtestZone.SETTING_COUNT) return null;
  if (IsSwitch()) return SCR_BaseEditorAttributeVar.CreateBool(zone.GetSetting(m_Key) != 0);
  return SCR_BaseEditorAttributeVar.CreateFloat(zone.GetSetting(m_Key));
 }

 override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var, SCR_AttributesManagerEditorComponent manager, int playerID)
 {
  if (!Replication.IsServer() || !var) return;
  // Native and CDF session restore write without an editor manager and with player -1.
  if (!manager)
  {
   if (playerID != -1) return;
  }
  else
  {
   SCR_EditorManagerEntity editor = manager.GetManager();
   if (!editor || editor.GetPlayerID() != playerID || editor.IsLimited() || editor.GetCurrentMode() != EEditorMode.EDIT) return;
  }
  EAU_ProtestZone zone = Zone(item);
  if (!zone) return;
  // Native bool, int and float values share the same serialized slot.
  int value = Math.Round(var.GetFloat());
  zone.SetSetting(m_Key, value);
 }
}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAU_EnabledAttribute : EAU_ZoneAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAU_RadiusAttribute : EAU_ZoneAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAU_CrowdMinAttribute : EAU_ZoneAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAU_CrowdMaxAttribute : EAU_ZoneAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAU_DebugAttribute : EAU_ZoneAttribute {}

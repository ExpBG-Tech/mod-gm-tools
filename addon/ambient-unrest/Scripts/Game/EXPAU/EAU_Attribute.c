// One adapter for every protest zone setting; concrete types keep the editor's
// duplicate-type check intact. Keys match EAU_ProtestZone.KEY_*.
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAU_ZoneAttribute : SCR_BaseValueListEditorAttribute
{
 [Attribute("0")] int m_Key;
 // Crowd sound rows. The GM spinbox exchanges row indices; the zone and its saves
 // keep these permanent values, so the rows may be reordered later.
 [Attribute()] protected ref array<ref SCR_EditorAttributeFloatStringValueHolder> m_aValues;

 protected bool IsSwitch() { return m_Key == EAU_ProtestZone.KEY_ENABLED || m_Key == EAU_ProtestZone.KEY_DEBUG; }
 protected bool UsesValueList() { return m_Key == EAU_ProtestZone.KEY_SOUND; }

 protected static EAU_ProtestZone Zone(Managed item)
 {
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(item);
  if (!editable) return null;
  return EAU_ProtestZone.Cast(editable.GetOwner());
 }

 override int GetEntries(notnull array<ref SCR_BaseEditorAttributeEntry> outEntries)
 {
  if (IsSwitch()) return outEntries.Count();
  if (UsesValueList())
  {
   outEntries.Insert(new SCR_BaseEditorAttributeFloatStringValues(m_aValues));
   return outEntries.Count();
  }
  return super.GetEntries(outEntries);
 }

 override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
 {
  EAU_ProtestZone zone = Zone(item);
  if (!zone || m_Key < 0 || m_Key >= EAU_ProtestZone.SETTING_COUNT) return null;
  if (IsSwitch()) return SCR_BaseEditorAttributeVar.CreateBool(zone.GetSetting(m_Key) != 0);
  if (UsesValueList())
  {
   int durable = zone.GetSetting(m_Key);
   // A session save (no manager) stores the permanent value, never the row index.
   if (!manager) return SCR_BaseEditorAttributeVar.CreateInt(durable);
   if (!m_aValues) return null;
   foreach (int index, SCR_EditorAttributeFloatStringValueHolder entry : m_aValues)
   {
    if (entry.GetFloatValue() == durable) return SCR_BaseEditorAttributeVar.CreateInt(index);
   }
   return null;
  }
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
  if (UsesValueList() && manager)
  {
   // GM spinbox row index to its permanent value; an unknown row changes nothing.
   if (!m_aValues || value < 0 || value >= m_aValues.Count()) return;
   value = Math.Round(m_aValues[value].GetFloatValue());
  }
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
class EAU_CrowdSoundAttribute : EAU_ZoneAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAU_DebugAttribute : EAU_ZoneAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAU_WakeDistanceAttribute : EAU_ZoneAttribute {}

// Read-only status line (Off, Gathering, Protesting, Sleeping). Strings are not
// native attribute values: the replicated zone status is shown as the only,
// disabled entry. Never saved and never written back.
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAU_StatusAttribute : EAU_ZoneAttribute
{
 protected EAU_ProtestZone m_Inspected;
 protected ref array<ref SCR_EditorAttributeFloatStringValueHolder> m_StatusEntries = {};

 override bool IsServer() { return false; }
 override bool IsEnabled() { return false; }

 override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
 {
  if (!manager) return null;
  m_Inspected = Zone(item);
  if (!m_Inspected) return null;
  return SCR_BaseEditorAttributeVar.CreateFloat(0);
 }

 override int GetEntries(notnull array<ref SCR_BaseEditorAttributeEntry> outEntries)
 {
  string status = "Select one zone to see its status";
  if (m_Inspected && !GetIsMultiSelect()) status = m_Inspected.DescribeState();
  SCR_EditorAttributeFloatStringValueHolder entry = new SCR_EditorAttributeFloatStringValueHolder();
  entry.SetName(status);
  entry.SetFloatValue(0);
  m_StatusEntries.Clear();
  m_StatusEntries.Insert(entry);
  outEntries.Insert(new SCR_BaseEditorAttributeFloatStringValues(m_StatusEntries));
  return outEntries.Count();
 }

 override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var, SCR_AttributesManagerEditorComponent manager, int playerID) {}
}

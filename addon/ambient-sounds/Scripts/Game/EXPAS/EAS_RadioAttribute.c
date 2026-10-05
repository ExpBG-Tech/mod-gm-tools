// One native adapter; concrete types keep the editor's duplicate-type check intact.
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAS_RadioAttribute : SCR_BaseValueListEditorAttribute
{
 [Attribute("0")] protected int m_Key;
 [Attribute()] protected ref array<ref SCR_EditorAttributeFloatStringValueHolder> m_aValues;

 override bool IsSerializable() { return m_Key >= 0 && m_Key <= 6; }


 bool SupportsModule(EAS_RadioModule module) { return module && module.AudioKind() == 0; }

 override int GetEntries(notnull array<ref SCR_BaseEditorAttributeEntry> outEntries)
 {
  if (m_Key == 0) outEntries.Insert(new SCR_BaseEditorAttributeFloatStringValues(m_aValues));
  else if (m_Key == 1 || m_Key == 4 || m_Key == 5) return super.GetEntries(outEntries);
  return outEntries.Count();
 }

 override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
 {
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(item);
  if (!editable) return null;
  EAS_RadioModule module = EAS_RadioModule.Cast(editable.GetOwner());
  if (!SupportsModule(module) || m_Key < 0 || m_Key > 6) return null;
  if (!manager && m_Key == 2) module.TraceSettings("session-save");
  if (m_Key == 2 || m_Key == 3 || m_Key == 6) return SCR_BaseEditorAttributeVar.CreateBool(module.Value(m_Key) == 1);
  // Native spinboxes exchange row indices, not the configured recording IDs.
  if (m_Key == 0)
  {
   // Session saves store durable content IDs, never the UI's row ordering.
   if (!manager) return SCR_BaseEditorAttributeVar.CreateInt(module.Recording);
   foreach (int index, SCR_EditorAttributeFloatStringValueHolder entry : m_aValues)
    if (entry.GetFloatValue() == module.Recording) return SCR_BaseEditorAttributeVar.CreateInt(index);
   return null;
  }
  return SCR_BaseEditorAttributeVar.CreateFloat(module.Value(m_Key));
 }

 override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var, SCR_AttributesManagerEditorComponent manager, int playerID)
 {
  if (!Replication.IsServer() || !var || !IsSerializable()) return;
  if (!manager && playerID != -1) return;
  if (manager)
  {
   SCR_EditorManagerEntity editor = manager.GetManager();
   if (!editor || editor.GetPlayerID() != playerID || editor.IsLimited() || editor.GetCurrentMode() != EEditorMode.EDIT) return;
  }
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(item);
  if (!editable) return;
  EAS_RadioModule module = EAS_RadioModule.Cast(editable.GetOwner());
  if (!SupportsModule(module) || !module.IsAuthority()) return;
  vector raw = var.GetVector();
  float value = raw[0];
  if (m_Key == 0 && manager)
  {
   if (value != value || value < 0 || value != Math.Floor(value) || value >= m_aValues.Count()) value = -1;
   else value = m_aValues[value].GetFloatValue();
  }
  // Preserve atomic rejection: an invalid field poisons the entire deferred batch.
  if (raw[1] != 0 || raw[2] != 0 || !module.ValidSetting(m_Key, value)) value = -1;
  if (manager) module.QueueEditorSetting(m_Key, value, manager, playerID);
  else module.QueueSavedSetting(m_Key, value);
 }
}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAS_RadioRecordingAttribute : EAS_RadioAttribute {}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAS_RadioVolumeAttribute : EAS_RadioAttribute {}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAS_RadioEnabledAttribute : EAS_RadioAttribute {}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAS_RadioLoopAttribute : EAS_RadioAttribute {}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAS_RadioDebugAttribute : EAS_RadioAttribute {}

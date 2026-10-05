// One native adapter; concrete types keep the editor's duplicate-type check intact.
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAS_EditorAttribute : SCR_BaseValueListEditorAttribute
{
 [Attribute("0")] protected int m_Key;
 [Attribute()] protected ref array<ref SCR_EditorAttributeFloatStringValueHolder> m_aValues;

 // Native session/CDF saves use the same authored attribute identities.
 override bool IsSerializable() { return m_Key >= 0 && m_Key <= 22; }

 static bool ValidValue(int key, float value) { return EAS_ContentSettings.ValidValue(key, value); }
 static bool IsBoolean(int key) { return key == 6 || key == 7 || key == 8 || key == 10 || key == 12 || key == 14; }

 override int GetEntries(notnull array<ref SCR_BaseEditorAttributeEntry> outEntries)
 {
  if (m_Key <= 1 || m_Key == 9 || m_Key == 11 || m_Key == 13 || m_Key == 15 || m_Key >= 20) outEntries.Insert(new SCR_BaseEditorAttributeFloatStringValues(m_aValues));
  else if (m_Key < 6 || (m_Key >= 16 && m_Key <= 19)) return super.GetEntries(outEntries);
  return outEntries.Count();
 }

 override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
 {
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(item);
  if (!editable) return null;
  EAS_AmbientModule module = EAS_AmbientModule.Cast(editable.GetOwner());
  if (!module || m_Key < 0 || m_Key > 22) return null;
  // Retain legacy CDF/session identities without presenting a second gain/frequency.
  if (manager && (m_Key == 0 || m_Key == 1 || m_Key == 4)) return null;
  if (!manager && m_Key == 6) module.TraceSettings("session-save");
  if (IsBoolean(m_Key)) return SCR_BaseEditorAttributeVar.CreateBool(module.Value(m_Key) == 1);
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
  EAS_AmbientModule module = EAS_AmbientModule.Cast(editable.GetOwner());
  if (!module || !module.IsAuthority()) return;
  vector raw = var.GetVector();
  float value = raw[0];
  // Preserve atomic rejection: an invalid field poisons the entire deferred batch.
  if (raw[1] != 0 || raw[2] != 0 || !ValidValue(m_Key, value)) value = -2;
  if (manager) module.QueueEditorSetting(m_Key, value, manager, playerID);
  else module.QueueSavedSetting(m_Key, value);
 }
}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAS_SoundTypeAttribute : EAS_EditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAS_IntensityAttribute : EAS_EditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAS_SpreadAttribute : EAS_EditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAS_DistanceAttribute : EAS_EditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAS_VolumeAttribute : EAS_EditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAS_RunForAttribute : EAS_EditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAS_EnabledAttribute : EAS_EditorAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAS_DebugAttribute : EAS_EditorAttribute {}


[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAS_GunshotsEnabledAttribute : EAS_EditorAttribute {}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAS_GunshotsSetAttribute : EAS_EditorAttribute {}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAS_ExplosionsEnabledAttribute : EAS_EditorAttribute {}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAS_ExplosionsSetAttribute : EAS_EditorAttribute {}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAS_JetsEnabledAttribute : EAS_EditorAttribute {}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAS_JetSelectionAttribute : EAS_EditorAttribute {}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAS_PremixedEnabledAttribute : EAS_EditorAttribute {}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAS_PremixedSelectionAttribute : EAS_EditorAttribute {}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAS_GunshotsVolumeAttribute : EAS_EditorAttribute {}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAS_ExplosionsVolumeAttribute : EAS_EditorAttribute {}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAS_JetsVolumeAttribute : EAS_EditorAttribute {}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAS_PremixedVolumeAttribute : EAS_EditorAttribute {}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAS_GunshotsFrequencyAttribute : EAS_EditorAttribute {}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAS_ExplosionsFrequencyAttribute : EAS_EditorAttribute {}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAS_JetsFrequencyAttribute : EAS_EditorAttribute {}

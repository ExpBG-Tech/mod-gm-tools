class EXPG_GarrisonSettings
{
 // Metres; at least 25 m separation prevents repeated wake/sleep at one boundary.
 static vector Normalize(vector settings)
 {
  if (settings[0] != settings[0]) settings[0] = 300;
  if (settings[1] != settings[1]) settings[1] = 400;
  if (settings[2] != settings[2]) settings[2] = 1;
  settings[0] = Math.Clamp(settings[0], 50, 3000);
  settings[1] = Math.Clamp(settings[1], settings[0] + 25, 4000);
  settings[2] = Math.Round(Math.Clamp(settings[2], 0, 1));
  return settings;
 }
}

modded class SCR_AIGroup
{
 [RplProp(), NonSerialized()] bool EXPG_Active;
 [RplProp(), NonSerialized()] float EXPG_WakeDistance = 300;
 [RplProp(), NonSerialized()] float EXPG_SleepDistance = 400;
 [RplProp(), NonSerialized()] int EXPG_CacheMode = 1; // 0 off, 1 Simulation; original actors and group retained.
 [RplProp(), NonSerialized()] string EXPG_Status;

 void EXPG_Changed()
 {
  if (!Replication.IsClient()) Replication.BumpMe();
 }

 void EXPG_SetSetting(int key, float var)
 {
  if (Replication.IsClient() || !EXPG_Active) return;
  vector settings = Vector(EXPG_WakeDistance, EXPG_SleepDistance, EXPG_CacheMode);
  if (key == 0) settings[2] = var;
  else if (key == 1) settings[0] = var;
  else if (key == 2) settings[1] = var;
  else return;
  settings = EXPG_GarrisonSettings.Normalize(settings);
  EXPG_WakeDistance = settings[0];
  EXPG_SleepDistance = settings[1];
  EXPG_CacheMode = settings[2];
  EXPG_Changed();
 }
}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EXPG_GarrisonAttribute : SCR_BaseValueListEditorAttribute
{
 [Attribute("0")] protected int m_Key;
 [Attribute()] protected ref array<ref SCR_EditorAttributeFloatStringValueHolder> m_Choices;

 override bool IsSerializable() { return false; }

 protected SCR_AIGroup GetGarrison(Managed item)
 {
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(item);
  if (!editable) return null;
  SCR_AIGroup group = SCR_AIGroup.Cast(editable.GetOwner());
  if (!group || !group.EXPG_Active) return null;
  return group;
 }

 override int GetEntries(notnull array<ref SCR_BaseEditorAttributeEntry> outEntries)
 {
  if (m_Key == 0) outEntries.Insert(new SCR_BaseEditorAttributeFloatStringValues(m_Choices));
  else if (m_Key != 3) return super.GetEntries(outEntries);
  return outEntries.Count();
 }

 override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
 {
  if (!manager) return null; // Mission-only: no native/CDF save replay without the ownership ledger.
  SCR_AIGroup group = GetGarrison(item);
  if (!group) return null;
  if (m_Key == 0) return SCR_BaseEditorAttributeVar.CreateFloat(group.EXPG_CacheMode);
  if (m_Key == 1) return SCR_BaseEditorAttributeVar.CreateFloat(group.EXPG_WakeDistance);
  if (m_Key == 2) return SCR_BaseEditorAttributeVar.CreateFloat(group.EXPG_SleepDistance);
  if (m_Key == 3) return SCR_BaseEditorAttributeVar.CreateBool(false);
  return null;
 }

 override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var, SCR_AttributesManagerEditorComponent manager, int playerID)
 {
  if (Replication.IsClient() || !var || !manager) return;
  SCR_AIGroup group = GetGarrison(item);
  if (!group) return;
  SCR_EditorManagerEntity editor = manager.GetManager();
  if (!editor || editor.GetPlayerID() != playerID || editor.IsLimited() || !editor.IsOpened() || !editor.HasMode(EEditorMode.EDIT) || editor.GetCurrentMode() != EEditorMode.EDIT) return;
  SCR_EditorManagerCore core = SCR_EditorManagerCore.Cast(SCR_EditorManagerCore.GetInstance(SCR_EditorManagerCore));
  if (!core || core.GetEditorManager(playerID) != editor) return;
  if (m_Key == 3)
  {
   EXPG_GarrisonManager garrisons = EXPG_GarrisonManager.Get();
   if (var.GetBool() && garrisons) garrisons.Release(group);
   return;
  }
  group.EXPG_SetSetting(m_Key, var.GetFloat());
 }
}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EXPG_CacheModeAttribute : EXPG_GarrisonAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EXPG_WakeDistanceAttribute : EXPG_GarrisonAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EXPG_SleepDistanceAttribute : EXPG_GarrisonAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EXPG_ReleaseGarrisonAttribute : EXPG_GarrisonAttribute {}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EXPG_GarrisonStatusAttribute : EXPG_GarrisonAttribute
{
 protected SCR_AIGroup m_Group;
 protected ref array<ref SCR_EditorAttributeFloatStringValueHolder> m_StatusEntries = {};

 // Strings are not supported by native attribute variables. Display the already
 // replicated group status as the sole, disabled entry; never transmit an edit.
 override bool IsServer() { return false; }
 override bool IsEnabled() { return false; }

 override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
 {
  if (!manager) return null;
  m_Group = GetGarrison(item);
  if (!m_Group) return null;
  return SCR_BaseEditorAttributeVar.CreateFloat(0);
 }

 override int GetEntries(notnull array<ref SCR_BaseEditorAttributeEntry> outEntries)
 {
  string status = "Select one garrison to inspect its status";
  if (m_Group && !GetIsMultiSelect()) status = m_Group.EXPG_Status;
  if (status.IsEmpty()) status = "Preparing garrison";
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

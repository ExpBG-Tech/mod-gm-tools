// Native GM attribute registry. Distinct subclasses are required by the editor's duplicate-type checks.
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EBG_CacheAttribute : SCR_BaseValueListEditorAttribute
{
 [Attribute("0")] int m_Key;
 [Attribute("0")] bool m_Checkbox;
 [Attribute("0")] bool m_Choice;
 [Attribute()] ref array<ref SCR_EditorAttributeFloatStringValueHolder> m_aValues;
 override bool IsSerializable() { return m_Key < 100; }
 override int GetEntries(notnull array<ref SCR_BaseEditorAttributeEntry> outEntries)
 {
  if (m_Choice) outEntries.Insert(new SCR_BaseEditorAttributeFloatStringValues(m_aValues));
  else if (!m_Checkbox) return super.GetEntries(outEntries);
  return outEntries.Count();
 }
 override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
 {
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(item);
  if (!editable) return null;
  EBG_CacheZone zone = EBG_CacheZone.Cast(editable.GetOwner());
  if (!zone) return null;
  // Preserve registry indices and script actions; interactive save control is global.
  if (manager && (m_Key == 103 || m_Key == 104)) return null;
  // Session saves retain the operator's enabled choice, including a temporary save pause.
  if (m_Key == 0) return SCR_BaseEditorAttributeVar.CreateBool(zone.Enabled && (!manager || !zone.Editing || zone.EBG_HasSettingsLoadHold()));
  if (m_Checkbox) return SCR_BaseEditorAttributeVar.CreateBool(zone.GetValue(m_Key) != 0);
  return SCR_BaseEditorAttributeVar.CreateFloat(zone.GetValue(m_Key));
 }
 override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var, SCR_AttributesManagerEditorComponent manager, int playerID)
 {
  if (!Replication.IsServer() || !var) return;
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(item);
  if (!editable) return;
  EBG_CacheZone zone = EBG_CacheZone.Cast(editable.GetOwner());
  if (!zone) return;
  // Native session loaders (including CDF) have no editor manager or player.
  // These are server script calls; interactive edits still require the GM checks below.
  if (!manager)
  {
   if (playerID != -1 || !IsSerializable()) return;
   if (m_Checkbox) zone.EBG_QueueSavedAttribute(m_Key, var.GetBool());
   else zone.EBG_QueueSavedAttribute(m_Key, var.GetFloat());
   return;
  }
  SCR_EditorManagerEntity editor = manager.GetManager();
  if (!editor || editor.GetPlayerID() != playerID || editor.IsLimited() || editor.GetCurrentMode() != EEditorMode.EDIT) return;
  if (m_Checkbox)
  {
   if (m_Key >= 100 && !var.GetBool()) return;
   zone.SetValue(m_Key, var.GetBool());
  }
  else zone.SetValue(m_Key, var.GetFloat());
  // One plain-language notice to this GM after the zone's next enrollment pass.
  zone.EBG_RequestNotice(playerID);
 }
}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EBG_EnabledAttribute : EBG_CacheAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EBG_ModeAttribute : EBG_CacheAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EBG_StrategyAttribute : EBG_CacheAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EBG_AffectedAttribute : EBG_CacheAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EBG_ZoneWakeAttribute : EBG_CacheAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EBG_ZoneSleepAttribute : EBG_CacheAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EBG_GroupWakeAttribute : EBG_CacheAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EBG_GroupSleepAttribute : EBG_CacheAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EBG_HeightAttribute : EBG_CacheAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EBG_AboveAttribute : EBG_CacheAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EBG_BelowAttribute : EBG_CacheAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EBG_HeightMarginAttribute : EBG_CacheAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EBG_SleepDelayAttribute : EBG_CacheAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EBG_MinimumActiveAttribute : EBG_CacheAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EBG_CaptureNewAttribute : EBG_CacheAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EBG_CleanupAttribute : EBG_CacheAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EBG_CorpseAgeAttribute : EBG_CacheAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EBG_CleanupDelayAttribute : EBG_CacheAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EBG_MarkersAttribute : EBG_CacheAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EBG_ViewerAttribute : EBG_CacheAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EBG_MapAttribute : EBG_CacheAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EBG_RefreshAttribute : EBG_CacheAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EBG_RestoreAttribute : EBG_CacheAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EBG_ReleaseAttribute : EBG_CacheAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EBG_RestoreAllForSaveAttribute : EBG_CacheAttribute {}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EBG_DebugMessagesAttribute : EBG_CacheAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EBG_MilitaryOnlyAttribute : EBG_CacheAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EBG_CachedGroupMarkersAttribute : EBG_CacheAttribute {}

// Some mods expose global weather/vehicle attributes for every edited entity.
// A cache-only selection exposes only its own settings, preserving registry/RPC
// indices and leaving scenario properties and mixed selections unchanged.
modded class SCR_AttributesManagerEditorComponent
{
 override protected int GetVariables(bool onlyServer, notnull array<Managed> items, notnull out array<int> outIds, notnull out array<ref SCR_BaseEditorAttributeVar> outVars, notnull out array<ref EEditorAttributeMultiSelect> outAttributesMultiSelect)
 {
  super.GetVariables(onlyServer, items, outIds, outVars, outAttributesMultiSelect);
  if (items.IsEmpty()) return outVars.Count();
  foreach (Managed item : items)
  {
   SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(item);
   if (!editable || (!EBG_CacheZone.Cast(editable.GetOwner()) && !EBG_OptimizerController.Cast(editable.GetOwner()))) return outVars.Count();
  }
  for (int i = outIds.Count() - 1; i >= 0; i--)
  {
   if (EBG_CacheAttribute.Cast(m_PrefabData.GetAttribute(outIds[i]))) continue;
   outIds.Remove(i); outVars.Remove(i); outAttributesMultiSelect.Remove(i);
  }
  return outVars.Count();
 }
}

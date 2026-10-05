[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAC_AmbientAttribute : SCR_BaseValueListEditorAttribute
{
 [Attribute("0")] int m_Key;
 // A yes/no switch, or a named choice, instead of a slider. Same pattern the GM
 // Optimizer's cache-zone dialog uses, so both EXPBG modules read alike.
 [Attribute("0")] bool m_Checkbox;
 [Attribute("0")] bool m_Choice;
 [Attribute()] ref array<ref SCR_EditorAttributeFloatStringValueHolder> m_aValues;
 // A choice whose entries are found at run time (the civilian factions this mod
 // list offers) rather than written in the config.
 [Attribute("0")] bool m_Dynamic;

 override int GetEntries(notnull array<ref SCR_BaseEditorAttributeEntry> outEntries)
 {
  if (m_Choice && m_Dynamic)
  {
   // The native entry borrows this array; keep it alive after GetEntries returns.
   m_aValues = {};
   for (int i = 0; i < EAC_CivilianFactions.Count(); i++)
   {
    SCR_EditorAttributeFloatStringValueHolder holder = new SCR_EditorAttributeFloatStringValueHolder();
    holder.SetName(EAC_CivilianFactions.NameAt(i)); holder.SetFloatValue(i);
    m_aValues.Insert(holder);
   }
  }
  if (m_Choice) outEntries.Insert(new SCR_BaseEditorAttributeFloatStringValues(m_aValues));
  else if (!m_Checkbox) return super.GetEntries(outEntries);
  return outEntries.Count();
 }

 override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
 {
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(item);
  if (!editable) return null;
  EAC_AmbientModule module = EAC_AmbientModule.Cast(editable.GetOwner());
  if (!module) return null;
  if (m_Checkbox) return SCR_BaseEditorAttributeVar.CreateBool(module.GetSetting(m_Key) != 0);
  if (m_Key == 23)
  {
   if (module.SoundsEnabled <= 0) return SCR_BaseEditorAttributeVar.CreateFloat(0);
   return SCR_BaseEditorAttributeVar.CreateFloat(EAC_VoiceIntervalAttribute.PresetIndex(module.VoiceInterval));
  }
  return SCR_BaseEditorAttributeVar.CreateFloat(module.GetSetting(m_Key));
 }

 override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var, SCR_AttributesManagerEditorComponent manager, int playerID)
 {
  if (!Replication.IsServer() || !var) return;
  // Native session restore (including CDF) has no editor manager or player.
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
  EAC_AmbientModule module = EAC_AmbientModule.Cast(editable.GetOwner());
  if (!module) return;
  if (m_Checkbox) module.SetSwitch(m_Key, var.GetBool());
  else
  {
   int value = Math.Round(var.GetFloat());
   // Native spinboxes read and write a choice index, not the entry's float.
   if (m_Key == 23) value = EAC_VoiceIntervalAttribute.PresetInterval(value);
   module.SetSetting(m_Key, value);
   // One sound selector also honours profiles that used the old enable switch.
   if (m_Key == 23) module.SetSwitch(50, value > 0);
  }
 }
}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAC_PopulationLimitAttribute : EAC_AmbientAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAC_SmallHouseAttribute : EAC_AmbientAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAC_LargeHouseAttribute : EAC_AmbientAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAC_WakeDistanceAttribute : EAC_AmbientAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAC_SleepDistanceAttribute : EAC_AmbientAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAC_ClearDelayAttribute : EAC_AmbientAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAC_ThemeIndexAttribute : EAC_AmbientAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAC_ActivityLimitAttribute : EAC_AmbientAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAC_ActivityIntervalAttribute : EAC_AmbientAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAC_CalmDelayAttribute : EAC_AmbientAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAC_DebugLevelAttribute : EAC_AmbientAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAC_DebugDrawAttribute : EAC_AmbientAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAC_TrafficLimitAttribute : EAC_AmbientAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAC_TrafficSpeedAttribute : EAC_AmbientAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAC_TrafficDistanceAttribute : EAC_AmbientAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAC_TrafficPassengersAttribute : EAC_AmbientAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAC_TrafficIntervalAttribute : EAC_AmbientAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAC_TrafficStuckDelayAttribute : EAC_AmbientAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAC_TrafficSleepDistanceAttribute : EAC_AmbientAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAC_LocalPopulationLimitAttribute : EAC_AmbientAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAC_LocalPopulationRadiusAttribute : EAC_AmbientAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAC_LoadTargetFpsAttribute : EAC_AmbientAttribute {}
// Keys 22-32 existed in the runtime setting table from the start but had no editor
// entry, so a Game Master could not reach them. Each class below is an empty
// subclass exactly like the ones above: the server-authority gate in
// EAC_AmbientAttribute.WriteVariable is inherited unchanged, so there is one gate
// for every slider and no copy of it can drift.
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAC_IndoorSpawnDistanceAttribute : EAC_AmbientAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAC_VoiceIntervalAttribute : EAC_AmbientAttribute
{
 // Display old custom intervals as the nearest named choice without mutating
 // a mission merely because its GM properties were opened.
 static int PresetIndex(int interval)
 {
  if (interval <= 0) return 0;
  if (interval >= 23) return 1;
  if (interval >= 10) return 2;
  if (interval >= 3) return 3;
  return 4;
 }

 static int PresetInterval(int index)
 {
  switch (index)
  {
   case 1: return 30;
   case 2: return 15;
   case 3: return 5;
   case 4: return 1;
  }
  return 0;
 }
}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAC_IdleRecoveryAttribute : EAC_AmbientAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAC_IndoorSpawnShareAttribute : EAC_AmbientAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAC_RoutineRangeAttribute : EAC_AmbientAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAC_SceneSurveyAttribute : EAC_AmbientAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAC_SceneSurveyRateAttribute : EAC_AmbientAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAC_SceneSittingAttribute : EAC_AmbientAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAC_VanillaSeatingAttribute : EAC_AmbientAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAC_TableBiasAttribute : EAC_AmbientAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAC_VoiceRangeAttribute : EAC_AmbientAttribute {}
// Isolated houses (keys 33-36), the population floor (keys 37-39) and house
// recognition (key 40). Empty subclasses like every one above, so the
// server-authority gate in EAC_AmbientAttribute.WriteVariable is inherited
// unchanged and cannot drift.
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAC_HomeIsolationRuleAttribute : EAC_AmbientAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAC_HomeClusterMinAttribute : EAC_AmbientAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAC_HomeClusterRadiusAttribute : EAC_AmbientAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAC_SettlementRadiusAttribute : EAC_AmbientAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAC_MinLocalPopulationAttribute : EAC_AmbientAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAC_AdmissionsPerTickAttribute : EAC_AmbientAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAC_CatchUpAdmissionsAttribute : EAC_AmbientAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAC_HouseFilterAttribute : EAC_AmbientAttribute {}
// Roaming routine settings (keys 41-43).
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAC_RoutineStopsAttribute : EAC_AmbientAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAC_RoutineLegSpacingAttribute : EAC_AmbientAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAC_RoutineGapAttribute : EAC_AmbientAttribute {}
// Mission-start prewarm (keys 44-45). Empty subclasses like every one above, so
// the server-authority gate in EAC_AmbientAttribute.WriteVariable is inherited
// unchanged and cannot drift.
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAC_PrewarmModeAttribute : EAC_AmbientAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAC_PrewarmCellsAttribute : EAC_AmbientAttribute {}
// Idle watchdog deadline (key 46). Empty subclass like every one above, so the
// server-authority gate in EAC_AmbientAttribute.WriteVariable is inherited
// unchanged and cannot drift.
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAC_IdleForceAttribute : EAC_AmbientAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAC_DensityAttribute : EAC_AmbientAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAC_TrafficEnabledAttribute : EAC_AmbientAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAC_VoiceEnabledAttribute : EAC_AmbientAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAC_CivilianFactionAttribute : EAC_AmbientAttribute {}

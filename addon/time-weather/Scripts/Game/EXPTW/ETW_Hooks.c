// EXPBG Time and Weather hooks into the vanilla time and weather manager and the vanilla
// Scenario Properties weather attribute. Every override calls the vanilla code unchanged
// (or, for the smoothed weather change, replaces only the instant ForceWeatherTo with an
// EXPBG transition) and other mods' overrides stay in the chain.
modded class TimeAndWeatherManagerEntity
{
 //------------------------------------------------------------------------------------------------
 // The EXPBG transition never calls ForceWeatherTo, so every call is a foreign change
 // (Scenario Properties weather or automated weather, mission load, other mods).
 override void ForceWeatherTo(bool setLooping, string weatherID = string.Empty, float transitionDuration = 0, float stateDuration = 0.001, int playerThatChangedWeather = 0)
 {
  ETW_WeatherRunner.OnForeignWeather(playerThatChangedWeather);
  super.ForceWeatherTo(setLooping, weatherID, transitionDuration, stateDuration, playerThatChangedWeather);
 }

 //------------------------------------------------------------------------------------------------
 // Scenario Properties wind (the vanilla attribute goes through these delayed setters).
 override void DelayedSetWindOverride(bool overrideWind, int playerChangingWind = -1)
 {
  ETW_WeatherRunner.OnForeignWind();
  super.DelayedSetWindOverride(overrideWind, playerChangingWind);
 }

 //------------------------------------------------------------------------------------------------
 override void DelayedOverrideWindSpeed(float windSpeed, int playerChangingWind = -1)
 {
  ETW_WeatherRunner.OnForeignWind();
  super.DelayedOverrideWindSpeed(windSpeed, playerChangingWind);
 }

 //------------------------------------------------------------------------------------------------
 override void DelayedOverrideWindDirection(float windDirection, int playerChangingWind = -1)
 {
  ETW_WeatherRunner.OnForeignWind();
  super.DelayedOverrideWindDirection(windDirection, playerChangingWind);
 }

 //------------------------------------------------------------------------------------------------
 // Server: keep the vanilla "weather is looping" flag (saved by native persistence, read
 // by the Game Master UI) in step with an EXPBG transition, without forcing the state.
 void ETW_SetLoopingFlag(bool looping)
 {
  if (!Replication.IsServer() || looping == m_bWeatherIsLooping)
   return;
  UpdateWeatherLooping(looping);
  Rpc(UpdateWeatherLooping, looping);
 }

 //------------------------------------------------------------------------------------------------
 // Server: keep the replicated "automated wind disabled" flag that clients read in step
 // with the wind overrides an EXPBG transition sets or releases.
 void ETW_SetWindFlag(bool overridden)
 {
  if (!Replication.IsServer() || overridden == m_bDelayedWindOverride)
   return;
  m_bDelayedWindOverride = overridden;
  Replication.BumpMe();
  OnWindOverrideStateChanged();
 }
}

// Scenario Properties weather while a Weather Transition module has "Smooth Scenario
// Properties weather changes" ON: a Game Master's weather change blends over that module's
// duration instead of switching at once, and picking a weather shows no instant local
// preview (it jumped to the new sky and back on Save). Restores (no manager, player -1) and
// sessions without such a module keep the vanilla instant change and preview.
// A modded config class repeats the vanilla decorator; without it Edit.conf reports "Unknown
// class" and the vanilla weather attribute disappears from Scenario Properties.
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
modded class SCR_WeatherInstantEditorAttribute
{
 //------------------------------------------------------------------------------------------------
 override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var, SCR_AttributesManagerEditorComponent manager, int playerID)
 {
  if (item && var && manager && playerID > 0 && ETW_WeatherRunner.SmoothVanillaRequest(var.GetInt(), playerID))
   return;
  super.WriteVariable(item, var, manager, playerID);
 }

 //------------------------------------------------------------------------------------------------
 // Local, on the Game Master's machine: the module's smoothing switch is replicated.
 override void PreviewVariable(bool setPreview, SCR_AttributesManagerEditorComponent manager)
 {
  if (setPreview && ETW_WeatherModule.FindSmoothing())
  {
   super.PreviewVariable(false, manager);
   return;
  }
  super.PreviewVariable(setPreview, manager);
 }
}

// Some addons answer global attributes for every edited entity. When every edited item is
// a Weather Transition or Time Skip module, keep only this module's attribute classes. Runs
// wherever the manager collects variables, so id/value/state arrays stay aligned on server
// and owner; any other or mixed selection is left exactly as vanilla built it.
modded class SCR_AttributesManagerEditorComponent
{
 //------------------------------------------------------------------------------------------------
 protected bool ETW_OnlyModules(notnull array<Managed> items)
 {
  bool any;
  foreach (Managed item : items)
  {
   if (!item)
    continue;
   SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(item);
   if (!editable)
    return false;
   IEntity entity = editable.GetOwner();
   if (!ETW_WeatherModule.Cast(entity) && !ETW_TimeSkipModule.Cast(entity))
    return false;
   any = true;
  }
  return any;
 }

 //------------------------------------------------------------------------------------------------
 protected bool ETW_IsOwnAttribute(SCR_BaseEditorAttribute attribute)
 {
  if (ETW_Attribute.Cast(attribute) || ETW_WTargetAttribute.Cast(attribute))
   return true;
  if (ETW_TTextAttribute.Cast(attribute))
   return true;
  return false;
 }

 //------------------------------------------------------------------------------------------------
 override protected int GetVariables(bool onlyServer, notnull array<Managed> items, notnull out array<int> outIds, notnull out array<ref SCR_BaseEditorAttributeVar> outVars, notnull out array<ref EEditorAttributeMultiSelect> outAttributesMultiSelect)
 {
  int count = super.GetVariables(onlyServer, items, outIds, outVars, outAttributesMultiSelect);
  if (count <= 0 || !ETW_OnlyModules(items))
   return count;
  SCR_AttributesManagerEditorComponentClass data = SCR_AttributesManagerEditorComponentClass.Cast(GetEditorComponentData());
  if (!data || outIds.Count() != outVars.Count() || outIds.Count() != outAttributesMultiSelect.Count())
   return count;
  for (int i = outIds.Count() - 1; i >= 0; i--)
  {
   if (ETW_IsOwnAttribute(data.GetAttribute(outIds[i])))
    continue;
   outIds.RemoveOrdered(i);
   outVars.RemoveOrdered(i);
   outAttributesMultiSelect.RemoveOrdered(i);
  }
  return outVars.Count();
 }
}

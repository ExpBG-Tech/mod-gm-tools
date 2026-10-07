// Game Master attributes of the Weather Transition and Time Skip modules. Distinct
// subclasses are required by the editor's duplicate-type check; m_Key selects the
// setting (ETW_Settings). Settings are written on the server only for this player's own
// unlimited editor in Edit mode, or by a native or CDF restore (no manager, player -1).
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class ETW_Attribute : SCR_BaseValueListEditorAttribute
{
 [Attribute("0")]
 protected int m_Key;
 [Attribute("0")]
 protected bool m_Checkbox;
 [Attribute("0")]
 protected bool m_Choice;
 [Attribute()]
 protected ref array<ref SCR_EditorAttributeFloatStringValueHolder> m_aValues;

 //------------------------------------------------------------------------------------------------
 override int GetEntries(notnull array<ref SCR_BaseEditorAttributeEntry> outEntries)
 {
  if (m_Choice)
   outEntries.Insert(new SCR_BaseEditorAttributeFloatStringValues(m_aValues));
  else if (!m_Checkbox)
   return super.GetEntries(outEntries);
  return outEntries.Count();
 }

 //------------------------------------------------------------------------------------------------
 static bool IsAllowedWrite(SCR_AttributesManagerEditorComponent manager, int playerID)
 {
  if (!Replication.IsServer())
   return false;
  if (!manager)
   return playerID == -1;
  SCR_EditorManagerEntity editor = manager.GetManager();
  if (!editor || editor.GetPlayerID() != playerID || editor.IsLimited())
   return false;
  return editor.GetCurrentMode() == EEditorMode.EDIT;
 }

 //------------------------------------------------------------------------------------------------
 protected SCR_BaseEditorAttributeVar MakeVar(float value)
 {
  if (m_Checkbox)
   return SCR_BaseEditorAttributeVar.CreateBool(value != 0);
  return SCR_BaseEditorAttributeVar.CreateFloat(value);
 }

 //------------------------------------------------------------------------------------------------
 protected float VarValue(notnull SCR_BaseEditorAttributeVar var)
 {
  if (!m_Checkbox)
   return var.GetFloat();
  if (var.GetBool())
   return 1;
  return 0;
 }
}

//------------------------------------------------------------------------------------------------
// Weather Transition: settings 1-7, action 100.
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class ETW_WeatherAttribute : ETW_Attribute
{
 //------------------------------------------------------------------------------------------------
 override bool IsSerializable()
 {
  return m_Key > ETW_Settings.W_TARGET && m_Key < ETW_Settings.W_COUNT;
 }

 //------------------------------------------------------------------------------------------------
 protected ETW_WeatherModule Module(Managed item)
 {
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(item);
  if (!editable)
   return null;
  return ETW_WeatherModule.Cast(editable.GetOwner());
 }

 //------------------------------------------------------------------------------------------------
 override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
 {
  ETW_WeatherModule module = Module(item);
  if (!module)
   return null;
  if (m_Key == ETW_Settings.W_ACTION)
  {
   // One-time command: always reads "No extra action"; never part of a save.
   if (!manager)
    return null;
   return SCR_BaseEditorAttributeVar.CreateFloat(ETW_Settings.ACTION_NONE);
  }
  if (!IsSerializable())
   return null;
  return MakeVar(module.GetSetting(m_Key));
 }

 //------------------------------------------------------------------------------------------------
 override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var, SCR_AttributesManagerEditorComponent manager, int playerID)
 {
  if (!var || !IsAllowedWrite(manager, playerID))
   return;
  ETW_WeatherModule module = Module(item);
  if (!module)
   return;
  if (m_Key == ETW_Settings.W_ACTION)
  {
   int action = Math.Round(var.GetFloat());
   if (manager && action != ETW_Settings.ACTION_NONE)
    module.QueueAction(action, playerID);
   return;
  }
  if (!IsSerializable())
   return;
  module.ApplySetting(m_Key, VarValue(var));
  // Apply now: a confirmed change starts a transition after this Save. Restores never do,
  // and the Scenario Properties switch is a setting only.
  if (manager && m_Key != ETW_Settings.W_SMOOTH)
   module.QueueApply(playerID);
 }
}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class ETW_WMinutesAttribute : ETW_WeatherAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class ETW_WRainAttribute : ETW_WeatherAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class ETW_WFogAttribute : ETW_WeatherAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class ETW_WWindSpeedAttribute : ETW_WeatherAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class ETW_WWindDirectionAttribute : ETW_WeatherAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class ETW_WAfterAttribute : ETW_WeatherAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class ETW_WSmoothAttribute : ETW_WeatherAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class ETW_WActionAttribute : ETW_WeatherAttribute
{
 //------------------------------------------------------------------------------------------------
 override bool IsSerializable()
 {
  return false;
 }
}

//------------------------------------------------------------------------------------------------
// Weather Transition status (read-only): local on the Game Master's machine, from the
// module's replicated status. The text is the attribute description.
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class ETW_WStatusAttribute : ETW_Attribute
{
 //------------------------------------------------------------------------------------------------
 override bool IsSerializable()
 {
  return false;
 }

 //------------------------------------------------------------------------------------------------
 override void Initialize()
 {
  super.Initialize();
  Enable(false);
 }

 //------------------------------------------------------------------------------------------------
 override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
 {
  if (!manager)
   return null;
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(item);
  if (!editable)
   return null;
  ETW_WeatherModule module = ETW_WeatherModule.Cast(editable.GetOwner());
  if (!module)
   return null;
  SCR_EditorAttributeUIInfo info = GetUIInfo();
  if (info)
   info.SetDescription(module.GetStatusText() + " Reopen the settings to refresh.");
  return SCR_BaseEditorAttributeVar.CreateFloat(module.GetProgress());
 }

 //------------------------------------------------------------------------------------------------
 override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var, SCR_AttributesManagerEditorComponent manager, int playerID)
 {
 }
}

//------------------------------------------------------------------------------------------------
// Target weather: preset buttons like the vanilla Scenario Properties weather (one per
// weather state of the terrain). The value is the button index; the module keeps the state
// name. A module never set shows the current weather and keeps the clouds.
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class ETW_WTargetAttribute : SCR_BasePresetsEditorAttribute
{
 //------------------------------------------------------------------------------------------------
 protected ETW_WeatherModule Module(Managed item)
 {
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(item);
  if (!editable)
   return null;
  return ETW_WeatherModule.Cast(editable.GetOwner());
 }

 //------------------------------------------------------------------------------------------------
 override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
 {
  ETW_WeatherModule module = Module(item);
  TimeAndWeatherManagerEntity weather = ETW_WeatherRunner.Manager();
  if (!module || !weather)
   return null;
  array<ref WeatherState> states = {};
  weather.GetWeatherStatesList(states);
  if (states.IsEmpty())
   return null;
  int index = ETW_WeatherRunner.FindStateIndex(states, module.GetTargetName());
  if (index < 0)
  {
   // Nothing to save while the module keeps the clouds.
   if (!manager)
    return null;
   WeatherState current = weather.GetCurrentWeatherState();
   if (current)
    index = ETW_WeatherRunner.FindStateIndex(states, current.GetStateName());
  }
  if (index < 0)
   index = 0;
  return SCR_BaseEditorAttributeVar.CreateInt(index);
 }

 //------------------------------------------------------------------------------------------------
 override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var, SCR_AttributesManagerEditorComponent manager, int playerID)
 {
  // Previews (no item) are handled by PreviewVariable.
  if (!item || !var || !ETW_Attribute.IsAllowedWrite(manager, playerID))
   return;
  ETW_WeatherModule module = Module(item);
  TimeAndWeatherManagerEntity weather = ETW_WeatherRunner.Manager();
  if (!module || !weather)
   return;
  array<ref WeatherState> states = {};
  weather.GetWeatherStatesList(states);
  int index = var.GetInt();
  if (!states.IsIndexValid(index) || !states[index])
   return;
  module.SetTargetName(states[index].GetStateName());
  if (manager)
   module.QueueApply(playerID);
 }

 //------------------------------------------------------------------------------------------------
 // Local preview where the weather can preview (host and single player, like vanilla).
 override void PreviewVariable(bool setPreview, SCR_AttributesManagerEditorComponent manager)
 {
  TimeAndWeatherManagerEntity weather = ETW_WeatherRunner.Manager();
  if (!weather)
   return;
  BaseWeatherStateTransitionManager transitions = weather.GetTransitionManager();
  if (!transitions)
   return;
  if (!setPreview)
  {
   if (transitions.IsPreviewingState())
    weather.SetWeatherStatePreview(false);
   return;
  }
  SCR_BaseEditorAttributeVar var = GetVariable();
  if (!var)
   return;
  array<ref WeatherState> states = {};
  weather.GetWeatherStatesList(states);
  int index = var.GetInt();
  if (states.IsIndexValid(index) && states[index])
   weather.SetWeatherStatePreview(true, states[index].GetStateName());
 }

 //------------------------------------------------------------------------------------------------
 protected override void CreatePresets()
 {
  if (!m_aValues)
   m_aValues = new array<ref SCR_EditorAttributeFloatStringValueHolder>();
  m_aValues.Clear();
  TimeAndWeatherManagerEntity weather = ETW_WeatherRunner.Manager();
  if (!weather)
   return;
  array<ref WeatherState> states = {};
  weather.GetWeatherStatesList(states);
  for (int i = 0; i < states.Count(); i++)
  {
   if (!states[i])
    continue;
   SCR_EditorAttributeFloatStringValueHolder value = new SCR_EditorAttributeFloatStringValueHolder();
   string label = states[i].GetLocalizedName();
   if (label.IsEmpty())
    label = states[i].GetStateName();
   value.SetName(label);
   value.SetIcon(states[i].GetIconPath());
   value.SetFloatValue(i);
   m_aValues.Insert(value);
  }
 }
}

//------------------------------------------------------------------------------------------------
// Time Skip: settings 0-6, action 100.
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class ETW_SkipAttribute : ETW_Attribute
{
 //------------------------------------------------------------------------------------------------
 override bool IsSerializable()
 {
  return m_Key >= 0 && m_Key < ETW_Settings.T_COUNT;
 }

 //------------------------------------------------------------------------------------------------
 protected ETW_TimeSkipModule Module(Managed item)
 {
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(item);
  if (!editable)
   return null;
  return ETW_TimeSkipModule.Cast(editable.GetOwner());
 }

 //------------------------------------------------------------------------------------------------
 override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
 {
  ETW_TimeSkipModule module = Module(item);
  if (!module)
   return null;
  if (m_Key == ETW_Settings.T_ACTION)
  {
   // One-time command: always reads "No action"; never part of a save.
   if (!manager)
    return null;
   return SCR_BaseEditorAttributeVar.CreateFloat(ETW_Settings.SKIP_NONE);
  }
  if (!IsSerializable())
   return null;
  return MakeVar(module.GetSetting(m_Key));
 }

 //------------------------------------------------------------------------------------------------
 override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var, SCR_AttributesManagerEditorComponent manager, int playerID)
 {
  if (!var || !IsAllowedWrite(manager, playerID))
   return;
  ETW_TimeSkipModule module = Module(item);
  if (!module)
   return;
  if (m_Key == ETW_Settings.T_ACTION)
  {
   if (manager && Math.Round(var.GetFloat()) == ETW_Settings.SKIP_NOW)
    module.QueueSkip(playerID);
   return;
  }
  if (IsSerializable())
   module.ApplySetting(m_Key, VarValue(var));
 }
}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class ETW_THoursAttribute : ETW_SkipAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class ETW_TMinutesAttribute : ETW_SkipAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class ETW_TFadeOutAttribute : ETW_SkipAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class ETW_THoldAttribute : ETW_SkipAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class ETW_TFadeInAttribute : ETW_SkipAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class ETW_TShowTimeAttribute : ETW_SkipAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class ETW_TIncludeGmAttribute : ETW_SkipAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class ETW_TActionAttribute : ETW_SkipAttribute
{
 //------------------------------------------------------------------------------------------------
 override bool IsSerializable()
 {
  return false;
 }
}

//------------------------------------------------------------------------------------------------
// Time Skip status (read-only, local): Ready / Skipping now; the text is the description.
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class ETW_TStatusAttribute : ETW_Attribute
{
 //------------------------------------------------------------------------------------------------
 override bool IsSerializable()
 {
  return false;
 }

 //------------------------------------------------------------------------------------------------
 override void Initialize()
 {
  super.Initialize();
  Enable(false);
 }

 //------------------------------------------------------------------------------------------------
 override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
 {
  if (!manager)
   return null;
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(item);
  if (!editable)
   return null;
  ETW_TimeSkipModule module = ETW_TimeSkipModule.Cast(editable.GetOwner());
  if (!module)
   return null;
  SCR_EditorAttributeUIInfo info = GetUIInfo();
  if (info)
   info.SetDescription(module.GetStatusText() + " Reopen the settings to refresh.");
  if (module.IsBusy())
   return SCR_BaseEditorAttributeVar.CreateFloat(1);
  return SCR_BaseEditorAttributeVar.CreateFloat(0);
 }

 //------------------------------------------------------------------------------------------------
 override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var, SCR_AttributesManagerEditorComponent manager, int playerID)
 {
 }
}

//------------------------------------------------------------------------------------------------
// Time Skip text: the Unit Dialog single-line edit box (text travels in the EUD payload).
// Not part of attribute saves; the module's native serializer keeps it.
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class ETW_TTextAttribute : EUD_TextAttribute
{
 //------------------------------------------------------------------------------------------------
 override int GetLimit()
 {
  return ETW_Settings.TEXT_LIMIT;
 }

 //------------------------------------------------------------------------------------------------
 override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
 {
  if (!manager)
   return null;
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(item);
  if (!editable)
   return null;
  ETW_TimeSkipModule module = ETW_TimeSkipModule.Cast(editable.GetOwner());
  if (!module)
   return null;
  return SCR_BaseEditorAttributeVar.EUD_CreateText(module.GetText());
 }

 //------------------------------------------------------------------------------------------------
 override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var, SCR_AttributesManagerEditorComponent manager, int playerID)
 {
  if (!var || !manager || !ETW_Attribute.IsAllowedWrite(manager, playerID))
   return;
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(item);
  if (!editable)
   return;
  ETW_TimeSkipModule module = ETW_TimeSkipModule.Cast(editable.GetOwner());
  if (module && !module.SetText(var.EUD_GetText()))
   Print(string.Format("[ETW] time skip text rejected: maximum %1 characters", GetLimit()), LogLevel.WARNING);
 }
}

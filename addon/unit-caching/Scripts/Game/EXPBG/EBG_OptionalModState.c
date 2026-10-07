// Optional installed-mod state: no hard dependency on third-party script classes.
// Use inspected public APIs and verify each value after applying it. A changed
// component contract fails before deletion or retains the existing recovery.
class EBG_OptionalScalar
{
 string Component, Field, Setter;
 int Kind; // 0 int/enum, 1 float, 2 bool
 int IntValue;
 float FloatValue;
 bool BoolValue;
 int Parameter = -1;
 // Class layouts cannot change at runtime, so each (class, field) answer, misses
 // included, is remembered instead of scanning every script variable per call.
 protected static ref map<string, int> s_EBG_FieldIndexes;

 protected static int ScanFieldIndex(typename type, string field)
 {
  int count = type.GetVariableCount();
  if (count > 2048)
  {
   return -1;
  }
  for (int i = 0; i < count; i++)
  {
   if (type.GetVariableName(i) == field)
   {
    return i;
   }
  }
  return -1;
 }
 static int FieldIndex(Managed component, string field)
 {
  if (!component) return -1;
  EXPBG_LazyStatics_EBG_OptionalScalar();
  typename type = component.Type();
  string key = type.ToString() + ":" + field;
  int index;
  if (!s_EBG_FieldIndexes.Find(key, index))
  {
   index = ScanFieldIndex(type, field);
   s_EBG_FieldIndexes.Insert(key, index);
  }
  else if (EBG_DebugChecks.Enabled && index != ScanFieldIndex(type, field))
  {
   EBG_DebugChecks.Mismatch("optional field index " + key);
  }
  return index;
 }
 bool Read(Managed component)
 {
  int index = FieldIndex(component, Field);
  if (index < 0) return false;
  if (Kind == 1) return component.Type().GetVariableValue(component, index, FloatValue);
  if (Kind == 2) return component.Type().GetVariableValue(component, index, BoolValue);
  return component.Type().GetVariableValue(component, index, IntValue);
 }
 bool Matches(Managed component)
 {
  EBG_OptionalScalar actual = new EBG_OptionalScalar();
  actual.Kind = Kind; actual.Field = Field;
  if (!actual.Read(component)) return false;
  if (Kind == 1) return FloatValue == actual.FloatValue;
  if (Kind == 2) return BoolValue == actual.BoolValue;
  return IntValue == actual.IntValue;
 }
 bool Restore(Managed component)
 {
  if (Matches(component)) return true;
  if (Setter.IsEmpty()) return false;
  int ignored;
  bool called;
  if (Kind == 1) called = GetGame().GetScriptModule().Call(component, Setter, false, ignored, FloatValue);
  else if (Kind == 2) called = GetGame().GetScriptModule().Call(component, Setter, false, ignored, BoolValue);
  else if (Parameter >= 0) called = GetGame().GetScriptModule().Call(component, Setter, false, ignored, Parameter, IntValue);
  else if (Setter == "CycleModes")
  {
   EBG_OptionalScalar actual = new EBG_OptionalScalar(); actual.Field = Field;
   if (!actual.Read(component)) return false;
   // RHS public API advances the mode; obtain the current value before applying.
   called = GetGame().GetScriptModule().Call(component, Setter, false, ignored, (IntValue - actual.IntValue + 3) % 3);
  }
  else called = GetGame().GetScriptModule().Call(component, Setter, false, ignored, IntValue);
  return called && Matches(component);
 }

	//------------------------------------------------------------------------------------------------
	//! Creates the collections on first use (not in the global static initializer, which has a
	//! per-function instruction limit that large modsets exceed on Windows).
	protected static void EXPBG_LazyStatics_EBG_OptionalScalar()
	{
		if (!s_EBG_FieldIndexes)
			s_EBG_FieldIndexes = new map<string, int>();
	}
}

class EBG_OptionalModState
{
 protected static string ActiveAgentField(Managed component, string field, string getter, string reason)
 {
  if (EBG_OptionalScalar.FieldIndex(component, field) < 0) return "";
  bool active;
  // Released RO AI exposes the lock field but not every version has its getter.
  if (field == "m_bEXPBGWeaponLockActive")
  {
   EBG_OptionalScalar lock = new EBG_OptionalScalar(); lock.Field = field; lock.Kind = 2;
   if (!lock.Read(component)) return "EXPBG RO AI weapon lock could not be read";
   active = lock.BoolValue;
  }
  else if (!GetGame().GetScriptModule().Call(component, getter, false, active)) return "Optional AI operation contract could not be verified: " + getter;
  if (active) return reason;
  return "";
 }
 static string ActiveAgentOperation(AIAgent agent)
 {
  if (!agent || !agent.GetControlledEntity()) return "Native AI operation state not initialized";
  Managed combat = agent.GetControlledEntity().FindComponent(SCR_AICombatComponent);
  Managed info = agent.FindComponent(SCR_AIInfoComponent);
  // RO's stationary rocket commitment can outlive its trigger/item inputs.
  // Keep that native agent alive until its own public state says it is idle.
  string reason = ActiveAgentField(combat, "m_bEXPBGWeaponLockActive", "EXPBG_IsWeaponLocked", "EXPBG RO AI weapon lock still active");
  if (!reason.IsEmpty()) return reason;
  reason = ActiveAgentField(info, "m_EXPBG_RocketFiringLauncher", "EXPBG_HasRocketFiringState", "EXPBG RO AI rocket commitment still active");
  if (!reason.IsEmpty()) return reason;
  return ActiveAgentField(info, "m_bEXPBG_RestoreRifleRequested", "EXPBG_IsRifleRestoreRequested", "EXPBG RO AI rifle restoration still pending");
 }
 static string ActiveGroupOperation(SCR_AIGroup group)
 {
  if (!group) return "Missing native group";
  Managed info = group.FindComponent(SCR_AIGroupInfoComponent);
  if (EBG_OptionalScalar.FieldIndex(info, "m_fEXPBG_RocketSuppressionTimeout_ms") < 0) return "";
  float until;
  if (!GetGame().GetScriptModule().Call(info, "EXPBG_GetRocketSuppressionTimeout", false, until)) return "EXPBG RO AI group cooldown could not be verified";
  if (until > GetGame().GetWorld().GetWorldTime()) return "EXPBG RO AI group rocket cooldown still active";
  return "";
 }
 ref array<ref EBG_OptionalScalar> Values = {};
 string Error;
 protected ref EBG_FullIdentityMaterials m_Material;
 protected bool m_RhsLight;
 protected float m_LightLV, m_LightCone;
 protected int m_LightColor;
 protected bool m_LightEnabled;
 protected ref array<float> m_WeaponRails;
 protected bool ReadLight(IEntity entity, out float lv, out int packed)
 {
  Color color;
  if (!GetGame().GetScriptModule().Call(entity, "GetLV", false, lv) || !GetGame().GetScriptModule().Call(entity, "GetLightColor", false, color) || !color) return false;
  packed = color.PackToInt();
  return true;
 }
 protected bool NativeLightPreset(IEntity entity)
 {
  // Exact inspected .16.5150 ChargePro / ANPEQ child resources only. Reject
  // altered presets before deletion; this admission does not restore custom lights.
  ResourceName prefab = SCR_ResourceNameUtils.GetPrefabName(entity);
  float lv = -100; float cone = 39.133;
  Color palette = new Color(0.839, 0.944, 1, 0);
  if (prefab == "{51BD3CDB9E070694}Prefabs/Weapons/Attachments/Lasers/anpeq16/ANPEQ16_Iluminator_IR_HP_LightEntity.et") { lv = 6; cone = 25; }
  else if (prefab == "{2E88D1861AB3F2E2}Prefabs/Weapons/Attachments/Lasers/anpeq16/ANPEQ16_Iluminator_IR_LightEntity.et") { lv = 3; cone = 25; }
  else if (prefab == "{04D37D2BFD87A401}Prefabs/Weapons/Attachments/Lasers/anpeq16/ANPEQ16_Laser_IR_HP_LightEntity.et") { lv = 11; cone = 0.0395647; palette = new Color(1, 0, 0, 0); }
  else if (prefab == "{90A256F4E00AFBEF}Prefabs/Weapons/Attachments/Lasers/anpeq16/ANPEQ16_Laser_IR_LightEntity.et") { lv = 7.253; cone = 0.0395647; palette = new Color(1, 0, 0, 0); }
  else if (prefab == "{648E645C23EC2BF3}Prefabs/Weapons/Attachments/Lasers/anpeq16/ANPEQ16_Laser_LightEntity.et") { cone = 0.0395647; palette = new Color(0, 1, 0, 0); }
  else if (prefab != "{5CE8959505D80468}Prefabs/Items/Equipment/Nightvision/ChargePro/ChargePro_IR_lightEntity.et" && prefab != "{B485641A6178F443}Prefabs/Items/Equipment/Nightvision/ChargePro/ChargePro_lightEntity.et") return false;
  return !m_LightEnabled && m_LightLV == lv && m_LightColor == palette.PackToInt() && Math.AbsFloat(m_LightCone - cone) < 0.0001;
 }
 protected bool Add(Managed component, string field, string setter = "", int kind = 0, int parameter = -1)
 {
  EBG_OptionalScalar value = new EBG_OptionalScalar();
  value.Component = component.Type().ToString(); value.Field = field;
  value.Setter = setter; value.Kind = kind; value.Parameter = parameter;
  if (!value.Read(component)) { Error = "Optional mod field unavailable: " + value.Component + "." + field; return false; }
  Values.Insert(value);
  return true;
 }
 protected bool Idle(Managed component, string field)
 {
  if (!Add(component, field, "", 2)) return false;
  if (Values[Values.Count() - 1].BoolValue) { Error = "Mod operation still active: " + component.Type().ToString() + "." + field; return false; }
  return true;
 }
 protected bool DefaultInt(Managed component, string field, int expected)
 {
  if (!Add(component, field)) return false;
  if (Values[Values.Count() - 1].IntValue == expected) return true;
  Error = string.Format("Modified RHS device state needs a persistence adapter: %1 actual=%2 expected=%3", field, Values[Values.Count() - 1].IntValue, expected);
  return false;
 }
 protected bool CaptureWeaponRails(IEntity entity)
 {
  SCR_WeaponAttachmentsStorageComponent storage = SCR_WeaponAttachmentsStorageComponent.Cast(entity.FindComponent(SCR_WeaponAttachmentsStorageComponent));
  m_WeaponRails = {};
  if (!storage || !storage.EBG_ReadRHSRails(m_WeaponRails)) { Error = "RHS weapon rail state unavailable"; return false; }
  return true;
 }
 protected bool EmptyInts(Managed component, string field)
 {
  int index = EBG_OptionalScalar.FieldIndex(component, field);
  array<int> values;
  if (index < 0 || !component.Type().GetVariableValue(component, index, values) || !values || !values.IsEmpty())
  { Error = "Active or unverified mod medical state: " + field; return false; }
  return true;
 }
 bool Capture(IEntity entity)
 {
  m_RhsLight = entity.Type().ToString() == "RHS_LightEntity";
  if (m_RhsLight)
  {
   LightEntity light = LightEntity.Cast(entity);
   if (!light || !ReadLight(entity, m_LightLV, m_LightColor)) { Error = "RHS light state unavailable"; return false; }
   m_LightCone = light.GetConeAngle(); m_LightEnabled = light.IsEnabled();
   if (!NativeLightPreset(entity))
   { Error = string.Format("Modified RHS light preset requires an adapter (LV=%1 cone=%2 color=%3 enabled=%4)", m_LightLV, m_LightCone, m_LightColor, m_LightEnabled); return false; }
  }
  array<Managed> components = {};
  entity.FindComponents(GenericComponent, components);
  foreach (Managed component : components)
  {
   string type = component.Type().ToString();
   // User scope: AI only. CVON player radio preferences do not participate;
   // native inventory still owns and serializes the physical radio item.
   if (type == "RHS_WristWatchComponent")
   {
    if (!Add(component, "m_eGarminMode", "CycleModes")) return false;
    m_Material = new EBG_FullIdentityMaterials(); m_Material.Capture(entity);
   }
   else if (type == "RHS_HeadMountedLightDeviceComponent" || type == "RHS_SGC_ANPEQ15Component")
   {
    // Never destroy active timeout/strobe/suspension operations. These public
    // fields also verify that the restored inactive device has the same state.
    if (!Idle(component, "m_bIsTurnedOn") || !Add(component, "m_bIsSuspended", "SetSuspensionStatus", 2) || !Idle(component, "m_bPreSuspensionStatus")) return false;
    if (type == "RHS_HeadMountedLightDeviceComponent")
    { if (!Add(component, "m_eSelectedLightType", "AuthoritySyncLightType")) return false; }
    else if (!DefaultInt(component, "m_eSelectedLightType", 3)) return false;
    if (type == "RHS_SGC_ANPEQ15Component")
     if (!DefaultInt(component, "m_eCurrentMode", 0) || !DefaultInt(component, "m_iPulsePerSecond", 0) || !DefaultInt(component, "m_iSpotAngle", 25) || !Idle(component, "m_bIsAdjustingAngle")) return false;
   }
   else if (type == "RHS_2DPIPSightsComponent")
   {
    // The presentation callbacks do not set these fields and can require an
    // initialized ADS camera. Admit only native untouched AI scope state.
    if (!DefaultInt(component, "m_iSelectedZoomLevel", 0) || !Idle(component, "m_bIsIlluminationOn")) return false;
   }
   else if (type == "RHS_WeaponRplComponent") { if (!CaptureWeaponRails(entity)) return false; }
   else if (type == "GRS_IRIlluminatorComponent")
   {
    if (!Add(component, "m_eChannel", "SetChannel") || !Add(component, "m_ePowerLevel", "SetPowerLevel") || !Add(component, "m_eNVGGating", "SetNVGGating") || !Add(component, "m_bArmed", "SetArmed", 2)) return false;
   }
   else if (type == "GRS_DeviceStateComponent")
   {
    array<string> channels = {"m_iIRLaser", "m_iIRIlluminator", "m_iWhiteLight", "m_iVisibleLaser", "m_iIRStrobe", "m_iHelmetL", "m_iHelmetR", "m_iChest"};
    for (int channel = 0; channel < channels.Count(); channel++)
     if (!Add(component, channels[channel], "RequestSetChannel", 0, channel)) return false;
    if (!Add(component, "m_bNVGDeployed", "SetNVGDeployed", 2) || !Add(component, "m_iNVGColor", "SetNVGColor")) return false;
   }
   else if (type == "ACE_Medical_VitalsComponent")
   {
    if (!Idle(component, "m_bIsCPRPerformed") || !Idle(component, "m_bWasRevived")) return false;
    if (!Add(component, "m_eVitalStateID", "SetVitalStateID")) return false;
    array<string> fields = {"m_fHeartRateBPM", "m_fCardiacOutput", "m_fSystemicVascularResistance", "m_fMeanArterialPressureKPA", "m_fPulsePressureKPA", "m_fCoreTemperature", "m_fHeartRateMedicationAdjustment", "m_fSystemicVascularResistanceMedicationAdjustment", "m_fReviveSuccessCheckTimerScale"};
    array<string> setters = {"SetHeartRate", "SetCardiacOutput", "SetSystemicVascularResistance", "SetMeanArterialPressure", "SetPulsePressure", "SetTemperature", "SetHeartRateMedicationAdjustment", "SetSystemicVascularResistenceMedicationAdjustment", "SetReviveSuccessCheckTimerScale"};
    for (int f = 0; f < fields.Count(); f++) if (!Add(component, fields[f], setters[f], 1)) return false;
   }
   else if (type == "ACE_Medical_MedicationComponent")
   {
    if (!EmptyInts(component, "m_aDrugs")) return false;
    int logsIndex = EBG_OptionalScalar.FieldIndex(component, "m_aLogMessages");
    array<string> messages;
    if (logsIndex < 0 || !component.Type().GetVariableValue(component, logsIndex, messages) || !messages || !messages.IsEmpty()) { Error = "Mod medical history needs an explicit persistence adapter"; return false; }
   }
   else if (type == "EXPBG_UniformSwapComponent")
   {
    if (!Idle(component, "m_bSwapInProgress") || !Idle(component, "m_bHeadgearSwapInProgress") || !Idle(component, "m_bRollbackInProgress")) return false;
   }
   else if (type == "EXPBG_PatchManagerComponent") { if (!Idle(component, "m_bProcessing")) return false; }
   else if (type == "GRS_WeaponHolsterManagerComponent") { if (!Idle(component, "m_IsMoving") || !Idle(component, "m_WantHolster")) return false; }
   else if (type == "PGS_JammerPlayerComponent") { if (!Idle(component, "m_bCurrentlyJammed")) return false; }
   else if (type == "SMX_ItemDeviceCharComponent") { if (!Idle(component, "m_bActivateHeadCam")) return false; }
   else if (type == "BaconRISAttachments_2DPIPScopeIlluminatedTextureComponent")
   {
    if (!Add(component, "m_bIsIlluminationOn", "EnableReticleIllumination", 2) || !Add(component, "m_iSelectedZoomLevel", "SelectZoomLevel")) return false;
   }
  }
  return true;
 }
 bool Apply(IEntity entity, bool restore)
 {
  if (!entity) return false;
  if (m_WeaponRails)
  {
   SCR_WeaponAttachmentsStorageComponent rails = SCR_WeaponAttachmentsStorageComponent.Cast(entity.FindComponent(SCR_WeaponAttachmentsStorageComponent));
   if (!rails || (restore && !rails.EBG_RestoreRHSRails(m_WeaponRails)) || (!restore && !rails.EBG_RHSRailsMatch(m_WeaponRails)))
   { Error = "RHS attachment rail state differs"; return false; }
  }
  if (m_RhsLight)
  {
   LightEntity light = LightEntity.Cast(entity);
   float lv; int color;
   if (!light || !ReadLight(entity, lv, color) || lv != m_LightLV || color != m_LightColor || light.GetConeAngle() != m_LightCone || light.IsEnabled() != m_LightEnabled)
   { Error = "RHS regenerated light state differs; recovery retained"; return false; }
  }
  foreach (EBG_OptionalScalar value : Values)
  {
   Managed component = entity.FindComponent(value.Component.ToType());
   if (!component || (restore && !value.Restore(component)) || (!restore && !value.Matches(component)))
   { Error = "Optional mod state differs: " + value.Component + "." + value.Field; return false; }
  }
  if (m_Material && ((restore && !m_Material.Restore(entity)) || (!restore && !m_Material.Matches(entity))))
  { Error = "Optional gadget material state differs"; return false; }
  return true;
 }
}

// Native RHS slots persist their attached items, but their slide offsets are
// script state. Restore through RHS's public setter and publish to existing/JIP
// replicas. No RHS class is a hard dependency, and ordinary weapons stay idle.
modded class SCR_WeaponAttachmentsStorageComponent
{
 [RplProp(onRplName: "EBG_OnRHSRails"), NonSerialized()]
 protected ref array<float> m_EBG_RHSRails = {};
 protected int m_EBG_RailsAttempts;

 bool EBG_ReadRHSRails(out array<float> values)
 {
  if (!values) values = {};
  values.Clear();
  int count = GetSlotsCount();
  if (count < 1 || count > 128) return false;
  for (int i = 0; i < count; i++)
  {
   InventoryStorageSlot slot = GetSlot(i);
   if (!slot) return false;
   float value = 10000; // Non-sliding slot; preserve its own native behavior.
   if (slot.Type().ToString() == "RHS_SlidingInventoryStorageSlot")
    if (!GetGame().GetScriptModule().Call(slot, "GetCurrentSlideOffset", false, value) || value < -1 || value > 1) return false;
   values.Insert(value);
  }
  return true;
 }
 bool EBG_RHSRailsMatch(array<float> expected)
 {
  array<float> current = {};
  if (!expected || !EBG_ReadRHSRails(current) || current.Count() != expected.Count()) return false;
  for (int i = 0; i < current.Count(); i++) if (current[i] != expected[i]) return false;
  return true;
 }
 bool EBG_PreflightRHSRails(array<float> values, out array<float> previous)
 {
  if (!EBG_ReadRHSRails(previous) || !values || previous.Count() != values.Count()) return false;
  for (int i = 0; i < values.Count(); i++)
  {
   if (!EBG_MissionPersistence.Finite(values[i]) || !EBG_MissionPersistence.Finite(previous[i])) return false;
   if (previous[i] == 10000) { if (values[i] != 10000) return false; }
   else if (values[i] < -1 || values[i] > 1) return false;
  }
  return true;
 }
 bool EBG_ApplyMissionRails(array<float> values) { return Replication.IsServer() && EBG_ApplyRHSRails(values); }
 void EBG_PublishMissionRails()
 {
  EBG_SampleRHSRails();
  GetGame().GetCallqueue().Remove(EBG_SampleRHSRails);
  GetGame().GetCallqueue().CallLater(EBG_SampleRHSRails, 1000, true);
 }
 protected bool EBG_ApplyRHSRails(array<float> values)
 {
  if (!values || values.Count() != GetSlotsCount() || values.IsEmpty() || values.Count() > 128) return EBG_RailsFailure("count");
  for (int i = 0; i < values.Count(); i++)
  {
   InventoryStorageSlot slot = GetSlot(i);
   if (!slot) return EBG_RailsFailure("slot", i);
   float value = values[i];
   if (slot.Type().ToString() == "RHS_SlidingInventoryStorageSlot")
   {
    if (value < -1 || value > 1) return EBG_RailsFailure("range", i);
    float current; int ignored;
    if (!GetGame().GetScriptModule().Call(slot, "GetCurrentSlideOffset", false, current)) return EBG_RailsFailure("getter", i);
    if (current != value)
    {
     bool called = GetGame().GetScriptModule().Call(slot, "SetSlideOffset", false, ignored, value);
#ifdef EBG_ACCEPTANCE_TEST
     if (Replication.IsServer()) PrintFormat("[EBG RHS RAIL SET] slot=%1 current=%2 requested=%3 called=%4 type=%5", i, current, value, called, slot.Type().ToString());
#endif
     if (!called) return EBG_RailsFailure("setter", i);
    }
   }
   else if (value != 10000) return EBG_RailsFailure("type", i);
  }
  return EBG_RHSRailsMatch(values);
 }
 protected bool EBG_RailsFailure(string phase, int slot = -1)
 {
#ifdef EBG_ACCEPTANCE_TEST
  if (Replication.IsServer()) PrintFormat("[EBG RHS RAIL APPLY FAILURE] phase=%1 slot=%2", phase, slot);
#endif
  return false;
 }
 bool EBG_RestoreRHSRails(array<float> values)
 {
  if (!Replication.IsServer()) return false;
  if (!EBG_ApplyRHSRails(values))
  {
#ifdef EBG_ACCEPTANCE_TEST
   array<float> actual = {}; bool readable = EBG_ReadRHSRails(actual);
   PrintFormat("[EBG RHS RAIL DIFF] owner=%1 expectedCount=%2 actualCount=%3 readable=%4", SCR_ResourceNameUtils.GetPrefabName(GetOwner()), values.Count(), actual.Count(), readable);
   for (int i = 0; i < Math.Min(values.Count(), actual.Count()); i++)
    if (values[i] != actual[i]) PrintFormat("[EBG RHS RAIL DIFF] slot=%1 expected=%2 actual=%3 slotType=%4 attachment=%5", i, values[i], actual[i], GetSlot(i).Type(), SCR_ResourceNameUtils.GetPrefabName(GetSlot(i).GetAttachedEntity()));
#endif
   return false;
  }
  EBG_SampleRHSRails();
  GetGame().GetCallqueue().Remove(EBG_SampleRHSRails);
  GetGame().GetCallqueue().CallLater(EBG_SampleRHSRails, 1000, true);
  return true;
 }
 protected void EBG_SampleRHSRails()
 {
  if (!Replication.IsServer()) return;
  array<float> current = {};
  if (!EBG_ReadRHSRails(current)) return;
  bool changed = current.Count() != m_EBG_RHSRails.Count();
  if (!changed)
   for (int i = 0; i < current.Count(); i++) if (current[i] != m_EBG_RHSRails[i]) changed = true;
  if (!changed) return;
  m_EBG_RHSRails.Copy(current);
  Replication.BumpMe();
 }
 protected void EBG_OnRHSRails()
 {
  if (Replication.IsServer() || !GetGame() || m_EBG_RHSRails.IsEmpty()) return;
  m_EBG_RailsAttempts = 0;
  GetGame().GetCallqueue().Remove(EBG_TryRHSRails);
  GetGame().GetCallqueue().CallLater(EBG_TryRHSRails, 50, true);
 }
 protected void EBG_TryRHSRails()
 {
  m_EBG_RailsAttempts++;
  if (EBG_ApplyRHSRails(m_EBG_RHSRails) || m_EBG_RailsAttempts >= 100)
   GetGame().GetCallqueue().Remove(EBG_TryRHSRails);
 }
 override void OnPostInit(IEntity owner)
 {
  super.OnPostInit(owner);
  EBG_OnRHSRails();
 }
 void ~SCR_WeaponAttachmentsStorageComponent()
 {
  if (!GetGame()) return;
  GetGame().GetCallqueue().Remove(EBG_SampleRHSRails);
  GetGame().GetCallqueue().Remove(EBG_TryRHSRails);
 }
}

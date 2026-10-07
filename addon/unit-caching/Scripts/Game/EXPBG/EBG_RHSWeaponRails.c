// Native RHS slots persist their attached items, but their slide offsets are
// script state. Restore through RHS's public setter and publish to existing/JIP
// replicas. No RHS class is a hard dependency, and ordinary weapons stay idle.
modded class SCR_WeaponAttachmentsStorageComponent
{
 [RplProp(onRplName: "EBG_OnRHSRails"), NonSerialized()]
 protected ref array<float> m_EBG_RHSRails = {};
 protected int m_EBG_RailsAttempts;
 // One shared server sampler for every published rail storage, in place of one 1 s
 // timer per storage for its whole life. A cycle of four 250 ms steps visits each
 // registered storage once (about once per second, as before) with the unchanged
 // compare-and-bump, a quarter of them per step, so there is no synchronised spike.
 // Weak entries: destroyed storages drop out when the cursor reaches them.
 protected static ref array<SCR_WeaponAttachmentsStorageComponent> s_EBG_RailSamplers;
 protected static int s_EBG_RailCursor;
 protected static int s_EBG_RailPhase;
 protected static int s_EBG_RailDue;
 protected static int s_EBG_RailBudget;
 protected static bool s_EBG_RailArmed;
 protected static World s_EBG_RailWorld;
 protected bool m_EBG_RailListed;

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
  EBG_RegisterRailSampler(this);
 }
 // Adds a storage to the shared sampler once and arms the sampler when it is idle.
 protected static void EBG_RegisterRailSampler(SCR_WeaponAttachmentsStorageComponent storage)
 {
  EXPBG_LazyStatics_SCR_WeaponAttachmentsStorageComponent();
  if (!storage || !GetGame()) return;
  // A new world starts the cycle over and re-arms the one shared timer.
  if (EBG_RailSamplerWorldChanged()) s_EBG_RailArmed = false;
  if (!storage.m_EBG_RailListed)
  {
   storage.m_EBG_RailListed = true;
   s_EBG_RailSamplers.Insert(storage);
  }
  if (s_EBG_RailArmed) return;
  s_EBG_RailArmed = true;
  GetGame().GetCallqueue().Remove(EBG_StepRailSamplers);
  GetGame().GetCallqueue().CallLater(EBG_StepRailSamplers, 250, true);
 }
 protected static bool EBG_RailSamplerWorldChanged()
 {
  World world = GetGame().GetWorld();
  if (!world || world == s_EBG_RailWorld)
  {
   return false;
  }
  s_EBG_RailWorld = world;
  s_EBG_RailCursor = 0;
  s_EBG_RailPhase = 0;
  return true;
 }
 protected static void EBG_StepRailSamplers()
 {
  EXPBG_LazyStatics_SCR_WeaponAttachmentsStorageComponent();
  if (!GetGame()) return;
  EBG_RailSamplerWorldChanged();
  if (s_EBG_RailSamplers.IsEmpty())
  {
   GetGame().GetCallqueue().Remove(EBG_StepRailSamplers);
   s_EBG_RailArmed = false;
   s_EBG_RailPhase = 0;
   return;
  }
  // Each cycle covers the storages registered when it starts, ceil(n / 4) per step.
  if (s_EBG_RailPhase == 0)
  {
   s_EBG_RailDue = s_EBG_RailSamplers.Count();
   s_EBG_RailBudget = (s_EBG_RailDue + 3) / 4;
  }
  int visits = s_EBG_RailBudget;
  if (visits > s_EBG_RailDue) visits = s_EBG_RailDue;
  while (visits > 0 && !s_EBG_RailSamplers.IsEmpty())
  {
   visits--;
   s_EBG_RailDue--;
   if (s_EBG_RailCursor >= s_EBG_RailSamplers.Count()) s_EBG_RailCursor = 0;
   SCR_WeaponAttachmentsStorageComponent storage = s_EBG_RailSamplers[s_EBG_RailCursor];
   if (!storage)
   {
    s_EBG_RailSamplers.RemoveOrdered(s_EBG_RailCursor);
    continue;
   }
   s_EBG_RailCursor++;
   storage.EBG_SampleRHSRails();
  }
  s_EBG_RailPhase = (s_EBG_RailPhase + 1) % 4;
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
  EBG_RegisterRailSampler(this);
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
  GetGame().GetCallqueue().Remove(EBG_TryRHSRails);
 }

	//------------------------------------------------------------------------------------------------
	//! Creates the collections on first use (not in the global static initializer, which has a
	//! per-function instruction limit that large modsets exceed on Windows).
	protected static void EXPBG_LazyStatics_SCR_WeaponAttachmentsStorageComponent()
	{
		if (!s_EBG_RailSamplers)
			s_EBG_RailSamplers = new array<SCR_WeaponAttachmentsStorageComponent>();
	}
}

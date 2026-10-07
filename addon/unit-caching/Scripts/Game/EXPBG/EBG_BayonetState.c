// Native attachment serialization omits this script accumulator and materials.
// Preserve them for Full restoration and stream the current state to late clients.
modded class SCR_BayonetComponent
{
 [RplProp(onRplName: "EBG_OnBayonetNetState"), NonSerialized()]
 protected float m_EBG_NetBlood;
 [RplProp(onRplName: "EBG_OnBayonetNetState"), NonSerialized()]
 protected ref array<int> m_EBG_NetMaterial = {};
 protected int m_EBG_NetAttempts;
 // One shared server sampler for every published bayonet, in place of one 1 s timer
 // per bayonet for its whole life. A cycle of four 250 ms steps visits each registered
 // bayonet once (about once per second, as before) with the unchanged compare-and-bump,
 // a quarter of them per step. Weak entries: destroyed bayonets drop out when reached.
 protected static ref array<SCR_BayonetComponent> s_EBG_BayonetSamplers;
 protected static int s_EBG_BayonetCursor;
 protected static int s_EBG_BayonetPhase;
 protected static int s_EBG_BayonetDue;
 protected static int s_EBG_BayonetBudget;
 protected static bool s_EBG_BayonetArmed;
 protected static World s_EBG_BayonetWorld;
 protected bool m_EBG_BayonetListed;

 static array<int> EBG_PackMaterial(EBG_FullIdentityMaterials material)
 {
  array<int> values = {};
  if (!material || !material.Present || material.Values.Count() != 10) return values;
  foreach (int value : material.Values) values.Insert(value);
  values.Insert(material.Wetness); values.Insert(material.Drops);
  return values;
 }
 static bool EBG_MaterialPacketMatches(array<int> packet, EBG_FullIdentityMaterials material)
 {
  if (!packet || packet.Count() != 12 || !material || !material.Present || material.Values.Count() != 10) return false;
  for (int i = 0; i < 10; i++) if (packet[i] != material.Values[i]) return false;
  return packet[10] == material.Wetness && packet[11] == material.Drops;
 }
 static EBG_FullIdentityMaterials EBG_UnpackMaterial(array<int> packet)
 {
  if (!packet || packet.Count() != 12 || packet[10] < 0 || packet[10] > 1 || packet[11] < 0 || packet[11] > 1) return null;
  EBG_FullIdentityMaterials material = new EBG_FullIdentityMaterials();
  material.Present = true; material.Values = {};
  for (int i = 0; i < 10; i++) material.Values.Insert(packet[i]);
  material.Wetness = packet[10] != 0; material.Drops = packet[11] != 0;
  return material;
 }
 // GenericEntity/ScriptComponent declare empty replication events. RplProp
 // carries initial and later state without claiming those callback chains.
 protected void EBG_SampleBayonetNetState()
 {
  if (!Replication.IsServer() || !GetOwner() || EBG_CacheManager.Unloading) return;
  EBG_FullIdentityMaterials material = new EBG_FullIdentityMaterials(); material.Capture(GetOwner());
  if (!material.Present || (m_EBG_NetBlood == m_fBloodStainLevel && EBG_MaterialPacketMatches(m_EBG_NetMaterial, material))) return;
  m_EBG_NetBlood = m_fBloodStainLevel;
  m_EBG_NetMaterial = EBG_PackMaterial(material);
  Replication.BumpMe();
 }
 // Adds a bayonet to the shared sampler once and arms the sampler when it is idle.
 protected static void EBG_RegisterBayonetSampler(SCR_BayonetComponent bayonet)
 {
  EXPBG_LazyStatics_SCR_BayonetComponent();
  if (!bayonet || !GetGame()) return;
  // A new world starts the cycle over and re-arms the one shared timer.
  if (EBG_BayonetSamplerWorldChanged()) s_EBG_BayonetArmed = false;
  if (!bayonet.m_EBG_BayonetListed)
  {
   bayonet.m_EBG_BayonetListed = true;
   s_EBG_BayonetSamplers.Insert(bayonet);
  }
  if (s_EBG_BayonetArmed) return;
  s_EBG_BayonetArmed = true;
  GetGame().GetCallqueue().Remove(EBG_StepBayonetSamplers);
  GetGame().GetCallqueue().CallLater(EBG_StepBayonetSamplers, 250, true);
 }
 protected static bool EBG_BayonetSamplerWorldChanged()
 {
  World world = GetGame().GetWorld();
  if (!world || world == s_EBG_BayonetWorld)
  {
   return false;
  }
  s_EBG_BayonetWorld = world;
  s_EBG_BayonetCursor = 0;
  s_EBG_BayonetPhase = 0;
  return true;
 }
 protected static void EBG_StepBayonetSamplers()
 {
  EXPBG_LazyStatics_SCR_BayonetComponent();
  if (!GetGame()) return;
  EBG_BayonetSamplerWorldChanged();
  if (s_EBG_BayonetSamplers.IsEmpty())
  {
   GetGame().GetCallqueue().Remove(EBG_StepBayonetSamplers);
   s_EBG_BayonetArmed = false;
   s_EBG_BayonetPhase = 0;
   return;
  }
  // Each cycle covers the bayonets registered when it starts, ceil(n / 4) per step.
  if (s_EBG_BayonetPhase == 0)
  {
   s_EBG_BayonetDue = s_EBG_BayonetSamplers.Count();
   s_EBG_BayonetBudget = (s_EBG_BayonetDue + 3) / 4;
  }
  int visits = s_EBG_BayonetBudget;
  if (visits > s_EBG_BayonetDue) visits = s_EBG_BayonetDue;
  while (visits > 0 && !s_EBG_BayonetSamplers.IsEmpty())
  {
   visits--;
   s_EBG_BayonetDue--;
   if (s_EBG_BayonetCursor >= s_EBG_BayonetSamplers.Count()) s_EBG_BayonetCursor = 0;
   SCR_BayonetComponent bayonet = s_EBG_BayonetSamplers[s_EBG_BayonetCursor];
   if (!bayonet)
   {
    s_EBG_BayonetSamplers.RemoveOrdered(s_EBG_BayonetCursor);
    continue;
   }
   s_EBG_BayonetCursor++;
   bayonet.EBG_SampleBayonetNetState();
  }
  s_EBG_BayonetPhase = (s_EBG_BayonetPhase + 1) % 4;
 }
 protected void EBG_OnBayonetNetState()
 {
  if (Replication.IsServer() || !GetGame()) return;
  GetGame().GetCallqueue().Remove(EBG_ApplyBayonetNetState);
  m_EBG_NetAttempts = 0;
  EBG_ApplyBayonetNetState();
 }
 protected void EBG_ApplyBayonetNetState()
 {
  if (!GetGame() || EBG_CacheManager.Unloading || !GetOwner()) return;
  EBG_FullIdentityMaterials material = EBG_UnpackMaterial(m_EBG_NetMaterial);
  if (!material) return;
  bool applied = EBG_ApplyFullState(m_EBG_NetBlood, material.Values, material.Wetness, material.Drops);
  if (applied)
  {
#ifdef EBG_ACCEPTANCE_TEST
   PrintFormat("[EBG BAYONET CLIENT] property blood=%1 material1=%2 material2=%3 allGettersMatched=1", m_fBloodStainLevel, material.Values[3], material.Values[4]);
#endif
   return;
  }
  if (++m_EBG_NetAttempts < 100) GetGame().GetCallqueue().CallLater(EBG_ApplyBayonetNetState, 50);
  else Print("[EBG] Replicated bayonet state could not apply within five seconds");
 }
 override void OnPostInit(IEntity owner)
 {
  super.OnPostInit(owner);
  EBG_OnBayonetNetState();
 }
 void ~SCR_BayonetComponent()
 {
  if (!GetGame()) return;
  GetGame().GetCallqueue().Remove(EBG_ApplyBayonetNetState);
 }
 float EBG_GetBloodStainLevel() { return m_fBloodStainLevel; }
 bool EBG_ApplyFullState(float blood, array<int> values, bool wetness, bool drops)
 {
  if (blood < 0 || blood > MAX_BLOOD_LEVEL || !values || values.Count() != 10 || !GetOwner()) { return false; }
  EBG_FullIdentityMaterials material = new EBG_FullIdentityMaterials();
  material.Present = true; material.Values = values; material.Wetness = wetness; material.Drops = drops;
  if (!material.Restore(GetOwner())) { return false; }
  m_fBloodStainLevel = blood;
  return true;
 }
 bool EBG_PreflightMissionBlood(float blood) { return GetOwner() && EBG_MissionPersistence.Finite(blood) && blood >= 0 && blood <= MAX_BLOOD_LEVEL; }
 void EBG_PublishMissionBlood(float blood, EBG_FullIdentityMaterials material)
 {
  Rpc(EBG_RpcFullState, blood, material.Values, material.Wetness, material.Drops);
  EBG_SampleBayonetNetState();
  EBG_RegisterBayonetSampler(this);
 }
 bool EBG_RestoreFullState(float blood, EBG_FullIdentityMaterials material)
 {
  if (!Replication.IsServer() || !material || !material.Present || !EBG_ApplyFullState(blood, material.Values, material.Wetness, material.Drops)) { return false; }
  Rpc(EBG_RpcFullState, blood, material.Values, material.Wetness, material.Drops);
  EBG_SampleBayonetNetState();
  EBG_RegisterBayonetSampler(this);
  return m_fBloodStainLevel == blood && material.Matches(GetOwner());
 }
 [RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
 protected void EBG_RpcFullState(float blood, array<int> values, bool wetness, bool drops)
 {
  bool applied = EBG_ApplyFullState(blood, values, wetness, drops);
#ifdef EBG_ACCEPTANCE_TEST
  PrintFormat("[EBG BAYONET CLIENT] restore blood=%1 material1=%2 material2=%3 allGettersMatched=%4", m_fBloodStainLevel, values[3], values[4], applied);
#endif
 }

	//------------------------------------------------------------------------------------------------
	//! Creates the collections on first use (not in the global static initializer, which has a
	//! per-function instruction limit that large modsets exceed on Windows).
	protected static void EXPBG_LazyStatics_SCR_BayonetComponent()
	{
		if (!s_EBG_BayonetSamplers)
			s_EBG_BayonetSamplers = new array<SCR_BayonetComponent>();
	}
}

// The 6B3 cloth slot creates its scabbard locally; its blade is not a separate
// replication node. Carry that one exact native slot's state on its character.
modded class SCR_ChimeraCharacter
{
 protected IEntity m_EBG_FullBlade;
 [RplProp(onRplName: "EBG_OnClothBladeNetState"), NonSerialized()]
 protected float m_EBG_NetBladeBlood;
 [RplProp(onRplName: "EBG_OnClothBladeNetState"), NonSerialized()]
 protected ref array<int> m_EBG_NetBladeMaterial = {};
 protected ref EBG_FullIdentityMaterials m_EBG_BladePending;
 protected float m_EBG_BladeBlood;
 protected int m_EBG_BladeAttempts;
 // One shared server sampler for every published cloth blade, in place of one 1 s timer
 // per wearer. A cycle of four 250 ms steps visits each registered wearer once (about
 // once per second, as before), a quarter of them per step. A wearer whose blade is gone
 // stops sampling, as its own timer did, and leaves the registry at that visit.
 protected static ref array<SCR_ChimeraCharacter> s_EBG_BladeSamplers;
 protected static int s_EBG_BladeCursor;
 protected static int s_EBG_BladePhase;
 protected static int s_EBG_BladeDue;
 protected static int s_EBG_BladeBudget;
 protected static bool s_EBG_BladeArmed;
 protected static World s_EBG_BladeWorld;
 protected bool m_EBG_BladeListed;
 protected bool m_EBG_BladeSampling;
 IEntity EBG_FindClothBlade()
 {
  SCR_CharacterInventoryStorageComponent inventory = SCR_CharacterInventoryStorageComponent.Cast(FindComponent(SCR_CharacterInventoryStorageComponent));
  if (!inventory) { return null; }
  IEntity garment = inventory.GetClothFromArea(LoadoutArmoredVestSlotArea);
  if (!garment || SCR_ResourceNameUtils.GetPrefabName(garment) != "{4CBDC206FEF9897C}Prefabs/Characters/Vests/Vest_6B3/Vest_6B3.et") { return null; }
  IEntity found;
  int examined;
  for (IEntity scabbard = GetChildren(); scabbard; scabbard = scabbard.GetSibling())
  {
   if (++examined > 512) { return null; }
   if (SCR_ResourceNameUtils.GetPrefabName(scabbard) != "{F759F0488730620F}Prefabs/Items/Equipment/Accessories/Scabbard_Bayonet_6Kh4/Scabbard_Bayonet_6Kh4.et") continue;
   InventoryItemComponent scabbardItem = InventoryItemComponent.Cast(scabbard.FindComponent(InventoryItemComponent));
   if (!scabbardItem || !scabbardItem.GetParentSlot()) continue;
   InventoryStorageSlot clothSlot = scabbardItem.GetParentSlot();
   if (clothSlot.GetOwner() != garment || clothSlot.GetSourceName() != "Bayonet" || clothSlot.GetAttachedEntity() != scabbard || clothSlot.GetSlotTemplate() != SCR_ResourceNameUtils.GetPrefabName(scabbard)) continue;
   SCR_EquipmentStorageComponent storage = SCR_EquipmentStorageComponent.Cast(scabbard.FindComponent(SCR_EquipmentStorageComponent));
   if (!storage || storage.GetSlotsCount() != 1) { return null; }
   InventoryStorageSlot slot = storage.GetSlot(0);
   if (!slot || slot.GetSourceName() != "BayonetSlot" || slot.GetStorage() != storage) { return null; }
   IEntity blade = slot.GetAttachedEntity();
   if (!blade || found || blade.GetParent() != scabbard || blade.GetChildren() || storage.Get(0) != blade || storage.FindItemSlot(blade) != slot || SCR_ResourceNameUtils.GetPrefabName(blade) != "{98C79F5FAE12F9B6}Prefabs/Weapons/Attachments/Bayonets/Bayonet_6Kh4.et") { return null; }
   InventoryItemComponent bladeItem = InventoryItemComponent.Cast(blade.FindComponent(InventoryItemComponent));
   if (!bladeItem || bladeItem.GetParentSlot() != slot) { return null; }
   found = blade;
  }
  return found;
 }
 bool EBG_PublishClothBlade(IEntity blade, float blood, EBG_FullIdentityMaterials material)
 {
  if (!Replication.IsServer() || !blade || EBG_FindClothBlade() != blade || !material || !material.Present) { return false; }
  m_EBG_FullBlade = blade;
#ifdef EBG_ACCEPTANCE_TEST
  PrintFormat("[EBG CLOTH BAYONET SEND] wearer=%1 blood=%2 material1=%3 material2=%4", GetID(), blood, material.Values[3], material.Values[4]);
#endif
  Rpc(EBG_RpcClothBlade, blood, material.Values, material.Wetness, material.Drops);
  EBG_SampleClothBladeNetState();
  EBG_RegisterBladeSampler(this);
  return true;
 }
 // Adds a wearer to the shared sampler once (sampling again if it had stopped) and
 // arms the sampler when it is idle.
 protected static void EBG_RegisterBladeSampler(SCR_ChimeraCharacter wearer)
 {
  EXPBG_LazyStatics_SCR_ChimeraCharacter_Blades();
  if (!wearer || !GetGame()) return;
  // A new world starts the cycle over and re-arms the one shared timer.
  if (EBG_BladeSamplerWorldChanged()) s_EBG_BladeArmed = false;
  wearer.m_EBG_BladeSampling = true;
  if (!wearer.m_EBG_BladeListed)
  {
   wearer.m_EBG_BladeListed = true;
   s_EBG_BladeSamplers.Insert(wearer);
  }
  if (s_EBG_BladeArmed) return;
  s_EBG_BladeArmed = true;
  GetGame().GetCallqueue().Remove(EBG_StepBladeSamplers);
  GetGame().GetCallqueue().CallLater(EBG_StepBladeSamplers, 250, true);
 }
 protected static bool EBG_BladeSamplerWorldChanged()
 {
  World world = GetGame().GetWorld();
  if (!world || world == s_EBG_BladeWorld)
  {
   return false;
  }
  s_EBG_BladeWorld = world;
  s_EBG_BladeCursor = 0;
  s_EBG_BladePhase = 0;
  return true;
 }
 protected static void EBG_StepBladeSamplers()
 {
  EXPBG_LazyStatics_SCR_ChimeraCharacter_Blades();
  if (!GetGame()) return;
  EBG_BladeSamplerWorldChanged();
  if (s_EBG_BladeSamplers.IsEmpty())
  {
   GetGame().GetCallqueue().Remove(EBG_StepBladeSamplers);
   s_EBG_BladeArmed = false;
   s_EBG_BladePhase = 0;
   return;
  }
  // Each cycle covers the wearers registered when it starts, ceil(n / 4) per step.
  if (s_EBG_BladePhase == 0)
  {
   s_EBG_BladeDue = s_EBG_BladeSamplers.Count();
   s_EBG_BladeBudget = (s_EBG_BladeDue + 3) / 4;
  }
  int visits = s_EBG_BladeBudget;
  if (visits > s_EBG_BladeDue) visits = s_EBG_BladeDue;
  while (visits > 0 && !s_EBG_BladeSamplers.IsEmpty())
  {
   visits--;
   s_EBG_BladeDue--;
   if (s_EBG_BladeCursor >= s_EBG_BladeSamplers.Count()) s_EBG_BladeCursor = 0;
   SCR_ChimeraCharacter wearer = s_EBG_BladeSamplers[s_EBG_BladeCursor];
   if (wearer && wearer.m_EBG_BladeSampling) wearer.EBG_SampleClothBladeNetState();
   if (wearer && wearer.m_EBG_BladeSampling)
   {
    s_EBG_BladeCursor++;
    continue;
   }
   // Destroyed, or stopped by its own sample: no further samples until published again.
   if (wearer) wearer.m_EBG_BladeListed = false;
   s_EBG_BladeSamplers.RemoveOrdered(s_EBG_BladeCursor);
  }
  s_EBG_BladePhase = (s_EBG_BladePhase + 1) % 4;
 }
 [RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
 protected void EBG_RpcClothBlade(float blood, array<int> values, bool wetness, bool drops)
 {
  if (!values || values.Count() != 10 || blood < 0 || blood > 255) return;
  m_EBG_BladeBlood = blood;
  m_EBG_BladePending = new EBG_FullIdentityMaterials();
  m_EBG_BladePending.Present = true; m_EBG_BladePending.Values = values;
  m_EBG_BladePending.Wetness = wetness; m_EBG_BladePending.Drops = drops;
  m_EBG_BladeAttempts = 0;
  GetGame().GetCallqueue().Remove(EBG_ApplyClothBlade);
  EBG_ApplyClothBlade();
 }
 protected void EBG_ApplyClothBlade()
 {
  if (!m_EBG_BladePending || !GetGame() || EBG_CacheManager.Unloading || IsDeleted()) return;
  IEntity blade = EBG_FindClothBlade();
  SCR_BayonetComponent bayonet;
  if (blade) bayonet = SCR_BayonetComponent.Cast(blade.FindComponent(SCR_BayonetComponent));
  if (bayonet && bayonet.EBG_ApplyFullState(m_EBG_BladeBlood, m_EBG_BladePending.Values, m_EBG_BladePending.Wetness, m_EBG_BladePending.Drops))
  {
#ifdef EBG_ACCEPTANCE_TEST
   PrintFormat("[EBG CLOTH BAYONET CLIENT] blood=%1 material1=%2 material2=%3 exactNativeSlot=1 allGettersMatched=%4", bayonet.EBG_GetBloodStainLevel(), m_EBG_BladePending.Values[3], m_EBG_BladePending.Values[4], m_EBG_BladePending.Matches(blade));
#endif
   m_EBG_BladePending = null;
   return;
  }
  if (++m_EBG_BladeAttempts < 100) GetGame().GetCallqueue().CallLater(EBG_ApplyClothBlade, 50);
  else { Print("[EBG] Native cloth bayonet state could not resolve within five seconds"); m_EBG_BladePending = null; }
 }
 protected void EBG_SampleClothBladeNetState()
 {
  if (!Replication.IsServer() || !GetGame() || EBG_CacheManager.Unloading || IsDeleted()) return;
  SCR_BayonetComponent bayonet;
  if (m_EBG_FullBlade && EBG_FindClothBlade() == m_EBG_FullBlade) bayonet = SCR_BayonetComponent.Cast(m_EBG_FullBlade.FindComponent(SCR_BayonetComponent));
  if (!bayonet)
  {
   m_EBG_FullBlade = null;
   // Stop sampling this wearer; the shared sampler drops it at this visit.
   m_EBG_BladeSampling = false;
   if (!m_EBG_NetBladeMaterial.IsEmpty()) { m_EBG_NetBladeMaterial = {}; Replication.BumpMe(); }
   return;
  }
  EBG_FullIdentityMaterials material = new EBG_FullIdentityMaterials(); material.Capture(m_EBG_FullBlade);
  float blood = bayonet.EBG_GetBloodStainLevel();
  if (!material.Present || (m_EBG_NetBladeBlood == blood && SCR_BayonetComponent.EBG_MaterialPacketMatches(m_EBG_NetBladeMaterial, material))) return;
  m_EBG_NetBladeBlood = blood;
  m_EBG_NetBladeMaterial = SCR_BayonetComponent.EBG_PackMaterial(material);
  Replication.BumpMe();
 }
 protected void EBG_OnClothBladeNetState()
 {
  if (Replication.IsServer() || !GetGame()) return;
  GetGame().GetCallqueue().Remove(EBG_ApplyClothBlade);
  m_EBG_BladePending = null;
  EBG_FullIdentityMaterials material = SCR_BayonetComponent.EBG_UnpackMaterial(m_EBG_NetBladeMaterial);
  if (material) EBG_RpcClothBlade(m_EBG_NetBladeBlood, material.Values, material.Wetness, material.Drops);
 }
 override void EOnInit(IEntity owner)
 {
  super.EOnInit(owner);
  EBG_OnClothBladeNetState();
 }
 void ~SCR_ChimeraCharacter()
 {
  if (GetGame())
  {
   GetGame().GetCallqueue().Remove(EBG_ApplyClothBlade);
  }
  m_EBG_BladePending = null; m_EBG_FullBlade = null;
 }

	//------------------------------------------------------------------------------------------------
	//! Creates the collections on first use (not in the global static initializer, which has a
	//! per-function instruction limit that large modsets exceed on Windows). Named apart from
	//! EXPBG_LazyStatics_SCR_ChimeraCharacter in EBG_SimulationCache.c (same modded class).
	protected static void EXPBG_LazyStatics_SCR_ChimeraCharacter_Blades()
	{
		if (!s_EBG_BladeSamplers)
			s_EBG_BladeSamplers = new array<SCR_ChimeraCharacter>();
	}
}

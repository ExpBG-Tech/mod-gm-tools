// Native attachment serialization omits this script accumulator and materials.
// Preserve them for Full restoration and stream the current state to late clients.
modded class SCR_BayonetComponent
{
 [RplProp(onRplName: "EBG_OnBayonetNetState"), NonSerialized()]
 protected float m_EBG_NetBlood;
 [RplProp(onRplName: "EBG_OnBayonetNetState"), NonSerialized()]
 protected ref array<int> m_EBG_NetMaterial = {};
 protected int m_EBG_NetAttempts;

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
  GetGame().GetCallqueue().Remove(EBG_SampleBayonetNetState);
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
  GetGame().GetCallqueue().Remove(EBG_SampleBayonetNetState);
  GetGame().GetCallqueue().CallLater(EBG_SampleBayonetNetState, 1000, true);
 }
 bool EBG_RestoreFullState(float blood, EBG_FullIdentityMaterials material)
 {
  if (!Replication.IsServer() || !material || !material.Present || !EBG_ApplyFullState(blood, material.Values, material.Wetness, material.Drops)) { return false; }
  Rpc(EBG_RpcFullState, blood, material.Values, material.Wetness, material.Drops);
  EBG_SampleBayonetNetState();
  GetGame().GetCallqueue().Remove(EBG_SampleBayonetNetState);
  GetGame().GetCallqueue().CallLater(EBG_SampleBayonetNetState, 1000, true);
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
  GetGame().GetCallqueue().Remove(EBG_SampleClothBladeNetState);
  GetGame().GetCallqueue().CallLater(EBG_SampleClothBladeNetState, 1000, true);
  return true;
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
   GetGame().GetCallqueue().Remove(EBG_SampleClothBladeNetState);
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
   GetGame().GetCallqueue().Remove(EBG_SampleClothBladeNetState);
  }
  m_EBG_BladePending = null; m_EBG_FullBlade = null;
 }
}

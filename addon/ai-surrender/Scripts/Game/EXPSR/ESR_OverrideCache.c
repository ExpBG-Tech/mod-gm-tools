// AI Surrender squad and soldier overrides (ESR_Overrides) across caching.
// - Simulation caching (Unit Caching, Garrison) keeps the entities: nothing to do.
// - Garrison Full caching keeps the squad entity (only its soldiers are respawned) and
//   Unit Caching Full caching deletes and recreates the squad from EBG_CacheGroupSnapshot:
//   the squad's values ride in that snapshot (captured with the group, applied with its
//   settings) and in its portable JSON ("esrSquadOverrides", written only when set).
// - Respawned soldiers take theirs from the Unit Caching survivor carry, applied before
//   they rejoin the squad; portable snapshots also hold them ("esrSurvivorOverrides", four
//   values per survivor in survivor order, written only when one is set).
// Snapshots written before these keys existed simply have none: no overrides.
modded class EBG_SurvivorCarry
{
 protected ref array<int> m_aESR_Overrides;

 // The carried soldier overrides, or null when he had none.
 array<int> ESR_GetOverrides()
 {
  return m_aESR_Overrides;
 }

 void ESR_SetOverrides(array<int> values)
 {
  m_aESR_Overrides = null;
  if (values && values.Count() == ESR_Overrides.COUNT)
  {
   m_aESR_Overrides = {};
   m_aESR_Overrides.Copy(values);
  }
 }

 override void Capture(SCR_ChimeraCharacter entity)
 {
  super.Capture(entity);
  m_aESR_Overrides = ESR_Overrides.CopyUnit(entity);
 }

 override void Apply(SCR_ChimeraCharacter entity)
 {
  super.Apply(entity);
  if (entity && m_aESR_Overrides)
   ESR_Overrides.RestoreUnit(entity, m_aESR_Overrides, "Full cache wake");
 }
}

modded class EBG_CacheGroupSnapshot
{
 protected ref array<int> m_aESR_Squad;

 array<int> ESR_GetSquadOverrides()
 {
  return m_aESR_Squad;
 }

 override bool CaptureGroup(SCR_AIGroup group)
 {
  m_aESR_Squad = ESR_Overrides.CopyGroup(group);
  return super.CaptureGroup(group);
 }

 override bool Apply(SCR_AIGroup group)
 {
  bool applied = super.Apply(group);
  if (applied && group && m_aESR_Squad)
   ESR_Overrides.RestoreGroup(group, m_aESR_Squad, "Full cache wake");
  return applied;
 }

 override bool Write(SaveContext context)
 {
  if (!super.Write(context))
   return false;
  if (!m_aESR_Squad)
   return true;
  return context.WriteValue("esrSquadOverrides", m_aESR_Squad);
 }

 override bool Read(LoadContext context)
 {
  if (!super.Read(context))
   return false;
  m_aESR_Squad = null;
  if (!context.DoesKeyExist("esrSquadOverrides"))
   return true;
  array<int> values = {};
  if (!context.ReadValue("esrSquadOverrides", values) || values.Count() != ESR_Overrides.COUNT)
  {
   Print("[EXPBG SURRENDER] cached squad overrides unreadable: the squad wakes with module settings", LogLevel.WARNING);
   return true;
  }
  m_aESR_Squad = values;
  return true;
 }
}

modded class EBG_PrefabFullCache
{
 // Test and diagnostics access: the squad overrides held by this transaction's snapshot.
 array<int> ESR_GetSnapshotSquadOverrides()
 {
  if (!m_Snapshot)
   return null;
  return m_Snapshot.ESR_GetSquadOverrides();
 }

 override bool WriteSnapshot(SaveContext context)
 {
  if (!super.WriteSnapshot(context))
   return false;
  array<int> values = {};
  bool any;
  foreach (EBG_PrefabSurvivor row : m_Survivors)
  {
   array<int> carried;
   if (row && row.Carry)
    carried = row.Carry.ESR_GetOverrides();
   for (int slot = 0; slot < ESR_Overrides.COUNT; slot++)
   {
    int value = ESR_Overrides.UNSET;
    if (carried && carried.IsIndexValid(slot))
     value = carried[slot];
    if (value >= 0)
     any = true;
    values.Insert(value);
   }
  }
  if (!any)
   return true;
  return context.WriteValue("esrSurvivorOverrides", values);
 }

 override bool ReadSnapshot(LoadContext context)
 {
  if (!super.ReadSnapshot(context))
   return false;
  if (!context.DoesKeyExist("esrSurvivorOverrides"))
   return true;
  array<int> values = {};
  if (!context.ReadValue("esrSurvivorOverrides", values) || values.Count() != m_Survivors.Count() * ESR_Overrides.COUNT)
  {
   Print("[EXPBG SURRENDER] cached soldier overrides unreadable: survivors wake with squad or module settings", LogLevel.WARNING);
   return true;
  }
  for (int i = 0; i < m_Survivors.Count(); i++)
  {
   EBG_PrefabSurvivor row = m_Survivors[i];
   if (!row || !row.Carry)
    continue;
   array<int> soldier = {};
   bool hasValue;
   for (int slot = 0; slot < ESR_Overrides.COUNT; slot++)
   {
    int value = values[i * ESR_Overrides.COUNT + slot];
    if (value >= 0)
     hasValue = true;
    soldier.Insert(value);
   }
   if (hasValue)
    row.Carry.ESR_SetOverrides(soldier);
  }
  return true;
 }
}

// EXPBG AI Surrender per-squad and per-soldier overrides. In the "EXPBG Surrender & Intel"
// tab of an AI squad's or an AI soldier's Edit properties a Game Master sets the surrender
// chance and the three interrogation chances (reveal squad, identity, reveal intel items).
// Each is "use the module setting" (UNSET, the default: no behaviour change) or 0-100 %.
// Server only; resolved at decision time, never polled:
//   soldier override > his squad's override > module setting.
// - Surrender (ESR_SurrenderManager.Evaluate): whether a squad breaks stays the squad-level
//   module casualty threshold. Each able soldier of a broken squad rolls his own effective
//   chance: his override, else his squad's (cached on the group), else the module chance.
//   An override is exact; the random factor spreads only the module chance.
// - Interrogation (ESR_SurrenderManager.Interrogate / RevealIntel): a prisoner leaves his
//   squad when he surrenders, so his squad's overrides are copied into his ESR_Prisoner
//   record then (CaptureSquad). His own overrides stay on him and are read when he is
//   questioned, so a Game Master can still change them on the prisoner. Neither set: the
//   module setting at question time.
// Values live on the entity as plain server fields (the attributes are read on the server
// and sent with the dialog, so nothing is replicated). They follow the entity through
// Unit Caching and Garrison caching (ESR_OverrideCache.c), native saves
// (ESR_OverridePersistence.c) and attribute-based saves such as CDF Game Master Save
// (ESR_OverrideAttributes.c).
class ESR_Overrides
{
 static const int SURRENDER = 0;
 static const int REVEAL = 1;
 static const int IDENTITY = 2;
 static const int INTEL = 3;
 static const int COUNT = 4;
 static const int UNSET = -1;
 static const int SOURCE_MODULE = 0;
 static const int SOURCE_SQUAD = 1;
 static const int SOURCE_SOLDIER = 2;
 // Spinbox entries: 0 = no override, entry k = (k - 1) * STEP percent.
 static const int STEP = 5;
 static const int ENTRY_COUNT = 22;
 // Entities with overrides tracked for native saves (weak; deleted ones become null).
 static const int MAX_TRACKED = 2048;
 // Saved overrides bind to their loaded entities by persistence id for this long.
 static const int BIND_RETRY_MS = 2000;
 static const float BIND_SECONDS = 120;

 protected static ref array<SCR_AIGroup> s_aGroups = {};
 protected static ref array<SCR_ChimeraCharacter> s_aUnits = {};
 protected static ref array<UUID> s_aPendingIds = {};
 protected static ref array<bool> s_aPendingUnits = {};
 protected static ref array<int> s_aPendingValues = {};
 protected static float s_fBindUntil;
 protected static bool s_bBindQueued;

 //------------------------------------------------------------------------------------------------
 // Values
 //------------------------------------------------------------------------------------------------
 static bool IsSlot(int slot)
 {
  return slot >= 0 && slot < COUNT;
 }

 // The module setting a slot overrides.
 static int SettingKey(int slot)
 {
  if (slot == REVEAL)
   return ESR_Settings.REVEAL;
  if (slot == IDENTITY)
   return ESR_Settings.IDENTITY;
  if (slot == INTEL)
   return ESR_Settings.INTEL;
  return ESR_Settings.CHANCE;
 }

 static string SlotName(int slot)
 {
  if (slot == REVEAL)
   return "reveal";
  if (slot == IDENTITY)
   return "identity";
  if (slot == INTEL)
   return "intel";
  return "surrender";
 }

 static string SourceName(int source)
 {
  if (source == SOURCE_SOLDIER)
   return "soldier";
  if (source == SOURCE_SQUAD)
   return "squad";
  return "module";
 }

 // UNSET or a whole percentage.
 static int Normalize(int value)
 {
  if (value < 0)
   return UNSET;
  return Math.ClampInt(value, 0, 100);
 }

 static int FromEntry(int entry)
 {
  if (entry <= 0)
   return UNSET;
  return Math.ClampInt((entry - 1) * STEP, 0, 100);
 }

 static int ToEntry(int value)
 {
  if (value < 0)
   return 0;
  int steps = Math.Round(Math.ClampInt(value, 0, 100) / 5.0);
  return steps + 1;
 }

 static string Describe(int value)
 {
  if (value < 0)
   return "module";
  return value.ToString() + "%";
 }

 //------------------------------------------------------------------------------------------------
 // Resolution (server, decision time)
 //------------------------------------------------------------------------------------------------
 // Effective value for a soldier of a squad: his override, else the squad's (cached on the
 // group), else the module setting. Either entity may be null.
 static int Resolve(IEntity soldier, SCR_AIGroup squad, int slot, out int source)
 {
  source = SOURCE_MODULE;
  if (!IsSlot(slot))
   return 0;
  SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(soldier);
  if (character)
  {
   int own = character.ESR_GetOverride(slot);
   if (own >= 0)
   {
    source = SOURCE_SOLDIER;
    return own;
   }
  }
  if (squad)
   return squad.ESR_SquadValue(slot, source);
  return ESR_Settings.Get(SettingKey(slot));
 }

 // Surrender time: the squad's interrogation overrides go into the prisoner's record.
 static void CaptureSquad(notnull ESR_Prisoner prisoner, SCR_AIGroup squad)
 {
  prisoner.SquadOverrides.Clear();
  for (int slot = 0; slot < COUNT; slot++)
  {
   int value = UNSET;
   if (squad)
    value = squad.ESR_GetOverride(slot);
   prisoner.SquadOverrides.Insert(value);
  }
 }

 // Question time: his own override (still on him), else his former squad's (captured),
 // else moduleValue (the caller reads the module setting).
 static int ForPrisoner(ESR_Prisoner prisoner, int slot, int moduleValue)
 {
  int source;
  return ForPrisonerSource(prisoner, slot, moduleValue, source);
 }

 static int ForPrisonerSource(ESR_Prisoner prisoner, int slot, int moduleValue, out int source)
 {
  source = SOURCE_MODULE;
  if (!prisoner || !IsSlot(slot))
   return moduleValue;
  if (prisoner.Character)
  {
   int own = prisoner.Character.ESR_GetOverride(slot);
   if (own >= 0)
   {
    source = SOURCE_SOLDIER;
    return own;
   }
  }
  if (prisoner.SquadOverrides.IsIndexValid(slot) && prisoner.SquadOverrides[slot] >= 0)
  {
   source = SOURCE_SQUAD;
   return prisoner.SquadOverrides[slot];
  }
  return moduleValue;
 }

 // Diagnostics: "reveal=40%(module) identity=100%(squad) intel=0%(soldier)".
 static string DescribePrisoner(ESR_Prisoner prisoner)
 {
  string text;
  for (int slot = REVEAL; slot < COUNT; slot++)
  {
   int source;
   int value = ForPrisonerSource(prisoner, slot, ESR_Settings.Get(SettingKey(slot)), source);
   if (slot > REVEAL)
    text += " ";
   text += SlotName(slot) + "=" + value.ToString() + "%(" + SourceName(source) + ")";
  }
  return text;
 }

 //------------------------------------------------------------------------------------------------
 // Writes (server)
 //------------------------------------------------------------------------------------------------
 // One squad value; origin names the writer for the diagnostics line.
 static bool SetGroup(SCR_AIGroup squad, int slot, int value, string origin)
 {
  if (!squad || !IsSlot(slot) || !Replication.IsServer())
   return false;
  if (!squad.ESR_SetOverride(slot, value))
   return false;
  Track(squad, null);
  ESR_SurrenderManager.Trace(string.Format("squad %1 %2 override=%3 (%4)", squad, SlotName(slot), Describe(squad.ESR_GetOverride(slot)), origin));
  return true;
 }

 static bool SetUnit(SCR_ChimeraCharacter soldier, int slot, int value, string origin)
 {
  if (!soldier || !IsSlot(slot) || !Replication.IsServer())
   return false;
  if (!soldier.ESR_SetOverride(slot, value))
   return false;
  Track(null, soldier);
  ESR_SurrenderManager.Trace(string.Format("soldier %1 %2 override=%3 (%4)", soldier, SlotName(slot), Describe(soldier.ESR_GetOverride(slot)), origin));
  return true;
 }

 // A full set (cache wake, save load); values must hold COUNT entries.
 static void RestoreGroup(SCR_AIGroup squad, array<int> values, string origin)
 {
  if (!squad || !values || values.Count() != COUNT)
   return;
  for (int slot = 0; slot < COUNT; slot++)
   SetGroup(squad, slot, values[slot], origin);
 }

 static void RestoreUnit(SCR_ChimeraCharacter soldier, array<int> values, string origin)
 {
  if (!soldier || !values || values.Count() != COUNT)
   return;
  for (int slot = 0; slot < COUNT; slot++)
   SetUnit(soldier, slot, values[slot], origin);
 }

 // The entity's current set, or null when it has no override.
 static array<int> CopyGroup(SCR_AIGroup squad)
 {
  if (!squad || !squad.ESR_HasOverrides())
   return null;
  array<int> values = {};
  for (int slot = 0; slot < COUNT; slot++)
   values.Insert(squad.ESR_GetOverride(slot));
  return values;
 }

 static array<int> CopyUnit(SCR_ChimeraCharacter soldier)
 {
  if (!soldier || !soldier.ESR_HasOverrides())
   return null;
  array<int> values = {};
  for (int slot = 0; slot < COUNT; slot++)
   values.Insert(soldier.ESR_GetOverride(slot));
  return values;
 }

 // Keeps the save registry in step: entities with an override in, others out. Bounded.
 protected static void Track(SCR_AIGroup squad, SCR_ChimeraCharacter soldier)
 {
  if (squad)
  {
   int groupIndex = s_aGroups.Find(squad);
   if (!squad.ESR_HasOverrides())
   {
    if (groupIndex >= 0)
     s_aGroups.Remove(groupIndex);
    return;
   }
   if (groupIndex >= 0)
    return;
   PruneGroups();
   if (s_aGroups.Count() >= MAX_TRACKED)
   {
    Print("[EXPBG SURRENDER] squad override registry full: this squad's overrides work but are not in native saves", LogLevel.WARNING);
    return;
   }
   s_aGroups.Insert(squad);
   return;
  }
  if (!soldier)
   return;
  int unitIndex = s_aUnits.Find(soldier);
  if (!soldier.ESR_HasOverrides())
  {
   if (unitIndex >= 0)
    s_aUnits.Remove(unitIndex);
   return;
  }
  if (unitIndex >= 0)
   return;
  PruneUnits();
  if (s_aUnits.Count() >= MAX_TRACKED)
  {
   Print("[EXPBG SURRENDER] soldier override registry full: this soldier's overrides work but are not in native saves", LogLevel.WARNING);
   return;
  }
  s_aUnits.Insert(soldier);
 }

 protected static void PruneGroups()
 {
  for (int i = s_aGroups.Count() - 1; i >= 0; i--)
  {
   if (!s_aGroups[i] || !s_aGroups[i].ESR_HasOverrides())
    s_aGroups.Remove(i);
  }
 }

 protected static void PruneUnits()
 {
  for (int i = s_aUnits.Count() - 1; i >= 0; i--)
  {
   if (!s_aUnits[i] || !s_aUnits[i].ESR_HasOverrides())
    s_aUnits.Remove(i);
  }
 }

 //------------------------------------------------------------------------------------------------
 // Native saves (ESR_OverridesSerializer)
 //------------------------------------------------------------------------------------------------
 // Every tracked entity with a persistence id: its id, then COUNT values each.
 static void Export(notnull array<UUID> groupIds, notnull array<int> groupValues, notnull array<UUID> unitIds, notnull array<int> unitValues)
 {
  groupIds.Clear();
  groupValues.Clear();
  unitIds.Clear();
  unitValues.Clear();
  PersistenceSystem persistence = PersistenceSystem.GetInstance();
  if (!persistence)
   return;
  PruneGroups();
  PruneUnits();
  foreach (SCR_AIGroup squad : s_aGroups)
  {
   UUID groupId = persistence.GetId(squad);
   if (groupId.IsNull())
    continue;
   groupIds.Insert(groupId);
   for (int slot = 0; slot < COUNT; slot++)
    groupValues.Insert(squad.ESR_GetOverride(slot));
  }
  foreach (SCR_ChimeraCharacter soldier : s_aUnits)
  {
   UUID unitId = persistence.GetId(soldier);
   if (unitId.IsNull())
    continue;
   unitIds.Insert(unitId);
   for (int unitSlot = 0; unitSlot < COUNT; unitSlot++)
    unitValues.Insert(soldier.ESR_GetOverride(unitSlot));
  }
 }

 // Loaded entities may appear after this state: bind by id, retrying for BIND_SECONDS.
 static void Import(notnull array<UUID> groupIds, notnull array<int> groupValues, notnull array<UUID> unitIds, notnull array<int> unitValues)
 {
  s_aPendingIds.Clear();
  s_aPendingUnits.Clear();
  s_aPendingValues.Clear();
  int groups = Math.MinInt(groupIds.Count(), MAX_TRACKED);
  for (int i = 0; i < groups; i++)
   AddPending(groupIds[i], false, groupValues, i);
  int units = Math.MinInt(unitIds.Count(), MAX_TRACKED);
  for (int j = 0; j < units; j++)
   AddPending(unitIds[j], true, unitValues, j);
  ESR_SurrenderManager.Trace(string.Format("saved overrides loaded: squads=%1 soldiers=%2", groups, units));
  s_fBindUntil = ESR_SurrenderManager.Now() + BIND_SECONDS;
  BindPending();
 }

 protected static void AddPending(UUID id, bool unit, notnull array<int> values, int row)
 {
  if (id.IsNull() || (row + 1) * COUNT > values.Count())
   return;
  s_aPendingIds.Insert(id);
  s_aPendingUnits.Insert(unit);
  for (int slot = 0; slot < COUNT; slot++)
   s_aPendingValues.Insert(values[row * COUNT + slot]);
 }

 static int PendingCount()
 {
  return s_aPendingIds.Count();
 }

 // Bounded by the pending list (at most 2 x MAX_TRACKED); one retry timer at a time.
 static void BindPending()
 {
  s_bBindQueued = false;
  if (s_aPendingIds.IsEmpty() || !GetGame())
   return;
  PersistenceSystem persistence = PersistenceSystem.GetInstance();
  for (int i = s_aPendingIds.Count() - 1; i >= 0; i--)
  {
   Managed found;
   if (persistence)
    found = persistence.FindById(s_aPendingIds[i]);
   if (!found)
    continue;
   array<int> values = {};
   for (int slot = 0; slot < COUNT; slot++)
    values.Insert(s_aPendingValues[i * COUNT + slot]);
   if (s_aPendingUnits[i])
    RestoreUnit(SCR_ChimeraCharacter.Cast(found), values, "save");
   else
    RestoreGroup(SCR_AIGroup.Cast(found), values, "save");
   // Ordered removal keeps the parallel lists (COUNT values per id) aligned.
   s_aPendingIds.RemoveOrdered(i);
   s_aPendingUnits.RemoveOrdered(i);
   for (int drop = COUNT - 1; drop >= 0; drop--)
    s_aPendingValues.RemoveOrdered(i * COUNT + drop);
  }
  if (s_aPendingIds.IsEmpty())
   return;
  // Past the deadline, or a deadline from an earlier world (world time restarted).
  float now = ESR_SurrenderManager.Now();
  if (now >= s_fBindUntil || s_fBindUntil - now > BIND_SECONDS + 1)
  {
   PrintFormat("[EXPBG SURRENDER] %1 saved squad/soldier overrides had no matching entity", s_aPendingIds.Count());
   s_aPendingIds.Clear();
   s_aPendingUnits.Clear();
   s_aPendingValues.Clear();
   return;
  }
  if (s_bBindQueued)
   return;
  s_bBindQueued = true;
  GetGame().GetCallqueue().CallLater(ESR_Overrides.BindPending, BIND_RETRY_MS, false);
 }
}

// Squad overrides. The effective values (override, else module setting) are cached per
// group and recomputed only after this squad's override or any module setting changed
// (ESR_Settings.Revision). Membership changes need no invalidation: squad values do not
// depend on members, and a soldier's own value is read from him at decision time.
modded class SCR_AIGroup
{
 protected int m_iESR_Surrender = -1;
 protected int m_iESR_Reveal = -1;
 protected int m_iESR_Identity = -1;
 protected int m_iESR_Intel = -1;
 protected ref array<int> m_aESR_Effective;
 protected int m_iESR_EffectiveRevision = -1;

 int ESR_GetOverride(int slot)
 {
  if (slot == ESR_Overrides.SURRENDER)
   return m_iESR_Surrender;
  if (slot == ESR_Overrides.REVEAL)
   return m_iESR_Reveal;
  if (slot == ESR_Overrides.IDENTITY)
   return m_iESR_Identity;
  if (slot == ESR_Overrides.INTEL)
   return m_iESR_Intel;
  return ESR_Overrides.UNSET;
 }

 bool ESR_HasOverrides()
 {
  return m_iESR_Surrender >= 0 || m_iESR_Reveal >= 0 || m_iESR_Identity >= 0 || m_iESR_Intel >= 0;
 }

 // Server. True when the stored value changed (ESR_Overrides.SetGroup is the writer).
 bool ESR_SetOverride(int slot, int value)
 {
  if (!ESR_Overrides.IsSlot(slot))
   return false;
  int normalized = ESR_Overrides.Normalize(value);
  if (ESR_GetOverride(slot) == normalized)
   return false;
  if (slot == ESR_Overrides.SURRENDER)
   m_iESR_Surrender = normalized;
  else if (slot == ESR_Overrides.REVEAL)
   m_iESR_Reveal = normalized;
  else if (slot == ESR_Overrides.IDENTITY)
   m_iESR_Identity = normalized;
  else
   m_iESR_Intel = normalized;
  m_iESR_EffectiveRevision = -1;
  return true;
 }

 // The squad's effective value: its override, else the module setting.
 int ESR_SquadValue(int slot, out int source)
 {
  source = ESR_Overrides.SOURCE_MODULE;
  if (!ESR_Overrides.IsSlot(slot))
   return 0;
  int revision = ESR_Settings.Revision();
  if (!m_aESR_Effective || m_iESR_EffectiveRevision != revision)
  {
   if (!m_aESR_Effective)
    m_aESR_Effective = {};
   m_aESR_Effective.Clear();
   for (int key = 0; key < ESR_Overrides.COUNT; key++)
   {
    int value = ESR_GetOverride(key);
    if (value < 0)
     value = ESR_Settings.Get(ESR_Overrides.SettingKey(key));
    m_aESR_Effective.Insert(value);
   }
   m_iESR_EffectiveRevision = revision;
  }
  if (ESR_GetOverride(slot) >= 0)
   source = ESR_Overrides.SOURCE_SQUAD;
  return m_aESR_Effective[slot];
 }
}

// Soldier overrides: read from the soldier at decision time.
modded class SCR_ChimeraCharacter
{
 protected int m_iESR_Surrender = -1;
 protected int m_iESR_Reveal = -1;
 protected int m_iESR_Identity = -1;
 protected int m_iESR_Intel = -1;

 int ESR_GetOverride(int slot)
 {
  if (slot == ESR_Overrides.SURRENDER)
   return m_iESR_Surrender;
  if (slot == ESR_Overrides.REVEAL)
   return m_iESR_Reveal;
  if (slot == ESR_Overrides.IDENTITY)
   return m_iESR_Identity;
  if (slot == ESR_Overrides.INTEL)
   return m_iESR_Intel;
  return ESR_Overrides.UNSET;
 }

 bool ESR_HasOverrides()
 {
  return m_iESR_Surrender >= 0 || m_iESR_Reveal >= 0 || m_iESR_Identity >= 0 || m_iESR_Intel >= 0;
 }

 // Server. True when the stored value changed (ESR_Overrides.SetUnit is the writer).
 bool ESR_SetOverride(int slot, int value)
 {
  if (!ESR_Overrides.IsSlot(slot))
   return false;
  int normalized = ESR_Overrides.Normalize(value);
  if (ESR_GetOverride(slot) == normalized)
   return false;
  if (slot == ESR_Overrides.SURRENDER)
   m_iESR_Surrender = normalized;
  else if (slot == ESR_Overrides.REVEAL)
   m_iESR_Reveal = normalized;
  else if (slot == ESR_Overrides.IDENTITY)
   m_iESR_Identity = normalized;
  else
   m_iESR_Intel = normalized;
  return true;
 }
}

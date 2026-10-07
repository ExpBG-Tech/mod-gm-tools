// Game Master attributes of the Random Garrison module. Distinct subclasses keep the
// editor's duplicate-type check intact; m_Key selects the setting (the module's KEY_*).
// The class names are the permanent keys of attribute saves (CDF), and saved values
// are durable values and faction key hashes, never row indices. Writes happen on the
// server only, for this player's own unlimited editor in Edit mode, or for a restore
// (no manager, player -1).
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EXPG_RandomGarrisonAttribute : SCR_BaseValueListEditorAttribute
{
 [Attribute("0")]
 protected int m_Key;
 // Spinbox rows: the dialog exchanges row indices, the module keeps these values.
 [Attribute()]
 protected ref array<ref SCR_EditorAttributeFloatStringValueHolder> m_aValues;

 static EXPG_RandomGarrisonModule Zone(Managed item)
 {
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(item);
  if (!editable)
  {
   return null;
  }
  return EXPG_RandomGarrisonModule.Cast(editable.GetOwner());
 }

 static bool AllowedWrite(SCR_AttributesManagerEditorComponent manager, int playerID)
 {
  if (!Replication.IsServer() || !GetGame() || !GetGame().InPlayMode())
  {
   return false;
  }
  if (!manager)
  {
   return playerID == -1;
  }
  SCR_EditorManagerEntity editor = manager.GetManager();
  if (!editor || editor.GetPlayerID() != playerID || editor.IsLimited() || editor.GetCurrentMode() != EEditorMode.EDIT)
  {
   return false;
  }
  SCR_EditorManagerCore core = SCR_EditorManagerCore.Cast(SCR_EditorManagerCore.GetInstance(SCR_EditorManagerCore));
  return core && core.GetEditorManager(playerID) == editor;
 }

 protected bool IsSwitch()
 {
  return m_Key == EXPG_RandomGarrisonModule.KEY_EXCLUDE_SUPPORT || m_Key == EXPG_RandomGarrisonModule.KEY_ALLOW_GARRISONED;
 }

 protected bool UsesValueList()
 {
  return m_aValues && !m_aValues.IsEmpty();
 }

 override int GetEntries(notnull array<ref SCR_BaseEditorAttributeEntry> outEntries)
 {
  if (IsSwitch())
  {
   return outEntries.Count();
  }
  if (UsesValueList())
  {
   outEntries.Insert(new SCR_BaseEditorAttributeFloatStringValues(m_aValues));
   return outEntries.Count();
  }
  return super.GetEntries(outEntries);
 }

 override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
 {
  EXPG_RandomGarrisonModule zone = Zone(item);
  if (!zone || m_Key < 0 || m_Key >= EXPG_RandomGarrisonModule.INT_SETTINGS)
  {
   return null;
  }
  int durable = zone.GetSetting(m_Key);
  if (IsSwitch())
  {
   return SCR_BaseEditorAttributeVar.CreateBool(durable != 0);
  }
  if (UsesValueList())
  {
   // A session save (no manager) stores the durable value, never the row index.
   if (!manager)
   {
    return SCR_BaseEditorAttributeVar.CreateInt(durable);
   }
   foreach (int index, SCR_EditorAttributeFloatStringValueHolder entry : m_aValues)
   {
    if (entry.GetFloatValue() == durable)
    {
     return SCR_BaseEditorAttributeVar.CreateInt(index);
    }
   }
   return null;
  }
  return SCR_BaseEditorAttributeVar.CreateFloat(durable);
 }

 override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var, SCR_AttributesManagerEditorComponent manager, int playerID)
 {
  if (!var || m_Key < 0 || m_Key >= EXPG_RandomGarrisonModule.INT_SETTINGS || !AllowedWrite(manager, playerID)) { return; }
  EXPG_RandomGarrisonModule zone = Zone(item);
  if (!zone) { return; }
  int value = Math.Round(var.GetFloat());
  if (IsSwitch())
  {
   value = 0;
   if (var.GetBool()) { value = 1; }
  }
  else if (UsesValueList() && manager)
  {
   // Row index to its durable value; an unknown row changes nothing.
   if (value < 0 || value >= m_aValues.Count()) { return; }
   value = Math.Round(m_aValues[value].GetFloatValue());
  }
  zone.ApplySetting(m_Key, value, manager != null);
 }
}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EXPG_RGRadiusAttribute : EXPG_RandomGarrisonAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EXPG_RGSizesAttribute : EXPG_RandomGarrisonAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EXPG_RGExcludeSupportAttribute : EXPG_RandomGarrisonAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EXPG_RGBuildingsAttribute : EXPG_RandomGarrisonAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EXPG_RGShareAttribute : EXPG_RandomGarrisonAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EXPG_RGSquadsMinAttribute : EXPG_RandomGarrisonAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EXPG_RGSquadsMaxAttribute : EXPG_RandomGarrisonAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EXPG_RGPlayerDistanceAttribute : EXPG_RandomGarrisonAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EXPG_RGAllowGarrisonedAttribute : EXPG_RandomGarrisonAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EXPG_RGSeedAttribute : EXPG_RandomGarrisonAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EXPG_RGCacheModeAttribute : EXPG_RandomGarrisonAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EXPG_RGWakeAttribute : EXPG_RandomGarrisonAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EXPG_RGSleepAttribute : EXPG_RandomGarrisonAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EXPG_RGOnDeleteAttribute : EXPG_RandomGarrisonAttribute {}

// Read-only status line, local on the Game Master's machine from the replicated
// module status (strings are not attribute values). Never saved or written back.
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EXPG_RGStatusAttribute : EXPG_RandomGarrisonAttribute
{
 protected EXPG_RandomGarrisonModule m_Inspected;
 protected ref array<ref SCR_EditorAttributeFloatStringValueHolder> m_StatusEntries = {};

 override bool IsServer()
 {
  return false;
 }

 override bool IsEnabled()
 {
  return false;
 }

 override bool IsSerializable()
 {
  return false;
 }

 override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
 {
  if (!manager)
  {
   return null;
  }
  m_Inspected = Zone(item);
  if (!m_Inspected)
  {
   return null;
  }
  return SCR_BaseEditorAttributeVar.CreateFloat(0);
 }

 override int GetEntries(notnull array<ref SCR_BaseEditorAttributeEntry> outEntries)
 {
  string status = "Select one zone to see its status";
  if (m_Inspected && !GetIsMultiSelect()) { status = m_Inspected.GetStatus(); }
  if (status.IsEmpty()) { status = "Idle: press Generate"; }
  SCR_EditorAttributeFloatStringValueHolder entry = new SCR_EditorAttributeFloatStringValueHolder();
  entry.SetName(status);
  entry.SetFloatValue(0);
  m_StatusEntries.Clear();
  m_StatusEntries.Insert(entry);
  outEntries.Insert(new SCR_BaseEditorAttributeFloatStringValues(m_StatusEntries));
  return outEntries.Count();
 }

 override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var, SCR_AttributesManagerEditorComponent manager, int playerID)
 {
 }
}

// One-time action (No action, Generate, Regenerate, Clear generated garrisons, Stop):
// always reads No action, runs after the rest of the same Save, never saved.
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EXPG_RGRunAttribute : EXPG_RandomGarrisonAttribute
{
 override bool IsSerializable()
 {
  return false;
 }

 override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
 {
  if (!manager || !Zone(item))
  {
   return null;
  }
  return SCR_BaseEditorAttributeVar.CreateInt(EXPG_RandomGarrisonModule.ACTION_NONE);
 }

 override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var, SCR_AttributesManagerEditorComponent manager, int playerID)
 {
  if (!var || !manager || !AllowedWrite(manager, playerID)) { return; }
  EXPG_RandomGarrisonModule zone = Zone(item);
  if (!zone) { return; }
  int row = Math.Round(var.GetFloat());
  int action = row;
  if (UsesValueList())
  {
   if (row < 0 || row >= m_aValues.Count()) { return; }
   action = Math.Round(m_aValues[row].GetFloatValue());
  }
  if (action > EXPG_RandomGarrisonModule.ACTION_NONE) { zone.QueueRun(action, playerID); }
 }
}

// Faction and second faction: rows built the same way on the server and the Game
// Master's machine (military factions with a squad catalog, sorted by key; the second
// one adds None first). Attribute saves keep the key hash (hi16, lo16, 1).
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EXPG_RGFactionAttribute : EXPG_RandomGarrisonAttribute
{
 protected ref array<ref SCR_EditorAttributeFloatStringValueHolder> m_FactionEntries = {};

 protected bool AllowsNone()
 {
  return m_Key == EXPG_RandomGarrisonModule.KEY_SECOND_FACTION;
 }

 protected int Slot()
 {
  if (AllowsNone())
  {
   return 1;
  }
  return 0;
 }

 override int GetEntries(notnull array<ref SCR_BaseEditorAttributeEntry> outEntries)
 {
  array<string> keys = {};
  EXPG_SquadPool.MilitaryFactions(keys);
  m_FactionEntries.Clear();
  int row;
  if (AllowsNone())
  {
   SCR_EditorAttributeFloatStringValueHolder noneRow = new SCR_EditorAttributeFloatStringValueHolder();
   noneRow.SetName("None");
   noneRow.SetFloatValue(row);
   m_FactionEntries.Insert(noneRow);
   row++;
  }
  foreach (string key : keys)
  {
   SCR_EditorAttributeFloatStringValueHolder entry = new SCR_EditorAttributeFloatStringValueHolder();
   entry.SetName(EXPG_SquadPool.FactionName(key));
   entry.SetFloatValue(row);
   m_FactionEntries.Insert(entry);
   row++;
  }
  outEntries.Insert(new SCR_BaseEditorAttributeFloatStringValues(m_FactionEntries));
  return outEntries.Count();
 }

 override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
 {
  EXPG_RandomGarrisonModule zone = Zone(item);
  if (!zone)
  {
   return null;
  }
  if (!manager)
  {
   return SCR_BaseEditorAttributeVar.CreateVector(EXPG_RGRules.PackKey(zone.GetFactionSetting(Slot())));
  }
  string key = zone.ResolveFaction(Slot());
  array<string> keys = {};
  EXPG_SquadPool.MilitaryFactions(keys);
  int index = keys.Find(key);
  if (AllowsNone())
  {
   return SCR_BaseEditorAttributeVar.CreateInt(index + 1);
  }
  if (index < 0)
  {
   index = 0;
  }
  return SCR_BaseEditorAttributeVar.CreateInt(index);
 }

 override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var, SCR_AttributesManagerEditorComponent manager, int playerID)
 {
  if (!var || !AllowedWrite(manager, playerID)) { return; }
  EXPG_RandomGarrisonModule zone = Zone(item);
  if (!zone) { return; }
  array<string> keys = {};
  if (!manager)
  {
   // Restore: the saved key hash, matched against every faction of the mission.
   vector packed = var.GetVector();
   if (packed[2] < 0.5)
   {
    zone.SetFactionSetting(Slot(), string.Empty, false);
    return;
   }
   FactionManager factions = GetGame().GetFactionManager();
   array<Faction> list = {};
   if (factions) { factions.GetFactionsList(list); }
   foreach (Faction candidate : list)
   {
    if (candidate && EXPG_RGRules.SameKey(packed, candidate.GetFactionKey()))
    {
     zone.SetFactionSetting(Slot(), candidate.GetFactionKey(), false);
     return;
    }
   }
   Print("[EXPG RANDOM] a saved faction of a Random Garrison zone is not in this mission; the default is kept", LogLevel.WARNING);
   return;
  }
  EXPG_SquadPool.MilitaryFactions(keys);
  int row = Math.Round(var.GetFloat());
  if (AllowsNone())
  {
   if (row == 0)
   {
    zone.SetFactionSetting(Slot(), string.Empty, true);
    return;
   }
   row--;
  }
  if (row < 0 || row >= keys.Count()) { return; }
  zone.SetFactionSetting(Slot(), keys[row], true);
 }
}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EXPG_RGSecondFactionAttribute : EXPG_RGFactionAttribute {}

// Never shown (null for the dialog): attribute savers such as CDF keep the zone's
// token and whether it generated (with the last seed), so a loaded zone finds its
// garrisons again. The native mission save uses EXPG_RandomGarrisonSerializer instead.
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EXPG_RGSavedStateAttribute : EXPG_RandomGarrisonAttribute
{
 override int GetEntries(notnull array<ref SCR_BaseEditorAttributeEntry> outEntries)
 {
  return outEntries.Count();
 }

 override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
 {
  if (manager)
  {
   return null;
  }
  EXPG_RandomGarrisonModule zone = Zone(item);
  if (!zone)
  {
   return null;
  }
  return SCR_BaseEditorAttributeVar.CreateVector(zone.SavedState());
 }

 override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var, SCR_AttributesManagerEditorComponent manager, int playerID)
 {
  if (manager || !var || !AllowedWrite(manager, playerID)) { return; }
  EXPG_RandomGarrisonModule zone = Zone(item);
  if (zone) { zone.RestoreSavedVector(var.GetVector()); }
 }
}

// Keeps other modules' attributes out of the Random Garrison dialog. Only a selection
// made entirely of Random Garrison zones is filtered; the same filter runs on server
// and owner, so ids, values and multi-select states stay aligned.
modded class SCR_AttributesManagerEditorComponent
{
 protected bool EXPG_OnlyRandomGarrisons(notnull array<Managed> items)
 {
  bool any;
  foreach (Managed item : items)
  {
   if (!item) { continue; }
   if (!EXPG_RandomGarrisonAttribute.Zone(item))
   {
    return false;
   }
   any = true;
  }
  return any;
 }

 override protected int GetVariables(bool onlyServer, notnull array<Managed> items, notnull out array<int> outIds, notnull out array<ref SCR_BaseEditorAttributeVar> outVars, notnull out array<ref EEditorAttributeMultiSelect> outAttributesMultiSelect)
 {
  int count = super.GetVariables(onlyServer, items, outIds, outVars, outAttributesMultiSelect);
  if (count <= 0 || !EXPG_OnlyRandomGarrisons(items))
  {
   return count;
  }
  SCR_AttributesManagerEditorComponentClass data = SCR_AttributesManagerEditorComponentClass.Cast(GetEditorComponentData());
  if (!data || outIds.Count() != outVars.Count() || outIds.Count() != outAttributesMultiSelect.Count())
  {
   return count;
  }
  for (int i = outIds.Count() - 1; i >= 0; i--)
  {
   if (EXPG_RandomGarrisonAttribute.Cast(data.GetAttribute(outIds[i]))) { continue; }
   outIds.RemoveOrdered(i);
   outVars.RemoveOrdered(i);
   outAttributesMultiSelect.RemoveOrdered(i);
  }
  return outVars.Count();
 }
}

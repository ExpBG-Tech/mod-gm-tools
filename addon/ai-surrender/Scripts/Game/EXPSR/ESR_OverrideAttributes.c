// "EXPBG Surrender & Intel" tab of an AI squad's and an AI soldier's Edit properties
// (ESR_Overrides): surrender chance, reveal squad, identity and reveal intel items, each
// "use the module setting" (squad) or "use the squad or module setting" (soldier), or
// 0-100 % in steps of 5. Shown while an EXPBG AI Surrender module is placed; values are
// read and written on the server only, so nothing is replicated for the dialog.
// Session saves: attribute-based savers (CDF Game Master Save) read set overrides with a
// null manager and write them back on load with playerID -1. CDF keys an attribute by its
// class name and occurrence, so every slot and target has its own class.
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class ESR_OverrideAttribute : SCR_BaseEditorAttribute
{
 protected ref array<ref SCR_EditorAttributeFloatStringValueHolder> m_aESR_Entries;

 // ESR_Overrides slot of this class.
 protected int Slot()
 {
  return ESR_Overrides.SURRENDER;
 }

 protected string UnsetName()
 {
  return "Use module setting";
 }

 // True when the item is a valid target; value is its current override (UNSET for none).
 protected bool ReadOverride(Managed item, out int value)
 {
  return false;
 }

 protected void WriteOverride(Managed item, int value, string origin)
 {
 }

 override bool IsSerializable()
 {
  return ESR_Overrides.IsSlot(Slot());
 }

 override int GetEntries(notnull array<ref SCR_BaseEditorAttributeEntry> outEntries)
 {
  if (!m_aESR_Entries)
  {
   m_aESR_Entries = {};
   for (int entry = 0; entry < ESR_Overrides.ENTRY_COUNT; entry++)
   {
    SCR_EditorAttributeFloatStringValueHolder holder = new SCR_EditorAttributeFloatStringValueHolder();
    int percent = ESR_Overrides.FromEntry(entry);
    if (entry == 0)
     holder.SetName(UnsetName());
    else
     holder.SetName(percent.ToString() + "%");
    holder.SetFloatValue(entry);
    m_aESR_Entries.Insert(holder);
   }
  }
  outEntries.Insert(new SCR_BaseEditorAttributeFloatStringValues(m_aESR_Entries));
  return outEntries.Count();
 }

 override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
 {
  if (!IsSerializable())
   return null;
  int value;
  if (!ReadOverride(item, value))
   return null;
  if (!manager)
  {
   // Session save: only overrides that are set.
   if (value < 0)
    return null;
  }
  else if (ESR_SurrenderModule.ActiveCount() <= 0)
  {
   return null;
  }
  return SCR_BaseEditorAttributeVar.CreateInt(ESR_Overrides.ToEntry(value));
 }

 override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var, SCR_AttributesManagerEditorComponent manager, int playerID)
 {
  if (!var || !IsSerializable() || !Replication.IsServer() || !GetGame() || !GetGame().InPlayMode())
   return;
  string origin = "save";
  if (!manager)
  {
   // Native session and CDF restoration use the server-only system-write contract.
   if (playerID != -1)
    return;
  }
  else
  {
   SCR_EditorManagerEntity editor = manager.GetManager();
   if (!editor || editor.GetPlayerID() != playerID || editor.IsLimited() || editor.GetCurrentMode() != EEditorMode.EDIT)
    return;
   origin = "Game Master";
  }
  WriteOverride(item, ESR_Overrides.FromEntry(var.GetInt()), origin);
 }
}

//------------------------------------------------------------------------------------------------
//! AI squads: not playable and without players.
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class ESR_GroupOverrideAttribute : ESR_OverrideAttribute
{
 static SCR_AIGroup GroupTarget(Managed item)
 {
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(item);
  if (!editable || editable.GetEntityType() != EEditableEntityType.GROUP)
   return null;
  SCR_AIGroup squad = SCR_AIGroup.Cast(editable.GetOwner());
  if (!squad || squad.IsPlayable() || squad.GetPlayerCount(true) > 0)
   return null;
  return squad;
 }

 override protected bool ReadOverride(Managed item, out int value)
 {
  SCR_AIGroup squad = GroupTarget(item);
  if (!squad)
   return false;
  value = squad.ESR_GetOverride(Slot());
  return true;
 }

 override protected void WriteOverride(Managed item, int value, string origin)
 {
  ESR_Overrides.SetGroup(GroupTarget(item), Slot(), value, origin);
 }
}

//------------------------------------------------------------------------------------------------
//! AI soldiers: an AI-controlled character nobody plays or possesses.
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class ESR_UnitOverrideAttribute : ESR_OverrideAttribute
{
 static SCR_ChimeraCharacter UnitTarget(Managed item)
 {
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(item);
  if (!editable || editable.GetEntityType() != EEditableEntityType.CHARACTER)
   return null;
  SCR_ChimeraCharacter soldier = SCR_ChimeraCharacter.Cast(editable.GetOwner());
  if (!soldier || !soldier.FindComponent(AIControlComponent) || ESR_SurrenderManager.IsPlayerCharacter(soldier))
   return null;
  return soldier;
 }

 override protected string UnsetName()
 {
  return "Use squad or module setting";
 }

 override protected bool ReadOverride(Managed item, out int value)
 {
  SCR_ChimeraCharacter soldier = UnitTarget(item);
  if (!soldier)
   return false;
  value = soldier.ESR_GetOverride(Slot());
  return true;
 }

 override protected void WriteOverride(Managed item, int value, string origin)
 {
  ESR_Overrides.SetUnit(UnitTarget(item), Slot(), value, origin);
 }
}

// Distinct classes: the editor rejects duplicate attribute types and CDF keys by class.
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class ESR_GroupSurrenderAttribute : ESR_GroupOverrideAttribute
{
 override protected int Slot()
 {
  return ESR_Overrides.SURRENDER;
 }
}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class ESR_GroupRevealAttribute : ESR_GroupOverrideAttribute
{
 override protected int Slot()
 {
  return ESR_Overrides.REVEAL;
 }
}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class ESR_GroupIdentityAttribute : ESR_GroupOverrideAttribute
{
 override protected int Slot()
 {
  return ESR_Overrides.IDENTITY;
 }
}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class ESR_GroupIntelAttribute : ESR_GroupOverrideAttribute
{
 override protected int Slot()
 {
  return ESR_Overrides.INTEL;
 }
}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class ESR_UnitSurrenderAttribute : ESR_UnitOverrideAttribute
{
 override protected int Slot()
 {
  return ESR_Overrides.SURRENDER;
 }
}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class ESR_UnitRevealAttribute : ESR_UnitOverrideAttribute
{
 override protected int Slot()
 {
  return ESR_Overrides.REVEAL;
 }
}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class ESR_UnitIdentityAttribute : ESR_UnitOverrideAttribute
{
 override protected int Slot()
 {
  return ESR_Overrides.IDENTITY;
 }
}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class ESR_UnitIntelAttribute : ESR_UnitOverrideAttribute
{
 override protected int Slot()
 {
  return ESR_Overrides.INTEL;
 }
}

// EXPBG Unit Scripts attributes (EXPBG Unit Scripts category). Values are read
// and written on the server; writes are authorized against the editor owner.
// Mission-only: ReadVariable answers only inside a live attribute session and
// IsSerializable is false, so native/CDF Game Master saves never replay them.

// AI soldier: Normal AI, Hold position, Freeze or one approved animation.
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EUS_UnitScriptAttribute : SCR_BaseFloatValueHolderEditorAttribute
{
 override bool IsSerializable()
 {
  return false;
 }

 protected SCR_ChimeraCharacter GetUnit(Managed item)
 {
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(item);
  if (!editable || editable.GetEntityType() != EEditableEntityType.CHARACTER || editable.HasEntityState(EEditableEntityState.PLAYER)) return null;
  SCR_ChimeraCharacter actor = SCR_ChimeraCharacter.Cast(editable.GetOwner());
  if (!actor || !EUS_UnitControl.Eligibility(actor).IsEmpty()) return null;
  return actor;
 }

 protected SCR_BaseEditorAttributeVar CreateCode(int code)
 {
  int index = ConvertValueToIndex(code);
  if (index < 0) index = ConvertValueToIndex(EUS_Codes.NONE);
  if (index < 0) return null;
  return SCR_BaseEditorAttributeVar.CreateInt(index);
 }

 protected bool ReadCode(SCR_BaseEditorAttributeVar var, out int code)
 {
  float value;
  if (!var || !ConvertIndexToValue(var.GetInt(), value)) return false;
  code = Math.Round(value);
  return true;
 }

 override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
 {
  if (!manager) return null;
  SCR_ChimeraCharacter actor = GetUnit(item);
  if (!actor) return null;
  return CreateCode(actor.EUS_Script);
 }

 override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var, SCR_AttributesManagerEditorComponent manager, int playerID)
 {
  if (Replication.IsClient() || !manager) return;
  SCR_ChimeraCharacter actor = GetUnit(item);
  int code;
  if (!actor || !ReadCode(var, code) || code == EUS_Codes.MIXED) return;
  EUS_Manager units = EUS_Manager.Get();
  if (units) units.ApplyAttribute(manager, playerID, actor, null, code);
 }
}

// AI squad: the same scripts for every living AI member. Shows "Mixed" when
// the members differ; leaving it there changes nothing.
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EUS_SquadScriptAttribute : EUS_UnitScriptAttribute
{
 protected SCR_AIGroup GetSquad(Managed item)
 {
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(item);
  if (!editable || editable.GetEntityType() != EEditableEntityType.GROUP) return null;
  SCR_AIGroup group = SCR_AIGroup.Cast(editable.GetOwner());
  if (!group || !EUS_DisciplineRecord.Eligibility(group).IsEmpty() || group.EXPG_Active) return null;
  return group;
 }

 override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
 {
  if (!manager) return null;
  SCR_AIGroup group = GetSquad(item);
  if (!group) return null;
  array<AIAgent> agents = {};
  group.GetAgents(agents);
  int shared = EUS_Codes.MIXED;
  bool first = true;
  foreach (AIAgent agent : agents)
  {
   SCR_ChimeraCharacter member = SCR_ChimeraCharacter.Cast(agent.GetControlledEntity());
   if (!member || !EUS_UnitControl.Eligibility(member).IsEmpty()) continue;
   if (first)
   {
    shared = member.EUS_Script;
    first = false;
   }
   else if (shared != member.EUS_Script)
   {
    shared = EUS_Codes.MIXED;
    break;
   }
  }
  if (first) return null;
  return CreateCode(shared);
 }

 override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var, SCR_AttributesManagerEditorComponent manager, int playerID)
 {
  if (Replication.IsClient() || !manager) return;
  SCR_AIGroup group = GetSquad(item);
  int code;
  if (!group || !ReadCode(var, code) || code == EUS_Codes.MIXED) return;
  EUS_Manager units = EUS_Manager.Get();
  if (units) units.ApplyAttribute(manager, playerID, null, group, code);
 }
}

// AI squad: Force Night Discipline (Off, Light Discipline, Terror Tactics).
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EUS_DisciplineAttribute : SCR_BaseFloatValueHolderEditorAttribute
{
 override bool IsSerializable()
 {
  return false;
 }

 protected SCR_AIGroup GetSquad(Managed item)
 {
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(item);
  if (!editable || editable.GetEntityType() != EEditableEntityType.GROUP) return null;
  SCR_AIGroup group = SCR_AIGroup.Cast(editable.GetOwner());
  if (!group || !EUS_DisciplineRecord.Eligibility(group).IsEmpty()) return null;
  return group;
 }

 override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
 {
  if (!manager) return null;
  SCR_AIGroup group = GetSquad(item);
  if (!group) return null;
  int index = ConvertValueToIndex(group.EUS_Discipline);
  if (index < 0) index = ConvertValueToIndex(EUS_Codes.DISCIPLINE_OFF);
  if (index < 0) return null;
  return SCR_BaseEditorAttributeVar.CreateInt(index);
 }

 override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var, SCR_AttributesManagerEditorComponent manager, int playerID)
 {
  if (Replication.IsClient() || !var || !manager) return;
  SCR_AIGroup group = GetSquad(item);
  float value;
  if (!group || !ConvertIndexToValue(var.GetInt(), value)) return;
  int mode = Math.Round(value);
  EUS_Manager units = EUS_Manager.Get();
  if (units) units.DisciplineAttribute(manager, playerID, group, mode);
 }
}

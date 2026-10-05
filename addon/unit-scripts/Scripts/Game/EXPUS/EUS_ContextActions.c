// EXPBG Unit Scripts context actions (Configs/Editor/ActionLists/Context/TempEdit.conf).
// Visibility uses replicated editable state only; the server re-checks every
// unit, authorizes the Game Master and replies with one summary.
[BaseContainerProps(), SCR_BaseContainerCustomTitleUIInfo("m_Info")]
class EUS_UnitContextAction : SCR_SelectedEntitiesContextAction
{
 protected int ActionCode()
 {
  return 0;
 }

 // The acting player, for authorization and the reply on the server.
 override int GetParam()
 {
  PlayerController controller = GetGame().GetPlayerController();
  if (!controller) return -1;
  return controller.GetPlayerId();
 }

 protected SCR_ChimeraCharacter AIUnit(SCR_EditableEntityComponent editable)
 {
  if (!editable || editable.GetEntityType() != EEditableEntityType.CHARACTER || editable.HasEntityState(EEditableEntityState.PLAYER) || editable.IsDestroyed()) return null;
  return SCR_ChimeraCharacter.Cast(editable.GetOwner());
 }

 protected SCR_AIGroup AISquad(SCR_EditableEntityComponent editable)
 {
  if (!editable || editable.GetEntityType() != EEditableEntityType.GROUP) return null;
  SCR_AIGroup group = SCR_AIGroup.Cast(editable.GetOwner());
  if (!group || group.GetPlayerCount() > 0 || group.IsPlayable()) return null;
  return group;
 }

 // A soldier's squad through the replicated editable hierarchy (clients have no AI agents).
 protected SCR_AIGroup SquadOf(SCR_EditableEntityComponent editable)
 {
  SCR_AIGroup group = AISquad(editable);
  if (group != null || AIUnit(editable) == null) return group;
  return AISquad(editable.GetAIGroup());
 }

 override bool CanBeShown(SCR_EditableEntityComponent selectedEntity, vector cursorWorldPosition, int flags)
 {
  return AIUnit(selectedEntity) != null || AISquad(selectedEntity) != null;
 }

 override bool CanBePerformed(SCR_EditableEntityComponent selectedEntity, vector cursorWorldPosition, int flags)
 {
  return CanBeShown(selectedEntity, cursorWorldPosition, flags);
 }

 override void Perform(SCR_EditableEntityComponent hoveredEntity, notnull set<SCR_EditableEntityComponent> selectedEntities, vector cursorWorldPosition, int flags, int param = -1)
 {
  if (!Replication.IsServer()) return;
  EUS_Manager manager = EUS_Manager.Get();
  if (manager) manager.PerformEditorAction(ActionCode(), selectedEntities, param);
 }
}

[BaseContainerProps(), SCR_BaseContainerCustomTitleUIInfo("m_Info")]
class EUS_HoldContextAction : EUS_UnitContextAction
{
 override protected int ActionCode()
 {
  return EUS_Codes.ACTION_HOLD;
 }
}

[BaseContainerProps(), SCR_BaseContainerCustomTitleUIInfo("m_Info")]
class EUS_FreezeContextAction : EUS_UnitContextAction
{
 override protected int ActionCode()
 {
  return EUS_Codes.ACTION_FREEZE;
 }
}

// Shown only when a selected unit runs a script or a selected squad has
// scripted members or night discipline.
[BaseContainerProps(), SCR_BaseContainerCustomTitleUIInfo("m_Info")]
class EUS_ReleaseContextAction : EUS_UnitContextAction
{
 override protected int ActionCode()
 {
  return EUS_Codes.ACTION_RELEASE;
 }

 override bool CanBeShown(SCR_EditableEntityComponent selectedEntity, vector cursorWorldPosition, int flags)
 {
  SCR_ChimeraCharacter actor = AIUnit(selectedEntity);
  if (actor) return actor.EUS_Script != EUS_Codes.NONE;
  SCR_AIGroup group = AISquad(selectedEntity);
  if (!group) return false;
  return group.EUS_Scripted > 0 || group.EUS_Discipline != EUS_Codes.DISCIPLINE_OFF;
 }
}

// Discipline is per squad; a selected soldier stands for his squad.
[BaseContainerProps(), SCR_BaseContainerCustomTitleUIInfo("m_Info")]
class EUS_LightDisciplineContextAction : EUS_UnitContextAction
{
 protected int DisciplineMode()
 {
  return EUS_Codes.DISCIPLINE_LIGHT;
 }

 override protected int ActionCode()
 {
  return EUS_Codes.ACTION_LIGHT;
 }

 override bool CanBeShown(SCR_EditableEntityComponent selectedEntity, vector cursorWorldPosition, int flags)
 {
  SCR_AIGroup group = SquadOf(selectedEntity);
  if (!group) return false;
  return group.EUS_Discipline != DisciplineMode();
 }
}

[BaseContainerProps(), SCR_BaseContainerCustomTitleUIInfo("m_Info")]
class EUS_TerrorTacticsContextAction : EUS_LightDisciplineContextAction
{
 override protected int DisciplineMode()
 {
  return EUS_Codes.DISCIPLINE_TERROR;
 }

 override protected int ActionCode()
 {
  return EUS_Codes.ACTION_TERROR;
 }
}

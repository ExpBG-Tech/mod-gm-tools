// EXPBG Unit Scripts context actions (Configs/Editor/ActionLists/Context/TempEdit.conf).
// Only the quick actions live here: Hold Position, Freeze and Release. Night
// discipline is the "EXPBG Night discipline" attribute in the group's Group tab
// (EUS_DisciplineAttribute) and animations are in the EXPBG Unit Scripts tab.
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

 override bool CanBeShown(SCR_EditableEntityComponent selectedEntity, vector cursorWorldPosition, int flags)
 {
  return AIUnit(selectedEntity) != null || AISquad(selectedEntity) != null;
 }

 override bool CanBePerformed(SCR_EditableEntityComponent selectedEntity, vector cursorWorldPosition, int flags)
 {
  return CanBeShown(selectedEntity, cursorWorldPosition, flags);
 }

 // A soldier already running this script does not list it again (re-applying only
 // re-binds him where he stands; moving him with the editor sets his new spot). A
 // squad always lists it, since its members may differ.
 protected bool ShownUnlessRunning(SCR_EditableEntityComponent selectedEntity, int code)
 {
  SCR_ChimeraCharacter actor = AIUnit(selectedEntity);
  if (actor) return actor.EUS_Script != code;
  return AISquad(selectedEntity) != null;
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

 override bool CanBeShown(SCR_EditableEntityComponent selectedEntity, vector cursorWorldPosition, int flags)
 {
  return ShownUnlessRunning(selectedEntity, EUS_Codes.HOLD);
 }
}

[BaseContainerProps(), SCR_BaseContainerCustomTitleUIInfo("m_Info")]
class EUS_FreezeContextAction : EUS_UnitContextAction
{
 override protected int ActionCode()
 {
  return EUS_Codes.ACTION_FREEZE;
 }

 override bool CanBeShown(SCR_EditableEntityComponent selectedEntity, vector cursorWorldPosition, int flags)
 {
  return ShownUnlessRunning(selectedEntity, EUS_Codes.FREEZE);
 }
}

// Shown only when a selected unit runs a script or a selected squad has
// scripted members or night discipline (Release on a squad also sets its
// night discipline back to None).
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

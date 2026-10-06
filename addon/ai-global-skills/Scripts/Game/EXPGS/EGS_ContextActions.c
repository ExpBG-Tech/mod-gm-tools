// Right-click rules of engagement for selected AI groups or soldiers (their group).
// Executed on the server (m_bIsServer 1); visible only while an EXPBG AI Global Skills
// module exists. The option already in effect for every selected group is hidden.
[BaseContainerProps(), SCR_BaseContainerCustomTitleUIInfo("m_Info")]
class EGS_RoeContextAction : SCR_SelectedEntitiesContextAction
{
	[Attribute("0", desc: "Group override: 0 module default, 1 return fire only, 2 fire on sight, 3 warning shots first, 4 exempt")]
	protected int m_iRoe;

	//------------------------------------------------------------------------------------------------
	override bool CanBeShown(SCR_EditableEntityComponent hoveredEntity, notnull set<SCR_EditableEntityComponent> selectedEntities, vector cursorWorldPosition, int flags)
	{
		if (!EGS_Module.HasAny())
			return false;

		// Local Game Master UI only; the server trusts the owner's editor RPC like vanilla.
		if (!Replication.IsServer() || !Replication.IsRunning())
		{
			SCR_EditorManagerEntity editor = SCR_EditorManagerEntity.GetInstance();
			if (editor && (editor.IsLimited() || editor.GetCurrentMode() != EEditorMode.EDIT))
				return false;
		}

		return super.CanBeShown(hoveredEntity, selectedEntities, cursorWorldPosition, flags);
	}

	//------------------------------------------------------------------------------------------------
	override bool CanBeShown(SCR_EditableEntityComponent selectedEntity, vector cursorWorldPosition, int flags)
	{
		SCR_AIGroup group = EGS_Manager.ResolveGroup(selectedEntity);
		return group && group.EGS_GetRoeOverride() != m_iRoe;
	}

	//------------------------------------------------------------------------------------------------
	override bool CanBePerformed(SCR_EditableEntityComponent selectedEntity, vector cursorWorldPosition, int flags)
	{
		return EGS_Manager.ResolveGroup(selectedEntity) != null;
	}

	//------------------------------------------------------------------------------------------------
	override void Perform(SCR_EditableEntityComponent selectedEntity, vector cursorWorldPosition)
	{
		if (Replication.IsServer())
			EGS_Manager.SetGroupRoe(EGS_Manager.ResolveGroup(selectedEntity), m_iRoe);
	}
}

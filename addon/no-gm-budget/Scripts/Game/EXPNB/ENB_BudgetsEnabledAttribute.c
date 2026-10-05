// Game Settings checkbox for the No Game Master Budget switch. Server-authoritative.
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class ENB_BudgetsEnabledAttribute : SCR_BaseEditorAttribute
{
	//------------------------------------------------------------------------------------------------
	override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
	{
		if (!IsGameMode(item))
			return null;

		SCR_BaseGameMode gameMode = SCR_BaseGameMode.Cast(item);
		if (!gameMode)
			return null;

		return SCR_BaseEditorAttributeVar.CreateBool(gameMode.ENB_GetBudgetsEnabled());
	}

	//------------------------------------------------------------------------------------------------
	override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var, SCR_AttributesManagerEditorComponent manager, int playerID)
	{
		if (!var)
			return;

		SCR_BaseGameMode gameMode = SCR_BaseGameMode.Cast(item);
		if (!gameMode)
			return;

		gameMode.ENB_SetBudgetsEnabled(var.GetBool());
	}
}

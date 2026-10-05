// While Game Master budgets are OFF: every spawn counts as affordable, the selected
// prefab is kept when a budget owner changes, and reaching the maximum of a Game Master
// budget (props, AI, vehicles, waypoints, systems) no longer clears the selection.
modded class SCR_PlacingEditorComponent
{
	//------------------------------------------------------------------------------------------------
	protected static bool ENB_IsLiftedBudget(EEditableEntityBudget budget)
	{
		return budget == EEditableEntityBudget.PROPS || budget == EEditableEntityBudget.AI
			|| budget == EEditableEntityBudget.VEHICLES || budget == EEditableEntityBudget.WAYPOINTS
			|| budget == EEditableEntityBudget.SYSTEMS;
	}

	//------------------------------------------------------------------------------------------------
	override bool IsThereEnoughBudgetToSpawn(IEntityComponentSource entitySource)
	{
		if (!SCR_BaseGameMode.ENB_AreBudgetsEnabled())
			return true;

		return super.IsThereEnoughBudgetToSpawn(entitySource);
	}

	//------------------------------------------------------------------------------------------------
	override protected void CheckBudgetOwner()
	{
		if (!SCR_BaseGameMode.ENB_AreBudgetsEnabled())
			return;

		super.CheckBudgetOwner();
	}

	//------------------------------------------------------------------------------------------------
	override protected void OnBudgetMaxReached(EEditableEntityBudget entityBudget, bool maxReached)
	{
		if (!SCR_BaseGameMode.ENB_AreBudgetsEnabled() && ENB_IsLiftedBudget(entityBudget))
			return;

		super.OnBudgetMaxReached(entityBudget, maxReached);
	}
}

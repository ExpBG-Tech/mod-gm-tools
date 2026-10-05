// EXPBG No Game Master Budget: one replicated game-mode switch.
// ON (default) keeps every vanilla Game Master budget. OFF lifts the Game Master
// placement budgets; campaign building keeps its own supply budgets.
modded class SCR_BaseGameMode
{
	[RplProp(onRplName: "ENB_OnBudgetsReplicated")]
	protected bool m_bENB_BudgetsEnabled = true;

	//------------------------------------------------------------------------------------------------
	//! True while Game Master budgets apply. Reads the replicated value of the current game
	//! mode, so late joiners and restarted missions never act on a stale copy.
	static bool ENB_AreBudgetsEnabled()
	{
		SCR_BaseGameMode gameMode = SCR_BaseGameMode.Cast(GetGame().GetGameMode());
		if (!gameMode)
			return true;

		return gameMode.m_bENB_BudgetsEnabled;
	}

	//------------------------------------------------------------------------------------------------
	bool ENB_GetBudgetsEnabled()
	{
		return m_bENB_BudgetsEnabled;
	}

	//------------------------------------------------------------------------------------------------
	//! Server only: change the switch, replicate it and apply it locally at once.
	void ENB_SetBudgetsEnabled(bool enabled)
	{
		if (!Replication.IsServer() || m_bENB_BudgetsEnabled == enabled)
			return;

		m_bENB_BudgetsEnabled = enabled;
		Replication.BumpMe();
		ENB_OnBudgetsReplicated();
	}

	//------------------------------------------------------------------------------------------------
	protected void ENB_OnBudgetsReplicated()
	{
		SCR_BudgetEditorComponent.ENB_ApplyToAll(m_bENB_BudgetsEnabled);
		PrintFormat("[EXPBG NO BUDGET] Game Master budgets enabled=%1", m_bENB_BudgetsEnabled);
	}
}

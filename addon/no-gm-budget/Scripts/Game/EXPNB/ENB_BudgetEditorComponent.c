// While Game Master budgets are OFF, the budget cap is disabled and the displayed
// maximum of each Game Master budget is raised 500x. Switching back ON restores the
// authored maximums immediately. Campaign building budgets are never touched.
modded class SCR_BudgetEditorComponent
{
	protected static const int ENB_RAISED_FACTOR = 500;

	// Non-owning: entries become null when their editor component is deleted.
	protected static ref array<SCR_BudgetEditorComponent> s_aENB_Components;

	protected ref map<EEditableEntityBudget, int> m_mENB_AuthoredMax;
	protected bool m_bENB_Raised;

	//------------------------------------------------------------------------------------------------
	//! Apply the switch to every live Game Master budget component on this machine.
	static void ENB_ApplyToAll(bool budgetsEnabled)
	{
		EXPBG_LazyStatics_SCR_BudgetEditorComponent();
		for (int i = s_aENB_Components.Count() - 1; i >= 0; i--)
		{
			SCR_BudgetEditorComponent component = s_aENB_Components[i];
			if (!component)
			{
				s_aENB_Components.Remove(i);
				continue;
			}

			component.ENB_Apply(budgetsEnabled);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected bool ENB_IsGameMasterBudget()
	{
		return !SCR_CampaignBuildingBudgetEditorComponent.Cast(this);
	}

	//------------------------------------------------------------------------------------------------
	protected void ENB_Register()
	{
		EXPBG_LazyStatics_SCR_BudgetEditorComponent();
		if (!ENB_IsGameMasterBudget())
			return;

		if (!m_mENB_AuthoredMax)
		{
			m_mENB_AuthoredMax = new map<EEditableEntityBudget, int>();
			if (m_MaxBudgets)
			{
				foreach (SCR_EntityBudgetValue maxBudget : m_MaxBudgets)
				{
					if (maxBudget)
						m_mENB_AuthoredMax.Set(maxBudget.GetBudgetType(), maxBudget.GetBudgetValue());
				}
			}
		}

		if (!s_aENB_Components.Contains(this))
			s_aENB_Components.Insert(this);

		ENB_Apply(SCR_BaseGameMode.ENB_AreBudgetsEnabled());
	}

	//------------------------------------------------------------------------------------------------
	//! Raise (budgets OFF) or restore (budgets ON) the authored maximum of each budget.
	void ENB_Apply(bool budgetsEnabled)
	{
		if (!m_mENB_AuthoredMax || !m_MaxBudgets)
			return;

		bool raise = !budgetsEnabled;
		if (raise == m_bENB_Raised)
			return;

		m_bENB_Raised = raise;
		foreach (SCR_EntityBudgetValue maxBudget : m_MaxBudgets)
		{
			if (!maxBudget)
				continue;

			int authored;
			if (!m_mENB_AuthoredMax.Find(maxBudget.GetBudgetType(), authored))
				continue;

			if (raise)
				maxBudget.SetBudgetValue(authored * ENB_RAISED_FACTOR);
			else
				maxBudget.SetBudgetValue(authored);
		}

		// Re-announce maximums so open Game Master budget displays update.
		if (m_EntityCore)
			RefreshBudgetSettings();
	}

	//------------------------------------------------------------------------------------------------
	override protected bool IsBudgetCapEnabled()
	{
		if (ENB_IsGameMasterBudget() && !SCR_BaseGameMode.ENB_AreBudgetsEnabled())
			return false;

		return super.IsBudgetCapEnabled();
	}

	//------------------------------------------------------------------------------------------------
	override protected void EOnEditorInit()
	{
		super.EOnEditorInit();
		ENB_Register();
	}

	//------------------------------------------------------------------------------------------------
	override protected void EOnEditorInitServer()
	{
		super.EOnEditorInitServer();
		ENB_Register();
	}

	//------------------------------------------------------------------------------------------------
	//! Creates the collections on first use (not in the global static initializer, which has a
	//! per-function instruction limit that large modsets exceed on Windows).
	protected static void EXPBG_LazyStatics_SCR_BudgetEditorComponent()
	{
		if (!s_aENB_Components)
			s_aENB_Components = new array<SCR_BudgetEditorComponent>();
	}
}

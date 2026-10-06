// Rules of engagement per AI group (server state; the GM override is replicated so the
// context menu and late joiners see it).
// Return Fire Only -> vanilla RETURN_FIRE. Fire on Sight -> vanilla FIRE_AT_WILL.
// Warning Shots First -> RETURN_FIRE while armed (they still answer fire); when a member
// selects a player as target, that member fires a short burst beside the player, the group
// waits WARNING_PAUSE_S and then switches to FIRE_AT_WILL. After the contact ends the group
// re-arms. Vanilla restores the combat mode the group had before EXPBG touched it.
modded class SCR_AIGroup
{
	protected static const int EGS_WARN_ARMED = 0;
	protected static const int EGS_WARN_PENDING = 1;
	protected static const int EGS_WARN_LETHAL = 2;

	[RplProp(), NonSerialized()]
	protected int m_iEGS_RoeOverride;

	protected int m_iEGS_AppliedRoe;
	protected bool m_bEGS_RoeTouched;
	protected EAIGroupCombatMode m_eEGS_OriginalMode;
	protected int m_iEGS_WarnState;
	protected bool m_bEGS_WarnEnded;
	protected int m_iEGS_Token;
	protected int m_iEGS_Epoch = -1;
	protected SCR_AICombatComponent m_EGS_WarningShooter;

	//------------------------------------------------------------------------------------------------
	override void OnAgentAdded(AIAgent child)
	{
		super.OnAgentAdded(child);
		if (Replication.IsServer() && GetGame().InPlayMode())
			EGS_Manager.OnMemberChanged(this, child);
	}

	//------------------------------------------------------------------------------------------------
	override void OnAgentRemoved(AIAgent child)
	{
		super.OnAgentRemoved(child);
		if (Replication.IsServer() && GetGame().InPlayMode())
			EGS_Manager.OnMemberChanged(this, null);
	}

	//------------------------------------------------------------------------------------------------
	override void OnLeaderChanged(AIAgent currentLeader, AIAgent prevLeader)
	{
		super.OnLeaderChanged(currentLeader, prevLeader);
		if (!Replication.IsServer() || !GetGame().InPlayMode())
			return;

		if (currentLeader)
			EGS_Manager.QueueUnit(currentLeader.GetControlledEntity());

		if (prevLeader)
			EGS_Manager.QueueUnit(prevLeader.GetControlledEntity());
	}

	//------------------------------------------------------------------------------------------------
	//! Player groups and their AI recruits keep the player's own orders.
	bool EGS_IsManaged()
	{
		return !IsPlayable() && !IsSlave() && GetPlayerCount() == 0;
	}

	//------------------------------------------------------------------------------------------------
	//! Group override: 0 module default, 1 return fire, 2 fire on sight, 3 warning shots, 4 exempt.
	int EGS_GetRoeOverride()
	{
		return m_iEGS_RoeOverride;
	}

	//------------------------------------------------------------------------------------------------
	void EGS_SetRoeOverride(int value)
	{
		value = Math.ClampInt(value, EGS_Settings.GROUP_ROE_DEFAULT, EGS_Settings.GROUP_ROE_EXEMPT);
		if (!Replication.IsServer() || value == m_iEGS_RoeOverride)
			return;

		m_iEGS_RoeOverride = value;
		Replication.BumpMe();
	}

	//------------------------------------------------------------------------------------------------
	int EGS_GetAppliedRoe()
	{
		return m_iEGS_AppliedRoe;
	}

	//------------------------------------------------------------------------------------------------
	int EGS_GetEpoch()
	{
		return m_iEGS_Epoch;
	}

	//------------------------------------------------------------------------------------------------
	void EGS_SetEpoch(int epoch)
	{
		m_iEGS_Epoch = epoch;
	}

	//------------------------------------------------------------------------------------------------
	//! Server: apply an effective ROE. Re-applying the same ROE keeps the warning state and
	//! any vanilla combat mode a Game Master chose meanwhile.
	void EGS_ApplyRoe(int roe)
	{
		if (roe == m_iEGS_AppliedRoe)
			return;

		SCR_AIGroupUtilityComponent utility = GetGroupUtilityComponent();
		if (!utility)
			return;

		EGS_StopShooter();
		m_iEGS_Token++;
		m_iEGS_WarnState = EGS_WARN_ARMED;
		m_bEGS_WarnEnded = false;
		m_iEGS_AppliedRoe = roe;

		if (roe == EGS_Settings.ROE_VANILLA)
		{
			if (m_bEGS_RoeTouched)
				utility.SetCombatMode(m_eEGS_OriginalMode);

			m_bEGS_RoeTouched = false;
			return;
		}

		if (!m_bEGS_RoeTouched)
		{
			m_eEGS_OriginalMode = utility.GetCombatModeExternal();
			m_bEGS_RoeTouched = true;
		}

		if (roe == EGS_Settings.ROE_FIRE_ON_SIGHT)
			utility.SetCombatMode(EAIGroupCombatMode.FIRE_AT_WILL);
		else
			utility.SetCombatMode(EAIGroupCombatMode.RETURN_FIRE);
	}

	//------------------------------------------------------------------------------------------------
	//! Server: a member selected a new hostile target (only called while Warning Shots First).
	void EGS_OnHostileSelected(SCR_AICombatComponent spotter, IEntity target)
	{
		if (m_iEGS_AppliedRoe != EGS_Settings.ROE_WARNING_SHOTS || m_iEGS_WarnState != EGS_WARN_ARMED)
			return;

		if (!EGS_Manager.IsPlayerControlled(target))
			return;

		m_iEGS_Token++;
		m_iEGS_WarnState = EGS_WARN_PENDING;
		m_bEGS_WarnEnded = false;
		int token = m_iEGS_Token;

		float window = 0;
		if (spotter && spotter.EGS_StartWarningShots(target, this, token))
		{
			m_EGS_WarningShooter = spotter;
			window = EGS_Manager.WARNING_WINDOW_S;
		}

		PrintFormat("[EXPBG AI SKILLS] warning shots group=%1 shooter=%2 target=%3", this, spotter, target);
		EGS_Manager.ScheduleGroup(this, EGS_Manager.TIMER_WARNING_END, window, token);
	}

	//------------------------------------------------------------------------------------------------
	//! Server: the shooter fired its warning rounds.
	void EGS_OnWarningShotsDone(int token)
	{
		EGS_EndWarning(token);
	}

	//------------------------------------------------------------------------------------------------
	protected void EGS_EndWarning(int token)
	{
		if (token != m_iEGS_Token || m_iEGS_WarnState != EGS_WARN_PENDING || m_bEGS_WarnEnded)
			return;

		m_bEGS_WarnEnded = true;
		EGS_StopShooter();
		EGS_Manager.ScheduleGroup(this, EGS_Manager.TIMER_LETHAL, EGS_Manager.WARNING_PAUSE_S, token);
	}

	//------------------------------------------------------------------------------------------------
	//! Ends the warning burst (fewer rounds than planned, ROE change); no-op when already done.
	protected void EGS_StopShooter()
	{
		SCR_AICombatComponent shooter = m_EGS_WarningShooter;
		m_EGS_WarningShooter = null;
		if (shooter)
			shooter.EGS_EndWarningShots(false);
	}

	//------------------------------------------------------------------------------------------------
	//! Server: timers driven by EGS_Manager (bounded shared tick, no per-group call queue).
	void EGS_OnTimer(int kind, int token)
	{
		if (token != m_iEGS_Token || m_iEGS_AppliedRoe != EGS_Settings.ROE_WARNING_SHOTS)
			return;

		SCR_AIGroupUtilityComponent utility = GetGroupUtilityComponent();
		if (!utility)
			return;

		if (kind == EGS_Manager.TIMER_WARNING_END)
		{
			EGS_EndWarning(token);
			return;
		}

		if (kind == EGS_Manager.TIMER_LETHAL && m_iEGS_WarnState == EGS_WARN_PENDING)
		{
			m_iEGS_WarnState = EGS_WARN_LETHAL;
			utility.SetCombatMode(EAIGroupCombatMode.FIRE_AT_WILL);
			EGS_Manager.ScheduleGroup(this, EGS_Manager.TIMER_REARM, EGS_Manager.REARM_DELAY_S, token);
			return;
		}

		if (kind == EGS_Manager.TIMER_REARM && m_iEGS_WarnState == EGS_WARN_LETHAL)
		{
			if (EGS_HasTarget())
			{
				EGS_Manager.ScheduleGroup(this, EGS_Manager.TIMER_REARM, EGS_Manager.REARM_RETRY_S, token);
				return;
			}

			m_iEGS_WarnState = EGS_WARN_ARMED;
			utility.SetCombatMode(EAIGroupCombatMode.RETURN_FIRE);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected bool EGS_HasTarget()
	{
		array<AIAgent> agents = {};
		GetAgents(agents);
		foreach (AIAgent agent : agents)
		{
			if (!agent)
				continue;

			IEntity member = agent.GetControlledEntity();
			if (!member)
				continue;

			SCR_AICombatComponent combat = SCR_AICombatComponent.Cast(member.FindComponent(SCR_AICombatComponent));
			if (combat && combat.GetCurrentTarget())
				return true;
		}

		return false;
	}
}

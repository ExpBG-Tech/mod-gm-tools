// Rules of engagement per AI group (server state; the GM override, set with the EXPBG ROE
// group attribute, is replicated so clients and late joiners see it).
// Return Fire Only -> vanilla RETURN_FIRE as the external mode, with the EXPBG "fired upon"
// rule for the actual mode (EGS_UnitRoe.c, EGS_Provocation.c): the group holds fire until a
// member is hit or killed by an enemy, an enemy round passes within a few metres of a member
// or an enemy explosion goes off close by; then it fires at will until the contact is over.
// (Vanilla's own RETURN_FIRE fires as soon as an enemy shot's line passes within 13 m of the
// squad leader, an enemy gunshot goes off within 15 m of him or an enemy grenade lands near
// a member, whatever it was aimed at, and keeps firing for the whole contact; a hit on a
// member other than the leader does not count there.)
// Fire on Sight -> vanilla FIRE_AT_WILL.
// Warning Shots First -> as Return Fire Only while armed (they still answer fire); when a
// member selects a player, or a vehicle with a player inside, as target, that member (or
// the next member who sees it, if he cannot fire) fires a short burst beside it, the group
// waits WARNING_PAUSE_S and then switches to FIRE_AT_WILL. After the contact ends the group
// re-arms. Vanilla restores the combat mode the group had before EXPBG touched it. A Game
// Master's vanilla "Set combat mode" on a squad EXPBG steers becomes that squad's own mode
// and switches its EXPBG ROE to Exempt (vanilla) (EGS_Manager.OnGameMasterCombatMode).
modded class SCR_AIGroup
{
	protected static const int EGS_WARN_ARMED = 0;
	protected static const int EGS_WARN_PENDING = 1;
	protected static const int EGS_WARN_LETHAL = 2;
	protected static const int EGS_MAX_SHOOTER_TRIES = 32;

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

	// "Fired upon" state (EGS_Provocation.c): watched while the group's ROE is Return Fire Only
	// or Warning Shots First, or while a member has one of these as his own ROE.
	protected bool m_bEGS_Provoked;
	protected bool m_bEGS_OwnRoeMember;
	protected int m_iEGS_ProvokeToken;
	protected float m_fEGS_ProvokedAt;
	// Game Master save (EGS_Manager.OpenGameMasterSave) that last wrote this group's EXPBG ROE.
	protected int m_iEGS_RoeSave = -1;

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
	//! Warning-shot state: 0 armed, 1 warning (burst and pause), 2 lethal (tests, diagnostics).
	int EGS_GetWarnState()
	{
		return m_iEGS_WarnState;
	}

	//------------------------------------------------------------------------------------------------
	//! True while EXPBG sets this group's combat mode (an EXPBG ROE other than vanilla).
	bool EGS_IsRoeTouched()
	{
		return m_bEGS_RoeTouched;
	}

	//------------------------------------------------------------------------------------------------
	//! The combat mode this group had before the EXPBG rules of engagement changed it (its
	//! own mode, restored by Exempt (vanilla) and when the last module is deleted).
	bool EGS_GetOriginalMode(out EAIGroupCombatMode mode)
	{
		if (!m_bEGS_RoeTouched)
			return false;

		mode = m_eEGS_OriginalMode;
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! The external combat mode the applied EXPBG ROE sets (meaningful while touched).
	EAIGroupCombatMode EGS_ExpectedMode()
	{
		if (m_iEGS_AppliedRoe == EGS_Settings.ROE_FIRE_ON_SIGHT)
			return EAIGroupCombatMode.FIRE_AT_WILL;

		if (m_iEGS_AppliedRoe == EGS_Settings.ROE_WARNING_SHOTS && m_iEGS_WarnState == EGS_WARN_LETHAL)
			return EAIGroupCombatMode.FIRE_AT_WILL;

		return EAIGroupCombatMode.RETURN_FIRE;
	}

	//------------------------------------------------------------------------------------------------
	//! Return Fire Only, or Warning Shots First before it turned lethal: the actual combat mode
	//! follows the EXPBG "fired upon" rule (SCR_AIGroupUtilityComponent.EvaluateCombatMode).
	bool EGS_UsesStrictReturnFire()
	{
		if (!m_bEGS_RoeTouched)
			return false;

		if (m_iEGS_AppliedRoe == EGS_Settings.ROE_RETURN_FIRE)
			return true;

		return m_iEGS_AppliedRoe == EGS_Settings.ROE_WARNING_SHOTS && m_iEGS_WarnState != EGS_WARN_LETHAL;
	}

	//------------------------------------------------------------------------------------------------
	//! The provocation hooks (EGS_Provocation.c) only look at watched groups. A player's group
	//! is never watched for its soldiers' own ROE (they follow it there, EGS_CombatComponent.c).
	bool EGS_WatchesProvocation()
	{
		if (m_bEGS_OwnRoeMember && EGS_IsManaged())
			return true;

		return m_bEGS_RoeTouched && (m_iEGS_AppliedRoe == EGS_Settings.ROE_RETURN_FIRE || m_iEGS_AppliedRoe == EGS_Settings.ROE_WARNING_SHOTS);
	}

	//------------------------------------------------------------------------------------------------
	//! A member has Return Fire Only or Warning Shots First as his own ROE (EGS_Manager.ApplyUnit).
	void EGS_SetOwnRoeMember(bool value)
	{
		m_bEGS_OwnRoeMember = value;
	}

	//------------------------------------------------------------------------------------------------
	//! True from the first provocation until the contact is over (EGS_OnCalmTimer).
	bool EGS_IsProvoked()
	{
		return m_bEGS_Provoked;
	}

	//------------------------------------------------------------------------------------------------
	//! Server: an enemy fired upon this group (EGS_Provocation.c). Logged once per contact;
	//! a later provocation only extends it.
	void EGS_Provoke(string reason, IEntity source)
	{
		if (!Replication.IsServer() || !EGS_WatchesProvocation())
			return;

		m_fEGS_ProvokedAt = EGS_Manager.Now();
		if (m_bEGS_Provoked)
			return;

		m_bEGS_Provoked = true;
		m_iEGS_ProvokeToken++;
		EGS_Log(string.Format("group=%1 fired upon (%2, source=%3): returns fire", this, reason, source));
		EGS_Manager.ScheduleGroup(this, EGS_Manager.TIMER_CALM, EGS_Manager.REARM_DELAY_S, m_iEGS_ProvokeToken);
	}

	//------------------------------------------------------------------------------------------------
	//! Server: a Game Master save wrote this group's EXPBG ROE (EGS_Manager.SetGroupRoe).
	void EGS_StampGameMasterRoe()
	{
		m_iEGS_RoeSave = EGS_Manager.OpenGameMasterSave();
	}

	//------------------------------------------------------------------------------------------------
	//! True inside the Game Master save that also wrote this group's EXPBG ROE (attributes of
	//! one save are written in one call; the save ends on the next call-queue tick, also while
	//! a single-player Game Master pause stops world time).
	bool EGS_GameMasterRoeThisSave()
	{
		if (m_iEGS_RoeSave < 0)
			return false;

		return EGS_Manager.IsGameMasterSaveOpen(m_iEGS_RoeSave);
	}

	//------------------------------------------------------------------------------------------------
	//! Server log line of the AI Global Skills group logic (bounded: one per event above).
	void EGS_Log(string line)
	{
		PrintFormat("[EXPBG AI SKILLS] %1", line);
	}

	//------------------------------------------------------------------------------------------------
	//! Server: apply an effective ROE. Re-applying the same ROE keeps the warning state and
	//! any vanilla combat mode a Game Master chose meanwhile; with reassert (a Game Master saved
	//! this group's EXPBG ROE) a combat mode changed outside EXPBG is put back under it.
	void EGS_ApplyRoe(int roe, bool reassert = false)
	{
		if (roe == m_iEGS_AppliedRoe)
		{
			if (reassert)
				EGS_ReassertMode();

			return;
		}

		SCR_AIGroupUtilityComponent utility = GetGroupUtilityComponent();
		if (!utility)
			return;

		EGS_StopShooter();
		m_iEGS_Token++;
		m_iEGS_WarnState = EGS_WARN_ARMED;
		m_bEGS_WarnEnded = false;
		m_iEGS_AppliedRoe = roe;
		// A new ROE starts unprovoked; a pending calm-down timer finds a stale token.
		m_bEGS_Provoked = false;
		m_iEGS_ProvokeToken++;

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
	//! Something other than EXPBG (another addon's attribute, a waypoint) changed the combat mode
	//! of a group EXPBG steers: keep that mode as the group's own and put the EXPBG mode back.
	protected void EGS_ReassertMode()
	{
		SCR_AIGroupUtilityComponent utility = GetGroupUtilityComponent();
		if (!utility || !m_bEGS_RoeTouched)
			return;

		EAIGroupCombatMode current = utility.GetCombatModeExternal();
		EAIGroupCombatMode expected = EGS_ExpectedMode();
		if (current == expected)
			return;

		m_eEGS_OriginalMode = current;
		utility.SetCombatMode(expected);
		EGS_Log(string.Format("group=%1 combat mode %2 was set outside EXPBG: kept as its own mode, EXPBG mode %3 applied again", this, current, expected));
	}

	//------------------------------------------------------------------------------------------------
	//! Server: a Unit Caching wake, a native load or a session load has just written this
	//! group's own combat mode. Keep it as the mode Exempt (vanilla) restores and put the EXPBG
	//! mode back. No-op while EXPBG does not steer the group (it records the mode when it does).
	void EGS_AdoptVanillaMode()
	{
		SCR_AIGroupUtilityComponent utility = GetGroupUtilityComponent();
		if (!utility || !m_bEGS_RoeTouched)
			return;

		m_eEGS_OriginalMode = utility.GetCombatModeExternal();
		utility.SetCombatMode(EGS_ExpectedMode());
	}

	//------------------------------------------------------------------------------------------------
	//! Server: a Game Master set vanilla "Set combat mode" on this group. While EXPBG steers it,
	//! that mode becomes the group's own (what Exempt (vanilla) keeps); false otherwise.
	bool EGS_KeepGameMasterMode()
	{
		SCR_AIGroupUtilityComponent utility = GetGroupUtilityComponent();
		if (!utility || !m_bEGS_RoeTouched)
			return false;

		m_eEGS_OriginalMode = utility.GetCombatModeExternal();
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Server: a member selected a new hostile target (only called while Warning Shots First).
	void EGS_OnHostileSelected(SCR_AICombatComponent spotter, IEntity target)
	{
		if (m_iEGS_AppliedRoe != EGS_Settings.ROE_WARNING_SHOTS || m_iEGS_WarnState != EGS_WARN_ARMED)
			return;

		// Already fired upon: the squad is answering fire, no warning first.
		if (m_bEGS_Provoked || !EGS_Manager.IsPlayerTarget(target))
			return;

		m_iEGS_Token++;
		m_iEGS_WarnState = EGS_WARN_PENDING;
		m_bEGS_WarnEnded = false;
		int token = m_iEGS_Token;

		SCR_AICombatComponent shooter = spotter;
		if (!shooter || !shooter.EGS_StartWarningShots(target, this, token))
			shooter = EGS_FindWarningShooter(spotter, target, token);

		float window = 0;
		if (shooter)
		{
			m_EGS_WarningShooter = shooter;
			window = EGS_Manager.WARNING_WINDOW_S;
		}

		EGS_Log(string.Format("warning shots group=%1 shooter=%2 target=%3 spotter=%4", this, shooter, target, spotter));
		EGS_Manager.ScheduleGroup(this, EGS_Manager.TIMER_WARNING_END, window, token);
	}

	//------------------------------------------------------------------------------------------------
	//! The spotter cannot fire the warning burst (in a vehicle, no utility): another member who
	//! has the same target and can fire it (bounded); null when nobody can.
	protected SCR_AICombatComponent EGS_FindWarningShooter(SCR_AICombatComponent spotter, IEntity target, int token)
	{
		array<AIAgent> agents = {};
		GetAgents(agents);
		int count = Math.MinInt(agents.Count(), EGS_MAX_SHOOTER_TRIES);
		for (int i = 0; i < count; i++)
		{
			AIAgent agent = agents[i];
			if (!agent)
				continue;

			IEntity member = agent.GetControlledEntity();
			if (!member)
				continue;

			SCR_AICombatComponent combat = SCR_AICombatComponent.Cast(member.FindComponent(SCR_AICombatComponent));
			if (!combat || combat == spotter || combat.EGS_GetUnitRoe() != EGS_UnitRoe.FOLLOW)
				continue;

			BaseTarget seen = combat.GetCurrentTarget();
			if (!seen || seen.GetTargetEntity() != target)
				continue;

			if (combat.EGS_StartWarningShots(target, this, token))
				return combat;
		}

		return null;
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
		if (kind == EGS_Manager.TIMER_CALM)
		{
			EGS_OnCalmTimer(token);
			return;
		}

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
			EGS_Log(string.Format("warning shots group=%1: lethal now", this));
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
			EGS_Log(string.Format("warning shots group=%1: re-armed, the next player spotted gets a warning", this));
		}
	}

	//------------------------------------------------------------------------------------------------
	//! The contact is over once REARM_DELAY_S passed since the last provocation and no member
	//! has a target; otherwise checked again every REARM_RETRY_S (one timer per provoked group).
	protected void EGS_OnCalmTimer(int token)
	{
		if (token != m_iEGS_ProvokeToken || !m_bEGS_Provoked)
			return;

		if (EGS_Manager.Now() - m_fEGS_ProvokedAt < EGS_Manager.REARM_DELAY_S || EGS_HasTarget())
		{
			EGS_Manager.ScheduleGroup(this, EGS_Manager.TIMER_CALM, EGS_Manager.REARM_RETRY_S, token);
			return;
		}

		m_bEGS_Provoked = false;
		EGS_Log(string.Format("group=%1 contact over: holds fire again until fired upon", this));
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

	//------------------------------------------------------------------------------------------------
	//! Soldier ROE Return Fire Only (EGS_UnitRoe.c), and the actual mode of a group with the
	//! strict rule: hold fire until this group is fired upon (EGS_Provocation.c).
	EAIGroupCombatMode EGS_ReturnFireMode()
	{
		if (m_bEGS_Provoked)
			return EAIGroupCombatMode.FIRE_AT_WILL;

		return EAIGroupCombatMode.HOLD_FIRE;
	}

	//------------------------------------------------------------------------------------------------
	//! Soldier ROE "Exempt (vanilla)": the mode this group would have without EXPBG, with the
	//! vanilla return-fire rule when that mode is RETURN_FIRE.
	EAIGroupCombatMode EGS_VanillaMode()
	{
		SCR_AIGroupUtilityComponent utility = GetGroupUtilityComponent();
		if (!utility)
			return EAIGroupCombatMode.FIRE_AT_WILL;

		EAIGroupCombatMode mode = utility.GetCombatModeExternal();
		if (m_bEGS_RoeTouched)
			mode = m_eEGS_OriginalMode;

		if (mode != EAIGroupCombatMode.RETURN_FIRE)
			return mode;

		if (utility.EGS_IsEndangered())
			return EAIGroupCombatMode.FIRE_AT_WILL;

		return EAIGroupCombatMode.HOLD_FIRE;
	}
}

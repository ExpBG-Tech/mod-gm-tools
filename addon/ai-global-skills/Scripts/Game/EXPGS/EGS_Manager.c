// One-shot timer of a warning-shot sequence: a group's, or a soldier's own (m_Combat).
class EGS_GroupTimer
{
	SCR_AIGroup m_Group;
	SCR_AICombatComponent m_Combat;
	int m_iKind;
	int m_iToken;
	float m_fDue;
}

// Server scheduler of EXPBG AI Global Skills. Work is event driven (group member added,
// leader changed, loadout changed, settings changed) and drained by one shared call-queue
// tick with fixed per-tick budgets; the tick stops itself when nothing is pending. A
// settings change re-applies every existing AI once through the same budgeted queue.
class EGS_Manager
{
	static const int TICK_MS = 100;
	static const int AGENTS_PER_TICK = 24;
	static const int QUEUED_PER_TICK = 24;
	static const int TIMERS_PER_TICK = 32;
	static const int MAX_PENDING_GROUPS = 512;
	static const int MAX_OVERRIDE_UNITS = 2048;

	static const int TIMER_WARNING_END = 1;
	static const int TIMER_LETHAL = 2;
	static const int TIMER_REARM = 3;
	// End of a "fired upon" contact (SCR_AIGroup.EGS_Provoke, EGS_Provocation.c).
	static const int TIMER_CALM = 4;

	static const int WARNING_ROUNDS = 3;
	static const float WARNING_WINDOW_S = 4.0;
	static const float WARNING_FIRE_RATE = 2.0;
	static const float WARNING_PAUSE_S = 5.0;
	// Aim sphere of a warning burst, and the clearance kept between the target and the
	// furthest line end the vanilla suppress tree can draw from it.
	static const float WARNING_RADIUS = 1.0;
	static const float WARNING_CLEARANCE = 2.0;
	static const float REARM_DELAY_S = 60.0;
	static const float REARM_RETRY_S = 30.0;
	// Warning shots at a vehicle: seats searched for a player, extra clearance for its size.
	static const int MAX_TARGET_SEATS = 32;
	static const float MAX_VEHICLE_CLEARANCE = 6.0;
	static const float PENDING_BIND_S = 120.0;
	static const float PENDING_RETRY_S = 2.0;

	protected static BaseWorld s_World;
	protected static bool s_bTicking;
	protected static bool s_bFullRefresh;
	protected static bool s_bPublish;
	protected static bool s_bWasActive;
	protected static int s_iEpoch;
	protected static float s_fPendingUntil;
	protected static float s_fNextPendingTry;
	// Game Master saves (OpenGameMasterSave): the current save's number, and whether it is
	// still being written.
	protected static int s_iGameMasterSave;
	protected static bool s_bGameMasterSaveOpen;
	protected static ref array<AIAgent> s_aSweep;
	protected static ref array<IEntity> s_aUnits;
	protected static ref array<SCR_AIGroup> s_aGroups;
	protected static ref array<ref EGS_GroupTimer> s_aTimers;
	protected static ref array<SCR_AIGroup> s_aOverrideGroups;
	protected static ref array<UUID> s_aPendingIds;
	protected static ref array<int> s_aPendingRoe;
	protected static ref array<SCR_ChimeraCharacter> s_aOverrideUnits;
	protected static ref array<UUID> s_aPendingUnitIds;
	protected static ref array<int> s_aPendingUnitRoe;

	//------------------------------------------------------------------------------------------------
	//! Reset all scheduler state when a new world starts.
	protected static bool Ensure()
	{
		EXPBG_LazyStatics_EGS_Manager();
		if (!GetGame())
			return false;

		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return false;

		if (world == s_World)
			return true;

		s_World = world;
		GetGame().GetCallqueue().Remove(Tick);
		GetGame().GetCallqueue().Remove(CloseGameMasterSave);
		s_bGameMasterSaveOpen = false;
		s_iGameMasterSave++;
		s_bTicking = false;
		s_bFullRefresh = false;
		s_bPublish = false;
		s_bWasActive = false;
		s_fPendingUntil = 0;
		s_aSweep.Clear();
		s_aUnits.Clear();
		s_aGroups.Clear();
		s_aTimers.Clear();
		s_aOverrideGroups.Clear();
		s_aPendingIds.Clear();
		s_aPendingRoe.Clear();
		s_aOverrideUnits.Clear();
		s_aPendingUnitIds.Clear();
		s_aPendingUnitRoe.Clear();
		return true;
	}

	//------------------------------------------------------------------------------------------------
	static bool IsActive()
	{
		return EGS_Module.HasAny();
	}

	//------------------------------------------------------------------------------------------------
	//! World time in seconds (EXPBG AI Global Skills timers).
	static float Now()
	{
		return GetGame().GetWorld().GetWorldTime() * 0.001;
	}

	//------------------------------------------------------------------------------------------------
	static bool IsPlayerControlled(IEntity entity)
	{
		if (!entity || !GetGame() || !GetGame().GetPlayerManager())
			return false;

		return GetGame().GetPlayerManager().GetPlayerIdFromControlledEntity(entity) > 0;
	}

	//------------------------------------------------------------------------------------------------
	//! Warning Shots First target: a player, or a vehicle with a player inside (bounded seat loop).
	static bool IsPlayerTarget(IEntity target)
	{
		if (!target)
			return false;

		if (IsPlayerControlled(target))
			return true;

		if (!Vehicle.Cast(target))
			return false;

		BaseCompartmentManagerComponent compartments = BaseCompartmentManagerComponent.Cast(target.FindComponent(BaseCompartmentManagerComponent));
		if (!compartments)
			return false;

		array<BaseCompartmentSlot> seats = {};
		compartments.GetCompartments(seats);
		int count = Math.MinInt(seats.Count(), MAX_TARGET_SEATS);
		for (int i = 0; i < count; i++)
		{
			BaseCompartmentSlot seat = seats[i];
			if (seat && IsPlayerControlled(seat.GetOccupant()))
				return true;
		}

		return false;
	}

	//------------------------------------------------------------------------------------------------
	//! Extra distance a warning burst keeps from a vehicle target (half its footprint diagonal).
	static float TargetClearance(IEntity target)
	{
		if (!Vehicle.Cast(target))
			return 0;

		vector mins;
		vector maxs;
		target.GetBounds(mins, maxs);
		float halfX = (maxs[0] - mins[0]) * 0.5;
		float halfZ = (maxs[2] - mins[2]) * 0.5;
		return Math.Clamp(Math.Sqrt(halfX * halfX + halfZ * halfZ), 0, MAX_VEHICLE_CLEARANCE);
	}

	//------------------------------------------------------------------------------------------------
	protected static void EnsureTick()
	{
		if (s_bTicking || !Ensure())
			return;

		s_bTicking = true;
		GetGame().GetCallqueue().CallLater(Tick, TICK_MS, true);
	}

	//------------------------------------------------------------------------------------------------
	static void RequestPublish()
	{
		if (!Replication.IsServer() || !Ensure())
			return;

		s_bPublish = true;
		EnsureTick();
	}

	//------------------------------------------------------------------------------------------------
	//! Re-apply every existing AI unit and group once (budgeted).
	static void RequestFullRefresh()
	{
		if (!Replication.IsServer() || !Ensure())
			return;

		s_iEpoch++;
		s_bFullRefresh = true;
		EnsureTick();
	}

	//------------------------------------------------------------------------------------------------
	//! A module was placed or deleted: activation changes re-apply (or restore) everything.
	static void OnModulesChanged()
	{
		if (!Replication.IsServer() || !Ensure())
			return;

		bool active = IsActive();
		if (active == s_bWasActive)
			return;

		s_bWasActive = active;
		PrintFormat("[EXPBG AI SKILLS] module active=%1", active);
		RequestFullRefresh();
	}

	//------------------------------------------------------------------------------------------------
	static void QueueUnit(IEntity entity)
	{
		EXPBG_LazyStatics_EGS_Manager();
		if (!entity || !Replication.IsServer() || !Ensure())
			return;

		if (!s_aUnits.Contains(entity))
			s_aUnits.Insert(entity);

		EnsureTick();
	}

	//------------------------------------------------------------------------------------------------
	static void QueueGroup(SCR_AIGroup group)
	{
		EXPBG_LazyStatics_EGS_Manager();
		if (!group || !Replication.IsServer() || !Ensure())
			return;

		if (!s_aGroups.Contains(group))
			s_aGroups.Insert(group);

		EnsureTick();
	}

	//------------------------------------------------------------------------------------------------
	//! True while any squad or soldier has its own skill or rules of engagement: these apply
	//! without a module too, so membership changes must still be followed.
	static bool HasOverrides()
	{
		EXPBG_LazyStatics_EGS_Manager();
		return EGS_SkillOverrides.HasAny() || !s_aOverrideGroups.IsEmpty() || !s_aOverrideUnits.IsEmpty();
	}

	//------------------------------------------------------------------------------------------------
	//! Group membership changed: apply the new member and re-check the leader.
	static void OnMemberChanged(SCR_AIGroup group, AIAgent member)
	{
		if (!IsActive() && !HasOverrides())
			return;

		if (member)
			QueueUnit(member.GetControlledEntity());

		AIAgent leader = group.GetLeaderAgent();
		if (leader)
			QueueUnit(leader.GetControlledEntity());

		QueueGroup(group);
	}

	//------------------------------------------------------------------------------------------------
	//! Server: per-group override (EXPBG ROE group attribute, mission save or session load).
	//! Logged once per group and change; repeating the current value logs nothing. A Game
	//! Master's save (gameMaster) also puts the EXPBG combat mode back when something else
	//! changed it while the effective ROE stayed the same.
	static void SetGroupRoe(SCR_AIGroup group, int value, bool gameMaster = false)
	{
		EXPBG_LazyStatics_EGS_Manager();
		if (!group || !Replication.IsServer() || !Ensure() || !group.EGS_IsManaged())
			return;

		if (gameMaster)
			group.EGS_StampGameMasterRoe();

		int previous = group.EGS_GetRoeOverride();
		group.EGS_SetRoeOverride(value);
		int current = group.EGS_GetRoeOverride();
		if (current != EGS_Settings.GROUP_ROE_DEFAULT)
		{
			if (!s_aOverrideGroups.Contains(group))
				s_aOverrideGroups.Insert(group);
		}
		else
		{
			s_aOverrideGroups.RemoveItem(group);
		}

		if (current != previous)
			if (EBG_CacheDebug.Verbose()) PrintFormat("[EXPBG AI SKILLS] group=%1 roeOverride=%2", group, current);

		ApplyGroup(group, true, gameMaster);
	}

	//------------------------------------------------------------------------------------------------
	//! Server: a Game Master saved vanilla "Set combat mode" on this group (EGS_Attributes.c).
	//! While EXPBG steers the group, the new mode becomes its own and its EXPBG ROE switches to
	//! Exempt (vanilla), so both tabs agree. When the same save also set the EXPBG ROE, that
	//! choice stays, applied on top of the new mode.
	static void OnGameMasterCombatMode(SCR_AIGroup group)
	{
		if (!group || !Replication.IsServer() || !Ensure() || !group.EGS_IsManaged())
			return;

		// Without a module only a squad with its own EXPBG rules of engagement is steered.
		if (!IsActive() && group.EGS_GetRoeOverride() == EGS_Settings.GROUP_ROE_DEFAULT)
			return;

		if (group.EGS_GameMasterRoeThisSave())
		{
			group.EGS_AdoptVanillaMode();
			return;
		}

		if (!group.EGS_KeepGameMasterMode())
			return;

		group.EGS_Log(string.Format("group=%1 vanilla combat mode set by the Game Master: EXPBG rules of engagement now Exempt (vanilla)", group));
		SetGroupRoe(group, EGS_Settings.GROUP_ROE_EXEMPT);
	}

	//------------------------------------------------------------------------------------------------
	//! Server: a Game Master save is writing an EXPBG ROE now; returns the save's number. All
	//! attributes of one save are written in one call, so the save ends on the next call-queue
	//! tick (one call per save; the game call queue also runs while a single-player Game Master
	//! pause stops world time, so a later save never counts as the same one).
	static int OpenGameMasterSave()
	{
		if (!s_bGameMasterSaveOpen && GetGame())
		{
			s_bGameMasterSaveOpen = true;
			GetGame().GetCallqueue().CallLater(CloseGameMasterSave, 0);
		}

		return s_iGameMasterSave;
	}

	//------------------------------------------------------------------------------------------------
	//! True while the Game Master save with this number is still being written.
	static bool IsGameMasterSaveOpen(int saveNumber)
	{
		return s_bGameMasterSaveOpen && saveNumber == s_iGameMasterSave;
	}

	//------------------------------------------------------------------------------------------------
	protected static void CloseGameMasterSave()
	{
		s_bGameMasterSaveOpen = false;
		s_iGameMasterSave++;
	}

	//------------------------------------------------------------------------------------------------
	//! Server: per-soldier override (EXPBG AI Skill & ROE tab, cache wake, mission save
	//! or session load); applied to him at once. Logged once per soldier and change.
	static void SetUnitRoe(SCR_ChimeraCharacter soldier, int value)
	{
		EXPBG_LazyStatics_EGS_Manager();
		if (!soldier || !Replication.IsServer() || !Ensure())
			return;

		int previous = soldier.EGS_GetRoeOverride();
		soldier.EGS_SetRoeOverride(value);
		int current = soldier.EGS_GetRoeOverride();
		int index = s_aOverrideUnits.Find(soldier);
		if (current == EGS_Settings.GROUP_ROE_DEFAULT)
		{
			if (index >= 0)
				s_aOverrideUnits.Remove(index);
		}
		else if (index < 0)
		{
			for (int i = s_aOverrideUnits.Count() - 1; i >= 0; i--)
			{
				if (!s_aOverrideUnits[i] || s_aOverrideUnits[i].EGS_GetRoeOverride() == EGS_Settings.GROUP_ROE_DEFAULT)
					s_aOverrideUnits.Remove(i);
			}

			if (s_aOverrideUnits.Count() < MAX_OVERRIDE_UNITS)
				s_aOverrideUnits.Insert(soldier);
			else
				Print("[EXPBG AI SKILLS] soldier ROE registry full: this override works but is not in native saves", LogLevel.WARNING);
		}

		if (current != previous)
			if (EBG_CacheDebug.Verbose()) PrintFormat("[EXPBG AI SKILLS] unit=%1 roeOverride=%2", soldier, current);

		ApplyUnit(soldier);
	}

	//------------------------------------------------------------------------------------------------
	//! Server: re-apply one soldier at once (his own skill changed, EGS_SkillOverrides).
	static void ApplyUnitNow(IEntity entity)
	{
		if (!entity || !Replication.IsServer() || !Ensure())
			return;

		ApplyUnit(entity);
	}

	//------------------------------------------------------------------------------------------------
	//! Server: re-apply every member of a squad through the budgeted queue (its skill changed).
	static void QueueMembers(SCR_AIGroup group)
	{
		if (!group || !Replication.IsServer() || !Ensure())
			return;

		array<AIAgent> agents = {};
		group.GetAgents(agents);
		foreach (AIAgent agent : agents)
		{
			if (agent)
				QueueUnit(agent.GetControlledEntity());
		}
	}

	//------------------------------------------------------------------------------------------------
	static SCR_AIGroup ResolveGroup(SCR_EditableEntityComponent editable)
	{
		if (!editable)
			return null;

		SCR_AIGroup group = SCR_AIGroup.Cast(editable.GetOwner());
		if (!group)
		{
			SCR_EditableEntityComponent groupEditable = editable.GetAIGroup();
			if (groupEditable)
				group = SCR_AIGroup.Cast(groupEditable.GetOwner());
		}

		if (!group || !group.EGS_IsManaged())
			return null;

		return group;
	}

	//------------------------------------------------------------------------------------------------
	static void ScheduleGroup(SCR_AIGroup group, int kind, float delaySeconds, int token)
	{
		EXPBG_LazyStatics_EGS_Manager();
		if (!group || !Ensure())
			return;

		EGS_GroupTimer timer = new EGS_GroupTimer();
		timer.m_Group = group;
		timer.m_iKind = kind;
		timer.m_iToken = token;
		timer.m_fDue = Now() + delaySeconds;
		s_aTimers.Insert(timer);
		EnsureTick();
	}

	//------------------------------------------------------------------------------------------------
	//! One-shot timer of a soldier's own warning sequence (EGS_UnitRoe.c).
	static void ScheduleUnit(SCR_AICombatComponent combat, int kind, float delaySeconds, int token)
	{
		EXPBG_LazyStatics_EGS_Manager();
		if (!combat || !Ensure())
			return;

		EGS_GroupTimer timer = new EGS_GroupTimer();
		timer.m_Combat = combat;
		timer.m_iKind = kind;
		timer.m_iToken = token;
		timer.m_fDue = Now() + delaySeconds;
		s_aTimers.Insert(timer);
		EnsureTick();
	}

	//------------------------------------------------------------------------------------------------
	//! Point for warning rounds: beside and slightly short of the target, on the ground,
	//! never closer than 3 m to it (further at long range and for vehicles, see WarningOffset).
	static vector WarningPoint(vector shooter, vector target, float clearance = 0)
	{
		vector point = WarningOffset(shooter, target, Math.RandomFloat01() < 0.5, clearance);
		if (GetGame() && GetGame().GetWorld())
		{
			float ground = GetGame().GetWorld().GetSurfaceY(point[0], point[2]);
			point[1] = Math.Max(ground, target[1] - 1.5) + 0.3;
		}

		return point;
	}

	//------------------------------------------------------------------------------------------------
	//! Pure geometry used by WarningPoint and the fixture; extraClearance widens it for a
	//! target larger than a soldier (a vehicle, TargetClearance).
	static vector WarningOffset(vector shooter, vector target, bool leftSide, float extraClearance = 0)
	{
		vector direction = target - shooter;
		direction[1] = 0;
		float distance = direction.Length();
		if (distance < 0.1)
			direction = Vector(0, 0, 1);
		else
			direction = direction * (1.0 / distance);

		vector side = Vector(direction[2], 0, -direction[0]);
		if (leftSide)
			side = side * -1;

		// Vanilla GetRandomPosition sweeps every suppression line at least 2 degrees sideways
		// (SCR_AISuppressionVolume.c), so a line end can lie up to the sphere radius plus
		// tan(2 deg) x distance from the aim point, and rounds aimed above the ground fly on
		// past it. Keep the target clear of that reach.
		float reach = WARNING_RADIUS + Math.Tan(2 * Math.DEG2RAD) * distance + WARNING_CLEARANCE + Math.Max(extraClearance, 0);
		float lateral = Math.Max(Math.Clamp(distance * 0.06, 3.0, 8.0), reach);
		float shortfall = Math.Clamp(distance * 0.05, 2.0, 6.0);
		return target + side * lateral - direction * shortfall;
	}

	//------------------------------------------------------------------------------------------------
	protected static void Tick()
	{
		EXPBG_LazyStatics_EGS_Manager();
		if (!Ensure() || !Replication.IsServer())
		{
			StopTick();
			return;
		}

		if (s_bPublish)
		{
			s_bPublish = false;
			EGS_Module.PublishAll();
		}

		if (s_bFullRefresh)
		{
			s_bFullRefresh = false;
			s_aSweep.Clear();
			AIWorld aiWorld = GetGame().GetAIWorld();
			if (aiWorld)
				aiWorld.GetAIAgents(s_aSweep);
		}

		int budget = AGENTS_PER_TICK;
		while (budget > 0 && !s_aSweep.IsEmpty())
		{
			int last = s_aSweep.Count() - 1;
			AIAgent agent = s_aSweep[last];
			s_aSweep.Remove(last);
			ProcessAgent(agent);
			budget--;
		}

		budget = QUEUED_PER_TICK;
		while (budget > 0 && !s_aGroups.IsEmpty())
		{
			int lastGroup = s_aGroups.Count() - 1;
			SCR_AIGroup group = s_aGroups[lastGroup];
			s_aGroups.Remove(lastGroup);
			ApplyGroup(group, false);
			budget--;
		}

		while (budget > 0 && !s_aUnits.IsEmpty())
		{
			int lastUnit = s_aUnits.Count() - 1;
			IEntity unit = s_aUnits[lastUnit];
			s_aUnits.Remove(lastUnit);
			ApplyUnit(unit);
			budget--;
		}

		ProcessTimers();
		ProcessPendingOverrides();

		if (s_aSweep.IsEmpty() && s_aGroups.IsEmpty() && s_aUnits.IsEmpty() && s_aTimers.IsEmpty() && s_aPendingIds.IsEmpty() && s_aPendingUnitIds.IsEmpty() && !s_bPublish && !s_bFullRefresh)
			StopTick();
	}

	//------------------------------------------------------------------------------------------------
	protected static void StopTick()
	{
		if (GetGame())
			GetGame().GetCallqueue().Remove(Tick);

		s_bTicking = false;
	}

	//------------------------------------------------------------------------------------------------
	protected static void ProcessAgent(AIAgent agent)
	{
		if (!agent)
			return;

		SCR_AIGroup group = SCR_AIGroup.Cast(agent);
		if (group)
		{
			ApplyGroup(group, false);
			return;
		}

		ApplyUnit(agent.GetControlledEntity());
		ApplyGroup(SCR_AIGroup.Cast(agent.GetParentGroup()), false);
	}

	//------------------------------------------------------------------------------------------------
	//! Resolve and apply one soldier's skill, aim, spotting and ammunition policy.
	//! The module's faction values and ammunition policy apply while a module exists; a
	//! squad's or soldier's own skill and a soldier's own rules of engagement apply with or
	//! without one (soldier > squad > module).
	protected static void ApplyUnit(IEntity entity)
	{
		ChimeraCharacter character = ChimeraCharacter.Cast(entity);
		if (!character)
			return;

		SCR_AICombatComponent combat = SCR_AICombatComponent.Cast(character.FindComponent(SCR_AICombatComponent));
		if (!combat)
			return;

		CharacterControllerComponent controller = character.GetCharacterController();
		bool dead = controller && controller.IsDead();
		if (dead || IsPlayerControlled(character))
		{
			ResetUnit(combat);
			return;
		}

		SCR_ChimeraCharacter scripted = SCR_ChimeraCharacter.Cast(character);
		AIAgent agent = combat.GetAiAgent();
		SCR_AIGroup squad;
		if (agent)
			squad = SCR_AIGroup.Cast(agent.GetParentGroup());

		// Soldier override > squad override > module default (EGS_UnitRoe.c); FOLLOW leaves
		// him to his squad's combat mode.
		int unitOverride = EGS_Settings.GROUP_ROE_DEFAULT;
		if (scripted)
			unitOverride = scripted.EGS_GetRoeOverride();

		int ownSkill = EGS_SkillOverrides.Resolve(scripted, squad);
		bool active = IsActive();
		if (!active && ownSkill == EGS_SkillOverrides.FOLLOW && unitOverride == EGS_Settings.GROUP_ROE_DEFAULT)
		{
			ResetUnit(combat);
			return;
		}

		array<int> values;
		if (active && scripted)
			values = EGS_Settings.GetFactionValues(scripted.GetFactionKey());

		// Role detection only when the faction has a role override.
		int weaponRole = EGS_Settings.ROLE_RIFLEMAN;
		bool leader;
		if (EGS_Settings.HasRoleOverrides(values))
		{
			weaponRole = EGS_Roles.ClassifyWeaponRole(character);
			leader = EGS_Roles.IsLeader(agent);
		}

		int skill = ownSkill;
		if (skill == EGS_SkillOverrides.FOLLOW)
			skill = EGS_Settings.ResolveIndex(values, EGS_Settings.SLOT_SKILL, weaponRole, leader);

		int aim = EGS_Settings.ResolveIndex(values, EGS_Settings.SLOT_AIM, weaponRole, leader);

		combat.EGS_SetProfile(EGS_Settings.SkillFromIndex(skill), EGS_Settings.PerceptionFromIndex(skill), EGS_Settings.AimErrorScaleFromIndex(aim));
		if (active)
			combat.EGS_SetAmmoPolicy(EGS_Settings.GetAmmoMode(), EGS_Settings.GetRefills());
		else
			combat.EGS_SetAmmoPolicy(EGS_Settings.AMMO_VANILLA, 0);

		combat.EGS_SetUnitRoe(EGS_UnitRoe.Effective(unitOverride));
		// His own Return Fire Only or Warning Shots First holds fire until his squad is fired
		// upon: the squad is watched for that from now on (EGS_Provocation.c).
		int own = combat.EGS_GetUnitRoe();
		if ((own == EGS_Settings.ROE_RETURN_FIRE || own == EGS_Settings.ROE_WARNING_SHOTS) && squad)
			squad.EGS_SetOwnRoeMember(true);
	}

	//------------------------------------------------------------------------------------------------
	//! Back to vanilla: no own rules of engagement, no EXPBG skill, aim or ammunition policy.
	protected static void ResetUnit(SCR_AICombatComponent combat)
	{
		combat.EGS_SetUnitRoe(EGS_UnitRoe.FOLLOW);
		if (combat.EGS_HasProfile())
			combat.EGS_ResetProfile();
	}

	//------------------------------------------------------------------------------------------------
	//! True when a member of this squad has Return Fire Only or Warning Shots First as his own
	//! rules of engagement (bounded by the squad's size; only while soldier overrides exist).
	protected static bool HasOwnRoeMember(SCR_AIGroup group)
	{
		EXPBG_LazyStatics_EGS_Manager();
		if (s_aOverrideUnits.IsEmpty())
			return false;

		array<AIAgent> agents = {};
		group.GetAgents(agents);
		foreach (AIAgent agent : agents)
		{
			if (!agent)
				continue;

			SCR_ChimeraCharacter member = SCR_ChimeraCharacter.Cast(agent.GetControlledEntity());
			if (!member)
				continue;

			int own = EGS_UnitRoe.Effective(member.EGS_GetRoeOverride());
			if (own == EGS_Settings.ROE_RETURN_FIRE || own == EGS_Settings.ROE_WARNING_SHOTS)
				return true;
		}

		return false;
	}

	//------------------------------------------------------------------------------------------------
	//! Apply the effective ROE once per settings epoch (forced after a per-group override;
	//! reassert after a Game Master's save, see SCR_AIGroup.EGS_ApplyRoe).
	protected static void ApplyGroup(SCR_AIGroup group, bool force, bool reassert = false)
	{
		if (!group)
			return;

		// No module: only squads with a soldier who keeps his own Return Fire Only or Warning
		// Shots First stay watched (ApplyUnit marks them again).
		if (!IsActive())
			group.EGS_SetOwnRoeMember(HasOwnRoeMember(group));

		// A player joined: hand the group back (no-op for untouched groups).
		if (!group.EGS_IsManaged())
		{
			group.EGS_SetEpoch(-1);
			group.EGS_ApplyRoe(EGS_Settings.ROE_VANILLA);
			return;
		}

		if (!force && group.EGS_GetEpoch() == s_iEpoch)
			return;

		group.EGS_SetEpoch(s_iEpoch);
		group.EGS_ApplyRoe(EGS_Settings.EffectiveRoe(IsActive(), group.EGS_GetRoeOverride(), EGS_Settings.GetRoe()), reassert);
	}

	//------------------------------------------------------------------------------------------------
	protected static void ProcessTimers()
	{
		EXPBG_LazyStatics_EGS_Manager();
		if (s_aTimers.IsEmpty())
			return;

		float now = Now();
		int budget = TIMERS_PER_TICK;
		for (int i = s_aTimers.Count() - 1; i >= 0 && budget > 0; i--)
		{
			EGS_GroupTimer timer = s_aTimers[i];
			if (!timer || (!timer.m_Group && !timer.m_Combat))
			{
				s_aTimers.Remove(i);
				continue;
			}

			if (now < timer.m_fDue)
				continue;

			s_aTimers.Remove(i);
			budget--;
			if (timer.m_Group)
				timer.m_Group.EGS_OnTimer(timer.m_iKind, timer.m_iToken);
			else
				timer.m_Combat.EGS_OnUnitTimer(timer.m_iKind, timer.m_iToken);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Group overrides from a mission save bind to their groups by persistence id.
	protected static void ProcessPendingOverrides()
	{
		EXPBG_LazyStatics_EGS_Manager();
		if (s_aPendingIds.IsEmpty() && s_aPendingUnitIds.IsEmpty())
			return;

		float now = Now();
		if (now < s_fNextPendingTry)
			return;

		s_fNextPendingTry = now + PENDING_RETRY_S;
		PersistenceSystem persistence = PersistenceSystem.GetInstance();
		for (int i = s_aPendingIds.Count() - 1; i >= 0; i--)
		{
			SCR_AIGroup group;
			if (persistence)
				group = SCR_AIGroup.Cast(persistence.FindById(s_aPendingIds[i]));

			if (!group)
				continue;

			int value = s_aPendingRoe[i];
			s_aPendingIds.Remove(i);
			s_aPendingRoe.Remove(i);
			SetGroupRoe(group, value);
		}

		for (int j = s_aPendingUnitIds.Count() - 1; j >= 0; j--)
		{
			SCR_ChimeraCharacter soldier;
			if (persistence)
				soldier = SCR_ChimeraCharacter.Cast(persistence.FindById(s_aPendingUnitIds[j]));

			if (!soldier)
				continue;

			int unitValue = s_aPendingUnitRoe[j];
			s_aPendingUnitIds.Remove(j);
			s_aPendingUnitRoe.Remove(j);
			SetUnitRoe(soldier, unitValue);
		}

		if (now >= s_fPendingUntil && (!s_aPendingIds.IsEmpty() || !s_aPendingUnitIds.IsEmpty()))
		{
			PrintFormat("[EXPBG AI SKILLS] %1 saved group and %2 saved soldier ROE overrides had no match", s_aPendingIds.Count(), s_aPendingUnitIds.Count());
			s_aPendingIds.Clear();
			s_aPendingRoe.Clear();
			s_aPendingUnitIds.Clear();
			s_aPendingUnitRoe.Clear();
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Mission save: per-group overrides keyed by persistence id.
	static void ExportGroupOverrides(notnull array<UUID> outIds, notnull array<int> outRoe)
	{
		EXPBG_LazyStatics_EGS_Manager();
		outIds.Clear();
		outRoe.Clear();
		if (!Ensure())
			return;

		PersistenceSystem persistence = PersistenceSystem.GetInstance();
		if (!persistence)
			return;

		for (int i = s_aOverrideGroups.Count() - 1; i >= 0; i--)
		{
			SCR_AIGroup group = s_aOverrideGroups[i];
			if (!group || group.EGS_GetRoeOverride() == EGS_Settings.GROUP_ROE_DEFAULT)
			{
				s_aOverrideGroups.Remove(i);
				continue;
			}

			UUID id = persistence.GetId(group);
			if (id.IsNull() || outIds.Count() >= MAX_PENDING_GROUPS)
				continue;

			outIds.Insert(id);
			outRoe.Insert(group.EGS_GetRoeOverride());
		}
	}

	//------------------------------------------------------------------------------------------------
	static void ImportGroupOverrides(notnull array<UUID> ids, notnull array<int> roe)
	{
		EXPBG_LazyStatics_EGS_Manager();
		if (!Ensure())
			return;

		s_aPendingIds.Clear();
		s_aPendingRoe.Clear();
		int count = Math.MinInt(Math.MinInt(ids.Count(), roe.Count()), MAX_PENDING_GROUPS);
		for (int i = 0; i < count; i++)
		{
			if (ids[i].IsNull() || roe[i] == EGS_Settings.GROUP_ROE_DEFAULT)
				continue;

			s_aPendingIds.Insert(ids[i]);
			s_aPendingRoe.Insert(Math.ClampInt(roe[i], EGS_Settings.GROUP_ROE_DEFAULT, EGS_Settings.GROUP_ROE_EXEMPT));
		}

		s_fPendingUntil = Now() + PENDING_BIND_S;
		s_fNextPendingTry = 0;
		EnsureTick();
	}

	//------------------------------------------------------------------------------------------------
	//! Mission save: per-soldier overrides keyed by persistence id (bounded).
	static void ExportUnitOverrides(notnull array<UUID> outIds, notnull array<int> outRoe)
	{
		EXPBG_LazyStatics_EGS_Manager();
		outIds.Clear();
		outRoe.Clear();
		if (!Ensure())
			return;

		PersistenceSystem persistence = PersistenceSystem.GetInstance();
		if (!persistence)
			return;

		for (int i = s_aOverrideUnits.Count() - 1; i >= 0; i--)
		{
			SCR_ChimeraCharacter soldier = s_aOverrideUnits[i];
			if (!soldier || soldier.EGS_GetRoeOverride() == EGS_Settings.GROUP_ROE_DEFAULT)
			{
				s_aOverrideUnits.Remove(i);
				continue;
			}

			UUID id = persistence.GetId(soldier);
			if (id.IsNull() || outIds.Count() >= MAX_OVERRIDE_UNITS)
				continue;

			outIds.Insert(id);
			outRoe.Insert(soldier.EGS_GetRoeOverride());
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Mission load: soldier overrides bind to their soldiers by persistence id (retried).
	static void ImportUnitOverrides(notnull array<UUID> ids, notnull array<int> roe)
	{
		EXPBG_LazyStatics_EGS_Manager();
		if (!Ensure())
			return;

		s_aPendingUnitIds.Clear();
		s_aPendingUnitRoe.Clear();
		int count = Math.MinInt(Math.MinInt(ids.Count(), roe.Count()), MAX_OVERRIDE_UNITS);
		for (int i = 0; i < count; i++)
		{
			if (ids[i].IsNull() || roe[i] == EGS_Settings.GROUP_ROE_DEFAULT)
				continue;

			s_aPendingUnitIds.Insert(ids[i]);
			s_aPendingUnitRoe.Insert(Math.ClampInt(roe[i], EGS_Settings.GROUP_ROE_DEFAULT, EGS_Settings.GROUP_ROE_EXEMPT));
		}

		s_fPendingUntil = Now() + PENDING_BIND_S;
		s_fNextPendingTry = 0;
		EnsureTick();
	}

	//------------------------------------------------------------------------------------------------
	//! Creates the collections on first use (not in the global static initializer, which has a
	//! per-function instruction limit that large modsets exceed on Windows).
	protected static void EXPBG_LazyStatics_EGS_Manager()
	{
		if (!s_aSweep)
			s_aSweep = new array<AIAgent>();
		if (!s_aUnits)
			s_aUnits = new array<IEntity>();
		if (!s_aGroups)
			s_aGroups = new array<SCR_AIGroup>();
		if (!s_aTimers)
			s_aTimers = new array<ref EGS_GroupTimer>();
		if (!s_aOverrideGroups)
			s_aOverrideGroups = new array<SCR_AIGroup>();
		if (!s_aPendingIds)
			s_aPendingIds = new array<UUID>();
		if (!s_aPendingRoe)
			s_aPendingRoe = new array<int>();
		if (!s_aOverrideUnits)
			s_aOverrideUnits = new array<SCR_ChimeraCharacter>();
		if (!s_aPendingUnitIds)
			s_aPendingUnitIds = new array<UUID>();
		if (!s_aPendingUnitRoe)
			s_aPendingUnitRoe = new array<int>();
	}
}

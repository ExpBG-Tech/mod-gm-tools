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
	protected static float Now()
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
	//! Group membership changed: apply the new member and re-check the leader.
	static void OnMemberChanged(SCR_AIGroup group, AIAgent member)
	{
		if (!IsActive())
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
	//! Logged once per group and change; repeating the current value logs nothing.
	static void SetGroupRoe(SCR_AIGroup group, int value)
	{
		EXPBG_LazyStatics_EGS_Manager();
		if (!group || !Replication.IsServer() || !Ensure() || !group.EGS_IsManaged())
			return;

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
			PrintFormat("[EXPBG AI SKILLS] group=%1 roeOverride=%2", group, current);

		ApplyGroup(group, true);
	}

	//------------------------------------------------------------------------------------------------
	//! Server: per-soldier override (EXPBG Rules of Engagement tab, cache wake, mission save
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
			PrintFormat("[EXPBG AI SKILLS] unit=%1 roeOverride=%2", soldier, current);

		ApplyUnit(soldier);
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
	//! never closer than 3 m to it (further at long range, see WarningOffset).
	static vector WarningPoint(vector shooter, vector target)
	{
		vector point = WarningOffset(shooter, target, Math.RandomFloat01() < 0.5);
		if (GetGame() && GetGame().GetWorld())
		{
			float ground = GetGame().GetWorld().GetSurfaceY(point[0], point[2]);
			point[1] = Math.Max(ground, target[1] - 1.5) + 0.3;
		}

		return point;
	}

	//------------------------------------------------------------------------------------------------
	//! Pure geometry used by WarningPoint and the fixture.
	static vector WarningOffset(vector shooter, vector target, bool leftSide)
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
		float reach = WARNING_RADIUS + Math.Tan(2 * Math.DEG2RAD) * distance + WARNING_CLEARANCE;
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
		if (!IsActive() || dead || IsPlayerControlled(character))
		{
			combat.EGS_SetUnitRoe(EGS_UnitRoe.FOLLOW);
			if (combat.EGS_HasProfile())
				combat.EGS_ResetProfile();

			return;
		}

		array<int> values;
		SCR_ChimeraCharacter scripted = SCR_ChimeraCharacter.Cast(character);
		if (scripted)
			values = EGS_Settings.GetFactionValues(scripted.GetFactionKey());

		// Role detection only when the faction has a role override.
		int weaponRole = EGS_Settings.ROLE_RIFLEMAN;
		bool leader;
		if (EGS_Settings.HasRoleOverrides(values))
		{
			weaponRole = EGS_Roles.ClassifyWeaponRole(character);
			leader = EGS_Roles.IsLeader(combat.GetAiAgent());
		}

		int skill = EGS_Settings.ResolveIndex(values, EGS_Settings.SLOT_SKILL, weaponRole, leader);
		int aim = EGS_Settings.ResolveIndex(values, EGS_Settings.SLOT_AIM, weaponRole, leader);

		combat.EGS_SetProfile(EGS_Settings.SkillFromIndex(skill), EGS_Settings.PerceptionFromIndex(skill), EGS_Settings.AimErrorScaleFromIndex(aim));
		combat.EGS_SetAmmoPolicy(EGS_Settings.GetAmmoMode(), EGS_Settings.GetRefills());
		// Soldier override > squad override > module default (EGS_UnitRoe.c); FOLLOW leaves
		// him to his squad's combat mode.
		int unitOverride = EGS_Settings.GROUP_ROE_DEFAULT;
		if (scripted)
			unitOverride = scripted.EGS_GetRoeOverride();

		combat.EGS_SetUnitRoe(EGS_UnitRoe.Effective(unitOverride));
	}

	//------------------------------------------------------------------------------------------------
	//! Apply the effective ROE once per settings epoch (forced after a per-group override).
	protected static void ApplyGroup(SCR_AIGroup group, bool force)
	{
		if (!group)
			return;

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
		group.EGS_ApplyRoe(EGS_Settings.EffectiveRoe(IsActive(), group.EGS_GetRoeOverride(), EGS_Settings.GetRoe()));
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

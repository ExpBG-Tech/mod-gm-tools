// EXPBG AI Global Skills: skill per squad and per soldier. "AI skill (squad)" and "AI skill
// (soldier)" in the EXPBG AI Skill & ROE tab of any AI squad or AI soldier (vanilla, RHS or any
// other mod) set Novice, Rookie, Regular, Veteran or Expert: the same vanilla skill tier
// (aiming spread) and spotting speed as the module's per-faction skill. "Use module setting"
// (squad) and "Use squad or module setting" (soldier) are the defaults, so nothing changes until
// a Game Master picks one. Resolution when the soldier is (re)applied (EGS_Manager.ApplyUnit):
//   soldier > squad (AI squads only) > the module's faction and role skill.
// A squad's or soldier's own skill applies with or without a module; without one the other
// soldiers stay vanilla. Aim accuracy and ammunition stay the module's per-faction settings.
// Values are plain server fields (the dialog reads them on the server). They follow the
// entity through Unit Caching and Garrison caching (EGS_OverrideCache.c), native saves
// (EGS_Persistence.c, version 3) and attribute-based saves such as CDF Game Master Save
// (EGS_SavedGroupSkillAttribute, EGS_SavedUnitSkillAttribute).
class EGS_SkillOverrides
{
	//! No own skill: follow the squad (soldier) or the module (squad).
	static const int FOLLOW = 0;
	static const int MAX_TRACKED = 2048;
	static const int BIND_RETRY_MS = 2000;
	static const float BIND_SECONDS = 120;

	protected static ref array<SCR_AIGroup> s_aGroups;
	protected static ref array<SCR_ChimeraCharacter> s_aUnits;
	protected static ref array<UUID> s_aPendingIds;
	protected static ref array<bool> s_aPendingUnits;
	protected static ref array<int> s_aPendingValues;
	protected static float s_fBindUntil;
	protected static bool s_bBindQueued;

	//------------------------------------------------------------------------------------------------
	//! FOLLOW or a skill index 1 (Novice) to EGS_Settings.SKILL_MAX (Expert).
	static int Normalize(int value)
	{
		return Math.ClampInt(value, FOLLOW, EGS_Settings.SKILL_MAX);
	}

	//------------------------------------------------------------------------------------------------
	//! Pure: the soldier's own skill, else his squad's, else FOLLOW (the module decides).
	static int Pick(int unitValue, int squadValue)
	{
		if (unitValue > FOLLOW)
			return Normalize(unitValue);

		if (squadValue > FOLLOW)
			return Normalize(squadValue);

		return FOLLOW;
	}

	//------------------------------------------------------------------------------------------------
	//! Effective own skill of a soldier in his squad (a squad value counts only for AI squads).
	static int Resolve(SCR_ChimeraCharacter soldier, SCR_AIGroup squad)
	{
		int unitValue = FOLLOW;
		if (soldier)
			unitValue = soldier.EGS_GetSkillOverride();

		int squadValue = FOLLOW;
		if (squad && squad.EGS_IsManaged())
			squadValue = squad.EGS_GetSkillOverride();

		return Pick(unitValue, squadValue);
	}

	//------------------------------------------------------------------------------------------------
	//! True while any squad or soldier has its own skill (cheap; may include deleted entities).
	static bool HasAny()
	{
		EXPBG_LazyStatics_EGS_SkillOverrides();
		return !s_aGroups.IsEmpty() || !s_aUnits.IsEmpty();
	}

	//------------------------------------------------------------------------------------------------
	static string Describe(int value)
	{
		switch (value)
		{
			case 1: return "Novice";
			case 2: return "Rookie";
			case 3: return "Regular";
			case 4: return "Veteran";
			case 5: return "Expert";
		}

		return "follow";
	}

	//------------------------------------------------------------------------------------------------
	//! Server: a squad's own skill; its members are re-applied through the budgeted queue.
	static void SetGroup(SCR_AIGroup squad, int value, string origin)
	{
		if (!squad || !Replication.IsServer())
			return;

		int previous = squad.EGS_GetSkillOverride();
		squad.EGS_SetSkillOverride(value);
		int current = squad.EGS_GetSkillOverride();
		Track(squad, null);
		if (current == previous)
			return;

		PrintFormat("[EXPBG AI SKILLS] group=%1 skillOverride=%2 (%3)", squad, Describe(current), origin);
		EGS_Manager.QueueMembers(squad);
	}

	//------------------------------------------------------------------------------------------------
	//! Server: a soldier's own skill, applied to him at once.
	static void SetUnit(SCR_ChimeraCharacter soldier, int value, string origin)
	{
		if (!soldier || !Replication.IsServer())
			return;

		int previous = soldier.EGS_GetSkillOverride();
		soldier.EGS_SetSkillOverride(value);
		int current = soldier.EGS_GetSkillOverride();
		Track(null, soldier);
		if (current == previous)
			return;

		PrintFormat("[EXPBG AI SKILLS] unit=%1 skillOverride=%2 (%3)", soldier, Describe(current), origin);
		EGS_Manager.ApplyUnitNow(soldier);
	}

	//------------------------------------------------------------------------------------------------
	//! Keeps the save registry in step: entities with an own skill in, others out. Bounded.
	protected static void Track(SCR_AIGroup squad, SCR_ChimeraCharacter soldier)
	{
		EXPBG_LazyStatics_EGS_SkillOverrides();
		if (squad)
		{
			int groupIndex = s_aGroups.Find(squad);
			if (squad.EGS_GetSkillOverride() == FOLLOW)
			{
				if (groupIndex >= 0)
					s_aGroups.Remove(groupIndex);

				return;
			}

			if (groupIndex >= 0)
				return;

			Prune();
			if (s_aGroups.Count() >= MAX_TRACKED)
			{
				Print("[EXPBG AI SKILLS] squad skill registry full: this squad's skill works but is not in native saves", LogLevel.WARNING);
				return;
			}

			s_aGroups.Insert(squad);
			return;
		}

		if (!soldier)
			return;

		int unitIndex = s_aUnits.Find(soldier);
		if (soldier.EGS_GetSkillOverride() == FOLLOW)
		{
			if (unitIndex >= 0)
				s_aUnits.Remove(unitIndex);

			return;
		}

		if (unitIndex >= 0)
			return;

		Prune();
		if (s_aUnits.Count() >= MAX_TRACKED)
		{
			Print("[EXPBG AI SKILLS] soldier skill registry full: this soldier's skill works but is not in native saves", LogLevel.WARNING);
			return;
		}

		s_aUnits.Insert(soldier);
	}

	//------------------------------------------------------------------------------------------------
	protected static void Prune()
	{
		EXPBG_LazyStatics_EGS_SkillOverrides();
		for (int i = s_aGroups.Count() - 1; i >= 0; i--)
		{
			if (!s_aGroups[i] || s_aGroups[i].EGS_GetSkillOverride() == FOLLOW)
				s_aGroups.Remove(i);
		}

		for (int j = s_aUnits.Count() - 1; j >= 0; j--)
		{
			if (!s_aUnits[j] || s_aUnits[j].EGS_GetSkillOverride() == FOLLOW)
				s_aUnits.Remove(j);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Native mission save: every tracked squad and soldier with a persistence id.
	static void Export(notnull array<UUID> groupIds, notnull array<int> groupValues, notnull array<UUID> unitIds, notnull array<int> unitValues)
	{
		EXPBG_LazyStatics_EGS_SkillOverrides();
		groupIds.Clear();
		groupValues.Clear();
		unitIds.Clear();
		unitValues.Clear();
		PersistenceSystem persistence = PersistenceSystem.GetInstance();
		if (!persistence)
			return;

		Prune();
		foreach (SCR_AIGroup squad : s_aGroups)
		{
			UUID groupId = persistence.GetId(squad);
			if (groupId.IsNull())
				continue;

			groupIds.Insert(groupId);
			groupValues.Insert(squad.EGS_GetSkillOverride());
		}

		foreach (SCR_ChimeraCharacter soldier : s_aUnits)
		{
			UUID unitId = persistence.GetId(soldier);
			if (unitId.IsNull())
				continue;

			unitIds.Insert(unitId);
			unitValues.Insert(soldier.EGS_GetSkillOverride());
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Native mission load: loaded entities may appear after the state, so bind by id and retry
	//! for BIND_SECONDS.
	static void Import(notnull array<UUID> groupIds, notnull array<int> groupValues, notnull array<UUID> unitIds, notnull array<int> unitValues)
	{
		EXPBG_LazyStatics_EGS_SkillOverrides();
		s_aPendingIds.Clear();
		s_aPendingUnits.Clear();
		s_aPendingValues.Clear();
		int groups = Math.MinInt(Math.MinInt(groupIds.Count(), groupValues.Count()), MAX_TRACKED);
		for (int i = 0; i < groups; i++)
			AddPending(groupIds[i], false, groupValues[i]);

		int units = Math.MinInt(Math.MinInt(unitIds.Count(), unitValues.Count()), MAX_TRACKED);
		for (int j = 0; j < units; j++)
			AddPending(unitIds[j], true, unitValues[j]);

		if (s_aPendingIds.IsEmpty() || !GetGame() || !GetGame().GetWorld())
			return;

		s_fBindUntil = EGS_Manager.Now() + BIND_SECONDS;
		BindPending();
	}

	//------------------------------------------------------------------------------------------------
	protected static void AddPending(UUID id, bool unit, int value)
	{
		if (id.IsNull() || Normalize(value) == FOLLOW)
			return;

		s_aPendingIds.Insert(id);
		s_aPendingUnits.Insert(unit);
		s_aPendingValues.Insert(Normalize(value));
	}

	//------------------------------------------------------------------------------------------------
	//! Bounded by the pending list; one retry timer at a time; ordered removal keeps the
	//! parallel lists aligned.
	static void BindPending()
	{
		EXPBG_LazyStatics_EGS_SkillOverrides();
		s_bBindQueued = false;
		if (s_aPendingIds.IsEmpty() || !GetGame() || !GetGame().GetWorld())
			return;

		PersistenceSystem persistence = PersistenceSystem.GetInstance();
		for (int i = s_aPendingIds.Count() - 1; i >= 0; i--)
		{
			Managed found;
			if (persistence)
				found = persistence.FindById(s_aPendingIds[i]);

			if (!found)
				continue;

			if (s_aPendingUnits[i])
				SetUnit(SCR_ChimeraCharacter.Cast(found), s_aPendingValues[i], "save");
			else
				SetGroup(SCR_AIGroup.Cast(found), s_aPendingValues[i], "save");

			s_aPendingIds.RemoveOrdered(i);
			s_aPendingUnits.RemoveOrdered(i);
			s_aPendingValues.RemoveOrdered(i);
		}

		if (s_aPendingIds.IsEmpty())
			return;

		// Past the deadline, or a deadline from an earlier world (world time restarted).
		float now = EGS_Manager.Now();
		if (now >= s_fBindUntil || s_fBindUntil - now > BIND_SECONDS + 1)
		{
			PrintFormat("[EXPBG AI SKILLS] %1 saved squad/soldier skills had no matching entity", s_aPendingIds.Count());
			s_aPendingIds.Clear();
			s_aPendingUnits.Clear();
			s_aPendingValues.Clear();
			return;
		}

		if (s_bBindQueued)
			return;

		s_bBindQueued = true;
		GetGame().GetCallqueue().CallLater(EGS_SkillOverrides.BindPending, BIND_RETRY_MS, false);
	}

	//------------------------------------------------------------------------------------------------
	//! Creates the collections on first use (not in the global static initializer, which has a
	//! per-function instruction limit that large modsets exceed on Windows).
	protected static void EXPBG_LazyStatics_EGS_SkillOverrides()
	{
		if (!s_aGroups)
			s_aGroups = new array<SCR_AIGroup>();
		if (!s_aUnits)
			s_aUnits = new array<SCR_ChimeraCharacter>();
		if (!s_aPendingIds)
			s_aPendingIds = new array<UUID>();
		if (!s_aPendingUnits)
			s_aPendingUnits = new array<bool>();
		if (!s_aPendingValues)
			s_aPendingValues = new array<int>();
	}
}

//------------------------------------------------------------------------------------------------
//! Squad skill: 0 module setting, 1 Novice .. 5 Expert. Server field.
modded class SCR_AIGroup
{
	protected int m_iEGS_SkillOverride;

	//------------------------------------------------------------------------------------------------
	int EGS_GetSkillOverride()
	{
		return m_iEGS_SkillOverride;
	}

	//------------------------------------------------------------------------------------------------
	//! Server; EGS_SkillOverrides.SetGroup is the writer.
	void EGS_SetSkillOverride(int value)
	{
		if (!Replication.IsServer())
			return;

		m_iEGS_SkillOverride = EGS_SkillOverrides.Normalize(value);
	}
}

//------------------------------------------------------------------------------------------------
//! Soldier skill: 0 squad or module setting, 1 Novice .. 5 Expert. Server field.
modded class SCR_ChimeraCharacter
{
	protected int m_iEGS_SkillOverride;

	//------------------------------------------------------------------------------------------------
	int EGS_GetSkillOverride()
	{
		return m_iEGS_SkillOverride;
	}

	//------------------------------------------------------------------------------------------------
	//! Server; EGS_SkillOverrides.SetUnit is the writer.
	void EGS_SetSkillOverride(int value)
	{
		if (!Replication.IsServer())
			return;

		m_iEGS_SkillOverride = EGS_SkillOverrides.Normalize(value);
	}
}

//------------------------------------------------------------------------------------------------
//! Dialog attribute base: spinbox entries from the attribute list (entry index = stored value).
//! Not serializable: attribute-based savers use the saved attributes below.
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EGS_SkillOverrideAttribute : SCR_BaseEditorAttribute
{
	[Attribute(desc: "Spinbox entries, in value order (entry index = stored value)")]
	protected ref array<ref SCR_EditorAttributeFloatStringValueHolder> m_aValues;

	//------------------------------------------------------------------------------------------------
	override bool IsSerializable()
	{
		return false;
	}

	//------------------------------------------------------------------------------------------------
	override int GetEntries(notnull array<ref SCR_BaseEditorAttributeEntry> outEntries)
	{
		outEntries.Insert(new SCR_BaseEditorAttributeFloatStringValues(m_aValues));
		return outEntries.Count();
	}
}

//------------------------------------------------------------------------------------------------
//! "AI skill (squad)": an AI squad, edited itself or through one of its soldiers.
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EGS_GroupSkillAttribute : EGS_SkillOverrideAttribute
{
	//------------------------------------------------------------------------------------------------
	override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
	{
		if (!manager)
			return null;

		SCR_AIGroup squad = EGS_Attributes.DialogGroup(item);
		if (!squad)
			return null;

		return SCR_BaseEditorAttributeVar.CreateInt(squad.EGS_GetSkillOverride());
	}

	//------------------------------------------------------------------------------------------------
	override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var, SCR_AttributesManagerEditorComponent manager, int playerID)
	{
		if (!var || !Replication.IsServer() || !EGS_Attributes.IsEditingGameMaster(manager, playerID))
			return;

		EGS_SkillOverrides.SetGroup(EGS_Attributes.DialogGroup(item), var.GetInt(), "Game Master");
	}
}

//------------------------------------------------------------------------------------------------
//! "AI skill (soldier)": one AI soldier nobody plays or possesses.
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EGS_UnitSkillAttribute : EGS_SkillOverrideAttribute
{
	//------------------------------------------------------------------------------------------------
	override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
	{
		if (!manager)
			return null;

		SCR_ChimeraCharacter soldier = EGS_UnitRoe.ResolveSoldier(SCR_EditableEntityComponent.Cast(item));
		if (!soldier)
			return null;

		return SCR_BaseEditorAttributeVar.CreateInt(soldier.EGS_GetSkillOverride());
	}

	//------------------------------------------------------------------------------------------------
	override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var, SCR_AttributesManagerEditorComponent manager, int playerID)
	{
		if (!var || !Replication.IsServer() || !EGS_Attributes.IsEditingGameMaster(manager, playerID))
			return;

		EGS_SkillOverrides.SetUnit(EGS_UnitRoe.ResolveSoldier(SCR_EditableEntityComponent.Cast(item)), var.GetInt(), "Game Master");
	}
}

//------------------------------------------------------------------------------------------------
//! Session saves (CDF Game Master Save): a squad's own skill, only when set.
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EGS_SavedGroupSkillAttribute : EGS_SavedAttribute
{
	//------------------------------------------------------------------------------------------------
	override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
	{
		SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(item);
		if (manager || !editable || editable.GetEntityType() != EEditableEntityType.GROUP)
			return null;

		SCR_AIGroup squad = EGS_Manager.ResolveGroup(editable);
		if (!squad || squad.EGS_GetSkillOverride() == EGS_SkillOverrides.FOLLOW)
			return null;

		return SCR_BaseEditorAttributeVar.CreateInt(squad.EGS_GetSkillOverride());
	}

	//------------------------------------------------------------------------------------------------
	override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var, SCR_AttributesManagerEditorComponent manager, int playerID)
	{
		if (IsSessionLoad(manager, playerID, var))
			EGS_SkillOverrides.SetGroup(EGS_Manager.ResolveGroup(SCR_EditableEntityComponent.Cast(item)), var.GetInt(), "save");
	}
}

//------------------------------------------------------------------------------------------------
//! Session saves (CDF Game Master Save): a soldier's own skill, only when set.
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EGS_SavedUnitSkillAttribute : EGS_SavedAttribute
{
	//------------------------------------------------------------------------------------------------
	override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
	{
		if (manager)
			return null;

		SCR_ChimeraCharacter soldier = EGS_UnitRoe.ResolveSoldier(SCR_EditableEntityComponent.Cast(item));
		if (!soldier || soldier.EGS_GetSkillOverride() == EGS_SkillOverrides.FOLLOW)
			return null;

		return SCR_BaseEditorAttributeVar.CreateInt(soldier.EGS_GetSkillOverride());
	}

	//------------------------------------------------------------------------------------------------
	override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var, SCR_AttributesManagerEditorComponent manager, int playerID)
	{
		if (IsSessionLoad(manager, playerID, var))
			EGS_SkillOverrides.SetUnit(EGS_UnitRoe.ResolveSoldier(SCR_EditableEntityComponent.Cast(item)), var.GetInt(), "save");
	}
}

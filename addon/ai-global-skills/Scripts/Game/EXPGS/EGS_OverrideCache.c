// EXPBG ROE overrides of squads (EGS_Group.c) and soldiers (EGS_UnitRoe.c) across caching.
// - Simulation caching (Unit Caching, Garrison) keeps the entities: nothing to do.
// - Garrison Full caching keeps the squad entity; Unit Caching Full caching deletes and
//   recreates it from EBG_CacheGroupSnapshot, so the squad override rides in that snapshot
//   (applied with the squad's settings) and in its portable JSON ("egsRoe", only when set).
// - Respawned soldiers take theirs from the Unit Caching survivor carry, applied before they
//   rejoin the squad; portable snapshots hold them too ("egsSurvivorRoe", one value per
//   survivor in survivor order, only when one is set).
// The squad's and soldiers' own skill (EGS_SkillOverrides.c) ride along the same way ("egsSkill"
// on the squad and on garrison soldiers, "egsSurvivorSkill" per survivor, only when set).
// Snapshots written before these keys existed have none: no overrides.
// The squad's vanilla combat mode (snapshot setting 15) is the squad's own mode, not the one
// an EXPBG ROE set: a Full wake, and an EXPBG ROE applied afterwards, start from it, so
// Exempt (vanilla), a vanilla module default or deleting the module restore the real mode.
modded class EBG_SurvivorCarry
{
	protected int m_iEGS_Roe;
	protected int m_iEGS_Skill;

	//------------------------------------------------------------------------------------------------
	int EGS_GetSkill()
	{
		return m_iEGS_Skill;
	}

	//------------------------------------------------------------------------------------------------
	void EGS_SetSkill(int value)
	{
		m_iEGS_Skill = EGS_SkillOverrides.Normalize(value);
	}

	//------------------------------------------------------------------------------------------------
	int EGS_GetRoe()
	{
		return m_iEGS_Roe;
	}

	//------------------------------------------------------------------------------------------------
	void EGS_SetRoe(int value)
	{
		m_iEGS_Roe = Math.ClampInt(value, EGS_Settings.GROUP_ROE_DEFAULT, EGS_Settings.GROUP_ROE_EXEMPT);
	}

	//------------------------------------------------------------------------------------------------
	override void Capture(SCR_ChimeraCharacter entity)
	{
		super.Capture(entity);
		m_iEGS_Roe = EGS_Settings.GROUP_ROE_DEFAULT;
		m_iEGS_Skill = EGS_SkillOverrides.FOLLOW;
		if (entity)
		{
			m_iEGS_Roe = entity.EGS_GetRoeOverride();
			m_iEGS_Skill = entity.EGS_GetSkillOverride();
		}
	}

	//------------------------------------------------------------------------------------------------
	override void Apply(SCR_ChimeraCharacter entity)
	{
		super.Apply(entity);
		if (entity && m_iEGS_Roe != EGS_Settings.GROUP_ROE_DEFAULT)
			EGS_Manager.SetUnitRoe(entity, m_iEGS_Roe);

		if (entity && m_iEGS_Skill != EGS_SkillOverrides.FOLLOW)
			EGS_SkillOverrides.SetUnit(entity, m_iEGS_Skill, "cache");
	}
}

modded class EBG_CacheGroupSnapshot
{
	// Index of the external combat mode in EBG_CacheGroupSnapshot.Settings.
	protected static const int EGS_COMBAT_SETTING = 15;

	protected int m_iEGS_Roe;
	protected int m_iEGS_Skill;

	//------------------------------------------------------------------------------------------------
	int EGS_GetSquadRoe()
	{
		return m_iEGS_Roe;
	}

	//------------------------------------------------------------------------------------------------
	int EGS_GetSquadSkill()
	{
		return m_iEGS_Skill;
	}

	//------------------------------------------------------------------------------------------------
	override bool CaptureGroup(SCR_AIGroup group)
	{
		m_iEGS_Roe = EGS_Settings.GROUP_ROE_DEFAULT;
		m_iEGS_Skill = EGS_SkillOverrides.FOLLOW;
		if (group)
		{
			m_iEGS_Roe = group.EGS_GetRoeOverride();
			m_iEGS_Skill = group.EGS_GetSkillOverride();
		}

		bool captured = super.CaptureGroup(group);
		EAIGroupCombatMode original;
		if (captured && group && Settings && Settings.IsIndexValid(EGS_COMBAT_SETTING) && group.EGS_GetOriginalMode(original))
			Settings[EGS_COMBAT_SETTING] = original;

		return captured;
	}

	//------------------------------------------------------------------------------------------------
	override bool Apply(SCR_AIGroup group)
	{
		bool applied = super.Apply(group);
		if (!applied || !group)
			return applied;

		// The wake wrote the squad's own mode; a squad EXPBG already steers keeps its EXPBG mode.
		group.EGS_AdoptVanillaMode();
		// Applied at once, the module default included: the squad wakes under its EXPBG ROE,
		// not in its own mode until the next AI Global Skills tick (logs only a change).
		EGS_Manager.SetGroupRoe(group, m_iEGS_Roe);
		if (m_iEGS_Skill != EGS_SkillOverrides.FOLLOW)
			EGS_SkillOverrides.SetGroup(group, m_iEGS_Skill, "cache");

		return applied;
	}

	//------------------------------------------------------------------------------------------------
	override bool Write(SaveContext context)
	{
		if (!super.Write(context))
			return false;

		if (m_iEGS_Roe == EGS_Settings.GROUP_ROE_DEFAULT)
			return EGS_WriteSkill(context);

		if (!context.WriteValue("egsRoe", m_iEGS_Roe))
			return false;

		return EGS_WriteSkill(context);
	}

	//------------------------------------------------------------------------------------------------
	//! The squad's own skill, only when set.
	protected bool EGS_WriteSkill(SaveContext context)
	{
		if (m_iEGS_Skill == EGS_SkillOverrides.FOLLOW)
			return true;

		return context.WriteValue("egsSkill", m_iEGS_Skill);
	}

	//------------------------------------------------------------------------------------------------
	override bool Read(LoadContext context)
	{
		if (!super.Read(context))
			return false;

		m_iEGS_Roe = EGS_Settings.GROUP_ROE_DEFAULT;
		m_iEGS_Skill = EGS_SkillOverrides.FOLLOW;
		EGS_ReadSkill(context);
		if (!context.DoesKeyExist("egsRoe"))
			return true;

		int value;
		if (!context.ReadValue("egsRoe", value))
		{
			Print("[EXPBG AI SKILLS] cached squad ROE unreadable: the squad wakes with the module default", LogLevel.WARNING);
			return true;
		}

		m_iEGS_Roe = Math.ClampInt(value, EGS_Settings.GROUP_ROE_DEFAULT, EGS_Settings.GROUP_ROE_EXEMPT);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! The squad's own skill, only in snapshots that have it.
	protected void EGS_ReadSkill(LoadContext context)
	{
		if (!context.DoesKeyExist("egsSkill"))
			return;

		int skill;
		if (!context.ReadValue("egsSkill", skill))
		{
			Print("[EXPBG AI SKILLS] cached squad skill unreadable: the squad wakes with the module setting", LogLevel.WARNING);
			return;
		}

		m_iEGS_Skill = EGS_SkillOverrides.Normalize(skill);
	}
}

modded class EBG_PrefabFullCache
{
	//------------------------------------------------------------------------------------------------
	//! Test and diagnostics access: the squad ROE override held by this transaction's snapshot.
	int EGS_GetSnapshotSquadRoe()
	{
		if (!m_Snapshot)
			return EGS_Settings.GROUP_ROE_DEFAULT;

		return m_Snapshot.EGS_GetSquadRoe();
	}

	//------------------------------------------------------------------------------------------------
	override bool WriteSnapshot(SaveContext context)
	{
		if (!super.WriteSnapshot(context))
			return false;

		array<int> values = {};
		array<int> skills = {};
		bool any;
		bool anySkill;
		foreach (EBG_PrefabSurvivor row : m_Survivors)
		{
			int value = EGS_Settings.GROUP_ROE_DEFAULT;
			int skill = EGS_SkillOverrides.FOLLOW;
			if (row && row.Carry)
			{
				value = row.Carry.EGS_GetRoe();
				skill = row.Carry.EGS_GetSkill();
			}

			if (value != EGS_Settings.GROUP_ROE_DEFAULT)
				any = true;

			if (skill != EGS_SkillOverrides.FOLLOW)
				anySkill = true;

			values.Insert(value);
			skills.Insert(skill);
		}

		if (anySkill && !context.WriteValue("egsSurvivorSkill", skills))
			return false;

		if (!any)
			return true;

		return context.WriteValue("egsSurvivorRoe", values);
	}

	//------------------------------------------------------------------------------------------------
	override bool ReadSnapshot(LoadContext context)
	{
		if (!super.ReadSnapshot(context))
			return false;

		EGS_ReadSurvivorSkills(context);
		if (!context.DoesKeyExist("egsSurvivorRoe"))
			return true;

		array<int> values = {};
		if (!context.ReadValue("egsSurvivorRoe", values) || values.Count() != m_Survivors.Count())
		{
			Print("[EXPBG AI SKILLS] cached soldier ROE unreadable: survivors wake following their squad", LogLevel.WARNING);
			return true;
		}

		for (int i = 0; i < m_Survivors.Count(); i++)
		{
			EBG_PrefabSurvivor row = m_Survivors[i];
			if (row && row.Carry)
				row.Carry.EGS_SetRoe(values[i]);
		}

		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Survivors' own skills, only in portable snapshots that have them.
	protected void EGS_ReadSurvivorSkills(LoadContext context)
	{
		if (!context.DoesKeyExist("egsSurvivorSkill"))
			return;

		array<int> skills = {};
		if (!context.ReadValue("egsSurvivorSkill", skills) || skills.Count() != m_Survivors.Count())
		{
			Print("[EXPBG AI SKILLS] cached soldier skills unreadable: survivors wake following their squad", LogLevel.WARNING);
			return;
		}

		for (int i = 0; i < m_Survivors.Count(); i++)
		{
			EBG_PrefabSurvivor row = m_Survivors[i];
			if (row && row.Carry)
				row.Carry.EGS_SetSkill(skills[i]);
		}
	}
}

// Garrison ledger (EXPG_Snapshot.c): a garrison soldier's ROE override travels in the
// garrison's own save rows ("egsRoe", only when set), read from and written back into
// the survivor carry above, which applies it when he is respawned.
modded class EXPG_MemberSnapshot
{
	protected int m_iEGS_Roe;
	protected int m_iEGS_Skill;

	//------------------------------------------------------------------------------------------------
	int EGS_GetRoe()
	{
		return m_iEGS_Roe;
	}

	//------------------------------------------------------------------------------------------------
	override void CaptureCarry(EBG_SurvivorCarry carry)
	{
		super.CaptureCarry(carry);
		m_iEGS_Roe = EGS_Settings.GROUP_ROE_DEFAULT;
		m_iEGS_Skill = EGS_SkillOverrides.FOLLOW;
		if (carry)
		{
			m_iEGS_Roe = carry.EGS_GetRoe();
			m_iEGS_Skill = carry.EGS_GetSkill();
		}
	}

	//------------------------------------------------------------------------------------------------
	override void FillCarry(EBG_SurvivorCarry carry)
	{
		super.FillCarry(carry);
		if (carry)
		{
			carry.EGS_SetRoe(m_iEGS_Roe);
			carry.EGS_SetSkill(m_iEGS_Skill);
		}
	}

	//------------------------------------------------------------------------------------------------
	override bool WriteExtras(SaveContext context)
	{
		if (!super.WriteExtras(context))
			return false;

		if (m_iEGS_Skill != EGS_SkillOverrides.FOLLOW && !context.WriteValue("egsSkill", m_iEGS_Skill))
			return false;

		if (m_iEGS_Roe == EGS_Settings.GROUP_ROE_DEFAULT)
			return true;

		return context.WriteValue("egsRoe", m_iEGS_Roe);
	}

	//------------------------------------------------------------------------------------------------
	override bool ReadExtras(LoadContext context)
	{
		if (!super.ReadExtras(context))
			return false;

		m_iEGS_Roe = EGS_Settings.GROUP_ROE_DEFAULT;
		m_iEGS_Skill = EGS_SkillOverrides.FOLLOW;
		EGS_ReadSkill(context);
		if (!context.DoesKeyExist("egsRoe"))
			return true;

		int value;
		if (!context.ReadValue("egsRoe", value))
		{
			Print("[EXPBG AI SKILLS] garrison soldier ROE unreadable: he wakes following his squad", LogLevel.WARNING);
			return true;
		}

		m_iEGS_Roe = Math.ClampInt(value, EGS_Settings.GROUP_ROE_DEFAULT, EGS_Settings.GROUP_ROE_EXEMPT);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! A garrison soldier's own skill, only in rows that have it.
	protected void EGS_ReadSkill(LoadContext context)
	{
		if (!context.DoesKeyExist("egsSkill"))
			return;

		int skill;
		if (!context.ReadValue("egsSkill", skill))
		{
			Print("[EXPBG AI SKILLS] garrison soldier skill unreadable: he wakes following his squad", LogLevel.WARNING);
			return;
		}

		m_iEGS_Skill = EGS_SkillOverrides.Normalize(skill);
	}
}

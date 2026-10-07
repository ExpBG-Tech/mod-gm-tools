// EXPBG ROE overrides of squads (EGS_Group.c) and soldiers (EGS_UnitRoe.c) across caching.
// - Simulation caching (Unit Caching, Garrison) keeps the entities: nothing to do.
// - Garrison Full caching keeps the squad entity; Unit Caching Full caching deletes and
//   recreates it from EBG_CacheGroupSnapshot, so the squad override rides in that snapshot
//   (applied with the squad's settings) and in its portable JSON ("egsRoe", only when set).
// - Respawned soldiers take theirs from the Unit Caching survivor carry, applied before they
//   rejoin the squad; portable snapshots hold them too ("egsSurvivorRoe", one value per
//   survivor in survivor order, only when one is set).
// Snapshots written before these keys existed have none: no overrides.
modded class EBG_SurvivorCarry
{
	protected int m_iEGS_Roe;

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
		if (entity)
			m_iEGS_Roe = entity.EGS_GetRoeOverride();
	}

	//------------------------------------------------------------------------------------------------
	override void Apply(SCR_ChimeraCharacter entity)
	{
		super.Apply(entity);
		if (entity && m_iEGS_Roe != EGS_Settings.GROUP_ROE_DEFAULT)
			EGS_Manager.SetUnitRoe(entity, m_iEGS_Roe);
	}
}

modded class EBG_CacheGroupSnapshot
{
	protected int m_iEGS_Roe;

	//------------------------------------------------------------------------------------------------
	int EGS_GetSquadRoe()
	{
		return m_iEGS_Roe;
	}

	//------------------------------------------------------------------------------------------------
	override bool CaptureGroup(SCR_AIGroup group)
	{
		m_iEGS_Roe = EGS_Settings.GROUP_ROE_DEFAULT;
		if (group)
			m_iEGS_Roe = group.EGS_GetRoeOverride();

		return super.CaptureGroup(group);
	}

	//------------------------------------------------------------------------------------------------
	override bool Apply(SCR_AIGroup group)
	{
		bool applied = super.Apply(group);
		if (applied && group && m_iEGS_Roe != EGS_Settings.GROUP_ROE_DEFAULT)
			EGS_Manager.SetGroupRoe(group, m_iEGS_Roe);

		return applied;
	}

	//------------------------------------------------------------------------------------------------
	override bool Write(SaveContext context)
	{
		if (!super.Write(context))
			return false;

		if (m_iEGS_Roe == EGS_Settings.GROUP_ROE_DEFAULT)
			return true;

		return context.WriteValue("egsRoe", m_iEGS_Roe);
	}

	//------------------------------------------------------------------------------------------------
	override bool Read(LoadContext context)
	{
		if (!super.Read(context))
			return false;

		m_iEGS_Roe = EGS_Settings.GROUP_ROE_DEFAULT;
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
		bool any;
		foreach (EBG_PrefabSurvivor row : m_Survivors)
		{
			int value = EGS_Settings.GROUP_ROE_DEFAULT;
			if (row && row.Carry)
				value = row.Carry.EGS_GetRoe();

			if (value != EGS_Settings.GROUP_ROE_DEFAULT)
				any = true;

			values.Insert(value);
		}

		if (!any)
			return true;

		return context.WriteValue("egsSurvivorRoe", values);
	}

	//------------------------------------------------------------------------------------------------
	override bool ReadSnapshot(LoadContext context)
	{
		if (!super.ReadSnapshot(context))
			return false;

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
}

// Garrison ledger (EXPG_Snapshot.c): a garrison soldier's ROE override travels in the
// garrison's own save rows ("egsRoe", only when set), read from and written back into
// the survivor carry above, which applies it when he is respawned.
modded class EXPG_MemberSnapshot
{
	protected int m_iEGS_Roe;

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
		if (carry)
			m_iEGS_Roe = carry.EGS_GetRoe();
	}

	//------------------------------------------------------------------------------------------------
	override void FillCarry(EBG_SurvivorCarry carry)
	{
		super.FillCarry(carry);
		if (carry)
			carry.EGS_SetRoe(m_iEGS_Roe);
	}

	//------------------------------------------------------------------------------------------------
	override bool WriteExtras(SaveContext context)
	{
		if (!super.WriteExtras(context))
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
}

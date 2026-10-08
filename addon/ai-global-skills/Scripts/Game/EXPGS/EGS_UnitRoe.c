// EXPBG AI Global Skills: rules of engagement per soldier. The "EXPBG AI Skill & ROE"
// tab of an AI soldier's Edit properties sets his own ROE with the squad's choices (Return
// Fire Only, Fire on Sight, Warning Shots First, Exempt (vanilla)); "Use squad setting"
// (the default) follows his squad's EXPBG ROE, which follows the module default.
// Resolution: soldier override > squad override > module default ROE.
// Vanilla has no per-soldier combat mode (SCR_AICombatComponent.GetCombatMode reads the
// group's), so a soldier with his own ROE answers that query himself, at the moment the
// vanilla behaviour asks (EGS_CombatComponent.c):
//   Fire on Sight        FIRE_AT_WILL
//   Return Fire Only     HOLD_FIRE until his squad is fired upon (the EXPBG rule,
//                        EGS_Provocation.c); he also stays out of his squad's suppressive
//                        fire until then (SCR_AISuppressGroupClusterBehavior below)
//   Warning Shots First  as Return Fire Only; when he selects a player (or a vehicle with a
//                        player inside) as target he fires the warning burst himself, waits,
//                        then FIRE_AT_WILL; he re-arms after the contact (one-shot timers on
//                        the shared EGS_Manager tick); no warning once his squad is fired upon
//   Exempt (vanilla)     the mode his squad would have without EXPBG
// His effective ROE is cached on his combat component and recomputed only on events
// (module placed or removed, settings change, squad membership change, attribute change).
// Like the squad ROE it needs an AI squad without players (with or without a module: his own
// choice applies on its own; the module only adds the default); otherwise vanilla.
// The override is a plain server field: the dialog reads it on the server.
class EGS_UnitRoe
{
	static const int FOLLOW = -1;

	//------------------------------------------------------------------------------------------------
	//! Effective soldier ROE (EGS_Settings.ROE_*) or FOLLOW for his squad's (pure).
	static int Effective(int unitOverride)
	{
		if (unitOverride == EGS_Settings.GROUP_ROE_EXEMPT)
			return EGS_Settings.ROE_VANILLA;

		if (unitOverride >= EGS_Settings.ROE_RETURN_FIRE && unitOverride <= EGS_Settings.ROE_WARNING_SHOTS)
			return unitOverride;

		return FOLLOW;
	}

	//------------------------------------------------------------------------------------------------
	//! An AI soldier nobody plays or possesses, from his editable entity.
	static SCR_ChimeraCharacter ResolveSoldier(SCR_EditableEntityComponent editable)
	{
		if (!editable || editable.GetEntityType() != EEditableEntityType.CHARACTER)
			return null;

		SCR_ChimeraCharacter soldier = SCR_ChimeraCharacter.Cast(editable.GetOwner());
		if (!soldier || !soldier.FindComponent(SCR_AICombatComponent))
			return null;

		if (EGS_Manager.IsPlayerControlled(soldier) || SCR_PossessingManagerComponent.GetPlayerIdFromControlledEntity(soldier) > 0)
			return null;

		return soldier;
	}
}

//------------------------------------------------------------------------------------------------
//! Soldier override: 0 squad setting, 1 return fire, 2 fire on sight, 3 warning shots, 4 exempt.
modded class SCR_ChimeraCharacter
{
	protected int m_iEGS_RoeOverride;

	//------------------------------------------------------------------------------------------------
	int EGS_GetRoeOverride()
	{
		return m_iEGS_RoeOverride;
	}

	//------------------------------------------------------------------------------------------------
	//! Server; EGS_Manager.SetUnitRoe is the writer.
	void EGS_SetRoeOverride(int value)
	{
		if (!Replication.IsServer())
			return;

		m_iEGS_RoeOverride = Math.ClampInt(value, EGS_Settings.GROUP_ROE_DEFAULT, EGS_Settings.GROUP_ROE_EXEMPT);
	}
}

//------------------------------------------------------------------------------------------------
//! The actual combat mode of EXPBG Return Fire Only and armed Warning Shots First squads, and
//! the vanilla RETURN_FIRE rule (EvaluateCombatMode), readable whatever the group's mode.
modded class SCR_AIGroupUtilityComponent
{
	protected static const int EGS_MAX_CLUSTERS = 64;

	//------------------------------------------------------------------------------------------------
	//! A squad with EXPBG Return Fire Only, or Warning Shots First before it turned lethal,
	//! holds fire until it is fired upon (SCR_AIGroup.EGS_ReturnFireMode, EGS_Provocation.c).
	//! Vanilla would fire as soon as an enemy shot passed within 13 m of the squad leader or
	//! went off within 15 m of him. Every other group, external mode and Exempt (vanilla) squad
	//! keeps the vanilla evaluation. One flag check per group update.
	override void EvaluateCombatMode()
	{
		if (m_eCombatModeExternal == EAIGroupCombatMode.RETURN_FIRE && m_Owner && m_Owner.EGS_UsesStrictReturnFire())
		{
			m_eCombatModeActual = m_Owner.EGS_ReturnFireMode();
			return;
		}

		super.EvaluateCombatMode();
	}

	//------------------------------------------------------------------------------------------------
	//! True while a target cluster with living members endangers the group (bounded): the
	//! vanilla rule, also for a squad whose actual mode follows the EXPBG rule above.
	bool EGS_IsEndangered()
	{
		if (m_eCombatModeExternal == EAIGroupCombatMode.RETURN_FIRE && !(m_Owner && m_Owner.EGS_UsesStrictReturnFire()))
			return m_eCombatModeActual == EAIGroupCombatMode.FIRE_AT_WILL;

		if (!m_Perception || !m_Perception.m_aTargetClusters)
			return false;

		int count = Math.MinInt(m_Perception.m_aTargetClusters.Count(), EGS_MAX_CLUSTERS);
		for (int i = 0; i < count; i++)
		{
			SCR_AIGroupTargetCluster cluster = m_Perception.m_aTargetClusters[i];
			if (cluster && cluster.m_State && cluster.m_State.m_iCountEndangering != 0 && cluster.m_State.m_iCountAlive != 0)
				return true;
		}

		return false;
	}
}

//------------------------------------------------------------------------------------------------
//! Vanilla hands every member his squad's suppressed cluster without asking his own combat
//! mode, so a soldier holding fire under his own ROE (EGS_CombatComponent.GetCombatMode)
//! still joined his squad's suppressive fire. He now stays out of it until his own mode
//! allows firing. Soldiers who follow their squad, and EXPBG warning bursts (the base
//! suppress behaviour), are unchanged. Chains with other addons' overrides (EXPBG_RO_AI).
modded class SCR_AISuppressGroupClusterBehavior
{
	override float CustomEvaluate()
	{
		float priority = super.CustomEvaluate();
		if (priority <= 0 || !m_CombatComponent || m_CombatComponent.EGS_GetUnitRoe() == EGS_UnitRoe.FOLLOW)
			return priority;

		if (m_CombatComponent.GetCombatMode() == EAIGroupCombatMode.HOLD_FIRE)
			return 0;

		return priority;
	}
}

//------------------------------------------------------------------------------------------------
//! "Rules of engagement (soldier)" in the EXPBG AI Skill & ROE tab of an AI soldier
//! (any faction or mod, with or without a module). Attribute-based savers use
//! EGS_SavedUnitRoeAttribute.
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EGS_UnitRoeAttribute : SCR_BaseEditorAttribute
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

	//------------------------------------------------------------------------------------------------
	override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
	{
		if (!manager)
			return null;

		SCR_ChimeraCharacter soldier = EGS_UnitRoe.ResolveSoldier(SCR_EditableEntityComponent.Cast(item));
		if (!soldier)
			return null;

		return SCR_BaseEditorAttributeVar.CreateInt(soldier.EGS_GetRoeOverride());
	}

	//------------------------------------------------------------------------------------------------
	override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var, SCR_AttributesManagerEditorComponent manager, int playerID)
	{
		if (!var || !Replication.IsServer() || !EGS_Attributes.IsEditingGameMaster(manager, playerID))
			return;

		EGS_Manager.SetUnitRoe(EGS_UnitRoe.ResolveSoldier(SCR_EditableEntityComponent.Cast(item)), var.GetInt());
	}
}

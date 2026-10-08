// Native mission save of EXPBG AI Global Skills: per-faction skill/aim (by faction key),
// default ROE, ammunition policy, per-group ROE overrides (by group persistence id),
// from version 2 per-soldier ROE overrides (by soldier persistence id) and, from version 3,
// per-squad and per-soldier skill (EGS_SkillOverrides.c). The oldest layout that holds the
// mission is written (version 1 without soldier ROE or skills, version 2 without skills), so
// earlier releases still load it; version 1 and 2 saves still load.
// The module entity itself is saved by the vanilla editable-entity configuration; the
// settings apply while a module exists. Untouched missions write nothing (DEFAULT).
// Attribute-based savers (CDF Game Master Save) use EGS_SavedAttributes.c instead. The
// vanilla group serializer below saves a group's own combat mode, not the EXPBG one.
class EGS_SettingsState : PersistentState
{
}

class EGS_SettingsSerializer : ScriptedStateSerializer
{
	protected static const int VERSION = 2;
	protected static const int VERSION_GROUPS_ONLY = 1;
	protected static const int VERSION_SKILLS = 3;

	//------------------------------------------------------------------------------------------------
	override static typename GetTargetType()
	{
		return EGS_SettingsState;
	}

	//------------------------------------------------------------------------------------------------
	override static EDeserializeFailHandling GetDeserializeFailHandling()
	{
		return EDeserializeFailHandling.IGNORE;
	}

	//------------------------------------------------------------------------------------------------
	override protected ESerializeResult Serialize(notnull Managed instance, notnull SaveContext context)
	{
		array<string> factionKeys = {};
		array<int> factionValues = {};
		EGS_Settings.ExportPersistent(factionKeys, factionValues);

		array<UUID> groupIds = {};
		array<int> groupRoe = {};
		EGS_Manager.ExportGroupOverrides(groupIds, groupRoe);

		array<UUID> unitIds = {};
		array<int> unitRoe = {};
		EGS_Manager.ExportUnitOverrides(unitIds, unitRoe);

		array<UUID> skillGroupIds = {};
		array<int> skillGroupValues = {};
		array<UUID> skillUnitIds = {};
		array<int> skillUnitValues = {};
		EGS_SkillOverrides.Export(skillGroupIds, skillGroupValues, skillUnitIds, skillUnitValues);
		bool hasSkills = !skillGroupIds.IsEmpty() || !skillUnitIds.IsEmpty();

		if (factionKeys.IsEmpty() && groupIds.IsEmpty() && unitIds.IsEmpty() && !hasSkills && EGS_Settings.IsVanilla())
			return ESerializeResult.DEFAULT;

		// Without soldier overrides the version 1 layout is written, so 0.1.9 still loads it;
		// without squad or soldier skills version 2, so 0.1.17 still loads it.
		int version = VERSION;
		if (unitIds.IsEmpty())
			version = VERSION_GROUPS_ONLY;

		if (hasSkills)
			version = VERSION_SKILLS;

		int roe = EGS_Settings.GetRoe();
		int ammo = EGS_Settings.GetAmmoMode();
		int refills = EGS_Settings.GetRefills();
		if (!context.WriteValue("version", version))
			return ESerializeResult.ERROR;

		if (!context.WriteValue("factionKeys", factionKeys) || !context.WriteValue("factionValues", factionValues))
			return ESerializeResult.ERROR;

		if (!context.WriteValue("roe", roe) || !context.WriteValue("ammo", ammo) || !context.WriteValue("refills", refills))
			return ESerializeResult.ERROR;

		if (!context.WriteValue("groupIds", groupIds) || !context.WriteValue("groupRoe", groupRoe))
			return ESerializeResult.ERROR;

		if (version >= VERSION && (!context.WriteValue("unitIds", unitIds) || !context.WriteValue("unitRoe", unitRoe)))
			return ESerializeResult.ERROR;

		if (version >= VERSION_SKILLS && (!context.WriteValue("skillGroupIds", skillGroupIds) || !context.WriteValue("skillGroupValues", skillGroupValues)))
			return ESerializeResult.ERROR;

		if (version >= VERSION_SKILLS && (!context.WriteValue("skillUnitIds", skillUnitIds) || !context.WriteValue("skillUnitValues", skillUnitValues)))
			return ESerializeResult.ERROR;

		return ESerializeResult.OK;
	}

	//------------------------------------------------------------------------------------------------
	override protected bool Deserialize(notnull Managed instance, notnull LoadContext context)
	{
		int version;
		if (!context.ReadValue("version", version) || (version != VERSION && version != VERSION_GROUPS_ONLY && version != VERSION_SKILLS))
			return false;

		array<string> factionKeys = {};
		array<int> factionValues = {};
		int roe;
		int ammo;
		int refills = EGS_Settings.REFILLS_DEFAULT;
		if (!context.ReadValue("factionKeys", factionKeys) || !context.ReadValue("factionValues", factionValues))
			return false;

		if (factionKeys.Count() > EGS_Settings.MAX_FACTIONS || factionValues.Count() != factionKeys.Count() * EGS_Settings.SLOT_COUNT)
			return false;

		if (!context.ReadValue("roe", roe) || !context.ReadValue("ammo", ammo) || !context.ReadValue("refills", refills))
			return false;

		array<UUID> groupIds = {};
		array<int> groupRoe = {};
		if (!context.ReadValue("groupIds", groupIds) || !context.ReadValue("groupRoe", groupRoe) || groupIds.Count() != groupRoe.Count())
			return false;

		array<UUID> unitIds = {};
		array<int> unitRoe = {};
		if (version >= VERSION)
		{
			if (!context.ReadValue("unitIds", unitIds) || !context.ReadValue("unitRoe", unitRoe) || unitIds.Count() != unitRoe.Count())
				return false;
		}

		array<UUID> skillGroupIds = {};
		array<int> skillGroupValues = {};
		array<UUID> skillUnitIds = {};
		array<int> skillUnitValues = {};
		if (version >= VERSION_SKILLS)
		{
			if (!context.ReadValue("skillGroupIds", skillGroupIds) || !context.ReadValue("skillGroupValues", skillGroupValues) || skillGroupIds.Count() != skillGroupValues.Count())
				return false;

			if (!context.ReadValue("skillUnitIds", skillUnitIds) || !context.ReadValue("skillUnitValues", skillUnitValues) || skillUnitIds.Count() != skillUnitValues.Count())
				return false;
		}

		EGS_Settings.ImportPersistent(factionKeys, factionValues, roe, ammo, refills);
		EGS_Manager.ImportGroupOverrides(groupIds, groupRoe);
		EGS_Manager.ImportUnitOverrides(unitIds, unitRoe);
		EGS_SkillOverrides.Import(skillGroupIds, skillGroupValues, skillUnitIds, skillUnitValues);
		return true;
	}
}

//------------------------------------------------------------------------------------------------
//! Native mission save of an AI group's vanilla combat mode: the group's own mode, not the one
//! an EXPBG ROE set (the EXPBG ROE is saved above and applied again after the load). A load
//! onto a group EXPBG already steers keeps the loaded mode as the group's own and puts the
//! EXPBG mode back, so Exempt (vanilla) and deleting the module restore the real mode.
modded class SCR_AIGroupUtilityComponentSerializer
{
	override protected ESerializeResult Serialize(notnull IEntity owner, notnull GenericComponent component, notnull SaveContext context)
	{
		SCR_AIGroupUtilityComponent utility = SCR_AIGroupUtilityComponent.Cast(component);
		SCR_AIGroup group = SCR_AIGroup.Cast(owner);
		EAIGroupCombatMode original;
		if (!utility || !group || !group.EGS_GetOriginalMode(original))
			return super.Serialize(owner, component, context);

		// Written synchronously: the EXPBG mode is back before anything else reads it.
		EAIGroupCombatMode current = utility.GetCombatModeExternal();
		utility.SetCombatMode(original);
		ESerializeResult result = super.Serialize(owner, component, context);
		utility.SetCombatMode(current);
		return result;
	}

	override protected bool Deserialize(notnull IEntity owner, notnull GenericComponent component, notnull LoadContext context)
	{
		SCR_AIGroupUtilityComponent utility = SCR_AIGroupUtilityComponent.Cast(component);
		SCR_AIGroup group = SCR_AIGroup.Cast(owner);
		if (!utility || !group || !group.EGS_IsRoeTouched())
			return super.Deserialize(owner, component, context);

		// Vanilla writes only a mode other than its default: start from that default.
		utility.SetCombatMode(EAIGroupCombatMode.FIRE_AT_WILL);
		bool loaded = super.Deserialize(owner, component, context);
		group.EGS_AdoptVanillaMode();
		return loaded;
	}
}

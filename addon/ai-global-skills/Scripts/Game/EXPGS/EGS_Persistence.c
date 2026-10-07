// Native mission save of EXPBG AI Global Skills: per-faction skill/aim (by faction key),
// default ROE, ammunition policy, per-group ROE overrides (by group persistence id) and,
// from version 2, per-soldier ROE overrides (by soldier persistence id). A mission
// without soldier overrides is still written as version 1; version 1 saves still load.
// The module entity itself is saved by the vanilla editable-entity configuration; the
// settings apply while a module exists. Untouched missions write nothing (DEFAULT).
// Attribute-based savers (CDF Game Master Save) use EGS_SavedAttributes.c instead.
class EGS_SettingsState : PersistentState
{
}

class EGS_SettingsSerializer : ScriptedStateSerializer
{
	protected static const int VERSION = 2;
	protected static const int VERSION_GROUPS_ONLY = 1;

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

		if (factionKeys.IsEmpty() && groupIds.IsEmpty() && unitIds.IsEmpty() && EGS_Settings.IsVanilla())
			return ESerializeResult.DEFAULT;

		// Without soldier overrides the version 1 layout is written, so 0.1.9 still loads it.
		int version = VERSION;
		if (unitIds.IsEmpty())
			version = VERSION_GROUPS_ONLY;

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

		return ESerializeResult.OK;
	}

	//------------------------------------------------------------------------------------------------
	override protected bool Deserialize(notnull Managed instance, notnull LoadContext context)
	{
		int version;
		if (!context.ReadValue("version", version) || (version != VERSION && version != VERSION_GROUPS_ONLY))
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

		EGS_Settings.ImportPersistent(factionKeys, factionValues, roe, ammo, refills);
		EGS_Manager.ImportGroupOverrides(groupIds, groupRoe);
		EGS_Manager.ImportUnitOverrides(unitIds, unitRoe);
		return true;
	}
}

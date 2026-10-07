// Session-only attributes (never shown in the dialog): attribute-based Game Master savers
// such as CDF Game Master Save read them with a null manager and write them back on load
// with playerID -1. The native 1.8 mission save uses EGS_SettingsState instead.
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EGS_SavedAttribute : SCR_BaseEditorAttribute
{
	//------------------------------------------------------------------------------------------------
	protected static bool IsSessionLoad(SCR_AttributesManagerEditorComponent manager, int playerID, SCR_BaseEditorAttributeVar var)
	{
		return !manager && playerID == -1 && var && Replication.IsServer() && GetGame() && GetGame().InPlayMode();
	}

	//------------------------------------------------------------------------------------------------
	protected static bool IsModule(Managed item)
	{
		SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(item);
		return editable && EGS_Module.Cast(editable.GetOwner());
	}
}

//------------------------------------------------------------------------------------------------
//! Module: default ROE, ammunition mode and refill count.
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EGS_SavedGlobalAttribute : EGS_SavedAttribute
{
	override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
	{
		if (manager || !IsModule(item))
			return null;

		return SCR_BaseEditorAttributeVar.CreateVector(Vector(EGS_Settings.GetRoe(), EGS_Settings.GetAmmoMode(), EGS_Settings.GetRefills()));
	}

	override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var, SCR_AttributesManagerEditorComponent manager, int playerID)
	{
		if (IsSessionLoad(manager, playerID, var) && IsModule(item))
			EGS_Settings.ImportSavedGlobal(var.GetVector());
	}
}

//------------------------------------------------------------------------------------------------
//! Module: the n-th faction with EXPBG values (m_iSavedSlot), packed into one vector.
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EGS_SavedFactionAttribute : EGS_SavedAttribute
{
	[Attribute("0", desc: "Saved faction slot")]
	protected int m_iSavedSlot;

	override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
	{
		if (manager || !IsModule(item))
			return null;

		vector saved;
		if (!EGS_Settings.ExportSavedFaction(m_iSavedSlot, saved))
			return null;

		return SCR_BaseEditorAttributeVar.CreateVector(saved);
	}

	override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var, SCR_AttributesManagerEditorComponent manager, int playerID)
	{
		if (IsSessionLoad(manager, playerID, var) && IsModule(item))
			EGS_Settings.ImportSavedFaction(var.GetVector());
	}
}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EGS_SavedFaction0Attribute : EGS_SavedFactionAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EGS_SavedFaction1Attribute : EGS_SavedFactionAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EGS_SavedFaction2Attribute : EGS_SavedFactionAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EGS_SavedFaction3Attribute : EGS_SavedFactionAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EGS_SavedFaction4Attribute : EGS_SavedFactionAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EGS_SavedFaction5Attribute : EGS_SavedFactionAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EGS_SavedFaction6Attribute : EGS_SavedFactionAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EGS_SavedFaction7Attribute : EGS_SavedFactionAttribute {}

//------------------------------------------------------------------------------------------------
//! AI group: its rules-of-engagement override.
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EGS_SavedGroupRoeAttribute : EGS_SavedAttribute
{
	override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
	{
		SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(item);
		if (manager || !editable || editable.GetEntityType() != EEditableEntityType.GROUP)
			return null;

		SCR_AIGroup group = EGS_Manager.ResolveGroup(editable);
		if (!group || group.EGS_GetRoeOverride() == EGS_Settings.GROUP_ROE_DEFAULT)
			return null;

		return SCR_BaseEditorAttributeVar.CreateInt(group.EGS_GetRoeOverride());
	}

	override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var, SCR_AttributesManagerEditorComponent manager, int playerID)
	{
		if (IsSessionLoad(manager, playerID, var))
			EGS_Manager.SetGroupRoe(EGS_Manager.ResolveGroup(SCR_EditableEntityComponent.Cast(item)), var.GetInt());
	}
}

//------------------------------------------------------------------------------------------------
//! AI soldier: his own rules-of-engagement override (EGS_UnitRoe.c).
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EGS_SavedUnitRoeAttribute : EGS_SavedAttribute
{
	override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
	{
		if (manager)
			return null;

		SCR_ChimeraCharacter soldier = EGS_UnitRoe.ResolveSoldier(SCR_EditableEntityComponent.Cast(item));
		if (!soldier || soldier.EGS_GetRoeOverride() == EGS_Settings.GROUP_ROE_DEFAULT)
			return null;

		return SCR_BaseEditorAttributeVar.CreateInt(soldier.EGS_GetRoeOverride());
	}

	override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var, SCR_AttributesManagerEditorComponent manager, int playerID)
	{
		if (IsSessionLoad(manager, playerID, var))
			EGS_Manager.SetUnitRoe(EGS_UnitRoe.ResolveSoldier(SCR_EditableEntityComponent.Cast(item)), var.GetInt());
	}
}

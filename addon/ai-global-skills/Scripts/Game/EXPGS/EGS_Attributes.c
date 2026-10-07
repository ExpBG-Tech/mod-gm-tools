// Game Master attributes of the EXPBG AI Global Skills module and of AI groups.
//
// Per-faction UI: the attribute system cannot create tabs at runtime, so the "EXPBG AI
// Skills" tab has one "Faction to edit" selector (filled with the factions detected in the
// running mission) and ten skill/aim controls that show and edit the selected faction.
// Changing the selector refreshes the controls from the replicated table at once. Every
// per-faction value carries its faction index (var.y), so Save always writes to the faction
// that was shown; one faction is edited per Save.
class EGS_Attributes
{
	//------------------------------------------------------------------------------------------------
	//! Attribute type that edits a per-faction slot (used for the client-side faction switch).
	static typename SlotAttributeType(int slot)
	{
		switch (slot)
		{
			case 0: return EGS_SkillAllAttribute;
			case 1: return EGS_SkillRiflemanAttribute;
			case 2: return EGS_SkillMachineGunnerAttribute;
			case 3: return EGS_SkillMarksmanAttribute;
			case 4: return EGS_SkillLeaderAttribute;
			case 5: return EGS_AimAllAttribute;
			case 6: return EGS_AimRiflemanAttribute;
			case 7: return EGS_AimMachineGunnerAttribute;
			case 8: return EGS_AimMarksmanAttribute;
			case 9: return EGS_AimLeaderAttribute;
		}

		return EGS_SkillAllAttribute;
	}

	//------------------------------------------------------------------------------------------------
	//! Writes come only from the editing Game Master's own unlimited editor in Edit mode.
	static bool IsEditingGameMaster(SCR_AttributesManagerEditorComponent manager, int playerID)
	{
		if (!manager)
			return false;

		SCR_EditorManagerEntity editor = manager.GetManager();
		if (!editor || editor.GetPlayerID() != playerID || editor.IsLimited() || editor.GetCurrentMode() != EEditorMode.EDIT)
			return false;

		return true;
	}
}

//------------------------------------------------------------------------------------------------
//! Base of every module attribute. Values are persisted by EGS_SettingsState, not by the
//! attribute session serializer.
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EGS_ModuleAttribute : SCR_BaseEditorAttribute
{
	[Attribute("0", desc: "EXPBG AI Global Skills setting key")]
	protected int m_iKey;

	[Attribute(desc: "Spinbox entries, in value order (entry index = stored value)")]
	protected ref array<ref SCR_EditorAttributeFloatStringValueHolder> m_aValues;

	[Attribute(desc: "Slider range (slider attributes only)")]
	protected ref SCR_EditorAttributeBaseValues m_SliderValues;

	//------------------------------------------------------------------------------------------------
	override bool IsSerializable()
	{
		return false;
	}

	//------------------------------------------------------------------------------------------------
	override int GetEntries(notnull array<ref SCR_BaseEditorAttributeEntry> outEntries)
	{
		if (m_aValues && !m_aValues.IsEmpty())
			outEntries.Insert(new SCR_BaseEditorAttributeFloatStringValues(m_aValues));
		else if (m_SliderValues)
			outEntries.Insert(new SCR_BaseEditorAttributeEntrySlider(m_SliderValues));

		return outEntries.Count();
	}

	//------------------------------------------------------------------------------------------------
	protected EGS_Module GetModule(Managed item)
	{
		SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(item);
		if (!editable)
			return null;

		return EGS_Module.Cast(editable.GetOwner());
	}

	//------------------------------------------------------------------------------------------------
	override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
	{
		if (!manager || !GetModule(item))
			return null;

		return SCR_BaseEditorAttributeVar.CreateVector(EGS_Settings.ReadSetting(m_iKey));
	}

	//------------------------------------------------------------------------------------------------
	override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var, SCR_AttributesManagerEditorComponent manager, int playerID)
	{
		if (!var || !Replication.IsServer() || !GetModule(item))
			return;

		if (!EGS_Attributes.IsEditingGameMaster(manager, playerID))
			return;

		EGS_Settings.WriteSetting(m_iKey, var.GetVector());
	}
}

//------------------------------------------------------------------------------------------------
//! Faction selector. Entries are the military factions detected in the mission (replicated
//! from the server), so modded factions appear without a compatibility addon.
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EGS_FactionAttribute : EGS_ModuleAttribute
{
	protected ref array<ref SCR_EditorAttributeFloatStringValueHolder> m_aFactionEntries = {};

	//------------------------------------------------------------------------------------------------
	override int GetEntries(notnull array<ref SCR_BaseEditorAttributeEntry> outEntries)
	{
		m_aFactionEntries.Clear();
		EGS_Module module = EGS_Module.GetAny();
		int count;
		if (module)
			count = module.EGS_GetFactionCount();

		for (int i = 0; i < count; i++)
		{
			SCR_EditorAttributeFloatStringValueHolder entry = new SCR_EditorAttributeFloatStringValueHolder();
			entry.SetName(EGS_Settings.FactionDisplayName(module.EGS_GetFactionKey(i)));
			entry.SetFloatValue(i);
			m_aFactionEntries.Insert(entry);
		}

		if (m_aFactionEntries.IsEmpty())
		{
			SCR_EditorAttributeFloatStringValueHolder none = new SCR_EditorAttributeFloatStringValueHolder();
			none.SetName("No military faction detected");
			none.SetFloatValue(0);
			m_aFactionEntries.Insert(none);
		}

		outEntries.Insert(new SCR_BaseEditorAttributeFloatStringValues(m_aFactionEntries));
		return outEntries.Count();
	}

	//------------------------------------------------------------------------------------------------
	//! Client side: show the newly selected faction in the ten skill/aim controls.
	override void UpdateInterlinkedVariables(SCR_BaseEditorAttributeVar var, SCR_AttributesManagerEditorComponent manager, bool isInit = false)
	{
		if (!var || !manager || isInit)
			return;

		EGS_Module module = EGS_Module.GetAny();
		if (!module)
			return;

		int faction = var.GetInt();
		for (int slot = 0; slot < EGS_Settings.SLOT_COUNT; slot++)
		{
			int value = module.EGS_GetValue(faction, slot);
			manager.SetAttributeVariable(EGS_Attributes.SlotAttributeType(slot), SCR_BaseEditorAttributeVar.CreateVector(Vector(value, faction, 0)));
		}
	}
}

//------------------------------------------------------------------------------------------------
//! The refill count only matters for "Auto-refill when out".
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EGS_AmmoModeAttribute : EGS_ModuleAttribute
{
	override void UpdateInterlinkedVariables(SCR_BaseEditorAttributeVar var, SCR_AttributesManagerEditorComponent manager, bool isInit = false)
	{
		if (!var || !manager)
			return;

		manager.SetAttributeEnabled(EGS_RefillCountAttribute, var.GetInt() == EGS_Settings.AMMO_REFILL);
	}
}

// Distinct types are required by the editor's duplicate-attribute check.
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EGS_SkillAllAttribute : EGS_ModuleAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EGS_SkillRiflemanAttribute : EGS_ModuleAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EGS_SkillMachineGunnerAttribute : EGS_ModuleAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EGS_SkillMarksmanAttribute : EGS_ModuleAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EGS_SkillLeaderAttribute : EGS_ModuleAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EGS_AimAllAttribute : EGS_ModuleAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EGS_AimRiflemanAttribute : EGS_ModuleAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EGS_AimMachineGunnerAttribute : EGS_ModuleAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EGS_AimMarksmanAttribute : EGS_ModuleAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EGS_AimLeaderAttribute : EGS_ModuleAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EGS_RoeDefaultAttribute : EGS_ModuleAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EGS_RefillCountAttribute : EGS_ModuleAttribute {}

//------------------------------------------------------------------------------------------------
//! "Rules of engagement (squad)": per-group rules of engagement in the EXPBG Rules of
//! Engagement tab of the group's attribute dialog (shown while a module exists), next to
//! the soldier's own (EGS_UnitRoe.c). The editor writes only changed values, once per
//! edited group.
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EGS_GroupRoeAttribute : SCR_BaseEditorAttribute
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
		if (!manager || !EGS_Module.HasAny())
			return null;

		SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(item);
		if (!editable || editable.GetEntityType() != EEditableEntityType.GROUP)
			return null;

		SCR_AIGroup group = EGS_Manager.ResolveGroup(editable);
		if (!group)
			return null;

		return SCR_BaseEditorAttributeVar.CreateInt(group.EGS_GetRoeOverride());
	}

	//------------------------------------------------------------------------------------------------
	override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var, SCR_AttributesManagerEditorComponent manager, int playerID)
	{
		if (!var || !Replication.IsServer() || !EGS_Attributes.IsEditingGameMaster(manager, playerID))
			return;

		EGS_Manager.SetGroupRoe(EGS_Manager.ResolveGroup(SCR_EditableEntityComponent.Cast(item)), var.GetInt());
	}
}

//------------------------------------------------------------------------------------------------
//! A module-only selection shows only the module's own controls (other addons may answer
//! global settings for every edited entity). Native RPC ids are preserved: rows are removed,
//! never renumbered.
modded class SCR_AttributesManagerEditorComponent
{
	override protected int GetVariables(bool onlyServer, notnull array<Managed> items, notnull out array<int> outIds, notnull out array<ref SCR_BaseEditorAttributeVar> outVars, notnull out array<ref EEditorAttributeMultiSelect> outAttributesMultiSelect)
	{
		super.GetVariables(onlyServer, items, outIds, outVars, outAttributesMultiSelect);
		if (items.IsEmpty())
			return outVars.Count();

		foreach (Managed item : items)
		{
			SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(item);
			if (!editable || !EGS_Module.Cast(editable.GetOwner()))
				return outVars.Count();
		}

		for (int i = outIds.Count() - 1; i >= 0; i--)
		{
			if (EGS_ModuleAttribute.Cast(m_PrefabData.GetAttribute(outIds[i])))
				continue;

			outIds.RemoveOrdered(i);
			outVars.RemoveOrdered(i);
			outAttributesMultiSelect.RemoveOrdered(i);
		}

		return outVars.Count();
	}
}

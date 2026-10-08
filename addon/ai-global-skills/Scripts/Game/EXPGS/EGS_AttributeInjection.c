// The squad and soldier attributes of the EXPBG AI Skill & ROE tab ("AI skill" and "Rules of
// engagement", squad and soldier) are listed in the vanilla Edit attribute list override
// (Configs/Editor/AttributeLists/Edit.conf) and, as a fallback, appended here from this
// module's own list (Configs/Editor/AttributeLists/EXPGS/EGS_SquadSoldier.conf). Another mod
// that replaces the vanilla Edit list, or an editor mode prefab override that swaps the list,
// therefore cannot hide them. Only classes the Game Master Edit list does not hold yet are
// appended, once, lazily, before the class first hands out attribute indexes; server and
// clients run the same code on the same lists, so the indexes sent between them agree.
// Only the Edit mode list is extended (it holds the vanilla "Set combat mode" attribute);
// Admin and Photo modes keep theirs.
modded class SCR_AttributesManagerEditorComponentClass
{
	protected static const ResourceName EGS_SQUAD_SOLDIER_ATTRIBUTES = "{A4BB2BF355CA2A33}Configs/Editor/AttributeLists/EXPGS/EGS_SquadSoldier.conf";

	protected bool m_bEGS_Injected;
	// Owns the appended attributes (m_aAttributes holds weak references).
	protected ref SCR_EditorAttributeList m_EGS_SquadSoldierList;

	//------------------------------------------------------------------------------------------------
	//! True for the Game Master Edit list: it holds the vanilla "Set combat mode" attribute or
	//! this module's own attributes.
	protected bool EGS_IsEditList()
	{
		foreach (SCR_BaseEditorAttribute attribute : m_aAttributes)
		{
			if (SCR_AIGroupCombatModeAttribute.Cast(attribute) || EGS_ModuleAttribute.Cast(attribute))
				return true;
		}

		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected bool EGS_HasAttributeType(typename type)
	{
		foreach (SCR_BaseEditorAttribute attribute : m_aAttributes)
		{
			if (attribute && attribute.Type() == type)
				return true;
		}

		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected void EGS_InjectSquadSoldier()
	{
		if (m_bEGS_Injected)
			return;

		m_bEGS_Injected = true;
		if (!m_aAttributes || !EGS_IsEditList())
			return;

		Resource resource = BaseContainerTools.LoadContainer(EGS_SQUAD_SOLDIER_ATTRIBUTES);
		if (!resource || !resource.IsValid())
		{
			Print("[EXPBG AI SKILLS] squad and soldier attribute list missing: only the Edit list entries are used", LogLevel.WARNING);
			return;
		}

		BaseResourceObject container = resource.GetResource();
		if (!container)
			return;

		m_EGS_SquadSoldierList = SCR_EditorAttributeList.Cast(BaseContainerTools.CreateInstanceFromContainer(container.ToBaseContainer()));
		if (!m_EGS_SquadSoldierList)
			return;

		int appended;
		for (int i = 0, count = m_EGS_SquadSoldierList.GetAttributesCount(); i < count; i++)
		{
			SCR_BaseEditorAttribute attribute = m_EGS_SquadSoldierList.GetAttribute(i);
			if (!attribute || EGS_HasAttributeType(attribute.Type()))
				continue;

			m_aAttributes.Insert(attribute);
			appended++;
		}

		if (appended > 0)
			PrintFormat("[EXPBG AI SKILLS] %1 squad/soldier attributes appended to the Game Master Edit list (its override did not carry them)", appended);
	}

	//------------------------------------------------------------------------------------------------
	override SCR_BaseEditorAttribute GetAttribute(int index)
	{
		EGS_InjectSquadSoldier();
		return super.GetAttribute(index);
	}

	//------------------------------------------------------------------------------------------------
	override int GetAttributesCount()
	{
		EGS_InjectSquadSoldier();
		return super.GetAttributesCount();
	}

	//------------------------------------------------------------------------------------------------
	override int FindAttribute(SCR_BaseEditorAttribute attribute)
	{
		EGS_InjectSquadSoldier();
		return super.FindAttribute(attribute);
	}
}

// Adapted from GME Mod Team, Game Master Enhanced 1.3.8; APL. See THIRD_PARTY_NOTICES.md.
//------------------------------------------------------------------------------------------------
class EII_EditboxEditorAttributeUIComponent: SCR_BaseEditorAttributeUIComponent
{
	protected SCR_EditBoxComponent m_EditBoxComponent;

	// Vanilla CreateCopyVariable clones only a vector when a mixed-value field is disabled.
	override protected void ToggleEnable(bool enabled)
	{
		SCR_BaseEditorAttribute attribute = GetAttribute();
		bool preserve = !enabled && attribute && attribute.GetHasConflictingValues() && attribute.GetVariable(false);
		string value;
		if (preserve) value = attribute.GetVariable(false).EII_GetString();
		super.ToggleEnable(enabled);
		if (preserve && attribute.GetCopyVariable()) attribute.GetCopyVariable().EII_SetString(value);
	}

	//------------------------------------------------------------------------------------------------
	override void Init(Widget w, SCR_BaseEditorAttribute attribute)
	{
		Widget editboxWidget = w.FindAnyWidget(m_sUiComponentName);
		if (!editboxWidget)
			return;

		m_EditBoxComponent = SCR_EditBoxComponent.Cast(editboxWidget.FindHandler(SCR_EditBoxComponent));
		if (!m_EditBoxComponent)
			return;

		// Reject oversized edits whole; do not cut a UTF-8 sequence at the byte limit.
		Widget input = editboxWidget.FindAnyWidget("EditBox");
		if (input)
		{
			EditBoxFilterComponent filter = EditBoxFilterComponent.Cast(input.FindHandler(EditBoxFilterComponent));
			if (filter) filter.SetCharacterLimit(8192);
		}

		m_EditBoxComponent.m_OnChanged.Insert(OnChangeEditbox);

		super.Init(w, attribute);
	}

	//------------------------------------------------------------------------------------------------
	//! Sets a default state for the UI and var value if conflicting attribute
	override void SetVariableToDefaultValue(SCR_BaseEditorAttributeVar var)
	{
		m_EditBoxComponent.SetValue("");

		if (!var)
			return;

		var.EII_SetString("");
	}

	//------------------------------------------------------------------------------------------------
	override void SetFromVar(SCR_BaseEditorAttributeVar var)
	{
		super.SetFromVar(var);

		if (!var)
			return;

		m_EditBoxComponent.SetValue(var.EII_GetString());
	}

	//------------------------------------------------------------------------------------------------
	protected void OnChangeEditbox(SCR_EditBoxComponent selectionBox, string value)
	{
		OnChange(null, false);
	}

	//------------------------------------------------------------------------------------------------
	override bool OnChange(Widget w, bool finished)
	{
		if (!m_EditBoxComponent)
			return false;

		string newValue = m_EditBoxComponent.GetValue();

		SCR_BaseEditorAttribute attribute = GetAttribute();
		if (!attribute) return false;

		SCR_BaseEditorAttributeVar var = attribute.GetVariable(true);
		if (!var) return false;
		int limit = EII_IntelComponent.TITLE_LIMIT;
		if (EII_ContentAttribute.Cast(attribute)) limit = EII_IntelComponent.CONTENT_LIMIT;
		if (newValue.Length() > limit)
		{
			m_EditBoxComponent.SetValue(var.EII_GetString());
			return false;
		}

		if (var.EII_GetString() == newValue)
			return false;

		var.EII_SetString(newValue);
		super.OnChange(w, finished);
		return false;
	}

	//------------------------------------------------------------------------------------------------
	override void HandlerDeattached(Widget w)
	{
		if (m_EditBoxComponent)
			m_EditBoxComponent.m_OnChanged.Remove(OnChangeEditbox);
		super.HandlerDeattached(w);
	}
}

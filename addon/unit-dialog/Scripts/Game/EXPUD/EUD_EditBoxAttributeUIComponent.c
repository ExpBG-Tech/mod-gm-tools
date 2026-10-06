// Editor attribute row with a single-line vanilla edit box (UI/layouts/EXPUD/EUD_EditBox.layout).
// Moves text between the box and the attribute variable's EUD text payload.
class EUD_EditBoxAttributeUIComponent : SCR_BaseEditorAttributeUIComponent
{
 protected SCR_EditBoxComponent m_EUD_EditBox;
 protected int m_iEUD_Limit = EUD_Dialog.LINE_LIMIT;

 override void Init(Widget w, SCR_BaseEditorAttribute attribute)
 {
  Widget boxRoot = w.FindAnyWidget(m_sUiComponentName);
  if (!boxRoot) return;
  m_EUD_EditBox = SCR_EditBoxComponent.Cast(boxRoot.FindHandler(SCR_EditBoxComponent));
  if (!m_EUD_EditBox) return;
  EUD_TextAttribute textAttribute = EUD_TextAttribute.Cast(attribute);
  if (textAttribute) m_iEUD_Limit = textAttribute.GetLimit();
  // Character limit as a typing aid; the byte limit is enforced below and on the server.
  Widget input = boxRoot.FindAnyWidget("EditBox");
  if (input)
  {
   EditBoxFilterComponent filter = EditBoxFilterComponent.Cast(input.FindHandler(EditBoxFilterComponent));
   if (filter) filter.SetCharacterLimit(m_iEUD_Limit);
  }
  m_EUD_EditBox.m_OnChanged.Insert(EUD_OnTextChanged);
  super.Init(w, attribute);
 }

 override void SetFromVar(SCR_BaseEditorAttributeVar var)
 {
  super.SetFromVar(var);
  if (var && m_EUD_EditBox) m_EUD_EditBox.SetValue(var.EUD_GetText());
 }

 // Mixed values across a multi-selection start empty.
 override protected void SetVariableToDefaultValue(SCR_BaseEditorAttributeVar var)
 {
  if (m_EUD_EditBox) m_EUD_EditBox.SetValue(string.Empty);
  if (var) var.EUD_SetText(string.Empty);
 }

 // Disabling a mixed-value row parks the variable in a copy that only keeps the
 // vector; carry the typed text across so re-enabling restores it.
 override protected void ToggleEnable(bool enabled)
 {
  SCR_BaseEditorAttribute attribute = GetAttribute();
  string kept;
  bool keep = !enabled && attribute && attribute.GetHasConflictingValues() && attribute.GetVariable(false);
  if (keep) kept = attribute.GetVariable(false).EUD_GetText();
  super.ToggleEnable(enabled);
  if (keep && attribute.GetCopyVariable()) attribute.GetCopyVariable().EUD_SetText(kept);
 }

 protected void EUD_OnTextChanged(SCR_EditBoxComponent box, string text)
 {
  OnChangeInternal(null, 0, 0, false);
 }

 override bool OnChangeInternal(Widget w, int x, int y, bool finished)
 {
  if (!m_EUD_EditBox) return false;
  SCR_BaseEditorAttribute attribute = GetAttribute();
  if (!attribute) return false;
  SCR_BaseEditorAttributeVar var = attribute.GetVariable(true);
  if (!var) return false;
  string text = m_EUD_EditBox.GetValue();
  // Reject an oversized edit whole; never cut inside a UTF-8 sequence.
  if (text.Length() > m_iEUD_Limit)
  {
   m_EUD_EditBox.SetValue(var.EUD_GetText());
   return false;
  }
  if (text == var.EUD_GetText()) return false;
  var.EUD_SetText(text);
  return super.OnChangeInternal(w, x, y, finished);
 }

 override void HandlerDeattached(Widget w)
 {
  if (m_EUD_EditBox) m_EUD_EditBox.m_OnChanged.Remove(EUD_OnTextChanged);
  super.HandlerDeattached(w);
 }
}

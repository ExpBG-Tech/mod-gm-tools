// GM attributes in the "EXPBG Unit Dialog" category of an AI character.
// Read on the server for the editing GM; written on the server only after the
// request is tied to that player's own unlimited Game Master editor.
// Not session-serializable: the numeric session format cannot hold text, and
// mission saves use EUD_DialogPersistenceSerializer instead.
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EUD_TextAttribute : SCR_BaseEditorAttribute
{
 override bool IsSerializable() { return false; }
 int GetLimit() { return EUD_Dialog.LINE_LIMIT; }
 protected string ReadText(SCR_EditableCharacterComponent unit) { return string.Empty; }
 protected bool WriteText(SCR_EditableCharacterComponent unit, string text) { return false; }

 override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
 {
  if (!manager) return null;
  SCR_EditableCharacterComponent unit = EUD_Dialog.FromEditable(item);
  if (!unit) return null;
  return SCR_BaseEditorAttributeVar.EUD_CreateText(ReadText(unit));
 }
 override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var, SCR_AttributesManagerEditorComponent manager, int playerID)
 {
  if (!var || !EUD_Dialog.IsGameMasterWrite(manager, playerID)) return;
  SCR_EditableCharacterComponent unit = EUD_Dialog.FromEditable(item);
  if (!unit) return;
  if (!WriteText(unit, var.EUD_GetText()))
   Print(string.Format("[EUD] Dialog text rejected: maximum %1 UTF-8 bytes", GetLimit()), LogLevel.WARNING);
 }
}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EUD_NameAttribute : EUD_TextAttribute
{
 override int GetLimit() { return EUD_Dialog.NAME_LIMIT; }
 override protected string ReadText(SCR_EditableCharacterComponent unit) { return unit.EUD_GetName(); }
 override protected bool WriteText(SCR_EditableCharacterComponent unit, string text) { return unit.EUD_SetName(text); }
}

// One class per slot: the attribute manager refuses two attributes of one type.
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EUD_LineAttribute : EUD_TextAttribute
{
 int GetLineIndex() { return 0; }
 override protected string ReadText(SCR_EditableCharacterComponent unit) { return unit.EUD_GetLine(GetLineIndex()); }
 override protected bool WriteText(SCR_EditableCharacterComponent unit, string text) { return unit.EUD_SetLine(GetLineIndex(), text); }
}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EUD_Line1Attribute : EUD_LineAttribute { override int GetLineIndex() { return 0; } }
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EUD_Line2Attribute : EUD_LineAttribute { override int GetLineIndex() { return 1; } }
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EUD_Line3Attribute : EUD_LineAttribute { override int GetLineIndex() { return 2; } }
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EUD_Line4Attribute : EUD_LineAttribute { override int GetLineIndex() { return 3; } }
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EUD_Line5Attribute : EUD_LineAttribute { override int GetLineIndex() { return 4; } }
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EUD_Line6Attribute : EUD_LineAttribute { override int GetLineIndex() { return 5; } }
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EUD_Line7Attribute : EUD_LineAttribute { override int GetLineIndex() { return 6; } }
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EUD_Line8Attribute : EUD_LineAttribute { override int GetLineIndex() { return 7; } }
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EUD_Line9Attribute : EUD_LineAttribute { override int GetLineIndex() { return 8; } }
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EUD_Line10Attribute : EUD_LineAttribute { override int GetLineIndex() { return 9; } }

// Spinbox of gesture choices; the value is the choice index (see EUD_Dialog.GestureId).
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EUD_GestureAttribute : SCR_BaseValueListEditorAttribute
{
 [Attribute()]
 protected ref array<ref SCR_EditorAttributeFloatStringValueHolder> m_aEUD_Choices;

 override bool IsSerializable() { return false; }
 override int GetEntries(notnull array<ref SCR_BaseEditorAttributeEntry> outEntries)
 {
  outEntries.Insert(new SCR_BaseEditorAttributeFloatStringValues(m_aEUD_Choices));
  return outEntries.Count();
 }
 override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
 {
  if (!manager) return null;
  SCR_EditableCharacterComponent unit = EUD_Dialog.FromEditable(item);
  if (!unit) return null;
  return SCR_BaseEditorAttributeVar.CreateFloat(unit.EUD_GetGesture());
 }
 override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var, SCR_AttributesManagerEditorComponent manager, int playerID)
 {
  if (!var || !EUD_Dialog.IsGameMasterWrite(manager, playerID)) return;
  SCR_EditableCharacterComponent unit = EUD_Dialog.FromEditable(item);
  if (!unit) return;
  int choice = Math.Round(var.GetFloat());
  if (!unit.EUD_SetGesture(choice))
   Print("[EUD] Talking gesture rejected", LogLevel.WARNING);
 }
}

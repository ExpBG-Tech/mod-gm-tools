// Text payload for EXPBG Unit Dialog GM attributes. Native editor attribute
// variables carry one vector only, so a text field cannot reach the server.
// This layer appends one string to every variable's snapshot and network form.
// Every method runs the previous layer first and then handles only its own
// string, so other modded string layers keep their fields and their order.
modded class SCR_BaseEditorAttributeVar
{
 protected string m_sEUD_Text;

 void EUD_SetText(string text) { m_sEUD_Text = text; }
 string EUD_GetText() { return m_sEUD_Text; }
 static SCR_BaseEditorAttributeVar EUD_CreateText(string text)
 {
  SCR_BaseEditorAttributeVar var = new SCR_BaseEditorAttributeVar();
  var.EUD_SetText(text);
  return var;
 }

 // Snapshot <- instance.
 override static bool Extract(SCR_BaseEditorAttributeVar prop, ScriptCtx hint, SSnapSerializerBase snapshot)
 {
  bool previous = super.Extract(prop, hint, snapshot);
  snapshot.SerializeString(prop.m_sEUD_Text);
  return previous;
 }
 // Instance <- snapshot.
 override static bool Inject(SSnapSerializerBase snapshot, ScriptCtx hint, SCR_BaseEditorAttributeVar prop)
 {
  bool previous = super.Inject(snapshot, hint, prop);
  snapshot.SerializeString(prop.m_sEUD_Text);
  return previous;
 }
 // Packet <- snapshot.
 override static void Encode(SSnapSerializerBase snapshot, ScriptCtx hint, ScriptBitSerializer packet)
 {
  super.Encode(snapshot, hint, packet);
  string text;
  snapshot.SerializeString(text);
  packet.SerializeString(text);
 }
 // Snapshot <- packet.
 override static bool Decode(ScriptBitSerializer packet, ScriptCtx hint, SSnapSerializerBase snapshot)
 {
  bool previous = super.Decode(packet, hint, snapshot);
  string text;
  packet.SerializeString(text);
  snapshot.SerializeString(text);
  return previous;
 }
 override static bool SnapCompare(SSnapSerializerBase lhs, SSnapSerializerBase rhs, ScriptCtx hint)
 {
  if (!super.SnapCompare(lhs, rhs, hint)) return false;
  return lhs.CompareStringSnapshots(rhs);
 }
 // The attribute manager compares against a fixed 96-byte snapshot. Text longer
 // than EUD_COMPARE_BYTES may not fit there, so it always counts as changed: an
 // unchanged long line is re-sent (harmless), an edited one is never skipped.
 static const int EUD_COMPARE_BYTES = 64;
 override static bool PropCompare(SCR_BaseEditorAttributeVar prop, SSnapSerializerBase snapshot, ScriptCtx hint)
 {
  if (!super.PropCompare(prop, snapshot, hint)) return false;
  if (prop.m_sEUD_Text.Length() > EUD_COMPARE_BYTES) return false;
  return snapshot.CompareString(prop.m_sEUD_Text);
 }
}

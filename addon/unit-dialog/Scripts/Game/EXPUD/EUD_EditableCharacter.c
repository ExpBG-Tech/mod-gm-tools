// Unit dialog state on the vanilla editable character component, which every
// GM-placeable character carries. RplProp replicates it to every client and to
// join-in-progress clients on stream-in. Only the server validates and writes.
// NonSerialized: native saves go through EUD_DialogPersistenceSerializer.
modded class SCR_EditableCharacterComponent
{
 [RplProp(), NonSerialized()]
 protected string m_sEUD_Name;
 [RplProp(), NonSerialized()]
 protected ref array<string> m_aEUD_Lines = {};
 [RplProp(), NonSerialized()]
 protected int m_iEUD_Gesture;

 // Server only, world-time seconds.
 protected float m_fEUD_GestureReady;
 protected float m_fEUD_GestureUntil;

 //------------------------------------------------------------------------------------------------
 string EUD_GetName() { return m_sEUD_Name; }
 int EUD_GetGesture() { return m_iEUD_Gesture; }
 string EUD_GetLine(int index)
 {
  if (index < 0 || index >= m_aEUD_Lines.Count()) return string.Empty;
  return m_aEUD_Lines[index];
 }
 // Non-blank lines in slot order; empty slots are skipped in conversation.
 int EUD_GetSpokenLines(notnull array<string> outLines)
 {
  outLines.Clear();
  foreach (string line : m_aEUD_Lines)
  {
   if (!EUD_Dialog.IsBlank(line)) outLines.Insert(line);
  }
  return outLines.Count();
 }
 bool EUD_HasDialog()
 {
  foreach (string line : m_aEUD_Lines)
  {
   if (!EUD_Dialog.IsBlank(line)) return true;
  }
  return false;
 }
 bool EUD_IsConfigured()
 {
  return !m_sEUD_Name.IsEmpty() || !m_aEUD_Lines.IsEmpty() || m_iEUD_Gesture != 0;
 }

 //------------------------------------------------------------------------------------------------
 // GM name first; otherwise the character's own vanilla identity name; then the
 // editable entity name. Resolved where it is shown (client), never replicated.
 string EUD_GetDisplayName()
 {
  string custom = m_sEUD_Name.Trim();
  if (!custom.IsEmpty()) return custom;
  IEntity owner = GetOwner();
  SCR_CharacterIdentityComponent identity;
  if (owner) identity = SCR_CharacterIdentityComponent.Cast(owner.FindComponent(SCR_CharacterIdentityComponent));
  if (identity && identity.GetIdentity())
  {
   string format, first, alias, surname;
   identity.GetFormattedFullName(format, first, alias, surname);
   if (!format.IsEmpty())
   {
    string full = WidgetManager.Translate(format, WidgetManager.Translate(first), WidgetManager.Translate(alias), WidgetManager.Translate(surname));
    full = full.Trim();
    if (!full.IsEmpty()) return full;
   }
  }
  SCR_UIInfo info = GetInfo();
  if (info)
  {
   string label = WidgetManager.Translate(info.GetName());
   label = label.Trim();
   if (!label.IsEmpty()) return label;
  }
  return "Unknown";
 }

 //------------------------------------------------------------------------------------------------
 // Alive (not incapacitated), still a character, and not a player or a possessed body.
 bool EUD_IsAvailableSpeaker()
 {
  ChimeraCharacter character = ChimeraCharacter.Cast(GetOwner());
  if (!character) return false;
  CharacterControllerComponent controller = character.GetCharacterController();
  if (!controller || controller.GetLifeState() != ECharacterLifeState.ALIVE) return false;
  return !EUD_Dialog.IsPlayer(this);
 }
 bool EUD_CanTalkWith(IEntity user, float range)
 {
  IEntity owner = GetOwner();
  if (!user || !owner || user == owner || !EUD_HasDialog()) return false;
  if (vector.DistanceSq(user.GetOrigin(), owner.GetOrigin()) > range * range) return false;
  return EUD_IsAvailableSpeaker();
 }

 //------------------------------------------------------------------------------------------------
 // Server writes. Each builds a full candidate, validates it, then commits once.
 bool EUD_SetName(string name)
 {
  return EUD_RestoreState(name, m_aEUD_Lines, m_iEUD_Gesture);
 }
 bool EUD_SetLine(int index, string text)
 {
  if (index < 0 || index >= EUD_Dialog.MAX_LINES) return false;
  array<string> lines = {};
  lines.Copy(m_aEUD_Lines);
  while (lines.Count() < EUD_Dialog.MAX_LINES) lines.Insert(string.Empty);
  lines[index] = text;
  return EUD_RestoreState(m_sEUD_Name, lines, m_iEUD_Gesture);
 }
 bool EUD_SetGesture(int choice)
 {
  return EUD_RestoreState(m_sEUD_Name, m_aEUD_Lines, choice);
 }
 bool EUD_Clear()
 {
  array<string> none = {};
  return EUD_RestoreState(string.Empty, none, 0);
 }
 // Also the restore entry point for native saves and a CDF adapter.
 bool EUD_RestoreState(string name, array<string> lines, int gesture)
 {
  if (!Replication.IsServer() || !EUD_Dialog.ValidState(name, lines, gesture)) return false;
  array<string> normalized = {};
  normalized.Copy(lines);
  EUD_Dialog.NormalizeLines(normalized);
  bool configured = !name.IsEmpty() || !normalized.IsEmpty() || gesture != 0;
  if (configured && !EUD_Dialog.HasRoom(this))
  {
   Print(string.Format("[EUD] Dialog edit rejected: at most %1 units per mission may carry dialog", EUD_Dialog.MAX_UNITS), LogLevel.WARNING);
   return false;
  }
  m_sEUD_Name = name;
  m_aEUD_Lines.Copy(normalized);
  m_iEUD_Gesture = gesture;
  Replication.BumpMe();
  EUD_Dialog.Track(this, configured);
  return true;
 }
 // Copy for saves; id is filled by the caller.
 EUD_DialogRecord EUD_Capture()
 {
  EUD_DialogRecord record = new EUD_DialogRecord();
  record.name = m_sEUD_Name;
  record.lines.Copy(m_aEUD_Lines);
  record.gesture = m_iEUD_Gesture;
  return record;
 }

 //------------------------------------------------------------------------------------------------
 // Server: short vanilla character gesture, rate limited per unit. Gestures are
 // replicated by the character controller; nothing here is sent to clients.
 void EUD_PlayGesture()
 {
  if (!Replication.IsServer() || !GetGame() || !GetGame().GetWorld()) return;
  int gesture = EUD_Dialog.GestureId(m_iEUD_Gesture);
  if (gesture <= 0 || !EUD_IsAvailableSpeaker()) return;
  float now = GetGame().GetWorld().GetWorldTime() * 0.001;
  if (now < m_fEUD_GestureReady) return;
  ChimeraCharacter character = ChimeraCharacter.Cast(GetOwner());
  CharacterControllerComponent controller = character.GetCharacterController();
  if (!controller || controller.IsPlayingGesture()) return;
  m_fEUD_GestureReady = now + EUD_Dialog.GESTURE_GAP;
  if (controller.TryStartCharacterGesture(gesture, EUD_Dialog.GESTURE_MS))
   m_fEUD_GestureUntil = now + EUD_Dialog.GESTURE_MS * 0.001;
 }
 // Server: stop only a gesture this module started and that is still running.
 void EUD_StopGesture()
 {
  if (!Replication.IsServer() || !GetGame() || !GetGame().GetWorld()) return;
  float now = GetGame().GetWorld().GetWorldTime() * 0.001;
  if (now >= m_fEUD_GestureUntil) return;
  m_fEUD_GestureUntil = 0;
  ChimeraCharacter character = ChimeraCharacter.Cast(GetOwner());
  if (!character) return;
  CharacterControllerComponent controller = character.GetCharacterController();
  if (controller && controller.IsPlayingGesture()) controller.StopCharacterGesture();
 }

 //------------------------------------------------------------------------------------------------
 // Only configured units are registered, so ordinary deletions cost nothing here.
 override void OnDelete(IEntity owner)
 {
  if (Replication.IsServer() && EUD_IsConfigured()) EUD_Dialog.Track(this, false);
  super.OnDelete(owner);
 }
}

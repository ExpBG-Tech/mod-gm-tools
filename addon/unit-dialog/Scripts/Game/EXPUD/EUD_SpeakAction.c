// "Speak to <Name>" on a character whose Game Master gave it dialog lines.
// Listed in the Character_Base ActionsManagerComponent (Head and Chest contexts).
// The speaking player's machine opens the conversation window locally; the
// server plays the optional talking gesture. Nothing else is replicated.
class EUD_SpeakAction : ScriptedUserAction
{
 // Zero-duration actions re-perform while the key is held, and the broadcast
 // echoes to the performer; accept one perform per player per second per machine.
 static const int REPEAT_MS = 1000;
 protected static ref map<int, int> s_mEUD_LastPerform = new map<int, int>();

 protected SCR_EditableCharacterComponent m_EUD_Unit;

 override void Init(IEntity pOwnerEntity, GenericComponent pManagerComponent)
 {
  m_EUD_Unit = EUD_Dialog.Find(pOwnerEntity);
 }
 protected SCR_EditableCharacterComponent Unit()
 {
  if (!m_EUD_Unit) m_EUD_Unit = EUD_Dialog.Find(GetOwner());
  return m_EUD_Unit;
 }

 override bool CanBeShownScript(IEntity user)
 {
  SCR_EditableCharacterComponent unit = Unit();
  return unit && user && user != GetOwner() && unit.EUD_HasDialog() && unit.EUD_IsAvailableSpeaker();
 }
 override bool CanBePerformedScript(IEntity user)
 {
  SCR_EditableCharacterComponent unit = Unit();
  return unit && unit.EUD_CanTalkWith(user, EUD_Dialog.TALK_RANGE) && !EUD_DialogWindow.IsOpen();
 }
 override bool GetActionNameScript(out string outName)
 {
  SCR_EditableCharacterComponent unit = Unit();
  if (!unit) return false;
  outName = "Speak to " + unit.EUD_GetDisplayName();
  return true;
 }
 override bool HasLocalEffectOnlyScript() { return false; }
 override bool CanBroadcastScript() { return true; }

 protected static bool Repeated(IEntity user)
 {
  int player = GetGame().GetPlayerManager().GetPlayerIdFromControlledEntity(user);
  int now = System.GetTickCount();
  int last;
  bool repeated = s_mEUD_LastPerform.Find(player, last) && now - last >= 0 && now - last < REPEAT_MS;
  if (!s_mEUD_LastPerform.Contains(player) && s_mEUD_LastPerform.Count() >= 256) s_mEUD_LastPerform.Clear();
  s_mEUD_LastPerform.Set(player, now);
  return repeated;
 }

 override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
 {
  if (!pOwnerEntity || !pUserEntity) return;
  SCR_EditableCharacterComponent unit = EUD_Dialog.Find(pOwnerEntity);
  // KEEP_RANGE tolerates movement between the client check and the server perform.
  if (!unit || !unit.EUD_CanTalkWith(pUserEntity, EUD_Dialog.KEEP_RANGE)) return;
  PlayerController controller = GetGame().GetPlayerController();
  bool speaker = controller && controller.GetControlledEntity() == pUserEntity;
  bool authority = Replication.IsServer();
  // Other clients receive the broadcast too; only the speaking player reads.
  if (!speaker && !authority) return;
  if (Repeated(pUserEntity)) return;
  if (authority) unit.EUD_PlayGesture();
  if (speaker) EUD_DialogWindow.Open(pOwnerEntity, unit);
 }
}

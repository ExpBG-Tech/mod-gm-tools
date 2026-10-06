// Conversation-end requests from the conversation window. A client may only send
// server RPCs through an item it owns; its own player controller is that item. The
// server re-validates the speaker, the distance and the dialog, and rate limits
// each controller. A request can only stop the talking gesture; the gesture starts
// once per conversation, on the server, from EUD_SpeakAction.
modded class SCR_PlayerController
{
 protected float m_fEUD_NextTalk;

 void EUD_RequestEnd(RplId speaker)
 {
  if (Replication.IsServer())
  {
   EUD_HandleEnd(speaker);
   return;
  }
  Rpc(RPC_EUD_End, speaker);
 }

 [RplRpc(RplChannel.Reliable, RplRcver.Server)]
 protected void RPC_EUD_End(RplId speaker)
 {
  EUD_HandleEnd(speaker);
 }

 protected void EUD_HandleEnd(RplId speaker)
 {
  if (!Replication.IsServer() || !GetGame() || !GetGame().GetWorld()) return;
  float now = GetGame().GetWorld().GetWorldTime() * 0.001;
  if (now < m_fEUD_NextTalk) return;
  m_fEUD_NextTalk = now + EUD_Dialog.REQUEST_GAP;
  RplComponent rpl = RplComponent.Cast(Replication.FindItem(speaker));
  if (!rpl) return;
  SCR_EditableCharacterComponent unit = EUD_Dialog.Find(rpl.GetEntity());
  if (!unit || !unit.EUD_CanTalkWith(GetControlledEntity(), EUD_Dialog.KEEP_RANGE)) return;
  unit.EUD_StopGesture();
 }
}

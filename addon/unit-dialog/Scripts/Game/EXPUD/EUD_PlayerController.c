// Talk requests from the conversation window. A client may only send server RPCs
// through an item it owns; its own player controller is that item. The server
// re-validates the speaker, the distance and the dialog before any gesture, and
// rate limits each controller. A request only plays or stops a short gesture.
modded class SCR_PlayerController
{
 protected float m_fEUD_NextTalk;

 void EUD_RequestTalk(RplId speaker, bool ended)
 {
  if (Replication.IsServer())
  {
   EUD_HandleTalk(speaker, ended);
   return;
  }
  Rpc(RPC_EUD_Talk, speaker, ended);
 }

 [RplRpc(RplChannel.Reliable, RplRcver.Server)]
 protected void RPC_EUD_Talk(RplId speaker, bool ended)
 {
  EUD_HandleTalk(speaker, ended);
 }

 protected void EUD_HandleTalk(RplId speaker, bool ended)
 {
  if (!Replication.IsServer() || !GetGame() || !GetGame().GetWorld()) return;
  float now = GetGame().GetWorld().GetWorldTime() * 0.001;
  if (now < m_fEUD_NextTalk) return;
  m_fEUD_NextTalk = now + EUD_Dialog.REQUEST_GAP;
  RplComponent rpl = RplComponent.Cast(Replication.FindItem(speaker));
  if (!rpl) return;
  SCR_EditableCharacterComponent unit = EUD_Dialog.Find(rpl.GetEntity());
  if (!unit || !unit.EUD_CanTalkWith(GetControlledEntity(), EUD_Dialog.KEEP_RANGE)) return;
  if (ended) unit.EUD_StopGesture();
  else unit.EUD_PlayGesture();
 }
}

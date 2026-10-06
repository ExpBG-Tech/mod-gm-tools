// What a cache zone does, told once to the Game Master who saved its settings or
// used a global zone switch. The server composes the line (EBG_CacheManager.
// ZoneNoticeText) after the zone's next enrollment pass and routes it through that
// GM's own editor manager; the owner shows a hint and the same line in chat
// history (profiles can turn hints off). Listen server and single player: the host
// owns its editor and shows it at once. Feedback only; no cache decision reads it.
class EBG_ZoneFeedback
{
 static void Show(string message)
 {
  SCR_HintManagerComponent.ShowCustomHint(message, "EXPBG Unit Caching", 12);
  SCR_ChatPanelManager chat = SCR_ChatPanelManager.GetInstance();
  if (chat) chat.ShowHelpMessage("EXPBG Unit Caching: " + message);
 }

 // Server only.
 static void Send(int playerId, string message)
 {
  if (!Replication.IsServer() || playerId <= 0 || message.IsEmpty()) return;
  PrintFormat("[EBG ZONE NOTICE] player=%1 %2", playerId, message);
  SCR_EditorManagerCore core = SCR_EditorManagerCore.Cast(SCR_EditorManagerCore.GetInstance(SCR_EditorManagerCore));
  if (!core) return;
  SCR_EditorManagerEntity editor = core.GetEditorManager(playerId);
  if (editor) editor.EBG_ZoneNotify(message);
 }
}

modded class SCR_EditorManagerEntity
{
 void EBG_ZoneNotify(string message)
 {
  if (Replication.IsClient()) return;
  if (IsOwner())
  {
   EBG_ZoneFeedback.Show(message);
   return;
  }
  Rpc(EBG_ZoneNotifyOwner, message);
 }

 [RplRpc(RplChannel.Reliable, RplRcver.Owner)]
 protected void EBG_ZoneNotifyOwner(string message)
 {
  EBG_ZoneFeedback.Show(message);
 }
}

// Replicated read-only state for context-menu visibility and attribute display.
// The server is the only writer; RplProps reach join-in-progress clients with
// the entity snapshot. Mission-only: never serialized into saves.
modded class SCR_ChimeraCharacter
{
 // EUS_Codes unit script code (0 = normal AI).
 [RplProp(), NonSerialized()] int EUS_Script;

 void EUS_SetScript(int code)
 {
  if (Replication.IsClient() || EUS_Script == code) return;
  EUS_Script = code;
  Replication.BumpMe();
 }
}

modded class SCR_AIGroup
{
 // EUS_Codes discipline mode (0 = off).
 [RplProp(), NonSerialized()] int EUS_Discipline;
 // Number of members currently running a unit script.
 [RplProp(), NonSerialized()] int EUS_Scripted;

 void EUS_SetDiscipline(int mode)
 {
  if (Replication.IsClient() || EUS_Discipline == mode) return;
  EUS_Discipline = mode;
  Replication.BumpMe();
 }

 void EUS_SetScripted(int count)
 {
  if (Replication.IsClient() || EUS_Scripted == count) return;
  EUS_Scripted = count;
  Replication.BumpMe();
 }
}

// Local feedback for the acting Game Master only. Profiles can disable hints, so
// every line also enters the local chat history (vanilla system-message style).
class EUS_Feedback
{
 static void Show(string message)
 {
  Print("[EUS] " + message);
  SCR_HintManagerComponent.ShowCustomHint(message, "EXPBG Unit Scripts", 8);
  SCR_ChatPanelManager chat = SCR_ChatPanelManager.GetInstance();
  if (chat) chat.ShowHelpMessage("EXPBG Unit Scripts: " + message);
 }

 // Server: route to the Game Master's own editor manager.
 static void Send(int playerId, string message)
 {
  PrintFormat("[EUS] reply to player %1: %2", playerId, message);
  if (playerId <= 0) return;
  SCR_EditorManagerCore core = SCR_EditorManagerCore.Cast(SCR_EditorManagerCore.GetInstance(SCR_EditorManagerCore));
  if (!core) return;
  SCR_EditorManagerEntity editor = core.GetEditorManager(playerId);
  if (editor) editor.EUS_Notify(message);
 }
}

modded class SCR_EditorManagerEntity
{
 void EUS_Notify(string message)
 {
  if (Replication.IsClient()) return;
  // Listen server or single player: the host owns its own editor.
  if (IsOwner())
  {
   EUS_Feedback.Show(message);
   return;
  }
  Rpc(EUS_NotifyOwner, message);
 }

 [RplRpc(RplChannel.Reliable, RplRcver.Owner)]
 protected void EUS_NotifyOwner(string message)
 {
  EUS_Feedback.Show(message);
 }
}

// Server-side Game Master authorization shared by attributes and context actions.
class EUS_Authority
{
 // Empty when the player holds an open, unlimited editor in Edit mode.
 static string Failure(int playerId)
 {
  SCR_EditorManagerCore core = SCR_EditorManagerCore.Cast(SCR_EditorManagerCore.GetInstance(SCR_EditorManagerCore));
  if (!core) return "the editor core is unavailable";
  SCR_EditorManagerEntity editor = core.GetEditorManager(playerId);
  if (!editor) return string.Format("player %1 has no Game Master editor", playerId);
  if (editor.IsLimited()) return "the editor is limited; full Game Master rights are required";
  if (!editor.IsOpened()) return "the editor is closed";
  if (!editor.HasMode(EEditorMode.EDIT) || editor.GetCurrentMode() != EEditorMode.EDIT) return "the editor is not in Edit mode";
  return string.Empty;
 }

 static string AttributeFailure(SCR_AttributesManagerEditorComponent attributes, int playerId)
 {
  if (!attributes) return "the attribute manager is unavailable";
  SCR_EditorManagerEntity editor = attributes.GetManager();
  if (!editor || editor.GetPlayerID() != playerId) return "the attribute request does not come from this editor's owner";
  return Failure(playerId);
 }
}

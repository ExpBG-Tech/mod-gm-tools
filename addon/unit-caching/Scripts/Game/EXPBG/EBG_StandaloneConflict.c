// EXPBG GM Tools: the retired standalone EXPBG mods must not run next to this pack.
// The pack already contains them; loading both duplicates classes, prefabs, saved
// keys and editor entries. One native loaded-addon read per game session: no
// timer, entity scan or replication. Every machine loads the server's addon set,
// so each checks its own list. The server and every client log one error line at
// game start; each Game Master gets a persistent hint plus a chat line the first
// time the full editor opens. If duplicate scripts already stop the Game module
// from compiling, the engine's compile error is shown instead and none of this runs.
class EBG_StandaloneConflict
{
 // Workshop GUIDs (gproj guid) of the standalone items and their CDF companions.
 protected static const ref array<string> IDS = {
  "F3B7C6FB18AB1F79", "8C5A6D9E73B241F0", "E110000000000001", "E110000000000002",
  "A9C45E82D6710B3F", "A93E9F6271894A3C", "E2A47D19C8B6503F", "D7A82F4139C60BE5",
  "6A32DB878B264D05"
 };
 protected static const ref array<string> NAMES = {
  "EXPBG GM Optimizer", "EXPBG GM Optimizer CDF", "EXPBG Intel Items", "EXPBG Intel Items CDF",
  "EXPBG Ambient Civilians", "EXPBG Ambient Sounds", "EXPBG Ambient Destruction", "EXPBG Ambient Destruction CDF",
  "EXPBG Persistent Battlefield"
 };
 // Weak game-mode references: world teardown deletes the game mode and clears
 // them, so the next mission reads, logs and warns again.
 protected static SCR_BaseGameMode s_CheckedMode;
 protected static SCR_BaseGameMode s_LoggedMode;
 protected static SCR_BaseGameMode s_WarnedMode;
 protected static string s_Names;
 protected static string s_Detail;
 // An old CDF companion is replaced by EXPBG CDF Compat, not by this pack.
 protected static bool s_CDF;

 // Comma-separated titles of the loaded standalone items; empty when none.
 // detail adds each Workshop GUID for the log.
 static string Loaded(SCR_BaseGameMode mode, bool detail = false)
 {
  if (!mode) return string.Empty;
  if (mode != s_CheckedMode)
  {
   s_CheckedMode = mode;
   s_Names = string.Empty;
   s_Detail = string.Empty;
   s_CDF = false;
   array<string> loaded = {};
   GameProject.GetLoadedAddons(loaded);
   foreach (string guid : loaded)
   {
    string id = guid;
    id.ToUpper();
    int index = IDS.Find(id);
    if (index < 0) continue;
    if (!s_Names.IsEmpty())
    {
     s_Names += ", ";
     s_Detail += ", ";
    }
    s_Names += NAMES[index];
    s_Detail += NAMES[index] + " (" + id + ")";
    if (NAMES[index].EndsWith(" CDF")) s_CDF = true;
   }
  }
  if (detail) return s_Detail;
  return s_Names;
 }

 // Game start on every machine (also a Game Master client's first editor open):
 // one error line per session when any standalone item is loaded.
 static void LogOnce(SCR_BaseGameMode mode)
 {
  if (!mode || mode == s_LoggedMode) return;
  string found = Loaded(mode, true);
  if (found.IsEmpty()) return;
  s_LoggedMode = mode;
  Print("[EXPBG GM TOOLS] Conflicting standalone mods loaded: " + found, LogLevel.ERROR);
 }

 // Local full editor opened. Delayed so the editor HUD exists when the hint arrives.
 static void ScheduleWarning(SCR_EditorManagerEntity editor)
 {
  if (!editor || editor != SCR_EditorManagerEntity.GetInstance() || editor.IsLimited()) return;
  SCR_BaseGameMode mode = SCR_BaseGameMode.Cast(GetGame().GetGameMode());
  if (!mode || mode == s_WarnedMode || Loaded(mode).IsEmpty()) return;
  GetGame().GetCallqueue().Remove(EBG_StandaloneConflict.WarnGameMaster);
  GetGame().GetCallqueue().CallLater(EBG_StandaloneConflict.WarnGameMaster, 2500, false);
 }

 // Once per session for this Game Master: an endless hint (profiles can turn hints
 // off) and the same text in chat history. A limited editor retries on next open.
 static void WarnGameMaster()
 {
  if (!GetGame()) return;
  SCR_EditorManagerEntity editor = SCR_EditorManagerEntity.GetInstance();
  if (!editor || editor.IsLimited()) return;
  SCR_BaseGameMode mode = SCR_BaseGameMode.Cast(GetGame().GetGameMode());
  if (!mode || mode == s_WarnedMode) return;
  string found = Loaded(mode);
  if (found.IsEmpty()) return;
  LogOnce(mode);
  string message = "Disable the old standalone EXPBG mods: " + found + " - GM Tools already contains them.";
  if (s_CDF) message += " Use EXPBG CDF Compat in place of the old CDF companions.";
  bool hinted = SCR_HintManagerComponent.ShowCustomHint(message, "EXPBG GM Tools: conflicting mods", -1);
  SCR_ChatPanelManager chat = SCR_ChatPanelManager.GetInstance();
  if (chat) chat.ShowHelpMessage("EXPBG GM Tools: " + message);
  if (hinted || chat != null) s_WarnedMode = mode;
 }
}

modded class SCR_BaseGameMode
{
 // Called on every machine when the game mode enters GAME.
 override protected void OnGameModeStart()
 {
  super.OnGameModeStart();
  EBG_StandaloneConflict.LogOnce(this);
 }
}

modded class SCR_EditorManagerEntity
{
 override protected void StartEvents(EEditorEventOperation type = EEditorEventOperation.NONE)
 {
  super.StartEvents(type);
  if (type == EEditorEventOperation.OPEN) EBG_StandaloneConflict.ScheduleWarning(this);
 }
}

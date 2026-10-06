// EXPBG Unit Dialog: a Game Master gives an AI character a speaker name and up to
// ten dialog lines; players read them through the "Speak to <Name>" action.
// Limits, validation, the server-side registry of configured units and the
// client request path live here. Unit state lives on the vanilla editable
// character component (EUD_EditableCharacter.c) so no prefab carries it.
class EUD_Dialog
{
 static const int MAX_LINES = 10;
 // UTF-8 bytes; oversized edits are rejected whole, never truncated.
 static const int NAME_LIMIT = 64;
 static const int LINE_LIMIT = 512;
 // Configured units per mission; bounds replication, save work and memory.
 static const int MAX_UNITS = 256;
 // Metres. The action needs TALK_RANGE; an open conversation survives to KEEP_RANGE.
 static const float TALK_RANGE = 3;
 static const float KEEP_RANGE = 6;
 // Gesture choices are indices into GestureId; 0 is off.
 static const int GESTURE_CHOICES = 5;
 static const int GESTURE_MS = 2000;
 // Seconds between gestures of one unit, and between end requests of one player.
 static const float GESTURE_GAP = 1.5;
 static const float REQUEST_GAP = 0.5;

 protected static ref array<SCR_EditableCharacterComponent> s_aUnits;
 protected static World s_World;

 //------------------------------------------------------------------------------------------------
 static bool ValidName(string name) { return name.Length() <= NAME_LIMIT; }
 static bool ValidLine(string line) { return line.Length() <= LINE_LIMIT; }
 static bool ValidGesture(int choice) { return choice >= 0 && choice < GESTURE_CHOICES; }
 static bool ValidState(string name, array<string> lines, int gesture)
 {
  if (!lines || lines.Count() > MAX_LINES || !ValidName(name) || !ValidGesture(gesture)) return false;
  foreach (string line : lines)
   if (!ValidLine(line)) return false;
  return true;
 }
 static bool IsBlank(string text)
 {
  string trimmed = text.Trim();
  return trimmed.IsEmpty();
 }
 // Lines are stored either empty (no dialog) or as exactly MAX_LINES slots, so
 // attribute "Line N" keeps its slot across edits and saves.
 static void NormalizeLines(notnull array<string> lines)
 {
  bool any;
  foreach (string line : lines)
  {
   if (!line.IsEmpty()) { any = true; break; }
  }
  if (!any) { lines.Clear(); return; }
  while (lines.Count() < MAX_LINES) lines.Insert(string.Empty);
 }
 // The GM text box cannot enter a line break; a typed [br] (or \n) marks one.
 // Display only: the stored text keeps the typed marker.
 static string DisplayText(string text)
 {
  string display = text;
  display.Replace("[br]", "\n");
  display.Replace("\\n", "\n");
  return display;
 }
 // Choice index -> vanilla ECharacterGestures id.
 static int GestureId(int choice)
 {
  switch (choice)
  {
   case 1: return ECharacterGestures.COMMAND_STOP;
   case 2: return ECharacterGestures.POINT_WITH_FINGER;
   case 3: return ECharacterGestures.COMMAND_FOLLOW;
   case 4: return ECharacterGestures.SALUTE;
  }
  return 0;
 }

 //------------------------------------------------------------------------------------------------
 static SCR_EditableCharacterComponent Find(IEntity entity)
 {
  if (!entity) return null;
  return SCR_EditableCharacterComponent.Cast(entity.FindComponent(SCR_EditableCharacterComponent));
 }
 static bool IsPlayer(SCR_EditableCharacterComponent unit)
 {
  if (!unit || !unit.GetOwner()) return false;
  if (unit.GetPlayerID() > 0) return true;
  return GetGame() && GetGame().GetPlayerManager() && GetGame().GetPlayerManager().GetPlayerIdFromControlledEntity(unit.GetOwner()) > 0;
 }
 // Attribute target: an editable character that is not a player.
 static SCR_EditableCharacterComponent FromEditable(Managed item)
 {
  SCR_EditableCharacterComponent unit = SCR_EditableCharacterComponent.Cast(item);
  if (!unit || !ChimeraCharacter.Cast(unit.GetOwner()) || IsPlayer(unit)) return null;
  return unit;
 }
 // Server: only the requesting player's own, unlimited Game Master editor in
 // Edit mode may change dialog. Session-save replays (null manager) never write.
 static bool IsGameMasterWrite(SCR_AttributesManagerEditorComponent manager, int playerID)
 {
  if (!Replication.IsServer() || !manager) return false;
  SCR_EditorManagerEntity editor = manager.GetManager();
  if (!editor || editor.GetPlayerID() != playerID || editor.IsLimited() || !editor.IsOpened()) return false;
  if (!editor.HasMode(EEditorMode.EDIT) || editor.GetCurrentMode() != EEditorMode.EDIT) return false;
  SCR_EditorManagerCore core = SCR_EditorManagerCore.Cast(SCR_EditorManagerCore.GetInstance(SCR_EditorManagerCore));
  return core && core.GetEditorManager(playerID) == editor;
 }

 //------------------------------------------------------------------------------------------------
 // Server registry of configured units; weak references, pruned on every use.
 protected static void EnsureRegistry()
 {
  World world;
  if (GetGame()) world = GetGame().GetWorld();
  if (!s_aUnits || s_World != world)
  {
   s_aUnits = {};
   s_World = world;
  }
  for (int i = s_aUnits.Count() - 1; i >= 0; i--)
  {
   if (!s_aUnits[i]) s_aUnits.Remove(i);
  }
 }
 static bool HasRoom(SCR_EditableCharacterComponent unit)
 {
  EnsureRegistry();
  return s_aUnits.Contains(unit) || s_aUnits.Count() < MAX_UNITS;
 }
 static void Track(SCR_EditableCharacterComponent unit, bool configured)
 {
  if (!unit) return;
  EnsureRegistry();
  int index = s_aUnits.Find(unit);
  if (configured && index < 0) s_aUnits.Insert(unit);
  else if (!configured && index >= 0) s_aUnits.Remove(index);
 }
 static int GetConfigured(notnull array<SCR_EditableCharacterComponent> outUnits)
 {
  EnsureRegistry();
  outUnits.Copy(s_aUnits);
  return outUnits.Count();
 }

 //------------------------------------------------------------------------------------------------
 // Client: the conversation ended; ask the server to stop the speaker's talking
 // gesture if it still runs. The gesture starts once per conversation, on the
 // server, from EUD_SpeakAction; no request can start one. The request travels
 // through the local player controller, which this client owns.
 static void RequestEnd(RplId speaker)
 {
  if (!speaker.IsValid() || !GetGame()) return;
  SCR_PlayerController controller = SCR_PlayerController.Cast(GetGame().GetPlayerController());
  if (controller) controller.EUD_RequestEnd(speaker);
 }
}

// Plain data row for saves and the CDF adapter; also the WhenAvailable context.
class EUD_DialogRecord
{
 UUID id = UUID.NULL_UUID;
 string name;
 ref array<string> lines = {};
 int gesture;

 bool Valid() { return EUD_Dialog.ValidState(name, lines, gesture); }
 bool Write(SaveContext context)
 {
  return context.WriteValue("id", id) && context.WriteValue("name", name) && context.WriteValue("lines", lines) && context.WriteValue("gesture", gesture);
 }
 bool Read(LoadContext context)
 {
  lines = {};
  return context.ReadValue("id", id) && context.ReadValue("name", name) && context.ReadValue("lines", lines) && context.ReadValue("gesture", gesture);
 }
}

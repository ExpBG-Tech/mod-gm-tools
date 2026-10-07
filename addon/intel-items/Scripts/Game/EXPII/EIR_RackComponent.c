// Why a download ended or was refused; sent only to the downloading player.
enum EIR_Result
{
 NONE,
 COMPLETE,
 OVERWRITTEN,
 CANCELLED,
 OUT_OF_RANGE,
 INTERRUPTED,
 NO_DRIVE,
 NO_DATA,
 BUSY,
 ALREADY
}

[ComponentEditorProps(category: "EXPBG/Intel", description: "Static server rack: GM-authored intel that players download onto a USB drive")]
class EIR_RackComponentClass : ScriptComponentClass {}

// Server-authoritative download sessions. One download per rack AND one per player:
// a second player gets "in use", and a player cannot run two racks at once (both
// would race for the same drive). The authored text stays on the server; clients
// only receive the downloader id, progress and duration, so a JIP client sees a
// busy rack without ever receiving undownloaded intel.
class EIR_RackComponent : ScriptComponent
{
 static const int TITLE_LIMIT = 128;
 static const int CONTENT_LIMIT = 4096;
 static const int MIN_SECONDS = 5;
 static const int MAX_SECONDS = 900;
 static const float RANGE = 3;
 static const float RANGE_SQ = 9;
 static const int TICK_MS = 250;
 static const int LOG_LIMIT = 64;
 // Server only: player id -> rack entity running that player's download. Entities are
 // weak references, so a rack deleted without cleanup can never block its player.
 protected static ref map<int, IEntity> s_mSessions;

 [Attribute("", UIWidgets.EditBox, "Rack intel title (up to 128 UTF-8 bytes); kept on the server")]
 protected string m_sTitle;
 [Attribute("", UIWidgets.EditBox, "Rack intel text (up to 4096 UTF-8 bytes); kept on the server")]
 protected string m_sContent;
 [Attribute("30", UIWidgets.Slider, "Download time in seconds", "5 900 5"), RplProp()]
 protected int m_iSeconds;
 [Attribute("0", UIWidgets.CheckBox, "Bounded server diagnostics; never logs intel text")]
 protected bool m_bDebug;

 [RplProp(onRplName: "OnStateRpl")]
 protected int m_iDownloader;
 [RplProp(onRplName: "OnStateRpl")]
 protected int m_iPermille;
 [RplProp(onRplName: "OnStateRpl")]
 protected int m_iSessionSeconds;

 protected IEntity m_User;
 protected float m_fStartMs;
 protected int m_iLogCount;

 bool IsAuthority()
 {
  RplComponent rpl = RplComponent.Cast(GetOwner().FindComponent(RplComponent));
  return !rpl || !rpl.IsProxy();
 }
 string GetTitle() { return m_sTitle; }
 string GetContent() { return m_sContent; }
 int GetSeconds() { return m_iSeconds; }
 bool GetDebug() { return m_bDebug; }
 int GetDownloader() { return m_iDownloader; }
 int GetPermille() { return m_iPermille; }
 static bool ValidText(string title, string content)
 {
  return title.Length() <= TITLE_LIMIT && content.Length() <= CONTENT_LIMIT;
 }
 static EIR_RackComponent FromItem(Managed item)
 {
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(item);
  if (!editable || !editable.GetOwner()) return null;
  return EIR_RackComponent.Cast(editable.GetOwner().FindComponent(EIR_RackComponent));
 }
 // Server writes: the GM who owns this attribute manager in edit mode, or a
 // native/CDF session restore (no manager, player -1).
 static bool EditAllowed(SCR_AttributesManagerEditorComponent manager, int playerID)
 {
  if (!Replication.IsServer()) return false;
  if (!manager) return playerID == -1;
  SCR_EditorManagerEntity editor = manager.GetManager();
  return editor && editor.GetPlayerID() == playerID && !editor.IsLimited() && editor.GetCurrentMode() == EEditorMode.EDIT;
 }
 static bool Alive(IEntity user)
 {
  ChimeraCharacter character = ChimeraCharacter.Cast(user);
  if (!character) return false;
  CharacterControllerComponent controller = character.GetCharacterController();
  return controller && controller.GetLifeState() == ECharacterLifeState.ALIVE;
 }
 static string ResultText(int result)
 {
  switch (result)
  {
   case EIR_Result.COMPLETE: return "Intel saved to your USB drive.";
   case EIR_Result.OVERWRITTEN: return "Intel saved to your USB drive. Its previous data was overwritten.";
   case EIR_Result.CANCELLED: return "Download cancelled.";
   case EIR_Result.OUT_OF_RANGE: return "Download aborted: you moved more than 3 m from the rack.";
   case EIR_Result.INTERRUPTED: return "Download interrupted.";
   case EIR_Result.NO_DRIVE: return "No USB drive in your inventory.";
   case EIR_Result.NO_DATA: return "Download finished: no intel found on this rack.";
   case EIR_Result.BUSY: return "This rack is already in use.";
   case EIR_Result.ALREADY: return "You are already downloading from another rack.";
  }
  return "Download stopped.";
 }

 bool SetText(string title, string content)
 {
  if (!IsAuthority() || !ValidText(title, content)) return false;
  m_sTitle = title;
  m_sContent = content;
  Trace("edited");
  return true;
 }
 void SetSeconds(int seconds)
 {
  if (!IsAuthority()) return;
  m_iSeconds = Math.Clamp(seconds, MIN_SECONDS, MAX_SECONDS);
  Replication.BumpMe();
  Trace("download time edited");
 }
 void SetDebug(bool enabled)
 {
  if (!IsAuthority()) return;
  m_bDebug = enabled;
  m_iLogCount = 0;
  Trace("diagnostics enabled; range=3m tick=250ms");
 }
 // For a persistence bridge: restore authored state without an editor transaction.
 bool RestoreState(string title, string content, int seconds)
 {
  if (!SetText(title, content)) return false;
  SetSeconds(seconds);
  return true;
 }
 void Trace(string eventName)
 {
  if (!m_bDebug || !IsAuthority() || m_iLogCount >= LOG_LIMIT) return;
  m_iLogCount++;
  PrintFormat("[EIR] %1 rack=%2 downloader=%3 permille=%4 seconds=%5 titleBytes=%6 textBytes=%7", eventName, GetOwner(), m_iDownloader, m_iPermille, m_iSeconds, m_sTitle.Length(), m_sContent.Length());
 }

 override void OnPostInit(IEntity owner)
 {
  super.OnPostInit(owner);
  if (!GetGame().InPlayMode() || !IsAuthority()) return;
  // Before the first snapshot: a clamped prefab value needs no bump.
  m_iSeconds = Math.Clamp(m_iSeconds, MIN_SECONDS, MAX_SECONDS);
  Trace("created");
 }

 // Server: the same action starts a download, or cancels the caller's own one.
 void ToggleDownload(IEntity user, int player)
 {
		EXPBG_LazyStatics_EIR_RackComponent();
  if (!IsAuthority() || !user || player <= 0) return;
  if (m_iDownloader == player)
  {
   EndSession(EIR_Result.CANCELLED);
   return;
  }
  int refusal = EIR_Result.NONE;
  IEntity active;
  if (s_mSessions.Find(player, active) && active && active != GetOwner()) refusal = EIR_Result.ALREADY;
  else if (m_iDownloader > 0) refusal = EIR_Result.BUSY;
  else if (!Alive(user)) refusal = EIR_Result.INTERRUPTED;
  else if (vector.DistanceSq(user.GetOrigin(), GetOwner().GetOrigin()) > RANGE_SQ) refusal = EIR_Result.OUT_OF_RANGE;
  else
  {
   bool overwriteAtStart;
   if (!EIR_DriveComponent.FindDrive(user, m_sTitle, m_sContent, overwriteAtStart)) refusal = EIR_Result.NO_DRIVE;
  }
  if (refusal != EIR_Result.NONE)
  {
   Trace("start refused " + typename.EnumToString(EIR_Result, refusal));
   NotifyPlayer(player, refusal);
   return;
  }
  s_mSessions.Set(player, GetOwner());
  m_User = user;
  m_iDownloader = player;
  m_iPermille = 0;
  m_iSessionSeconds = Math.Clamp(m_iSeconds, MIN_SECONDS, MAX_SECONDS);
  m_fStartMs = GetGame().GetWorld().GetWorldTime();
  Replication.BumpMe();
  GetGame().GetCallqueue().Remove(ServerTick);
  GetGame().GetCallqueue().CallLater(ServerTick, TICK_MS, true);
  Trace("download started");
  UpdateLocalHud();
 }
 // Bounded: one repeating 250 ms call per running download, O(1) work per call.
 protected void ServerTick()
 {
  if (m_iDownloader <= 0)
  {
   GetGame().GetCallqueue().Remove(ServerTick);
   return;
  }
  IEntity user = m_User;
  if (!user || GetGame().GetPlayerManager().GetPlayerControlledEntity(m_iDownloader) != user || !Alive(user))
  {
   EndSession(EIR_Result.INTERRUPTED);
   return;
  }
  if (vector.DistanceSq(user.GetOrigin(), GetOwner().GetOrigin()) > RANGE_SQ)
  {
   EndSession(EIR_Result.OUT_OF_RANGE);
   return;
  }
  float elapsed = GetGame().GetWorld().GetWorldTime() - m_fStartMs;
  float length = m_iSessionSeconds * 1000;
  if (length <= 0 || elapsed >= length)
  {
   CompleteSession();
   return;
  }
  int permille = Math.Clamp(Math.Floor(elapsed * 1000 / length), 0, 999);
  if (permille == m_iPermille) return;
  m_iPermille = permille;
  Replication.BumpMe();
  UpdateLocalHud();
 }
 protected void CompleteSession()
 {
  if (m_sTitle.IsEmpty() && m_sContent.IsEmpty())
  {
   EndSession(EIR_Result.NO_DATA);
   return;
  }
  // The drive is chosen at completion: it may have been moved or handed over meanwhile.
  bool overwrite;
  EIR_DriveComponent drive = EIR_DriveComponent.FindDrive(m_User, m_sTitle, m_sContent, overwrite);
  if (!drive)
  {
   EndSession(EIR_Result.NO_DRIVE);
   return;
  }
  if (!drive.Store(m_sTitle, m_sContent))
  {
   EndSession(EIR_Result.INTERRUPTED);
   return;
  }
  if (overwrite) EndSession(EIR_Result.OVERWRITTEN);
  else EndSession(EIR_Result.COMPLETE);
 }
 protected void EndSession(int result)
 {
  int player = m_iDownloader;
  GetGame().GetCallqueue().Remove(ServerTick);
  ReleaseSession(player);
  m_User = null;
  m_iDownloader = 0;
  m_iPermille = 0;
  Replication.BumpMe();
  Trace("download ended " + typename.EnumToString(EIR_Result, result));
  NotifyPlayer(player, result);
  UpdateLocalHud();
 }
 protected void ReleaseSession(int player)
 {
		EXPBG_LazyStatics_EIR_RackComponent();
  if (player <= 0) return;
  IEntity active;
  if (!s_mSessions.Find(player, active)) return;
  if (!active || active == GetOwner()) s_mSessions.Remove(player);
 }
 protected void NotifyPlayer(int player, int result)
 {
  if (player <= 0) return;
  Rpc(RpcDo_Result, player, result);
  RpcDo_Result(player, result);
 }
 // Transient feedback: delivered to clients streaming the rack, shown only by the
 // addressed player. No JIP state; the replicated session props cover late joiners.
 [RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
 protected void RpcDo_Result(int player, int result)
 {
  if (System.IsConsoleApp() || player <= 0 || player != SCR_PlayerController.GetLocalPlayerId()) return;
  bool success = result == EIR_Result.COMPLETE || result == EIR_Result.OVERWRITTEN;
  EIR_ProgressHud.ShowResult(this, ResultText(result), success);
 }
 // Replication callback on proxies (public, like vanilla onRplName handlers).
 void OnStateRpl()
 {
  UpdateLocalHud();
 }
 // Clients and a listen-server host: only the downloading player's machine shows progress.
 protected void UpdateLocalHud()
 {
  if (System.IsConsoleApp() || !GetGame() || !GetGame().InPlayMode()) return;
  int localPlayer = SCR_PlayerController.GetLocalPlayerId();
  if (localPlayer > 0 && m_iDownloader == localPlayer) EIR_ProgressHud.ShowProgress(this, m_iPermille, m_iSessionSeconds);
  else EIR_ProgressHud.Release(this);
 }
 override void OnDelete(IEntity owner)
 {
  if (GetGame()) GetGame().GetCallqueue().Remove(ServerTick);
  ReleaseSession(m_iDownloader);
  EIR_ProgressHud.Detach(this);
  super.OnDelete(owner);
 }

	//------------------------------------------------------------------------------------------------
	//! Creates the collections on first use (not in the global static initializer, which has a
	//! per-function instruction limit that large modsets exceed on Windows).
	protected static void EXPBG_LazyStatics_EIR_RackComponent()
	{
		if (!s_mSessions)
			s_mSessions = new map<int, IEntity>();
	}
}

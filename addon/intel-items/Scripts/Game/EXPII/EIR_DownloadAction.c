// "Download intel" on an EXPBG server rack; "Cancel download" for the player already
// downloading there. Clients only gate the prompt; the server re-checks every rule.
class EIR_DownloadAction : ScriptedUserAction
{
 // Zero-duration actions can re-perform while the key is held: every perform within
 // REPEAT_MS of the previous one from that player is ignored, so a hold never cancels.
 static const int REPEAT_MS = 1000;
 static const int DRIVE_CACHE_MS = 500;
 protected static ref map<int, int> s_mLastPerform;

 protected EIR_RackComponent m_Rack;
 protected IEntity m_CachedUser;
 protected int m_iCachedAt;
 protected bool m_bCachedDrive;

 override bool HasLocalEffectOnlyScript() { return false; }
 override bool CanBroadcastScript() { return false; }

 protected EIR_RackComponent Rack()
 {
  if (!m_Rack && GetOwner()) m_Rack = EIR_RackComponent.Cast(GetOwner().FindComponent(EIR_RackComponent));
  return m_Rack;
 }
 protected static int PlayerOf(IEntity user)
 {
  if (!user || !GetGame().GetPlayerManager()) return 0;
  return GetGame().GetPlayerManager().GetPlayerIdFromControlledEntity(user);
 }
 // The prompt is evaluated every frame while shown; cache the inventory search.
 protected bool HasDrive(IEntity user)
 {
  int now = System.GetTickCount();
  int age = now - m_iCachedAt;
  if (user != m_CachedUser || age < 0 || age >= DRIVE_CACHE_MS)
  {
   bool overwrite;
   m_bCachedDrive = EIR_DriveComponent.FindDrive(user, string.Empty, string.Empty, overwrite) != null;
   m_CachedUser = user;
   m_iCachedAt = now;
  }
  return m_bCachedDrive;
 }
 protected static bool Repeated(int player)
 {
		EXPBG_LazyStatics_EIR_DownloadAction();
  int now = System.GetTickCount();
  int last;
  bool repeated = s_mLastPerform.Find(player, last) && now - last >= 0 && now - last < REPEAT_MS;
  if (!s_mLastPerform.Contains(player) && s_mLastPerform.Count() >= 256) s_mLastPerform.Clear();
  s_mLastPerform.Set(player, now);
  return repeated;
 }

 override bool GetActionNameScript(out string outName)
 {
  EIR_RackComponent rack = Rack();
  int player = PlayerOf(SCR_PlayerController.GetLocalControlledEntity());
  if (!rack || player <= 0 || rack.GetDownloader() != player) return false;
  int percent = rack.GetPermille() / 10;
  outName = "Cancel download (" + percent.ToString() + "%)";
  return true;
 }
 override bool CanBeShownScript(IEntity user)
 {
  return user && Rack();
 }
 override bool CanBePerformedScript(IEntity user)
 {
  EIR_RackComponent rack = Rack();
  if (!rack || !user) return false;
  if (vector.DistanceSq(user.GetOrigin(), GetOwner().GetOrigin()) > EIR_RackComponent.RANGE_SQ)
  {
   SetCannotPerformReason("Too far away");
   return false;
  }
  int player = PlayerOf(user);
  int downloader = rack.GetDownloader();
  if (downloader > 0 && downloader == player) return true;
  if (downloader > 0)
  {
   SetCannotPerformReason("In use");
   return false;
  }
  if (!HasDrive(user))
  {
   SetCannotPerformReason("USB drive required");
   return false;
  }
  return true;
 }
 override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
 {
  EIR_RackComponent rack = Rack();
  if (!rack || !pUserEntity || !rack.IsAuthority()) return;
  int player = PlayerOf(pUserEntity);
  if (player <= 0 || Repeated(player)) return;
  rack.ToggleDownload(pUserEntity, player);
 }

	//------------------------------------------------------------------------------------------------
	//! Creates the collections on first use (not in the global static initializer, which has a
	//! per-function instruction limit that large modsets exceed on Windows).
	protected static void EXPBG_LazyStatics_EIR_DownloadAction()
	{
		if (!s_mLastPerform)
			s_mLastPerform = new map<int, int>();
	}
}

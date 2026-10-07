class EII_ReadAction : ScriptedUserAction
{
 // Zero-duration actions re-perform every few frames while the key is held.
 static const int REPEAT_MS = 1000;
 protected static ref map<int, int> s_mLastPerform;

 override bool HasLocalEffectOnlyScript() { return false; }
 override bool CanBroadcastScript() { return true; }
 override bool CanBePerformedScript(IEntity user)
 {
  return user && vector.DistanceSq(user.GetOrigin(), GetOwner().GetOrigin()) <= 9;
 }
 // Per user and per machine: accept the first perform of a burst; every perform
 // within REPEAT_MS of the previous one (held key, echoed broadcast) is ignored.
 protected static bool Repeated(IEntity user)
 {
		EXPBG_LazyStatics_EII_ReadAction();
  int player = GetGame().GetPlayerManager().GetPlayerIdFromControlledEntity(user);
  int now = System.GetTickCount();
  int last;
  bool repeated = s_mLastPerform.Find(player, last) && now - last >= 0 && now - last < REPEAT_MS;
  if (!s_mLastPerform.Contains(player) && s_mLastPerform.Count() >= 256) s_mLastPerform.Clear();
  s_mLastPerform.Set(player, now);
  return repeated;
 }
 override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
 {
  if (!pOwnerEntity || !pUserEntity || vector.DistanceSq(pOwnerEntity.GetOrigin(), pUserEntity.GetOrigin()) > 9) return;
  EII_IntelComponent intel = EII_IntelComponent.Cast(pOwnerEntity.FindComponent(EII_IntelComponent));
  if (!intel) return;
  PlayerController controller = GetGame().GetPlayerController();
  bool reader = controller && controller.GetControlledEntity() == pUserEntity;
  bool authority = intel.IsAuthority();
  // Other clients receive the broadcast too; only the reading player opens the text.
  if (!reader && !authority) return;
  if (Repeated(pUserEntity)) return;
  if (authority)
  {
   intel.Trace("read");
   intel.TryStartup(pOwnerEntity.GetOrigin());
  }
  if (!reader) return;
  string content = EII_IntelComponent.DisplayText(intel.GetContent());
  if (content.IsEmpty()) content = "No intel text has been entered.";
  string title = intel.GetTitle();
  if (title.IsEmpty()) title = "Intel";
  EII_ReadDialog.Open(title, content);
 }

	//------------------------------------------------------------------------------------------------
	//! Creates the collections on first use (not in the global static initializer, which has a
	//! per-function instruction limit that large modsets exceed on Windows).
	protected static void EXPBG_LazyStatics_EII_ReadAction()
	{
		if (!s_mLastPerform)
			s_mLastPerform = new map<int, int>();
	}
}

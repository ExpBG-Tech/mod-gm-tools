class EII_ReadAction : ScriptedUserAction
{
 override bool HasLocalEffectOnlyScript() { return false; }
 override bool CanBroadcastScript() { return true; }
 override bool CanBePerformedScript(IEntity user)
 {
  return user && vector.DistanceSq(user.GetOrigin(), GetOwner().GetOrigin()) <= 9;
 }
 override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
 {
  if (!pOwnerEntity || !pUserEntity || vector.DistanceSq(pOwnerEntity.GetOrigin(), pUserEntity.GetOrigin()) > 9) return;
  EII_IntelComponent intel = EII_IntelComponent.Cast(pOwnerEntity.FindComponent(EII_IntelComponent));
  if (!intel) return;
  if (intel.IsAuthority())
  {
   intel.Trace("read");
   intel.TryStartup(pOwnerEntity.GetOrigin());
  }
  PlayerController controller = GetGame().GetPlayerController();
  if (controller && controller.GetControlledEntity() == pUserEntity)
  {
   string content = intel.GetContent();
   if (content.IsEmpty()) content = "No intel text has been entered.";
   SCR_HintManagerComponent.ShowCustomHint(content, intel.GetTitle(), 30, true);
  }
 }
}

// "EXPBG: Brief on map". The server grants the board lock; the briefer's client then opens
// the map on its own (see EBM_BriefingClient). Others see who holds the board.
class EBM_BriefAction : ScriptedUserAction
{
 override bool HasLocalEffectOnlyScript() { return false; }
 override bool CanBroadcastScript() { return false; }
 override bool CanBeShownScript(IEntity user)
 {
  return EBM_BriefingBoardComponent.Find(GetOwner()) != null;
 }
 override bool CanBePerformedScript(IEntity user)
 {
  EBM_BriefingBoardComponent board = EBM_BriefingBoardComponent.Find(GetOwner());
  if (!board || !user) return false;
  int briefer = board.GetBrieferId();
  if (briefer <= 0) return true;
  if (briefer == GetGame().GetPlayerManager().GetPlayerIdFromControlledEntity(user)) return true;
  SetCannotPerformReason("Briefing: " + board.GetBrieferName());
  return false;
 }
 override bool GetActionNameScript(out string outName)
 {
  outName = "EXPBG: Brief on map";
  return true;
 }
 override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
 {
  EBM_BriefingBoardComponent board = EBM_BriefingBoardComponent.Find(pOwnerEntity);
  if (!board || !pUserEntity || !board.IsAuthority()) return;
  if (vector.Distance(pUserEntity.GetOrigin(), pOwnerEntity.GetOrigin()) > board.GetMaxBriefDistance()) return;
  board.ServerRequestBriefing(GetGame().GetPlayerManager().GetPlayerIdFromControlledEntity(pUserEntity));
 }
}

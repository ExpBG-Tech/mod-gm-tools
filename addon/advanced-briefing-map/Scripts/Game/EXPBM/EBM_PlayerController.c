// Briefing traffic rides on the player's own controller: a client may only send server
// RPCs through an item it owns, and the server reaches the briefer through its owner.
modded class SCR_PlayerController
{
 // Server -> owning client: the board lock was granted; open the map and start streaming.
 void EBM_BeginBriefing(RplId board)
 {
  if (GetGame().GetPlayerController() == this)
  {
   EBM_BriefingClient.Begin(board);
   return;
  }
  Rpc(RPC_EBM_BeginBriefing, board);
 }

 [RplRpc(RplChannel.Reliable, RplRcver.Owner)]
 protected void RPC_EBM_BeginBriefing(RplId board)
 {
  if (GetGame().GetPlayerController() != this) return;
  EBM_BriefingClient.Begin(board);
 }

 // Client -> server: current view, own markers and drawn lines for the board this player holds.
 void EBM_SendBoardState(RplId board, array<float> view, array<int> markers, array<string> texts, array<float> lines)
 {
  if (Replication.IsServer())
  {
   EBM_ApplyBoardState(board, view, markers, texts, lines);
   return;
  }
  Rpc(RPC_EBM_BoardState, board, view, markers, texts, lines);
 }

 [RplRpc(RplChannel.Reliable, RplRcver.Server)]
 protected void RPC_EBM_BoardState(RplId board, array<float> view, array<int> markers, array<string> texts, array<float> lines)
 {
  EBM_ApplyBoardState(board, view, markers, texts, lines);
 }

 protected void EBM_ApplyBoardState(RplId board, array<float> view, array<int> markers, array<string> texts, array<float> lines)
 {
  EBM_BriefingBoardComponent component = EBM_BriefingBoardComponent.FromRplId(board);
  if (component) component.ServerApplyState(GetPlayerId(), view, markers, texts, lines);
 }

 // Client -> server: the briefer closed the map; free the board.
 void EBM_ReleaseBoard(RplId board)
 {
  if (Replication.IsServer())
  {
   EBM_ApplyRelease(board);
   return;
  }
  Rpc(RPC_EBM_ReleaseBoard, board);
 }

 [RplRpc(RplChannel.Reliable, RplRcver.Server)]
 protected void RPC_EBM_ReleaseBoard(RplId board)
 {
  EBM_ApplyRelease(board);
 }

 protected void EBM_ApplyRelease(RplId board)
 {
  EBM_BriefingBoardComponent component = EBM_BriefingBoardComponent.FromRplId(board);
  if (component) component.ServerRelease(GetPlayerId());
 }
}

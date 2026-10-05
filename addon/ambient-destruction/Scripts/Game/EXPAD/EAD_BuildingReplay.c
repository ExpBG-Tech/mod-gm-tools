// Native Kill is asynchronous and ultimately deletes the original map entity.
// CDF's repair hook must finish that work before it allows saved actors to spawn.
class EAD_BuildingReplayToken
{
 IEntity Entity;
 vector Transform[4];
 bool Complete;
}
// Multi-phase models also use a delayed native ChangeModel callback. Complete
// only a requested CDF replay before actors spawn; normal damage stays native.
modded class SCR_DestructionMultiPhaseComponent
{
 bool EAD_CompletePhaseReplay(int phase)
 {
  if (!Replication.IsServer() || !EAD_Snapshot.Loading || !EAD_Buildings.NativePhases(GetOwner()) || GetDamagePhase() != phase) return false;
  if (!EAD_ApplyReplayModel(phase)) return false;
  Rpc(RPC_EAD_CompletePhaseReplay, phase);
  return true;
 }
 protected bool EAD_ApplyReplayModel(int phase)
 {
  SCR_DamagePhaseData stage = GetDamagePhaseData(phase);
  if (!stage || GetDamagePhase() != phase) return false;
  GetGame().GetCallqueue().Remove(ChangeModel);
  ChangeModel(stage.m_PhaseModel, stage.m_bUseMaterialsFromParent);
  IEntity owner = GetOwner();
  if (!owner || !owner.GetVObject()) return false;
  ResourceName model; string remap;
  SCR_Global.GetModelAndRemapFromResource(stage.m_PhaseModel, model, remap);
  Physics physics = owner.GetPhysics();
  return owner.GetVObject().GetResourceName() == model && physics && !physics.IsDynamic() && physics.GetNumGeoms() > 0;
 }
 [RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
 protected void RPC_EAD_CompletePhaseReplay(int phase)
 {
  EAD_ApplyReplayModel(phase);
 }
}
class EAD_BuildingReplay
{
 static const int MAX_PARTS = 128;
 static const int MAX_INTERIOR_BATCHES = 256;
 protected static ref map<IEntity, ref EAD_BuildingReplayToken> s_Active = new map<IEntity, ref EAD_BuildingReplayToken>();
 ref array<ref EAD_BuildingReplayToken> Parts = {};
 bool Prepare(IEntity root)
 {
  Parts.Clear();
  if (!Add(root)) return false;
  for (int index = 0; index < Parts.Count(); index++)
  {
   IEntity child = Parts[index].Entity.GetChildren();
   while (child)
   {
    // Native DestroyChildDestructibles detaches and kills these direct children.
    if (child.FindComponent(SCR_DestructibleBuildingComponent) && !Add(child)) return false;
    child = child.GetSibling();
   }
  }
  return true;
 }
 protected bool Add(IEntity entity)
 {
  if (!entity || Parts.Count() >= MAX_PARTS) return false;
  SCR_DestructibleBuildingComponent damage = SCR_DestructibleBuildingComponent.Cast(entity.FindComponent(SCR_DestructibleBuildingComponent));
  if (!damage || !damage.GetDefaultHitZone() || damage.GetDefaultHitZone().GetDamageState() == EDamageState.DESTROYED) return false;
  EAD_BuildingReplayToken token = new EAD_BuildingReplayToken();
  token.Entity = entity; entity.GetWorldTransform(token.Transform); Parts.Insert(token);
  return true;
 }
 static void Completed(IEntity entity)
 {
  EAD_BuildingReplayToken token = s_Active.Get(entity);
  if (token) token.Complete = true;
 }
 bool Run()
 {
  if (!Replication.IsServer() || !EAD_Snapshot.Loading || Parts.IsEmpty() || !s_Active.IsEmpty()) return false;
  foreach (EAD_BuildingReplayToken pending : Parts)
  {
   SCR_DestructibleBuildingComponent pendingDamage;
   if (pending.Entity) pendingDamage = SCR_DestructibleBuildingComponent.Cast(pending.Entity.FindComponent(SCR_DestructibleBuildingComponent));
   if (!pendingDamage || pendingDamage.GetDefaultHitZone().GetDamageState() == EDamageState.DESTROYED) return false;
   vector transform[4]; pending.Entity.GetWorldTransform(transform);
   for (int axis = 0; axis < 4; axis++) { if (vector.DistanceSq(transform[axis], pending.Transform[axis]) > 0.0025) return false; }
  }
  foreach (EAD_BuildingReplayToken part : Parts) s_Active.Insert(part.Entity, part);
  bool success = true;
  // Children first: the parent's native interior query must not start a later
  // collapse in a detached child after CDF has restored actors.
  for (int index = Parts.Count() - 1; index >= 0; index--)
  {
   EAD_BuildingReplayToken token = Parts[index];
   if (token.Complete) continue;
   SCR_DestructibleBuildingComponent damage;
   if (token.Entity) damage = SCR_DestructibleBuildingComponent.Cast(token.Entity.FindComponent(SCR_DestructibleBuildingComponent));
   if (!damage || !damage.EAD_ReplayDestruction(token)) { success = false; break; }
  }
  s_Active.Clear();
  return success;
 }
}
modded class SCR_DestructibleBuildingComponent
{
 bool EAD_ReplayDestruction(EAD_BuildingReplayToken token)
 {
  if (!Replication.IsServer() || !EAD_Snapshot.Loading || !token || token.Entity != GetOwner()) return false;
  // Preserve native health, reliable destruction broadcast and effect selection.
  if (GetDefaultHitZone().GetDamageState() != EDamageState.DESTROYED) Kill(Instigator.CreateInstigator(null));
  if (token.Complete) return true;
  if (!token.Entity || GetDefaultHitZone().GetDamageState() != EDamageState.DESTROYED) return false;
  Rpc(RPC_EAD_CompleteReplay, token.Transform[0], token.Transform[1], token.Transform[2], token.Transform[3]);
  RPC_EAD_CompleteReplay(token.Transform[0], token.Transform[1], token.Transform[2], token.Transform[3]);
  // Native "immediate" destruction still yields after 20 interior checks.
  // Drain only this component's existing queue; never query newly restored actors.
  for (int batch = 0; !token.Complete && token.Entity && batch < EAD_BuildingReplay.MAX_INTERIOR_BATCHES; batch++)
  {
   GetGame().GetCallqueue().Remove(DestroyInterior);
   DestroyInterior(true);
  }
  return token.Complete;
 }
 [RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
 protected void RPC_EAD_CompleteReplay(vector t0, vector t1, vector t2, vector t3)
 {
  GetGame().GetCallqueue().Remove(GoToDestroyedState);
  GetGame().GetCallqueue().Remove(DestroyInterior);
  IEntity owner = GetOwner();
  if (!owner) return;
  vector transform[4]; transform[0] = t0; transform[1] = t1; transform[2] = t2; transform[3] = t3;
  // Use the recorded pre-collapse transform even if a client already began sinking.
  owner.SetWorldTransform(transform);
  GoToDestroyedStateLoad(true);
 }
 protected override void DeleteBuilding()
 {
  // Positive native completion, recorded outside the component before deletion.
  EAD_BuildingReplay.Completed(GetOwner());
  super.DeleteBuilding();
 }
}

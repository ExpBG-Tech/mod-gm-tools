// CDF 1.4.1 uses the native editor Serialize/EOnEditorSessionLoad contract.
// Keep the extension here independent of CDF types and its optional dependency.
class EAC_SessionLifecycle
{
 // Never veto a foreign parent: Serialize=false skips its entire subtree in
 // CDF, which would silently lose manually authored siblings from the save.
 // CDF owns recursive deletion of such foreign parents during clear-before-load;
 // Ambient's guarded retirement cannot override that external deletion policy.
 static bool ContainsOwned(SCR_EditableEntityComponent candidate, IEntity managedEntity)
 {
  if (!candidate || !managedEntity) return false;
  return candidate.GetOwner() == managedEntity;
 }

 static bool IsTransient(SCR_EditableEntityComponent candidate)
 {
  if (!Replication.IsServer() || !candidate) return false;
  EAC_ResidentClaims claims = EAC_AmbientModule.GetMissionClaims();
  if (claims && claims.EAC_ContainsSessionEntity(candidate)) return true;
  EAC_PedestrianSpawner spawner = EAC_AmbientModule.EAC_GetSessionSpawner();
  if (spawner && spawner.EAC_ContainsSessionHelper(candidate)) return true;
  EAC_TrafficDirector traffic = EAC_TrafficDirector.Get();
  return traffic && traffic.EAC_ContainsSessionEntity(candidate);
 }
}

modded class SCR_EditableEntityComponent
{
 override bool Serialize(out SCR_EditableEntityComponent outTarget = null, out int outTargetIndex = -1, out EEditableEntitySaveFlag outSaveFlags = 0)
 {
  if (EAC_SessionLifecycle.IsTransient(this)) return false;
  return super.Serialize(outTarget, outTargetIndex, outSaveFlags);
 }

 override void EOnEditorSessionLoad(SCR_EditableEntityComponent parent)
 {
  super.EOnEditorSessionLoad(parent);
  EAC_AmbientModule module = EAC_AmbientModule.Cast(GetOwner());
  if (!module || !Replication.IsServer()) return;
  module.EAC_BeginSessionSettingsLoad();
  EAC_PedestrianSpawner spawner = module.GetSpawner();
  if (spawner) spawner.EAC_RetireSessionPopulation();
  EAC_TrafficDirector traffic = EAC_TrafficDirector.Get();
  if (traffic) traffic.EAC_RetireSessionPopulation();
 }
}

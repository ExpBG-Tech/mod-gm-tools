// A short-lived owned cleanup transaction. It stores no civilian snapshot and
// exists only while an original actor or its empty group still needs deletion.
class EAC_PedestrianCache
{
 protected EAC_ResidentClaim m_Claim;
 protected bool m_Deleted;
 protected string m_Error;

 void EAC_PedestrianCache(EAC_ResidentClaim claim) { m_Claim = claim; }
 bool HasDeletionAttempted() { return m_Deleted; }
 bool IsComplete() { return m_Deleted && m_Claim && !m_Claim.Character && !m_Claim.Group && (!m_Claim.OptimizerMember || !m_Claim.OptimizerMember.Entity); }
 string GetError() { return m_Error; }
 protected bool Refuse(string reason) { m_Error = reason; return false; }

 protected bool Owns()
 {
  EAC_AmbientModule module = EAC_AmbientModule.GetActive();
  return Replication.IsServer() && module && m_Claim && m_Claim.Cache == this && module.GetResidentActivation(m_Claim.Home, m_Claim.Resident) == m_Claim;
 }

 protected bool SafeGroup(SCR_AIGroup group)
 {
  if (!group || group != m_Claim.Group || group.GetWorld() != EAC_AmbientModule.GetActive().GetWorld()) return false;
  if (group.Type() != SCR_AIGroup || group.IsPlayable() || group.GetMaster() || group.GetSlave() || group.IsCreatedByCommander() || group.GetRallyPointId() >= 0) return false;
  RplComponent replication = RplComponent.Cast(group.FindComponent(RplComponent));
  if (!replication || replication.IsProxy() || !replication.IsOwner() || group.GetPlayerCount() != 0 || group.GetAgentsCount() > 1) return false;
  if (group.GetPermanentLOD() >= 0 || group.GetLifecyclePolicy() == SCR_EAIGroupLifecyclePolicy.ProximityDriven || group.EBG_NativeHasSceneReferences() || group.GetChildren()) return false;
  if (!EBG_PrefabFullCache.CanDeleteFullEntity(group)) return false;
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(group);
  if (editable)
  {
   set<SCR_EditableEntityComponent> children = new set<SCR_EditableEntityComponent>();
   editable.GetChildren(children, true);
   foreach (SCR_EditableEntityComponent child : children)
    if (!child || child.GetParentEntity() != editable || child.GetOwner() != m_Claim.Character) return false;
  }
  array<AIWaypoint> orders = {}; group.GetWaypoints(orders);
  if (!orders.IsEmpty()) return false;
  SCR_AIGroupSettingsComponent settings = SCR_AIGroupSettingsComponent.Cast(group.FindComponent(SCR_AIGroupSettingsComponent));
  if (!settings) return false;
  array<SCR_AISettingBase> entries = {}; settings.GetAllSettings(entries);
  array<SCR_AISettingBase> known = {};
  foreach (SCR_AISettingBase entry : entries)
  {
   if (entry.GetOrigin() == SCR_EAISettingOrigin.DEFAULT || entry.GetOrigin() == SCR_EAISettingOrigin.WAYPOINT) continue;
   typename kind = entry.Type();
   if (entry.GetOrigin() != SCR_EAISettingOrigin.SCENARIO || (kind != SCR_AICharacterStanceSetting && kind != SCR_AICharacterMovementSpeedSetting && kind != SCR_AIGroupCharactersMovementSpeedSetting && kind != SCR_AIGroupCombatModeSetting)) return false;
   foreach (SCR_AISettingBase previous : known)
    if (previous.GetCategorizationType() == entry.GetCategorizationType()) return false;
   known.Insert(entry);
  }
  return known.Count() <= 4;
 }

 protected void DeleteOwned(IEntity entity)
 {
  m_Deleted = true;
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(entity);
  if (editable) editable.Delete(false, false);
  else SCR_EntityHelper.DeleteEntityAndChildren(entity);
  if (!entity) EAC_SchedulerStats.RecordDespawn();
 }

 bool BeginManagedSleep(SCR_AIGroup group)
 {
  if (m_Deleted || !Owns() || !group || group != m_Claim.Group || !m_Claim.Character) return Refuse("Ambient sleep requires its live owned claim");
  return RemoveNext();
 }

 // One native entity per scheduler operation. A failed deletion retains its
 // claim and reservation; no later operation can adopt another actor or group.
 bool RemoveNext()
 {
  if (!Owns()) return Refuse("Ambient cleanup requires its active owner");
  if (IsComplete()) return true;
  if (!SafeGroup(m_Claim.Group)) return Refuse("Ambient group ownership, orders or deletion protection changed");
  SCR_ChimeraCharacter actor = SCR_ChimeraCharacter.Cast(m_Claim.Character);
  if (actor)
  {
   if (m_Claim.AlarmUntil > actor.GetWorld().GetWorldTime() * 0.001 || EAC_CivilianDanger.HasThreat(actor)) return Refuse("Ambient danger recovery is still active");
   if (!m_Claim.OptimizerMember || m_Claim.OptimizerMember.Entity != actor || m_Claim.OptimizerMember.Dead || m_Claim.OptimizerMember.WasPlayer || m_Claim.Resident.Dead || m_Claim.Resident.Removed) return Refuse("Ambient resident ownership or outcome changed");
   RplComponent replication = RplComponent.Cast(actor.FindComponent(RplComponent));
   AIControlComponent control = AIControlComponent.Cast(actor.FindComponent(AIControlComponent));
   if (!replication || replication.IsProxy() || !replication.IsOwner() || !control || !control.GetAIAgent() || control.GetAIAgent().GetParentGroup() != m_Claim.Group) return Refuse("Ambient actor authority changed");
   if (!EAC_PedestrianSpawner.IsCivilian(actor, m_Claim.Group) || actor.EBG_WasPlayerControlled() || SCR_PossessingManagerComponent.GetPlayerIdFromControlledEntity(actor) != 0 || CompartmentAccessComponent.GetVehicleIn(actor) || !EBG_PrefabFullCache.CanDeleteFullEntity(actor)) return Refuse("Ambient actor control or deletion protection changed");
   string unsupported = EBG_SimulationCache.Unsupported(actor, false);
   if (!unsupported.IsEmpty()) return Refuse(unsupported);
   DeleteOwned(actor);
   if (m_Claim.Character) return Refuse("Ambient actor deletion remains pending");
   return true;
  }
  if (!m_Deleted || (m_Claim.OptimizerMember && m_Claim.OptimizerMember.Entity) || m_Claim.Group.GetAgentsCount() != 0) return Refuse("Ambient cleanup has no confirmed owned absence");
  DeleteOwned(m_Claim.Group);
  return IsComplete();
 }
}

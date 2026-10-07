// Character-local monotonic history exists before any cache zone/manager.
// This latch covers the live character lifetime and never owns the entity.
modded class SCR_ChimeraCharacter
{
 protected bool m_EBG_EverPlayerControlled;
 protected bool m_EBG_GMCreated;
 void SCR_ChimeraCharacter(IEntitySource src, IEntity parent) { m_EBG_GMCreated = EBG_MissionPersistence.GMSpawnInProgress(); }
 bool EBG_IsGMCreated() { return m_EBG_GMCreated; }
 void EBG_MarkPlayerControlled() { m_EBG_EverPlayerControlled = true; EBG_MissionPlayerHistory.Mark(this); }
 // The latch alone. EBG_MissionPlayerHistory.Contains skips its observed-entity scan for
 // a character without it: only EBG_MarkPlayerControlled lists one, after setting it.
 bool EBG_IsMarkedPlayer()
 {
  return m_EBG_EverPlayerControlled;
 }
 bool EBG_WasPlayerControlled()
 {
  CharacterControllerComponent controller = GetCharacterController();
  return m_EBG_EverPlayerControlled || EBG_MissionPlayerHistory.Contains(this) || (controller && controller.IsPlayerControlled());
 }
 // Neutral seam for any module that takes a living soldier out of his squad for
 // good (AI Surrender's prisoners): call it on the server once he has left the
 // native group. Unit Caching and Garrison then forget him as a member and never
 // enroll, cache, respawn or delete him; the rest of his squad caches and wakes as
 // before. Server latch for the live character, not saved.
 protected bool m_EBG_LeftSquad;
 void EBG_MarkLeftSquad()
 {
  m_EBG_LeftSquad = true;
  // Zone status counts him near a cache zone (text only; no cache decision reads it).
  EBG_EnrollmentTally.TrackLeftSquad(this);
  // A settled, awake record forgets him at once; otherwise the regroup scan does.
  EBG_CacheManager manager = EBG_CacheManager.Instance;
  if (Replication.IsServer() && manager && !EBG_CacheManager.Unloading) manager.Regroup.RetireLeftSquad(manager, this);
 }
 bool EBG_HasLeftSquad() { return m_EBG_LeftSquad; }
}

// Native placement only. Every early return stays inside super, so this scope
// closes synchronously. Neither native loading nor ordinary joins enter it.
modded class SCR_PlacingEditorComponent
{
 override protected void CreateEntityServer(SCR_EditorPreviewParams params, RplId prefabID, int playerID, int entityIndex, bool isQueue, array<RplId> recipientIds, bool canBePlayer, RplId holderId)
 {
  EBG_MissionPersistence.GMSpawnDepth++;
  super.CreateEntityServer(params, prefabID, playerID, entityIndex, isQueue, recipientIds, canBePlayer, holderId);
  EBG_MissionPersistence.GMSpawnDepth--;
 }
}
modded class SCR_AIGroup
{
 protected int m_EBG_EmptyPolicyDepth;
 protected bool m_EBG_RequestedDeletePolicy;
 override void SetDeleteWhenEmpty(bool deleteWhenEmpty)
 {
  // Listeners may change policy inside the native empty notification. Remember
  // their intent without allowing native's unconditional deletion to be queued.
  if (m_EBG_EmptyPolicyDepth > 0) { m_EBG_RequestedDeletePolicy = deleteWhenEmpty; return; }
  super.SetDeleteWhenEmpty(deleteWhenEmpty);
 }
#ifdef EBG_ACCEPTANCE_TEST
 protected int m_EBG_SetupEmptyNotices;
#endif
 override void OnEmpty()
 {
  // Preserve only the exact saved AI group while native load callbacks detach
  // members. Retain policy changes made by native notification listeners.
  bool previous = m_bDeleteWhenEmpty;
  bool preserve = EBG_MissionPersistence.PreserveOriginalGroupDuringSetup(this);
  bool managed = EBG_CacheManager.Instance && EBG_CacheManager.Instance.FindGroup(this);
  if (preserve || managed)
  {
   if (m_EBG_EmptyPolicyDepth == 0) m_EBG_RequestedDeletePolicy = previous;
   m_EBG_EmptyPolicyDepth++;
   m_bDeleteWhenEmpty = false;
  }
  super.OnEmpty();
  if (preserve || managed)
  {
   m_EBG_EmptyPolicyDepth--;
   if (m_EBG_EmptyPolicyDepth == 0) m_bDeleteWhenEmpty = m_EBG_RequestedDeletePolicy;
  }
  if (managed && !preserve && m_bDeleteWhenEmpty)
  {
   // Native queues an unconditional static deletion. Recheck this exact group
   // after the callback: it may have been refilled or Full-cached meanwhile.
   GetGame().GetCallqueue().Remove(EBG_DeleteWhenStillEmpty);
   GetGame().GetCallqueue().CallLater(EBG_DeleteWhenStillEmpty, 1, false);
  }
#ifdef EBG_ACCEPTANCE_TEST
  if (preserve && m_EBG_SetupEmptyNotices++ < 3)
   PrintFormat("[EBG MISSION GROUP SETUP] exactSavedGroup=%1 nativeEmptyEventRetained=1 deletePolicyRestored=%2", PersistenceSystem.GetInstance().GetId(this), m_bDeleteWhenEmpty);
#endif
 }
 protected void EBG_DeleteWhenStillEmpty()
 {
  if (!GetGame() || EBG_CacheManager.Unloading || GetWorld() != GetGame().GetWorld() || !Replication.IsServer()) return;
  EBG_CacheGroup record;
  if (EBG_CacheManager.Instance) record = EBG_CacheManager.Instance.FindGroup(this);
  if (record && record.Full && record.Full.GetState() != EBG_FullGroupPhase.RELEASED) return;
  if (IsDormant() || !m_bDeleteWhenEmpty || GetAgentsCount() != 0 || GetPlayerCount() > 0) return;
  SCR_EntityHelper.DeleteEntityAndChildren(this);
 }
 void ~SCR_AIGroup()
 {
  if (GetGame()) GetGame().GetCallqueue().Remove(EBG_DeleteWhenStillEmpty);
 }
 protected bool m_EBG_GMCreated;
 void SCR_AIGroup(IEntitySource src, IEntity parent) { m_EBG_GMCreated = EBG_MissionPersistence.GMSpawnInProgress(); }
 override protected bool SpawnGroupMember(bool snapToTerrain, int index, ResourceName res, bool editMode, bool isLast)
 {
  if (m_EBG_GMCreated) EBG_MissionPersistence.GMSpawnDepth++;
  bool result = super.SpawnGroupMember(snapToTerrain, index, res, editMode, isLast);
  if (m_EBG_GMCreated) EBG_MissionPersistence.GMSpawnDepth--;
  return result;
 }
}

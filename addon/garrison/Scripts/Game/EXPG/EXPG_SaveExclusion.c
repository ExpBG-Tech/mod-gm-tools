// Keeps garrison-owned entities out of every other save: the garrison ledger saves
// them (EXPG_Snapshot.c). Same seam as Ambient Civilians (EAC_SessionLifecycle.c):
//  - native persistence: StopTracking, repeated right before each native save reads
//    its data (SCR_PersistenceSystem.GetOnBeforeSave), however late tracking began;
//  - CDF Game Master Save, when its EXPBG bridge is loaded: the NON_SERIALIZABLE
//    editor flag, which CDF Serialize() honours for capture (the entity and its
//    subtree are left out). The bridge's IsManaged makes CDF clear them on load.
// Owned: a Ready garrison's squad, the squad's waypoints and every living guard who
// is still in that squad (not possessed, not a leaver). Whatever stops being owned
// (a possessed or surrendered guard, a casualty, a released garrison) is handed
// back within one sync: only a flag set here is cleared, and native tracking is
// restarted only for an entity this file took out of it. Bounded by MAX_ENTITIES.
class EXPG_SaveExclusion
{
 static const int MAX_ENTITIES = 8192;
 protected static ref array<IEntity> s_Entities;
 protected static ref array<bool> s_Flagged;
 protected static ref array<bool> s_Untracked;
 protected static ref array<int> s_Confirmed;
 protected static BaseWorld s_World;
 protected static int s_Stamp;
 protected static bool s_SaveHooked;
 protected static int s_Stopped;
 protected static int s_Refused;
 // Right before a native save: stop tracking every kept entity again, even when
 // IsTracked says no (lazy registration).
 protected static bool s_Forcing;

 protected static void CheckWorld()
 {
		EXPBG_LazyStatics_EXPG_SaveExclusion();
  if (!GetGame()) { return; }
  BaseWorld world = GetGame().GetWorld();
  if (world == s_World) { return; }
  s_World = world;
  s_Entities.Clear();
  s_Flagged.Clear();
  s_Untracked.Clear();
  s_Confirmed.Clear();
  s_Stamp = 0;
  s_SaveHooked = false;
  s_Stopped = 0;
  s_Refused = 0;
 }

 // One hook per world; retried until the persistence system exists.
 protected static void HookSaves()
 {
  if (s_SaveHooked || !s_World) { return; }
  SCR_PersistenceSystem persistence = SCR_PersistenceSystem.GetByCurrentWorld();
  if (!persistence) { return; }
  persistence.GetOnBeforeSave().Insert(OnBeforeSave);
  s_SaveHooked = true;
 }

 protected static void OnBeforeSave(ESaveGameType saveType)
 {
  if (!Replication.IsServer()) { return; }
  s_Forcing = true;
  EXPG_GarrisonPersistence.SyncSaveExclusion();
  s_Forcing = false;
  if (s_Refused > 0) { PrintFormat("[EXPG SAVE] %1 garrison entities refused to stop native tracking", s_Refused, level: LogLevel.WARNING); }
 }

 // StopTracking even when IsTracked says no: lazy registration may be pending.
 protected static bool StopNativeTracking(IEntity entity)
 {
  if (!entity) { return false; }
  SCR_PersistenceSystem persistence = SCR_PersistenceSystem.GetByEntityWorld(entity);
  if (!persistence) { return false; }
  bool tracked = persistence.IsTracked(entity);
  bool stopped = persistence.StopTracking(entity);
  if (persistence.IsTracked(entity))
  {
   s_Refused++;
   return false;
  }
  if (tracked && s_Stopped < 1000000000) { s_Stopped++; }
  return tracked || stopped;
 }

 static bool IsNativelyTracked(IEntity entity)
 {
  if (!entity) { return false; }
  SCR_PersistenceSystem persistence = SCR_PersistenceSystem.GetByEntityWorld(entity);
  return persistence && persistence.IsTracked(entity);
 }

 static void BeginSync()
 {
  CheckWorld();
  HookSaves();
  if (s_Stamp >= 1000000000) { s_Stamp = 0; }
  s_Stamp++;
  s_Refused = 0;
 }

 // Exclude one owned entity now (cheap when already kept). flag: also set the
 // CDF-visible NON_SERIALIZABLE editor flag (CDF bridge loaded).
 static void Keep(IEntity entity, bool flag)
 {
		EXPBG_LazyStatics_EXPG_SaveExclusion();
  if (!entity || !Replication.IsServer()) { return; }
  CheckWorld();
  HookSaves();
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(entity);
  int index = s_Entities.Find(entity);
  if (index >= 0)
  {
   s_Confirmed[index] = s_Stamp;
   if (editable && flag != s_Flagged[index]) { SetFlag(index, editable, flag); }
   // Tracking can begin again at any time (a regroup, lazy registration).
   if ((s_Forcing || IsNativelyTracked(entity)) && StopNativeTracking(entity)) { s_Untracked[index] = true; }
   return;
  }
  if (s_Entities.Count() >= MAX_ENTITIES)
  {
   StopNativeTracking(entity);
   return;
  }
  bool untracked = StopNativeTracking(entity);
  s_Entities.Insert(entity);
  s_Flagged.Insert(false);
  s_Untracked.Insert(untracked);
  s_Confirmed.Insert(s_Stamp);
  if (editable && flag) { SetFlag(s_Entities.Count() - 1, editable, true); }
 }

 // Someone else's NON_SERIALIZABLE is never ours to set or clear.
 protected static void SetFlag(int index, SCR_EditableEntityComponent editable, bool flag)
 {
		EXPBG_LazyStatics_EXPG_SaveExclusion();
  if (flag)
  {
   if (editable.HasEntityFlag(EEditableEntityFlag.NON_SERIALIZABLE)) { return; }
   editable.SetEntityFlag(EEditableEntityFlag.NON_SERIALIZABLE, true);
   s_Flagged[index] = true;
   return;
  }
  if (s_Flagged[index]) { editable.SetEntityFlag(EEditableEntityFlag.NON_SERIALIZABLE, false); }
  s_Flagged[index] = false;
 }

 // Hand back everything this sync did not confirm.
 static void EndSync()
 {
		EXPBG_LazyStatics_EXPG_SaveExclusion();
  for (int index = s_Entities.Count() - 1; index >= 0; index--)
  {
   if (s_Entities[index] && s_Confirmed[index] == s_Stamp) { continue; }
   Release(index);
  }
 }

 protected static void Release(int index)
 {
		EXPBG_LazyStatics_EXPG_SaveExclusion();
  IEntity entity = s_Entities[index];
  if (entity)
  {
   SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(entity);
   if (editable && s_Flagged[index]) { editable.SetEntityFlag(EEditableEntityFlag.NON_SERIALIZABLE, false); }
   if (s_Untracked[index])
   {
    SCR_PersistenceSystem persistence = SCR_PersistenceSystem.GetByEntityWorld(entity);
    if (persistence) { persistence.StartTracking(entity); }
   }
  }
  s_Entities.Remove(index);
  s_Flagged.Remove(index);
  s_Untracked.Remove(index);
  s_Confirmed.Remove(index);
 }

 // A discarded garrison's entity that its caller now deletes (Random Garrison Clear):
 // it leaves this set WITHOUT being handed back, so no later sync puts it into a save
 // before it is gone; flagged/untracked say what this file had changed.
 static bool HandOver(IEntity entity, out bool flagged, out bool untracked)
 {
		EXPBG_LazyStatics_EXPG_SaveExclusion();
  flagged = false;
  untracked = false;
  if (!entity)
  {
   return false;
  }
  CheckWorld();
  int index = s_Entities.Find(entity);
  if (index < 0)
  {
   return false;
  }
  flagged = s_Flagged[index];
  untracked = s_Untracked[index];
  s_Entities.Remove(index);
  s_Flagged.Remove(index);
  s_Untracked.Remove(index);
  s_Confirmed.Remove(index);
  return true;
 }

 // Everything goes back (all garrisons released, or a load replaced them).
 static void ReleaseAll()
 {
		EXPBG_LazyStatics_EXPG_SaveExclusion();
  CheckWorld();
  for (int index = s_Entities.Count() - 1; index >= 0; index--) { Release(index); }
 }

 static bool Owns(IEntity entity)
 {
		EXPBG_LazyStatics_EXPG_SaveExclusion();
  if (!entity) { return false; }
  CheckWorld();
  return s_Entities.Contains(entity);
 }

 static bool IsFlagged(IEntity entity)
 {
		EXPBG_LazyStatics_EXPG_SaveExclusion();
  CheckWorld();
  int index = s_Entities.Find(entity);
  if (index < 0) { return false; }
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(entity);
  return editable && editable.HasEntityFlag(EEditableEntityFlag.NON_SERIALIZABLE);
 }

 static int Count()
 {
		EXPBG_LazyStatics_EXPG_SaveExclusion();
  CheckWorld();
  int count;
  foreach (IEntity entity : s_Entities)
  {
   if (entity) { count++; }
  }
  return count;
 }

 static bool IsSaveHooked()
 {
  CheckWorld();
  HookSaves();
  return s_SaveHooked;
 }

	//------------------------------------------------------------------------------------------------
	//! Creates the collections on first use (not in the global static initializer, which has a
	//! per-function instruction limit that large modsets exceed on Windows).
	protected static void EXPBG_LazyStatics_EXPG_SaveExclusion()
	{
		if (!s_Entities)
			s_Entities = new array<IEntity>();
		if (!s_Flagged)
			s_Flagged = new array<bool>();
		if (!s_Untracked)
			s_Untracked = new array<bool>();
		if (!s_Confirmed)
			s_Confirmed = new array<int>();
	}
}

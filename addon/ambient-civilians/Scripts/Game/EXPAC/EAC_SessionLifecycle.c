// Keeps Ambient Civilians' transient population out of Game Master saves.
// Independent of CDF types and of its optional dependency.
//
// Reforger 1.8 marks SCR_EditableEntityComponent.Serialize [Obsolete] ("will be
// removed entirely"), so this file no longer overrides it: the override logged a
// warning on every compile, hid CDF's own obsolete warnings, and becomes a
// compile error for the whole pack the day the method goes. Vanilla Serialize
// already returns false for EEditableEntityFlag.NON_SERIALIZABLE, and that is
// the call CDF GameMaster Save 1.4.1 makes for capture, clear-before-load and
// post-restore cleanup. So the exact set IsTransient describes is kept flagged:
//  - flagged as an owned entity is created (Keep) and on every Sync;
//  - unflagged again within one Sync (1 s) once it stops being transient:
//    a player or regroup handoff, an owned group that gained a foreign member,
//    or the ledger letting it go.
// Only flags this file set are ever cleared; a prefab- or other-mod-authored
// NON_SERIALIZABLE is left alone. Vanilla persistence (SCR_PersistenceSystem)
// does not read the flag, so the same entities also leave its tracking, and are
// handed back when they stop being transient - only if it had tracked them.
//
// 0.1.5 live evidence: the native save restored five leftover civilians and
// their "Non-Combatants" groups at every server start (server-logs-015c and
// -m3, identical prefabs and positions). StopTracking at creation is not enough:
// tracking can begin after it (lazy registration, another system, a regroup),
// and an entity carrying someone else's NON_SERIALIZABLE was never untracked at
// all, because vanilla persistence does not read that flag. Like the Ambient
// Unrest crowd, every entity the ledgers answer for is now taken out of native
// tracking again right before each save reads its data (OnPersistenceBeforeSave).
// The module itself and its settings are not ledger entities and stay saved.
//
// Define EAC_LEGACY_SERIALIZE_HOOK to restore the old override as a fallback.
class EAC_SessionLifecycle
{
 static const float SYNC_SECONDS = 1;
 // Above every ledger that feeds it: 200 residents, 64 traffic parties and
 // their helpers. A full table refuses new flags rather than growing.
 static const int MAX_MARKED = 2048;
 // Parallel arrays: the editable this file flagged, whether StopTracking took it
 // out of vanilla persistence, and the Sync that last confirmed it. Weak
 // component references: a deleted entity leaves null, dropped on the next Sync.
 protected static ref array<SCR_EditableEntityComponent> s_Marked = {};
 protected static ref array<bool> s_Untracked = {};
 protected static ref array<int> s_Confirmed = {};
 // Whether this file set the NON_SERIALIZABLE flag. False for an entity that
 // already carried someone else's flag: it is still taken out of native
 // tracking (and handed back), but its flag is never cleared here.
 protected static ref array<bool> s_Flagged = {};
 protected static BaseWorld s_World;
 protected static float s_NextSync;
 protected static int s_SyncStamp;
 protected static bool s_SaveHooked;
 // Native StopTracking calls this file made that took an entity out of a save.
 protected static int s_Stopped;

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

 // Static records must not outlive their world; the entities went with it.
 protected static void CheckWorld()
 {
  if (!GetGame()) return;
  BaseWorld world = GetGame().GetWorld();
  if (world == s_World) return;
  s_World = world;
  s_Marked.Clear(); s_Untracked.Clear(); s_Confirmed.Clear(); s_Flagged.Clear();
  s_NextSync = 0; s_SyncStamp = 0; s_SaveHooked = false; s_Stopped = 0;
 }

 // The persistence system can come up after the first owned entity; Keep and
 // Sync retry until the hook is in. One hook per world.
 protected static void HookSaves()
 {
  if (s_SaveHooked || !s_World) return;
  SCR_PersistenceSystem persistence = SCR_PersistenceSystem.GetByCurrentWorld();
  if (!persistence) return;
  persistence.GetOnBeforeSave().Insert(OnPersistenceBeforeSave);
  s_SaveHooked = true;
 }

 // Vanilla 1.8 saves what IsTracked reports. StopTracking is called even when
 // IsTracked says no, as 0.1.5 did at creation: the editable's PLACEABLE
 // registration is lazy (SCR_EditableEntityComponent StartTracking(owner)) and
 // may not be reported yet, and stopping it then keeps it from completing.
 // True when this call took the entity out of native tracking (Release hands
 // it back); s_Stopped counts only entities IsTracked had reported.
 protected static bool StopNativeTracking(IEntity entity)
 {
  if (!entity) return false;
  SCR_PersistenceSystem persistence = SCR_PersistenceSystem.GetByEntityWorld(entity);
  if (!persistence) return false;
  bool tracked = persistence.IsTracked(entity);
  bool stopped = persistence.StopTracking(entity);
  if (persistence.IsTracked(entity)) return false;
  if (tracked && s_Stopped < 1000000000) s_Stopped++;
  return tracked || stopped;
 }

 // Right before each native save reads its data: refresh the ledgers' answer,
 // then nothing this file holds stays tracked, however late tracking began.
 // Bounded by MAX_MARKED; no world scan.
 protected static void OnPersistenceBeforeSave(ESaveGameType saveType)
 {
  if (!Replication.IsServer() || !GetGame()) return;
  CheckWorld();
  int before = s_Stopped;
  EAC_ResidentClaims claims = EAC_AmbientModule.GetMissionClaims();
  if (claims) claims.EAC_KeepSessionEntities();
  EAC_PedestrianSpawner spawner = EAC_AmbientModule.EAC_GetSessionSpawner();
  if (spawner) spawner.EAC_KeepSessionHelpers();
  EAC_TrafficDirector traffic = EAC_TrafficDirector.Get();
  if (traffic) traffic.EAC_KeepSessionEntities();
  int refused;
  for (int index = 0; index < s_Marked.Count(); index++)
  {
   SCR_EditableEntityComponent editable = s_Marked[index];
   if (!editable) continue;
   IEntity markedOwner = editable.GetOwner();
   if (!markedOwner) continue;
   if (StopNativeTracking(markedOwner)) s_Untracked[index] = true;
   SCR_PersistenceSystem persistence = SCR_PersistenceSystem.GetByEntityWorld(markedOwner);
   if (persistence && persistence.IsTracked(markedOwner)) refused++;
  }
  string type = typename.EnumToString(ESaveGameType, saveType);
  if (refused > 0) PrintFormat("[EAC] Save %1: %2 ambient entities refused to stop tracking", type, refused, level: LogLevel.WARNING);
  if (s_Stopped > before) PrintFormat("[EAC] Save %1: %2 late-tracked ambient entities kept out", type, s_Stopped - before);
 }

 // Exclude one owned transient entity from GM saves now. Called where owned
 // entities are created and, for every entity the ledgers still answer for, by
 // Sync. Cheap when already flagged: one native array search.
 static void Keep(IEntity entity)
 {
  if (!entity || !Replication.IsServer()) return;
  CheckWorld();
  HookSaves();
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(entity);
  // No editable: invisible to GM saves, but not to native persistence. Nothing
  // to remember it by, so it is untracked on every pass and never handed back.
  if (!editable) { StopNativeTracking(entity); return; }
  int index = s_Marked.Find(editable);
  if (index >= 0) { s_Confirmed[index] = s_SyncStamp; return; }
  if (s_Marked.Count() >= MAX_MARKED) { StopNativeTracking(entity); return; }
  // Someone else's flag already excludes it from GM saves and is never ours to
  // clear; native persistence still has to let it go.
  bool flagged = !editable.HasEntityFlag(EEditableEntityFlag.NON_SERIALIZABLE);
  if (flagged) editable.SetEntityFlag(EEditableEntityFlag.NON_SERIALIZABLE, true);
  bool untracked = StopNativeTracking(entity);
  s_Marked.Insert(editable); s_Untracked.Insert(untracked); s_Confirmed.Insert(s_SyncStamp); s_Flagged.Insert(flagged);
 }

 // One pass over the ledgers (bounded by them: <= 200 claims, 64 parties and
 // their helpers) once a second, from the module tick or, during a controller
 // gap, from the spawner's cleanup pump. Whatever the ledgers no longer answer
 // for loses the flag this file gave it.
 static void Sync(float now)
 {
  if (!Replication.IsServer() || !GetGame()) return;
  CheckWorld();
  HookSaves();
  if (now < s_NextSync) return;
  s_NextSync = now + SYNC_SECONDS;
  if (s_SyncStamp >= 1000000000) s_SyncStamp = 0;
  s_SyncStamp++;
  EAC_ResidentClaims claims = EAC_AmbientModule.GetMissionClaims();
  if (claims) claims.EAC_KeepSessionEntities();
  EAC_PedestrianSpawner spawner = EAC_AmbientModule.EAC_GetSessionSpawner();
  if (spawner) spawner.EAC_KeepSessionHelpers();
  EAC_TrafficDirector traffic = EAC_TrafficDirector.Get();
  if (traffic) traffic.EAC_KeepSessionEntities();
  // Backwards, so the unordered Remove only ever swaps in an entry already seen.
  for (int index = s_Marked.Count() - 1; index >= 0; index--)
  {
   if (s_Marked[index] && s_Confirmed[index] == s_SyncStamp) continue;
   Release(index);
  }
 }

 protected static void Release(int index)
 {
  SCR_EditableEntityComponent editable = s_Marked[index];
  if (editable)
  {
   if (s_Flagged[index]) editable.SetEntityFlag(EEditableEntityFlag.NON_SERIALIZABLE, false);
   IEntity owner = editable.GetOwner();
   if (owner && s_Untracked[index])
   {
    SCR_PersistenceSystem persistence = SCR_PersistenceSystem.GetByEntityWorld(owner);
    if (persistence) persistence.StartTracking(owner);
   }
  }
  s_Marked.Remove(index); s_Untracked.Remove(index); s_Confirmed.Remove(index); s_Flagged.Remove(index);
 }

 static int GetMarkedCount() { CheckWorld(); return s_Marked.Count(); }
 static int GetStoppedCount() { CheckWorld(); return s_Stopped; }
 static bool IsSaveHooked() { CheckWorld(); HookSaves(); return s_SaveHooked; }
}

modded class SCR_EditableEntityComponent
{
#ifdef EAC_LEGACY_SERIALIZE_HOOK
 override bool Serialize(out SCR_EditableEntityComponent outTarget = null, out int outTargetIndex = -1, out EEditableEntitySaveFlag outSaveFlags = 0)
 {
  if (EAC_SessionLifecycle.IsTransient(this)) return false;
  return super.Serialize(outTarget, outTargetIndex, outSaveFlags);
 }
#endif

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

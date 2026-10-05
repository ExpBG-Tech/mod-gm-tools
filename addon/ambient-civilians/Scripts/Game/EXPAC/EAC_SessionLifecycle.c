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
 protected static BaseWorld s_World;
 protected static float s_NextSync;
 protected static int s_SyncStamp;

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
  s_Marked.Clear(); s_Untracked.Clear(); s_Confirmed.Clear();
  s_NextSync = 0; s_SyncStamp = 0;
 }

 // Exclude one owned transient entity from GM saves now. Called where owned
 // entities are created and, for every entity the ledgers still answer for, by
 // Sync. Cheap when already flagged: one native array search.
 static void Keep(IEntity entity)
 {
  if (!entity || !Replication.IsServer()) return;
  CheckWorld();
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(entity);
  if (!editable) return;
  int index = s_Marked.Find(editable);
  if (index >= 0) { s_Confirmed[index] = s_SyncStamp; return; }
  // Someone else's flag: already excluded, and never ours to clear.
  if (editable.HasEntityFlag(EEditableEntityFlag.NON_SERIALIZABLE)) return;
  if (s_Marked.Count() >= MAX_MARKED) return;
  editable.SetEntityFlag(EEditableEntityFlag.NON_SERIALIZABLE, true);
  bool untracked;
  SCR_PersistenceSystem persistence = SCR_PersistenceSystem.GetByEntityWorld(entity);
  if (persistence) untracked = persistence.StopTracking(entity);
  s_Marked.Insert(editable); s_Untracked.Insert(untracked); s_Confirmed.Insert(s_SyncStamp);
 }

 // One pass over the ledgers (bounded by them: <= 200 claims, 64 parties and
 // their helpers) once a second, from the module tick or, during a controller
 // gap, from the spawner's cleanup pump. Whatever the ledgers no longer answer
 // for loses the flag this file gave it.
 static void Sync(float now)
 {
  if (!Replication.IsServer() || !GetGame()) return;
  CheckWorld();
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
   editable.SetEntityFlag(EEditableEntityFlag.NON_SERIALIZABLE, false);
   IEntity owner = editable.GetOwner();
   if (owner && s_Untracked[index])
   {
    SCR_PersistenceSystem persistence = SCR_PersistenceSystem.GetByEntityWorld(owner);
    if (persistence) persistence.StartTracking(owner);
   }
  }
  s_Marked.Remove(index); s_Untracked.Remove(index); s_Confirmed.Remove(index);
 }

 static int GetMarkedCount() { CheckWorld(); return s_Marked.Count(); }
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

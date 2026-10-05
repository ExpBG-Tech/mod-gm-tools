// Native Full Cache keeps the manager's logical ownership across entity replacement.
class EBG_CacheFullCoordinator
{
 static string Status(EBG_CacheGroup record)
 {
  if (record.Recovery != "") return "Full recovery required: " + record.Recovery;
  if (record.Full.GetState() == EBG_FullGroupPhase.CACHED) return "Full cached";
  if (record.Full.GetState() == EBG_FullGroupPhase.READY) return "Validating restored group";
  if (record.Full.GetState() == EBG_FullGroupPhase.RELEASING) return "Checking restored native AI activation";
  if (record.Full.GetState() == EBG_FullGroupPhase.RESTORING) return "Respawning surviving AI with default equipment";
  return record.Full.GetError();
 }
 static void ClearReservation(EBG_CacheGroup record)
 {
  record.Full = null;
  record.FullCleanup = null;
  record.FullGroupId = UUID.NULL_UUID;
  record.FullMemberIds.Clear();
  record.FullMembers.Clear();
  record.WakeRequested = false;
  record.RecoveryRetryRequested = false;
 }
 static bool Sleep(EBG_CacheManager manager, EBG_CacheGroup record)
 {
  if (record.Full || record.Simulation || !record.Group) return false;
  array<AIAgent> agents = {};
  record.Group.GetAgents(agents);
  array<EBG_CacheMember> bindings = {};
  array<UUID> ids = {};
  PersistenceSystem system = PersistenceSystem.GetInstance();
  foreach (AIAgent agent : agents)
  {
   EBG_CacheMember member = manager.FindMember(agent.GetControlledEntity());
   if (!member || !record.Members.Contains(member) || member.Dead || member.WasPlayer)
   { record.Reason = "Current survivor ownership changed";
   return false;
   }
   string unsupported = EBG_SimulationCache.Unsupported(member.Entity, false);
   if (!unsupported.IsEmpty()) { record.Reason = unsupported;
   return false;
   }
   UUID id;
   if (system) id = system.GetId(member.Entity);
   bindings.Insert(member);
   ids.Insert(id);
  }
  if (bindings.Count() != record.Alive || bindings.IsEmpty()) { record.Reason = "Current survivor roster is incomplete";
  return false;
  }
  string reason;
  if (!EBG_FullSaveGate.TryAcquire(manager, reason)) { record.Reason = reason;
  return false;
  }
  if (system) record.FullGroupId = system.GetId(record.Group);
  record.FullMembers.Copy(bindings);
  record.FullMemberIds.Copy(ids);
  EBG_PrefabFullCache transaction = new EBG_DurableFullCache();
  transaction.SetRecord(record);
  record.Full = transaction;
  if (transaction.BeginManagedSleep(record.Group))
  {
   record.ClearSince = -1;
   record.Reason = "Full cached: living unit prefabs recorded";
   return true;
  }
  reason = transaction.GetError();
  if (!transaction.HasDeletionAttempted())
  {
   ClearReservation(record);
   EBG_FullSaveGate.CancelUncommittedAcquire(manager);
  }
  else record.Recovery = reason;
  record.Reason = reason;
  return false;
 }
 static bool WantsWake(EBG_CacheManager manager, EBG_CacheGroup record)
 {
  EBG_CacheZone zone = record.Zone;
  if (record.WakeRequested || record.ReleaseRequested || !zone || !zone.Enabled || zone.Editing || zone.HasPendingSettings() || zone.Mode != 1) return true;
  return manager.IsProtected(record, false);
 }
 static float Priority(EBG_CacheManager manager, EBG_CacheGroup record)
 {
  if (record.ReleaseRequested || record.WakeRequested || !record.Zone) return -1;
  float distance = float.MAX;
  foreach (vector player : manager.Players) distance = Math.Min(distance, vector.DistanceSq(player, record.Anchor));
  return distance;
 }
 static bool BindAndRelease(EBG_CacheManager manager, EBG_CacheGroup record)
 {
  if (!record.Full.ReleaseRestored()) { record.Recovery = record.Full.GetError();
  return false;
  }
  record.Group = record.Full.GetRestoredGroup();
  if (record.Group && record.Zone && record.Zone.Editing)
  {
   // A fully cached group was absent when RestoreAllForSave requested this.
   SCR_EditableGroupComponent editable = SCR_EditableGroupComponent.Cast(SCR_EditableEntityComponent.GetEditableEntity(record.Group));
   if (editable) editable.EBG_UseWaypointOrderForSave();
  }
  return true;
 }
 static void CompleteRelease(EBG_CacheManager manager, EBG_CacheGroup record)
 {
  ClearReservation(record);
  record.Recovery = "";
  record.Reason = "Surviving AI respawned; default kit; native group and supported orders recreated";
  record.ActiveSince = manager.Now();
  record.ClearSince = -1;
 }
 static bool Tick(EBG_CacheManager manager)
 {
  EBG_CacheGroup pending;
  bool hasCandidate;
  foreach (EBG_CacheGroup current : manager.Records)
  {
   if (!current.Full) { continue; }
   int state = current.Full.GetState();
   if (state == EBG_FullGroupPhase.FAILED && !current.RecoveryRetryRequested)
   {
    current.Recovery = current.Full.GetError();
   }
   if (!pending && (state == EBG_FullGroupPhase.RELEASED || state == EBG_FullGroupPhase.RESTORING || state == EBG_FullGroupPhase.RELEASING))
   {
    pending = current;
   }
   if (current.Recovery == "") { hasCandidate = true; }
  }
  // Finish in-flight work before scanning proximity or choosing another group.
  if (pending)
  {
   if (pending.Full.GetState() == EBG_FullGroupPhase.RELEASED)
   {
    CompleteRelease(manager, pending);
    return true;
   }
   pending.Full.Poll();
   if (pending.Full.GetState() == EBG_FullGroupPhase.FAILED) pending.Recovery = pending.Full.GetError();
   if (pending.Full.GetState() == EBG_FullGroupPhase.READY) BindAndRelease(manager, pending);
   if (pending.Full.GetState() == EBG_FullGroupPhase.RELEASED) CompleteRelease(manager, pending);
   return true;
  }
  if (!hasCandidate) { return false; }
  EBG_CacheGroup selected;
  float nearest = float.MAX;
  foreach (EBG_CacheGroup record : manager.Records)
  {
   if (!record.Full || record.Recovery != "") continue;
   if (record.Full.GetState() == EBG_FullGroupPhase.READY) { BindAndRelease(manager, record);
   return true;
   }
   if (!WantsWake(manager, record)) continue;
   float score = Priority(manager, record);
   if (!selected || score < nearest) { selected = record;
   nearest = score;
   }
  }
  if (!selected || EBG_FullCacheGroup.IsNativeOperationBusy()) return false;
  selected.RecoveryRetryRequested = false;
  if (!selected.Full.BeginWake()) selected.Recovery = selected.Full.GetError();
  else selected.Full.Poll();
  return true;
 }
}

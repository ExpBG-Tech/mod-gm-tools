// Full Cache replaces living AI with their prefab defaults. It never replenishes a roster.
// Per-survivor state other modules carry across one Full cycle. Unit Caching
// itself carries nothing here: a module adds its own fields and extends Capture
// and Apply through a modded class (Unit Dialog does). Server session memory
// only; never written to native or portable (CDF) snapshots, so a survivor
// imported from a portable snapshot carries nothing.
class EBG_SurvivorCarry
{
 // Server, before the living survivor is deleted. Read only: a refused capture
 // must leave the original untouched.
 void Capture(SCR_ChimeraCharacter entity) {}
 // Server, once, right after the survivor was respawned from its prefab and
 // before it joins the restored group or streams to any client.
 void Apply(SCR_ChimeraCharacter entity) {}
 // Portable (CDF) Full snapshot, inside the survivor's object: optional keys, so a
 // snapshot written before a module carried state still loads. A carry that does not
 // read back is dropped with a warning; it never fails the snapshot.
 bool Write(SaveContext context) { return true; }
 void Read(LoadContext context) {}
}
class EBG_PrefabSurvivor
{
 EBG_CacheMember Member;
 SCR_ChimeraCharacter Entity;
 ResourceName Prefab;
 vector Transform[4];
 UUID OriginalId;
 bool Created;
 ref EBG_StaticEmplacement Emplacement;
 bool MountRequested;
 float MountDeadline;
 // Operator release only: this row is left where it is and never spawned again.
 bool Abandoned;
 ref EBG_CacheAuthor Author = new EBG_CacheAuthor();
 ref EBG_SurvivorCarry Carry = new EBG_SurvivorCarry();
}

modded class SCR_AIGroup
{
 bool EBG_PrefabCacheHeld;
 void EBG_UseCapturedRoster()
 {
  // Native queued expansion ignores the ordinary spawning limits. Recreated
  // groups derive their sole roster from captured survivors, never prefab slots.
  m_aUnitPrefabSlots = new array<ResourceName>();
  ChimeraAIWorld aiWorld = ChimeraAIWorld.Cast(GetGame().GetAIWorld());
  if (aiWorld) aiWorld.PurgeSpawnRequestsForGroup(this);
 }
 override void OnEmpty()
 {
  // Temporary empty membership must not signal elimination to mission listeners.
  if (EBG_PrefabCacheHeld) return;
  super.OnEmpty();
 }
}

class EBG_PrefabFullCache : EBG_FullCacheGroup
{
 protected EBG_CacheGroup m_Record;
 protected SCR_AIGroup m_Group;
 protected ref EBG_SimulationAgent m_GroupPolicy;
 protected ref array<ref EBG_PrefabSurvivor> m_Survivors = {};
 protected EBG_FullGroupPhase m_State;
 protected string m_Problem;
 protected bool m_Deleted;
 protected bool m_DeleteEmpty;
 protected bool m_DeleteNoPlayer;
 protected int m_PrefabDormantAlive;
 protected int m_PrefabDormantDead;
 protected ref EBG_CacheGroupSnapshot m_Snapshot;
 protected bool m_GroupCreated;
 protected bool m_GroupSettingsApplied;
 // Operator escape for a blocked wake ("Release blocked groups"). Never automatic.
 // It never spawns a created row twice, refills casualties or deletes AI; it only
 // stops waiting for the seat, waypoint or ownership check that blocked the wake.
 protected bool m_Lenient;
 protected bool m_LenientAbandoned;
 protected bool m_LenientOrdersSkipped;
 void BeginLenientRelease() { m_Lenient = true; }
 bool IsLenient() { return m_Lenient; }
 bool IsLenientAbandoned() { return m_LenientAbandoned; }
 string LenientSummary()
 {
  int kept, skipped, lost;
  foreach (EBG_PrefabSurvivor row : m_Survivors)
  {
   if (!row.Member || row.Member.Dead) continue;
   if (row.Abandoned && row.Entity) skipped++;
   else if (row.Entity) kept++;
   else if (!row.Member.WasPlayer) lost++;
  }
  return string.Format("kept=%1 leftInPlace=%2 missing=%3 ordersSkipped=%4 groupGone=%5", kept, skipped, lost, m_LenientOrdersSkipped, m_LenientAbandoned);
 }
 // No native group can take the survivors: hand back what exists and forget the
 // rest of the snapshot. Nothing is spawned or deleted here.
 protected void AbandonLenient()
 {
  if (m_Group)
  {
   if (m_GroupPolicy) m_GroupPolicy.Restore();
   m_Group.EBG_PrefabCacheHeld = false;
   m_Group.SetDeleteWhenEmpty(m_DeleteEmpty);
   m_Group.SetCanDeleteIfNoPlayer(m_DeleteNoPlayer);
  }
  if (m_Record) m_Record.Group = m_Group;
  m_LenientAbandoned = true;
  m_Problem = "";
  m_State = EBG_FullGroupPhase.RELEASED;
 }
 // Standalone adapters retain their native group and its private settings/orders.
 // GM-managed Full explicitly opts into portable snapshot/group replacement.
 protected bool CapturesNativeGroup()
 {
  return false;
 }
 // Seam for owners of a transaction: called in the spawning call, right after a survivor
 // row has its entity (author and carried state applied), before it joins the group.
 protected void OnSurvivorSpawned(EBG_PrefabSurvivor row) {}
 static bool CanDeleteFullEntity(IEntity entity)
 {
  if (!entity) return false;
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(entity);
  return !editable || !editable.HasEntityFlag(EEditableEntityFlag.NON_DELETABLE);
 }
 protected static bool DeleteFullEntity(IEntity entity)
 {
  if (!entity) return true;
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(entity);
  // Native Delete(false,false) retains RPC deletion and balances authored
  // ownership. Its arguments disable user-dirtying and navmesh rebuilding.
  if (editable) return editable.Delete(false, false);
  SCR_EntityHelper.DeleteEntityAndChildren(entity);
  return true;
 }

 void SetRecord(EBG_CacheGroup record)
 {
  m_Record = record;
 }
 override EBG_FullGroupPhase GetState()
 {
  return m_State;
 }
 override string GetError()
 {
  return m_Problem;
 }
 override bool IsPending()
 {
  return m_State == EBG_FullGroupPhase.RESTORING;
 }
 override int GetMemberCount()
 {
  return m_Survivors.Count();
 }
 override bool HasDeletionAttempted()
 {
  return m_Deleted;
 }
 override bool AreOriginalsRecovered()
 {
  return !m_Deleted;
 }
 override SCR_AIGroup GetRestoredGroup()
 {
  return m_Group;
 }
 override SCR_ChimeraCharacter GetRestoredMember(int index)
 {
  if (index < 0 || index >= m_Survivors.Count()) return null;
  return m_Survivors[index].Entity;
 }
 override UUID GetNativeId(int index)
 {
  if (index < 0 || index >= m_Survivors.Count()) return UUID.NULL_UUID;
  return m_Survivors[index].OriginalId;
 }
 EBG_SurvivorCarry GetSurvivorCarry(int index)
 {
  if (index < 0 || index >= m_Survivors.Count()) return null;
  return m_Survivors[index].Carry;
 }
 override bool HasReservedUUID(UUID id)
 {
  if (id.IsNull()) return false;
  foreach (EBG_PrefabSurvivor row : m_Survivors) if (row.OriginalId == id) return true;
  return false;
 }
 protected bool Refuse(string reason)
 {
  m_Problem = reason;
  m_State = EBG_FullGroupPhase.FAILED;
  return false;
 }
 override bool BeginManagedSleep(SCR_AIGroup group)
 {
  if (!Replication.IsServer() || !group || !m_Record || m_State != EBG_FullGroupPhase.NEW) return Refuse("Full prefab capture requires a new authority transaction");
  if (group.GetPlayerCount() || group.GetPermanentLOD() >= 0 || group.GetLifecyclePolicy() == SCR_EAIGroupLifecyclePolicy.ProximityDriven) return Refuse("Group has external or player ownership");
  if (CapturesNativeGroup())
  {
   if (!CanDeleteFullEntity(group)) return Refuse("Native editor protects this group from deletion");
   m_Snapshot = new EBG_CacheGroupSnapshot();
   m_Snapshot.Token = m_Record.Id;
   m_Snapshot.Dead = m_Record.Dead;
   if (!m_Snapshot.CaptureGroup(group)) return Refuse(m_Snapshot.CaptureProblem);
  }
  PersistenceSystem persistence = PersistenceSystem.GetInstance();
  foreach (EBG_CacheMember member : m_Record.FullMembers)
  {
   if (!member || member.Dead || member.WasPlayer || !member.Entity || member.Entity.GetCharacterGroup() != group) return Refuse("Survivor roster changed before capture");
   if (!CanDeleteFullEntity(member.Entity)) return Refuse("Native editor protects a survivor from deletion");
   string unsafe = EBG_SimulationCache.Unsupported(member.Entity, false);
   if (!unsafe.IsEmpty()) return Refuse(unsafe);
   EBG_PrefabSurvivor row = new EBG_PrefabSurvivor();
   row.Member = member;
   row.Entity = member.Entity;
   row.Prefab = SCR_ResourceNameUtils.GetPrefabName(member.Entity);
   Resource resource = Resource.Load(row.Prefab);
   if (!resource || !resource.IsValid()) return Refuse("Survivor prefab is unavailable");
   row.Emplacement = new EBG_StaticEmplacement();
   if (!row.Emplacement.Capture(member.Entity)) return Refuse("Static emplacement changed during Full capture");
   member.Entity.GetWorldTransform(row.Transform);
   row.Author.Capture(member.Entity);
   row.Carry.Capture(member.Entity);
   if (persistence) row.OriginalId = persistence.GetId(member.Entity);
   m_Survivors.Insert(row);
  }
  if (m_Survivors.IsEmpty() || group.GetAgentsCount() != m_Survivors.Count()) return Refuse("Survivor roster is incomplete");
  SCR_EditableEntityComponent groupEditable = SCR_EditableEntityComponent.GetEditableEntity(group);
  if (m_Snapshot && groupEditable)
  {
   set<SCR_EditableEntityComponent> children = new set<SCR_EditableEntityComponent>();
   groupEditable.GetChildren(children, true);
   foreach (SCR_EditableEntityComponent child : children)
   {
    bool capturedChild = false;
    if (child && child.GetParentEntity() == groupEditable)
    {
     foreach (EBG_PrefabSurvivor ownedSurvivor : m_Survivors) if (child.GetOwner() == ownedSurvivor.Entity) capturedChild = true;
     foreach (EBG_CacheOrder ownedOrder : m_Snapshot.Orders) if (child.GetOwner() == ownedOrder.Entity) capturedChild = true;
    }
    // Native editable group deletion cascades to remaining hierarchy children.
    // Admit only entities this transaction has explicitly captured and owns.
    if (!capturedChild) return Refuse("Full capture holds unknown, shared or dead editor group child");
   }
  }
  m_Group = group;
  m_GroupSettingsApplied = true;
  m_DeleteEmpty = group.EBG_NativeDeleteWhenEmpty();
  m_DeleteNoPlayer = group.GetDeleteIfNoPlayer();
  m_PrefabDormantAlive = group.GetDormantAliveCount();
  m_PrefabDormantDead = group.GetDormantDeadCount();
  m_GroupPolicy = new EBG_SimulationAgent();
  m_GroupPolicy.Capture(group);
  group.EBG_PrefabCacheHeld = true;
  group.SetDeleteWhenEmpty(false);
  group.SetCanDeleteIfNoPlayer(false);
  m_GroupPolicy.Suspend();
  foreach (EBG_PrefabSurvivor survivor : m_Survivors)
  {
   // Discard only the soon-to-be-deleted living inventory's ownership entries.
   // Existing corpses, drops and equipment transferred to players stay untouched.
   EBG_CacheCleanup.Get().ForgetPrefabMember(m_Record, survivor.Member);
   m_Deleted = true;
   if (!DeleteFullEntity(survivor.Entity)) return Refuse("Native survivor deletion was refused; snapshot retained");
   if (survivor.Entity) return Refuse("Full deletion was not acknowledged; restore retained survivors");
  }
  if (!m_Group || m_Group.GetAgentsCount() != 0) return Refuse("Group changed during Full capture");
  if (!m_Snapshot)
  {
   m_State = EBG_FullGroupPhase.CACHED;
   return true;
  }
  // Native removal callbacks can change orders even within this synchronous
  // transaction. Keep recovery state rather than deleting newly shared orders.
  if (!m_Snapshot.OwnsOrders(m_Group)) return Refuse("Waypoint ownership changed during Full capture; snapshot retained");
  m_Snapshot.Detach(m_Group);
  // Editor cycles may retain repeated queue entries when detached. The new
  // native Defend path never cycles; keep that revalidation scoped accordingly.
  if (!m_Snapshot.Cycled && !m_Snapshot.OwnsOrders(m_Group)) return Refuse("Waypoint ownership changed while detaching Full orders; snapshot retained");
  foreach (EBG_CacheOrder cachedOrder : m_Snapshot.Orders)
  {
   if (!DeleteFullEntity(cachedOrder.Entity)) return Refuse("Native waypoint deletion was refused; snapshot retained");
   if (cachedOrder.Entity) return Refuse("Waypoint deletion was not acknowledged; snapshot retained");
  }
  if (!DeleteFullEntity(m_Group)) return Refuse("Native group deletion was refused; snapshot retained");
  if (m_Group) return Refuse("Native group deletion was not acknowledged; snapshot retained");
  m_Record.Group = null;
  m_GroupSettingsApplied = false;
  m_GroupPolicy.Agent = null;
  m_State = EBG_FullGroupPhase.CACHED;
  return true;
 }
 override bool BeginWake()
 {
  if (!Replication.IsServer() || !m_Record) return Refuse("Full wake requires its authority record");
  if (!m_Snapshot && !m_Group)
  {
   if (m_Lenient && m_State == EBG_FullGroupPhase.FAILED) { AbandonLenient(); return true; }
   return Refuse("Original group no longer exists; cached survivors retained");
  }
  if (m_State != EBG_FullGroupPhase.CACHED && m_State != EBG_FullGroupPhase.FAILED) return false;
  if (m_State == EBG_FullGroupPhase.FAILED)
  foreach (EBG_PrefabSurvivor row : m_Survivors)
  {
   if (!row.Created || row.Member.Dead || row.Member.WasPlayer || !row.Emplacement || !row.Emplacement.HadMount || row.Emplacement.Matches(row.Entity)) continue;
   // A fresh recovery gets a fresh mount attempt, never a fresh survivor.
   row.MountDeadline = 0;
   row.MountRequested = false;
  }
  m_Problem = "";
  m_State = EBG_FullGroupPhase.RESTORING;
  return true;
 }
 override void Poll()
 {
  if (m_State != EBG_FullGroupPhase.RESTORING) return;
  if (!m_Record || (!m_Snapshot && !m_Group))
  {
   if (m_Lenient && m_Record) { AbandonLenient(); return; }
   Refuse("Logical group disappeared during restore");
   return;
  }
  if (!m_Group)
  {
   // Once a new group was bound, external deletion is a recovery hold. It is
   // never permission to spawn a second group or refill already created slots.
   if (m_GroupCreated)
   {
    if (m_Lenient) { AbandonLenient(); return; }
    Refuse("Restored group was removed externally");
    return;
   }
   EntitySpawnParams groupParams = new EntitySpawnParams();
   groupParams.TransformMode = ETransformMode.WORLD;
   m_Snapshot.Transform(groupParams.Transform);
   bool ignoredSpawning = SCR_AIGroup.EBG_NativeSpawningIgnored();
   bool ignoredTerrain = SCR_AIGroup.EBG_SnapshotIgnoresTerrain();
   SCR_AIGroup.IgnoreSpawning(true);
   SCR_AIGroup.IgnoreSnapToTerrain(true);
   // Keep the loaded resource alive through the spawn call.
   Resource groupResource = Resource.Load(m_Snapshot.Prefab);
   m_Group = SCR_AIGroup.Cast(GetGame().SpawnEntityPrefab(groupResource, GetGame().GetWorld(), groupParams));
   SCR_AIGroup.IgnoreSpawning(ignoredSpawning);
   SCR_AIGroup.IgnoreSnapToTerrain(ignoredTerrain);
   if (!m_Group)
   {
    if (m_Lenient) { AbandonLenient(); return; }
    Refuse("Group prefab spawn failed; snapshot retained for retry");
    return;
   }
   // The newly spawned empty group must remain owned during the bounded pause
   // before its settings are applied and the first survivor is admitted.
   m_Group.EBG_UseCapturedRoster();
   m_Group.EBG_PrefabCacheHeld = true;
   m_Group.SetDeleteWhenEmpty(false);
   m_Group.SetCanDeleteIfNoPlayer(false);
   m_GroupCreated = true;
   m_Record.Group = m_Group;
   m_Group.SetSpawnImmediately(false);
   m_Group.SetNumberOfMembersToSpawn(0);
   m_Group.SetMaxUnitsToSpawn(0);
   if (!m_GroupPolicy) m_GroupPolicy = new EBG_SimulationAgent();
   m_GroupPolicy.Agent = m_Group;
   m_GroupPolicy.PermanentLOD = m_Snapshot.Settings[16];
   m_GroupPolicy.LOD = m_Snapshot.Settings[17];
   m_GroupPolicy.Active = m_Snapshot.Settings[18];
   m_GroupPolicy.Suspend();
   PersistenceSystem groupPersistence = PersistenceSystem.GetInstance();
   m_Record.PersistentId = UUID.NULL_UUID;
   if (groupPersistence) m_Record.PersistentId = groupPersistence.GetId(m_Group);
   return;
  }
  if (!m_GroupSettingsApplied)
  {
   // A failed setter is retried before any survivor is admitted. A retained
   // failed native group is never mistaken for a fully configured group.
   if (m_Group.GetAgentsCount() != 0 || !m_Snapshot.Apply(m_Group))
   {
    // Released by the operator: keep the group with whatever settings it took.
    if (!m_Lenient)
    {
     Refuse("New native group settings or empty roster could not be confirmed");
     return;
    }
   }
   m_GroupSettingsApplied = true;
  }
  foreach (EBG_PrefabSurvivor row : m_Survivors)
  {
   // A spawned survivor killed/deleted during wake is never spawned a second time.
   if (row.Member.Dead || row.Member.WasPlayer || row.Abandoned) continue;
   if (row.Created)
   {
    // Check ownership before a native remount can move a survivor reassigned by GM.
    if (!row.Entity || row.Member.Entity != row.Entity || row.Entity.GetCharacterGroup() != m_Group)
    {
     // Released by the operator: a removed or reassigned survivor stays where it is.
     if (m_Lenient) { row.Abandoned = true; continue; }
     Refuse("Restored survivor was externally removed, rebound or regrouped");
     return;
    }
    // Released by the operator: accept the survivor's current compartment state.
    if (!m_Lenient && !row.Emplacement.Matches(row.Entity))
    {
     float now = EBG_CacheManager.Get().Now();
     bool mountTimedOut = row.MountDeadline > 0 && now >= row.MountDeadline;
     if (row.Emplacement.RestoreOnFootIfUnavailable(row.Entity, m_Group, mountTimedOut))
     {
      row.MountRequested = false;
      row.MountDeadline = 0;
      if (m_Record.Zone && m_Record.Zone.DebugMessages > 0) PrintFormat("[EXPBG] group=%1 member=%2 static seat unavailable or timed out; survivor returned on foot", m_Record.Id, row.Member.Id);
      continue;
     }
     if (row.MountDeadline == 0) row.MountDeadline = now + 10;
     if (now >= row.MountDeadline)
     {
      Refuse("Survivor compartment state did not settle; survivor retained | " + row.Emplacement.RestoreState(row.Entity));
      return;
     }
     if (!row.MountRequested)
     {
      string mountReason;
      row.MountRequested = row.Emplacement.RequestMount(row.Entity, mountReason);
      if (!row.MountRequested && !mountReason.Contains("pending"))
      {
       Refuse(mountReason + " | " + row.Emplacement.RestoreState(row.Entity));
       return;
      }
     }
     return;
    }
    continue;
   }
   // Only a fresh prefab respawn takes carried module state; an original
   // retained after a refused deletion still holds its own.
   bool respawned = !row.Entity;
   if (!row.Entity)
   {
    EntitySpawnParams params = new EntitySpawnParams();
    params.TransformMode = ETransformMode.WORLD;
    params.Transform = row.Transform;
    // Keep the loaded resource alive through the spawn call.
    Resource survivorResource = Resource.Load(row.Prefab);
    row.Entity = SCR_ChimeraCharacter.Cast(GetGame().SpawnEntityPrefab(survivorResource, GetGame().GetWorld(), params));
    if (!row.Entity)
    {
     // Released by the operator: this survivor is not respawned; one attempt per call.
     if (m_Lenient) { row.Abandoned = true; return; }
     Refuse("Survivor prefab spawn failed; missing slot retained for retry");
     return;
    }
   }
   row.Created = true;
   row.Member.Entity = row.Entity;
   row.Member.Position = row.Entity.GetOrigin();
   row.Member.Missing = false;
   row.Author.Apply(row.Entity);
   if (respawned) row.Carry.Apply(row.Entity);
   // Same call as the spawn, before the survivor joins the group: an owner may pin its AI.
   OnSurvivorSpawned(row);
   PersistenceSystem persistence = PersistenceSystem.GetInstance();
   row.Member.PersistentId = UUID.NULL_UUID;
   if (persistence) row.Member.PersistentId = persistence.GetId(row.Entity);
   EBG_CacheCleanup.Get().RegisterPrefabMember(m_Record, row.Member);
   if (!m_Group.AddAIEntityToGroup(row.Entity) || row.Entity.GetCharacterGroup() != m_Group)
   {
    if (m_Lenient) { row.Abandoned = true; return; }
    Refuse("Spawned survivor could not join restored group; entity retained");
    return;
   }
   FactionAffiliationComponent faction = FactionAffiliationComponent.Cast(row.Entity.FindComponent(FactionAffiliationComponent));
   if (faction && m_Group.GetFaction()) faction.SetAffiliatedFaction(m_Group.GetFaction());
   return;
   // Bound spawn cost to one survivor per coordinator tick.
  }
  if (m_Snapshot && !m_LenientOrdersSkipped)
  {
   if (!m_Snapshot.RestoreOrders(m_Group))
   {
    // Released by the operator: keep the orders that exist and stop waiting.
    if (!m_Lenient)
    {
     Refuse("Supported group orders could not be restored; snapshot retained");
     return;
    }
    m_LenientOrdersSkipped = true;
   }
   else if (!m_Snapshot.OrdersBound) { return; }
  }
  m_State = EBG_FullGroupPhase.READY;
 }
 override bool ReleaseRestored()
 {
  if (m_State != EBG_FullGroupPhase.READY || !m_Group) return false;
  if (!m_Lenient && m_Snapshot && !m_Snapshot.OwnsOrders(m_Group)) return Refuse("Restored waypoint ownership changed before release; snapshot retained");
  array<AIAgent> nativeRoster = {};
  m_Group.GetAgents(nativeRoster);
  foreach (AIAgent agent : nativeRoster)
  {
   bool known = false;
   foreach (EBG_PrefabSurvivor survivor : m_Survivors)
   if (agent && survivor.Entity && agent.GetControlledEntity() == survivor.Entity)
   {
    known = true;
    break;
   }
   if (!known && !m_Lenient)
   {
    IEntity controlled = null;
    if (agent) controlled = agent.GetControlledEntity();
    PrintFormat("[EBG Full ownership hold] agent=%1 controlled=%2 nativeCount=%3 survivors=%4", agent, controlled, nativeRoster.Count(), m_Survivors.Count());
    return Refuse("Restored group contains a foreign agent; snapshot retained without transfer");
   }
  }
  int newDeaths;
  foreach (EBG_PrefabSurvivor row : m_Survivors)
  {
   if (row.Abandoned || (row.Member.WasPlayer && !row.Member.Dead)) continue;
   if (!row.Member.Dead && row.Entity)
   {
    if (row.Member.Entity != row.Entity)
    {
     if (m_Lenient) continue;
     return Refuse("Restored survivor identity changed during wake");
    }
    CharacterControllerComponent controller = row.Entity.GetCharacterController();
    if (controller && controller.IsDead()) EBG_CacheManager.Get().ConfirmDeath(row.Entity);
   }
   if (row.Member.Dead)
   {
    newDeaths++;
    continue;
   }
   // Released by the operator: survivors that left, changed seat or state stay as they are.
   if (m_Lenient) continue;
   if (!row.Entity || row.Entity.GetCharacterGroup() != m_Group) return Refuse("Restored survivor was externally removed or regrouped");
   if (!row.Emplacement.Matches(row.Entity) && !row.Emplacement.RestoreOnFootIfUnavailable(row.Entity, m_Group)) return Refuse("Restored static seat changed before group release");
   if (!row.Entity || !row.Entity.GetCharacterController() || row.Entity.GetCharacterController().IsDead()) return Refuse("Restored survivor life state requires recovery");
  }
  // Native deletion counts our temporary removals as deaths. Remove that
  // bookkeeping only after recovery, preserving actual casualties during wake.
  int restoredAlive = m_PrefabDormantAlive;
  if (restoredAlive >= 0) restoredAlive = Math.Max(0, restoredAlive - newDeaths);
  int restoredDead = m_PrefabDormantDead + newDeaths;
  m_Group.SetDormantCounts(restoredAlive, restoredDead);
  if (!m_Lenient && (m_Group.GetDormantAliveCount() != restoredAlive || m_Group.GetDormantDeadCount() != restoredDead)) return Refuse("Native group casualty counts were not restored");
  m_GroupPolicy.Restore();
  m_Group.EBG_PrefabCacheHeld = false;
  m_Group.SetDeleteWhenEmpty(m_DeleteEmpty);
  m_Group.SetCanDeleteIfNoPlayer(m_DeleteNoPlayer);
  m_State = EBG_FullGroupPhase.RELEASED;
  if (m_Group.GetAgentsCount() == 0) m_Group.OnEmpty();
  return true;
 }
 override void ReleaseForWorldCleanup()
 {
  m_Record = null;
  m_Group = null;
  m_GroupPolicy = null;
  m_Survivors.Clear();
  m_State = EBG_FullGroupPhase.FAILED;
 }
 // A survivor was captured in a static weapon seat: CDF cannot export this cache.
 bool HasMountedSurvivor()
 {
  foreach (EBG_PrefabSurvivor row : m_Survivors)
  {
   if (row && row.Emplacement && row.Emplacement.HadMount)
   {
    return true;
   }
  }
  return false;
 }
 bool CanExport(out string reason)
 {
  reason = "Standalone retained-group caches require restoration before saving";
  if (!CapturesNativeGroup()) { return false; }
  reason = "Full transition or recovery is pending";
  if (m_State != EBG_FullGroupPhase.CACHED || !m_Snapshot || m_Group || !m_Record || !m_Snapshot.ValidGroup()) return false;
  foreach (EBG_PrefabSurvivor row : m_Survivors)
  {
   if (row.Created || !row.Member || row.Member.WasPlayer || row.Member.Dead || row.Entity) return false;
   EBG_CachePose pose = new EBG_CachePose();
   pose.Prefab = row.Prefab;
   for (int axis = 0; axis < 4; axis++) pose.Matrix.Insert(row.Transform[axis]);
   if (!pose.Valid() || !pose.PrefabType(SCR_ChimeraCharacter))
   {
    reason = "A cached survivor prefab or transform is unavailable for portable export";
    return false;
   }
   if (row.Emplacement && row.Emplacement.HadMount)
   {
    reason = "Mounted Full survivors require restoration before portable export";
    return false;
   }
  }
  if (m_Survivors.IsEmpty() || m_Survivors.Count() != m_Record.Alive) return false;
  reason = "";
  return true;
 }
 int SnapshotToken()
 {
  return m_Snapshot.Token;
 }
 bool WriteSnapshot(SaveContext context)
 {
  string reason;
  if (!CanExport(reason) || !m_Snapshot.Write(context) || !context.WriteValue("survivors", m_Survivors.Count())) return false;
  for (int i = 0; i < m_Survivors.Count(); i++)
  {
   EBG_PrefabSurvivor row = m_Survivors[i];
   array<vector> matrix = {};
   for (int axis = 0; axis < 4; axis++) matrix.Insert(row.Transform[axis]);
   if (!context.StartObject("survivor" + i.ToString()) || !context.WriteValue("prefab", row.Prefab) || !context.WriteValue("matrix", matrix) || !row.Author.Write(context) || !row.Carry.Write(context) || !context.EndObject()) return false;
  }
  return true;
 }
 bool ReadSnapshot(LoadContext context)
 {
  if (!CapturesNativeGroup() || m_State != EBG_FullGroupPhase.NEW) { return false; }
  m_Snapshot = new EBG_CacheGroupSnapshot();
  int count;
  if (!m_Snapshot.Read(context) || !context.ReadValue("survivors", count) || count < 1 || count > 128 || count + m_Snapshot.Dead > 128) return false;
  for (int i = 0; i < count; i++)
  {
   EBG_CachePose pose = new EBG_CachePose();
   EBG_PrefabSurvivor row = new EBG_PrefabSurvivor();
   if (!context.StartObject("survivor" + i.ToString()) || !pose.Read(context) || !pose.PrefabType(SCR_ChimeraCharacter) || !row.Author.Read(context)) return false;
   row.Carry.Read(context);
   if (!context.EndObject()) return false;
   row.Prefab = pose.Prefab;
   pose.Transform(row.Transform);
   row.Emplacement = new EBG_StaticEmplacement();
   m_Survivors.Insert(row);
  }
  m_DeleteEmpty = m_Snapshot.Settings[0];
  m_DeleteNoPlayer = m_Snapshot.Settings[1];
  m_PrefabDormantAlive = m_Snapshot.Settings[2];
  m_PrefabDormantDead = m_Snapshot.Settings[3];
  m_Deleted = true;
  m_State = EBG_FullGroupPhase.CACHED;
  return true;
 }
 void Import(EBG_CacheManager manager, EBG_CacheZone zone)
 {
  m_Record = manager.AddRegroupRecord(zone, null);
  m_Snapshot.Token = m_Record.Id;
  m_Record.Full = this;
  m_Record.Anchor = m_Snapshot.Matrix[3];
  m_Record.MinY = float.MAX;
  m_Record.MaxY = -float.MAX;
  foreach (EBG_PrefabSurvivor row : m_Survivors)
  {
   row.Member = manager.AddCachedMember(m_Record, row.Transform[3], false);
   m_Record.FullMembers.Insert(row.Member);
   m_Record.FullMemberIds.Insert(UUID.NULL_UUID);
   vector position = row.Transform[3];
   m_Record.MinY = Math.Min(m_Record.MinY, position[1]);
   m_Record.MaxY = Math.Max(m_Record.MaxY, position[1]);
  }
  for (int casualty = 0; casualty < m_Snapshot.Dead; casualty++) manager.AddCachedMember(m_Record, m_Record.Anchor, true);
  m_Record.Alive = m_Survivors.Count();
  m_Record.Dead = m_Snapshot.Dead;
  m_Record.Reason = "Portable Full snapshot loaded; native group remains absent";
 }
}

// The GM coordinator and portable reader own complete native group replacement.
class EBG_DurableFullCache : EBG_PrefabFullCache
{
 override protected bool CapturesNativeGroup()
 {
  return true;
 }
}

// Automatic ownership repair uses authoritative native rosters, never proximity
// as evidence of membership. Existing snapshots and member ledgers remain owned
// until a complete connected component can commit in one server callback.
class EBG_RegroupPlan
{
 ref array<EBG_CacheGroup> Records = {};
 ref array<SCR_AIGroup> Groups = {};
 ref array<SCR_ChimeraCharacter> Entities = {};
 ref array<SCR_AIGroup> Parents = {};
 ref array<EBG_CacheMember> Removed = {};
 EBG_CacheZone Zone;
 float Observed;
 string Problem;
 bool Same(EBG_RegroupPlan other)
 {
  if (!other || Problem != "" || other.Problem != "" || Zone != other.Zone || Records.Count() != other.Records.Count() || Groups.Count() != other.Groups.Count() || Entities.Count() != other.Entities.Count()) return false;
  foreach (EBG_CacheGroup record : Records) if (!other.Records.Contains(record)) return false;
  foreach (SCR_AIGroup group : Groups) if (!other.Groups.Contains(group)) return false;
  if (Removed.Count() != other.Removed.Count()) return false;
  foreach (EBG_CacheMember missing : Removed) if (!other.Removed.Contains(missing)) return false;
  for (int i = 0; i < Entities.Count(); i++)
  {
   int index = other.Entities.Find(Entities[i]);
   if (index < 0 || Parents[i] != other.Parents[index]) return false;
  }
  return true;
 }
 void Hold(string reason)
 {
  foreach (EBG_CacheGroup record : Records) if (record) record.RegroupReason = reason;
 }
}
class EBG_CacheRegroup
{
 protected ref array<ref EBG_RegroupPlan> m_Plans = {};
 protected int m_Cursor;
 bool ReservesGroup(SCR_AIGroup group)
 {
  if (!group) return false;
  foreach (EBG_RegroupPlan plan : m_Plans) if (plan.Groups.Contains(group)) return true;
  return false;
 }
 bool ReservesMember(SCR_ChimeraCharacter member)
 {
  foreach (EBG_RegroupPlan plan : m_Plans) if (plan.Entities.Contains(member)) return true;
  return false;
 }
 protected EBG_CacheGroup Owner(EBG_CacheManager manager, EBG_CacheMember member)
 {
  if (!member) return null;
  foreach (EBG_CacheGroup record : manager.Records) if (record.Members.Contains(member)) return record;
  return null;
 }
 // A living soldier who left his squad for good (EBG_MarkLeftSquad) and is out of
 // every native group leaves his settled, awake record at once, so no save or scan
 // counts him as a member. Cached, transitional or unresolved records keep him
 // until Build retires him through plan.Removed. Never spawns or deletes anything.
 bool RetireLeftSquad(EBG_CacheManager manager, SCR_ChimeraCharacter entity)
 {
  if (!manager || !entity || !entity.EBG_HasLeftSquad() || entity.GetCharacterGroup()) return false;
  EBG_CacheMember member = manager.FindMember(entity);
  EBG_CacheGroup owner = Owner(manager, member);
  if (!owner || member.Dead || owner.Full || owner.FullCleanup || owner.Simulation || owner.Recovery != "" || owner.ReleaseRequested) return false;
  if (owner.PersistenceIssue != "" || owner.PersistentScalarFailure || owner.PersistentScalarRollbackPending || (owner.PersistentData && !owner.PersistentData.Imported)) return false;
  EBG_CacheCleanup cleanup = EBG_CacheCleanup.Get();
  array<EBG_CacheGroup> affected = {owner};
  if (!cleanup.CanRegroup(affected)) return false;
  PrintFormat("[EBG MEMBER LEFT SQUAD] group=%1 member=%2 entity=%3 reason=living soldier left his squad for good; forgotten at once, never cached, respawned or deleted", owner.Id, member.Id, entity);
  cleanup.ReleaseRemovedMember(owner, member);
  owner.Members.RemoveItem(member);
  owner.ClearSince = -1; owner.ActiveSince = manager.Now();
  if (owner.Members.IsEmpty()) { cleanup.ReleaseGroup(owner); manager.Records.RemoveItem(owner); }
  return true;
 }
 protected bool Changed(EBG_CacheManager manager, EBG_CacheGroup record)
 {
  if (!record.Zone || !record.Zone.Enabled || record.ReleaseRequested) return false;
  if (!record.Full)
  foreach (EBG_CacheMember member : record.Members)
  {
   if (!member || member.Dead) continue;
   // Full absence is intentional. A missing live parent alone never authorizes
   // discarding the transaction; Build retains it with its survivor snapshots.
   SCR_ChimeraCharacter entity = member.Entity;
   if (!entity) return true;
   // Left for good (EBG_MarkLeftSquad): retire him even when no native group remains.
   if (entity.EBG_HasLeftSquad()) return true;
   if (entity.GetCharacterGroup() != record.Group) return true;
  }
  if (!record.Group) return record.Full && record.Full.GetState() != EBG_FullGroupPhase.CACHED;
  if (record.Group.GetAgentsCount() > 128) return true;
  array<AIAgent> agents = {}; record.Group.GetAgents(agents);
  foreach (AIAgent agent : agents)
  {
   if (!agent) return true;
   EBG_CacheMember known = manager.FindMember(agent.GetControlledEntity());
   if (!known || !record.Members.Contains(known)) return true;
  }
  return false;
 }
 protected EBG_RegroupPlan Build(EBG_CacheManager manager, EBG_CacheGroup seed)
 {
  EBG_RegroupPlan plan = new EBG_RegroupPlan();
  plan.Zone = seed.Zone; plan.Observed = manager.Now(); plan.Records.Insert(seed);
  int recordIndex, groupIndex;
  PersistenceSystem system = PersistenceSystem.GetInstance();
  while (recordIndex < plan.Records.Count() || groupIndex < plan.Groups.Count())
  {
   if (plan.Records.Count() > 128 || plan.Groups.Count() > 128 || plan.Entities.Count() > 512) { plan.Problem = "Regroup held: affected roster exceeds bounded repair limit"; return plan; }
   if (recordIndex < plan.Records.Count())
   {
    EBG_CacheGroup record = plan.Records[recordIndex++];
    if (!record.Zone || record.Zone != plan.Zone) { plan.Problem = "Regroup held: groups belong to different zones; explicit ownership decision required"; return plan; }
    if (record.ReleaseRequested || record.PersistenceIssue != "" || record.PersistentScalarFailure || record.PersistentScalarRollbackPending || (record.PersistentData && !record.PersistentData.Imported)) { plan.Problem = "Regroup held: original recovery or imported ownership unresolved"; return plan; }
    if (record.Full && !record.Group) { plan.Problem = "Regroup held: original Full parent missing; cached survivors retained"; return plan; }
    if (record.Group && !plan.Groups.Contains(record.Group)) plan.Groups.Insert(record.Group);
    if (record.Full) continue;
    foreach (EBG_CacheMember member : record.Members)
    {
     if (member.Dead) continue;
     if (!member.Entity)
     {
      // A null server entity in an active, settled record has been removed.
      // Never infer deletion from an intentional cache or unresolved save restore.
      if (!record.Group || record.Simulation || record.Recovery != "" || member.WasPlayer || (system && !member.PersistentId.IsNull() && (system.FindById(member.PersistentId) || EBG_MissionPersistence.HasUnresolvedRelease(member.PersistentId))))
      { plan.Problem = "Regroup held: missing member has retained state or protected identity"; return plan; }
      plan.Removed.Insert(member);
      continue;
     }
     SCR_AIGroup parent = member.Entity.GetCharacterGroup();
     // A living soldier who left his squad for good (an AI Surrender prisoner)
     // is retired from this ledger: never respawned, cached or deleted by us.
     if (member.Entity.EBG_HasLeftSquad())
     {
      if (parent) { plan.Problem = "Regroup held: a soldier who left his squad is in a native roster"; return plan; }
      plan.Removed.Insert(member);
      continue;
     }
     if (!parent) { plan.Problem = "Regroup held: living member has no authoritative native group"; return plan; }
     if (!plan.Groups.Contains(parent)) plan.Groups.Insert(parent);
    }
    continue;
   }
   SCR_AIGroup group = plan.Groups[groupIndex++];
   EBG_CacheGroup established = manager.FindGroup(group);
   if (established && !plan.Records.Contains(established)) plan.Records.Insert(established);
   if (group.EBG_Exclude || !group.EBG_HasCompletedInitialSpawn() || group.GetPlayerCount() > 0 || group.IsSlave() || group.GetMaster() || group.IsCreatedByCommander() || group.GetLifecyclePolicy() == SCR_EAIGroupLifecyclePolicy.ProximityDriven) { plan.Problem = "Regroup held: destination has player, external or incomplete native ownership"; return plan; }
   if (!established && system && EBG_MissionPersistence.Reserves(system.GetId(group))) { plan.Problem = "Regroup held: destination has retained imported identity"; return plan; }
   if (group.GetAgentsCount() > 128) { plan.Problem = "Regroup held: destination roster exceeds 128 members"; return plan; }
   array<AIAgent> agents = {}; group.GetAgents(agents);
   foreach (AIAgent agent : agents)
   {
    if (!agent) { plan.Problem = "Regroup held: native agent is unresolved"; return plan; }
    SCR_ChimeraCharacter entity = SCR_ChimeraCharacter.Cast(agent.GetControlledEntity());
    if (!entity || !entity.GetCharacterController() || entity.GetCharacterController().IsDead() || entity.GetCharacterGroup() != group || agent.GetParentGroup() != group || plan.Entities.Contains(entity)) { plan.Problem = "Regroup held: native roster is incomplete or inconsistent"; return plan; }
    if (entity.EBG_HasLeftSquad()) { plan.Problem = "Regroup held: a soldier who left his squad is in a native roster"; return plan; }
    EBG_CacheMember known = manager.FindMember(entity);
    EBG_CacheGroup previous = Owner(manager, known);
    if (known && (!previous || known.Dead)) { plan.Problem = "Regroup held: original member ownership is unresolved"; return plan; }
    if (previous && !plan.Records.Contains(previous)) plan.Records.Insert(previous);
    if (!known && (!EBG_MissionPersistence.MayEnroll(entity) || entity.EBG_WasPlayerControlled() || (system && EBG_MissionPersistence.HasUnresolvedRelease(system.GetId(entity))))) { plan.Problem = "Regroup held: newcomer has protected or unresolved identity"; return plan; }
    if (!established && EBG_CacheGeometry.DistanceSq(entity.GetOrigin(), plan.Zone.GetOrigin()) > plan.Zone.Affected * plan.Zone.Affected) { plan.Problem = "Regroup held: unknown destination extends outside owning zone"; return plan; }
    plan.Entities.Insert(entity); plan.Parents.Insert(group);
   }
  }
  // Every present survivor must occur exactly once in the native destination.
  foreach (EBG_CacheGroup original : plan.Records)
   if (!original.Full)
   foreach (EBG_CacheMember survivor : original.Members)
    if (!survivor.Dead && survivor.Entity && !plan.Removed.Contains(survivor) && !plan.Entities.Contains(survivor.Entity)) { plan.Problem = "Regroup held: living member is absent from authoritative roster"; return plan; }
  return plan;
 }
 protected bool Commit(EBG_CacheManager manager, EBG_RegroupPlan plan)
 {
  EBG_CacheCleanup cleanup = EBG_CacheCleanup.Get();
  if (!cleanup.CanRegroup(plan.Records)) { plan.Hold("Regroup held: cleanup transfer remains unresolved"); return false; }
  // Durable member limits include retained casualties, not just native agents.
  // Validate the entire projection before transferring any member reference.
  foreach (SCR_AIGroup projectedGroup : plan.Groups)
  {
   EBG_CacheGroup established = manager.FindGroup(projectedGroup);
   int projectedMembers, retainedObjects;
   foreach (SCR_AIGroup parent : plan.Parents) if (parent == projectedGroup) projectedMembers++;
   foreach (EBG_CacheGroup original : plan.Records)
    foreach (EBG_CacheMember originalMember : original.Members)
    {
     bool belongs;
     if (originalMember.Dead)
     {
      belongs = original == established;
      if (belongs) projectedMembers++;
     }
     else
     {
      int nativeIndex = plan.Entities.Find(originalMember.Entity);
      belongs = nativeIndex >= 0 && plan.Parents[nativeIndex] == projectedGroup;
     }
     if (belongs) retainedObjects += cleanup.CountPersistentMemberObjects(originalMember);
    }
   for (int prospectiveIndex = 0; prospectiveIndex < plan.Entities.Count(); prospectiveIndex++)
    if (plan.Parents[prospectiveIndex] == projectedGroup && !manager.FindMember(plan.Entities[prospectiveIndex]))
     retainedObjects += cleanup.CountNewMemberObjects(plan.Entities[prospectiveIndex]);
   if (projectedMembers > 128 || retainedObjects > 2048)
   { plan.Hold("Regroup held: combined survivor/casualty history exceeds native save capacity"); return false; }
  }
  array<EBG_CacheGroup> destinations = {};
  foreach (SCR_AIGroup group : plan.Groups)
  {
   EBG_CacheGroup record = manager.FindGroup(group);
   if (!record && plan.Parents.Contains(group)) record = manager.AddRegroupRecord(plan.Zone, group);
   if (record) destinations.Insert(record);
  }
  array<EBG_CacheMember> newcomers = {};
  foreach (EBG_CacheMember removed : plan.Removed)
  {
   EBG_CacheGroup originalOwner = Owner(manager, removed);
   bool leftSquad = removed.Entity && removed.Entity.EBG_HasLeftSquad();
   if (leftSquad)
    PrintFormat("[EBG MEMBER LEFT SQUAD] group=%1 member=%2 entity=%3 reason=living soldier left his squad for good; retired by regroup, never cached, respawned or deleted", originalOwner.Id, removed.Id, removed.Entity);
   cleanup.ReleaseRemovedMember(originalOwner, removed);
   originalOwner.Members.RemoveItem(removed);
   if (!leftSquad && EBG_CacheDebug.Level > 0)
    PrintFormat("[EBG MEMBER RETIRED] group=%1 member=%2 native=%3 reason=absent server entity after stable active roster; no respawn", originalOwner.Id, removed.Id, removed.PersistentId);
  }
  for (int i = 0; i < plan.Entities.Count(); i++)
  {
   SCR_ChimeraCharacter entity = plan.Entities[i];
   EBG_CacheGroup destination = manager.FindGroup(plan.Parents[i]);
   EBG_CacheMember member = manager.FindMember(entity);
   if (!member) { member = manager.AddRegroupMember(destination, entity); newcomers.Insert(member); continue; }
   EBG_CacheGroup previous = Owner(manager, member);
   if (previous == destination) continue;
   // Both rosters own strong member refs; insert before removing the old owner.
   destination.Members.Insert(member); previous.Members.RemoveItem(member);
   destination.LastUnsafe = Math.Max(destination.LastUnsafe, previous.LastUnsafe);
  }
  foreach (EBG_CacheGroup affected : plan.Records) if (!destinations.Contains(affected)) destinations.Insert(affected);
  cleanup.CommitRegroup(plan.Records, destinations, newcomers);
  foreach (EBG_CacheGroup changed : destinations)
  {
   changed.RegroupReason = ""; changed.ClearSince = -1; changed.ActiveSince = manager.Now();
   changed.CleanupClearSince = -1; changed.CleanupNextAttempt = 0;
   changed.LastCacheRejection = "";
   if (changed.Members.IsEmpty()) manager.Records.RemoveItem(changed);
  }
  PrintFormat("[EBG REGROUP] zone=%1 records=%2 destinations=%3 live=%4 stableSeconds=%5", plan.Zone.GetID(), plan.Records.Count(), destinations.Count(), plan.Entities.Count(), manager.Now() - plan.Observed);
  return true;
 }
 // ponytail: fixed scan/transaction bounds; increase only after measured need.
 // Share 32 seed inspections between retained repairs and new discovery. Each
 // connected repair is separately capped in Build; admit one commit per scan.
 void Scan(EBG_CacheManager manager)
 {
  array<EBG_CacheGroup> seeds = {};
  array<ref EBG_RegroupPlan> next = {};
  int retainedBudget = Math.Min(16, m_Plans.Count());
  for (int retainedIndex = 0; retainedIndex < m_Plans.Count(); retainedIndex++)
  {
   EBG_RegroupPlan pending = m_Plans[retainedIndex];
   if (retainedIndex >= retainedBudget) { next.Insert(pending); continue; }
   pending.Hold("");
   if (!pending.Records.IsEmpty() && pending.Records[0] && manager.Records.Contains(pending.Records[0])) seeds.Insert(pending.Records[0]);
  }
  int count = Math.Min(32 - retainedBudget, manager.Records.Count());
  for (int n = 0; n < count; n++)
  {
   if (m_Cursor >= manager.Records.Count()) m_Cursor = 0;
   EBG_CacheGroup record = manager.Records[m_Cursor++];
   if (!seeds.Contains(record) && Changed(manager, record)) seeds.Insert(record);
  }
  array<EBG_CacheGroup> visited = {};
  bool committed;
  foreach (EBG_CacheGroup seed : seeds)
  {
   if (!seed || visited.Contains(seed) || !manager.Records.Contains(seed) || !seed.Zone || !seed.Zone.Enabled) continue;
   EBG_RegroupPlan plan = Build(manager, seed);
   foreach (EBG_CacheGroup affected : plan.Records) if (!visited.Contains(affected)) visited.Insert(affected);
   EBG_RegroupPlan prior;
   foreach (EBG_RegroupPlan candidate : m_Plans) if (plan.Same(candidate)) { prior = candidate; break; }
   if (prior) plan.Observed = prior.Observed;
   // A changed connected component can subsume an unprocessed prior plan.
   // Preserve all other reservations and move processed plans to the back.
   for (int previousIndex = next.Count() - 1; previousIndex >= 0; previousIndex--)
   {
    bool overlaps;
    foreach (EBG_CacheGroup planned : plan.Records) if (next[previousIndex].Records.Contains(planned)) overlaps = true;
    if (!overlaps) continue;
    next[previousIndex].Hold(""); next.Remove(previousIndex);
   }
   next.Insert(plan);
   if (plan.Problem != "") { plan.Hold(plan.Problem); continue; }
   if (!prior || manager.Now() - plan.Observed < 4) { plan.Hold("Regroup pending: waiting for consecutive stable native roster scans"); continue; }
   bool restoring;
   foreach (EBG_CacheGroup retained : plan.Records)
   {
    if (retained.Full) { retained.WakeRequested = true; restoring = true; }
    if (retained.Simulation || retained.Recovery != "") restoring = true;
   }
   plan.Hold("Regroup pending: restoring original state before ownership commit");
   if (restoring || committed || plan.Zone.Editing || plan.Zone.HasPendingSettings()) continue;
   // Build and commit are synchronous read/field operations on the server. No
   // native spawn/delete, yield, or group membership mutation occurs here.
   if (Commit(manager, plan)) { next.RemoveItem(plan); committed = true; }
  }
  m_Plans = next;
 }
}

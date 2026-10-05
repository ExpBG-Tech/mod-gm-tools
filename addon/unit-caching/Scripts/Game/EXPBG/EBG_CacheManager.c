// EXPBG GM Optimizer | M.Pac and K.Edgar
// One session manager; world-wide enrollment runs only on its bounded schedule.
class EBG_CacheMember
{
 int Id;
 SCR_ChimeraCharacter Entity;
 UUID PersistentId;
 vector Position;
 bool Dead;
 bool Missing;
 bool WasPlayer;
 float DiedAt;
 float LastHealth = -1;
 // Cleanup cache of the member's confirmed corpse-row death time: -1 unknown,
 // -2 no confirmed row and no body. Runtime only; never persisted.
 float CleanupDeathTime = -1;
 // Cleanup: a body-less casualty with nothing left to delete. Re-armed with the
 // record's settled flag. Runtime only; never persisted.
 bool CleanupDrained;
 // Cleanup: bounded per-casualty retry (H3); the whole casualty is one unit of work.
 // Runtime only; never persisted, a reload starts a fresh count.
 int CleanupBlocks;
 float CleanupRetryAt;
}
enum EBG_CacheRecordState { ACTIVE, SIM_CACHED, FULL_CACHED, PENDING, RECOVERY, ELIMINATED }
class EBG_CacheGroup
{
 int Id;
 EBG_CacheZone Zone;
 SCR_AIGroup Group;
 UUID PersistentId;
 string PersistenceIssue;
 bool PersistentResolutionLogged;
 bool PersistentScalarFailure;
 bool PersistentScalarRollbackPending;
 ref array<ref EBG_MissionScalarBackup> PersistentScalarBackups;
 ref EBG_MissionGroupData PersistentData;
 ref array<ref EBG_CacheMember> Members = {};
 vector Anchor;
 float MinY;
 float MaxY;
 float ActiveSince;
 float ClearSince = -1;
 float CleanupClearSince = -1;
 float CleanupNextAttempt;
 int CleanupObjectCursor;
 float LastUnsafe;
 int Alive;
 int Dead;
 bool CleanupRegistered;
 // Per-casualty cleanup bookkeeping. Diagnostics only: never a cache or sleep gate.
 bool CleanupCasualtiesSettled;
 string CleanupStatus;
 string CleanupPhase;
 ref EBG_SimulationState Simulation;
 ref EBG_FullCacheGroup Full;
 UUID FullGroupId;
 ref array<UUID> FullMemberIds = {};
 ref array<EBG_CacheMember> FullMembers = {};
 ref EBG_CleanupFullTransfer FullCleanup;
 bool WakeRequested;
 bool RecoveryRetryRequested;
 float RecoveryNextAttempt;
 bool ReleaseRequested;
 string Recovery;
 string Reason;
 string LastCacheRejection;
 string RegroupReason;
 protected string m_LastRecoveryWarning;
 protected float m_NextRecoveryWarning;
 protected string m_DebugLast;
 protected float m_DebugNext;
 EBG_CacheRecordState CacheState()
 {
  if (Recovery != "" || PersistenceIssue != "" || PersistentScalarRollbackPending) return EBG_CacheRecordState.RECOVERY;
  if (Full)
  {
   if (Full.GetState() == EBG_FullGroupPhase.FAILED) return EBG_CacheRecordState.RECOVERY;
   if (Full.GetState() == EBG_FullGroupPhase.CACHED) return EBG_CacheRecordState.FULL_CACHED;
   if (Full.GetState() != EBG_FullGroupPhase.RELEASED) return EBG_CacheRecordState.PENDING;
  }
  if (Simulation && Simulation.Suspended) return EBG_CacheRecordState.SIM_CACHED;
  if (Alive == 0 && Dead == Members.Count()) return EBG_CacheRecordState.ELIMINATED;
  return EBG_CacheRecordState.ACTIVE;
 }
 string DebugState(bool cleanupDetail = true)
 {
  string state = "Active";
  if (Full) state = EBG_CacheFullCoordinator.Status(this);
  if (Alive == 0 && Dead == Members.Count()) state = "Eliminated";
  if (Simulation)
  {
   if (Simulation.Suspended) state = "Simulation cached";
   else state = "Recovery retained; survivors restored";
  }
  if (Reason != "" && Reason != state) state += " | " + Reason;
  // Records with survivors show casualty cleanup separately; eliminated records
  // already carry the cleanup reason in Reason (EBG_CacheManager.Tick).
  if (Alive > 0)
  {
   if (cleanupDetail && CleanupStatus != "") state += " | " + CleanupStatus;
   else if (!cleanupDetail && CleanupPhase != "") state += " | Casualty cleanup: " + CleanupPhase;
  }
  return state;
 }
 void DebugStatus(float now)
 {
  // Material failures remain visible with Debug Off, without repeated retry spam.
  if (Recovery == "") m_LastRecoveryWarning = "";
  else if (Recovery != m_LastRecoveryWarning && now >= m_NextRecoveryWarning)
  {
   m_LastRecoveryWarning = Recovery; m_NextRecoveryWarning = now + 5;
   PrintFormat("[EBG RECOVERY WARNING] group=%1 native=%2 anchor=%3 reason=%4", Id, FullGroupId, Anchor, Recovery);
  }
  if (!Zone || EBG_CacheDebug.Level == 0) { m_DebugLast = ""; m_DebugNext = 0; return; }
  string message = string.Format("%1 alive / %2 dead | %3", Alive, Dead, DebugState());
  string logState = DebugState(false);
  bool countdown = Reason.StartsWith("Sleep countdown:") || Reason.StartsWith("Safety cooldown:");
  if (Reason.StartsWith("Sleep countdown:")) logState = "Waiting for continuous clear delay";
  if (Reason.StartsWith("Safety cooldown:")) logState = "Safety cooldown | " + LastCacheRejection;
  if (countdown && Alive > 0 && CleanupPhase != "") logState += " | Casualty cleanup: " + CleanupPhase;
  string logKey = string.Format("%1/%2/%3", Alive, Dead, logState);
  if (logKey == m_DebugLast || now < m_DebugNext) return;
  m_DebugLast = logKey; m_DebugNext = now + 5;
  PrintFormat("[EBG DEBUG GROUP] zone=%1 group=%2 native=%3 anchor=%4 state=%5", Zone.GetID(), Id, FullGroupId, Anchor, message);
 }
}
// Shared only within one scheduler pass. Native/member sampling remains unchanged.
class EBG_ZoneProtection
{
 vector Origin;
 bool WakeKnown, Wake, SleepKnown, Sleep;
}
class EBG_CacheManager
{
 static ref EBG_CacheManager Instance;
 static bool Unloading;
 protected static World s_UnloadingWorld;
 protected static SCR_BaseGameMode s_UnloadingMode;
 ref array<ref EBG_CacheGroup> Records = {};
 ref array<vector> Players = {};
 ref array<EBG_CacheZone> Owners = {};
 protected World m_World;
 protected int m_NextId;
 protected int m_Ticks;
 protected int m_PumpTicks;
 protected int m_RecoveryCursor;
 protected ref map<EBG_CacheZone, ref EBG_ZoneProtection> m_Protection = new map<EBG_CacheZone, ref EBG_ZoneProtection>();
 ref EBG_CacheRegroup Regroup = new EBG_CacheRegroup();
 // Shared wake budget: restored characters per second across all cache zones. A
 // full bucket admits one ordinary squad at once, so a single group still wakes
 // immediately; groups woken in the same moment follow over successive ticks.
 // Fixed in code: a GM setting would change the saved zone settings format.
 static const float WAKE_BUDGET_RATE = 4;
 static const float WAKE_BUDGET_BURST = 12;
 protected float m_WakeBudget = WAKE_BUDGET_BURST;
 protected float m_WakeBudgetAt = -1;
 protected int m_WakeDeferredId;
 protected float m_WakeDeferredLog;
 static void ShutdownForWorldCleanup()
 {
  if (GetGame())
  {
   s_UnloadingWorld = GetGame().GetWorld();
   s_UnloadingMode = SCR_BaseGameMode.Cast(GetGame().GetGameMode());
  }
  Unloading = true;
  EBG_OptimizerControl.Reset();
  EBG_CacheSnapshot.ShutdownForWorldCleanup();
  SCR_AIGroupSerializer.EBG_ReleaseCallbacksForWorldCleanup();
  EBG_FullSaveGate.ShutdownForWorldCleanup();
#ifdef EBG_ACCEPTANCE_TEST
  EBG_FullSaveProof.ObserveAfterSaveGateShutdown();
#endif
  if (Instance && GetGame()) GetGame().GetCallqueue().Remove(Instance.Pump);
  EBG_CacheCleanup.ShutdownForWorldCleanup();
  if (Instance)
  {
   foreach (EBG_CacheGroup record : Instance.Records)
   {
    if (record.Full) record.Full.ReleaseForWorldCleanup();
    record.Full = null; record.FullCleanup = null;
    record.FullMembers.Clear(); record.FullMemberIds.Clear();
   }
   // World teardown owns entity destruction. Never wake AI or reinsert garbage here.
   Instance.Records.Clear();
   Instance.Owners.Clear();
   Instance.Players.Clear();
   Instance.m_World = null;
  }
  Instance = null;

 }
 static EBG_CacheManager Get()
 {
  if (!Instance || Instance.m_World != GetGame().GetWorld())
  {
   Unloading = false;
   if (Instance) GetGame().GetCallqueue().Remove(Instance.Pump);
   Instance = new EBG_CacheManager();
   Instance.m_World = GetGame().GetWorld();
   GetGame().GetCallqueue().CallLater(Instance.Pump, 100, true);
  }
  return Instance;
 }
 static bool IsPortableWorldReady()
 {
  if (!GetGame() || !GetGame().InPlayMode() || !GetGame().GetWorld()) return false;
  SCR_BaseGameMode mode = SCR_BaseGameMode.Cast(GetGame().GetGameMode());
  if (!mode || !mode.IsRunning() || mode.GetState() != SCR_EGameModeState.GAME || GameStateTransitions.IsTransitionRequestedOrInProgress()) return false;
  // Teardown can leave its static flag set until the next world's first module.
  // A cold import is allowed to create that manager, but never in the same
  // world whose cleanup is still in progress.
  return !Unloading || s_UnloadingWorld != GetGame().GetWorld() || s_UnloadingMode != mode;
 }
 static EBG_CacheManager ReadyForPortableImport()
 {
  if (!GetGame()) return null;
  if (!IsPortableWorldReady())
  {
   SCR_BaseGameMode mode = SCR_BaseGameMode.Cast(GetGame().GetGameMode());
   PrintFormat("[EBG PORTABLE READY] play=%1 unloading=%2 sameWorld=%3 sameMode=%4 mode=%5 transition=%6", GetGame().InPlayMode(), Unloading, s_UnloadingWorld == GetGame().GetWorld(), s_UnloadingMode == mode, mode, GameStateTransitions.IsTransitionRequestedOrInProgress());
   if (mode) PrintFormat("[EBG PORTABLE READY] running=%1 state=%2", mode.IsRunning(), mode.GetState());
   return null;
  }
  return Get();
 }
 float Now() { return GetGame().GetWorld().GetWorldTime() * 0.001; }
 // Urgent restores (changed cached state, deleted module) always pass and still pay.
 bool TakeWakeBudget(int characters, bool urgent = false)
 {
  float now = Now();
  if (m_WakeBudgetAt >= 0) m_WakeBudget = Math.Min(WAKE_BUDGET_BURST, m_WakeBudget + (now - m_WakeBudgetAt) * WAKE_BUDGET_RATE);
  m_WakeBudgetAt = now;
  int cost = characters;
  if (cost < 1) cost = 1;
  // A group larger than the bucket waits for a full bucket, then borrows the rest.
  if (!urgent && m_WakeBudget < Math.Min(cost, WAKE_BUDGET_BURST)) return false;
  m_WakeBudget -= cost;
  return true;
 }
 void ReportWakeDeferred(EBG_CacheGroup record, int characters)
 {
  if (!record || !record.Zone || record.Zone.DebugMessages == 0) return;
  float now = Now();
  if (record.Id == m_WakeDeferredId && now < m_WakeDeferredLog) return;
  m_WakeDeferredId = record.Id; m_WakeDeferredLog = now + 5;
  PrintFormat("[EBG WAKE BUDGET] group=%1 zone=%2 waits: %3 characters, budget %4 of %5 (%6 per second)", record.Id, record.Zone.GetID(), characters, Math.Floor(m_WakeBudget), WAKE_BUDGET_BURST, WAKE_BUDGET_RATE);
 }
 // Disk records recreate the original logical roster, including absent casualties.
 // This stages references only; cleanup commits listeners after complete mapping.
 // Diagnostics only: candidate names and proximity never authorize a rebind.
 void LogPersistentResolution(EBG_MissionGroupData saved, string phase)
 {
#ifdef EBG_ACCEPTANCE_TEST
  PersistenceSystem system = PersistenceSystem.GetInstance();
  if (!system || !saved) return;
  Managed rawZone = system.FindById(saved.ZoneId);
  Managed rawGroup = system.FindById(saved.GroupId);
  string zoneType = "null", groupType = "null";
  if (rawZone) zoneType = rawZone.Type().ToString();
  if (rawGroup) groupType = rawGroup.Type().ToString();
  PrintFormat("[EBG MISSION RESOLVE] phase=%1 expectedZone=%2 zoneType=%3 expectedGroup=%4 groupType=%5 savedPresent=%6", phase, saved.ZoneId, zoneType, saved.GroupId, groupType, saved.GroupPresent);
  int zoneCount;
  foreach (EBG_CacheZone zone : EBG_CacheZone.Zones)
  {
   if (!zone || zoneCount++ >= 16) continue;
   PrintFormat("[EBG MISSION RESOLVE ZONE] name=%1 id=%2 exact=%3 tracked=%4 editing=%5", zone.GetName(), system.GetId(zone), system.GetId(zone) == saved.ZoneId, system.IsTracked(zone), zone.Editing);
  }
  for (int i = 0; i < saved.Members.Count(); i++)
  {
   Managed raw = system.FindById(saved.Members[i].Id);
   SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(raw);
   string actualType = "null";
   if (raw) actualType = raw.Type().ToString();
   PrintFormat("[EBG MISSION RESOLVE MEMBER] index=%1 expected=%2 type=%3 savedDead=%4 savedMissing=%5", i, saved.Members[i].Id, actualType, saved.Members[i].Dead, saved.Members[i].Missing);
  }
  AIWorld world = GetGame().GetAIWorld();
  if (!world) return;
  array<AIAgent> agents = {};
  world.GetAIAgents(agents);
  array<SCR_AIGroup> visited = {};
  int emitted;
  foreach (AIAgent agent : agents)
  {
   SCR_AIGroup group = SCR_AIGroup.Cast(agent);
   if (!group) group = SCR_AIGroup.Cast(agent.GetParentGroup());
   if (!group || visited.Contains(group)) continue;
   visited.Insert(group);
   array<AIAgent> roster = {}; group.GetAgents(roster);
   int originalMatches;
   foreach (AIAgent memberAgent : roster)
   {
    UUID actualId = system.GetId(memberAgent.GetControlledEntity());
    foreach (EBG_MissionMemberData expectedMember : saved.Members) if (expectedMember.Id == actualId) originalMatches++;
   }
   UUID groupId = system.GetId(group);
   if (emitted >= 16 && originalMatches == 0 && groupId != saved.GroupId) continue;
   emitted++;
   PrintFormat("[EBG MISSION RESOLVE GROUP] name=%1 id=%2 exact=%3 tracked=%4 registrySame=%5 agents=%6 originalMemberMatches=%7", group.GetName(), groupId, groupId == saved.GroupId, system.IsTracked(group), system.FindById(groupId) == group, roster.Count(), originalMatches);
  }
  PrintFormat("[EBG MISSION RESOLVE TOTAL] groups=%1 emitted=%2 zones=%3", visited.Count(), emitted, EBG_CacheZone.Zones.Count());
#endif
 }
 EBG_CacheGroup PreparePersistentGroup(EBG_MissionGroupData saved)
 {
  PersistenceSystem system = PersistenceSystem.GetInstance();
  EBG_CacheGroup record;
  foreach (EBG_CacheGroup existing : Records)
   if (existing.PersistentId == saved.GroupId) { record = existing; break; }
  if (!record)
  {
   record = new EBG_CacheGroup(); record.Id = ++m_NextId;
   record.PersistentId = saved.GroupId; record.PersistentData = saved;
   record.ActiveSince = Now(); record.LastUnsafe = Now();
   foreach (EBG_MissionMemberData source : saved.Members)
   {
    EBG_CacheMember member = new EBG_CacheMember();
    member.Id = ++m_NextId; member.PersistentId = source.Id;
    member.Position = source.Position; member.Dead = source.Dead;
    member.Missing = source.Missing; member.WasPlayer = source.WasPlayer;
    member.DiedAt = Now() - source.DeathAge;
    record.Members.Insert(member);
   }
   Records.Insert(record);
  }
  record.PersistenceIssue = "";
  record.Zone = EBG_CacheZone.Cast(system.FindById(saved.ZoneId));
  record.Group = SCR_AIGroup.Cast(system.FindById(saved.GroupId));
  if (!record.Zone) record.PersistenceIssue = "Original zone UUID unresolved";
  else if (saved.GroupPresent && !record.Group) record.PersistenceIssue = "Original group UUID unresolved";
  if (record.Zone && !Owners.Contains(record.Zone)) Owners.Insert(record.Zone);
  for (int i = 0; i < saved.Members.Count(); i++)
  {
   EBG_MissionMemberData source = saved.Members[i];
   EBG_CacheMember member = record.Members[i];
   if (EBG_MissionPersistence.HasUnresolvedRelease(source.Id)) record.PersistenceIssue = "Original member has retained UUID-less transfer lineage";
   member.Entity = SCR_ChimeraCharacter.Cast(system.FindById(source.Id));
   if (!member.Entity)
   {
    if (!source.Dead && !source.Missing) record.PersistenceIssue = "Original living member identity unresolved";
    continue;
   }
   CharacterControllerComponent controller = member.Entity.GetCharacterController();
   if (!controller || controller.IsDead() != source.Dead) record.PersistenceIssue = "Saved member life state differs from native load";
   if (member.Entity.EBG_WasPlayerControlled()) member.WasPlayer = true;
  }
  if (!record.PersistentResolutionLogged) { record.PersistentResolutionLogged = true; LogPersistentResolution(saved, "bind-first"); }
  return record;
 }
 EBG_MissionGroupData ExportPersistentGroup(EBG_CacheGroup record, out string reason)
 {
  PersistenceSystem system = PersistenceSystem.GetInstance();
  if (!system || !record.Zone) { reason = "Managed record has no persistent owning zone"; return null; }
  EBG_MissionGroupData saved = new EBG_MissionGroupData();
  saved.ZoneId = system.GetId(record.Zone);
  if (record.Group && !system.GetId(record.Group).IsNull()) record.PersistentId = system.GetId(record.Group);
  saved.GroupId = record.PersistentId; saved.GroupPresent = record.Group != null;
  saved.Imported = true;
  if (saved.ZoneId.IsNull() || saved.GroupId.IsNull() || record.Members.IsEmpty() || record.Members.Count() > 128) { reason = "Original zone/group identity or roster is not durable"; return null; }
  foreach (EBG_CacheMember member : record.Members)
  {
   if (member.Entity && !system.GetId(member.Entity).IsNull()) member.PersistentId = system.GetId(member.Entity);
   EBG_MissionMemberData target = new EBG_MissionMemberData();
   target.Id = member.PersistentId; target.Position = member.Position;
   target.Dead = member.Dead; target.Missing = member.Missing || !member.Entity;
   target.WasPlayer = member.WasPlayer || (member.Entity && member.Entity.EBG_WasPlayerControlled());
   if (member.Dead) target.DeathAge = Math.Max(0, Now() - member.DiedAt);
   if (!target.Valid()) { reason = "Original member identity/age is unavailable"; return null; }
   saved.Members.Insert(target);
  }
  return saved;
 }
 void Register(EBG_CacheZone zone)
 {
  if (!zone) return;
  if (EBG_OptimizerControl.Preparing) zone.Editing = true;
  if (!Owners.Contains(zone)) Owners.Insert(zone);
  Refresh(zone);
 }
 EBG_CacheGroup FindGroup(SCR_AIGroup group)
 {
  foreach (EBG_CacheGroup record : Records)
   if (record.Group == group) return record;
  return null;
 }
 bool IsReserved(SCR_AIGroup group)
 {
  if (Regroup.ReservesGroup(group)) return true;
  PersistenceSystem system = PersistenceSystem.GetInstance();
  if (!system || !group) return false;
  UUID id = system.GetId(group);
  if (id.IsNull()) return false;
  foreach (EBG_CacheGroup record : Records)
   if (record.Full && (record.FullGroupId == id || record.Full.HasReservedUUID(id))) return true;
  return false;
 }
 EBG_CacheMember FindMember(IEntity entity)
 {
  if (!entity) return null;
  foreach (EBG_CacheGroup record : Records)
   foreach (EBG_CacheMember member : record.Members)
    if (member.Entity == entity) return member;
  // Partial restores are not rebound into member.Entity until the whole group
  // validates. Retained root references still identify their original ledger
  // after native StopTracking removes the UUID, including inside death events.
  foreach (EBG_CacheGroup partial : Records)
  {
   if (!partial.Full || partial.FullMembers.Count() != partial.FullMemberIds.Count()) continue;
   for (int rootIndex = 0; rootIndex < partial.FullMembers.Count(); rootIndex++)
   {
    EBG_CacheMember original = partial.FullMembers[rootIndex];
    if (!original || !partial.Members.Contains(original) || partial.Full.GetNativeId(rootIndex) != partial.FullMemberIds[rootIndex]) continue;
    if (partial.Full.GetRestoredMember(rootIndex) == entity) return original;
   }
  }
  PersistenceSystem system = PersistenceSystem.GetInstance();
  if (!system) return null;
  UUID id = system.GetId(entity);
  if (id.IsNull()) return null;
  foreach (EBG_CacheGroup reserved : Records)
  {
   if (!reserved.Full) continue;
   int index = reserved.FullMemberIds.Find(id);
   if (index >= 0 && index < reserved.FullMembers.Count()) return reserved.FullMembers[index];
  }
  return null;
 }
 void Release(EBG_CacheZone zone)
 {
  if (EBG_CacheSnapshot.Loading)
  {
   // CDF owns destruction of the old world cohort. Do not wake its snapshots
   // while it clears modules, or retain their dangling zone as a recovery owner.
   for (int old = Records.Count() - 1; old >= 0; old--)
    if (Records[old].Zone == zone)
    {
     if (Records[old].Full) Records[old].Full.ReleaseForWorldCleanup();
     if (EBG_CacheCleanup.Instance) EBG_CacheCleanup.Instance.ReleaseGroup(Records[old]);
     Records.Remove(old);
    }
   Owners.RemoveItem(zone); return;
  }
  for (int i = Records.Count() - 1; i >= 0; i--)
   if (Records[i].Zone == zone)
   {
    Records[i].ReleaseRequested = true;
    // Deletion/release is an intent, not permission to wake the whole zone in
    // this callback. Keep orphaned snapshots for the normal shared scheduler.
    if (Records[i].PersistentScalarRollbackPending || Records[i].Full || Records[i].Simulation)
    {
     Records[i].WakeRequested = true;
     Records[i].Zone = null;
     continue;
    }
    if (EBG_CacheCleanup.Instance) EBG_CacheCleanup.Instance.ReleaseGroup(Records[i]);
    Records.Remove(i);
   }
  bool retained;
  foreach (EBG_CacheGroup remaining : Records) if (remaining.Zone == zone) retained = true;
  if (!retained) Owners.RemoveItem(zone);
 }
 bool RestoreRecord(EBG_CacheGroup record)
 {
  if (record.PersistentScalarRollbackPending || record.Simulation) record.RecoveryNextAttempt = Now() + 5;
  if (record.PersistentScalarRollbackPending)
  {
   if (!EBG_CacheCleanup.Get().RecoverPersistentScalars(record))
   {
    record.Recovery = "Native scalar rollback pending; original ownership must remain intact";
    record.DebugStatus(Now());
    return false;
   }
   record.Recovery = "";
  }
  if (record.Full) { record.WakeRequested = true; return false; }
  if (!record.Simulation) { record.RecoveryNextAttempt = 0; record.WakeRequested = false; return true; }
  string reason;
  bool complete = EBG_SimulationCache.Restore(record.Simulation, reason);
  record.Reason = reason;
  record.ClearSince = -1;
  record.ActiveSince = Now();
  if (complete) { record.Simulation = null; record.Recovery = ""; record.RecoveryNextAttempt = 0; record.WakeRequested = false; }
  else record.Recovery = reason;
  record.DebugStatus(Now());
  return complete;
 }
 // Operator escape for recovery holds (controller action "Release blocked
 // groups"). Never automatic and never a save bypass: each held record is handed
 // back to normal AI. No restored survivor is spawned twice, no casualty is
 // refilled and no AI is deleted. One server log line per record.
 int ReleaseBlocked(int playerId)
 {
  int released;
  for (int i = Records.Count() - 1; i >= 0; i--)
  {
   EBG_CacheGroup record = Records[i];
   if (record.Recovery == "" && record.PersistenceIssue == "") continue;
   int id = record.Id;
   vector anchor = record.Anchor;
   string held = record.Recovery + record.PersistenceIssue;
   string where = "deleted module";
   if (record.Zone) where = string.Format("module @ %1", record.Zone.GetOrigin());
   string outcome = ReleaseBlockedRecord(record, i);
   if (!outcome.StartsWith("Refused")) released++;
   PrintFormat("[EBG RECOVERY RELEASE] group=%1 anchor=%2 zone='%3' held='%4' outcome='%5' player=%6", id, anchor, where, held, outcome, playerId);
  }
  return released;
 }
 protected string ReleaseBlockedRecord(EBG_CacheGroup record, int index)
 {
  if (record.PersistentScalarRollbackPending) return "Refused: native scalar rollback is still pending; run Prepare for save to retry it";
  if (record.Simulation)
  {
   // Restore what can be restored once more, then stop waiting for the rest.
   string restoreReason;
   EBG_SimulationCache.Restore(record.Simulation, restoreReason);
   foreach (EBG_SimulationAgent cached : record.Simulation.Members)
    if (cached.Devices) cached.Devices.Discard();
   record.Simulation = null; record.Recovery = ""; record.RecoveryNextAttempt = 0; record.WakeRequested = false;
   record.ClearSince = -1; record.ActiveSince = Now();
   return "Simulation state released; the original AI stay awake (" + restoreReason + ")";
  }
  if (record.Full)
  {
   EBG_PrefabFullCache full = EBG_PrefabFullCache.Cast(record.Full);
   if (!full) return "Refused: unsupported Full transaction type";
   if (full.GetState() != EBG_FullGroupPhase.FAILED) return "Refused: the Full transition is still in progress";
   full.BeginLenientRelease();
   record.Recovery = ""; record.RecoveryRetryRequested = true; record.WakeRequested = true;
   return "Full restore resumes without waiting for the blocking check; survivors already respawned are kept";
  }
  if (record.PersistenceIssue != "")
  {
   // Saved ownership could not be verified after loading. The AI are native and
   // awake; forget the unverified record instead of holding every save.
   if (EBG_CacheCleanup.Instance) EBG_CacheCleanup.Instance.ReleaseGroup(record);
   Records.Remove(index);
   return "Unverified saved ownership forgotten; the AI stay as an ordinary group";
  }
  record.Recovery = "";
  return "Recovery note cleared; no cached state remained";
 }
 bool RestoreZone(EBG_CacheZone zone, bool recover = false)
 {
  bool complete = true;
  foreach (EBG_CacheGroup record : Records)
   if (record.Zone == zone || (recover && !record.Zone && record.ReleaseRequested))
   {
    if (recover && record.Full && !record.Full.IsPending())
    { record.Recovery = ""; record.RecoveryRetryRequested = true; }
    if (record.Full || record.Simulation || record.PersistentScalarRollbackPending)
    {
     record.WakeRequested = true;
     complete = false;
    }
   }
  return complete;
 }
 void RestoreAllForSave()
 {
  if (!Replication.IsServer() || Unloading) return;
  EBG_OptimizerControl.BeginPreparation();
  SCR_AIGroupSerializer.EBG_RestoreMemberCallbacks();
  foreach (EBG_CacheZone zone : EBG_CacheZone.Zones)
  {
   if (!zone) continue;
   zone.Editing = true;
   zone.Status = "Restoring all cache zones before saving";
   zone.PublishStatus();
  }
  // Include disabled zones and orphan recovery records after a module is deleted.
  foreach (EBG_CacheGroup record : Records)
  {
   if (record.Group)
   {
    SCR_EditableGroupComponent editableGroup = SCR_EditableGroupComponent.Cast(SCR_EditableEntityComponent.GetEditableEntity(record.Group));
    if (editableGroup) editableGroup.EBG_UseWaypointOrderForSave();
   }
   if (record.Full && !record.Full.IsPending())
   { record.Recovery = ""; record.RecoveryRetryRequested = true; }
   if (record.Full || record.Simulation || record.PersistentScalarRollbackPending) record.WakeRequested = true;
  }
  EBG_FullSaveGate.RequestRestoreForSave();
 }
 protected ref set<SCR_AIGroup> m_SoldiersOnlySkipped = new set<SCR_AIGroup>();
 // The vanilla test first (SCR_AIGroupUtilityComponent.IsMilitary). Mod lists can
 // hand a group a different utility component or none, so the faction's own flag
 // is read directly as well; only a faction that says it is not military counts
 // as civilian. A group with no resolvable faction is treated as military, which
 // is what every version before 0.1.18 did with it.
 bool IsMilitaryGroup(SCR_AIGroup group)
 {
  SCR_AIGroupUtilityComponent groupUtility = SCR_AIGroupUtilityComponent.Cast(group.FindComponent(SCR_AIGroupUtilityComponent));
  if (groupUtility && groupUtility.IsMilitary()) return true;
  SCR_Faction faction = SCR_Faction.Cast(group.GetFaction());
  if (!faction)
  {
   FactionManager factions = GetGame().GetFactionManager();
   if (factions) faction = SCR_Faction.Cast(factions.GetFactionByKey(group.GetFactionName()));
  }
  if (!faction) return true;
  return faction.IsMilitary();
 }
 void Refresh(EBG_CacheZone zone, array<AIAgent> sharedAgents = null)
 {
  if (!Replication.IsServer() || !zone || !zone.Enabled || zone.Editing || zone.HasPendingSettings()) return;
  if (!EBG_MissionPersistence.Ready(this)) return;
  if (!Owners.Contains(zone)) Owners.Insert(zone);
  AIWorld world = GetGame().GetAIWorld();
  if (!world) return;
  array<AIAgent> agents = sharedAgents;
  if (!agents) { agents = {}; world.GetAIAgents(agents); }
  map<SCR_AIGroup, bool> visited = new map<SCR_AIGroup, bool>();
  foreach (AIAgent agent : agents)
  {
   SCR_AIGroup group = SCR_AIGroup.Cast(agent);
   if (!group) group = SCR_AIGroup.Cast(agent.GetParentGroup());
   if (!group || visited.Contains(group)) continue;
   PersistenceSystem enrollmentPersistence = PersistenceSystem.GetInstance();
   if (enrollmentPersistence && EBG_MissionPersistence.Reserves(enrollmentPersistence.GetId(group)) && !FindGroup(group)) continue;
   visited.Insert(group, true);
   if (FindGroup(group) || IsReserved(group) || group.EBG_Exclude || !group.EBG_HasCompletedInitialSpawn()) continue;
   if (group.GetPlayerCount() > 0 || group.IsSlave() || group.GetMaster() || group.IsCreatedByCommander()) continue;
   if (group.GetLifecyclePolicy() == SCR_EAIGroupLifecyclePolicy.ProximityDriven) continue;
   array<AIAgent> members = {};
   group.GetAgents(members);
   if (members.IsEmpty()) continue;
   bool inside;
   bool outside;
   bool unsafe;
   foreach (AIAgent memberAgent : members)
   {
    SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(memberAgent.GetControlledEntity());
    if (!character || !character.GetCharacterController() || !EBG_MissionPersistence.MayEnroll(character) || character.EBG_WasPlayerControlled() || (enrollmentPersistence && EBG_MissionPersistence.HasUnresolvedRelease(enrollmentPersistence.GetId(character)))) { unsafe = true; break; }
    if (character.GetCharacterController().IsDead()) continue;
    // A split/merged native group does not transfer logical member ownership.
    // Require release of the existing record before another group can enroll it.
    if (FindMember(character) || Regroup.ReservesMember(character)) { unsafe = true; break; }
    if (EBG_CacheGeometry.DistanceSq(character.GetOrigin(), zone.GetOrigin()) <= zone.Affected * zone.Affected) inside = true;
    else outside = true;
   }
   if (!inside || unsafe) continue;
   // Soldiers only: a civilian-faction group inside the zone is never enrolled.
   // Civilians are cached by EXPBG Ambient Civilians. Each skipped group is named
   // once in the server log so a Game Master can see why it stayed awake.
   if (zone.MilitaryOnly > 0 && !IsMilitaryGroup(group))
   {
    if (!m_SoldiersOnlySkipped.Contains(group))
    {
     m_SoldiersOnlySkipped.Insert(group);
     PrintFormat("[EBG] Soldiers only: zone left group %1 alone (faction '%2', utility=%3). Switch 'Soldiers only' off on the zone to cache it.", group, group.GetFactionName(), group.FindComponent(SCR_AIGroupUtilityComponent) != null);
    }
    continue;
   }
   EBG_CacheGroup record = new EBG_CacheGroup();
   record.Id = ++m_NextId;
   record.Zone = zone;
   record.Group = group;
   if (enrollmentPersistence) record.PersistentId = enrollmentPersistence.GetId(group);
   record.ActiveSince = Now();
   // Initial settling uses the module's minimum-active time. Combat has its
   // own cooldown only after an observed combat/danger event.
   record.LastUnsafe = Now() - 60;
   foreach (AIAgent memberAgent : members)
   {
    SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(memberAgent.GetControlledEntity());
    if (!character || character.GetCharacterController().IsDead()) continue;
    EBG_CacheMember member = new EBG_CacheMember();
    member.Id = ++m_NextId;
    member.Entity = character;
    if (enrollmentPersistence) member.PersistentId = enrollmentPersistence.GetId(character);
    member.Position = character.GetOrigin();
    record.Members.Insert(member);
   }
   Records.Insert(record);
   UpdateRecord(record);

  }
 }
 // Only the validated regroup commit calls this. Existing records/member objects
 // are reused whenever possible so cleanup and persistent history retain identity.
 EBG_CacheGroup AddRegroupRecord(EBG_CacheZone zone, SCR_AIGroup group)
 {
  EBG_CacheGroup record = new EBG_CacheGroup();
  record.Id = ++m_NextId; record.Zone = zone; record.Group = group;
  PersistenceSystem system = PersistenceSystem.GetInstance();
  if (system) record.PersistentId = system.GetId(group);
  record.ActiveSince = Now(); record.LastUnsafe = Now() - 60;
  Records.Insert(record); return record;
 }
 EBG_CacheMember AddRegroupMember(EBG_CacheGroup record, SCR_ChimeraCharacter entity)
 {
  EBG_CacheMember member = new EBG_CacheMember(); member.Id = ++m_NextId;
  member.Entity = entity; member.Position = entity.GetOrigin();
  PersistenceSystem system = PersistenceSystem.GetInstance();
  if (system) member.PersistentId = system.GetId(entity);
  record.Members.Insert(member); return member;
 }
 EBG_CacheMember AddCachedMember(EBG_CacheGroup record, vector position, bool dead)
 {
  EBG_CacheMember member = new EBG_CacheMember(); member.Id = ++m_NextId;
  member.Position = position; member.Dead = dead; record.Members.Insert(member); return member;
 }
 void PlayerPossession(IEntity entity)
 {
  SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(entity);
  if (character) character.EBG_MarkPlayerControlled();
  EBG_CacheMember member = FindMember(entity);
  if (member) member.WasPlayer = true;
  if (member)
   foreach (EBG_CacheGroup record : Records)
    if (record.Members.Contains(member) && record.Simulation && record.Simulation.Suspended) RestoreRecord(record);
  if (EBG_CacheCleanup.Instance) EBG_CacheCleanup.Instance.PlayerPossession(entity);
 }
 void ConfirmDeath(IEntity entity)
 {
  EBG_CacheMember member = FindMember(entity);
  if (!member || member.Dead) return;
  PersistenceSystem deathPersistence = PersistenceSystem.GetInstance();
  if (deathPersistence && !deathPersistence.GetId(entity).IsNull()) member.PersistentId = deathPersistence.GetId(entity);
  member.Dead = true;
  member.DiedAt = Now();
  member.Position = entity.GetOrigin();
  if (EBG_CacheCleanup.Instance)
   foreach (EBG_CacheGroup record : Records)
    if (record.Members.Contains(member)) EBG_CacheCleanup.Instance.ConfirmDeath(record, member, member.DiedAt);
 }
 protected void UpdatePlayers()
 {
  Players.Clear();
  array<int> ids = {};
  GetGame().GetPlayerManager().GetPlayers(ids);
  foreach (int id : ids)
  {
   SCR_ChimeraCharacter player = SCR_ChimeraCharacter.Cast(GetGame().GetPlayerManager().GetPlayerControlledEntity(id));
   if (!player || !player.GetCharacterController() || player.GetCharacterController().IsDead()) continue;
   Players.Insert(player.GetOrigin());
   PlayerPossession(player);
  }
 }
 // Native PhysicsContact ignores living active AI. A queued squadmate contact
 // stays harmless at native far LOD and while this addon suspends that squadmate.
 // Our own permanent-LOD pin must not turn the same contact into a wake trigger.
 static bool HasBlockingDanger(AIAgent agent)
 {
  if (!agent || agent.GetDangerEventsCount() > 128) return true;
  for (int i = 0; i < agent.GetDangerEventsCount(); i++)
  {
   int aggregated;
   AIDangerEvent danger = agent.GetDangerEvent(i, aggregated);
   if (!danger || danger.GetDangerType() != EAIDangerEventType.Danger_PhysicsContact) return true;
   IEntity other = danger.GetObject();
   CharacterControllerComponent controller;
   AIControlComponent control;
   if (other)
   {
    controller = CharacterControllerComponent.Cast(other.FindComponent(CharacterControllerComponent));
    control = AIControlComponent.Cast(other.FindComponent(AIControlComponent));
   }
   if (!controller || controller.GetLifeState() != ECharacterLifeState.ALIVE || controller.IsPlayerControlled() || !control) return true;
   if (control.IsAIActivated()) continue;
   SCR_ChimeraCharacter otherCharacter = SCR_ChimeraCharacter.Cast(other);
   AIAgent otherAgent = control.GetAIAgent();
   AIGroup group = agent.GetParentGroup();
   if (!otherCharacter || otherCharacter.EBG_WasPlayerControlled() || !otherAgent || !group || otherAgent.GetControlledEntity() != other || otherAgent.GetParentGroup() != group || otherAgent.GetLOD() != AIAgent.GetMaxLOD()) return true;
   if (otherAgent.GetPermanentLOD() != -1 && (!otherCharacter.EBG_IsSimulationCached() || otherAgent.GetPermanentLOD() != AIAgent.GetMaxLOD())) return true;
  }
  return false;
 }
 protected void UpdateRecord(EBG_CacheGroup record)
 {
  // Native absence is expected while the existing logical record owns a snapshot.
  if (record.Full)
  {
   record.Reason = EBG_CacheFullCoordinator.Status(record);
   if (record.RegroupReason != "") record.Reason += " | " + record.RegroupReason;
   return;
  }
  record.Alive = 0;
  record.Dead = 0;
  vector sum;
  float minY = float.MAX;
  float maxY = -float.MAX;
  bool combatObserved;
  record.Reason = "";
  foreach (EBG_CacheMember member : record.Members)
  {
   if (member.Dead) { record.Dead++; continue; }
   SCR_ChimeraCharacter entity = member.Entity;
   if (!entity) { member.Missing = true; record.Reason = "Missing member: external deletion or ownership change"; continue; }
   CharacterControllerComponent controller = entity.GetCharacterController();
   if (controller && controller.IsDead()) { ConfirmDeath(entity); record.Dead++; continue; }
   member.Position = entity.GetOrigin();
   record.Alive++;
   sum += member.Position;
   minY = Math.Min(minY, member.Position[1]);
   maxY = Math.Max(maxY, member.Position[1]);
   if (entity.EBG_WasPlayerControlled()) member.WasPlayer = true;
   if (member.WasPlayer) record.Reason = "Player-controlled or previously possessed member";
   else if (!EBG_StaticEmplacement.Unsupported(entity).IsEmpty() || !controller || controller.IsUnconscious() || controller.IsFalling() || controller.IsSwimming() || controller.IsClimbing())
    record.Reason = "Unsupported vehicle, medical or movement state";
   else if (entity.GetCharacterGroup() != record.Group) record.Reason = "Group roster changed; automatic reconciliation pending";
   SCR_CharacterDamageManagerComponent damage = SCR_CharacterDamageManagerComponent.Cast(entity.GetDamageManager());
   if (!damage || damage.IsBleeding()) record.Reason = "Medically unstable member: active bleeding";
   if (damage)
   {
    float health = damage.GetHealthScaled();
    if (member.LastHealth >= 0 && health < member.LastHealth - 0.0001)
    {
     combatObserved = true;
     record.Reason = "New damage observed";
    }
    member.LastHealth = health;
   }
   AIControlComponent control = entity.GetAIControlComponent();
   AIAgent agent;
   if (control) agent = control.GetAIAgent();
   bool dangerPending = agent && HasBlockingDanger(agent);
   CharacterInputContext input;
   if (controller) input = controller.GetInputContext();
   if (dangerPending || (damage && damage.IsBleeding()) || (controller && controller.IsMeleeAttack()) || (input && (input.WeaponIsPullingTrigger() || input.GetThrow() || input.GetMeleeAttack()))) combatObserved = true;
   if (!agent || (!record.Simulation && agent.GetPermanentLOD() >= 0) || dangerPending || (controller && EBG_SimulationCache.HasActiveOperation(controller)))
   {
    record.Reason = "Recent combat, active task or external AI control";
#ifdef EBG_ACCEPTANCE_TEST
    if (Now() - record.LastUnsafe >= 1 && agent && controller)
    {
     PrintFormat("[EBG MOD UNSAFE] member=%1 danger=%2 permanentLOD=%3 reload=%4 item=%5 changingItem=%6 changingFireMode=%7 melee=%8", entity.GetID(), agent.GetDangerEventsCount(), agent.GetPermanentLOD(), controller.IsReloading(), controller.IsUsingItem(), controller.IsChangingItem(), controller.IsChangingFireMode(), controller.IsMeleeAttack());
     CharacterInputContext diagnosticInput = controller.GetInputContext();
     for (int dangerIndex = 0; dangerIndex < Math.Min(agent.GetDangerEventsCount(), 8); dangerIndex++)
     {
      int aggregated;
      AIDangerEvent danger = agent.GetDangerEvent(dangerIndex, aggregated);
      if (danger) PrintFormat("[EBG MOD DANGER] type=%1 count=%2 object=%3 victim=%4 text='%5'", danger.GetDangerType(), aggregated, danger.GetObject(), danger.GetVictim(), danger.GetText());
     }
     if (diagnosticInput) PrintFormat("[EBG MOD UNSAFE INPUT] trigger=%1 reload=%2 throw=%3 melee=%4", diagnosticInput.WeaponIsPullingTrigger(), diagnosticInput.WeaponIsStartReloading(), diagnosticInput.GetThrow(), diagnosticInput.GetMeleeAttack());
    }
#endif
   }
  }
  if (record.Simulation && record.Simulation.Suspended)
   foreach (EBG_SimulationAgent cachedMember : record.Simulation.Members)
    if (cachedMember.Emplacement && !cachedMember.Emplacement.Matches(cachedMember.Character)) record.Reason = "Cached compartment changed; restoring original AI";
  if (record.Alive > 0)
  {
   record.Anchor = sum / record.Alive;
   record.MinY = minY;
   record.MaxY = maxY;
  }
  // Routine reload/item/fire-mode changes block this scan and restart the
  // configured clear delay; only observed combat starts the extra cooldown.
  if (combatObserved) record.LastUnsafe = Now();
  if (!record.Group && record.Dead != record.Members.Count()) record.Reason = "Group removed externally; survivors remain active";
  if (record.Group)
  {
   array<AIAgent> roster = {};
   record.Group.GetAgents(roster);
   foreach (AIAgent rosterAgent : roster)
   {
    SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(rosterAgent.GetControlledEntity());
    bool known;
    foreach (EBG_CacheMember ownMember : record.Members)
     if (character && ownMember.Entity == character) { known = true; break; }
    if (!known) record.Reason = "Group roster changed; automatic reconciliation pending";
   }
  }
  if (record.Recovery != "") record.Reason = record.Recovery;
  if (record.PersistenceIssue != "") record.Reason = "Saved ownership held: " + record.PersistenceIssue;
  if (record.RegroupReason != "") record.Reason = record.RegroupReason;
 }
 bool InVolume(EBG_CacheGroup record, bool sleep)
 {
  EBG_CacheZone zone = record.Zone;
  if (!zone) return true;
  vector center = record.Anchor;
  float radius = zone.GroupWake;
  if (sleep) radius = zone.GroupSleep;
  if (zone.Strategy == 0)
  {
   center = zone.GetOrigin();
   radius = zone.ZoneWake;
   if (sleep) radius = zone.ZoneSleep;
  }
  float margin;
  if (sleep) margin = zone.HeightMargin;
  if (EBG_CacheGeometry.AnyPlayer(Players, center, radius, record.MinY, record.MaxY, zone.Height != 0, zone.Above, zone.Below, margin)) return true;
  // A patrol stays protected after leaving its enrollment area.
  foreach (EBG_CacheMember member : record.Members)
  {
   if (member.Dead) continue;
   float memberRadius = zone.GroupWake;
   if (sleep) memberRadius = zone.GroupSleep;
   if (EBG_CacheGeometry.AnyPlayer(Players, member.Position, memberRadius, member.Position[1], member.Position[1], zone.Height != 0, zone.Above, zone.Below, margin)) return true;
  }
  return false;
 }
 void InvalidateProtection() { m_Protection.Clear(); }
 static bool NativeSaveBusy()
 {
  SaveGameManager saving = GetGame().GetSaveGameManager();
  return saving && saving.IsBusy();
 }
 protected void CountBlocked(EBG_CacheZone zone, EBG_CacheGroup record)
 {
  zone.SkippedCount++;
  if (zone.BlockedReason == "") zone.BlockedReason = record.Reason;
 }
 bool IsProtected(EBG_CacheGroup record, bool sleep)
 {
  EBG_CacheZone zone = record.Zone;
  if (!zone) return true;
  if (Players.IsEmpty()) return false;
  if (zone.Strategy != 0) return InVolume(record, sleep);
  EBG_ZoneProtection sample = m_Protection.Get(zone);
  if (!sample || sample.Origin != zone.GetOrigin())
  {
   sample = new EBG_ZoneProtection(); sample.Origin = zone.GetOrigin();
   m_Protection.Set(zone, sample);
  }
  if (sleep && sample.SleepKnown) return sample.Sleep;
  if (!sleep && sample.WakeKnown) return sample.Wake;
  bool protectedArea;
  foreach (EBG_CacheGroup peer : Records)
   if (peer.Zone == zone && InVolume(peer, sleep)) { protectedArea = true; break; }
  if (sleep) { sample.SleepKnown = true; sample.Sleep = protectedArea; }
  else { sample.WakeKnown = true; sample.Wake = protectedArea; }
  return protectedArea;
 }
 // Wake progresses without repeating the expensive ownership/enrollment scan.
 // The native coordinator serializes requests and retains pending callbacks.
 void Pump()
 {
  if (Unloading || EBG_CacheSnapshot.Loading || !GetGame() || !GetGame().InPlayMode() || !Replication.IsServer() || GetGame().GetWorld() != m_World) return;
  // New/loaded zones are not Owners yet. Progress their deferred registration
  // before the native-load gate, without relying on rendering/entity frames.
  foreach (EBG_CacheZone loadingZone : EBG_CacheZone.Zones)
  {
   if (loadingZone) loadingZone.EBG_UpdateSettingsLoad();
  }
  if (!EBG_MissionPersistence.Ready(this)) { EBG_OptimizerControl.Poll(this); return; }
  m_PumpTicks++;
  if (m_PumpTicks % 5 == 0) { Tick(); return; }
  InvalidateProtection();
  EBG_CacheFullCoordinator.Tick(this);
 }
 void Tick()
 {
  if (EBG_CacheSnapshot.Loading) return;
  if (!GetGame() || !GetGame().InPlayMode() || !Replication.IsServer() || GetGame().GetWorld() != m_World) return;
  if (!EBG_MissionPersistence.Ready(this)) return;
  EBG_OptimizerControl.HoldZones();
  UpdatePlayers();
  m_Ticks++;
  if (m_Ticks % 10 == 0) Regroup.Scan(this);
  array<AIAgent> enrollmentAgents;
  foreach (EBG_CacheZone zone : Owners)
  {
   if (!zone) continue;
   zone.SyncCleanupPolicy();
   if (zone.Enabled && m_Ticks % 10 == 0)
   {
    AIWorld aiWorld = GetGame().GetAIWorld();
    if (aiWorld)
    {
     if (!enrollmentAgents) { enrollmentAgents = {}; aiWorld.GetAIAgents(enrollmentAgents); }
     Refresh(zone, enrollmentAgents);
    }
   }
   zone.ManagedCount = 0; zone.AliveCount = 0; zone.DeadCount = 0; zone.SkippedCount = 0; zone.CachedCount = 0; zone.PendingCount = 0; zone.RecoveryCount = 0; zone.BlockedReason = "";
  }
  foreach (EBG_CacheGroup record : Records) UpdateRecord(record);
  InvalidateProtection();
  float now = Now();
  // Restore before admitting sleep work; at most one normal transition per tick.
  // Urgent restores first, then the cached group nearest a player, within the
  // shared wake budget. A deferred wake does not hold other work this tick.
  bool transitioned;
  EBG_CacheGroup waking;
  float wakeScore;
  bool wakeUrgent;
  foreach (EBG_CacheGroup dormant : Records)
  {
   if (!dormant.Simulation || !dormant.Simulation.Suspended) continue;
   EBG_CacheZone owner = dormant.Zone;
   bool urgent = !owner || dormant.Reason != "";
   bool wake = urgent || dormant.WakeRequested || dormant.ReleaseRequested || !owner.Enabled || owner.Editing || owner.HasPendingSettings() || owner.Mode != 0;
   if (owner && IsProtected(dormant, false)) wake = true;
   if (!wake) continue;
   float score = EBG_CacheFullCoordinator.Priority(this, dormant);
   if (urgent) score = -2;
   if (waking && score >= wakeScore) continue;
   waking = dormant; wakeScore = score; wakeUrgent = urgent;
  }
  if (waking)
  {
   int wakeCost = waking.Simulation.Members.Count();
   if (TakeWakeBudget(wakeCost, wakeUrgent))
   {
    RestoreRecord(waking);
    transitioned = true;
   }
   else ReportWakeDeferred(waking, wakeCost);
  }
  // This scan tick gives urgent Simulation wakes priority. Between scans the
  // lightweight pump advances at most one serialized Full request per call.
  // Retained restores outlive their initiating settings edit. Retry one fairly,
  // at most every five seconds per record, even if that edit was reverted.
  if (!transitioned && !Records.IsEmpty())
   for (int retryIndex = 0; retryIndex < Records.Count(); retryIndex++)
   {
    int index = (m_RecoveryCursor + retryIndex) % Records.Count();
    EBG_CacheGroup recovery = Records[index];
    if (recovery.Full || now < recovery.RecoveryNextAttempt) continue;
    if (!recovery.PersistentScalarRollbackPending && (!recovery.Simulation || recovery.Simulation.Suspended || recovery.Recovery == "")) continue;
    RestoreRecord(recovery); m_RecoveryCursor = (index + 1) % Records.Count(); transitioned = true; break;
   }
  if (!transitioned) transitioned = EBG_CacheFullCoordinator.Tick(this);
  for (int releasedIndex = Records.Count() - 1; releasedIndex >= 0; releasedIndex--)
  {
   EBG_CacheGroup releasedRecord = Records[releasedIndex];
   if (releasedRecord.PersistentScalarRollbackPending) continue;
   if (EBG_CacheCleanup.Instance && EBG_CacheCleanup.Instance.CanRetire(releasedRecord))
   {
    EBG_CacheCleanup.Instance.ReleaseGroup(releasedRecord);
    Records.Remove(releasedIndex);
    continue;
   }
   if (!releasedRecord.ReleaseRequested || releasedRecord.Full != null || releasedRecord.Simulation != null) continue;
   if (EBG_CacheCleanup.Instance) EBG_CacheCleanup.Instance.ReleaseGroup(releasedRecord);
   Records.Remove(releasedIndex);
  }
  InvalidateProtection();
  foreach (EBG_CacheGroup record : Records)
  {
   EBG_CacheZone zone = record.Zone;
   if (!zone) continue;
   zone.ManagedCount++;
   zone.AliveCount += record.Alive;
   zone.DeadCount += record.Dead;
   if (record.Full)
   {
    EBG_CacheRecordState fullState = record.CacheState();
    if (fullState == EBG_CacheRecordState.FULL_CACHED) zone.CachedCount++;
    else if (fullState == EBG_CacheRecordState.RECOVERY) zone.RecoveryCount++;
    else if (fullState == EBG_CacheRecordState.PENDING) zone.PendingCount++;
    record.Reason = EBG_CacheFullCoordinator.Status(record);
    if (record.RegroupReason != "") record.Reason += " | " + record.RegroupReason;
    if (fullState == EBG_CacheRecordState.RECOVERY || record.RegroupReason != "") CountBlocked(zone, record);
    // Casualty remains outlive the survivors' Full snapshot. Survivor rows were
    // forgotten at capture (ForgetPrefabMember); only dead members' rows remain
    // deletable. Never during a Full transition, recovery or a transfer token.
    if (fullState == EBG_CacheRecordState.FULL_CACHED && !record.FullCleanup && record.CleanupRegistered && EBG_CacheCleanup.Instance)
     EBG_CacheCleanup.Instance.Tick(record, Players, now);
    else
    {
     // No cleanup tick: continuous clearance restarts and stale progress is hidden.
     record.CleanupClearSince = -1; record.CleanupStatus = ""; record.CleanupPhase = "";
    }
    continue;
   }
   if (record.CacheState() == EBG_CacheRecordState.RECOVERY)
   {
    zone.RecoveryCount++; CountBlocked(zone, record);
    record.CleanupClearSince = -1; record.CleanupStatus = ""; record.CleanupPhase = "";
    continue;
   }
   if (record.Simulation && record.Simulation.Suspended)
   {
    zone.CachedCount++;
    record.Reason = "Simulation cached";
    // Corpses of members that died before suspension are not in the snapshot.
    // Cleanup touches no Reason/ClearSince/LastUnsafe, so caching is unchanged.
    if (record.CleanupRegistered && EBG_CacheCleanup.Instance) EBG_CacheCleanup.Instance.Tick(record, Players, now);
    continue;
   }
   if (zone.Enabled && !record.CleanupRegistered && record.PersistenceIssue == "")
   {
    EBG_CacheCleanup.Get().RegisterGroup(record);
    record.CleanupRegistered = true;
    foreach (EBG_CacheMember casualty : record.Members)
     if (casualty.Dead) EBG_CacheCleanup.Get().ConfirmDeath(record, casualty, casualty.DiedAt);
   }
   if (EBG_CacheCleanup.Instance) EBG_CacheCleanup.Instance.Tick(record, Players, now);
   if (!zone.Enabled) record.CleanupRegistered = false;
   if (record.Alive == 0 && record.Dead == record.Members.Count())
   {
    record.Reason = "Eliminated; zone disabled";
    if (zone.Enabled && EBG_CacheCleanup.Instance) record.Reason = EBG_CacheCleanup.Instance.GetLastReason();
    continue;
   }
   bool playerNear = IsProtected(record, true);
   // Free GM cameras are not activation candidates. Zero controlled players is clear.
   if (!zone.Enabled || zone.Editing || zone.HasPendingSettings() || playerNear || record.Reason != "" || now - record.LastUnsafe < 60)
   {
    record.ClearSince = -1;
    if (record.Reason != "") CountBlocked(zone, record);
    else if (!zone.Enabled) record.Reason = "Disabled";
    else if (zone.Editing) record.Reason = "Held active for editing";
    else if (zone.HasPendingSettings()) record.Reason = "Waiting for saved groups to restore before applying settings";
    else if (playerNear)
    {
     if (zone.Strategy == 0) record.Reason = string.Format("Whole zone awake: player near module or any managed member (sleep radius %1 m)", zone.ZoneSleep);
     else record.Reason = string.Format("Group awake: player near group anchor or member (sleep radius %1 m)", zone.GroupSleep);
    }
    else
    {
     record.Reason = string.Format("Safety cooldown: %1 seconds", Math.Max(0, Math.Ceil(60 - (now - record.LastUnsafe))));
     if (record.LastCacheRejection != "") record.Reason += " | " + record.LastCacheRejection;
    }
    continue;
   }
   if (record.ClearSince < 0) record.ClearSince = now;
   if (!EBG_CacheGeometry.CanSleep(now, record.ActiveSince, record.ClearSince, zone.MinimumActive, zone.SleepDelay))
   {
    record.Reason = string.Format("Sleep countdown: %1 seconds", Math.Ceil(Math.Max(zone.MinimumActive - (now - record.ActiveSince), zone.SleepDelay - (now - record.ClearSince))));
    continue;
   }
   if (record.Alive == 0) continue;
   if (zone.Mode == 0)
   {
    // Native saves may run while zones are enabled. Do not hide AI mid-save;
    // Full captures already wait through the save gate.
    if (transitioned || NativeSaveBusy()) continue;
    string reason;
    record.Simulation = EBG_SimulationCache.Suspend(record.Group, reason);
    record.Reason = reason;
    transitioned = true;
    if (record.Simulation && record.Simulation.PreparationFailed)
    { record.Recovery = reason; zone.RecoveryCount++; CountBlocked(zone, record); record.DebugStatus(now); }
    else if (record.Simulation) { record.LastCacheRejection = ""; zone.CachedCount++; }
    else { record.LastCacheRejection = record.Reason; record.LastUnsafe = now; CountBlocked(zone, record); }
   }
   else
   {
    if (transitioned || EBG_FullCacheGroup.IsNativeOperationBusy()) continue;
    transitioned = true;
    if (EBG_CacheFullCoordinator.Sleep(this, record)) { record.LastCacheRejection = ""; zone.CachedCount++; }
    else { record.LastCacheRejection = record.Reason; record.LastUnsafe = now; CountBlocked(zone, record); if (record.CacheState() == EBG_CacheRecordState.RECOVERY) zone.RecoveryCount++; }
   }
  }
  foreach (EBG_CacheGroup diagnostic : Records) diagnostic.DebugStatus(now);
  foreach (EBG_CacheZone settingsZone : Owners)
  {
   if (!settingsZone) continue;
   settingsZone.ApplyPendingSettings();
   // A queued Enable must not lift the explicit save hold as restoration ends.
   if (EBG_FullSaveGate.IsRestoreRequested() || EBG_OptimizerControl.Preparing) settingsZone.Editing = true;
  }
  EBG_FullSaveGate.Poll(this);
  EBG_OptimizerControl.Poll(this);
  foreach (EBG_CacheZone zone : Owners)
  {
   if (!zone) continue;
   if (!zone.Enabled && zone.CachedCount > 0) zone.Status = "Disabled; saved groups are restoring";
   else if (!zone.Enabled) zone.Status = "Disabled; managed units remain active";
   else if (zone.HasPendingSettings()) zone.Status = "Restoring groups before applying queued settings";
   else if (EBG_OptimizerControl.Preparing) zone.Status = EBG_OptimizerControl.Message;
   else if (zone.Editing) zone.Status = "Held active for editing";
   else zone.Status = string.Format("%1 groups / %2 survivors / %3 cached / %4 pending / %5 recovery / %6 blocked", zone.ManagedCount, zone.AliveCount, zone.CachedCount, zone.PendingCount, zone.RecoveryCount, zone.SkippedCount);
   if (!zone.Editing && zone.SkippedCount > 0 && !zone.BlockedReason.IsEmpty()) zone.Status += " | " + zone.BlockedReason;
   string saveStatus = EBG_FullSaveGate.GetStatus();
   if (saveStatus != "") zone.Status += " | " + saveStatus;
   zone.PublishStatus();
  }
 }
}
modded class SCR_AIGroup
{
 // 1.8 queue completion survives later casualties. IsInitializing and
 // GetSpawnQueueSize are compatibility stubs in the installed game scripts.
 bool EBG_HasCompletedInitialSpawn()
 {
  return m_bOnInitInvoked || !m_aUnitPrefabSlots || m_aUnitPrefabSlots.IsEmpty();
 }
 [Attribute("0", UIWidgets.CheckBox, "Exclude mission-critical group from EXPBG optimization", category: "EXPBG GM Optimizer")]
 bool EBG_Exclude;
}
modded class SCR_CharacterControllerComponent
{
  override protected void OnDeath(IEntity instigatorEntity, notnull Instigator instigator)
  {
   // Native super invokes death listeners and garbage insertion. Record the
   // monotonic veto while the actual owner association is still available.
   if (Replication.IsServer() && EBG_CacheManager.Instance) EBG_CacheManager.Instance.ConfirmDeath(GetOwner());
   super.OnDeath(instigatorEntity, instigator);
  }
 override protected void OnControlledByPlayer(IEntity owner, bool controlled)
 {
   // Native super invokes control listeners and changes inventory setup.
   if (controlled && Replication.IsServer())
   {
    SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(owner);
    if (character) character.EBG_MarkPlayerControlled();
    if (EBG_CacheManager.Instance) EBG_CacheManager.Instance.PlayerPossession(owner);
   }
   super.OnControlledByPlayer(owner, controlled);
 }
}

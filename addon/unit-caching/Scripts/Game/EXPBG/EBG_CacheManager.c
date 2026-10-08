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
 // Non-empty while an optional module keeps this record awake (EBG_CacheManager.
 // KeepAwakeReason); the scheduler logs each change. Runtime only; never persisted.
 string KeepAwake;
 // Per-group activation only: this record's protection verdicts, valid while
 // ProtectionVersion equals the manager's (EBG_CacheManager.IsProtected). Runtime only.
 ref EBG_ZoneProtection ProtectionSample;
 int ProtectionVersion = -1;
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
// Shared until the manager invalidates it: every tick, and a pump only after a record,
// member or Full transition changed (EBG_CacheManager.MarkProtectionDirty). Native/member
// sampling remains unchanged.
class EBG_ZoneProtection
{
 vector Origin;
 bool WakeKnown, Wake, SleepKnown, Sleep;
 // The zone settings the verdicts were computed with; any difference recomputes them.
 int Strategy, ZoneWake, ZoneSleep, GroupWake, GroupSleep, Height, Above, Below, HeightMargin;
 void Capture(EBG_CacheZone zone)
 {
  Origin = zone.GetOrigin();
  Strategy = zone.Strategy;
  ZoneWake = zone.ZoneWake;
  ZoneSleep = zone.ZoneSleep;
  GroupWake = zone.GroupWake;
  GroupSleep = zone.GroupSleep;
  Height = zone.Height;
  Above = zone.Above;
  Below = zone.Below;
  HeightMargin = zone.HeightMargin;
 }
 bool Matches(EBG_CacheZone zone)
 {
  return Origin == zone.GetOrigin() && Strategy == zone.Strategy && ZoneWake == zone.ZoneWake && ZoneSleep == zone.ZoneSleep && GroupWake == zone.GroupWake && GroupSleep == zone.GroupSleep && Height == zone.Height && Above == zone.Above && Below == zone.Below && HeightMargin == zone.HeightMargin;
 }
}
// One AI group as an enrollment pass sees it: its agents and its living soldiers'
// positions, read once per pass and shared by every zone of that pass
// (EBG_CacheManager.EnrollmentCandidates). Nothing here decides enrollment.
class EBG_EnrollmentCandidate
{
 SCR_AIGroup Group;
 ref array<AIAgent> Members = {};
 ref array<vector> Living = {};
 bool SpawnDone;
 vector GroupOrigin;
 // The same member reads EBG_EnrollmentTally.Nearby makes, for every member.
 void Sample(SCR_AIGroup group)
 {
  Group = group;
  group.GetAgents(Members);
  foreach (AIAgent member : Members)
  {
   if (!member) continue;
   SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(member.GetControlledEntity());
   if (!character) continue;
   CharacterControllerComponent controller = character.GetCharacterController();
   if (!controller || controller.IsDead()) continue;
   Living.Insert(character.GetOrigin());
  }
  // Nearby reads these only when no soldier lives.
  if (!Living.IsEmpty()) return;
  SpawnDone = group.EBG_HasCompletedInitialSpawn();
  GroupOrigin = group.GetOrigin();
 }
}
// Soldiers only, per zone: the first civilian group it leaves alone is named in full,
// later ones are counted and summarised at most every 300 s. Log text only.
class EBG_SoldiersOnlyNote
{
 int Skipped;
 float NextSummary;
}
// One enrollment pass of one zone: why groups inside its affected radius stayed
// out. Text for the zone status and the Game Master notice only; enrollment,
// caching and ownership never read it.
class EBG_EnrollmentTally
{
 int LeftSquad;
 ref array<string> Reasons = {};
 ref array<int> Counts = {};
 void Add(string reason)
 {
  int index = Reasons.Find(reason);
  if (index < 0)
  {
   Reasons.Insert(reason);
   Counts.Insert(1);
   return;
  }
  Counts[index] = Counts[index] + 1;
 }
 // Squads another EXPBG module caches itself (EBG_CacheManager.DescribeExternalCache:
 // Garrison), counted by module and cache state. They are cached, just not by this zone.
 ref array<string> ExternalModules = {};
 ref array<string> ExternalStates = {};
 ref array<int> ExternalCounts = {};
 void AddExternal(string moduleName, string cacheState)
 {
  for (int i = 0; i < ExternalStates.Count(); i++)
  {
   if (ExternalModules[i] != moduleName || ExternalStates[i] != cacheState) continue;
   ExternalCounts[i] = ExternalCounts[i] + 1;
   return;
  }
  ExternalModules.Insert(moduleName);
  ExternalStates.Insert(cacheState);
  ExternalCounts.Insert(1);
 }
 // "Cached by EXPBG Garrison itself, with its own wake and sleep distances: 3 groups
 // Simulation cached (CDF loaded), 1 group awake on their posts". One clause per module.
 string ExternalNote()
 {
  string text;
  array<string> named = {};
  foreach (string moduleName : ExternalModules)
  {
   if (named.Contains(moduleName)) continue;
   named.Insert(moduleName);
   string clause = string.Empty;
   for (int i = 0; i < ExternalModules.Count(); i++)
   {
    if (ExternalModules[i] != moduleName) continue;
    if (!clause.IsEmpty()) clause += ", ";
    string groups = "groups";
    if (ExternalCounts[i] == 1) groups = "group";
    clause += string.Format("%1 %2 %3", ExternalCounts[i], groups, ExternalStates[i]);
   }
   if (!text.IsEmpty()) text += " | ";
   text += string.Format("Cached by %1 itself, with its own wake and sleep distances: %2", moduleName, clause);
  }
  return text;
 }
 // Soldiers who left their squad for good (EBG_MarkLeftSquad: AI Surrender's
 // prisoners). AI Surrender switches their AI off, so the AI world's agent list
 // no longer holds them; this server list does. Weak entity handles, pruned here.
 protected static ref array<SCR_ChimeraCharacter> s_LeftSquad;
 static void TrackLeftSquad(SCR_ChimeraCharacter character)
 {
		EXPBG_LazyStatics_EBG_EnrollmentTally();
  if (character && Replication.IsServer() && !s_LeftSquad.Contains(character)) s_LeftSquad.Insert(character);
 }
 // Living soldiers outside any group who left their squad, inside the radius. They
 // are never enrolled; the GM is told so.
 void CountLeftSquad(vector origin, float radiusSq)
 {
		EXPBG_LazyStatics_EBG_EnrollmentTally();
  for (int i = s_LeftSquad.Count() - 1; i >= 0; i--)
  {
   SCR_ChimeraCharacter character = s_LeftSquad[i];
   CharacterControllerComponent controller;
   if (character) controller = character.GetCharacterController();
   if (!controller || controller.IsDead())
   {
    s_LeftSquad.Remove(i);
    continue;
   }
   if (character.GetCharacterGroup()) continue;
   if (EBG_CacheGeometry.DistanceSq(character.GetOrigin(), origin) <= radiusSq) LeftSquad++;
  }
 }
 // A living soldier inside the radius, or a group still spawning at a point inside it.
 static bool Nearby(SCR_AIGroup group, array<AIAgent> members, vector origin, float radiusSq)
 {
  bool living;
  foreach (AIAgent member : members)
  {
   if (!member) continue;
   SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(member.GetControlledEntity());
   if (!character) continue;
   CharacterControllerComponent controller = character.GetCharacterController();
   if (!controller || controller.IsDead()) continue;
   living = true;
   if (EBG_CacheGeometry.DistanceSq(character.GetOrigin(), origin) <= radiusSq) return true;
  }
  return !living && !group.EBG_HasCompletedInitialSpawn() && EBG_CacheGeometry.DistanceSq(group.GetOrigin(), origin) <= radiusSq;
 }
 // The same verdict from a pass's candidate (EBG_EnrollmentCandidate.Sample).
 static bool Nearby(EBG_EnrollmentCandidate candidate, vector origin, float radiusSq)
 {
  foreach (vector living : candidate.Living)
  {
   if (EBG_CacheGeometry.DistanceSq(living, origin) <= radiusSq)
   {
    return true;
   }
  }
  return candidate.Living.IsEmpty() && !candidate.SpawnDone && EBG_CacheGeometry.DistanceSq(candidate.GroupOrigin, origin) <= radiusSq;
 }
 string Note(int affected, bool managed)
 {
  string text;
  for (int i = 0; i < Reasons.Count(); i++)
  {
   if (text.IsEmpty()) text = "Not enrolled: ";
   else text += "; ";
   string groups = "groups";
   if (Counts[i] == 1) groups = "group";
   text += string.Format("%1 %2 %3", Counts[i], groups, Reasons[i]);
  }
  if (text.IsEmpty() && !managed && ExternalStates.IsEmpty()) text = string.Format("No AI group with a free living soldier inside the %1 m affected radius", affected);
  string cachedElsewhere = ExternalNote();
  if (!cachedElsewhere.IsEmpty())
  {
   if (text.IsEmpty()) text = cachedElsewhere;
   else text = cachedElsewhere + " | " + text;
  }
  if (LeftSquad > 0)
  {
   if (!text.IsEmpty()) text += " | ";
   text += string.Format("%1 prisoners or soldiers who left their squad nearby are never cached", LeftSquad);
  }
  return text;
 }

	//------------------------------------------------------------------------------------------------
	//! Creates the collections on first use (not in the global static initializer, which has a
	//! per-function instruction limit that large modsets exceed on Windows).
	protected static void EXPBG_LazyStatics_EBG_EnrollmentTally()
	{
		if (!s_LeftSquad)
			s_LeftSquad = new array<SCR_ChimeraCharacter>();
	}
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
 // Protection verdicts only change with players (each tick), records, members, Full
 // transitions and zone settings. Pumps between ticks recompute them only when marked
 // dirty; each invalidation starts a new version for the per-group verdicts.
 protected bool m_ProtectionDirty = true;
 protected int m_ProtectionVersion;
 // Possession safety net (OnControlledByPlayer handles live possession at once). A
 // player's possession pass reruns when his controlled entity changes, when a record,
 // member or ledger binding changed (BumpRoster, EBG_CacheCleanup.s_EBG_BindVersion), and
 // on his round-robin turn, so every player is still re-covered within N ticks.
 protected ref map<int, IEntity> m_PossessionSeen = new map<int, IEntity>();
 protected ref array<int> m_PlayerIds = {};
 protected int m_RosterVersion;
 protected int m_SeenRoster = -1;
 protected int m_SeenBind = -1;
 protected int m_PossessionCursor;
 // The enrollment pass's candidates, valid for the agent list and tick that built them.
 // Tick releases both after its zone loop.
 protected ref array<ref EBG_EnrollmentCandidate> m_Candidates;
 protected ref array<AIAgent> m_CandidateAgents;
 protected int m_CandidatesTick = -1;
 // Soldiers only: the per-zone log state (NoteSoldiersOnly, FlushSoldiersOnly).
 protected ref map<EBG_CacheZone, ref EBG_SoldiersOnlyNote> m_SoldiersOnlyNotes = new map<EBG_CacheZone, ref EBG_SoldiersOnlyNote>();
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
   Instance.BumpRoster();
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
 // A record or a member binding changed: every player's possession pass reruns at the
 // next tick and protection is recomputed at the next pump.
 void BumpRoster()
 {
  m_RosterVersion++;
  MarkProtectionDirty();
 }
 // An input of the protection verdicts changed outside the tick.
 void MarkProtectionDirty()
 {
  m_ProtectionDirty = true;
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
 // From this world's OnBeforeWorldCleanup until the next world: Optimizer state is
 // already released, so no capture may vouch for it.
 static bool IsCleaningUpCurrentWorld()
 {
  if (!Unloading || !GetGame() || s_UnloadingWorld != GetGame().GetWorld())
   return false;
  SCR_BaseGameMode mode = SCR_BaseGameMode.Cast(GetGame().GetGameMode());
  return !mode || mode == s_UnloadingMode;
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
   BumpRoster();
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
  // The zone, group and member bindings above are re-read on every binding call.
  BumpRoster();
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
 // Published seam: true while cached or transitional state of this squad is held
 // (Simulation snapshot, Full transaction or recovery; Garrison adds its records).
 // A module that would take a living member out of the squad (AI Surrender) waits
 // until the squad is plainly awake, so no snapshot ever restores a prisoner.
 static bool IsCacheHeld(SCR_AIGroup group)
 {
  if (!group || !Instance) return false;
  EBG_CacheGroup record = Instance.FindGroup(group);
  if (!record) return false;
  if (record.Full || record.Simulation || record.PersistentScalarRollbackPending) return true;
  return record.Recovery != "";
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
 // Published seam, the sleep-side twin of IsReserved. Enrollment consults only
 // IsReserved, so a module that starts running live scripts on an already enrolled
 // squad (EXPBG Unit Scripts) returns a reason here: the record then never starts
 // a Simulation or Full sleep and a suspended record restores. Empty (the default)
 // resumes the normal clear-delay rules. Read once per scheduler tick and record.
 string KeepAwakeReason(SCR_AIGroup group)
 {
  return string.Empty;
 }
 // Published seam, text only: a module that caches a reserved squad itself (EXPBG
 // Garrison, with its own wake and sleep distances) names itself and the squad's
 // cache state, so the zone status does not count that squad as uncached. False
 // (the default) when no module claims it. Enrollment and caching never read it.
 bool DescribeExternalCache(SCR_AIGroup group, out string moduleName, out string cacheState)
 {
  moduleName = string.Empty;
  cacheState = string.Empty;
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
  // Records leave the zone or the manager below; its whole-zone verdicts change.
  MarkProtectionDirty();
  m_SoldiersOnlyNotes.Remove(zone);
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
     BumpRoster();
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
    BumpRoster();
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
 // A loaded record of an eliminated group: nothing alive to own; forgotten, not held.
 void ForgetEliminated(EBG_CacheGroup record)
 {
  if (!record) return;
  if (EBG_CacheCleanup.Instance) EBG_CacheCleanup.Instance.ReleaseGroup(record);
  Records.RemoveItem(record);
  BumpRoster();
 }
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
   BumpRoster();
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
 // Deleted groups leave null entries in the set of weak group handles; drop them first.
 protected void PruneSoldiersOnlySkipped()
 {
  for (int i = m_SoldiersOnlySkipped.Count() - 1; i >= 0; i--)
  {
   if (!m_SoldiersOnlySkipped.Get(i)) m_SoldiersOnlySkipped.Remove(i);
  }
 }
 // The first civilian group a zone leaves alone is named in full; later ones are counted.
 protected void NoteSoldiersOnly(EBG_CacheZone zone, SCR_AIGroup group)
 {
  EBG_SoldiersOnlyNote note = m_SoldiersOnlyNotes.Get(zone);
  if (note)
  {
   note.Skipped++;
   return;
  }
  note = new EBG_SoldiersOnlyNote();
  note.NextSummary = Now() + 300;
  m_SoldiersOnlyNotes.Set(zone, note);
  PrintFormat("[EBG] Soldiers only: zone left group %1 alone (faction '%2', utility=%3). Switch 'Soldiers only' off on the zone to cache it.", group, group.GetFactionName(), group.FindComponent(SCR_AIGroupUtilityComponent) != null);
 }
 // At most one count line per zone every 300 s, and none while nothing new was skipped.
 protected void FlushSoldiersOnly(EBG_CacheZone zone)
 {
  EBG_SoldiersOnlyNote note = m_SoldiersOnlyNotes.Get(zone);
  if (!note || note.Skipped == 0) return;
  float now = Now();
  if (now < note.NextSummary) return;
  PrintFormat("[EBG] Soldiers only: %1 more civilian groups skipped near %2", note.Skipped, zone.GetOrigin());
  note.Skipped = 0;
  note.NextSummary = now + 300;
 }
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
  // Every distinct group in first-seen agent order, read once per enrollment pass.
  array<ref EBG_EnrollmentCandidate> candidates = EnrollmentCandidates(agents, sharedAgents != null);
  PersistenceSystem enrollmentPersistence = PersistenceSystem.GetInstance();
  vector zoneOrigin = zone.GetOrigin();
  float affectedSq = zone.Affected * zone.Affected;
  // Diagnostics only: why groups inside the affected radius stayed out. The
  // enrollment decision below is unchanged; only the order of its pure checks
  // now names the first one that refused a group.
  EBG_EnrollmentTally tally = new EBG_EnrollmentTally();
  bool soldiersOnlyPruned;
  foreach (EBG_EnrollmentCandidate candidate : candidates)
  {
   SCR_AIGroup group = candidate.Group;
   if (!group) continue;
   array<AIAgent> members = candidate.Members;
   if (EBG_DebugChecks.Enabled) CheckCandidate(candidate, zoneOrigin, affectedSq);
   // A group with no living soldier inside the radius is never enrolled; one
   // still spawning at a point inside it is only reported.
   if (!EBG_EnrollmentTally.Nearby(candidate, zoneOrigin, affectedSq)) continue;
   EBG_CacheGroup existing = FindGroup(group);
   if (existing)
   {
    if (existing.Zone != zone) tally.Add("managed by another cache zone");
    continue;
   }
   string skip;
   if (enrollmentPersistence && EBG_MissionPersistence.Reserves(enrollmentPersistence.GetId(group))) skip = "waiting for saved mission ownership to bind";
   else if (IsReserved(group))
   {
    // Text only: a module that caches the squad itself (Garrison) reports its own
    // cache state, so the zone never implies that squad is left uncached.
    string externalModule, externalState;
    if (DescribeExternalCache(group, externalModule, externalState))
    {
     tally.AddExternal(externalModule, externalState);
     continue;
    }
    // Text only: a module that also publishes a keep-awake reason (Unit Scripts) names itself.
    string holder = KeepAwakeReason(group);
    if (holder.IsEmpty()) skip = "held by another EXPBG module (Unit Scripts, ambient crowds) or a pending cache transfer";
    else skip = "held by another EXPBG module (" + holder + ")";
   }
   else if (group.EBG_Exclude) skip = "marked Exclude from EXPBG optimization";
   else if (!group.EBG_HasCompletedInitialSpawn()) skip = "still spawning members";
   else if (group.GetPlayerCount() > 0) skip = "containing a player";
   else if (group.IsSlave() || group.GetMaster() || group.IsCreatedByCommander()) skip = "commanded by another group or the commander";
   else if (group.GetLifecyclePolicy() == SCR_EAIGroupLifecyclePolicy.ProximityDriven) skip = "run by the vanilla proximity spawner";
   else skip = MemberSkip(members, enrollmentPersistence);
   // Soldiers only: a civilian-faction group inside the zone is never enrolled.
   // Civilians are cached by EXPBG Ambient Civilians. The first skipped group of a
   // zone is named in the server log so a Game Master can see why it stayed awake;
   // later ones are counted (NoteSoldiersOnly, FlushSoldiersOnly).
   if (skip.IsEmpty() && zone.MilitaryOnly > 0 && !IsMilitaryGroup(group))
   {
    if (!soldiersOnlyPruned)
    {
     PruneSoldiersOnlySkipped();
     soldiersOnlyPruned = true;
    }
    if (!m_SoldiersOnlySkipped.Contains(group))
    {
     m_SoldiersOnlySkipped.Insert(group);
     NoteSoldiersOnly(zone, group);
    }
    skip = "of a civilian faction ('Soldiers only' is on)";
   }
   if (!skip.IsEmpty())
   {
    tally.Add(skip);
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
   BumpRoster();
   UpdateRecord(record);

  }
  bool managed;
  foreach (EBG_CacheGroup zoneRecord : Records)
  {
   if (zoneRecord.Zone == zone) { managed = true; break; }
  }
  tally.CountLeftSquad(zoneOrigin, affectedSq);
  zone.EnrollmentNote = tally.Note(zone.Affected, managed);
  zone.EnrollmentPasses++;
  FlushSoldiersOnly(zone);
 }
 // Distinct groups of the agent list in first-seen order, each with its agents and its
 // living soldiers' positions. The tick's pass (shared agents) builds the list once for
 // every zone: no zone's enrollment moves, kills or regroups AI, and every zone is read
 // in the same frame. FindGroup, IsReserved and the skip reasons stay live per zone.
 protected array<ref EBG_EnrollmentCandidate> EnrollmentCandidates(array<AIAgent> agents, bool shared)
 {
  if (shared && m_Candidates && agents == m_CandidateAgents && m_CandidatesTick == m_Ticks)
  {
   return m_Candidates;
  }
  array<ref EBG_EnrollmentCandidate> candidates = {};
  map<SCR_AIGroup, bool> visited = new map<SCR_AIGroup, bool>();
  foreach (AIAgent agent : agents)
  {
   SCR_AIGroup group = SCR_AIGroup.Cast(agent);
   if (!group) group = SCR_AIGroup.Cast(agent.GetParentGroup());
   if (!group) continue;
   if (visited.Contains(group)) continue;
   visited.Insert(group, true);
   EBG_EnrollmentCandidate candidate = new EBG_EnrollmentCandidate();
   candidate.Sample(group);
   candidates.Insert(candidate);
  }
  if (shared)
  {
   m_Candidates = candidates;
   m_CandidateAgents = agents;
   m_CandidatesTick = m_Ticks;
  }
  return candidates;
 }
 // Debug cross-check (EBG_DebugChecks): a shared candidate equals a live read of its group.
 protected void CheckCandidate(EBG_EnrollmentCandidate candidate, vector origin, float radiusSq)
 {
  array<AIAgent> live = {};
  candidate.Group.GetAgents(live);
  bool same = live.Count() == candidate.Members.Count();
  for (int i = 0; same && i < live.Count(); i++) same = live[i] == candidate.Members[i];
  if (same) same = EBG_EnrollmentTally.Nearby(candidate.Group, live, origin, radiusSq) == EBG_EnrollmentTally.Nearby(candidate, origin, radiusSq);
  if (!same) EBG_DebugChecks.Mismatch(string.Format("enrollment candidate group=%1", candidate.Group));
 }
 // Enrollment refusals that depend on a member; empty when every member may enroll.
 // The same per-member checks enrollment always made, each naming itself for the status.
 protected string MemberSkip(array<AIAgent> members, PersistenceSystem persistence)
 {
  foreach (AIAgent memberAgent : members)
  {
   SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(memberAgent.GetControlledEntity());
   if (!character || !character.GetCharacterController()) return "with an unsupported member entity";
   if (character.EBG_WasPlayerControlled()) return "with a member a player controls or once possessed";
   if (character.EBG_HasLeftSquad()) return "still holding a soldier who left his squad";
   if (!EBG_MissionPersistence.MayEnroll(character) || (persistence && EBG_MissionPersistence.HasUnresolvedRelease(persistence.GetId(character)))) return "with an unresolved saved member release";
   if (character.GetCharacterController().IsDead()) continue;
   // A split/merged native group does not transfer logical member ownership.
   // Require release of the existing record before another group can enroll it.
   if (FindMember(character) || Regroup.ReservesMember(character)) return "with a member another cache record still owns";
  }
  return string.Empty;
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
  Records.Insert(record);
  BumpRoster();
  return record;
 }
 EBG_CacheMember AddRegroupMember(EBG_CacheGroup record, SCR_ChimeraCharacter entity)
 {
  EBG_CacheMember member = new EBG_CacheMember(); member.Id = ++m_NextId;
  member.Entity = entity; member.Position = entity.GetOrigin();
  PersistenceSystem system = PersistenceSystem.GetInstance();
  if (system) member.PersistentId = system.GetId(entity);
  record.Members.Insert(member);
  BumpRoster();
  return member;
 }
 EBG_CacheMember AddCachedMember(EBG_CacheGroup record, vector position, bool dead)
 {
  EBG_CacheMember member = new EBG_CacheMember(); member.Id = ++m_NextId;
  member.Position = position; member.Dead = dead; record.Members.Insert(member);
  MarkProtectionDirty();
  return member;
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
  // A death between ticks drops this member from the protection volumes.
  MarkProtectionDirty();
  if (EBG_CacheCleanup.Instance)
   foreach (EBG_CacheGroup record : Records)
    if (record.Members.Contains(member)) EBG_CacheCleanup.Instance.ConfirmDeath(record, member, member.DiedAt);
 }
 protected void UpdatePlayers()
 {
  Players.Clear();
  m_PlayerIds.Clear();
  EBG_CollectPlayers(m_PlayerIds);
  // Any record, member or ledger binding change reruns every player's pass at once.
  if (m_SeenRoster != m_RosterVersion || m_SeenBind != EBG_CacheCleanup.s_EBG_BindVersion || m_PossessionSeen.Count() > m_PlayerIds.Count() + 32)
  {
   m_PossessionSeen.Clear();
   m_SeenRoster = m_RosterVersion;
   m_SeenBind = EBG_CacheCleanup.s_EBG_BindVersion;
  }
  int sweep = -1;
  if (!m_PlayerIds.IsEmpty())
  {
   sweep = m_PossessionCursor % m_PlayerIds.Count();
   m_PossessionCursor++;
  }
  for (int i = 0; i < m_PlayerIds.Count(); i++)
  {
   int id = m_PlayerIds[i];
   SCR_ChimeraCharacter player = SCR_ChimeraCharacter.Cast(EBG_PlayerEntity(id));
   if (!player || !player.GetCharacterController() || player.GetCharacterController().IsDead())
   {
    m_PossessionSeen.Remove(id);
    continue;
   }
   Players.Insert(player.GetOrigin());
   if (i == sweep || m_PossessionSeen.Get(id) != player)
   {
    PlayerPossession(player);
    m_PossessionSeen.Set(id, player);
   }
   else if (EBG_DebugChecks.Enabled) CheckPossession(player);
  }
 }
 // Player seam of UpdatePlayers: the connected players' ids and each one's controlled
 // entity, straight from the player manager. Test fixtures override both to stand spawned
 // characters in for players (the test server has none); production never overrides them.
 protected void EBG_CollectPlayers(notnull array<int> ids)
 {
  GetGame().GetPlayerManager().GetPlayers(ids);
 }
 protected IEntity EBG_PlayerEntity(int id)
 {
  return GetGame().GetPlayerManager().GetPlayerControlledEntity(id);
 }
 // Debug cross-check (EBG_DebugChecks): a player whose pass was skipped needs none. He is
 // flagged, his member (if any) is marked and no record holding that member sleeps.
 protected void CheckPossession(SCR_ChimeraCharacter player)
 {
  EBG_CacheMember member = FindMember(player);
  bool stale = !player.EBG_IsMarkedPlayer() || (member && !member.WasPlayer);
  if (member)
  {
   foreach (EBG_CacheGroup holder : Records)
   {
    if (holder.Members.Contains(member) && holder.Simulation && holder.Simulation.Suspended) stale = true;
   }
  }
  if (stale) EBG_DebugChecks.Mismatch(string.Format("possession player=%1", player));
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
  // Lowest-priority reason; any reason skips sleep and wakes a suspended record.
  string keepAwake = KeepAwakeReason(record.Group);
  if (keepAwake != record.KeepAwake)
  {
   if (keepAwake != "") PrintFormat("[EBG KEEP AWAKE] group=%1 native=%2 anchor=%3 held: %4", record.Id, record.Group, record.Anchor, keepAwake);
   else PrintFormat("[EBG KEEP AWAKE] group=%1 native=%2 anchor=%3 released; normal sleep rules resume", record.Id, record.Group, record.Anchor);
   record.KeepAwake = keepAwake;
  }
  if (keepAwake != "" && record.Reason == "") record.Reason = keepAwake;
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
 void InvalidateProtection()
 {
  m_Protection.Clear();
  m_ProtectionDirty = false;
  m_ProtectionVersion++;
 }
 // Whole-zone activation: any record of the zone inside its volume.
 protected bool ZoneInVolume(EBG_CacheZone zone, bool sleep)
 {
  foreach (EBG_CacheGroup peer : Records)
  {
   if (peer.Zone == zone && InVolume(peer, sleep))
   {
    return true;
   }
  }
  return false;
 }
 // Debug cross-check (EBG_DebugChecks): every verdict a clean pump keeps equals a fresh
 // computation, which is what each pump made before the dirty flag existed.
 protected void CheckProtectionCache()
 {
  if (Players.IsEmpty()) return;
  foreach (EBG_CacheZone sampledZone, EBG_ZoneProtection zoneSample : m_Protection)
  {
   if (!sampledZone || !zoneSample || sampledZone.Strategy != 0 || !zoneSample.Matches(sampledZone)) continue;
   if ((zoneSample.WakeKnown && zoneSample.Wake != ZoneInVolume(sampledZone, false)) || (zoneSample.SleepKnown && zoneSample.Sleep != ZoneInVolume(sampledZone, true)))
    EBG_DebugChecks.Mismatch(string.Format("protection zone=%1", sampledZone.GetID()));
  }
  foreach (EBG_CacheGroup record : Records)
  {
   EBG_ZoneProtection own = record.ProtectionSample;
   EBG_CacheZone owner = record.Zone;
   if (!own || !owner || owner.Strategy == 0 || record.ProtectionVersion != m_ProtectionVersion || !own.Matches(owner)) continue;
   if ((own.WakeKnown && own.Wake != InVolume(record, false)) || (own.SleepKnown && own.Sleep != InVolume(record, true)))
    EBG_DebugChecks.Mismatch(string.Format("protection group=%1", record.Id));
  }
 }
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
  EBG_ZoneProtection sample;
  if (zone.Strategy != 0)
  {
   // Per-group activation: the record's own verdicts, kept until the next invalidation.
   sample = record.ProtectionSample;
   if (!sample || record.ProtectionVersion != m_ProtectionVersion || !sample.Matches(zone))
   {
    sample = new EBG_ZoneProtection(); sample.Capture(zone);
    record.ProtectionSample = sample;
    record.ProtectionVersion = m_ProtectionVersion;
   }
  }
  else
  {
   sample = m_Protection.Get(zone);
   if (!sample || !sample.Matches(zone))
   {
    sample = new EBG_ZoneProtection(); sample.Capture(zone);
    m_Protection.Set(zone, sample);
   }
  }
  if (sleep && sample.SleepKnown) return sample.Sleep;
  if (!sleep && sample.WakeKnown) return sample.Wake;
  bool protectedArea;
  if (zone.Strategy != 0) protectedArea = InVolume(record, sleep);
  else protectedArea = ZoneInVolume(zone, sleep);
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
  // Players and member positions only change in Tick; everything else marks dirty.
  if (EBG_DebugChecks.Enabled && !m_ProtectionDirty) CheckProtectionCache();
  if (m_ProtectionDirty) InvalidateProtection();
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
   zone.PlayerAwakeCount = 0; zone.PlayerAwakeDistance = -1;
  }
  // The pass's shared candidates hold only this frame's reads.
  m_Candidates = null;
  m_CandidateAgents = null;
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
    BumpRoster();
    continue;
   }
   if (!releasedRecord.ReleaseRequested || releasedRecord.Full != null || releasedRecord.Simulation != null) continue;
   if (EBG_CacheCleanup.Instance) EBG_CacheCleanup.Instance.ReleaseGroup(releasedRecord);
   Records.Remove(releasedIndex);
   BumpRoster();
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
     // Status text only: how many groups a player keeps awake, and how close. The
     // full players x members distance scan runs only while a notice is pending or
     // debug is on (its only readers); the replicated status shows no distance.
     zone.PlayerAwakeCount++;
     if (zone.DebugMessages > 0 || EBG_CacheDebug.Level > 0 || zone.EBG_NoticePending())
     {
      float playerDistance = NearestPlayerDistance(record);
      if (playerDistance >= 0 && (zone.PlayerAwakeDistance < 0 || playerDistance < zone.PlayerAwakeDistance)) zone.PlayerAwakeDistance = playerDistance;
     }
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
   string fullUnavailable = ZoneNotes(zone);
   string saveStatus = EBG_FullSaveGate.GetStatus();
   if (saveStatus != "" && saveStatus != fullUnavailable) zone.Status += " | " + saveStatus;
   zone.PublishStatus();
   int noticePlayer = zone.EBG_TakeNotice();
   if (noticePlayer > 0) EBG_ZoneFeedback.Send(noticePlayer, ZoneNoticeText(zone));
  }
 }
 // Appends why an enabled zone enrolls or caches nothing: enrollment refusals,
 // player characters inside the sleep radius and Full cache this session cannot
 // run. Returns the Full refusal (empty when Full is available or not selected).
 protected string ZoneNotes(EBG_CacheZone zone)
 {
  if (!zone.Enabled || zone.Editing || zone.HasPendingSettings() || EBG_OptimizerControl.Preparing) return string.Empty;
  if (!zone.EnrollmentNote.IsEmpty()) zone.Status += " | " + zone.EnrollmentNote;
  if (zone.PlayerAwakeCount > 0) zone.Status += " | " + PlayerAwakeNote(zone, false);
  string fullUnavailable = FullUnavailable(zone);
  if (!fullUnavailable.IsEmpty()) zone.Status += " | Full cache unavailable in this session: " + fullUnavailable;
  string standalone = StandaloneNote();
  if (!standalone.IsEmpty()) zone.Status += " | " + standalone;
  return fullUnavailable;
 }
 // The retired standalone EXPBG mods next to the pack (EBG_StandaloneConflict reads
 // the loaded-addon list once per mission). Status and notice text only.
 static string StandaloneNote()
 {
  if (!GetGame()) return string.Empty;
  string loaded = EBG_StandaloneConflict.Loaded(SCR_BaseGameMode.Cast(GetGame().GetGameMode()));
  if (loaded.IsEmpty()) return string.Empty;
  return "old standalone EXPBG mods are loaded next to GM Tools (" + loaded + "); disable them, their duplicate scripts and prefabs make caching unpredictable";
 }
 // Read once per scheduler tick; the loaded-addon list is only read for Full zones.
 protected float m_FullCheckedAt = -1;
 protected string m_FullRefusal;
 string FullUnavailable(EBG_CacheZone zone)
 {
  if (!zone || zone.Mode != 1) return string.Empty;
  float now = Now();
  if (now != m_FullCheckedAt)
  {
   m_FullCheckedAt = now;
   string reason;
   if (EBG_FullSaveGate.Available(reason)) m_FullRefusal = string.Empty;
   else m_FullRefusal = reason;
  }
  return m_FullRefusal;
 }
 // withDistance: the one-time notice and the debug panel (a per-second sample anyway)
 // name the nearest player character's distance. The replicated zone status leaves
 // it out; a moving player would otherwise change and re-replicate it every few seconds.
 static string PlayerAwakeNote(EBG_CacheZone zone, bool withDistance = true)
 {
  string nearest = "a player character is";
  if (withDistance && zone.PlayerAwakeDistance >= 0)
  {
   // XZ metres, rounded down to 10 m so it never reads as the radius itself.
   float metres = Math.Floor(zone.PlayerAwakeDistance / 10) * 10;
   nearest += string.Format(" %1 m away,", metres);
  }
  return string.Format("%1 awake: %2 inside the %3 m sleep radius (a Game Master's own character counts; the free GM camera does not)", zone.PlayerAwakeCount, nearest, zone.ZoneSleep);
 }
 // XZ metres from the nearest player character to this record's protection
 // centre (anchor, or the module for whole-zone activation) or a living member.
 protected float NearestPlayerDistance(EBG_CacheGroup record)
 {
  if (Players.IsEmpty() || !record.Zone) return -1;
  vector center = record.Anchor;
  if (record.Zone.Strategy == 0) center = record.Zone.GetOrigin();
  float best = float.MAX;
  foreach (vector player : Players)
  {
   best = Math.Min(best, EBG_CacheGeometry.DistanceSq(player, center));
   foreach (EBG_CacheMember member : record.Members)
   {
    if (!member.Dead) best = Math.Min(best, EBG_CacheGeometry.DistanceSq(player, member.Position));
   }
  }
  return Math.Sqrt(best);
 }
 // One plain-language line for the Game Master who saved the zone or used a
 // global switch, from the latest tick. Read only.
 string ZoneNoticeText(EBG_CacheZone zone)
 {
  vector origin = zone.GetOrigin();
  string mode = "Simulation";
  if (zone.Mode == 1) mode = "Full";
  string text = string.Format("Cache zone @ %1, %2 (%3): ", Math.Round(origin[0]), Math.Round(origin[2]), mode);
  if (!zone.Enabled) return text + "disabled; its AI stay awake. Enable the zone to cache them.";
  if (zone.Editing || zone.HasPendingSettings() || EBG_OptimizerControl.Preparing) return text + zone.Status;
  string groups = "groups";
  if (zone.ManagedCount == 1) groups = "group";
  text += string.Format("%1 %2 enrolled, %3 cached.", zone.ManagedCount, groups, zone.CachedCount);
  string fullUnavailable = FullUnavailable(zone);
  if (!fullUnavailable.IsEmpty()) text += " Full cache unavailable in this session: " + fullUnavailable + "; Simulation mode still works.";
  if (!zone.EnrollmentNote.IsEmpty()) text += " " + zone.EnrollmentNote + ".";
  if (zone.PlayerAwakeCount > 0) text += " " + PlayerAwakeNote(zone) + ".";
  else if (zone.ManagedCount > zone.CachedCount && zone.SkippedCount == 0 && fullUnavailable.IsEmpty())
   text += string.Format(" Groups cache after %1 s with no player character within %2 m.", zone.SleepDelay, zone.ZoneSleep);
  if (zone.SkippedCount > 0 && !zone.BlockedReason.IsEmpty()) text += " Held: " + zone.BlockedReason + ".";
  string standalone = StandaloneNote();
  if (!standalone.IsEmpty()) text += " Warning: " + standalone + ".";
  return text;
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

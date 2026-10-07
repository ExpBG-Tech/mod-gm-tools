// Native availability precedes deferred character loading. Delay only callbacks
// already proven to belong to the saved optimizer cohort; never rebuild a roster.
class EBG_MissionGroupCallback
{
 SCR_ChimeraCharacter Member;
 SCR_AIGroup Group;
 UUID MemberId;
 UUID GroupId;
 ref PersistenceDeferredDeserializeTask Task;
 ref Managed Context;
 bool Expired;
 bool Delivered;
 bool RecoveryReported;
}

modded class SCR_AIGroupSerializer
{
 protected static World s_EBG_CallbackWorld;
 protected static ref array<ref EBG_MissionGroupCallback> s_EBG_Callbacks;
#ifdef EBG_ACCEPTANCE_TEST
#ifdef EBG_ACCEPTANCE_MISSION_CONTINUITY
 protected static World s_EBG_EntryWorld;
 protected static int s_EBG_EntryCount;
#endif
#endif
 // Native 1.8.0.13 SCR_AIGroupSerializer.c, Deserialize version 1, AI-only branch.
 // Wire order: version, factionKey, optional commandingGroup, aiMembers,
 // waypoints, dormantAlive, dormantDead. Native waypoint contexts and handlers
 // are retained. Only the AI task delegate changes; no fallback after reading.
 override protected bool Deserialize(notnull IEntity entity, notnull LoadContext context)
 {
  SCR_AIGroup group = SCR_AIGroup.Cast(entity);
  if (!EBG_MissionPersistence.PreserveOriginalGroupDuringSetup(group)) return super.Deserialize(entity, context);
  EBG_MissionPersistenceState state = EBG_MissionPersistenceState.Get();
  EBG_MissionGroupData saved;
  foreach (EBG_MissionGroupData candidate : state.Groups)
   if (candidate.GroupId == GetSystem().GetId(group)) saved = candidate;
  if (!saved) return super.Deserialize(entity, context);
  // DeserializeSpawnData disabled prefab member spawning, as in native code.
  SCR_AIGroup.IgnoreSpawning(false);
  int version;
  if (!context.Read(version) || version != 1) return false;
  FactionKey factionKey;
  if (!context.Read(factionKey)) return false;
  UUID commandingGroup = UUID.NULL_UUID;
  if (SCR_CommandingManagerComponent.GetInstance()) context.ReadDefault(commandingGroup, UUID.NULL_UUID);
  if (!commandingGroup.IsNull()) return false;
  array<UUID> aiMembers();
  if (!context.Read(aiMembers)) return false;
  array<UUID> expected = {};
  foreach (EBG_MissionMemberData original : saved.Members)
   if (!original.Dead && !original.Missing) expected.Insert(original.Id);
  if (expected.Count() != aiMembers.Count()) return false;
  array<UUID> seen = {};
  foreach (UUID memberId : aiMembers)
  {
   if (memberId.IsNull() || !expected.Contains(memberId) || seen.Contains(memberId)) return false;
   seen.Insert(memberId);
  }
  array<UUID> waypoints();
  context.Read(waypoints);
  int dormantAlive = -1, dormantDead;
  context.ReadDefault(dormantAlive, -1);
  context.ReadDefault(dormantDead, 0);
  group.ActivateAI();
  Faction faction = GetGame().GetFactionManager().GetFactionByKey(factionKey);
  if (faction) group.SetFaction(faction);
  foreach (int idx, UUID member : aiMembers)
  {
   Tuple2<SCR_AIGroup, bool> memberContext(group, idx == 0);
   PersistenceWhenAvailableTask memberTask(EBG_OnAiMemberAvailable, memberContext);
   GetSystem().WhenAvailable(member, memberTask);
  }
  array<AIWaypoint> outWaypoints();
  group.GetWaypoints(outWaypoints);
  foreach (AIWaypoint waypoint : outWaypoints) group.RemoveWaypoint(waypoint);
  if (!waypoints.IsEmpty())
  {
   SCR_WaypointLoadContextShared sharedContext();
   sharedContext.m_iPendingResults = waypoints.Count();
   sharedContext.m_aWaypoints = {};
   sharedContext.m_aWaypoints.Resize(sharedContext.m_iPendingResults);
   sharedContext.m_Group = group;
   foreach (int waypointIndex, UUID waypointId : waypoints)
   {
    SCR_WaypointLoadContext waypointContext();
    waypointContext.m_Shared = sharedContext;
    waypointContext.m_iIdx = waypointIndex;
    PersistenceWhenAvailableTask waypointTask(OnWaypointAvailable, waypointContext);
    GetSystem().WhenAvailable(waypointId, waypointTask);
   }
  }
  int confirmedDead, prefabSlots;
  foreach (EBG_MissionMemberData casualty : saved.Members) if (casualty.Dead) confirmedDead++;
  if (group.m_aUnitPrefabSlots) prefabSlots = group.m_aUnitPrefabSlots.Count();
  // Native expansion uses prefab slots minus this counter even for alive=-1.
  // Preserve casualties without lowering capacity below verified saved survivors.
  int deathFloor = Math.Min(confirmedDead, Math.Max(0, prefabSlots - aiMembers.Count()));
  int restoredDead = Math.Max(dormantDead, deathFloor);
  if (dormantAlive >= 0 || restoredDead > 0)
  {
   group.SetDormantCounts(dormantAlive, restoredDead);
   if (group.GetDormantAliveCount() != dormantAlive || group.GetDormantDeadCount() != restoredDead) return false;
  }
  return true;
 }
 protected static void EBG_OnAiMemberAvailable(Managed instance, PersistenceDeferredDeserializeTask task, bool expired, Managed context)
 {
  SCR_ChimeraCharacter member = SCR_ChimeraCharacter.Cast(instance);
  Tuple2<SCR_AIGroup, bool> originalContext = Tuple2<SCR_AIGroup, bool>.Cast(context);
  PersistenceSystem system = PersistenceSystem.GetInstance();
  EBG_MissionPersistenceState state = EBG_MissionPersistenceState.Get();
  bool preserve;
  if (originalContext) preserve = EBG_MissionPersistence.PreserveOriginalGroupDuringSetup(originalContext.param1);
#ifdef EBG_ACCEPTANCE_TEST
#ifdef EBG_ACCEPTANCE_MISSION_CONTINUITY
  if (GetGame())
  {
   if (s_EBG_EntryWorld != GetGame().GetWorld()) { s_EBG_EntryWorld = GetGame().GetWorld(); s_EBG_EntryCount = 0; }
   if (s_EBG_EntryCount < 8)
   {
    s_EBG_EntryCount++;
    int phase = -1, version;
    bool loaded, bound;
    UUID entryMemberId, entryGroupId;
    if (system)
    {
     phase = system.GetState(); loaded = system.WasDataLoaded();
     if (member) entryMemberId = system.GetId(member);
     if (originalContext && originalContext.param1) entryGroupId = system.GetId(originalContext.param1);
    }
    if (state) { version = state.LoadedVersion; bound = state.Bound; }
    PrintFormat("[EBG GROUP CALLBACK ENTRY] authority=%1 state=%2 loaded=%3 member=%4 group=%5 expired=%6 version=%7 bound=%8 preserve=%9", Replication.IsServer(), phase, loaded, entryMemberId, entryGroupId, expired, version, bound, preserve);
   }
  }
#endif
#endif
  EBG_MissionGroupData saved;
  if (!expired && member && originalContext && system && preserve)
  {
   UUID groupId = system.GetId(originalContext.param1);
   UUID memberId = system.GetId(member);
   foreach (EBG_MissionGroupData candidate : state.Groups)
   {
    if (candidate.GroupId != groupId) continue;
    foreach (EBG_MissionMemberData original : candidate.Members)
     if (original.Id == memberId && !original.Dead && !original.Missing && !original.WasPlayer && !member.EBG_WasPlayerControlled()) saved = candidate;
   }
  }
  if (!saved)
  {
   super.OnAiMemberAvailable(instance, task, expired, context);
   return;
  }
  if (s_EBG_CallbackWorld != GetGame().GetWorld() || !s_EBG_Callbacks)
  {
   s_EBG_CallbackWorld = GetGame().GetWorld(); s_EBG_Callbacks = {};
  }
  EBG_MissionGroupCallback pending = new EBG_MissionGroupCallback();
  pending.Member = member; pending.Group = originalContext.param1;
  pending.MemberId = system.GetId(member); pending.GroupId = saved.GroupId;
  pending.Task = task; pending.Context = context; pending.Expired = expired;
  s_EBG_Callbacks.Insert(pending);
#ifdef EBG_ACCEPTANCE_TEST
  PrintFormat("[EBG GROUP CALLBACK] queued group=%1 member=%2 leader=%3", pending.GroupId, pending.MemberId, originalContext.param2);
#endif
 }

 protected static bool EBG_CallbackSafe(EBG_MissionGroupCallback row)
 {
  PersistenceSystem system = PersistenceSystem.GetInstance();
  Tuple2<SCR_AIGroup, bool> originalContext = Tuple2<SCR_AIGroup, bool>.Cast(row.Context);
  if (!system || !row.Member || !row.Group || !originalContext || originalContext.param1 != row.Group || row.Expired) return false;
  if (system.FindById(row.GroupId) != row.Group || system.FindById(row.MemberId) != row.Member || system.GetId(row.Member) != row.MemberId) return false;
  if (row.Group.IsPlayable() || row.Group.GetPlayerCount() > 0 || row.Group.IsSlave() || row.Group.GetMaster() || row.Group.IsCreatedByCommander()) return false;
  if (row.Member.EBG_WasPlayerControlled() || EBG_MissionPlayerHistory.Contains(row.Member) || !row.Member.GetCharacterController() || row.Member.GetCharacterController().IsDead()) return false;
  AIAgent agent = SCR_AIUtils.GetAIAgent(row.Member);
  return agent && agent.GetControlledEntity() == row.Member && (!agent.GetParentGroup() || agent.GetParentGroup() == row.Group);
 }

 protected static bool EBG_PreflightCallbacks(EBG_MissionGroupData saved, array<ref EBG_MissionGroupCallback> rows, out string reason)
 {
  PersistenceSystem system = PersistenceSystem.GetInstance();
  EBG_MissionPersistenceState state = EBG_MissionPersistenceState.Get();
  if (!system || system.GetState() != EPersistenceSystemState.ACTIVE || !state || rows.IsEmpty()) { reason = "Native member callback cohort unavailable"; return false; }
  SCR_AIGroup group = rows[0].Group;
  if (!group || system.FindById(saved.GroupId) != group || group.IsPlayable() || group.GetPlayerCount() > 0 || group.IsSlave() || group.GetMaster() || group.IsCreatedByCommander()) { reason = "Native callback original group identity or policy changed"; return false; }
  array<UUID> ids = {};
  foreach (EBG_MissionGroupCallback row : rows)
  {
   Tuple2<SCR_AIGroup, bool> originalContext = Tuple2<SCR_AIGroup, bool>.Cast(row.Context);
   if (!row.Member || row.Group != group || !originalContext || originalContext.param1 != group || row.Expired || system.FindById(row.MemberId) != row.Member || system.GetId(row.Member) != row.MemberId || ids.Contains(row.MemberId)) { reason = "Native callback identity changed or duplicate callback"; return false; }
   if (row.Member.EBG_WasPlayerControlled() || EBG_MissionPlayerHistory.EverContains(state, row.MemberId) || !row.Member.GetCharacterController() || row.Member.GetCharacterController().IsDead()) { reason = "Native callback member became dead or player-associated"; return false; }
   AIAgent agent = SCR_AIUtils.GetAIAgent(row.Member);
   if (!agent || agent.GetControlledEntity() != row.Member || (agent.GetParentGroup() && agent.GetParentGroup() != group)) { reason = "Native callback member has unavailable agent or foreign group"; return false; }
   ids.Insert(row.MemberId);
  }
  // Late native ACTIVE callbacks may already have supplied part of this cohort.
  array<AIAgent> agents = {}; group.GetAgents(agents);
  foreach (AIAgent existing : agents)
  {
   SCR_ChimeraCharacter character;
   if (existing) character = SCR_ChimeraCharacter.Cast(existing.GetControlledEntity());
   if (!character || existing.GetParentGroup() != group || character.EBG_WasPlayerControlled() || !character.GetCharacterController() || character.GetCharacterController().IsDead()) { reason = "Native callback group contains an unsafe agent"; return false; }
   UUID actualId = system.GetId(character);
   if (!ids.Contains(actualId)) ids.Insert(actualId);
  }
  int expected;
  foreach (EBG_MissionMemberData original : saved.Members)
  {
   if (original.WasPlayer || EBG_MissionPlayerHistory.EverContains(state, original.Id)) { reason = "Native callback original cohort has player history"; return false; }
   if (original.Dead || original.Missing) continue;
   expected++;
   if (!ids.Contains(original.Id)) { reason = "Native member callback cohort incomplete after load"; return false; }
  }
  if (expected != ids.Count()) { reason = "Native member callback cohort contains extra agents or callbacks"; return false; }
  return true;
 }

 // Pending native delegates are session recovery, never serializable metadata.
 // Only explicit Restore ALL retries them; saving must not lose this queue.
 static void EBG_ReleaseCallbacksForWorldCleanup()
 {
  if (!EBG_CacheManager.Unloading) return;
  // Confirmed native world cleanup owns entity destruction. Release contexts and
  // task references here without dispatching any saved gameplay operation.
  if (s_EBG_Callbacks) s_EBG_Callbacks.Clear();
  s_EBG_Callbacks = null; s_EBG_CallbackWorld = null;
 }
 static bool EBG_HasPendingMemberCallbacks()
 {
  if (!GetGame() || !Replication.IsServer() || EBG_CacheManager.Unloading || s_EBG_CallbackWorld != GetGame().GetWorld() || !s_EBG_Callbacks) return false;
  foreach (EBG_MissionGroupCallback row : s_EBG_Callbacks) if (!row.Delivered) return true;
  return false;
 }
 static bool EBG_RestoreMemberCallbacks()
 {
  if (!EBG_HasPendingMemberCallbacks()) return true;
  PersistenceSystem system = PersistenceSystem.GetInstance();
  if (!system || system.GetState() != EPersistenceSystemState.ACTIVE) return false;
  EBG_CompleteMemberCallbacks(GetGame().GetWorld(), true);
  return !EBG_HasPendingMemberCallbacks();
 }
 protected static void EBG_SetCallbackIssue(EBG_MissionGroupData saved, string reason)
 {
  if (saved.Issue != "" && saved.Issue != saved.CallbackIssue) return;
  saved.Issue = reason; saved.CallbackIssue = reason;
 }
 static void EBG_CompleteMemberCallbacks(World world, bool success)
 {
  if (!GetGame() || GetGame().GetWorld() != world || s_EBG_CallbackWorld != world || !s_EBG_Callbacks || EBG_CacheManager.Unloading) return;
  EBG_MissionPersistenceState state = EBG_MissionPersistenceState.Get();
  if (!state || state.LoadedVersion != 2) return; // Retain pending callbacks if metadata is unavailable.
#ifdef EBG_ACCEPTANCE_TEST
#ifdef EBG_ACCEPTANCE_CALLBACK_RECOVERY
  EBG_CallbackRecoveryProof.BeforeCompletion(s_EBG_Callbacks);
#endif
#endif
  foreach (EBG_MissionGroupData saved : state.Groups)
  {
   array<ref EBG_MissionGroupCallback> rows = {};
   foreach (EBG_MissionGroupCallback pending : s_EBG_Callbacks) if (pending.GroupId == saved.GroupId) rows.Insert(pending);
   if (rows.IsEmpty()) continue;
   string reason;
   if (!success) reason = "Native world load failed before member callbacks";
   else if (!EBG_PreflightCallbacks(saved, rows, reason) && reason == "") reason = "Native member callback preflight failed";
   if (reason != "")
   {
    EBG_SetCallbackIssue(saved, reason);
#ifdef EBG_ACCEPTANCE_TEST
    PrintFormat("[EBG GROUP CALLBACK] blocked group=%1 reason=%2", saved.GroupId, reason);
#endif
   }
   // Optimizer refusal must not suppress the original native load callbacks.
   foreach (EBG_MissionGroupCallback row : rows)
   {
    if (row.Delivered) continue;
    if (!EBG_CallbackSafe(row))
    {
     EBG_SetCallbackIssue(saved, "Native member callback retained for recovery: identity, life or ownership changed");
     if (!row.RecoveryReported) PrintFormat("[EBG MISSION LOAD] group=%1 member=%2 native callback retained for recovery; identity, life or ownership changed", row.GroupId, row.MemberId);
     row.RecoveryReported = true;
     continue;
    }
    // Mark before native event dispatch so reentrant completion cannot replay it.
    row.Delivered = true;
#ifdef EBG_ACCEPTANCE_TEST
#ifdef EBG_ACCEPTANCE_CALLBACK_RECOVERY
    EBG_CallbackRecoveryProof.ObserveDelivery(row);
#endif
#endif
    // Execute the original native operation once, including its saved leader flag.
    super.OnAiMemberAvailable(row.Member, row.Task, row.Expired, row.Context);
#ifdef EBG_ACCEPTANCE_TEST
    PrintFormat("[EBG GROUP CALLBACK] executed group=%1 member=%2", row.GroupId, row.MemberId);
#endif
   }
   bool delivered = true;
   foreach (EBG_MissionGroupCallback completed : rows) if (!completed.Delivered) delivered = false;
   string finalReason;
   if (delivered && saved.CallbackIssue != "" && saved.Issue == saved.CallbackIssue && EBG_PreflightCallbacks(saved, rows, finalReason))
   {
    // Release only this callback-owned refusal. Ledger/identity binding must run
    // again; scalar and unrelated recovery errors remain authoritative.
    EBG_CacheManager manager = EBG_CacheManager.Instance;
    bool otherFailure;
    if (manager)
     foreach (EBG_CacheGroup record : manager.Records)
      if (record.PersistentId == saved.GroupId && (record.PersistentScalarFailure || record.PersistentScalarRollbackPending || (record.PersistenceIssue != "" && record.PersistenceIssue != saved.CallbackIssue))) otherFailure = true;
    if (!otherFailure)
    {
     if (manager)
      foreach (EBG_CacheGroup matching : manager.Records)
       if (matching.PersistentId == saved.GroupId && matching.PersistenceIssue == saved.CallbackIssue) matching.PersistenceIssue = "";
     saved.Issue = ""; saved.CallbackIssue = ""; saved.Imported = false;
     state.Bound = false; state.BindingStarted = false;
    }
   }
  }
  // Unsafe callbacks stay explicit pending recovery until same-world revalidation;
  // no timer retries, forced transfer, or silent loss on optimizer refusal.
  for (int i = s_EBG_Callbacks.Count() - 1; i >= 0; i--)
   if (s_EBG_Callbacks[i].Delivered) s_EBG_Callbacks.Remove(i);
#ifdef EBG_ACCEPTANCE_TEST
#ifdef EBG_ACCEPTANCE_CALLBACK_RECOVERY
  EBG_CallbackRecoveryProof.AfterCompletion();
#endif
#endif
 }
}

modded class SCR_PersistenceSystem
{
 protected World m_EBG_CallbackLoadWorld;
 protected void EBG_FinishNativeCallbacks(bool success)
 {
  SCR_AIGroupSerializer.EBG_CompleteMemberCallbacks(m_EBG_CallbackLoadWorld, success);
 }
 override protected void OnAfterLoad(bool success)
 {
  super.OnAfterLoad(success);
  if (GetGame() && Replication.IsServer())
  {
   m_EBG_CallbackLoadWorld = GetGame().GetWorld();
   GetGame().GetCallqueue().Call(EBG_FinishNativeCallbacks, success);
  }
 }
}

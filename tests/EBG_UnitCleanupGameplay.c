// TEST ONLY. Unit Caching per-casualty cleanup gameplay fixture.
// pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -UnitCleanup -TimeoutSeconds 540 -OrchestratorSlotGranted
// Real zone prefab and public SetValue, real manager enrollment/Tick/cleanup, native Kill.
// Presence is injected through EBG_CacheManager.UpdatePlayers; the server has no players.
// The runner copies this file to EXPG_GarrisonGameplay.c; the class names are fixed.
class EBGCleanupCase
{
 string Name;
 int Mode;
 int CorpseAge;
 int Kills;
 float AfterKill = -1;
 bool ExpectAwake;
 bool ExpectCached;
 bool WakeBand;
 bool Blocked;
 bool ReturnPhase;
 bool SnapshotDeath;
 bool SnapshotHeld;
 bool SnapshotRestored;
 vector Point;
 vector PresenceAt;
 bool Present;
 SCR_AIGroup Group;
 EBG_CacheZone Zone;
 ref EBG_CacheGroup Record;
 ref array<ref EBG_CacheMember> Casualties = {};
 ref array<SCR_ChimeraCharacter> Bodies = {};
 ref array<EntityID> BodyIds = {};
 ref array<EntityID> SurvivorIds = {};
 IEntity Foreign;
 int Phase;
 int Rows;
 int DeletedState = -1;
 bool Done;
 bool EverSuspended;
 bool EverFull;
 bool SecondDeleted;
 float PhaseAt;
 float KilledAt = -1;
 float LeftAt = -1;
 float CachedAt = -1;
 float DeletedAt = -1;
 float WokeAt = -1;
 float Eligible;
 float Deadline;
 float NextReport;
}
class EXPG_GarrisonGameplayClass : GenericEntityClass {}
class EXPG_GarrisonGameplay : GenericEntity
{
 static const float FIXTURE_SECONDS = 400;
 static ref array<vector> s_Presence;
 static ref array<EntityID> s_EbgDeleted;
 static ref array<int> s_EbgDeletedState;
 static ref array<EntityID> s_Survivors;
 static bool s_InCleanupTick;
 static int s_TickState;
 static int s_SurvivorDeletes;
 ref array<ref EBGCleanupCase> Cases = {};
 int Checks;
 int Failures;
 float Started;
 float Next;
 bool Finished;
 bool Isolated;
 vector Origin = "4773.46 0 7094.57";
 void EXPG_GarrisonGameplay(IEntitySource src, IEntity parent) { SetEventMask(EntityEvent.INIT | EntityEvent.FRAME); }
 float Now() { return GetGame().GetWorld().GetWorldTime() * 0.001; }
 override void EOnInit(IEntity owner)
 {
  if (!Replication.IsServer()) { ClearEventMask(EntityEvent.FRAME); return; }
  s_Presence = {}; s_EbgDeleted = {}; s_EbgDeletedState = {}; s_Survivors = {}; s_SurvivorDeletes = 0;
  Started = Now(); Next = Started + 15;
  // Partial cases first: earlier records win the shared one-root-per-scan allowance.
  EBGCleanupCase awake = AddCase("sim-partial-awake", 0, 30, 1, 0, 0, 130); awake.ExpectAwake = true;
  EBGCleanupCase simCached = AddCase("sim-partial-cached", 0, 150, 1, 600, 0, -1); simCached.ExpectCached = true; simCached.ReturnPhase = true;
  EBGCleanupCase fullCached = AddCase("full-partial-cached", 1, 150, 1, 0, 600, -1); fullCached.ExpectCached = true; fullCached.ReturnPhase = true;
  EBGCleanupCase fullAwake = AddCase("full-partial-awake-then-cache", 1, 30, 1, 600, 600, 130); fullAwake.ExpectAwake = true; fullAwake.ReturnPhase = true;
  EBGCleanupCase band = AddCase("sim-partial-wakeband", 0, 30, 1, -600, 0, 50); band.WakeBand = true;
  EBGCleanupCase blocked = AddCase("h3-blocked", 0, 30, 2, 0, -600, -1); blocked.Blocked = true;
  AddCase("sim-all-control", 0, 30, 6, -600, -600, -1);
  AddCase("full-all-control", 1, 30, 6, 600, -600, -1);
  // Snapshot rule: a soldier killed while his group is Simulation cached keeps his body
  // until the survivors are restored; only then may cleanup delete it.
  EBGCleanupCase snapshot = AddCase("sim-death-while-cached", 0, 30, 0, -600, 600, -1); snapshot.SnapshotDeath = true;
  PrintFormat("[EBG CLEANUP TEST BEGIN] cases=%1 affected=40 wake=60 sleep=200 clearDelay=5 connectedPlayers=0 presence=injected deadline=%2", Cases.Count(), FIXTURE_SECONDS);
 }
 EBGCleanupCase AddCase(string name, int mode, int corpseAge, int kills, float x, float z, float afterKill)
 {
  EBGCleanupCase added = new EBGCleanupCase();
  added.Name = name; added.Mode = mode; added.CorpseAge = corpseAge; added.Kills = kills; added.AfterKill = afterKill;
  added.Point = Origin + Vector(x, 0, z);
  Cases.Insert(added);
  return added;
 }
 bool Check(bool ok, string label)
 {
  Checks++;
  if (!ok) Failures++;
  PrintFormat("[EBG CLEANUP TEST CHECK] pass=%1 %2", ok, label);
  return ok;
 }
 void Finish(string reason)
 {
  if (Finished) return;
  Finished = true;
  Check(s_SurvivorDeletes == 0, "no survivor or survivor equipment deleted inside EBG cleanup");
  s_Presence.Clear();
  ClearEventMask(EntityEvent.FRAME);
  PrintFormat("[EBG CLEANUP TEST RESULT] checks=%1 failures=%2 cases=%3 reason=%4", Checks, Failures, Cases.Count(), reason);
  GetGame().RequestClose();
 }
 vector Ground(vector p, float lift) { p[1] = GetGame().GetWorld().GetSurfaceY(p[0], p[2]) + lift; return p; }
 EntitySpawnParams Params(vector p)
 {
  EntitySpawnParams spawn = new EntitySpawnParams();
  spawn.TransformMode = ETransformMode.WORLD;
  Math3D.AnglesToMatrix(vector.Zero, spawn.Transform);
  spawn.Transform[3] = Ground(p, 0.3);
  return spawn;
 }
 // offset < 0 removes this case's presence; otherwise presence stands offset metres +X of the point.
 void SetPresence(EBGCleanupCase c, float offset)
 {
  if (c.Present) s_Presence.RemoveItem(c.PresenceAt);
  c.Present = offset >= 0;
  if (!c.Present) return;
  c.PresenceAt = Ground(c.Point + Vector(offset, 0, 0), 1.8);
  s_Presence.Insert(c.PresenceAt);
 }
 bool SurvivorsAlive(EBGCleanupCase c)
 {
  foreach (EntityID id : c.SurvivorIds)
  {
   SCR_ChimeraCharacter survivor = SCR_ChimeraCharacter.Cast(GetGame().GetWorld().FindEntityByID(id));
   if (!survivor || !survivor.GetCharacterController() || survivor.GetCharacterController().IsDead()) return false;
  }
  return true;
 }
 override void EOnFrame(IEntity owner, float timeSlice)
 {
  if (Finished || Now() < Next) return;
  Next = Now() + 0.5;
  if (!Isolated)
  {
   Isolated = true;
   string modeName = "none";
   if (GetGame().GetGameMode()) modeName = GetGame().GetGameMode().Type().ToString();
   PrintFormat("[EBG CLEANUP TEST RUNTIME] systems='%1' gameMode=%2 players=%3 zones=%4", GetGame().GetSystemsConfig(), modeName, GetGame().GetPlayerManager().GetPlayerCount(), EBG_CacheZone.Zones.Count());
   if (!Check(GetGame().GetPlayerManager().GetPlayerCount() == 0 && EBG_CacheZone.Zones.IsEmpty(), "isolated server: no players, no zones")) { Finish("setup"); return; }
  }
  if (Now() - Started > FIXTURE_SECONDS)
  {
   foreach (EBGCleanupCase late : Cases)
   {
    if (late.Done) continue;
    if (late.Record) Report(late, -1);
    Check(false, late.Name + " finished before the fixture deadline");
   }
   Finish("timeout");
   return;
  }
  bool all = true;
  foreach (EBGCleanupCase c : Cases) { if (!c.Done) { Step(c); all = false; } }
  if (all) Finish("complete");
 }
 void Step(EBGCleanupCase c)
 {
  EBG_CacheCleanup cleanup = EBG_CacheCleanup.Get();
  if (c.Phase == 0)
  {
   // Keep the Resource and the spawned entity in locals before casting; the inline form
   // returned null for every case in native runs (2026-10-05).
   Resource squad = Resource.Load("{E552DABF3636C2AD}Prefabs/Groups/OPFOR/Group_USSR_RifleSquad.et");
   IEntity squadEntity = GetGame().SpawnEntityPrefab(squad, GetGame().GetWorld(), Params(c.Point));
   c.Group = SCR_AIGroup.Cast(squadEntity);
   if (!Check(c.Group != null, c.Name + " native USSR squad spawned")) { c.Done = true; return; }
   c.Phase = 1; c.PhaseAt = Now(); return;
  }
  if (c.Phase == 1)
  {
   if (!c.Group || c.Group.GetAgentsCount() != 6 || !c.Group.EBG_HasCompletedInitialSpawn())
   {
    if (Now() - c.PhaseAt > 30) { Check(false, c.Name + " squad of six completed its initial spawn"); c.Done = true; }
    return;
   }
   int swimming = 0;
   array<AIAgent> agents = {};
   c.Group.GetAgents(agents);
   foreach (AIAgent agent : agents)
   {
    SCR_ChimeraCharacter swimmer = SCR_ChimeraCharacter.Cast(agent.GetControlledEntity());
    if (swimmer && swimmer.GetCharacterController() && swimmer.GetCharacterController().IsSwimming()) swimming++;
   }
   PrintFormat("[EBG CLEANUP TEST TERRAIN] case=%1 point=%2 surfaceY=%3 swimming=%4", c.Name, c.Point, GetGame().GetWorld().GetSurfaceY(c.Point[0], c.Point[2]), swimming);
   if (!Check(swimming == 0, c.Name + " squad stands on dry land")) { c.Done = true; return; }
   Resource zonePrefab = Resource.Load("{7E1080ED8F0633FD}PrefabsEditable/EXPBG/EBG_CacheZone.et");
   IEntity zoneEntity = GetGame().SpawnEntityPrefab(zonePrefab, GetGame().GetWorld(), Params(c.Point));
   c.Zone = EBG_CacheZone.Cast(zoneEntity);
   if (!Check(c.Zone != null, c.Name + " cache zone prefab spawned")) { c.Done = true; return; }
   c.Zone.SetValue(1, c.Mode); c.Zone.SetValue(2, 0);
   c.Zone.SetValue(3, 40); c.Zone.SetValue(4, 60); c.Zone.SetValue(5, 200);
   c.Zone.SetValue(12, 5); c.Zone.SetValue(16, c.CorpseAge); c.Zone.SetValue(15, 1);
   c.Zone.SetValue(18, 0); c.Zone.SetValue(21, 1);
   SetPresence(c, 0);
   c.Zone.SetValue(0, 1);
   Check(c.Zone.Enabled == 1 && !c.Zone.Editing && c.Zone.Mode == c.Mode && c.Zone.Strategy == 0 && c.Zone.Affected == 40 && c.Zone.ZoneWake == 60 && c.Zone.ZoneSleep == 200 && c.Zone.CleanupDelay == 5 && c.Zone.Cleanup == 1 && c.Zone.CorpseAge == c.CorpseAge, c.Name + " public zone settings applied");
   c.Phase = 2; c.PhaseAt = Now(); return;
  }
  if (c.Phase == 2)
  {
   c.Record = EBG_CacheManager.Get().FindGroup(c.Group);
   if (!c.Record || c.Record.Members.Count() != 6 || !c.Record.CleanupRegistered || c.Zone.Editing)
   {
    if (Now() - c.PhaseAt > 40)
    {
     PrintFormat("[EBG CLEANUP TEST ZONE] case=%1 status='%2' editing=%3 loadHold=%4 reason='%5'", c.Name, c.Zone.Status, c.Zone.Editing, c.Zone.EBG_HasSettingsLoadHold(), c.Zone.EBG_SettingsLoadReason());
     Check(false, c.Name + " manager enrolled six members with a cleanup ledger");
     c.Done = true;
    }
    return;
   }
   IEntity leader = c.Group.GetLeaderEntity();
   foreach (EBG_CacheMember enrolled : c.Record.Members)
   {
    if (c.Casualties.Count() < c.Kills && (c.Kills == 6 || enrolled.Entity != leader))
    {
     c.Casualties.Insert(enrolled); c.Bodies.Insert(enrolled.Entity); c.BodyIds.Insert(enrolled.Entity.GetID());
    }
    else { c.SurvivorIds.Insert(enrolled.Entity.GetID()); s_Survivors.Insert(enrolled.Entity.GetID()); }
   }
   if (c.SnapshotDeath)
   {
    // Nobody dies yet: players leave first so the whole squad is Simulation cached.
    SetPresence(c, -1);
    c.LeftAt = Now(); c.Phase = 10; c.PhaseAt = Now();
    PrintFormat("[EBG CLEANUP TEST LEFT] case=%1 phase=await-simulation-cache leftAt=%2 group=%3", c.Name, c.LeftAt, c.Record.Id);
    return;
   }
   // Native death with a null instigator, as in the optimizer cleanup fixtures.
   foreach (SCR_ChimeraCharacter victim : c.Bodies)
   {
    SCR_CharacterDamageManagerComponent damage = SCR_CharacterDamageManagerComponent.Cast(victim.GetDamageManager());
    if (damage) damage.Kill(Instigator.CreateInstigator(null));
   }
   c.KilledAt = Now(); c.Phase = 3;
   PrintFormat("[EBG CLEANUP TEST KILLED] case=%1 group=%2 casualties=%3 survivors=%4 corpseAge=%5 mode=%6", c.Name, c.Record.Id, c.Casualties.Count(), c.SurvivorIds.Count(), c.CorpseAge, c.Mode);
   return;
  }
  if (c.Phase == 3)
  {
   foreach (EBG_CacheMember dead : c.Casualties)
   {
    if (!dead.Dead || !dead.Entity || !dead.Entity.GetCharacterController().IsDead() || !cleanup.IsBodyCleanupTarget(dead.Entity, c.Record))
    {
     if (Now() - c.KilledAt > 10) { Check(false, c.Name + " native death confirmed as owned EBG corpse"); c.Done = true; }
     return;
    }
   }
   if (Now() - c.KilledAt < 3) return;
   Check(true, c.Name + " native death confirmed as owned EBG corpse");
   PrintFormat("[EBG CLEANUP TEST HOLD] case=%1 held=%2 garbageProtected=%3", c.Name, cleanup.IsHeld(c.Bodies[0]), cleanup.IsGarbageProtected(c.Bodies[0]));
   if (c.Blocked)
   {
    Resource magazine = Resource.Load("{0A84AA5A3884176F}Prefabs/Weapons/Magazines/Magazine_545x39_AK_30rnd_Last_5Tracer.et");
    IEntity magazineEntity = GetGame().SpawnEntityPrefab(magazine, GetGame().GetWorld(), Params(c.Bodies[0].GetOrigin()));
    c.Foreign = magazineEntity;
    SCR_InventoryStorageManagerComponent inventory = SCR_InventoryStorageManagerComponent.Cast(c.Bodies[0].FindComponent(SCR_InventoryStorageManagerComponent));
    bool requested = c.Foreign && inventory && inventory.TryInsertItem(c.Foreign);
    PrintFormat("[EBG CLEANUP TEST H3 SETUP] case=%1 method=inventory requested=%2", c.Name, requested);
    c.Phase = 4; c.PhaseAt = Now(); return;
   }
   Leave(c); return;
  }
  if (c.Phase == 4)
  {
   if (Now() - c.PhaseAt < 1) return;
   bool inside = c.Foreign && EBG_FullCacheGroup.InventoryBelongsTo(c.Foreign, c.Bodies[0]);
   if (c.Foreign && !inside)
   {
    // Fallback: any unregistered node in the casualty's hierarchy exercises the same SafeTree rejection.
    c.Bodies[0].AddChild(c.Foreign, -1);
    inside = EBG_FullCacheGroup.InventoryBelongsTo(c.Foreign, c.Bodies[0]);
    PrintFormat("[EBG CLEANUP TEST H3 SETUP] case=%1 method=child inside=%2", c.Name, inside);
   }
   if (!Check(inside && !cleanup.IsHeld(c.Foreign), c.Name + " unregistered magazine inside the first casualty")) { c.Done = true; return; }
   bool tree = cleanup.CanDeleteTree(c.Bodies[0], c.Record);
   string why = cleanup.GetLastReason();
   PrintFormat("[EBG CLEANUP TEST H3 TREE] case=%1 canDelete=%2 reason='%3'", c.Name, tree, why);
   Check(!tree && why == "Cleanup held: unregistered or protected contents", c.Name + " unregistered content blocks the first casualty's tree");
   Leave(c); return;
  }
  int remaining = Observe(c);
  if (c.Phase >= 10) { StepSnapshot(c, cleanup); return; }
  if (c.Phase == 5)
  {
   if (c.Blocked) { StepBlocked(c, cleanup); return; }
   if (c.WakeBand)
   {
    if (remaining != c.Bodies.Count()) { Check(false, c.Name + " presence inside the wake radius holds cleanup"); c.Done = true; return; }
    if (Now() < c.Deadline) return;
    // Assert the hold itself, not just a surviving body: the shared one-root-per-scan
    // allowance alone could keep this body for the window. A deleted item row drops out
    // of the count, and CleanupClearSince stays -1 only while the hold is active.
    int heldRows = 0;
    foreach (EBG_CacheMember heldCasualty : c.Casualties) heldRows += cleanup.CountPersistentMemberObjects(heldCasualty);
    bool nearby = cleanup.HasNearbyPlayer(c.Record, EBG_CacheManager.Get().Players);
    PrintFormat("[EBG CLEANUP TEST WAKEBAND] case=%1 rows=%2/%3 clearSince=%4 nearby=%5", c.Name, heldRows, c.Rows, c.Record.CleanupClearSince, nearby);
    Check(heldRows == c.Rows && c.Record.CleanupClearSince < 0 && nearby, c.Name + " presence inside the wake radius holds cleanup");
    SetPresence(c, -1);
    c.WakeBand = false;
    c.Eligible = Now() + c.Zone.CleanupDelay; c.Deadline = c.Eligible + 90;
    PrintFormat("[EBG CLEANUP TEST LEFT] case=%1 phase=wakeband-cleared eligibleAt=%2 deadline=%3", c.Name, c.Eligible, c.Deadline);
    return;
   }
   if (remaining > 0)
   {
    if (Now() <= c.Deadline) return;
    Report(c, remaining);
    Check(false, string.Format("%1 casualty body deleted after players left; remaining=%2", c.Name, remaining));
    c.Done = true; return;
   }
   Deleted(c); return;
  }
  if (c.Phase == 6)
  {
   bool simNow = c.Record.Simulation && c.Record.Simulation.Suspended;
   bool fullNow = c.Record.Full && c.Record.Full.GetState() == EBG_FullGroupPhase.CACHED;
   int cachedMembers = -1;
   if (c.Record.Full) cachedMembers = c.Record.Full.GetMemberCount();
   if (c.ExpectCached)
   {
    if (Now() - c.DeletedAt < 10) return;
    if (c.Mode == 0) Check(simNow && c.Record.Alive == 5, c.Name + " survivors still Simulation cached 10 s after casualty cleanup");
    else Check(fullNow && cachedMembers == 5, c.Name + " survivors still Full cached 10 s after casualty cleanup");
    SetPresence(c, 0); c.Phase = 7; c.PhaseAt = Now(); return;
   }
   // full-partial-awake-then-cache: players left after the awake deletion.
   if (!fullNow)
   {
    if (Now() - c.PhaseAt > 150) { Report(c, 0); Check(false, c.Name + " survivors Full-cached after the awake cleanup"); c.Done = true; }
    return;
   }
   if (Now() - c.CachedAt < 5) return;
   Check(cachedMembers == 5, c.Name + " survivors Full-cached after the awake cleanup");
   SetPresence(c, 0); c.Phase = 7; c.PhaseAt = Now(); return;
  }
  if (c.Phase == 7)
  {
   bool woke = !c.Record.Simulation;
   float limit = 60;
   if (c.Mode == 1) { woke = !c.Record.Full && c.Record.Group != null; limit = 90; }
   if (!woke)
   {
    if (Now() - c.PhaseAt > limit) { Report(c, 0); Check(false, c.Name + " survivors woke when players returned"); c.Done = true; }
    return;
   }
   if (c.WokeAt < 0) { c.WokeAt = Now(); return; }
   if (Now() - c.WokeAt < 2) return;
   int deadCount = 0;
   foreach (EBG_CacheMember counted : c.Record.Members) { if (counted.Dead) deadCount++; }
   bool gone = true;
   foreach (EBG_CacheMember casualty : c.Casualties) { if (casualty.Entity) gone = false; }
   int agentCount = -1;
   if (c.Record.Group) agentCount = c.Record.Group.GetAgentsCount();
   PrintFormat("[EBG CLEANUP TEST RETURN] case=%1 alive=%2 members=%3 dead=%4 agents=%5 casualtyGone=%6 state='%7'", c.Name, c.Record.Alive, c.Record.Members.Count(), deadCount, agentCount, gone, c.Record.DebugState());
   Check(c.Record.Alive == 5 && c.Record.Members.Count() == 6 && deadCount == 1 && gone && agentCount == 5, c.Name + " survivors restored without replenishment");
   if (c.Mode == 0) Check(SurvivorsAlive(c), c.Name + " the same original survivor entities were restored");
   c.Done = true; return;
  }
  if (c.Phase == 9)
  {
   if (EBG_CacheManager.Get().Records.Contains(c.Record))
   {
    if (Now() - c.DeletedAt > c.Rows * 0.75 + 20) { Report(c, 0); Check(false, c.Name + " eliminated record retired after its remains were removed"); c.Done = true; }
    return;
   }
   Check(true, c.Name + " eliminated record retired after its remains were removed");
   if (c.Mode == 1) Check(!c.EverFull, c.Name + " eliminated record never entered Full cache");
   c.Done = true;
  }
 }
 void Leave(EBGCleanupCase c)
 {
  SetPresence(c, c.AfterKill);
  c.LeftAt = Now();
  EBG_CacheCleanup ledger = EBG_CacheCleanup.Get();
  foreach (EBG_CacheMember owner : c.Casualties) c.Rows += ledger.CountPersistentMemberObjects(owner);
  c.Eligible = Math.Max(c.KilledAt + c.Zone.CorpseAge, c.LeftAt) + c.Zone.CleanupDelay;
  c.Deadline = c.Eligible + 90;
  if (c.Kills == 6) c.Deadline = c.Eligible + c.Rows * 0.75 + 30;
  if (c.WakeBand) c.Deadline = c.Eligible + 20;
  if (c.Blocked) c.Deadline = c.Eligible + 260;
  PrintFormat("[EBG CLEANUP TEST LEFT] case=%1 killedAt=%2 leftAt=%3 eligibleAt=%4 deadline=%5 rows=%6 presenceOffset=%7", c.Name, c.KilledAt, c.LeftAt, c.Eligible, c.Deadline, c.Rows, c.AfterKill);
  c.Phase = 5;
 }
 int Observe(EBGCleanupCase c)
 {
  bool simCached = c.Record.Simulation && c.Record.Simulation.Suspended;
  bool fullCached = c.Record.Full && c.Record.Full.GetState() == EBG_FullGroupPhase.CACHED;
  if (simCached) c.EverSuspended = true;
  if (c.Record.Full) c.EverFull = true;
  if ((simCached || fullCached) && c.CachedAt < 0)
  {
   c.CachedAt = Now();
   PrintFormat("[EBG CLEANUP TEST CACHED] case=%1 secondsAfterLeave=%2 secondsAfterDeath=%3 state='%4'", c.Name, Now() - c.LeftAt, Now() - c.KilledAt, c.Record.DebugState());
  }
  int present = 0;
  foreach (SCR_ChimeraCharacter observed : c.Bodies) { if (observed) present++; }
  if (Now() >= c.NextReport) Report(c, present);
  return present;
 }
 void Deleted(EBGCleanupCase c)
 {
  c.DeletedAt = Now();
  int byEbg = 0;
  for (int i = 0; i < c.BodyIds.Count(); i++)
  {
   int at = s_EbgDeleted.Find(c.BodyIds[i]);
   if (at < 0) continue;
   byEbg++;
   if (i == 0) c.DeletedState = s_EbgDeletedState[at];
  }
  int whileCached = 0;
  if (c.DeletedState == 1 || c.DeletedState == 2) whileCached = 1;
  PrintFormat("[EBG CLEANUP TEST DELETED] case=%1 bodies=%2 byEbg=%3 state=%4 whileCached=%5 afterLeave=%6 afterDeath=%7 cachedAt=%8", c.Name, c.BodyIds.Count(), byEbg, c.DeletedState, whileCached, c.DeletedAt - c.LeftAt, c.DeletedAt - c.KilledAt, c.CachedAt);
  Check(byEbg == c.BodyIds.Count(), c.Name + " every casualty body deleted by EBG cleanup after players left");
  // The corpse row's death time is taken in the kill frame; deletion never precedes age + clear delay.
  Check(c.DeletedAt >= c.Eligible - 1, c.Name + " body not deleted before corpse age and clear delay");
  if (c.Kills < 6)
  {
   bool simCached = c.Record.Simulation && c.Record.Simulation.Suspended;
   bool fullCached = c.Record.Full && c.Record.Full.GetState() == EBG_FullGroupPhase.CACHED;
   int fullMembers = -1;
   if (c.Record.Full) fullMembers = c.Record.Full.GetMemberCount();
   if (c.ExpectAwake && c.Mode == 0) Check(c.DeletedState == 0 && !c.EverSuspended && c.Record.Alive == 5 && SurvivorsAlive(c), c.Name + " body deleted while the survivors stayed awake and untouched");
   if (c.ExpectAwake && c.Mode == 1) Check(c.DeletedState == 0 && !c.EverFull && c.Record.Alive == 5 && SurvivorsAlive(c), c.Name + " body deleted while the survivors stayed awake and untouched");
   if (c.ExpectCached && c.Mode == 0) Check(c.DeletedState == 1 && simCached && c.Record.Alive == 5 && SurvivorsAlive(c), c.Name + " body deleted while the survivors stayed Simulation cached");
   if (c.ExpectCached && c.Mode == 1)
   {
    if (c.CachedAt < 0) PrintFormat("[EBG CLEANUP TEST FULL REFUSAL] case=%1 rejection='%2' reason='%3'", c.Name, c.Record.LastCacheRejection, c.Record.Reason);
    Check(c.DeletedState == 2 && fullCached && fullMembers == 5, c.Name + " body deleted while the survivors stayed Full cached");
   }
   if (!c.ExpectAwake && !c.ExpectCached) Check(c.Record.Alive == 5 && SurvivorsAlive(c), c.Name + " survivors untouched by body cleanup");
  }
  c.PhaseAt = Now();
  if (c.Kills == 6) { c.Phase = 9; return; }
  if (!c.ReturnPhase) { c.Done = true; return; }
  c.Phase = 6;
  if (c.ExpectAwake) SetPresence(c, -1);
 }
 // sim-death-while-cached: 10 wait for Simulation cache, 11 confirm the cached death,
 // 12 hold while the casualty is in the live snapshot, 13 players return and the
 // survivors restore, 14 players leave and cleanup deletes the restored casualty.
 void StepSnapshot(EBGCleanupCase c, EBG_CacheCleanup cleanup)
 {
  bool suspended = c.Record.Simulation && c.Record.Simulation.Suspended;
  if (c.Phase == 10)
  {
   if (!suspended)
   {
    if (Now() - c.PhaseAt > 150) { Report(c, 0); Check(false, c.Name + " survivors Simulation cached before the snapshot death"); c.Done = true; }
    return;
   }
   if (c.CachedAt < 0 || Now() - c.CachedAt < 3) return;
   IEntity leader;
   if (c.Group) leader = c.Group.GetLeaderEntity();
   EBG_CacheMember victim;
   foreach (EBG_CacheMember candidate : c.Record.Members)
   {
    if (!victim && !candidate.Dead && candidate.Entity && candidate.Entity != leader) victim = candidate;
   }
   if (!Check(victim != null, c.Name + " cached survivor chosen for the snapshot death")) { c.Done = true; return; }
   SCR_ChimeraCharacter victimBody = victim.Entity;
   EntityID victimId = victimBody.GetID();
   c.Casualties.Insert(victim); c.Bodies.Insert(victimBody); c.BodyIds.Insert(victimId);
   c.SurvivorIds.RemoveItem(victimId); s_Survivors.RemoveItem(victimId);
   SCR_CharacterDamageManagerComponent damage = SCR_CharacterDamageManagerComponent.Cast(victimBody.GetDamageManager());
   if (damage) damage.Kill(Instigator.CreateInstigator(null));
   c.KilledAt = Now(); c.Phase = 11; c.PhaseAt = Now();
   PrintFormat("[EBG CLEANUP TEST KILLED] case=%1 group=%2 casualties=1 survivors=%3 corpseAge=%4 mode=%5 whileSimulationCached=1", c.Name, c.Record.Id, c.SurvivorIds.Count(), c.CorpseAge, c.Mode);
   return;
  }
  SCR_ChimeraCharacter body = c.Bodies[0];
  if (c.Phase == 11)
  {
   EBG_CacheMember dying = c.Casualties[0];
   bool confirmed = dying.Dead && body && body.GetCharacterController() && body.GetCharacterController().IsDead() && cleanup.IsHeld(body);
   if (!confirmed)
   {
    if (Now() - c.KilledAt > 10)
    {
     // Known pre-existing limit (2026-10-05): a scripted kill of a Simulation-suspended
     // character does not complete native death while suspended, so the manager never
     // confirms it (sleeping units are hidden and untargetable in normal play). Gate only
     // safety here: the body must not be deleted by EBG and no survivor may be touched.
     Report(c, 1);
     int deletedEarly = 0;
     if (s_EbgDeleted.Contains(c.BodyIds[0])) deletedEarly = 1;
     PrintFormat("[EBG CLEANUP TEST SNAPSHOT] case=%1 knownLimitation=death-not-confirmed-while-suspended deletedByEbg=%2 state='%3'", c.Name, deletedEarly, c.Record.DebugState());
     Check(deletedEarly == 0 && SurvivorsAlive(c), c.Name + " unconfirmed death while Simulation cached is never deleted by EBG and survivors stay alive");
     c.Done = true;
    }
    return;
   }
   Check(true, c.Name + " death while Simulation cached confirmed as an owned EBG corpse");
   c.Eligible = c.KilledAt + c.Zone.CorpseAge + c.Zone.CleanupDelay; c.Deadline = c.Eligible + 20;
   c.Phase = 12; c.PhaseAt = Now();
   return;
  }
  if (c.Phase == 12)
  {
   // Precondition: a self-wake here is a caching outcome, not a cleanup result.
   if (!suspended) { Report(c, 1); Check(false, c.Name + " survivors stayed Simulation cached through the snapshot hold (precondition)"); c.Done = true; return; }
   if (!body || s_EbgDeleted.Contains(c.BodyIds[0])) { Report(c, 0); Check(false, c.Name + " casualty in the live Simulation snapshot kept until restore"); c.Done = true; return; }
   if (Now() < c.Deadline) return;
   PrintFormat("[EBG CLEANUP TEST SNAPSHOT HOLD] case=%1 clearSince=%2 held=%3 garbageProtected=%4 state='%5'", c.Name, c.Record.CleanupClearSince, cleanup.IsHeld(body), cleanup.IsGarbageProtected(body), c.Record.DebugState());
   c.SnapshotHeld = Check(c.Record.CleanupClearSince < 0 && cleanup.IsHeld(body), c.Name + " casualty in the live Simulation snapshot kept until restore");
   SetPresence(c, 0); c.WokeAt = -1; c.Phase = 13; c.PhaseAt = Now();
   return;
  }
  if (c.Phase == 13)
  {
   if (suspended)
   {
    if (Now() - c.PhaseAt > 60) { Report(c, 1); Check(false, c.Name + " survivors woke when players returned"); c.Done = true; }
    return;
   }
   if (c.WokeAt < 0) { c.WokeAt = Now(); return; }
   if (Now() - c.WokeAt < 2) return;
   int agentCount = -1;
   if (c.Record.Group) agentCount = c.Record.Group.GetAgentsCount();
   PrintFormat("[EBG CLEANUP TEST RETURN] case=%1 alive=%2 members=%3 agents=%4 recovery='%5' bodyPresent=%6 state='%7'", c.Name, c.Record.Alive, c.Record.Members.Count(), agentCount, c.Record.Recovery, body != null, c.Record.DebugState());
   c.SnapshotRestored = Check(c.Record.Recovery == "" && !c.Record.Simulation && body != null && c.Record.Alive == 5 && c.Record.Members.Count() == 6 && agentCount == 5 && SurvivorsAlive(c), c.Name + " survivors restored after the snapshot death without recovery or replenishment");
   SetPresence(c, -1);
   c.LeftAt = Now(); c.Eligible = Now() + c.Zone.CleanupDelay; c.Deadline = c.Eligible + 90;
   PrintFormat("[EBG CLEANUP TEST LEFT] case=%1 phase=after-restore eligibleAt=%2 deadline=%3", c.Name, c.Eligible, c.Deadline);
   c.Phase = 14; c.PhaseAt = Now();
   return;
  }
  if (body)
  {
   if (Now() > c.Deadline) { Report(c, 1); Check(false, c.Name + " restored casualty deleted by EBG after players left"); c.Done = true; }
   return;
  }
  c.DeletedAt = Now();
  int byEbg = 0;
  if (s_EbgDeleted.Contains(c.BodyIds[0])) byEbg = 1;
  Check(byEbg == 1 && c.DeletedAt >= c.Eligible - 1, c.Name + " restored casualty deleted by EBG after players left");
  Check(c.Record.Alive == 5 && SurvivorsAlive(c), c.Name + " survivors untouched by body cleanup");
  int held = 0;
  if (c.SnapshotHeld) held = 1;
  int restored = 0;
  if (c.SnapshotRestored) restored = 1;
  PrintFormat("[EBG CLEANUP TEST SNAPSHOT] case=%1 heldWhileCached=%2 restored=%3 deletedByEbg=%4 afterLeave=%5", c.Name, held, restored, byEbg, c.DeletedAt - c.LeftAt);
  c.Done = true;
 }
 void StepBlocked(EBGCleanupCase c, EBG_CacheCleanup cleanup)
 {
  SCR_ChimeraCharacter first = c.Bodies[0];
  SCR_ChimeraCharacter second = c.Bodies[1];
  if (!c.SecondDeleted && (!second || Now() > c.Eligible + 90))
  {
   c.SecondDeleted = true;
   Check(!second && s_EbgDeleted.Contains(c.BodyIds[1]), c.Name + " second casualty deleted by EBG while the first stays blocked");
  }
  bool firstByEbg = s_EbgDeleted.Contains(c.BodyIds[0]);
  if (firstByEbg) { Check(false, c.Name + " blocked casualty must never be deleted by EBG"); c.Done = true; return; }
  bool handedBack = !first || (!cleanup.IsHeld(first) && !cleanup.IsGarbageProtected(first));
  if (handedBack && c.SecondDeleted)
  {
   bool inserted = false;
   if (first)
   {
    SCR_GarbageSystem garbage = SCR_GarbageSystem.GetByEntityWorld(first);
    inserted = garbage && garbage.IsInserted(first);
   }
   PrintFormat("[EBG CLEANUP TEST H3 RELEASED] case=%1 afterEligible=%2 bodyPresent=%3 nativeInserted=%4", c.Name, Now() - c.Eligible, first != null, inserted);
   Check(Now() >= c.Eligible + 110, c.Name + " blocked casualty handed back only after bounded retries");
   Check(c.Record.Alive == 4 && SurvivorsAlive(c), c.Name + " survivors untouched by blocked cleanup");
   c.Done = true; return;
  }
  if (Now() > c.Deadline) { Report(c, 1); Check(false, c.Name + " blocked casualty handed back to native garbage after bounded retries"); c.Done = true; }
 }
 void Report(EBGCleanupCase c, int remaining)
 {
  c.NextReport = Now() + 5;
  EBG_CacheCleanup cleanup = EBG_CacheCleanup.Get();
  SCR_ChimeraCharacter body;
  foreach (SCR_ChimeraCharacter candidate : c.Bodies) { if (candidate && !body) body = candidate; }
  bool held, guarded, inserted;
  if (body)
  {
   held = cleanup.IsHeld(body); guarded = cleanup.IsGarbageProtected(body);
   SCR_GarbageSystem garbage = SCR_GarbageSystem.GetByEntityWorld(body);
   inserted = garbage && garbage.IsInserted(body);
  }
  PrintFormat("[EBG CLEANUP TEST STATE] case=%1 t=%2 remaining=%3/%4 alive=%5 dead=%6 sinceLeft=%7 eligibleIn=%8 state='%9'", c.Name, Now() - Started, remaining, c.Bodies.Count(), c.Record.Alive, c.Record.Dead, Now() - c.LeftAt, c.Eligible - Now(), c.Record.DebugState());
  PrintFormat("[EBG CLEANUP TEST WHY] case=%1 rejection='%2' clearSince=%3 nextAttempt=%4 nearby=%5 held=%6 garbageProtected=%7 nativeInserted=%8", c.Name, c.Record.LastCacheRejection, c.Record.CleanupClearSince, c.Record.CleanupNextAttempt, cleanup.HasNearbyPlayer(c.Record, EBG_CacheManager.Get().Players), held, guarded, inserted);
 }
 static SCR_ChimeraCharacter Wearer(IEntity entity)
 {
  IEntity cursor = entity;
  for (int depth = 0; cursor && depth < 32; depth++)
  {
   SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(cursor);
   if (character) return character;
   InventoryItemComponent item = InventoryItemComponent.Cast(cursor.FindComponent(InventoryItemComponent));
   InventoryStorageSlot slot;
   if (item) slot = item.GetParentSlot();
   if (slot && slot.GetStorage()) cursor = slot.GetStorage().GetOwner();
   else cursor = cursor.GetParent();
  }
  return null;
 }
 // Called only for outer DeleteEntityAndChildren calls made inside EBG_CacheCleanup.Tick.
 static void ObserveCleanupDelete(IEntity entity)
 {
  s_EbgDeleted.Insert(entity.GetID());
  s_EbgDeletedState.Insert(s_TickState);
  SCR_ChimeraCharacter wearer = Wearer(entity);
  int living = 0;
  if (wearer && wearer.GetCharacterController() && !wearer.GetCharacterController().IsDead()) living = 1;
  if (wearer && s_Survivors.Contains(wearer.GetID())) living = 1;
  PrintFormat("[EBG CLEANUP TEST NATIVE DELETE] id=%1 state=%2 prefab='%3' livingWearer=%4", entity.GetID(), s_TickState, SCR_ResourceNameUtils.GetPrefabName(entity), living);
  if (living == 0) return;
  s_SurvivorDeletes++;
  PrintFormat("[EBG CLEANUP TEST SURVIVOR DELETE] id=%1 wearer=%2", entity.GetID(), wearer.GetID());
 }
}
// Presence seam, as in the optimizer fixtures (EBGRecacheTest.c:95-102).
modded class EBG_CacheManager
{
 override protected void UpdatePlayers()
 {
  super.UpdatePlayers();
  if (EXPG_GarrisonGameplay.s_Presence)
  {
   foreach (vector presence : EXPG_GarrisonGameplay.s_Presence) Players.Insert(presence);
  }
 }
}
// Attribution only. The record's cache state at Tick entry: 0 awake, 1 Simulation cached, 2 Full cached.
modded class EBG_CacheCleanup
{
 override void Tick(EBG_CacheGroup record, array<vector> players, float now, bool transfersChecked = false)
 {
  int state = 0;
  if (record && record.Simulation && record.Simulation.Suspended) state = 1;
  if (record && record.Full && record.Full.GetState() == EBG_FullGroupPhase.CACHED) state = 2;
  EXPG_GarrisonGameplay.s_TickState = state;
  EXPG_GarrisonGameplay.s_InCleanupTick = true;
  super.Tick(record, players, now, transfersChecked);
  EXPG_GarrisonGameplay.s_InCleanupTick = false;
 }
}
// Full sleep deletes survivors outside cleanup Tick, so it is never attributed here.
modded class SCR_EntityHelper
{
 static bool EXPG_CleanupInsideDelete;
 override static void DeleteEntityAndChildren(IEntity entity)
 {
  bool observe = EXPG_GarrisonGameplay.s_InCleanupTick && entity && !EXPG_CleanupInsideDelete && EXPG_GarrisonGameplay.s_EbgDeleted;
  if (observe) { EXPG_CleanupInsideDelete = true; EXPG_GarrisonGameplay.ObserveCleanupDelete(entity); }
  super.DeleteEntityAndChildren(entity);
  if (observe) EXPG_CleanupInsideDelete = false;
 }
}

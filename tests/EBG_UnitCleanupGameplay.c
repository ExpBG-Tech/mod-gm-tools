// TEST ONLY. Unit Caching per-casualty cleanup gameplay fixture.
// pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -UnitCleanup -TimeoutSeconds 540 -OrchestratorSlotGranted
// Add -Rhs to also load RHS: Status Quo (linked from the installed addons) for the rhs-mg-team
// and rhs-usmc-recon cases.
// Real zone prefab and public SetValue, real manager enrollment/Tick/cleanup, native Kill.
// Presence is injected through EBG_CacheManager.UpdatePlayers; the server has no players.
// The possession probe (outside the 14 cases) stands two of its own soldiers in for players
// through the manager's player seam (EBG_CollectPlayers, EBG_PlayerEntity): a direct
// PlayerPossession releases the member's rows and marks him; a later pass skips an unchanged
// stand-in and cross-checks him (CheckPossession); after BumpRoster and after a ledger bind
// the next pass reruns every stand-in's possession.
// The runner copies this file to EXPG_GarrisonGameplay.c; the class names are fixed.
class EBGCleanupCase
{
 string Name;
 // Native squad and its size; USSR rifle squad unless a case says otherwise.
 ResourceName Squad = "{E552DABF3636C2AD}Prefabs/Groups/OPFOR/Group_USSR_RifleSquad.et";
 int Size = 6;
 int Mode;
 int CorpseAge;
 int Kills;
 float AfterKill = -1;
 bool ExpectAwake;
 bool ExpectCached;
 bool WakeBand;
 bool ForeignItem;
 bool ReturnPhase;
 bool SnapshotDeath;
 bool SnapshotHeld;
 bool SnapshotRestored;
 // Atomic per-casualty proof: the vanilla US E-tool casualty and the foreign-item casualty.
 bool ETool;
 bool EToolProven;
 // Identity-less gear: a worn vest with its native identity stripped and an unregistered
 // weapon part on the casualty's weapon (the RHS preset-vest and weapon-part situation).
 bool Identityless;
 // RHS: Status Quo units; skipped (available=0) unless the runner loaded RHS.
 bool RequiresRhs;
 // The rhs-mg-team save-provenance and deletion proof.
 bool Rhs;
 bool ProvenanceProven;
 // Worn accessory in a storage-less LoadoutSlotInfo of a cloth the native vest path does not
 // list (the RHS USMC boonie's Comtacs headset): it stays the casualty's own row, held and
 // owned by the wearer, never parks the casualty and goes with the body. UnlistedCloth marks
 // the prepared casualty's stock cloth unlisted (vanilla stand-in, test seam below).
 bool ClothSlotCheck;
 bool UnlistedCloth;
 bool ClothSlotProven;
 // Possession probe (EXPG_GarrisonGameplay.PossessionProbe, phases 20-24).
 bool Possession;
 int SkipMark;
 int FirstCalls = -1;
 int RosterCalls = -1;
 ref array<EntityID> AccessoryIds = {};
 ref array<EntityID> AccessoryWearerIds = {};
 ref array<EntityID> AccessoryClothIds = {};
 SCR_ChimeraCharacter Prepared;
 EntityID PreparedWeaponId;
 EntityID PartId;
 EntityID VestId;
 ResourceName VestPrefab;
 bool VestStripped;
 ref array<EntityID> OwnedIds = {};
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
 static ref array<int> s_EbgDeletedSerial;
 static ref array<EntityID> s_Survivors;
 // Stock cloths the test seam reports as unlisted to NativeVestAccessoryOwner.
 static ref array<IEntity> s_UnlistedCloths;
 static bool s_InCleanupTick;
 // Possession probe: stand-in characters the player seam reports as players with ids
 // STAND_IN_ID + index, and counters written by the modded EBG_CacheManager below.
 static const int STAND_IN_ID = 900001;
 static ref array<IEntity> s_StandIns;
 static int s_PossessionCalls;
 static int s_SkippedChecks;
 static int s_PassSerial;
 static int s_WatchPass;
 static int s_WatchCalls;
 static int s_TickState;
 static int s_TickSerial;
 static int s_SurvivorDeletes;
 static int s_PartialStrips;
 ref array<ref EBGCleanupCase> Cases = {};
 // Not one of Cases: the runner's evidence pins cases=14.
 ref EBGCleanupCase PossessionProbe;
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
  EBG_DebugChecks.Enabled = true; EBG_DebugChecks.Mismatches = 0; // indexes also run their old full scans
  s_Presence = {}; s_EbgDeleted = {}; s_EbgDeletedState = {}; s_EbgDeletedSerial = {}; s_Survivors = {}; s_UnlistedCloths = {}; s_SurvivorDeletes = 0; s_PartialStrips = 0; s_TickSerial = 0;
  s_StandIns = {}; s_PossessionCalls = 0; s_SkippedChecks = 0; s_PassSerial = 0; s_WatchPass = -1; s_WatchCalls = -1;
  Started = Now(); Next = Started + 15;
  // Partial cases first: earlier records win the shared one-casualty-per-scan allowance.
  EBGCleanupCase awake = AddCase("sim-partial-awake", 0, 30, 1, 0, 0, 130); awake.ExpectAwake = true;
  EBGCleanupCase simCached = AddCase("sim-partial-cached", 0, 150, 1, 600, 0, -1); simCached.ExpectCached = true; simCached.ReturnPhase = true;
  EBGCleanupCase fullCached = AddCase("full-partial-cached", 1, 150, 1, 0, 600, -1); fullCached.ExpectCached = true; fullCached.ReturnPhase = true;
  EBGCleanupCase fullAwake = AddCase("full-partial-awake-then-cache", 1, 30, 1, 600, 600, 130); fullAwake.ExpectAwake = true; fullAwake.ReturnPhase = true;
  EBGCleanupCase band = AddCase("sim-partial-wakeband", 0, 30, 1, -600, 0, 50); band.WakeBand = true;
  EBGCleanupCase foreign = AddCase("foreign-item", 0, 30, 2, 0, -600, -1); foreign.ForeignItem = true;
  // Atomic cleanup: a vanilla US fire-team casualty carrying the ALICE E-tool is removed with
  // its whole gear in one Tick (body root once, owned ground roots the same tick).
  EBGCleanupCase etool = AddCase("us-etool-atomic", 0, 30, 1, 300, 300, -1); etool.ETool = true;
  etool.Squad = "{84E5BBAB25EA23E5}Prefabs/Groups/BLUFOR/Group_US_FireTeam.et"; etool.Size = 4;
  // 1.8 persistence gives no UUID to prefabs outside its PrefabPersistenceConfigRule bases
  // (RHS preset vests, RHS_WeaponPart_Base parts). Reproduced natively without RHS: the
  // casualty's worn vest loses its identity (StopTracking) and an unregistered WeaponPart_Base
  // stock rides his weapon. The save leaves them out, nothing blocks, body and weapon go together.
  EBGCleanupCase identityless = AddCase("identityless-gear", 0, 30, 1, -300, 300, -1); identityless.Identityless = true;
  AddCase("sim-all-control", 0, 30, 6, -600, -600, -1);
  AddCase("full-all-control", 1, 30, 6, 600, -600, -1);
  // RHS AFRF machine-gun team, both killed: PKP and AK-74M parts plus the 6B45 PKM preset vest
  // have no native identity (server evidence 2026-10-06). Needs the runner's -Rhs switch.
  EBGCleanupCase rhs = AddCase("rhs-mg-team", 0, 30, 2, 300, -300, -1); rhs.Rhs = true; rhs.RequiresRhs = true;
  rhs.Squad = "{60E2D587BE5A9B43}Prefabs/Groups/OPFOR/RHS_AFRF/MSV/VKPO_Demiseason/Group_RHS_RF_MSV_VKPO_DS_MachineGunTeam.et"; rhs.Size = 2;
  // B1: an accessory in a storage-less LoadoutSlotInfo of an unlisted cloth resolved no owner,
  // so the casualty was parked (a CLEANUP KEEP line) or the accessory released as an ownership
  // transfer with holder NULL. Vanilla stand-in: a stock Lifchik/6B3 canteen whose cloth the
  // test seam marks unlisted. Same walk as the RHS USMC boonie's Comtacs headset.
  EBGCleanupCase clothSlot = AddCase("cloth-slot-accessory", 0, 30, 1, -300, -300, -1); clothSlot.ClothSlotCheck = true; clothSlot.UnlistedCloth = true;
  // RHS USMC MEF recon team (scout and scout RTO, both in Hat_USMC_Boonie_Comtac with the
  // Peltor headset in its storage-less Comtacs slot), both killed. Needs -Rhs.
  EBGCleanupCase usmc = AddCase("rhs-usmc-recon", 0, 30, 2, 300, 0, -1); usmc.ClothSlotCheck = true; usmc.RequiresRhs = true;
  usmc.Squad = "{CE3326F78B0125CC}Prefabs/Groups/BLUFOR/RHS_USAF/RHS_USAF_USMC_MEF/Group_USAF_USMC_MEF_ReconTeam.et"; usmc.Size = 2;
  // Snapshot rule: a soldier killed while his group is Simulation cached keeps his body
  // until the survivors are restored; only then may cleanup delete it.
  EBGCleanupCase snapshot = AddCase("sim-death-while-cached", 0, 30, 0, -600, 600, -1); snapshot.SnapshotDeath = true;
  // Possession probe: a Simulation squad, nobody killed, between four dry case points.
  PossessionProbe = new EBGCleanupCase();
  PossessionProbe.Name = "possession-rerun"; PossessionProbe.Possession = true;
  PossessionProbe.Mode = 0; PossessionProbe.CorpseAge = 30; PossessionProbe.Kills = 0; PossessionProbe.AfterKill = -1;
  PossessionProbe.Point = Origin + Vector(-300, 0, 0);
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
  Check(s_PartialStrips == 0, "no casualty body was stripped item by item inside EBG cleanup");
  s_Presence.Clear();
  s_StandIns.Clear();
  ClearEventMask(EntityEvent.FRAME);
  Check(EBG_DebugChecks.Mismatches == 0, "index cross-checks matched their old full scans");
  PrintFormat("[EBG CLEANUP TEST RESULT] checks=%1 failures=%2 cases=%3 reason=%4 mismatches=%5", Checks, Failures, Cases.Count(), reason, EBG_DebugChecks.Mismatches);
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
   if (!PossessionProbe.Done) Check(false, PossessionProbe.Name + " finished before the fixture deadline");
   Finish("timeout");
   return;
  }
  bool all = true;
  foreach (EBGCleanupCase c : Cases) { if (!c.Done) { Step(c); all = false; } }
  if (!PossessionProbe.Done) { Step(PossessionProbe); all = false; }
  if (all) Finish("complete");
 }
 void Step(EBGCleanupCase c)
 {
  EBG_CacheCleanup cleanup = EBG_CacheCleanup.Get();
  if (c.Phase == 0)
  {
   // Never load an RHS resource unless RHS is loaded: a missing one only logs errors.
   if (c.RequiresRhs && !RhsLoaded())
   {
    if (c.ClothSlotCheck) PrintFormat("[EBG CLEANUP TEST CLOTH SLOT] case=%1 available=0 reason='RHS: Status Quo and its content packs are not loaded; run with -Rhs'", c.Name);
    else PrintFormat("[EBG CLEANUP TEST RHS] case=%1 available=0 reason='RHS: Status Quo and its content packs are not loaded; run with -Rhs'", c.Name);
    c.Done = true; return;
   }
   // Keep the Resource and the spawned entity in locals before casting; the inline form
   // returned null for every case in native runs (2026-10-05).
   Resource squad = Resource.Load(c.Squad);
   IEntity squadEntity = GetGame().SpawnEntityPrefab(squad, GetGame().GetWorld(), Params(c.Point));
   c.Group = SCR_AIGroup.Cast(squadEntity);
   if (!Check(c.Group != null, c.Name + " native squad spawned")) { c.Done = true; return; }
   c.Phase = 1; c.PhaseAt = Now(); return;
  }
  if (c.Phase == 1)
  {
   if (!c.Group || c.Group.GetAgentsCount() != c.Size || !c.Group.EBG_HasCompletedInitialSpawn())
   {
    if (Now() - c.PhaseAt > 30) { Check(false, string.Format("%1 squad of %2 completed its initial spawn", c.Name, c.Size)); c.Done = true; }
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
   // Before enrollment, so the identity-less gear enters the ledger as the casualty's own.
   if (c.Identityless && !PrepareIdentityless(c)) { c.Done = true; return; }
   if (c.UnlistedCloth && !PrepareUnlistedCloth(c)) { c.Done = true; return; }
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
   if (!c.Record || c.Record.Members.Count() != c.Size || !c.Record.CleanupRegistered || c.Zone.Editing)
   {
    if (Now() - c.PhaseAt > 40)
    {
     PrintFormat("[EBG CLEANUP TEST ZONE] case=%1 status='%2' editing=%3 loadHold=%4 reason='%5'", c.Name, c.Zone.Status, c.Zone.Editing, c.Zone.EBG_HasSettingsLoadHold(), c.Zone.EBG_SettingsLoadReason());
     Check(false, string.Format("%1 manager enrolled %2 members with a cleanup ledger", c.Name, c.Size));
     c.Done = true;
    }
    return;
   }
   IEntity leader = c.Group.GetLeaderEntity();
   foreach (EBG_CacheMember enrolled : c.Record.Members)
   {
    bool chosen = c.Casualties.Count() < c.Kills && (c.Kills == c.Size || enrolled.Entity != leader);
    // The identity-less case kills exactly the soldier whose gear it prepared.
    if (c.Prepared) chosen = enrolled.Entity == c.Prepared;
    if (chosen)
    {
     c.Casualties.Insert(enrolled); c.Bodies.Insert(enrolled.Entity); c.BodyIds.Insert(enrolled.Entity.GetID());
    }
    else { c.SurvivorIds.Insert(enrolled.Entity.GetID()); s_Survivors.Insert(enrolled.Entity.GetID()); }
   }
   if (c.ClothSlotCheck && !CollectSlotAccessories(c, cleanup)) { c.Done = true; return; }
   if (c.Identityless)
   {
    IEntity part = GetGame().GetWorld().FindEntityByID(c.PartId);
    IEntity vest = GetGame().GetWorld().FindEntityByID(c.VestId);
    bool enrolledGear = c.Casualties.Count() == 1 && part && cleanup.IsHeld(part) && (!vest || cleanup.IsHeld(vest));
    if (!Check(enrolledGear, c.Name + " identity-less vest and weapon part enrolled as the casualty's own rows")) { c.Done = true; return; }
   }
   if (c.Possession)
   {
    c.Phase = 20; c.PhaseAt = Now();
    PrintFormat("[EBG CLEANUP TEST POSSESSION SETUP] case=%1 group=%2 members=%3", c.Name, c.Record.Id, c.Record.Members.Count());
    return;
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
   if (c.ForeignItem)
   {
    Resource magazine = Resource.Load("{0A84AA5A3884176F}Prefabs/Weapons/Magazines/Magazine_545x39_AK_30rnd_Last_5Tracer.et");
    IEntity magazineEntity = GetGame().SpawnEntityPrefab(magazine, GetGame().GetWorld(), Params(c.Bodies[0].GetOrigin()));
    c.Foreign = magazineEntity;
    SCR_InventoryStorageManagerComponent inventory = SCR_InventoryStorageManagerComponent.Cast(c.Bodies[0].FindComponent(SCR_InventoryStorageManagerComponent));
    bool requested = c.Foreign && inventory && inventory.TryInsertItem(c.Foreign);
    PrintFormat("[EBG CLEANUP TEST FOREIGN SETUP] case=%1 method=inventory requested=%2", c.Name, requested);
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
    // Fallback: any unregistered node in the casualty's hierarchy is the same foreign content.
    c.Bodies[0].AddChild(c.Foreign, -1);
    inside = EBG_FullCacheGroup.InventoryBelongsTo(c.Foreign, c.Bodies[0]);
    PrintFormat("[EBG CLEANUP TEST FOREIGN SETUP] case=%1 method=child inside=%2", c.Name, inside);
   }
   if (!Check(inside && !cleanup.IsHeld(c.Foreign), c.Name + " unregistered magazine inside the first casualty")) { c.Done = true; return; }
   // 0.1.4 rule: no per-item checks; foreign content inside the body goes with the body.
   Check(cleanup.CanDeleteCasualty(c.Record, c.Casualties[0]), c.Name + " unregistered content does not block the first casualty");
   RecordOwned(c, cleanup);
   PrintFormat("[EBG CLEANUP TEST FOREIGN OWNED] case=%1 owned=%2", c.Name, c.OwnedIds.Count());
   Leave(c); return;
  }
  if (c.Phase >= 20) { StepPossession(c, cleanup); return; }
  int remaining = Observe(c);
  if (c.Phase >= 10) { StepSnapshot(c, cleanup); return; }
  if (c.Phase == 5)
  {
   if (c.ForeignItem) { StepForeign(c); return; }
   if (c.WakeBand)
   {
    if (remaining != c.Bodies.Count()) { Check(false, c.Name + " presence inside the wake radius holds cleanup"); c.Done = true; return; }
    if (Now() < c.Deadline) return;
    // Assert the hold itself, not just a surviving body: the shared one-casualty-per-scan
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
   if (c.ETool && !c.EToolProven && remaining > 0 && Now() >= c.Eligible - 3)
   {
    // Just before eligibility: native death births have settled, nothing deleted yet.
    c.EToolProven = true;
    bool carriesETool = RecordOwned(c, cleanup);
    bool provable = cleanup.CanDeleteCasualty(c.Record, c.Casualties[0]);
    PrintFormat("[EBG CLEANUP TEST ETOOL SETUP] case=%1 owned=%2 etool=%3 provable=%4 reason='%5'", c.Name, c.OwnedIds.Count(), carriesETool, provable, cleanup.GetLastReason());
    Check(carriesETool, c.Name + " casualty carries the vanilla ALICE E-tool");
    Check(provable, c.Name + " whole casualty (body, gear and ground roots) proven deletable before any deletion");
   }
   if ((c.Identityless || c.Rhs) && !c.ProvenanceProven && remaining == c.Bodies.Count() && Now() >= c.Eligible - 3)
   {
    // Just before eligibility, nothing deleted yet: what a native save would write now.
    c.ProvenanceProven = true;
    ProveSaveProvenance(c, cleanup);
   }
   if (c.ClothSlotCheck && !c.ClothSlotProven && remaining == c.Bodies.Count() && Now() >= c.Eligible - 3)
   {
    c.ClothSlotProven = true;
    ProveSlotAccessories(c, cleanup);
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
  if (c.Kills == c.Size) c.Deadline = c.Eligible + c.Rows * 0.75 + 30;
  if (c.WakeBand) c.Deadline = c.Eligible + 20;
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
  if (c.Kills < c.Size)
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
   if (!c.ExpectAwake && !c.ExpectCached) Check(c.Record.Alive == c.Size - c.Kills && SurvivorsAlive(c), c.Name + " survivors untouched by body cleanup");
  }
  if (c.ETool) CheckAtomic(c);
  if (c.Identityless) CheckIdentitylessDeleted(c);
  if (c.Rhs) CheckRhsDeleted(c);
  if (c.ClothSlotCheck) CheckSlotAccessoriesDeleted(c);
  c.PhaseAt = Now();
  if (c.Kills == c.Size) { c.Phase = 9; return; }
  if (!c.ReturnPhase) { c.Done = true; return; }
  c.Phase = 6;
  if (c.ExpectAwake) SetPresence(c, -1);
 }
 // sim-death-while-cached: 10 wait for Simulation cache, 11 confirm the cached death,
 // 12 hold while the casualty is in the live snapshot, 13 players return and the
 // survivors restore, 14 players leave and cleanup deletes the restored casualty.
 // Possession probe. 20: a direct PlayerPossession releases the member's held rows and marks
 // him; then he and a squadmate stand in for two players. 21: their first pass runs both
 // possessions. 22: a later pass skips an unchanged stand-in and cross-checks him; then
 // BumpRoster. 23: the next pass reruns both; then a ledger bind. 24: the next pass reruns both.
 void StepPossession(EBGCleanupCase c, EBG_CacheCleanup cleanup)
 {
  EBG_CacheManager manager = EBG_CacheManager.Get();
  if (c.Phase == 20)
  {
   if (!Check(c.Record && c.Record.Members.Count() >= 2 && c.Record.Members[0].Entity && c.Record.Members[1].Entity, c.Name + " two living enrolled members for the possession probe")) { EndPossession(c); return; }
   EBG_CacheMember possessed = c.Record.Members[1];
   int heldBefore = cleanup.EXPG_HeldMemberRows(possessed);
   int callsBefore = s_PossessionCalls;
   manager.PlayerPossession(possessed.Entity);
   int heldAfter = cleanup.EXPG_HeldMemberRows(possessed);
   bool marked = possessed.Entity.EBG_IsMarkedPlayer();
   PrintFormat("[EBG CLEANUP TEST POSSESSION] case=%1 heldBefore=%2 heldAfter=%3 wasPlayer=%4 marked=%5 calls=%6", c.Name, heldBefore, heldAfter, possessed.WasPlayer, marked, s_PossessionCalls - callsBefore);
   Check(heldBefore > 0 && heldAfter == 0 && possessed.WasPlayer && marked, c.Name + " direct possession released the member's held rows and marked him");
   s_StandIns.Insert(possessed.Entity);
   s_StandIns.Insert(c.Record.Members[0].Entity);
   s_WatchPass = s_PassSerial + 1; s_WatchCalls = -1;
   c.Phase = 21; c.PhaseAt = Now();
   return;
  }
  if (c.Phase == 21)
  {
   if (s_WatchCalls < 0)
   {
    if (Now() - c.PhaseAt > 10) { Check(false, c.Name + " a pass ran with the stand-in players"); EndPossession(c); }
    return;
   }
   c.FirstCalls = s_WatchCalls;
   Check(c.FirstCalls == s_StandIns.Count(), string.Format("%1 the first pass ran every new stand-in's possession (%2 of %3)", c.Name, c.FirstCalls, s_StandIns.Count()));
   c.SkipMark = s_SkippedChecks;
   c.Phase = 22; c.PhaseAt = Now();
   return;
  }
  if (c.Phase == 22)
  {
   if (s_SkippedChecks <= c.SkipMark)
   {
    if (Now() - c.PhaseAt > 60) { Check(false, c.Name + " a pass skipped an unchanged stand-in and cross-checked him"); EndPossession(c); }
    return;
   }
   Check(true, c.Name + " a pass skipped an unchanged stand-in and cross-checked him");
   s_WatchPass = s_PassSerial + 1; s_WatchCalls = -1;
   manager.BumpRoster();
   c.Phase = 23; c.PhaseAt = Now();
   return;
  }
  if (c.Phase == 23)
  {
   if (s_WatchCalls < 0)
   {
    if (Now() - c.PhaseAt > 10) { Check(false, c.Name + " a pass ran after BumpRoster"); EndPossession(c); }
    return;
   }
   c.RosterCalls = s_WatchCalls;
   Check(c.RosterCalls == s_StandIns.Count(), string.Format("%1 after BumpRoster the next pass reran every stand-in's possession (%2 of %3)", c.Name, c.RosterCalls, s_StandIns.Count()));
   s_WatchPass = s_PassSerial + 1; s_WatchCalls = -1;
   EBG_CacheCleanup.s_EBG_BindVersion++;
   c.Phase = 24; c.PhaseAt = Now();
   return;
  }
  if (s_WatchCalls < 0)
  {
   if (Now() - c.PhaseAt > 10) { Check(false, c.Name + " a pass ran after a ledger bind"); EndPossession(c); }
   return;
  }
  Check(s_WatchCalls == s_StandIns.Count(), string.Format("%1 after a ledger bind the next pass reran every stand-in's possession (%2 of %3)", c.Name, s_WatchCalls, s_StandIns.Count()));
  PrintFormat("[EBG CLEANUP TEST POSSESSION RERUN] case=%1 standIns=%2 firstPass=%3 skippedChecks=%4 afterRoster=%5 afterBind=%6 mismatches=%7", c.Name, s_StandIns.Count(), c.FirstCalls, s_SkippedChecks, c.RosterCalls, s_WatchCalls, EBG_DebugChecks.Mismatches);
  EndPossession(c);
 }
 void EndPossession(EBGCleanupCase c)
 {
  s_StandIns.Clear();
  s_WatchPass = -1;
  c.Done = true;
 }
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
 void StepForeign(EBGCleanupCase c)
 {
  if (c.Bodies[0] || c.Bodies[1] || c.Foreign)
  {
   if (Now() > c.Deadline) { Report(c, 1); Check(false, c.Name + " casualties deleted whole by EBG, foreign item included"); c.Done = true; }
   return;
  }
  int firstByEbg = 0;
  if (s_EbgDeleted.Contains(c.BodyIds[0])) firstByEbg = 1;
  int secondByEbg = 0;
  if (s_EbgDeleted.Contains(c.BodyIds[1])) secondByEbg = 1;
  PrintFormat("[EBG CLEANUP TEST FOREIGN] case=%1 owned=%2 present=%3 firstByEbg=%4 secondByEbg=%5 foreignGone=1 afterEligible=%6", c.Name, c.OwnedIds.Count(), PresentOwned(c), firstByEbg, secondByEbg, Now() - c.Eligible);
  Check(firstByEbg == 1 && secondByEbg == 1, c.Name + " casualties deleted whole by EBG, foreign item included");
  Check(PresentOwned(c) == 0, c.Name + " nothing of the first casualty left in the world");
  Check(c.Record.Alive == 4 && SurvivorsAlive(c), c.Name + " survivors untouched by body cleanup");
  c.Done = true;
 }
 // Every entity the cleanup ledger holds for the case's first casualty (body, gear,
 // owned ground roots). Returns whether the vanilla ALICE E-tool is among them.
 bool RecordOwned(EBGCleanupCase c, EBG_CacheCleanup cleanup)
 {
  array<IEntity> heldEntities = {};
  cleanup.CollectHeldMemberEntities(c.Casualties[0], heldEntities);
  c.OwnedIds.Clear();
  bool etool;
  foreach (IEntity item : heldEntities)
  {
   if (!item) continue;
   c.OwnedIds.Insert(item.GetID());
   if (SCR_ResourceNameUtils.GetPrefabName(item) == "{6E35D94130954509}Prefabs/Items/Equipment/Accessories/ETool_ALICE/ETool_ALICE_FreeRoamBuilding_Gadget.et") etool = true;
  }
  return etool;
 }
 int PresentOwned(EBGCleanupCase c)
 {
  int present = 0;
  foreach (EntityID id : c.OwnedIds) { if (GetGame().GetWorld().FindEntityByID(id)) present++; }
  return present;
 }
 // The body and every owned entity are gone, and every outer EBG delete of them ran in the
 // same Tick call as the body's delete: no partial strip across scans.
 void CheckAtomic(EBGCleanupCase c)
 {
  int bodySerial = -1;
  int bodyAt = s_EbgDeleted.Find(c.BodyIds[0]);
  if (bodyAt >= 0) bodySerial = s_EbgDeletedSerial[bodyAt];
  int outer = 0;
  int sameTick = 1;
  foreach (EntityID ownedId : c.OwnedIds)
  {
   int deletedAt = s_EbgDeleted.Find(ownedId);
   if (deletedAt < 0) continue;
   outer++;
   if (s_EbgDeletedSerial[deletedAt] != bodySerial) sameTick = 0;
  }
  int gone = 0;
  if (c.OwnedIds.Count() > 1 && PresentOwned(c) == 0) gone = 1;
  if (bodySerial < 0) sameTick = 0;
  PrintFormat("[EBG CLEANUP TEST ATOMIC] case=%1 owned=%2 etool=1 gone=%3 outerDeletes=%4 sameTick=%5 partialStrips=%6", c.Name, c.OwnedIds.Count(), gone, outer, sameTick, s_PartialStrips);
  Check(gone == 1 && sameTick == 1, c.Name + " body and all gear including the E-tool removed together in one cleanup tick");
 }
 // RHS: Status Quo plus both content packs, as the runner's -Rhs switch loads them.
 static bool RhsLoaded()
 {
  array<string> loaded = {};
  GameProject.GetLoadedAddons(loaded);
  int found = 0;
  foreach (string addon : loaded)
  {
   string upper = addon;
   upper.ToUpper();
   if (upper == "595F2BF2F44836FB" || upper == "1337C0DE5DABBEEF" || upper == "BADC0DEDABBEDA5E") found++;
  }
  return found == 3;
 }
 // The prospective casualty (first non-leader) gets the identity-less gear before enrollment.
 bool PrepareIdentityless(EBGCleanupCase c)
 {
  IEntity leader = c.Group.GetLeaderEntity();
  array<AIAgent> agents = {};
  c.Group.GetAgents(agents);
  foreach (AIAgent agent : agents)
  {
   SCR_ChimeraCharacter candidate = SCR_ChimeraCharacter.Cast(agent.GetControlledEntity());
   if (!c.Prepared && candidate && candidate != leader) c.Prepared = candidate;
  }
  if (!Check(c.Prepared != null, c.Name + " non-leader casualty chosen before enrollment")) return false;
  PersistenceSystem persistence = PersistenceSystem.GetInstance();
  // Worn vest without native identity, like an RHS preset vest (no Vest_Base ancestor).
  SCR_CharacterInventoryStorageComponent storage = SCR_CharacterInventoryStorageComponent.Cast(c.Prepared.FindComponent(SCR_CharacterInventoryStorageComponent));
  IEntity vest;
  if (storage) vest = storage.GetClothFromArea(LoadoutVestArea);
  if (vest)
  {
   c.VestId = vest.GetID();
   c.VestPrefab = SCR_ResourceNameUtils.GetPrefabName(vest);
   if (persistence) persistence.StopTracking(vest);
   c.VestStripped = persistence && persistence.GetId(vest).IsNull();
  }
  // Weapon part without native identity, like an RHS_WeaponPart_Base part: an unregistered
  // WeaponPart_Base stock (no Attachment_Base ancestor, not an approved static part) that
  // rides the weapon as a hierarchy child.
  BaseWeaponManagerComponent weapons = BaseWeaponManagerComponent.Cast(c.Prepared.FindComponent(BaseWeaponManagerComponent));
  IEntity weapon;
  if (weapons && weapons.GetCurrentWeapon()) weapon = weapons.GetCurrentWeapon().GetOwner();
  if (!weapon && weapons)
  {
   array<IEntity> carried = {};
   weapons.GetWeaponsList(carried);
   foreach (IEntity carriedWeapon : carried) { if (!weapon && carriedWeapon && carriedWeapon.FindComponent(BaseWeaponComponent)) weapon = carriedWeapon; }
  }
  if (!Check(weapon != null, c.Name + " casualty carries a weapon")) return false;
  Resource partResource = Resource.Load("{AD045AFAFFC1AB6E}Prefabs/Weapons/Attachments/Stocks/Stock_VZ58/Stock_VZ58_folding.et");
  IEntity part = GetGame().SpawnEntityPrefab(partResource, GetGame().GetWorld(), Params(weapon.GetOrigin()));
  if (!Check(part != null, c.Name + " unregistered weapon part spawned")) return false;
  SCR_PhysicsHelper.ChangeSimulationState(part, SimulationState.NONE, true);
  weapon.AddChild(part, -1);
  c.PreparedWeaponId = weapon.GetID(); c.PartId = part.GetID();
  bool partTracked = persistence && !persistence.GetId(part).IsNull();
  int vestStripped = 0;
  if (c.VestStripped) vestStripped = 1;
  int tracked = 0;
  if (partTracked) tracked = 1;
  PrintFormat("[EBG CLEANUP TEST IDENTITYLESS SETUP] case=%1 casualty=%2 vest='%3' vestStripped=%4 weapon='%5' partTracked=%6 partOnWeapon=%7", c.Name, c.Prepared.GetID(), c.VestPrefab, vestStripped, SCR_ResourceNameUtils.GetPrefabName(weapon), tracked, part.GetParent() == weapon);
  return Check(part.GetParent() == weapon && !partTracked, c.Name + " weapon part rides the casualty's weapon without native identity");
 }
 // Every entity the ledger holds for all of the case's casualties.
 void RecordAllOwned(EBGCleanupCase c, EBG_CacheCleanup cleanup)
 {
  array<IEntity> heldEntities = {};
  foreach (EBG_CacheMember casualty : c.Casualties) cleanup.CollectHeldMemberEntities(casualty, heldEntities);
  c.OwnedIds.Clear();
  foreach (IEntity item : heldEntities) { if (item) c.OwnedIds.Insert(item.GetID()); }
 }
 // The native save decision for this record (shared with ExportPersistentGroup), and the
 // real export too when this world gives the zone and group durable identities.
 void ProveSaveProvenance(EBGCleanupCase c, EBG_CacheCleanup cleanup)
 {
  RecordAllOwned(c, cleanup);
  int blocking;
  int riders = cleanup.CountSaveProvenance(c.Record, blocking);
  string reason;
  EBG_MissionGroupData saved = EBG_CacheManager.Get().ExportPersistentGroup(c.Record, reason);
  int durable = 0;
  int exported = 0;
  int issueFree = 0;
  int bodyRows = 0;
  int riderRows = 0;
  // Re-read now: the stripped vest counts only while it still has no native identity.
  IEntity vestEntity = GetGame().GetWorld().FindEntityByID(c.VestId);
  PersistenceSystem persistence = PersistenceSystem.GetInstance();
  bool vestNoId = c.VestStripped && vestEntity && persistence && persistence.GetId(vestEntity).IsNull();
  if (saved)
  {
   durable = 1;
   if (cleanup.ExportPersistentGroup(saved, c.Record, reason)) exported = 1;
   if (saved.Issue == "") issueFree = 1;
   else PrintFormat("[EBG CLEANUP TEST SAVE ISSUE] case=%1 issue='%2'", c.Name, saved.Issue);
   foreach (EBG_MissionObjectData row : saved.Objects)
   {
    EBG_CacheMember owner;
    if (row.MemberIndex >= 0 && row.MemberIndex < c.Record.Members.Count()) owner = c.Record.Members[row.MemberIndex];
    if (row.Corpse && row.Present && owner && c.Casualties.Contains(owner)) bodyRows++;
    string prefab = row.Map.Prefab;
    if (prefab.Contains("Stock_VZ58") || prefab.Contains("Vest_Ratin6B45") || prefab.Contains("RHS_PKP_") || prefab.Contains("RHS_AK74M_") || prefab.Contains("Handguard_AK100")) riderRows++;
    // The stripped vest is a rider too: no row of the casualty may carry its prefab.
    if (vestNoId && !row.Corpse && owner && c.Casualties.Contains(owner) && row.Map.Prefab == c.VestPrefab) riderRows++;
   }
  }
  int vestStripped = 0;
  if (vestNoId) vestStripped = 1;
  if (c.Identityless)
  {
   PrintFormat("[EBG CLEANUP TEST IDENTITYLESS] case=%1 vestStripped=%2 riders=%3 blocking=%4 durable=%5 exported=%6 issueFree=%7 bodyRows=%8 riderRows=%9", c.Name, vestStripped, riders, blocking, durable, exported, issueFree, bodyRows, riderRows);
  }
  else PrintFormat("[EBG CLEANUP TEST RHS] case=%1 available=1 riders=%2 blocking=%3 durable=%4 exported=%5 issueFree=%6 bodyRows=%7 riderRows=%8 owned=%9", c.Name, riders, blocking, durable, exported, issueFree, bodyRows, riderRows, c.OwnedIds.Count());
  Check(riders >= 1 && blocking == 0, c.Name + " identity-less gear is left out of the save and nothing blocks it");
  Check(durable == 0 || (exported == 1 && issueFree == 1 && bodyRows == c.Casualties.Count() && riderRows == 0), c.Name + " native save export keeps the bodies, omits the identity-less gear and carries no provenance Issue");
 }
 void CheckIdentitylessDeleted(EBGCleanupCase c)
 {
  BaseWorld world = GetGame().GetWorld();
  int bodyByEbg = 0;
  if (s_EbgDeleted.Contains(c.BodyIds[0])) bodyByEbg = 1;
  int weaponGone = 0;
  if (!world.FindEntityByID(c.PreparedWeaponId)) weaponGone = 1;
  int partGone = 0;
  if (!world.FindEntityByID(c.PartId)) partGone = 1;
  int vestGone = 0;
  if (!world.FindEntityByID(c.VestId)) vestGone = 1;
  // A dropped weapon is its own outer delete, in the same Tick as the body.
  int sameTick = 1;
  int bodyAt = s_EbgDeleted.Find(c.BodyIds[0]);
  int weaponAt = s_EbgDeleted.Find(c.PreparedWeaponId);
  if (bodyAt < 0 || (weaponAt >= 0 && s_EbgDeletedSerial[weaponAt] != s_EbgDeletedSerial[bodyAt])) sameTick = 0;
  int present = PresentOwned(c);
  int proven = 0;
  if (c.ProvenanceProven && c.OwnedIds.Count() > 1) proven = 1;
  // The part went with its weapon; it was never released as UUID-less player loot.
  int lineage = 0;
  if (EBG_CacheCleanup.Get().HasReleasedLineage(c.Casualties[0])) lineage = 1;
  PrintFormat("[EBG CLEANUP TEST IDENTITYLESS DELETED] case=%1 bodyByEbg=%2 weaponGone=%3 partGone=%4 vestGone=%5 present=%6 sameTick=%7 proven=%8 lineage=%9", c.Name, bodyByEbg, weaponGone, partGone, vestGone, present, sameTick, proven, lineage);
  Check(bodyByEbg == 1 && weaponGone == 1 && partGone == 1 && vestGone == 1 && present == 0 && sameTick == 1 && proven == 1, c.Name + " body, its weapon and the identity-less vest and part removed together by EBG");
  Check(lineage == 0, c.Name + " deleting the identity-less gear records no UUID-less release lineage");
 }
 void CheckRhsDeleted(EBGCleanupCase c)
 {
  int byEbg = 0;
  foreach (EntityID bodyId : c.BodyIds) { if (s_EbgDeleted.Contains(bodyId)) byEbg++; }
  int present = PresentOwned(c);
  int proven = 0;
  if (c.ProvenanceProven && c.OwnedIds.Count() > c.BodyIds.Count()) proven = 1;
  int lineage = 0;
  foreach (EBG_CacheMember casualty : c.Casualties) { if (EBG_CacheCleanup.Get().HasReleasedLineage(casualty)) lineage++; }
  PrintFormat("[EBG CLEANUP TEST RHS DELETED] case=%1 bodies=%2 byEbg=%3 owned=%4 present=%5 proven=%6 lineage=%7", c.Name, c.BodyIds.Count(), byEbg, c.OwnedIds.Count(), present, proven, lineage);
  Check(byEbg == c.BodyIds.Count() && present == 0 && proven == 1, c.Name + " RHS casualties removed whole by EBG with their PKP, AK-74M and 6B45 gear");
  Check(lineage == 0, c.Name + " deleting the RHS weapon parts records no UUID-less release lineage");
 }
 // The cloth whose storage-less LoadoutSlotInfo holds this accessory: the slot is the item's
 // own and belongs to the cloth's BaseLoadoutClothComponent. Independent of production code.
 static IEntity SlotCloth(IEntity accessory)
 {
  if (!accessory) return null;
  InventoryItemComponent item = InventoryItemComponent.Cast(accessory.FindComponent(InventoryItemComponent));
  if (!item) return null;
  InventoryStorageSlot slot = item.GetParentSlot();
  if (!slot || !LoadoutSlotInfo.Cast(slot) || slot.GetStorage() || slot.GetAttachedEntity() != accessory) return null;
  IEntity cloth = slot.GetOwner();
  if (!cloth || cloth == accessory || !cloth.FindComponent(BaseLoadoutClothComponent) || slot.GetParentContainer() != cloth.FindComponent(BaseLoadoutClothComponent)) return null;
  return cloth;
 }
 // Every such accessory in the character's hierarchy (bounded).
 static void FindSlotAccessories(IEntity parent, array<IEntity> found, int depth)
 {
  if (!parent || depth > 4 || found.Count() > 64) return;
  for (IEntity child = parent.GetChildren(); child; child = child.GetSibling())
  {
   if (SlotCloth(child) && !found.Contains(child)) found.Insert(child);
   FindSlotAccessories(child, found, depth + 1);
  }
 }
 // Vanilla stand-in, before enrollment: the first non-leader wearing a cloth with a
 // storage-less loadout-slot accessory becomes the casualty and that cloth is unlisted.
 bool PrepareUnlistedCloth(EBGCleanupCase c)
 {
  IEntity leader = c.Group.GetLeaderEntity();
  array<AIAgent> agents = {};
  c.Group.GetAgents(agents);
  IEntity chosenCloth;
  IEntity chosenAccessory;
  foreach (AIAgent agent : agents)
  {
   SCR_ChimeraCharacter candidate = SCR_ChimeraCharacter.Cast(agent.GetControlledEntity());
   if (c.Prepared || !candidate || candidate == leader) continue;
   array<IEntity> found = {};
   FindSlotAccessories(candidate, found, 0);
   if (found.IsEmpty()) continue;
   c.Prepared = candidate; chosenAccessory = found[0]; chosenCloth = SlotCloth(found[0]);
  }
  if (!Check(c.Prepared != null && chosenCloth != null, c.Name + " non-leader wears a cloth with a storage-less loadout-slot accessory")) return false;
  s_UnlistedCloths.Insert(chosenCloth);
  PrintFormat("[EBG CLEANUP TEST CLOTH SLOT SETUP] case=%1 casualty=%2 cloth='%3' accessory='%4'", c.Name, c.Prepared.GetID(), SCR_ResourceNameUtils.GetPrefabName(chosenCloth), SCR_ResourceNameUtils.GetPrefabName(chosenAccessory));
  return true;
 }
 // After enrollment: the casualties' accessories the native vest path does not resolve.
 bool CollectSlotAccessories(EBGCleanupCase c, EBG_CacheCleanup cleanup)
 {
  foreach (SCR_ChimeraCharacter wearer : c.Bodies)
  {
   array<IEntity> found = {};
   FindSlotAccessories(wearer, found, 0);
   foreach (IEntity accessory : found)
   {
    if (!cleanup.EXPG_NativeVestMisses(accessory)) continue;
    c.AccessoryIds.Insert(accessory.GetID());
    c.AccessoryWearerIds.Insert(wearer.GetID());
    IEntity cloth = SlotCloth(accessory);
    EntityID clothId = cloth.GetID();
    if (!c.AccessoryClothIds.Contains(clothId)) c.AccessoryClothIds.Insert(clothId);
   }
  }
  PrintFormat("[EBG CLEANUP TEST CLOTH SLOT ENROLLED] case=%1 casualties=%2 accessories=%3 cloths=%4", c.Name, c.Casualties.Count(), c.AccessoryIds.Count(), c.AccessoryClothIds.Count());
  return Check(c.Casualties.Count() > 0 && c.AccessoryIds.Count() >= c.Casualties.Count(), c.Name + " every casualty wears a storage-less cloth-slot accessory the native vest path does not resolve");
 }
 // Just before eligibility, after a transfer check (which released each accessory as an
 // ownership transfer with holder NULL before the fix): held, owned by its wearer, unprotected.
 void ProveSlotAccessories(EBGCleanupCase c, EBG_CacheCleanup cleanup)
 {
  cleanup.CheckGroupTransfers(c.Record);
  RecordAllOwned(c, cleanup);
  BaseWorld world = GetGame().GetWorld();
  int unlisted = 0;
  int held = 0;
  int wearerHolder = 0;
  int protectedChain = 0;
  for (int i = 0; i < c.AccessoryIds.Count(); i++)
  {
   IEntity accessory = world.FindEntityByID(c.AccessoryIds[i]);
   IEntity wearer = world.FindEntityByID(c.AccessoryWearerIds[i]);
   if (!accessory || !wearer) continue;
   if (cleanup.EXPG_NativeVestMisses(accessory)) unlisted++;
   if (cleanup.IsHeld(accessory)) held++;
   if (cleanup.EXPG_Holder(accessory) == wearer) wearerHolder++;
   if (cleanup.EXPG_ProtectedChain(accessory)) protectedChain++;
  }
  int count = c.AccessoryIds.Count();
  // Now held, each accessory enters the native save: its cloth-slot provenance must resolve,
  // or the save carries an Issue and the whole group is blocked on the next load.
  int blocking;
  cleanup.CountSaveProvenance(c.Record, blocking);
  PrintFormat("[EBG CLEANUP TEST CLOTH SLOT] case=%1 available=1 accessories=%2 unlisted=%3 held=%4 wearerHolder=%5 protectedChain=%6 blocking=%7 owned=%8", c.Name, count, unlisted, held, wearerHolder, protectedChain, blocking, c.OwnedIds.Count());
  Check(count > 0 && unlisted == count && held == count && wearerHolder == count && protectedChain == 0, c.Name + " storage-less cloth-slot accessories stay held, owned by their wearer and unprotected");
  Check(blocking == 0, c.Name + " held cloth-slot accessories add no save provenance Issue");
 }
 void CheckSlotAccessoriesDeleted(EBGCleanupCase c)
 {
  BaseWorld world = GetGame().GetWorld();
  int byEbg = 0;
  foreach (EntityID bodyId : c.BodyIds) { if (s_EbgDeleted.Contains(bodyId)) byEbg++; }
  int accessoriesGone = 1;
  foreach (EntityID accessoryId : c.AccessoryIds) { if (world.FindEntityByID(accessoryId)) accessoriesGone = 0; }
  int clothGone = 1;
  foreach (EntityID clothId : c.AccessoryClothIds) { if (world.FindEntityByID(clothId)) clothGone = 0; }
  int present = PresentOwned(c);
  int proven = 0;
  if (c.ClothSlotProven && c.OwnedIds.Count() > c.BodyIds.Count()) proven = 1;
  int lineage = 0;
  foreach (EBG_CacheMember casualty : c.Casualties) { if (EBG_CacheCleanup.Get().HasReleasedLineage(casualty)) lineage++; }
  PrintFormat("[EBG CLEANUP TEST CLOTH SLOT DELETED] case=%1 bodies=%2 byEbg=%3 accessoriesGone=%4 clothGone=%5 present=%6 proven=%7 lineage=%8", c.Name, c.BodyIds.Count(), byEbg, accessoriesGone, clothGone, present, proven, lineage);
  Check(byEbg == c.BodyIds.Count() && accessoriesGone == 1 && clothGone == 1 && present == 0 && proven == 1, c.Name + " casualties removed whole by EBG with their cloth and its storage-less slot accessory");
  Check(lineage == 0, c.Name + " the cloth-slot accessory records no UUID-less release lineage");
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
  s_EbgDeletedSerial.Insert(s_TickSerial);
  SCR_ChimeraCharacter wearer = Wearer(entity);
  int living = 0;
  if (wearer && wearer.GetCharacterController() && !wearer.GetCharacterController().IsDead()) living = 1;
  if (wearer && s_Survivors.Contains(wearer.GetID())) living = 1;
  PrintFormat("[EBG CLEANUP TEST NATIVE DELETE] id=%1 state=%2 serial=%3 prefab='%4' livingWearer=%5", entity.GetID(), s_TickState, s_TickSerial, SCR_ResourceNameUtils.GetPrefabName(entity), living);
  // An item taken off a still-present casualty body as its own delete is a partial strip.
  if (living == 0 && wearer && wearer != entity)
  {
   s_PartialStrips++;
   PrintFormat("[EBG CLEANUP TEST PARTIAL STRIP] id=%1 wearer=%2 prefab='%3'", entity.GetID(), wearer.GetID(), SCR_ResourceNameUtils.GetPrefabName(entity));
  }
  if (living == 0) return;
  s_SurvivorDeletes++;
  PrintFormat("[EBG CLEANUP TEST SURVIVOR DELETE] id=%1 wearer=%2", entity.GetID(), wearer.GetID());
 }
}
// Presence seam, as in the optimizer fixtures (EBGRecacheTest.c:95-102), plus the possession
// probe's player seam and counters. Each override calls super.
modded class EBG_CacheManager
{
 override protected void UpdatePlayers()
 {
  int calls = EXPG_GarrisonGameplay.s_PossessionCalls;
  super.UpdatePlayers();
  EXPG_GarrisonGameplay.s_PassSerial++;
  if (EXPG_GarrisonGameplay.s_PassSerial == EXPG_GarrisonGameplay.s_WatchPass) EXPG_GarrisonGameplay.s_WatchCalls = EXPG_GarrisonGameplay.s_PossessionCalls - calls;
  if (EXPG_GarrisonGameplay.s_Presence)
  {
   foreach (vector presence : EXPG_GarrisonGameplay.s_Presence) Players.Insert(presence);
  }
 }
 override protected void EBG_CollectPlayers(notnull array<int> ids)
 {
  super.EBG_CollectPlayers(ids);
  if (!EXPG_GarrisonGameplay.s_StandIns) return;
  for (int i = 0; i < EXPG_GarrisonGameplay.s_StandIns.Count(); i++) ids.Insert(EXPG_GarrisonGameplay.STAND_IN_ID + i);
 }
 override protected IEntity EBG_PlayerEntity(int id)
 {
  int at = id - EXPG_GarrisonGameplay.STAND_IN_ID;
  if (EXPG_GarrisonGameplay.s_StandIns && at >= 0 && at < EXPG_GarrisonGameplay.s_StandIns.Count())
  {
   return EXPG_GarrisonGameplay.s_StandIns[at];
  }
  return super.EBG_PlayerEntity(id);
 }
 override void PlayerPossession(IEntity entity)
 {
  EXPG_GarrisonGameplay.s_PossessionCalls++;
  super.PlayerPossession(entity);
 }
 override protected void CheckPossession(SCR_ChimeraCharacter player)
 {
  EXPG_GarrisonGameplay.s_SkippedChecks++;
  super.CheckPossession(player);
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
  EXPG_GarrisonGameplay.s_TickSerial++;
  EXPG_GarrisonGameplay.s_InCleanupTick = true;
  super.Tick(record, players, now, transfersChecked);
  EXPG_GarrisonGameplay.s_InCleanupTick = false;
 }
 // Test seam: a stock cloth stands in for an unlisted modded cloth (the RHS USMC boonie).
 override protected IEntity NativeVestAccessoryOwner(IEntity item, InventoryStorageSlot slot)
 {
  if (slot && EXPG_GarrisonGameplay.s_UnlistedCloths && EXPG_GarrisonGameplay.s_UnlistedCloths.Contains(slot.GetOwner())) return null;
  return super.NativeVestAccessoryOwner(item, slot);
 }
 // Read-only views of the production ownership walk.
 IEntity EXPG_Holder(IEntity item) { return Holder(item); }
 // Held rows of one member (possession probe).
 int EXPG_HeldMemberRows(EBG_CacheMember member)
 {
  int count;
  foreach (EBG_CleanupObject object : m_Objects)
  {
   if (object.Member == member && object.Held) count++;
  }
  return count;
 }
 bool EXPG_ProtectedChain(IEntity item) { return ProtectedOwnerChain(item); }
 // A storage-less slot item the allow-listed native vest path does not resolve.
 bool EXPG_NativeVestMisses(IEntity item)
 {
  if (!item) return false;
  InventoryItemComponent inventory = InventoryItemComponent.Cast(item.FindComponent(InventoryItemComponent));
  if (!inventory || !inventory.GetParentSlot() || inventory.GetParentSlot().GetStorage()) return false;
  return NativeVestAccessoryOwner(item, inventory.GetParentSlot()) == null;
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

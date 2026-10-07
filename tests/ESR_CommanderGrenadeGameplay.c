// TEST ONLY. EXPBG AI Surrender: a broken squad's leader sets a grenade live at his own
// feet instead of surrendering (dedicated server, no players, no GM UI).
// pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/ESR_CommanderGrenadeGameplay.c -TimeoutSeconds 300 -OrchestratorSlotGranted -ExpectResult '\[ESR GRENADE RESULT\] checks=[1-9]\d* failures=0 own=1 spawned=1 mustCarry=1 control=1 reason=complete'
// The runner copies this file to EXPG_GarrisonGameplay.c; the class names are fixed. Judge
// the run by its one [ESR GRENADE RESULT] line (-ExpectResult also rejects script errors).
// Real module prefab and public settings, four native USSR rifle squads far apart, native
// Kill of two non-leaders per squad (2 of 6 = 33% >= threshold 10%; surrender 100%, random
// 0), production evaluation, surrender and commander grenade. The vanilla fragmentation
// grenade explodes by itself; nothing here damages the leader.
// Settings: prefab defaults (grenade 0%, must carry ON), clamp, and save compatibility
// (a ten-value set from before the commander settings restores with their defaults).
//  own        grenade 100%, must carry ON, the leader carries a frag (one is put into his
//             inventory when his loadout has none): he does not surrender and stays in his
//             squad with his AI off; one of his own grenades leaves his inventory and a
//             timed vanilla frag lies within 1 m of him and goes live; after the fuse he is
//             dead or unconscious; the other three able soldiers surrendered; the record
//             ends with "blast".
//  spawned    must carry OFF, his frags removed: a vanilla RGD-5 (USSR side) is placed live
//             within 1 m of him; he is down after the fuse.
//  mustCarry  must carry ON, his frags removed: he surrenders like the others.
//  control    grenade 0%: the leader surrenders as before; no commander record is left.
class EXPG_GarrisonGameplayClass : GenericEntityClass {}
class EXPG_GarrisonGameplay : GenericEntity
{
 static const float FIXTURE_SECONDS = 240;
 static const ResourceName MODULE = "{7F2E668385984EA1}PrefabsEditable/EXPSR/ESR_SurrenderModule.et";
 static const ResourceName SQUAD = "{E552DABF3636C2AD}Prefabs/Groups/OPFOR/Group_USSR_RifleSquad.et";
 static const int SIZE = 6;
 static const int CASE_OWN = 0;
 static const int CASE_SPAWNED = 1;
 static const int CASE_MUST = 2;
 static const int CASE_CONTROL = 3;
 static const int CASES = 4;
 vector Origin = "4773.46 0 7094.57";
 ESR_SurrenderModule Surrender;
 ref array<SCR_AIGroup> Squads = {};
 ref array<SCR_ChimeraCharacter> Leaders = {};
 // Every soldier as spawned and his case, to count prisoners after an emptied squad
 // deleted itself.
 ref array<SCR_ChimeraCharacter> Members = {};
 ref array<int> MemberCase = {};
 // Strong references: the records stay readable after the manager ends them.
 ref ESR_Commander OwnRecord;
 ref ESR_Commander SpawnedRecord;
 int OwnFragsBefore = -1;
 float TimerAtLive = -1;
 int Phase;
 int Checks;
 int Failures;
 int CaseStart;
 bool OwnPass;
 bool SpawnedPass;
 bool MustPass;
 bool ControlPass;
 float Started;
 float PhaseAt;
 float Next;
 bool Finished;

 void EXPG_GarrisonGameplay(IEntitySource src, IEntity parent) { SetEventMask(EntityEvent.INIT | EntityEvent.FRAME); }

 float Now()
 {
  return GetGame().GetWorld().GetWorldTime() * 0.001;
 }

 override void EOnInit(IEntity owner)
 {
  if (!Replication.IsServer()) { ClearEventMask(EntityEvent.FRAME); return; }
  Started = Now(); Next = Started + 10; PhaseAt = Started;
  PrintFormat("[ESR GRENADE BEGIN] origin=%1 cases=%2 deadline=%3", Origin, CASES, FIXTURE_SECONDS);
 }

 bool Check(bool ok, string label)
 {
  Checks++;
  if (!ok) Failures++;
  PrintFormat("[ESR GRENADE CHECK] pass=%1 %2", ok, label);
  return ok;
 }

 void Finish(string reason)
 {
  if (Finished) return;
  Finished = true;
  ClearEventMask(EntityEvent.FRAME);
  PrintFormat("[ESR GRENADE RESULT] checks=%1 failures=%2 own=%3 spawned=%4 mustCarry=%5 control=%6 reason=%7", Checks, Failures, ESR_Settings.FromBool(OwnPass), ESR_Settings.FromBool(SpawnedPass), ESR_Settings.FromBool(MustPass), ESR_Settings.FromBool(ControlPass), reason);
  GetGame().RequestClose();
 }

 vector Ground(vector p, float lift)
 {
  p[1] = GetGame().GetWorld().GetSurfaceY(p[0], p[2]) + lift;
  return p;
 }

 EntitySpawnParams Params(vector p)
 {
  EntitySpawnParams spawn = new EntitySpawnParams();
  spawn.TransformMode = ETransformMode.WORLD;
  Math3D.AnglesToMatrix(vector.Zero, spawn.Transform);
  spawn.Transform[3] = Ground(p, 0.3);
  return spawn;
 }

 // Dry land the other AI Surrender fixtures use, at least 140 m apart.
 vector CaseOffset(int index)
 {
  if (index == CASE_OWN)
   return Vector(0, 0, 20);
  if (index == CASE_SPAWNED)
   return Vector(-600, 0, -600);
  if (index == CASE_MUST)
   return Vector(200, 0, 200);
  return Vector(300, 0, 300);
 }

 SCR_AIGroup SpawnSquad(vector p)
 {
  // Keep the Resource and the spawned entity in locals before casting.
  Resource squad = Resource.Load(SQUAD);
  IEntity spawned = GetGame().SpawnEntityPrefab(squad, GetGame().GetWorld(), Params(p));
  return SCR_AIGroup.Cast(spawned);
 }

 void Configure(int grenade, bool mustCarry)
 {
  Surrender.ApplySetting(ESR_Settings.ENABLED, 1);
  Surrender.ApplySetting(ESR_Settings.CHANCE, 100);
  Surrender.ApplySetting(ESR_Settings.THRESHOLD, 10);
  Surrender.ApplySetting(ESR_Settings.RANDOM, 0);
  Surrender.ApplySetting(ESR_Settings.REVEAL, 0);
  Surrender.ApplySetting(ESR_Settings.IDENTITY, 0);
  Surrender.ApplySetting(ESR_Settings.RADIUS, 1000);
  Surrender.ApplySetting(ESR_Settings.ATTEMPTS, 3);
  Surrender.ApplySetting(ESR_Settings.LIFETIME, 0);
  Surrender.ApplySetting(ESR_Settings.DIAGNOSTICS, 1);
  Surrender.ApplySetting(ESR_Settings.GRENADE, grenade);
  Surrender.ApplySetting(ESR_Settings.GRENADE_CARRY, ESR_Settings.FromBool(mustCarry));
 }

 // Fragmentation grenades he carries, read here with the vanilla item query.
 int Frags(IEntity character, notnull array<IEntity> found)
 {
  found.Clear();
  if (!character)
   return -1;
  InventoryStorageManagerComponent inventory = InventoryStorageManagerComponent.Cast(character.FindComponent(InventoryStorageManagerComponent));
  if (!inventory)
   return -1;
  array<IEntity> items = {};
  array<typename> query = {};
  query.Insert(BaseWeaponComponent);
  inventory.FindItemsWithComponents(items, query, EStoragePurpose.PURPOSE_ANY);
  foreach (IEntity item : items)
  {
   if (!item) continue;
   BaseWeaponComponent weapon = BaseWeaponComponent.Cast(item.FindComponent(BaseWeaponComponent));
   if (weapon && weapon.GetWeaponType() == EWeaponType.WT_FRAGGRENADE) found.Insert(item);
  }
  return found.Count();
 }

 int StripFrags(IEntity character)
 {
  array<IEntity> found = {};
  Frags(character, found);
  InventoryStorageManagerComponent inventory;
  if (character) inventory = InventoryStorageManagerComponent.Cast(character.FindComponent(InventoryStorageManagerComponent));
  int removed = 0;
  foreach (IEntity frag : found)
  {
   if (inventory && inventory.TryDeleteItem(frag)) removed++;
  }
  return removed;
 }

 // Two members who are not the leader, through the native damage manager.
 int KillTwo(int index)
 {
  int killed = 0;
  foreach (int slot, SCR_ChimeraCharacter member : Members)
  {
   if (killed >= 2 || MemberCase[slot] != index || !member || member == Leaders[index]) continue;
   SCR_CharacterDamageManagerComponent damage = SCR_CharacterDamageManagerComponent.Cast(member.GetDamageManager());
   if (!damage) continue;
   damage.Kill(Instigator.CreateInstigator(null));
   killed++;
  }
  PrintFormat("[ESR GRENADE KILLED] case=%1 casualties=%2", index, killed);
  return killed;
 }

 int PrisonersOf(int index)
 {
  int taken = 0;
  foreach (int slot, SCR_ChimeraCharacter member : Members)
  {
   if (MemberCase[slot] == index && member && ESR_SurrenderManager.FindPrisoner(member)) taken++;
  }
  return taken;
 }

 bool InSquad(SCR_AIGroup squad, IEntity character)
 {
  if (!squad || !character)
   return false;
  array<AIAgent> agents = {};
  squad.GetAgents(agents);
  foreach (AIAgent agent : agents)
  {
   if (agent && agent.GetControlledEntity() == character) return true;
  }
  return false;
 }

 bool AiOff(IEntity character)
 {
  if (!character)
   return false;
  AIControlComponent control = AIControlComponent.Cast(character.FindComponent(AIControlComponent));
  return control && !control.IsAIActivated();
 }

 // Dead, unconscious, or his body already gone.
 bool Down(SCR_ChimeraCharacter character)
 {
  if (!character)
   return true;
  CharacterControllerComponent controller = character.GetCharacterController();
  if (!controller)
   return true;
  return controller.GetLifeState() != ECharacterLifeState.ALIVE || controller.IsUnconscious();
 }

 float GrenadeDistance(ESR_Commander record)
 {
  if (!record || !record.Grenade || !record.Character)
   return -1;
  return vector.Distance(record.Grenade.GetOrigin(), record.Character.GetOrigin());
 }

 string PrefabName(IEntity entity)
 {
  if (!entity || !entity.GetPrefabData())
   return string.Empty;
  return entity.GetPrefabData().GetPrefabName();
 }

 override void EOnFrame(IEntity owner, float timeSlice)
 {
  if (Finished || Now() < Next) return;
  Next = Now() + 0.5;
  if (Now() - Started > FIXTURE_SECONDS) { Check(false, string.Format("phase %1 finished before the fixture deadline", Phase)); Finish("timeout"); return; }
  Step();
 }

 void Step()
 {
  if (Phase == 0) { Setup(); return; }
  if (Phase == 1) { PrepareLeaders(); return; }
  if (Phase == 2) { StartOwn(); return; }
  if (Phase == 3) { OwnDecided(); return; }
  if (Phase == 4) { OwnPlaced(); return; }
  if (Phase == 5) { OwnLive(); return; }
  if (Phase == 6) { OwnDown(); return; }
  if (Phase == 7) { OwnEnded(); return; }
  if (Phase == 8) { SpawnedLive(); return; }
  if (Phase == 9) { SpawnedDown(); return; }
  if (Phase == 10) { MustCarrySurrendered(); return; }
  if (Phase == 11) ControlSurrendered();
 }

 void NextPhase()
 {
  Phase++;
  PhaseAt = Now();
 }

 void Setup()
 {
  if (!Check(GetGame().GetPlayerManager().GetPlayerCount() == 0 && ESR_SurrenderManager.PrisonerCount() == 0 && ESR_SurrenderManager.CommanderCount() == 0, "isolated server: no players, prisoners or commanders")) { Finish("setup"); return; }
  Resource modulePrefab = Resource.Load(MODULE);
  IEntity moduleEntity = GetGame().SpawnEntityPrefab(modulePrefab, GetGame().GetWorld(), Params(Origin));
  Surrender = ESR_SurrenderModule.Cast(moduleEntity);
  if (!Check(Surrender != null, "surrender module prefab spawned")) { Finish("setup"); return; }
  Check(Surrender.GetSetting(ESR_Settings.GRENADE) == 0 && Surrender.GetSetting(ESR_Settings.GRENADE_CARRY) == 1 && ESR_Settings.Get(ESR_Settings.GRENADE) == 0 && ESR_Settings.Get(ESR_Settings.GRENADE_CARRY) == 1, "prefab defaults: commander grenade 0%, must carry ON");
  Check(ESR_Settings.COUNT_V2 == 12 && ESR_Settings.COUNT_V1 == 10 && ESR_Settings.IsBoolean(ESR_Settings.GRENADE_CARRY) && !ESR_Settings.IsBoolean(ESR_Settings.GRENADE), "twelve settings in an esrVersion 2 save; must carry is a switch");
  // A save from before the commander settings: the first ten values only.
  array<int> saved = {};
  for (int key = 0; key < ESR_Settings.COUNT_V1; key++) saved.Insert(ESR_Settings.Default(key));
  saved[ESR_Settings.CHANCE] = 55;
  Surrender.ApplySetting(ESR_Settings.GRENADE, 35);
  bool restoredOld = Surrender.RestoreSettings(saved);
  Check(restoredOld && ESR_Settings.Get(ESR_Settings.CHANCE) == 55 && ESR_Settings.Get(ESR_Settings.GRENADE) == 0 && ESR_Settings.Get(ESR_Settings.GRENADE_CARRY) == 1, "a ten-value save restores with the commander defaults");
  saved.Insert(40);
  saved.Insert(0);
  bool restoredNew = Surrender.RestoreSettings(saved);
  Check(restoredNew && Surrender.GetSetting(ESR_Settings.GRENADE) == 40 && Surrender.GetSetting(ESR_Settings.GRENADE_CARRY) == 0, "a twelve-value save restores and mirrors the commander settings");
  // More values than settings (the intel setting made it thirteen): refused.
  while (saved.Count() <= ESR_Settings.COUNT) saved.Insert(1);
  Check(!Surrender.RestoreSettings(saved) && ESR_Settings.Get(ESR_Settings.GRENADE) == 40, "a save with more values than settings is refused");
  Surrender.ApplySetting(ESR_Settings.GRENADE, 150);
  Check(ESR_Settings.Get(ESR_Settings.GRENADE) == 100, "commander grenade chance clamped to 100");
  Configure(0, true);
  for (int index = 0; index < CASES; index++) Squads.Insert(SpawnSquad(Origin + CaseOffset(index)));
  bool spawned = Squads.Count() == CASES;
  foreach (SCR_AIGroup squad : Squads)
  {
   if (!squad) spawned = false;
  }
  if (!Check(spawned, "four native USSR squads spawned")) { Finish("setup"); return; }
  NextPhase();
 }

 void PrepareLeaders()
 {
  bool ready = true;
  foreach (SCR_AIGroup squad : Squads)
  {
   if (!squad || squad.GetAgentsCount() != SIZE || !squad.GetLeaderEntity()) ready = false;
  }
  if (!ready)
  {
   if (Now() - PhaseAt > 30) { Check(false, "four squads completed their initial spawn with a leader"); Finish("setup"); }
   return;
  }
  foreach (int index, SCR_AIGroup group : Squads)
  {
   Leaders.Insert(SCR_ChimeraCharacter.Cast(group.GetLeaderEntity()));
   array<AIAgent> agents = {};
   group.GetAgents(agents);
   foreach (AIAgent agent : agents)
   {
    SCR_ChimeraCharacter member = SCR_ChimeraCharacter.Cast(agent.GetControlledEntity());
    if (!member) continue;
    Members.Insert(member);
    MemberCase.Insert(index);
   }
  }
  // own: the leader needs a frag of his own; spawned and mustCarry: none.
  array<IEntity> ownFrags = {};
  bool given;
  if (Frags(Leaders[CASE_OWN], ownFrags) == 0 && Leaders[CASE_OWN])
  {
   InventoryStorageManagerComponent inventory = InventoryStorageManagerComponent.Cast(Leaders[CASE_OWN].FindComponent(InventoryStorageManagerComponent));
   if (inventory) given = inventory.TrySpawnPrefabToStorage(ESR_SurrenderManager.GRENADE_RGD5);
  }
  int strippedSpawned = StripFrags(Leaders[CASE_SPAWNED]);
  int strippedMust = StripFrags(Leaders[CASE_MUST]);
  PrintFormat("[ESR GRENADE SETUP] members=%1 ownGiven=%2 strippedSpawned=%3 strippedMust=%4", Members.Count(), given, strippedSpawned, strippedMust);
  NextPhase();
 }

 void StartOwn()
 {
  // Inventory operations settle first.
  if (Now() - PhaseAt < 1.5) return;
  bool leaders = Leaders.Count() == CASES;
  foreach (SCR_ChimeraCharacter leader : Leaders)
  {
   if (!leader) leaders = false;
  }
  if (!Check(leaders && Members.Count() == CASES * SIZE, "four squad leaders and 24 soldiers recorded")) { Finish("setup"); return; }
  array<IEntity> found = {};
  OwnFragsBefore = Frags(Leaders[CASE_OWN], found);
  int spawnedFrags = Frags(Leaders[CASE_SPAWNED], found);
  int mustFrags = Frags(Leaders[CASE_MUST], found);
  PrintFormat("[ESR GRENADE SETUP] frags own=%1 spawned=%2 mustCarry=%3", OwnFragsBefore, spawnedFrags, mustFrags);
  if (!Check(OwnFragsBefore >= 1, "own case: the leader carries a fragmentation grenade")) { Finish("setup"); return; }
  if (!Check(spawnedFrags == 0 && mustFrags == 0, "spawned and must-carry cases: the leaders carry no fragmentation grenade")) { Finish("setup"); return; }
  CaseStart = Failures;
  Configure(100, true);
  KillTwo(CASE_OWN);
  NextPhase();
 }

 void OwnDecided()
 {
  SCR_ChimeraCharacter leader = Leaders[CASE_OWN];
  ESR_Commander record = ESR_SurrenderManager.FindCommander(leader);
  int taken = PrisonersOf(CASE_OWN);
  if (!record || taken < SIZE - 3)
  {
   if (Now() - PhaseAt > 10)
   {
    PrintFormat("[ESR GRENADE OWN] record=%1 prisoners=%2 leaderPrisoner=%3", record != null, taken, ESR_SurrenderManager.FindPrisoner(leader) != null);
    Check(false, "own case: the leader took a grenade and his three able men surrendered");
    Finish("own");
   }
   return;
  }
  OwnRecord = record;
  PrintFormat("[ESR GRENADE OWN] decided prisoners=%1 inSquad=%2 aiOff=%3 own=%4 mustCarry=%5 prefab=%6", taken, InSquad(Squads[CASE_OWN], leader), AiOff(leader), record.OwnGrenade, record.MustCarry, record.Prefab);
  Check(ESR_SurrenderManager.FindPrisoner(leader) == null, "own case: the leader did not surrender");
  Check(taken == SIZE - 3, "own case: the other three able soldiers surrendered");
  Check(InSquad(Squads[CASE_OWN], leader) && AiOff(leader), "own case: the leader stays in his squad with his AI off");
  Check(record.OwnGrenade && record.MustCarry && !record.Live, "own case: decided with his own grenade, not live yet");
  NextPhase();
 }

 void OwnPlaced()
 {
  if (!OwnRecord.Grenade)
  {
   if (!OwnRecord.Ending.IsEmpty() || Now() - PhaseAt > ESR_SurrenderManager.GRENADE_PREP_MAX + 2)
   {
    PrintFormat("[ESR GRENADE OWN] not placed ending='%1'", OwnRecord.Ending);
    Check(false, "own case: the grenade was placed");
    Finish("own");
   }
   return;
  }
  float distance = GrenadeDistance(OwnRecord);
  array<IEntity> after = {};
  int fragsAfter = Frags(Leaders[CASE_OWN], after);
  string prefab = PrefabName(OwnRecord.Grenade);
  bool timed = OwnRecord.Grenade.FindComponent(TimerTriggerComponent) != null;
  PrintFormat("[ESR GRENADE OWN] placed distance=%1 prefab=%2 timer=%3 fragsBefore=%4 fragsAfter=%5 own=%6 after=%7", distance, prefab, timed, OwnFragsBefore, fragsAfter, OwnRecord.OwnGrenade, Now() - PhaseAt);
  Check(distance >= 0 && distance <= 1, "own case: the grenade lies within 1 m of the leader");
  Check(timed && prefab == OwnRecord.Prefab && ESR_SurrenderManager.IsFragGrenade(OwnRecord.Grenade), "own case: a timed vanilla fragmentation grenade of his own prefab");
  Check(OwnRecord.OwnGrenade && fragsAfter == OwnFragsBefore - 1, "own case: exactly one grenade left his inventory");
  NextPhase();
 }

 void OwnLive()
 {
  if (!OwnRecord.Live)
  {
   if (!OwnRecord.Ending.IsEmpty() || Now() - PhaseAt > 3) { Check(false, "own case: the grenade went live"); Finish("own"); }
   return;
  }
  TimerTriggerComponent timer;
  if (OwnRecord.Grenade) timer = TimerTriggerComponent.Cast(OwnRecord.Grenade.FindComponent(TimerTriggerComponent));
  if (timer) TimerAtLive = timer.GetTimer();
  PrintFormat("[ESR GRENADE OWN] live fuse=%1 timer=%2", OwnRecord.Fuse, TimerAtLive);
  Check(OwnRecord.Fuse >= 1 && OwnRecord.Fuse <= 15, "own case: live with a vanilla fuse");
  NextPhase();
 }

 void OwnDown()
 {
  SCR_ChimeraCharacter leader = Leaders[CASE_OWN];
  if (!Down(leader))
  {
   if (Now() - PhaseAt > OwnRecord.Fuse + 6)
   {
    PrintFormat("[ESR GRENADE OWN] still standing after=%1 grenadeLeft=%2", Now() - PhaseAt, OwnRecord.Grenade != null);
    Check(false, "own case: the leader is dead or unconscious after the fuse");
    Finish("own");
   }
   return;
  }
  bool dead = !leader || leader.GetCharacterController().GetLifeState() == ECharacterLifeState.DEAD;
  PrintFormat("[ESR GRENADE OWN] down after=%1 fuse=%2 timerAtLive=%3 grenadeLeft=%4 dead=%5 prisonersLeft=%6", Now() - PhaseAt, OwnRecord.Fuse, TimerAtLive, OwnRecord.Grenade != null, dead, PrisonersOf(CASE_OWN));
  Check(true, "own case: the leader is dead or unconscious after the fuse");
  NextPhase();
 }

 void OwnEnded()
 {
  if (OwnRecord.Ending.IsEmpty())
  {
   if (Now() - PhaseAt > ESR_SurrenderManager.GRENADE_AFTER_BLAST + 4) { Check(false, "own case: the record ended after the blast"); Finish("own"); }
   return;
  }
  Check(OwnRecord.Ending == "blast" && !ESR_SurrenderManager.FindCommander(Leaders[CASE_OWN]), "own case: the record ended after the blast");
  OwnPass = Failures == CaseStart;
  CaseStart = Failures;
  Configure(100, false);
  KillTwo(CASE_SPAWNED);
  NextPhase();
 }

 void SpawnedLive()
 {
  SCR_ChimeraCharacter leader = Leaders[CASE_SPAWNED];
  if (!SpawnedRecord) SpawnedRecord = ESR_SurrenderManager.FindCommander(leader);
  if (!SpawnedRecord || !SpawnedRecord.Live)
  {
   if (Now() - PhaseAt > 10)
   {
    PrintFormat("[ESR GRENADE SPAWNED] record=%1 prisoners=%2 leaderPrisoner=%3", SpawnedRecord != null, PrisonersOf(CASE_SPAWNED), ESR_SurrenderManager.FindPrisoner(leader) != null);
    Check(false, "spawned case: the leader set a grenade live");
    Finish("spawned");
   }
   return;
  }
  string prefab = PrefabName(SpawnedRecord.Grenade);
  float distance = GrenadeDistance(SpawnedRecord);
  int taken = PrisonersOf(CASE_SPAWNED);
  PrintFormat("[ESR GRENADE SPAWNED] live distance=%1 prefab=%2 own=%3 mustCarry=%4 prisoners=%5 fuse=%6", distance, prefab, SpawnedRecord.OwnGrenade, SpawnedRecord.MustCarry, taken, SpawnedRecord.Fuse);
  Check(!SpawnedRecord.OwnGrenade && !SpawnedRecord.MustCarry && prefab.Contains("Grenade_RGD5"), "spawned case: a vanilla RGD-5 for his USSR side");
  Check(distance >= 0 && distance <= 1, "spawned case: the grenade lies within 1 m of the leader");
  Check(ESR_SurrenderManager.FindPrisoner(leader) == null && taken == SIZE - 3, "spawned case: the leader did not surrender; the other three did");
  NextPhase();
 }

 void SpawnedDown()
 {
  SCR_ChimeraCharacter leader = Leaders[CASE_SPAWNED];
  if (!Down(leader))
  {
   if (Now() - PhaseAt > SpawnedRecord.Fuse + 6) { Check(false, "spawned case: the leader is dead or unconscious after the fuse"); Finish("spawned"); }
   return;
  }
  PrintFormat("[ESR GRENADE SPAWNED] down after=%1 grenadeLeft=%2", Now() - PhaseAt, SpawnedRecord.Grenade != null);
  Check(true, "spawned case: the leader is dead or unconscious after the fuse");
  SpawnedPass = Failures == CaseStart;
  CaseStart = Failures;
  Configure(100, true);
  KillTwo(CASE_MUST);
  NextPhase();
 }

 void MustCarrySurrendered()
 {
  SCR_ChimeraCharacter leader = Leaders[CASE_MUST];
  int taken = PrisonersOf(CASE_MUST);
  if (taken < SIZE - 2)
  {
   if (Now() - PhaseAt > 10)
   {
    PrintFormat("[ESR GRENADE MUSTCARRY] prisoners=%1 leaderPrisoner=%2 record=%3", taken, ESR_SurrenderManager.FindPrisoner(leader) != null, ESR_SurrenderManager.FindCommander(leader) != null);
    Check(false, "must-carry case: all four able soldiers surrendered, leader included");
    Finish("mustCarry");
   }
   return;
  }
  Check(ESR_SurrenderManager.FindPrisoner(leader) != null && !ESR_SurrenderManager.FindCommander(leader), "must-carry case: a leader without a grenade surrenders");
  Check(taken == SIZE - 2, "must-carry case: all four able soldiers surrendered");
  MustPass = Failures == CaseStart;
  CaseStart = Failures;
  Configure(0, true);
  KillTwo(CASE_CONTROL);
  NextPhase();
 }

 void ControlSurrendered()
 {
  SCR_ChimeraCharacter leader = Leaders[CASE_CONTROL];
  int taken = PrisonersOf(CASE_CONTROL);
  if (taken < SIZE - 2)
  {
   if (Now() - PhaseAt > 10)
   {
    PrintFormat("[ESR GRENADE CONTROL] prisoners=%1 leaderPrisoner=%2", taken, ESR_SurrenderManager.FindPrisoner(leader) != null);
    Check(false, "control: all four able soldiers surrendered, leader included");
    Finish("control");
   }
   return;
  }
  // The spawned case's record ends GRENADE_AFTER_BLAST seconds after its fuse.
  if (ESR_SurrenderManager.CommanderCount() > 0 && Now() - PhaseAt < ESR_SurrenderManager.GRENADE_AFTER_BLAST + 6) return;
  PrintFormat("[ESR GRENADE CONTROL] prisoners=%1 commanders=%2 spawnedEnding='%3'", taken, ESR_SurrenderManager.CommanderCount(), SpawnedRecord.Ending);
  Check(ESR_SurrenderManager.FindPrisoner(leader) != null && !ESR_SurrenderManager.FindCommander(leader), "control: with 0% the leader surrenders as before");
  Check(ESR_SurrenderManager.CommanderCount() == 0 && SpawnedRecord.Ending == "blast", "no commander record left; the spawned case ended after the blast");
  ControlPass = Failures == CaseStart;
  Finish("complete");
 }
}

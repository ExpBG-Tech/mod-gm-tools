// TEST ONLY. EXPBG AI Global Skills ammunition refill with RHS: Status Quo units (audit B2).
// pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/EGS_AmmoRefillGameplay.c -Rhs -TimeoutSeconds 420 -ExpectResult '\[EGS AMMO RESULT\] checks=[1-9]\d* failures=0 rhs=1 cases=5 reason=complete' -OrchestratorSlotGranted
// Needs RHS: Status Quo plus both content packs loaded. Run-Gameplay.ps1 links them only
// for -UnitCleanup today, so the runner's -Rhs guard must allow -FixturePath for this run.
// Without RHS only the vanilla control runs, the RHS cases log available=0 and the result
// says rhs=0, which the -ExpectResult above rejects.
// The runner copies this file to EXPG_GarrisonGameplay.c; the class names are fixed.
// Standalone soldiers without an AI agent: nobody reloads, moves or fires. The RHS M40A5
// (MagazineTemplate "") spawns loaded; the fixture unloads it, the state in which the old
// code found no magazine.
// Production entry points: SCR_AICombatComponent.EGS_SetAmmoPolicy (what EGS_Manager calls)
// and the real inventory-change trigger (spares deleted through the inventory manager).
// No players, no GM UI, no save/load, no combat reloads: those stay client-test gates.
class EGSAmmoCase
{
 string Name;
 ResourceName PrefabName;
 bool Rhs;
 int Mode;
 int Refills;
 // Delete the long-gun spares before the policy: nothing loaded, carried or seen.
 bool PreDelete;
 // Spares of the chosen muzzle deleted after the policy; -1 deletes all of them.
 int DeleteCount = -1;
 bool ExpectSidearm;
 // Substring of the expected refill prefab; empty means the muzzle's own template.
 string ExpectPart;
 vector Point;
 SCR_ChimeraCharacter Soldier;
 SCR_AICombatComponent Combat;
 SCR_InventoryStorageManagerComponent Inv;
 BaseMuzzleComponent LongGun;
 BaseMuzzleComponent Sidearm;
 BaseMuzzleComponent Chosen;
 ResourceName Magazine;
 int Phase;
 float PhaseAt;
 int Baseline;
 int ChecksAtDelete;
 bool Done;
}
class EXPG_GarrisonGameplayClass : GenericEntityClass {}
class EXPG_GarrisonGameplay : GenericEntity
{
 static const float FIXTURE_SECONDS = 180;
 static const string M40_PART = "Magazine_762x51_M40_5rnd";
 ref array<ref EGSAmmoCase> Cases = {};
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
  Started = Now();
  Next = Started + 10;
  // Vanilla control: M21 has a magazine template, the refill must stay the template.
  AddCase("vanilla-m21-template", "{0F6689B491641155}Prefabs/Characters/Factions/BLUFOR/US_Army/Character_US_Sniper.et", false, EGS_Settings.AMMO_REFILL, 2, -20);
  // RHS M4A1 with a template: same vanilla path, magazines go back into RHS vest pouches.
  AddCase("rhs-m4-template", "{E92B2A4970D82027}Prefabs/Characters/Factions/BLUFOR/RHS_USAF/RHS_USAF_FORECON/Character_RHS_USAF_FORECON_Rifleman.et", true, EGS_Settings.AMMO_REFILL, 2, -10);
  // RHS M40A5 (no template, never loaded): resolved from a carried spare, then all spares
  // are deleted, so the refill can only come from the magazine remembered for its well.
  EGSAmmoCase cache = AddCase("rhs-m40-cache", "{1D1C88EC1B58537E}Prefabs/Characters/Factions/BLUFOR/RHS_USAF/RHS_USAF_FORECON/Character_RHS_USAF_FORECON_Sniper.et", true, EGS_Settings.AMMO_REFILL, 2, 0);
  cache.ExpectPart = M40_PART;
  // Unlimited on the MARSOC sniper: three spares go, the count must come back and stay.
  EGSAmmoCase unlimited = AddCase("rhs-m40-unlimited", "{8DE8E6B2CB15EFA5}Prefabs/Characters/Factions/BLUFOR/RHS_USAF/RHS_USAF_MARSOC/Character_RHS_USMC_MARSOC_Sniper.et", true, EGS_Settings.AMMO_UNLIMITED, 0, 10);
  unlimited.ExpectPart = M40_PART;
  unlimited.DeleteCount = 3;
  // Rifle magazine unknown (no template, not loaded, none carried, never seen): the
  // handgun must be refilled instead of nothing.
  EGSAmmoCase fallback = AddCase("rhs-m40-fallback", "{1D1C88EC1B58537E}Prefabs/Characters/Factions/BLUFOR/RHS_USAF/RHS_USAF_FORECON/Character_RHS_USAF_FORECON_Sniper.et", true, EGS_Settings.AMMO_REFILL, 1, 20);
  fallback.PreDelete = true;
  fallback.ExpectSidearm = true;
  PrintFormat("[EGS AMMO BEGIN] cases=%1 rhsLoaded=%2 deadline=%3", Cases.Count(), RhsLoaded(), FIXTURE_SECONDS);
 }

 EGSAmmoCase AddCase(string name, ResourceName prefabName, bool rhs, int mode, int refills, float x)
 {
  EGSAmmoCase added = new EGSAmmoCase();
  added.Name = name;
  added.PrefabName = prefabName;
  added.Rhs = rhs;
  added.Mode = mode;
  added.Refills = refills;
  added.Point = Origin + Vector(x, 0, 30);
  Cases.Insert(added);
  return added;
 }

 bool Check(bool ok, string label)
 {
  Checks++;
  if (!ok) Failures++;
  PrintFormat("[EGS AMMO CHECK] pass=%1 %2", ok, label);
  return ok;
 }

 void Finish(string reason)
 {
  if (Finished) return;
  Finished = true;
  ClearEventMask(EntityEvent.FRAME);
  int rhs = 0;
  if (RhsLoaded()) rhs = 1;
  PrintFormat("[EGS AMMO RESULT] checks=%1 failures=%2 rhs=%3 cases=%4 reason=%5", Checks, Failures, rhs, Cases.Count(), reason);
  GetGame().RequestClose();
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

 vector Ground(vector p, float lift) { p[1] = GetGame().GetWorld().GetSurfaceY(p[0], p[2]) + lift; return p; }

 EntitySpawnParams Params(vector p)
 {
  EntitySpawnParams spawn = new EntitySpawnParams();
  spawn.TransformMode = ETransformMode.WORLD;
  Math3D.AnglesToMatrix(vector.Zero, spawn.Transform);
  spawn.Transform[3] = Ground(p, 0.3);
  return spawn;
 }

 void Advance(EGSAmmoCase c, int phase)
 {
  c.Phase = phase;
  c.PhaseAt = Now();
 }

 static BaseMuzzleComponent FirstBaseMuzzle(BaseWeaponComponent weapon)
 {
  array<BaseMuzzleComponent> muzzles = {};
  weapon.GetMuzzlesList(muzzles);
  foreach (BaseMuzzleComponent muzzle : muzzles)
  {
   if (muzzle && muzzle.GetMuzzleType() == EMuzzleType.MT_BaseMuzzle) return muzzle;
  }
  return null;
 }

 // First long gun and first handgun, ranked like production (EGS_Roles.FirearmRank).
 void FindMuzzles(EGSAmmoCase c)
 {
  BaseWeaponManagerComponent weaponManager = c.Soldier.GetCharacterController().GetWeaponManagerComponent();
  if (!weaponManager) return;
  array<IEntity> weaponEntities = {};
  weaponManager.GetWeaponsList(weaponEntities);
  foreach (IEntity weaponEntity : weaponEntities)
  {
   if (!weaponEntity) continue;
   BaseWeaponComponent weapon = BaseWeaponComponent.Cast(weaponEntity.FindComponent(BaseWeaponComponent));
   if (!weapon) continue;
   int rank = EGS_Roles.FirearmRank(weapon.GetWeaponType());
   if (rank == 2 && !c.LongGun) c.LongGun = FirstBaseMuzzle(weapon);
   if (rank == 1 && !c.Sidearm) c.Sidearm = FirstBaseMuzzle(weapon);
  }
 }

 // Spare magazines that fit this muzzle's well (independent of the production count).
 int WellItems(EGSAmmoCase c, BaseMuzzleComponent muzzle, notnull array<IEntity> items)
 {
  items.Clear();
  if (!muzzle) return 0;
  BaseMagazineWell well = muzzle.GetMagazineWell();
  if (!well) return 0;
  SCR_MagazinePredicate predicate = new SCR_MagazinePredicate();
  predicate.magWellType = well.Type();
  return c.Inv.FindItems(items, predicate);
 }

 static ResourceName PrefabOf(IEntity entity)
 {
  if (!entity || !entity.GetPrefabData()) return ResourceName.Empty;
  return entity.GetPrefabData().GetPrefabName();
 }

 int DeleteSpares(EGSAmmoCase c, BaseMuzzleComponent muzzle, int limit)
 {
  array<IEntity> items = {};
  WellItems(c, muzzle, items);
  int deleted = 0;
  foreach (IEntity item : items)
  {
   if (limit >= 0 && deleted >= limit) break;
   if (c.Inv.TryDeleteItem(item)) deleted++;
  }
  return deleted;
 }

 int Spares(EGSAmmoCase c)
 {
  return c.Inv.GetMagazineCountByMuzzle(null, c.Chosen);
 }

 override void EOnFrame(IEntity owner, float timeSlice)
 {
  if (Finished || Now() < Next) return;
  Next = Now() + 0.5;
  if (!Isolated)
  {
   Isolated = true;
   if (!Check(GetGame().GetPlayerManager().GetPlayerCount() == 0, "isolated server: no players")) { Finish("setup"); return; }
  }
  if (Now() - Started > FIXTURE_SECONDS)
  {
   foreach (EGSAmmoCase late : Cases)
   {
    if (!late.Done) Check(false, string.Format("%1 finished before the deadline (phase %2)", late.Name, late.Phase));
   }
   Finish("timeout");
   return;
  }
  bool all = true;
  foreach (EGSAmmoCase c : Cases)
  {
   if (!c.Done) { Step(c); all = false; }
  }
  if (all) Finish("complete");
 }

 void Step(EGSAmmoCase c)
 {
  if (c.Phase == 0)
  {
   // Never load an RHS resource unless RHS is loaded: a missing one only logs errors.
   if (c.Rhs && !RhsLoaded())
   {
    PrintFormat("[EGS AMMO CASE] case=%1 available=0 reason='RHS: Status Quo and its content packs are not loaded; run with RHS'", c.Name);
    c.Done = true;
    return;
   }
   // Keep the Resource and the spawned entity in locals before casting (null spawns seen inline).
   Resource soldierResource = Resource.Load(c.PrefabName);
   IEntity spawned = GetGame().SpawnEntityPrefab(soldierResource, GetGame().GetWorld(), Params(c.Point));
   c.Soldier = SCR_ChimeraCharacter.Cast(spawned);
   if (!Check(c.Soldier != null, c.Name + " soldier spawned")) { c.Done = true; return; }
   Advance(c, 1);
   return;
  }
  if (c.Phase == 1)
  {
   // Initial inventory and weapons settle first.
   if (Now() - c.PhaseAt < 4) return;
   CharacterControllerComponent controller = c.Soldier.GetCharacterController();
   c.Combat = SCR_AICombatComponent.Cast(c.Soldier.FindComponent(SCR_AICombatComponent));
   if (controller) c.Inv = SCR_InventoryStorageManagerComponent.Cast(controller.GetInventoryStorageManager());
   if (!Check(controller != null && !controller.IsSwimming() && c.Combat != null && c.Inv != null, c.Name + " soldier on dry land with combat and inventory components")) { c.Done = true; return; }
   FindMuzzles(c);
   array<IEntity> longGunSpares = {};
   array<IEntity> sidearmSpares = {};
   WellItems(c, c.LongGun, longGunSpares);
   WellItems(c, c.Sidearm, sidearmSpares);
   ResourceName longGunTemplate;
   bool longGunLoaded = false;
   if (c.LongGun)
   {
    longGunTemplate = c.LongGun.GetDefaultMagazineOrProjectileName();
    longGunLoaded = c.LongGun.GetMagazine() != null;
   }
   PrintFormat("[EGS AMMO LOADOUT] case=%1 longGun=%2 template='%3' loaded=%4 longGunSpares=%5 sidearm=%6 sidearmSpares=%7", c.Name, c.LongGun != null, longGunTemplate, longGunLoaded, longGunSpares.Count(), c.Sidearm != null, sidearmSpares.Count());
   if (!Check(c.LongGun != null && longGunSpares.Count() > 0, c.Name + " long gun with carried spare magazines")) { c.Done = true; return; }
   if (c.ExpectPart != string.Empty || c.ExpectSidearm)
   {
    Check(longGunTemplate.IsEmpty(), c.Name + " long gun has no magazine template (RHS M40A5)");
    // The RHS M40A5 spawns loaded. Unload it: no template and nothing loaded is the
    // state in which the old code found no magazine (spent last magazine, AI reload).
    if (longGunLoaded)
    {
     IEntity loaded = c.LongGun.GetMagazine().GetOwner();
     if (!c.Inv.TryDeleteItem(loaded)) RplComponent.DeleteRplEntity(loaded, false);
    }
   }
   if (c.PreDelete)
   {
    if (!Check(c.Sidearm != null && sidearmSpares.Count() > 0, c.Name + " handgun with carried spare magazines")) { c.Done = true; return; }
    int removed = DeleteSpares(c, c.LongGun, -1);
    Check(removed == longGunSpares.Count(), string.Format("%1 every long-gun spare deleted before the policy (%2 of %3)", c.Name, removed, longGunSpares.Count()));
   }
   Advance(c, 2);
   return;
  }
  if (c.Phase == 2)
  {
   if (Now() - c.PhaseAt < 2) return;
   if (c.ExpectPart != string.Empty || c.ExpectSidearm) Check(c.LongGun.GetMagazine() == null, c.Name + " long gun unloaded before the policy");
   if (c.PreDelete)
   {
    array<IEntity> leftover = {};
    Check(WellItems(c, c.LongGun, leftover) == 0, c.Name + " no long-gun spare left before the policy");
   }
   c.Combat.EGS_SetAmmoPolicy(c.Mode, c.Refills);
   Advance(c, 3);
   return;
  }
  if (c.Phase == 3)
  {
   // The production check runs EGS_AMMO_CHECK_DELAY_MS (1.5 s) after the policy.
   if (Now() - c.PhaseAt < 3) return;
   BaseMuzzleComponent resolvedMuzzle;
   ResourceName resolvedMagazine;
   bool resolved = c.Combat.EGSTest_Resolve(resolvedMuzzle, resolvedMagazine);
   if (!Check(resolved && resolvedMuzzle != null, c.Name + " production resolves a refill muzzle and magazine")) { c.Done = true; return; }
   c.Chosen = resolvedMuzzle;
   c.Magazine = resolvedMagazine;
   BaseMuzzleComponent expectedMuzzle = c.LongGun;
   if (c.ExpectSidearm) expectedMuzzle = c.Sidearm;
   Check(c.Chosen == expectedMuzzle, string.Format("%1 refill targets the %2", c.Name, ExpectedWeapon(c)));
   string expectedMagazine = c.ExpectPart;
   if (expectedMagazine.IsEmpty() || c.ExpectSidearm) expectedMagazine = c.Chosen.GetDefaultMagazineOrProjectileName();
   Check(!c.Magazine.IsEmpty() && c.Magazine.Contains(expectedMagazine), string.Format("%1 refill prefab %2 matches %3", c.Name, c.Magazine, expectedMagazine));
   c.Baseline = c.Combat.EGSTest_Baseline();
   int spares = Spares(c);
   PrintFormat("[EGS AMMO POLICY] case=%1 mode=%2 refills=%3 magazine=%4 baseline=%5 spares=%6 refillsLeft=%7 checks=%8", c.Name, c.Mode, c.Refills, c.Magazine, c.Baseline, spares, c.Combat.EGSTest_RefillsLeft(), c.Combat.EGSTest_Checks());
   Check(c.Baseline >= 1 && c.Baseline == Math.MinInt(spares, 12), string.Format("%1 baseline %2 is the carried count %3", c.Name, c.Baseline, spares));
   c.ChecksAtDelete = c.Combat.EGSTest_Checks();
   int deleted = DeleteSpares(c, c.Chosen, c.DeleteCount);
   PrintFormat("[EGS AMMO DELETE] case=%1 deleted=%2 requested=%3", c.Name, deleted, c.DeleteCount);
   Check(deleted > 0, c.Name + " spare magazines deleted through the inventory manager");
   Advance(c, 4);
   return;
  }
  if (c.Phase == 4)
  {
   // Before the deferred check: nothing loaded, nothing carried for the RHS M40A5. The
   // muzzle must still resolve (the magazine remembered for its well, as during a reload).
   if (Now() - c.PhaseAt < 0.5) return;
   if (c.ExpectPart != string.Empty && c.DeleteCount < 0 && !c.ExpectSidearm)
   {
    array<IEntity> emptyWell = {};
    BaseMuzzleComponent cachedMuzzle;
    ResourceName cachedMagazine;
    bool stillResolved = c.Combat.EGSTest_Resolve(cachedMuzzle, cachedMagazine);
    PrintFormat("[EGS AMMO EMPTY] case=%1 spares=%2 wellItems=%3 loaded=%4 resolved=%5 magazine=%6", c.Name, Spares(c), WellItems(c, c.Chosen, emptyWell), c.Chosen.GetMagazine() != null, stillResolved, cachedMagazine);
    Check(stillResolved && cachedMuzzle == c.Chosen && cachedMagazine == c.Magazine, c.Name + " with no loaded or carried magazine the remembered M40 magazine still resolves");
   }
   Advance(c, 5);
   return;
  }
  if (c.Phase == 5)
  {
   int count = Spares(c);
   if (count < c.Baseline)
   {
    if (Now() - c.PhaseAt > 12)
    {
     PrintFormat("[EGS AMMO REFILL] case=%1 refilled=0 spares=%2 baseline=%3 checksSinceDelete=%4", c.Name, count, c.Baseline, c.Combat.EGSTest_Checks() - c.ChecksAtDelete);
     Check(false, c.Name + " spare magazines refilled to the baseline within 12 s");
     c.Done = true;
    }
    return;
   }
   array<IEntity> items = {};
   WellItems(c, c.Chosen, items);
   int matching = 0;
   foreach (IEntity item : items)
   {
    if (PrefabOf(item) == c.Magazine) matching++;
   }
   int expectedLeft = c.Refills;
   if (c.Mode == EGS_Settings.AMMO_REFILL) expectedLeft = c.Refills - 1;
   PrintFormat("[EGS AMMO REFILL] case=%1 refilled=1 spares=%2 baseline=%3 wellItems=%4 matchingPrefab=%5 refillsLeft=%6 checksSinceDelete=%7", c.Name, count, c.Baseline, items.Count(), matching, c.Combat.EGSTest_RefillsLeft(), c.Combat.EGSTest_Checks() - c.ChecksAtDelete);
   Check(count == c.Baseline, string.Format("%1 spare count back to the baseline %2", c.Name, c.Baseline));
   Check(items.Count() == count, string.Format("%1 production count %2 equals the magazine-well count %3", c.Name, count, items.Count()));
   if (c.DeleteCount < 0) Check(matching == items.Count(), c.Name + " every refilled magazine is the resolved prefab");
   if (c.Mode == EGS_Settings.AMMO_REFILL) Check(c.Combat.EGSTest_RefillsLeft() == expectedLeft, string.Format("%1 one refill used (%2 left)", c.Name, expectedLeft));
   if (c.ExpectSidearm)
   {
    array<IEntity> rifleSpares = {};
    Check(WellItems(c, c.LongGun, rifleSpares) == 0, c.Name + " no rifle magazine invented for the unknown M40 magazine");
   }
   Advance(c, 6);
   return;
  }
  if (c.Phase == 6)
  {
   // No runaway: the count must hold after further deferred checks.
   if (Now() - c.PhaseAt < 5) return;
   int settled = Spares(c);
   PrintFormat("[EGS AMMO STABLE] case=%1 spares=%2 baseline=%3 checksSinceDelete=%4", c.Name, settled, c.Baseline, c.Combat.EGSTest_Checks() - c.ChecksAtDelete);
   Check(settled == c.Baseline, string.Format("%1 spare count stays at the baseline (no repeated refills)", c.Name));
   c.Done = true;
  }
 }

 string ExpectedWeapon(EGSAmmoCase c)
 {
  if (c.ExpectSidearm) return "handgun";
  return "long gun";
 }
}

// Read-only test hooks on the production component.
modded class SCR_AICombatComponent
{
 protected int m_iEGSTest_Checks;

 override protected void EGS_CheckAmmo()
 {
  m_iEGSTest_Checks++;
  super.EGS_CheckAmmo();
 }

 int EGSTest_Checks() { return m_iEGSTest_Checks; }
 int EGSTest_Baseline() { return m_iEGS_MagazineBaseline; }
 int EGSTest_RefillsLeft() { return m_iEGS_RefillsLeft; }
 bool EGSTest_Resolve(out BaseMuzzleComponent muzzle, out ResourceName magazine) { return EGS_FindRefillMuzzle(muzzle, magazine); }
}

// TEST ONLY. EXPBG Unit Scripts night discipline with RHS: Status Quo units (audit B3).
// pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/EUS_DisciplineRhsGameplay.c -TimeoutSeconds 300 -ExpectResult '\[EUS RHS TEST RESULT\] checks=[1-9]\d* failures=0 reason=completed' -OrchestratorSlotGranted
// Needs RHS: Status Quo plus both content packs loaded. Run-Gameplay.ps1 links them
// only for -UnitCleanup today, so the runner's -Rhs guard must allow -FixturePath for
// this run. Without RHS the fixture logs available=0 and ends with reason=rhs-not-loaded,
// which the -ExpectResult above rejects.
// The runner copies this file to EXPG_GarrisonGameplay.c; the class names are fixed.
// Three RHS ION Scouts (INDFOR) in one AI squad. Each wears an OPS-Core helmet whose
// NVG slot (RHS_LoadoutSlotInfo, not an EquipmentStorageSlot) holds the PATROL IR head
// mount (RHS_RhinoAttachmentComponent: EGadgetType.NIGHT_VISION, use mask CUSTOM from
// NVG_Base.et). Production EUS_Manager.SetDiscipline entry points (the ones the squad
// attribute calls). No players, no GM UI, no save/load: those stay client-test gates.
class EUSRhsTestTally
{
 int Living;
 int Listed;
 int Worn;
 int Unslotted;
 int InSlot;
 int NoFromAction;
 int Lit;
 int Flashlights;
 int FlashlightsLit;
 int Owned;

 string Describe()
 {
  return string.Format("living=%1 nvgListed=%2 nvgWorn=%3 nvgOutsideEquipmentSlot=%4 nvgInSlot=%5 nvgWithoutFromAction=%6 nvgOn=%7 flashlights=%8 flashlightsOn=%9", Living, Listed, Worn, Unslotted, InSlot, NoFromAction, Lit, Flashlights, FlashlightsLit);
 }
}

class EXPG_GarrisonGameplayClass : GenericEntityClass {}
class EXPG_GarrisonGameplay : GenericEntity
{
 static const float FIXTURE_SECONDS = 150;
 static const ResourceName SQUAD = "{000CD338713F2B5A}Prefabs/AI/Groups/Group_Base.et";
 // RHS: Status Quo ION Scouts; all three wear a helmet with the PATROL IR head mount.
 static const ResourceName SCOUT_A = "{AB12C80AE86DC7DD}Prefabs/Characters/Factions/INDFOR/RHS_ION/Character_RHS_ION_Scout.et";
 static const ResourceName SCOUT_B = "{CA4AE6DF518EA781}Prefabs/Characters/Factions/INDFOR/RHS_ION/Character_RHS_ION_Scout2.et";
 static const ResourceName SCOUT_C = "{2FD6AA265D6B30EA}Prefabs/Characters/Factions/INDFOR/RHS_ION/Character_RHS_ION_Scout3.et";
 vector Origin = "4773.46 0 7094.57";
 int Checks;
 int Failures;
 int Phase;
 float Started;
 float Next;
 float PhaseAt;
 bool Finished;
 EUS_Manager Manager;
 ref EUS_Report Report = new EUS_Report();
 SCR_AIGroup Squad;
 int Spawned;
 SCR_GadgetComponent Probe;

 void EXPG_GarrisonGameplay(IEntitySource src, IEntity parent) { SetEventMask(EntityEvent.INIT | EntityEvent.FRAME); }
 float Now() { return GetGame().GetWorld().GetWorldTime() * 0.001; }

 override void EOnInit(IEntity owner)
 {
  if (!Replication.IsServer()) { ClearEventMask(EntityEvent.FRAME); return; }
  Started = Now();
  Next = Started + 10;
  PrintFormat("[EUS RHS TEST BEGIN] scouts=%1 origin=%2 deadline=%3", SCOUT_A, Origin, FIXTURE_SECONDS);
 }

 bool Check(bool ok, string label)
 {
  Checks++;
  if (!ok) Failures++;
  PrintFormat("[EUS RHS TEST CHECK] pass=%1 %2", ok, label);
  return ok;
 }

 void Finish(string reason)
 {
  if (Finished) return;
  Finished = true;
  ClearEventMask(EntityEvent.FRAME);
  PrintFormat("[EUS RHS TEST RESULT] checks=%1 failures=%2 reason=%3", Checks, Failures, reason);
  GetGame().RequestClose();
 }

 void Advance(int phase)
 {
  Phase = phase;
  PhaseAt = Now();
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

 // RHS: Status Quo plus both content packs (same test as EBG_UnitCleanupGameplay.c).
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

 bool HasOwnLights(SCR_ChimeraCharacter actor)
 {
  SCR_AICharacterSettingsComponent settings = SCR_AICharacterSettingsComponent.FindOnControlledEntity(actor);
  return settings && EUS_LightSetting.Cast(settings.GetCurrentSetting(SCR_AICharacterLightInteractionSettingBase)) != null;
 }

 // Night vision and flashlight state of every living squad member, judged by the
 // production worn test (EUS_DisciplineRecord.IsWorn / CountLights).
 EUSRhsTestTally Tally(bool detail)
 {
  EUSRhsTestTally tally = new EUSRhsTestTally();
  array<AIAgent> agents = {};
  Squad.GetAgents(agents);
  foreach (AIAgent agent : agents)
  {
   SCR_ChimeraCharacter member = SCR_ChimeraCharacter.Cast(agent.GetControlledEntity());
   if (!member || member.GetCharacterController().IsDead()) continue;
   tally.Living++;
   if (HasOwnLights(member)) tally.Owned++;
   int memberFlashlights;
   int memberFlashlightsLit;
   EUS_DisciplineRecord.CountLights(member, memberFlashlights, memberFlashlightsLit);
   tally.Flashlights += memberFlashlights;
   tally.FlashlightsLit += memberFlashlightsLit;
   SCR_GadgetManagerComponent gadgets = SCR_GadgetManagerComponent.GetGadgetManager(member);
   if (!gadgets) continue;
   array<SCR_GadgetComponent> list = gadgets.GetGadgetsByType(EGadgetType.NIGHT_VISION);
   if (!list) continue;
   foreach (SCR_GadgetComponent gadget : list)
   {
    if (!gadget) continue;
    tally.Listed++;
    InventoryItemComponent item = InventoryItemComponent.Cast(gadget.GetOwner().FindComponent(InventoryItemComponent));
    string slotType = "none";
    if (item && item.GetParentSlot()) slotType = item.GetParentSlot().Type().ToString();
    bool occluded;
    bool worn = EUS_DisciplineRecord.IsWorn(gadget, occluded);
    if (detail) PrintFormat("[EUS RHS TEST NVG] member=%1 device=%2 mode=%3 useMask=%4 parentSlot=%5 worn=%6 on=%7", member, gadget.GetOwner().GetPrefabData().GetPrefabName(), typename.EnumToString(EGadgetMode, gadget.GetMode()), gadget.GetUseMask(), slotType, worn, gadget.IsToggledOn());
    if (!worn) continue;
    tally.Worn++;
    if (item && !EquipmentStorageSlot.Cast(item.GetParentSlot())) tally.Unslotted++;
    if (gadget.GetMode() == EGadgetMode.IN_SLOT) tally.InSlot++;
    if ((gadget.GetUseMask() & SCR_EUseContext.FROM_ACTION) == 0) tally.NoFromAction++;
    if (gadget.IsToggledOn()) tally.Lit++;
    if (!Probe) Probe = gadget;
   }
  }
  return tally;
 }

 override void EOnFrame(IEntity owner, float timeSlice)
 {
  if (Finished || Now() < Next) return;
  Next = Now() + 0.5;
  if (Now() - Started > FIXTURE_SECONDS)
  {
   Check(false, string.Format("fixture finished before its deadline (stuck in phase %1)", Phase));
   Finish("timeout");
   return;
  }
  Step();
 }

 void Step()
 {
  if (Phase == 0)
  {
   // Never load an RHS resource unless RHS is loaded: a missing one only logs errors.
   if (!RhsLoaded())
   {
    PrintFormat("[EUS RHS TEST] available=0 reason='RHS: Status Quo and its content packs are not loaded'");
    Finish("rhs-not-loaded");
    return;
   }
   PrintFormat("[EUS RHS TEST] available=1");
   // Keep Resource and spawned entity in locals before casting (inline form returned null natively).
   Resource squadResource = Resource.Load(SQUAD);
   IEntity squadEntity = GetGame().SpawnEntityPrefab(squadResource, GetGame().GetWorld(), Params(Origin));
   Squad = SCR_AIGroup.Cast(squadEntity);
   if (!Check(Squad != null, "vanilla base AI group spawned")) { Finish("setup"); return; }
   Squad.SetDeleteWhenEmpty(false);
   array<ResourceName> prefabs = {SCOUT_A, SCOUT_B, SCOUT_C};
   for (int i = 0; i < prefabs.Count(); i++)
   {
    Resource scoutResource = Resource.Load(prefabs[i]);
    IEntity scoutEntity = GetGame().SpawnEntityPrefab(scoutResource, GetGame().GetWorld(), Params(Origin + Vector(2 * i, 0, 3)));
    SCR_ChimeraCharacter scout = SCR_ChimeraCharacter.Cast(scoutEntity);
    if (!scout) continue;
    FactionAffiliationComponent affiliation = FactionAffiliationComponent.Cast(scout.FindComponent(FactionAffiliationComponent));
    if (Spawned == 0 && affiliation && affiliation.GetAffiliatedFaction()) Squad.SetFaction(affiliation.GetAffiliatedFaction());
    if (Squad.AddAIEntityToGroup(scout)) Spawned++;
   }
   if (!Check(Spawned == 3, "three RHS ION Scouts spawned into the squad")) { Finish("setup"); return; }
   Advance(1);
   return;
  }
  if (Phase == 1)
  {
   if (Squad.GetAgentsCount() < 3)
   {
    if (Now() - PhaseAt > 30) { Check(false, "squad reports 3 AI members"); Finish("setup"); }
    return;
   }
   if (Now() - PhaseAt < 5) return;
   EUSRhsTestTally start = Tally(true);
   PrintFormat("[EUS RHS TEST LIGHTS] mode=none %1 ownedSettings=%2", start.Describe(), start.Owned);
   Check(start.Living == 3, "three living RHS ION Scouts");
   if (!Check(start.Worn == start.Living, "every scout's helmet night device is listed by the gadget manager and counted as worn")) { Finish("rhs-loadout"); return; }
   Check(start.Unslotted == start.Worn, "the RHS night devices sit outside any EquipmentStorageSlot (the slot the old filter required)");
   Check(start.InSlot == start.Worn, "RHS reports each helmet-mounted device as IN_SLOT");
   Check(start.NoFromAction == start.Worn, "the RHS night devices do not accept FROM_ACTION (use mask CUSTOM)");
   Check(start.Lit == 0, "the night devices start off");
   // Control for the B3 root cause: the old call. Vanilla ToggleActive returns early.
   if (Probe) Probe.ToggleActive(true, SCR_EUseContext.FROM_ACTION);
   Advance(2);
   return;
  }
  if (Phase == 2)
  {
   if (Now() - PhaseAt < 2) return;
   bool ignored = Probe && !Probe.IsToggledOn();
   PrintFormat("[EUS RHS TEST CONTROL] fromActionIgnored=%1 (the pre-fix call; 1 confirms the vanilla use-mask early return)", ignored);
   if (Probe && Probe.IsToggledOn()) EUS_DisciplineRecord.SwitchGadget(Probe, false);
   Manager = EUS_Manager.Get();
   if (!Check(Manager != null, "server unit script manager available")) { Finish("setup"); return; }
   Check(Manager.SetDiscipline(Squad, EUS_Codes.DISCIPLINE_LIGHT, Report), "Light Discipline applied");
   Advance(3);
   return;
  }
  if (Phase == 3)
  {
   if (Now() - PhaseAt < 3) return;
   EUSRhsTestTally light = Tally(true);
   PrintFormat("[EUS RHS TEST LIGHTS] mode=light %1 ownedSettings=%2", light.Describe(), light.Owned);
   Check(Squad.EUS_Discipline == EUS_Codes.DISCIPLINE_LIGHT && light.Owned == light.Living && light.Living > 0, "Light Discipline owns every living member's light setting");
   Check(light.Worn > 0 && light.Lit == light.Worn, "Light Discipline switches every worn RHS night device on");
   Check(light.FlashlightsLit == 0, "Light Discipline keeps flashlights off on RHS units");
   Check(Manager.SetDiscipline(Squad, EUS_Codes.DISCIPLINE_TERROR, Report), "Terror Tactics applied");
   Advance(4);
   return;
  }
  if (Phase == 4)
  {
   if (Now() - PhaseAt < 3) return;
   EUSRhsTestTally terror = Tally(false);
   PrintFormat("[EUS RHS TEST LIGHTS] mode=terror %1 ownedSettings=%2", terror.Describe(), terror.Owned);
   Check(Squad.EUS_Discipline == EUS_Codes.DISCIPLINE_TERROR && terror.Owned == terror.Living, "Terror Tactics owns every living member's light setting");
   Check(terror.Lit == 0, "Terror Tactics switches off the night devices Light Discipline switched on");
   if (terror.Flashlights > 0) Check(terror.FlashlightsLit == terror.Flashlights, "Terror Tactics switches worn flashlights on on RHS units");
   else PrintFormat("[EUS RHS TEST LIGHTS] knownLimitation=no-worn-flashlights-in-ion-scout-loadout");
   Check(Manager.SetDiscipline(Squad, EUS_Codes.DISCIPLINE_LIGHT, Report), "Light Discipline applied again");
   Advance(5);
   return;
  }
  if (Phase == 5)
  {
   if (Now() - PhaseAt < 3) return;
   EUSRhsTestTally again = Tally(false);
   PrintFormat("[EUS RHS TEST LIGHTS] mode=light-again %1 ownedSettings=%2", again.Describe(), again.Owned);
   Check(again.Worn > 0 && again.Lit == again.Worn, "Light Discipline switches the RHS night devices on again");
   Check(Manager.SetDiscipline(Squad, EUS_Codes.DISCIPLINE_OFF, Report), "night discipline released");
   Advance(6);
   return;
  }
  if (Phase == 6)
  {
   if (Now() - PhaseAt < 3) return;
   EUSRhsTestTally released = Tally(true);
   PrintFormat("[EUS RHS TEST LIGHTS] mode=off %1 ownedSettings=%2", released.Describe(), released.Owned);
   Check(Squad.EUS_Discipline == EUS_Codes.DISCIPLINE_OFF && released.Owned == 0, "release removes every owned light setting");
   Check(released.Lit == 0, "release switches off the night devices the discipline switched on");
   Check(!Manager.SetDiscipline(Squad, EUS_Codes.DISCIPLINE_OFF, Report), "second release is a no-op");
   Finish("completed");
   return;
  }
 }
}

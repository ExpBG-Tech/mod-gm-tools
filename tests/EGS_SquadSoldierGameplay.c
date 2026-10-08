// TEST ONLY. EXPBG AI Global Skills and AI Surrender squad and soldier overrides WITHOUT any
// module placed, for a vanilla squad and (with -Rhs) an RHS: Status Quo squad (dedicated
// server, no players, no GM UI).
// pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/EGS_SquadSoldierGameplay.c -TimeoutSeconds 240 -OrchestratorSlotGranted -ExpectResult '\[EGS SQUAD SOLDIER RESULT\] checks=[1-9]\d* failures=0 squads=[12] registered=12 reason=complete'
// Add -Rhs to also run the RHS squad (squads=2); without RHS it logs available=0 and runs the
// vanilla squad only (squads=1).
// The runner copies this file to EXPG_GarrisonGameplay.c; the class names are fixed. Judge
// the run by its one [EGS SQUAD SOLDIER RESULT] line (-ExpectResult also rejects script errors).
//  1. Registration: the merged Game Master Edit list (Configs/Editor/AttributeLists/Edit.conf)
//     and both modules' own fallback lists (EXPGS/EGS_SquadSoldier.conf, EXPSR/ESR_SquadSoldier.conf)
//     hold all twelve squad and soldier dialog attributes (registered=12).
//  2. No module exists. Dialog targets (the production helpers the attributes call): a squad
//     resolves to itself and through each soldier (like the vanilla Group tab), soldiers
//     resolve to themselves; session saves keep to the squad itself.
//  3. Effect without a module, through the production writers: squad skill Expert, one soldier
//     Novice, squad Return Fire Only, squad surrender 100%. Members take Expert, the soldier
//     Novice; the squad's external combat mode is RETURN_FIRE; the effective surrender chance
//     is 100% from the squad. Cleared again: members back to vanilla skill, no EXPBG profile.
// Not covered: the GM dialog itself (no editor without a player), the editor's script
// fallback when another mod drops the Edit list entries (needs the editor mode prefab), and
// surrenders (they need the module).
class EXPG_GarrisonGameplayClass : GenericEntityClass {}
class EXPG_GarrisonGameplay : GenericEntity
{
 static const float FIXTURE_SECONDS = 180;
 static const ResourceName EDIT_LIST = "{F3D6C6D25642352C}Configs/Editor/AttributeLists/Edit.conf";
 static const ResourceName EGS_LIST = "{A4BB2BF355CA2A33}Configs/Editor/AttributeLists/EXPGS/EGS_SquadSoldier.conf";
 static const ResourceName ESR_LIST = "{867D8E3D247F48BF}Configs/Editor/AttributeLists/EXPSR/ESR_SquadSoldier.conf";
 static const ResourceName VANILLA_SQUAD = "{84E5BBAB25EA23E5}Prefabs/Groups/BLUFOR/Group_US_FireTeam.et";
 static const ResourceName RHS_SQUAD = "{60E2D587BE5A9B43}Prefabs/Groups/OPFOR/RHS_AFRF/MSV/VKPO_Demiseason/Group_RHS_RF_MSV_VKPO_DS_MachineGunTeam.et";
 vector Origin = "4773.46 0 7094.57";
 vector RhsOffset = "120 0 0";
 ref array<SCR_AIGroup> Squads = {};
 ref array<SCR_ChimeraCharacter> Novices = {};
 int Phase;
 int Checks;
 int Failures;
 int Registered;
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
  PrintFormat("[EGS SQUAD SOLDIER BEGIN] origin=%1 deadline=%2", Origin, FIXTURE_SECONDS);
 }

 bool Check(bool ok, string label)
 {
  Checks++;
  if (!ok) Failures++;
  PrintFormat("[EGS SQUAD SOLDIER CHECK] pass=%1 phase=%2 %3", ok, Phase, label);
  return ok;
 }

 void Finish(string reason)
 {
  if (Finished) return;
  Finished = true;
  ClearEventMask(EntityEvent.FRAME);
  PrintFormat("[EGS SQUAD SOLDIER RESULT] checks=%1 failures=%2 squads=%3 registered=%4 reason=%5", Checks, Failures, Squads.Count(), Registered, reason);
  GetGame().RequestClose();
 }

 void Advance(int phase)
 {
  Phase = phase;
  PhaseAt = Now();
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

 SCR_AIGroup SpawnSquad(ResourceName prefab, vector p)
 {
  EntitySpawnParams spawn = new EntitySpawnParams();
  spawn.TransformMode = ETransformMode.WORLD;
  Math3D.MatrixIdentity4(spawn.Transform);
  p[1] = GetGame().GetWorld().GetSurfaceY(p[0], p[2]) + 0.3;
  spawn.Transform[3] = p;
  // Keep the Resource and the spawned entity in locals before casting.
  Resource resource = Resource.Load(prefab);
  IEntity spawned = GetGame().SpawnEntityPrefab(resource, GetGame().GetWorld(), spawn);
  SCR_AIGroup squad = SCR_AIGroup.Cast(spawned);
  return squad;
 }

 SCR_EditableEntityComponent Editable(IEntity entity)
 {
  if (!entity) return null;
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(entity);
  return editable;
 }

 void Members(SCR_AIGroup squad, notnull array<SCR_ChimeraCharacter> members)
 {
  members.Clear();
  if (!squad) return;
  array<AIAgent> agents = {};
  squad.GetAgents(agents);
  foreach (AIAgent agent : agents)
  {
   if (!agent) continue;
   SCR_ChimeraCharacter member = SCR_ChimeraCharacter.Cast(agent.GetControlledEntity());
   if (member) members.Insert(member);
  }
 }

 SCR_EditorAttributeList LoadList(ResourceName list)
 {
  Resource resource = BaseContainerTools.LoadContainer(list);
  if (!resource || !resource.IsValid()) return null;
  BaseContainer container = resource.GetResource().ToBaseContainer();
  if (!container) return null;
  SCR_EditorAttributeList loaded = SCR_EditorAttributeList.Cast(BaseContainerTools.CreateInstanceFromContainer(container));
  return loaded;
 }

 bool HasType(SCR_EditorAttributeList list, typename type)
 {
  if (!list) return false;
  for (int i = 0; i < list.GetAttributesCount(); i++)
  {
   SCR_BaseEditorAttribute attribute = list.GetAttribute(i);
   if (attribute && attribute.Type() == type) return true;
  }
  return false;
 }

 void CheckRegistration()
 {
  SCR_EditorAttributeList edit = LoadList(EDIT_LIST);
  SCR_EditorAttributeList skills = LoadList(EGS_LIST);
  SCR_EditorAttributeList surrender = LoadList(ESR_LIST);
  Check(edit && skills && surrender, "Edit list and both fallback lists load");
  array<typename> egsTypes = {EGS_GroupSkillAttribute, EGS_UnitSkillAttribute, EGS_GroupRoeAttribute, EGS_UnitRoeAttribute};
  array<typename> esrTypes = {ESR_GroupSurrenderAttribute, ESR_GroupRevealAttribute, ESR_GroupIdentityAttribute, ESR_GroupIntelAttribute, ESR_UnitSurrenderAttribute, ESR_UnitRevealAttribute, ESR_UnitIdentityAttribute, ESR_UnitIntelAttribute};
  foreach (typename egsType : egsTypes)
  {
   if (Check(HasType(edit, egsType) && HasType(skills, egsType), egsType.ToString() + " in the Edit list and the AI Global Skills fallback list")) Registered++;
  }
  foreach (typename esrType : esrTypes)
  {
   if (Check(HasType(edit, esrType) && HasType(surrender, esrType), esrType.ToString() + " in the Edit list and the AI Surrender fallback list")) Registered++;
  }
 }

 override void EOnFrame(IEntity owner, float timeSlice)
 {
  if (Finished || Now() < Next) return;
  Next = Now() + 0.25;
  if (Now() - Started > FIXTURE_SECONDS) { Check(false, string.Format("phase %1 finished before the fixture deadline", Phase)); Finish("timeout"); return; }
  Step();
 }

 void Step()
 {
  if (Phase == 0)
  {
   Check(GetGame().GetPlayerManager().GetPlayerCount() == 0, "isolated server: no players");
   Check(!EGS_Module.HasAny() && ESR_SurrenderModule.ActiveCount() == 0 && !EGS_Manager.IsActive(), "no AI Global Skills and no AI Surrender module");
   CheckRegistration();
   SCR_AIGroup vanilla = SpawnSquad(VANILLA_SQUAD, Origin);
   if (!Check(vanilla != null, "vanilla US fire team spawned")) { Finish("setup"); return; }
   Squads.Insert(vanilla);
   // Never load an RHS resource unless RHS is loaded: a missing one only logs errors.
   if (RhsLoaded())
   {
    PrintFormat("[EGS SQUAD SOLDIER RHS] available=1");
    SCR_AIGroup rhs = SpawnSquad(RHS_SQUAD, Origin + RhsOffset);
    if (!Check(rhs != null, "RHS: Status Quo machine-gun team spawned")) { Finish("setup"); return; }
    Squads.Insert(rhs);
   }
   else
   {
    PrintFormat("[EGS SQUAD SOLDIER RHS] available=0 reason='RHS: Status Quo and its content packs are not loaded; run with -Rhs'");
   }
   Advance(1); return;
  }
  if (Phase == 1)
  {
   array<SCR_ChimeraCharacter> members = {};
   bool ready = true;
   foreach (SCR_AIGroup waiting : Squads)
   {
    Members(waiting, members);
    if (members.Count() < 2) ready = false;
   }
   if (!ready)
   {
    if (Now() - PhaseAt > 30) { Check(false, "squads have at least two soldiers within 30 s"); Finish("setup"); }
    return;
   }
   foreach (SCR_AIGroup squad : Squads)
   {
    Members(squad, members);
    SCR_EditableEntityComponent squadEditable = Editable(squad);
    SCR_EditableEntityComponent memberEditable = Editable(members[0]);
    string name = squad.GetPrefabData().GetPrefabName();
    Check(EGS_Attributes.DialogGroup(squadEditable) == squad && EGS_Attributes.DialogGroup(memberEditable) == squad, "skill/ROE squad attributes resolve the squad itself and through a soldier: " + name);
    Check(EGS_UnitRoe.ResolveSoldier(memberEditable) == members[0], "skill/ROE soldier attributes resolve the soldier: " + name);
    Check(ESR_GroupOverrideAttribute.GroupTarget(squadEditable) == squad && ESR_GroupOverrideAttribute.GroupTarget(memberEditable, true) == squad && ESR_GroupOverrideAttribute.GroupTarget(memberEditable, false) == null, "surrender squad attributes: the squad itself, through a soldier in the dialog only: " + name);
    Check(ESR_UnitOverrideAttribute.UnitTarget(memberEditable) == members[0], "surrender soldier attributes resolve the soldier: " + name);
    // Production writers, no module.
    EGS_SkillOverrides.SetGroup(squad, EGS_Settings.SKILL_MAX, "fixture");
    EGS_SkillOverrides.SetUnit(members[1], 1, "fixture");
    Novices.Insert(members[1]);
    EGS_Manager.SetGroupRoe(squad, EGS_Settings.ROE_RETURN_FIRE);
    ESR_Overrides.SetGroup(squad, ESR_Overrides.SURRENDER, 100, "fixture");
   }
   Advance(2); return;
  }
  if (Phase == 2)
  {
   if (Now() - PhaseAt < 3) return;
   array<SCR_ChimeraCharacter> applied = {};
   foreach (int index, SCR_AIGroup squad : Squads)
   {
    Members(squad, applied);
    string name = squad.GetPrefabData().GetPrefabName();
    int experts;
    int novices;
    foreach (SCR_ChimeraCharacter member : applied)
    {
     SCR_AICombatComponent combat = SCR_AICombatComponent.Cast(member.FindComponent(SCR_AICombatComponent));
     if (!combat) continue;
     EAISkill skill = combat.EGS_ResolveSkill(EAISkill.NONE);
     if (member == Novices[index] && skill == EAISkill.NOOB) novices++;
     if (member != Novices[index] && skill == EAISkill.EXPERT) experts++;
    }
    Check(novices == 1 && experts == applied.Count() - 1, string.Format("no module: squad Expert, one soldier Novice (experts=%1 novices=%2 members=%3): %4", experts, novices, applied.Count(), name));
    SCR_AIGroupUtilityComponent utility = squad.GetGroupUtilityComponent();
    Check(utility && utility.GetCombatModeExternal() == EAIGroupCombatMode.RETURN_FIRE, "no module: squad Return Fire Only applied (external RETURN_FIRE): " + name);
    int source;
    int chance = ESR_Overrides.Resolve(applied[0], squad, ESR_Overrides.SURRENDER, source);
    Check(chance == 100 && source == ESR_Overrides.SOURCE_SQUAD, string.Format("squad surrender chance 100 resolved from the squad (chance=%1 source=%2): %3", chance, source, name));
    EGS_SkillOverrides.SetGroup(squad, EGS_SkillOverrides.FOLLOW, "fixture");
    EGS_SkillOverrides.SetUnit(Novices[index], EGS_SkillOverrides.FOLLOW, "fixture");
    EGS_Manager.SetGroupRoe(squad, EGS_Settings.GROUP_ROE_DEFAULT);
    ESR_Overrides.SetGroup(squad, ESR_Overrides.SURRENDER, ESR_Overrides.UNSET, "fixture");
   }
   Advance(3); return;
  }
  if (Phase == 3)
  {
   if (Now() - PhaseAt < 3) return;
   array<SCR_ChimeraCharacter> cleared = {};
   foreach (SCR_AIGroup squad : Squads)
   {
    Members(squad, cleared);
    int profiled;
    foreach (SCR_ChimeraCharacter member : cleared)
    {
     SCR_AICombatComponent combat = SCR_AICombatComponent.Cast(member.FindComponent(SCR_AICombatComponent));
     if (combat && (combat.EGS_HasProfile() || combat.EGS_ResolveSkill(EAISkill.NONE) != EAISkill.NONE)) profiled++;
    }
    Check(profiled == 0 && squad.EGS_GetSkillOverride() == EGS_SkillOverrides.FOLLOW && squad.EGS_GetRoeOverride() == EGS_Settings.GROUP_ROE_DEFAULT, string.Format("cleared: every soldier back to vanilla skill (profiled=%1): %2", profiled, squad.GetPrefabData().GetPrefabName()));
   }
   Finish("complete");
  }
 }
}

// TEST ONLY. EXPBG AI Surrender and AI Global Skills: per-squad and per-soldier overrides
// (dedicated server, no players, no GM UI).
// pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/ESR_OverrideGameplay.c -TimeoutSeconds 420 -OrchestratorSlotGranted -ExpectResult '\[ESR OVERRIDE RESULT\] checks=[1-9]\d* failures=0 surrendered=4 heldOut=1 control=0 interrogation=3 intel=1 roe=1 cached=1 reason=complete'
// The runner copies this file to EXPG_GarrisonGameplay.c; the class names are fixed. Judge
// the run by its one [ESR OVERRIDE RESULT] line (-ExpectResult also rejects script errors).
// Real AI Surrender and AI Global Skills module prefabs, two native USSR rifle squads, a
// real Unit Caching zone in Full mode and one EXPBG Intel Items notebook. Module settings:
// surrender 0%, threshold 10%, random 0, reveal 0%, identity 0%, intel 0%, radius 300 m,
// grenade 0%, default rules of engagement Return Fire Only.
//  1. Overrides through the production attribute classes (the attribute-saver path: null
//     manager, playerID -1) and setters: squad A surrender 100%, reveal 100%, intel 100%
//     (identity left on the module); one soldier ("holdout") of A surrender 0%; squad A ROE
//     Fire on Sight; the holdout's own ROE Return Fire Only. Reads: set values come back as
//     spinbox entries, unset ones as nothing (never saved). Resolution: plain A soldier
//     100% (squad), holdout 0% (soldier), squad B 0% (module).
//  2. ROE: A Fire on Sight (FIRE_AT_WILL), B the module default (RETURN_FIRE); the holdout
//     answers HOLD_FIRE himself while his squad mates fire at will.
//  3. Caching: squad A Full caches (squad deleted, survivors respawned). While cached the
//     snapshot holds the squad values and exactly one survivor carry holds the holdout's;
//     a portable snapshot written and read back keeps both. After the wake the recreated
//     squad and the respawned holdout have every value back.
//  4. Native-save path: exported squad and soldier overrides bind back by persistence id
//     after being cleared (skipped, with a log line, when the world has no persistence id).
//     Mid-mission change: squad B ROE override set and cleared, applied at once.
//  5. Surrender: one casualty in B (module 0%): nobody surrenders. One casualty in A: the
//     four able soldiers surrender (100%, exact), the holdout (0%) stays in his squad.
//     Each prisoner carries A's interrogation overrides.
//  6. Interrogation after A's reveal override is changed to 0% (prisoners keep what their
//     squad had): prisoner 1 reveals squad B and points out the notebook; prisoner 2, given
//     his own reveal 0% / identity 100% while a prisoner, gives his identity; prisoner 3,
//     given his own reveal and identity 0%, refuses.
// Not covered: the GM dialog itself, clients and JIP (nothing replicates), Garrison Full
// caching (it keeps the squad entity and uses the same survivor carry), and a real native
// or CDF save and reload.
class EXPG_GarrisonGameplayClass : GenericEntityClass {}
class EXPG_GarrisonGameplay : GenericEntity
{
 static const float FIXTURE_SECONDS = 300;
 static const ResourceName MODULE = "{7F2E668385984EA1}PrefabsEditable/EXPSR/ESR_SurrenderModule.et";
 static const ResourceName SKILLS = "{48F68574BBAED1D5}PrefabsEditable/EXPBG/EGS_AIGlobalSkills.et";
 static const ResourceName SQUAD = "{E552DABF3636C2AD}Prefabs/Groups/OPFOR/Group_USSR_RifleSquad.et";
 static const ResourceName ZONE = "{7E1080ED8F0633FD}PrefabsEditable/EXPBG/EBG_CacheZone.et";
 static const ResourceName NOTEBOOK = "{23BD5CC4A9B2CC83}PrefabsEditable/EXPII/EII_NotebookBlue.et";
 static const int SIZE = 6;
 static const int RADIUS = 300;
 static ref array<vector> s_Presence;
 vector Origin = "4773.46 0 7094.57";
 vector ControlOffset = "150 0 0";
 vector NotebookOffset = "0 0 -40";
 vector ModuleOffset = "0 0 20";
 vector SkillsOffset = "0 0 25";
 ESR_SurrenderModule Surrender;
 EGS_Module Skills;
 SCR_AIGroup SquadA;
 SCR_AIGroup SquadB;
 SCR_ChimeraCharacter Holdout;
 EBG_CacheZone FullZone;
 EBG_CacheGroup Record;
 EBG_CacheMember HoldMember;
 IEntity Notebook;
 // Strong references: the records stay readable whatever the manager does with them.
 ref ESR_Prisoner FirstPrisoner;
 ref ESR_Prisoner SecondPrisoner;
 ref ESR_Prisoner ThirdPrisoner;
 ref array<UUID> SavedUnitIds = {};
 ref array<int> SavedUnitRoe = {};
 bool UnitBindExpected;
 int Phase;
 int Checks;
 int Failures;
 int Surrendered;
 int HeldOut;
 int ControlPrisoners = -1;
 int Interrogations;
 int IntelPointed;
 int RoeOk;
 int CachedOk;
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
  s_Presence = {};
  Started = Now(); Next = Started + 10; PhaseAt = Started;
  PrintFormat("[ESR OVERRIDE BEGIN] origin=%1 controlOffset=%2 radius=%3 deadline=%4", Origin, ControlOffset, RADIUS, FIXTURE_SECONDS);
 }

 bool Check(bool ok, string label)
 {
  Checks++;
  if (!ok) Failures++;
  PrintFormat("[ESR OVERRIDE CHECK] pass=%1 phase=%2 %3", ok, Phase, label);
  return ok;
 }

 void Finish(string reason)
 {
  if (Finished) return;
  Finished = true;
  if (s_Presence) s_Presence.Clear();
  ClearEventMask(EntityEvent.FRAME);
  string tail = string.Format(" roe=%1 cached=%2 reason=%3", RoeOk, CachedOk, reason);
  Print(string.Format("[ESR OVERRIDE RESULT] checks=%1 failures=%2 surrendered=%3 heldOut=%4 control=%5 interrogation=%6 intel=%7", Checks, Failures, Surrendered, HeldOut, ControlPrisoners, Interrogations, IntelPointed) + tail);
  GetGame().RequestClose();
 }

 void Advance(int phase)
 {
  Phase = phase;
  PhaseAt = Now();
 }

 bool Waited(float seconds, string label)
 {
  if (Now() - PhaseAt <= seconds) return false;
  Check(false, label);
  Finish("phase " + Phase.ToString());
  return true;
 }

 vector Ground(vector p, float lift)
 {
  p[1] = GetGame().GetWorld().GetSurfaceY(p[0], p[2]) + lift;
  return p;
 }

 IEntity Spawn(ResourceName prefab, vector p)
 {
  EntitySpawnParams spawn = new EntitySpawnParams();
  spawn.TransformMode = ETransformMode.WORLD;
  Math3D.MatrixIdentity4(spawn.Transform);
  spawn.Transform[3] = Ground(p, 0.3);
  // Keep the Resource and the spawned entity in locals before casting.
  Resource resource = Resource.Load(prefab);
  IEntity spawned = GetGame().SpawnEntityPrefab(resource, GetGame().GetWorld(), spawn);
  return spawned;
 }

 // Full mode (1), 60 m zone, 5 s sleep delay, debug messages on (as the dialog cache fixture).
 EBG_CacheZone SpawnZone(vector point)
 {
  EBG_CacheZone zone = EBG_CacheZone.Cast(Spawn(ZONE, point));
  if (!zone) return null;
  zone.SetValue(1, 1); zone.SetValue(2, 0); zone.SetValue(3, 60);
  zone.SetValue(4, 100); zone.SetValue(5, 300); zone.SetValue(12, 5);
  zone.SetValue(15, 0); zone.SetValue(18, 0); zone.SetValue(21, 1);
  zone.SetValue(0, 1);
  return zone;
 }

 void Configure()
 {
  Surrender.ApplySetting(ESR_Settings.ENABLED, ESR_Settings.FromBool(true));
  Surrender.ApplySetting(ESR_Settings.CHANCE, 0);
  Surrender.ApplySetting(ESR_Settings.THRESHOLD, 10);
  Surrender.ApplySetting(ESR_Settings.RANDOM, 0);
  Surrender.ApplySetting(ESR_Settings.REVEAL, 0);
  Surrender.ApplySetting(ESR_Settings.IDENTITY, 0);
  Surrender.ApplySetting(ESR_Settings.INTEL, 0);
  Surrender.ApplySetting(ESR_Settings.RADIUS, RADIUS);
  Surrender.ApplySetting(ESR_Settings.ATTEMPTS, 3);
  Surrender.ApplySetting(ESR_Settings.LIFETIME, 0);
  Surrender.ApplySetting(ESR_Settings.GRENADE, 0);
  Surrender.ApplySetting(ESR_Settings.DIAGNOSTICS, 1);
  EGS_Settings.WriteSetting(EGS_Settings.KEY_ROE, Vector(EGS_Settings.ROE_RETURN_FIRE, 0, 0));
 }

 SCR_EditableEntityComponent Editable(IEntity entity)
 {
  if (!entity) return null;
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(entity);
  return editable;
 }

 // Session-load write through a production attribute class (CDF restore contract).
 void WriteAttribute(SCR_BaseEditorAttribute attribute, IEntity entity, int entry)
 {
  attribute.WriteVariable(Editable(entity), SCR_BaseEditorAttributeVar.CreateInt(entry), null, -1);
 }

 // Session-save read: the entry index, or -1 when the attribute saves nothing.
 int ReadAttribute(SCR_BaseEditorAttribute attribute, IEntity entity)
 {
  SCR_BaseEditorAttributeVar var = attribute.ReadVariable(Editable(entity), null);
  if (!var) return -1;
  int entry = var.GetInt();
  return entry;
 }

 // A living member other than the leader and the excluded ones.
 SCR_ChimeraCharacter Member(SCR_AIGroup group, IEntity excluded, IEntity alsoExcluded)
 {
  if (!group) return null;
  array<AIAgent> agents = {};
  group.GetAgents(agents);
  IEntity leader = group.GetLeaderEntity();
  foreach (AIAgent agent : agents)
  {
   if (!agent) continue;
   SCR_ChimeraCharacter member = SCR_ChimeraCharacter.Cast(agent.GetControlledEntity());
   if (!member || member == leader || member == excluded || member == alsoExcluded || !member.GetCharacterController() || member.GetCharacterController().IsDead()) continue;
   return member;
  }
  return null;
 }

 int Living(SCR_AIGroup group)
 {
  if (!group) return 0;
  array<AIAgent> agents = {};
  group.GetAgents(agents);
  int living;
  foreach (AIAgent agent : agents)
  {
   if (!agent) continue;
   SCR_ChimeraCharacter member = SCR_ChimeraCharacter.Cast(agent.GetControlledEntity());
   if (member && member.GetCharacterController() && !member.GetCharacterController().IsDead()) living++;
  }
  return living;
 }

 int SoldierOverrides(SCR_AIGroup group)
 {
  if (!group) return 0;
  array<AIAgent> agents = {};
  group.GetAgents(agents);
  int count;
  foreach (AIAgent agent : agents)
  {
   if (!agent) continue;
   SCR_ChimeraCharacter member = SCR_ChimeraCharacter.Cast(agent.GetControlledEntity());
   if (member && (member.ESR_HasOverrides() || member.EGS_GetRoeOverride() != EGS_Settings.GROUP_ROE_DEFAULT)) count++;
  }
  return count;
 }

 // The group's external (GM, scenario, EXPBG) combat mode, -1 without a utility.
 int External(SCR_AIGroup group)
 {
  if (!group || !group.GetGroupUtilityComponent()) return -1;
  int mode = group.GetGroupUtilityComponent().GetCombatModeExternal();
  return mode;
 }

 SCR_AICombatComponent Combat(IEntity entity)
 {
  if (!entity) return null;
  SCR_AICombatComponent combat = SCR_AICombatComponent.Cast(entity.FindComponent(SCR_AICombatComponent));
  return combat;
 }

 void Kill(IEntity entity)
 {
  SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(entity);
  if (!character) return;
  SCR_CharacterDamageManagerComponent damage = SCR_CharacterDamageManagerComponent.Cast(character.GetDamageManager());
  if (damage) damage.Kill(Instigator.CreateInstigator(null));
 }

 bool SquadValuesOfA(SCR_AIGroup group)
 {
  if (!group) return false;
  return group.ESR_GetOverride(ESR_Overrides.SURRENDER) == 100 && group.ESR_GetOverride(ESR_Overrides.REVEAL) == 100 && group.ESR_GetOverride(ESR_Overrides.IDENTITY) == ESR_Overrides.UNSET && group.ESR_GetOverride(ESR_Overrides.INTEL) == 100;
 }

 bool ValuesOfA(array<int> values)
 {
  if (!values || values.Count() != ESR_Overrides.COUNT) return false;
  return values[0] == 100 && values[1] == 100 && values[2] == ESR_Overrides.UNSET && values[3] == 100;
 }

 bool ValuesOfHoldout(array<int> values)
 {
  if (!values || values.Count() != ESR_Overrides.COUNT) return false;
  return values[0] == 0 && values[1] == ESR_Overrides.UNSET && values[2] == ESR_Overrides.UNSET && values[3] == ESR_Overrides.UNSET;
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
   if (!Check(GetGame().GetPlayerManager().GetPlayerCount() == 0 && ESR_SurrenderManager.PrisonerCount() == 0 && EBG_CacheZone.Zones.IsEmpty(), "isolated server: no players, no prisoners, no cache zones")) { Finish("setup"); return; }
   Surrender = ESR_SurrenderModule.Cast(Spawn(MODULE, Origin + ModuleOffset));
   Skills = EGS_Module.Cast(Spawn(SKILLS, Origin + SkillsOffset));
   if (!Check(Surrender != null && Skills != null && EGS_Module.HasAny(), "AI Surrender and AI Global Skills module prefabs spawned")) { Finish("setup"); return; }
   Configure();
   Check(ESR_Settings.Get(ESR_Settings.CHANCE) == 0 && ESR_Settings.Get(ESR_Settings.INTEL) == 0 && EGS_Settings.GetRoe() == EGS_Settings.ROE_RETURN_FIRE, "module settings: surrender 0%, intel 0%, default ROE Return Fire Only");
   // Pure mappings of the spinbox and of the soldier ROE.
   Check(ESR_Overrides.ToEntry(ESR_Overrides.UNSET) == 0 && ESR_Overrides.FromEntry(0) == ESR_Overrides.UNSET && ESR_Overrides.ToEntry(0) == 1 && ESR_Overrides.FromEntry(1) == 0 && ESR_Overrides.ToEntry(100) == 21 && ESR_Overrides.FromEntry(21) == 100 && ESR_Overrides.FromEntry(ESR_Overrides.ToEntry(55)) == 55, "spinbox entries: 0 = module, 1 = 0%, 21 = 100%, step 5");
   Check(EGS_UnitRoe.Effective(EGS_Settings.GROUP_ROE_DEFAULT) == EGS_UnitRoe.FOLLOW && EGS_UnitRoe.Effective(EGS_Settings.GROUP_ROE_EXEMPT) == EGS_Settings.ROE_VANILLA && EGS_UnitRoe.Effective(EGS_Settings.ROE_WARNING_SHOTS) == EGS_Settings.ROE_WARNING_SHOTS, "soldier ROE: squad setting follows, exempt is vanilla, choices map 1:1");
   SquadA = SCR_AIGroup.Cast(Spawn(SQUAD, Origin));
   SquadB = SCR_AIGroup.Cast(Spawn(SQUAD, Origin + ControlOffset));
   Notebook = Spawn(NOTEBOOK, Origin + NotebookOffset);
   if (!Check(SquadA != null && SquadB != null && Notebook != null, "two native USSR squads and one intel notebook spawned")) { Finish("setup"); return; }
   Advance(10); return;
  }
  if (Phase == 10)
  {
   if (!SquadA || !SquadB || !SquadA.EBG_HasCompletedInitialSpawn() || SquadA.GetAgentsCount() != SIZE || SquadB.GetAgentsCount() != SIZE) { Waited(60, "both squads finished their initial spawn with six members"); return; }
   SetOverrides();
   Advance(11); return;
  }
  if (Phase == 11)
  {
   // Vanilla re-evaluates the group's actual combat mode on its own update.
   if (Now() - PhaseAt < 3) return;
   RoeChecks(SquadA, Holdout, "before caching");
   FullZone = SpawnZone(Origin);
   if (!Check(FullZone != null, "Full cache zone spawned over squad A")) { Finish("setup"); return; }
   Advance(20); return;
  }
  if (Phase == 20)
  {
   if (!SquadA) { Check(false, "squad A enrolled before it was cached"); Finish("enroll"); return; }
   Record = EBG_CacheManager.Get().FindGroup(SquadA);
   if (!Record || Record.Members.Count() != SIZE) { Waited(60, "squad A enrolled by the Full zone"); return; }
   HoldMember = EBG_CacheManager.Get().FindMember(Holdout);
   if (!Check(HoldMember != null && Record.Members.Contains(HoldMember), "holdout bound to the zone's logical member")) { Finish("enroll"); return; }
   Check(EBG_CacheManager.Get().FindGroup(SquadB) == null, "control squad B is outside the zone");
   Advance(21); return;
  }
  if (!Record && Phase >= 21 && Phase < 30) { Check(false, "logical cache record retained across the cycle"); Finish("record"); return; }
  if (Phase == 21)
  {
   if (!Record.Full || Record.Full.GetState() != EBG_FullGroupPhase.CACHED) { Waited(90, "squad A Full cached; reason='" + Record.Reason + "' recovery='" + Record.Recovery + "'"); return; }
   CachedChecks();
   Advance(22); return;
  }
  if (Phase == 22)
  {
   if (Now() - PhaseAt < 3) return;
   s_Presence.Insert(Ground(Origin, 1.8));
   Advance(23); return;
  }
  if (Phase == 23)
  {
   if (Record.Full || !Record.Group || Record.Group.GetAgentsCount() != SIZE || Record.Recovery != "") { Waited(60, "squad A woke from Full with six members; reason='" + Record.Reason + "' recovery='" + Record.Recovery + "'"); return; }
   WakeChecks();
   Advance(24); return;
  }
  if (Phase == 24)
  {
   if (Now() - PhaseAt < 3) return;
   if (RoeChecks(SquadA, Holdout, "after the wake")) RoeOk = 1;
   SaveChecks();
   Advance(25); return;
  }
  if (Phase == 25)
  {
   // Soldier overrides from a save bind on the AI Global Skills tick (2 s retries).
   if (Now() - PhaseAt < 5) return;
   if (UnitBindExpected) Check(Holdout && Holdout.EGS_GetRoeOverride() == EGS_Settings.ROE_RETURN_FIRE && Combat(Holdout) && Combat(Holdout).EGS_GetUnitRoe() == EGS_Settings.ROE_RETURN_FIRE, "saved soldier ROE override bound back by persistence id and applied");
   // One casualty in the control squad: module 0%, nobody surrenders.
   Kill(Member(SquadB, null, null));
   Advance(30); return;
  }
  if (Phase == 30)
  {
   if (Now() - PhaseAt < 4) return;
   ControlPrisoners = ESR_SurrenderManager.PrisonerCount();
   Check(ControlPrisoners == 0, "control squad B (module 0%) broke without a surrender");
   Kill(Member(SquadA, Holdout, null));
   Advance(31); return;
  }
  if (Phase == 31)
  {
   if (ESR_SurrenderManager.PrisonerCount() < SIZE - 2) { Waited(12, string.Format("four able soldiers of A surrendered (got %1)", ESR_SurrenderManager.PrisonerCount())); return; }
   // Weapon drops and sit-downs are scheduled; give them time.
   if (Now() - PhaseAt < 6) return;
   SurrenderChecks();
   Advance(32); return;
  }
  if (Phase == 32)
  {
   InterrogationChecks();
   Finish("complete");
   return;
  }
 }

 void SetOverrides()
 {
  Holdout = Member(SquadA, null, null);
  SCR_ChimeraCharacter plain = Member(SquadA, Holdout, null);
  if (!Check(Holdout && plain, "holdout and a plain soldier chosen in squad A")) { Finish("setup"); return; }
  ESR_GroupSurrenderAttribute groupSurrender = new ESR_GroupSurrenderAttribute();
  ESR_GroupRevealAttribute groupReveal = new ESR_GroupRevealAttribute();
  ESR_GroupIdentityAttribute groupIdentity = new ESR_GroupIdentityAttribute();
  ESR_GroupIntelAttribute groupIntel = new ESR_GroupIntelAttribute();
  ESR_UnitSurrenderAttribute unitSurrender = new ESR_UnitSurrenderAttribute();
  Check(groupSurrender.IsSerializable() && unitSurrender.IsSerializable(), "override attributes are serializable (attribute-based saves keep them)");
  Check(ReadAttribute(groupSurrender, SquadA) == -1 && ReadAttribute(unitSurrender, Holdout) == -1, "nothing set: nothing saved");
  WriteAttribute(groupSurrender, SquadA, ESR_Overrides.ToEntry(100));
  WriteAttribute(groupReveal, SquadA, ESR_Overrides.ToEntry(100));
  WriteAttribute(groupIntel, SquadA, ESR_Overrides.ToEntry(100));
  WriteAttribute(unitSurrender, Holdout, ESR_Overrides.ToEntry(0));
  Check(SquadValuesOfA(SquadA), "squad A: surrender 100%, reveal 100%, identity module, intel 100%");
  Check(Holdout.ESR_GetOverride(ESR_Overrides.SURRENDER) == 0 && !plain.ESR_HasOverrides(), "holdout surrender 0%; the plain soldier has none");
  Check(ReadAttribute(groupSurrender, SquadA) == 21 && ReadAttribute(groupIdentity, SquadA) == -1 && ReadAttribute(unitSurrender, Holdout) == 1 && ReadAttribute(unitSurrender, plain) == -1, "saved reads: set values as entries, unset ones absent");
  // A squad attribute never reads a soldier and the other way round.
  Check(ReadAttribute(groupSurrender, Holdout) == -1 && ReadAttribute(unitSurrender, SquadA) == -1, "squad and soldier attributes keep to their own targets");
  int source;
  int plainChance = ESR_Overrides.Resolve(plain, SquadA, ESR_Overrides.SURRENDER, source);
  Check(plainChance == 100 && source == ESR_Overrides.SOURCE_SQUAD, "plain soldier of A: 100% from the squad");
  int holdChance = ESR_Overrides.Resolve(Holdout, SquadA, ESR_Overrides.SURRENDER, source);
  Check(holdChance == 0 && source == ESR_Overrides.SOURCE_SOLDIER, "holdout: 0% from himself");
  SCR_ChimeraCharacter control = Member(SquadB, null, null);
  int controlChance = ESR_Overrides.Resolve(control, SquadB, ESR_Overrides.SURRENDER, source);
  Check(controlChance == 0 && source == ESR_Overrides.SOURCE_MODULE, "squad B: 0% from the module");
  // A module change reaches squads without an override through the per-squad cache.
  Surrender.ApplySetting(ESR_Settings.CHANCE, 35);
  int changed = ESR_Overrides.Resolve(control, SquadB, ESR_Overrides.SURRENDER, source);
  Surrender.ApplySetting(ESR_Settings.CHANCE, 0);
  int restored = ESR_Overrides.Resolve(control, SquadB, ESR_Overrides.SURRENDER, source);
  Check(changed == 35 && restored == 0, "per-squad cache follows module changes (35% then 0%)");
  // ROE: squad A Fire on Sight (production setter), holdout Return Fire Only (saver path).
  EGS_Manager.SetGroupRoe(SquadA, EGS_Settings.ROE_FIRE_ON_SIGHT);
  EGS_SavedUnitRoeAttribute savedUnitRoe = new EGS_SavedUnitRoeAttribute();
  WriteAttribute(savedUnitRoe, Holdout, EGS_Settings.ROE_RETURN_FIRE);
  Check(SquadA.EGS_GetRoeOverride() == EGS_Settings.ROE_FIRE_ON_SIGHT && Holdout.EGS_GetRoeOverride() == EGS_Settings.ROE_RETURN_FIRE && ReadAttribute(savedUnitRoe, Holdout) == EGS_Settings.ROE_RETURN_FIRE && ReadAttribute(savedUnitRoe, plain) == -1, "squad A ROE Fire on Sight; holdout ROE Return Fire Only, saved; plain soldier none");
 }

 bool RoeChecks(SCR_AIGroup squad, SCR_ChimeraCharacter holdout, string stage)
 {
  SCR_ChimeraCharacter mate = Member(squad, holdout, null);
  SCR_AICombatComponent holdCombat = Combat(holdout);
  SCR_AICombatComponent mateCombat = Combat(mate);
  int holdMode = -1;
  int mateMode = -1;
  int holdRoe = -2;
  int mateRoe = -2;
  if (holdCombat) { holdMode = holdCombat.GetCombatMode(); holdRoe = holdCombat.EGS_GetUnitRoe(); }
  if (mateCombat) { mateMode = mateCombat.GetCombatMode(); mateRoe = mateCombat.EGS_GetUnitRoe(); }
  PrintFormat("[ESR OVERRIDE ROE] stage=%1 externalA=%2 externalB=%3 holdRoe=%4 holdMode=%5 mateRoe=%6 mateMode=%7", stage, External(squad), External(SquadB), holdRoe, holdMode, mateRoe, mateMode);
  bool ok = Check(squad && squad.EGS_GetAppliedRoe() == EGS_Settings.ROE_FIRE_ON_SIGHT && External(squad) == EAIGroupCombatMode.FIRE_AT_WILL, "squad A Fire on Sight: FIRE_AT_WILL " + stage);
  ok = Check(External(SquadB) == EAIGroupCombatMode.RETURN_FIRE, "squad B module default: RETURN_FIRE " + stage) && ok;
  ok = Check(holdRoe == EGS_Settings.ROE_RETURN_FIRE && holdMode == EAIGroupCombatMode.HOLD_FIRE, "holdout's own Return Fire Only: HOLD_FIRE while unthreatened " + stage) && ok;
  // Vanilla refreshes the group's actual mode on its own update; the mate must follow it.
  int actual = -1;
  if (squad && squad.GetGroupUtilityComponent()) actual = squad.GetGroupUtilityComponent().GetCombatModeActual();
  PrintFormat("[ESR OVERRIDE ROE] stage=%1 actualA=%2", stage, actual);
  ok = Check(mateRoe == EGS_UnitRoe.FOLLOW && mateMode == actual && mateMode != EAIGroupCombatMode.HOLD_FIRE, "squad mate follows the squad's actual mode, not hold fire " + stage) && ok;
  return ok;
 }

 void CachedChecks()
 {
  EBG_PrefabFullCache full = EBG_PrefabFullCache.Cast(Record.Full);
  Check(!SquadA && !Holdout, "Full capture deleted squad A and its soldiers");
  if (!Check(full && full.GetMemberCount() == SIZE, "Full transaction holds six survivor rows")) { Finish("cached"); return; }
  int carriedEsr;
  int carriedEgs;
  bool holdoutRow;
  for (int i = 0; i < full.GetMemberCount(); i++)
  {
   EBG_SurvivorCarry carry = full.GetSurvivorCarry(i);
   if (!carry) continue;
   if (carry.ESR_GetOverrides()) carriedEsr++;
   if (carry.EGS_GetRoe() != EGS_Settings.GROUP_ROE_DEFAULT) carriedEgs++;
   if (ValuesOfHoldout(carry.ESR_GetOverrides()) && carry.EGS_GetRoe() == EGS_Settings.ROE_RETURN_FIRE) holdoutRow = true;
  }
  PrintFormat("[ESR OVERRIDE CACHED] rows=%1 carriedSurrender=%2 carriedRoe=%3 snapshotRoe=%4", full.GetMemberCount(), carriedEsr, carriedEgs, full.EGS_GetSnapshotSquadRoe());
  bool ok = Check(carriedEsr == 1 && carriedEgs == 1 && holdoutRow, "exactly one survivor carry holds the holdout's surrender 0% and ROE");
  ok = Check(ValuesOfA(full.ESR_GetSnapshotSquadOverrides()) && full.EGS_GetSnapshotSquadRoe() == EGS_Settings.ROE_FIRE_ON_SIGHT, "the squad snapshot holds A's surrender values and ROE") && ok;
  // Portable snapshot (CDF Full export): written and read back by production.
  JsonSaveContext save = new JsonSaveContext();
  bool wrote = full.WriteSnapshot(save);
  string json;
  if (wrote) json = save.SaveToString();
  SCR_PersistenceJsonLoadContext load = new SCR_PersistenceJsonLoadContext();
  bool loaded = wrote && load.LoadFromString(json);
  EBG_DurableFullCache copy = new EBG_DurableFullCache();
  bool read = loaded && copy.ReadSnapshot(load);
  int copyEsr;
  int copyEgs;
  if (read)
  {
   for (int j = 0; j < copy.GetMemberCount(); j++)
   {
    EBG_SurvivorCarry copied = copy.GetSurvivorCarry(j);
    if (copied && ValuesOfHoldout(copied.ESR_GetOverrides())) copyEsr++;
    if (copied && copied.EGS_GetRoe() == EGS_Settings.ROE_RETURN_FIRE) copyEgs++;
   }
  }
  PrintFormat("[ESR OVERRIDE PORTABLE] wrote=%1 read=%2 keys=%3/%4/%5/%6 survivorsSurrender=%7 survivorsRoe=%8", wrote, read, json.Contains("esrSquadOverrides"), json.Contains("esrSurvivorOverrides"), json.Contains("egsRoe"), json.Contains("egsSurvivorRoe"), copyEsr, copyEgs);
  ok = Check(read && ValuesOfA(copy.ESR_GetSnapshotSquadOverrides()) && copy.EGS_GetSnapshotSquadRoe() == EGS_Settings.ROE_FIRE_ON_SIGHT && copyEsr == 1 && copyEgs == 1, "portable snapshot keeps squad and soldier values through write and read") && ok;
  if (ok) CachedOk = 1;
 }

 void WakeChecks()
 {
  SquadA = Record.Group;
  Holdout = HoldMember.Entity;
  bool alive = Holdout && Holdout.GetCharacterController() && !Holdout.GetCharacterController().IsDead();
  Check(alive && Holdout.GetCharacterGroup() == SquadA, "holdout respawned into the recreated squad");
  bool ok = Check(SquadValuesOfA(SquadA) && SquadA.EGS_GetRoeOverride() == EGS_Settings.ROE_FIRE_ON_SIGHT, "recreated squad has A's surrender values and ROE back");
  ok = Check(alive && Holdout.ESR_GetOverride(ESR_Overrides.SURRENDER) == 0 && Holdout.EGS_GetRoeOverride() == EGS_Settings.ROE_RETURN_FIRE, "respawned holdout has surrender 0% and ROE Return Fire Only back") && ok;
  ok = Check(SoldierOverrides(SquadA) == 1, "no other respawned soldier took an override") && ok;
  if (!ok) CachedOk = 0;
 }

 void SaveChecks()
 {
  // Mid-mission change: squad B ROE override set, applied at once, then cleared.
  EGS_Manager.SetGroupRoe(SquadB, EGS_Settings.ROE_FIRE_ON_SIGHT);
  int setMode = External(SquadB);
  EGS_Manager.SetGroupRoe(SquadB, EGS_Settings.GROUP_ROE_DEFAULT);
  int clearedMode = External(SquadB);
  if (!Check(setMode == EAIGroupCombatMode.FIRE_AT_WILL && clearedMode == EAIGroupCombatMode.RETURN_FIRE, "squad B ROE changed mid-mission: Fire on Sight at once, module default again when cleared")) RoeOk = 0;
  // Native save path of the surrender overrides: export, clear, import, bind by id.
  PersistenceSystem persistence = PersistenceSystem.GetInstance();
  UUID squadId;
  if (persistence) squadId = persistence.GetId(SquadB);
  if (!persistence || squadId.IsNull())
  {
   Print("[ESR OVERRIDE SAVE] skipped: no persistence id for squad B in this world");
   return;
  }
  ESR_Overrides.SetGroup(SquadB, ESR_Overrides.SURRENDER, 55, "fixture");
  array<UUID> groupIds = {};
  array<int> groupValues = {};
  array<UUID> unitIds = {};
  array<int> unitValues = {};
  ESR_Overrides.Export(groupIds, groupValues, unitIds, unitValues);
  int row = groupIds.Find(squadId);
  Check(row >= 0 && groupValues[row * ESR_Overrides.COUNT] == 55 && unitIds.Count() >= 1, "export holds squad B (55%) and the holdout by persistence id");
  ESR_Overrides.SetGroup(SquadB, ESR_Overrides.SURRENDER, ESR_Overrides.UNSET, "fixture");
  ESR_Overrides.Import(groupIds, groupValues, unitIds, unitValues);
  Check(SquadB.ESR_GetOverride(ESR_Overrides.SURRENDER) == 55 && ESR_Overrides.PendingCount() == 0 && SquadValuesOfA(SquadA) && Holdout.ESR_GetOverride(ESR_Overrides.SURRENDER) == 0, "import binds every saved override back at once");
  ESR_Overrides.SetGroup(SquadB, ESR_Overrides.SURRENDER, ESR_Overrides.UNSET, "fixture");
  // Soldier ROE: export, clear, import; binds on the AI Global Skills tick.
  EGS_Manager.ExportUnitOverrides(SavedUnitIds, SavedUnitRoe);
  UUID holdId = persistence.GetId(Holdout);
  UnitBindExpected = !holdId.IsNull() && SavedUnitIds.Find(holdId) >= 0;
  Check(UnitBindExpected, "soldier ROE export holds the holdout by persistence id");
  if (!UnitBindExpected) return;
  EGS_Manager.SetUnitRoe(Holdout, EGS_Settings.GROUP_ROE_DEFAULT);
  Check(Holdout.EGS_GetRoeOverride() == EGS_Settings.GROUP_ROE_DEFAULT && Combat(Holdout).EGS_GetUnitRoe() == EGS_UnitRoe.FOLLOW, "holdout ROE cleared: he follows his squad");
  EGS_Manager.ImportUnitOverrides(SavedUnitIds, SavedUnitRoe);
 }

 void SurrenderChecks()
 {
  Surrendered = ESR_SurrenderManager.PrisonerCount();
  Check(Surrendered == SIZE - 2, "exactly the four able soldiers without an override surrendered (100%, exact)");
  bool holdFree = Holdout && !ESR_SurrenderManager.FindPrisoner(Holdout) && Holdout.GetCharacterGroup() == SquadA && Holdout.GetCharacterController() && !Holdout.GetCharacterController().IsDead();
  if (Check(holdFree, "holdout (0%) stayed in his squad")) HeldOut = 1;
  int carrying;
  for (int i = 0; i < Surrendered; i++)
  {
   ESR_Prisoner prisoner = ESR_SurrenderManager.GetPrisonerAt(i);
   if (prisoner && ValuesOfA(prisoner.SquadOverrides) && prisoner.Group == SquadA) carrying++;
  }
  Check(carrying == Surrendered, "every prisoner carries A's overrides from the moment he surrendered");
  FirstPrisoner = ESR_SurrenderManager.GetPrisonerAt(0);
  SecondPrisoner = ESR_SurrenderManager.GetPrisonerAt(1);
  ThirdPrisoner = ESR_SurrenderManager.GetPrisonerAt(2);
 }

 void InterrogationChecks()
 {
  if (!Check(FirstPrisoner && FirstPrisoner.Point && SecondPrisoner && SecondPrisoner.Point && ThirdPrisoner && ThirdPrisoner.Point, "three prisoners with interrogation points")) return;
  // The squad's reveal changes after they surrendered: prisoners keep what it was.
  ESR_GroupRevealAttribute groupReveal = new ESR_GroupRevealAttribute();
  WriteAttribute(groupReveal, SquadA, ESR_Overrides.ToEntry(0));
  Check(SquadA.ESR_GetOverride(ESR_Overrides.REVEAL) == 0 && FirstPrisoner.SquadOverrides[ESR_Overrides.REVEAL] == 100, "squad A reveal now 0%; prisoners keep 100%");
  int controlLiving = Living(SquadB);
  int first = ESR_SurrenderManager.Interrogate(FirstPrisoner.Point, FirstPrisoner.Point, 0);
  PrintFormat("[ESR OVERRIDE FIRST] outcome=%1 count=%2 expected=%3 intel=%4", first, FirstPrisoner.RevealCount, controlLiving, FirstPrisoner.IntelDistances.Count());
  if (Check(first == ESR_SurrenderManager.OUTCOME_REVEAL && FirstPrisoner.RevealCount == controlLiving, "prisoner 1 (squad reveal 100%) reveals squad B")) Interrogations++;
  if (Check(FirstPrisoner.IntelDistances.Count() == 1, "prisoner 1 (squad intel 100%) points out the notebook")) IntelPointed = FirstPrisoner.IntelDistances.Count();
  // His own values, set while he is a prisoner, win over the squad's.
  ESR_UnitRevealAttribute unitReveal = new ESR_UnitRevealAttribute();
  ESR_UnitIdentityAttribute unitIdentity = new ESR_UnitIdentityAttribute();
  WriteAttribute(unitReveal, SecondPrisoner.Character, ESR_Overrides.ToEntry(0));
  WriteAttribute(unitIdentity, SecondPrisoner.Character, ESR_Overrides.ToEntry(100));
  int second = ESR_SurrenderManager.Interrogate(SecondPrisoner.Point, SecondPrisoner.Point, 0);
  PrintFormat("[ESR OVERRIDE SECOND] outcome=%1 chances=%2", second, ESR_Overrides.DescribePrisoner(SecondPrisoner));
  if (Check(second == ESR_SurrenderManager.OUTCOME_IDENTITY, "prisoner 2 (own reveal 0%, identity 100%) gives his identity")) Interrogations++;
  WriteAttribute(unitReveal, ThirdPrisoner.Character, ESR_Overrides.ToEntry(0));
  WriteAttribute(unitIdentity, ThirdPrisoner.Character, ESR_Overrides.ToEntry(0));
  int third = ESR_SurrenderManager.Interrogate(ThirdPrisoner.Point, ThirdPrisoner.Point, 0);
  PrintFormat("[ESR OVERRIDE THIRD] outcome=%1 chances=%2 intel=%3", third, ESR_Overrides.DescribePrisoner(ThirdPrisoner), ThirdPrisoner.IntelDistances.Count());
  if (Check(third == ESR_SurrenderManager.OUTCOME_REFUSED && ThirdPrisoner.IntelDistances.IsEmpty(), "prisoner 3 (own reveal and identity 0%) refuses, so no intel is rolled")) Interrogations++;
 }
}
// Presence seam, as in the dialog cache, wake budget and unit-cleanup fixtures.
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

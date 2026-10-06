// TEST ONLY. EXPBG Unit Scripts native gameplay fixture.
// pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/EUS_UnitScriptsGameplay.c -TimeoutSeconds 420 -ExpectResult '\[EUS TEST RESULT\] checks=[1-9]\d* failures=0 reason=completed' -OrchestratorSlotGranted
// The runner copies this file to EXPG_GarrisonGameplay.c; the class names are fixed.
// -ExpectResult judges the run by exactly one passing result line and no script errors.
// Real US fire team on GM Eden, production EUS_Manager entry points (the same ones
// the attributes and context actions call), native damage, a native Move waypoint.
// It also reads the packed (merged) editor configs: EXPBG Night discipline is a
// Group tab attribute, the unit script attributes stay in the EXPBG Unit Scripts
// tab and right-click keeps only Hold Position, Freeze and Release.
// No players, no GM UI, no possession, no save/load: those stay client-test gates.
class EXPG_GarrisonGameplayClass : GenericEntityClass {}
class EXPG_GarrisonGameplay : GenericEntity
{
 static const float FIXTURE_SECONDS = 260;
 static const ResourceName SQUAD = "{84E5BBAB25EA23E5}Prefabs/Groups/BLUFOR/Group_US_FireTeam.et";
 static const ResourceName MOVE = "{750A8D1695BD6998}Prefabs/AI/Waypoints/AIWaypoint_Move.et";
 static const ResourceName ATTRIBUTE_LIST = "{F3D6C6D25642352C}Configs/Editor/AttributeLists/Edit.conf";
 static const ResourceName CONTEXT_ACTIONS = "{2C1D87DE93C77A27}Configs/Editor/ActionLists/Context/TempEdit.conf";
 static const string GROUP_TAB = "{C952695559974DF0}Configs/Editor/AttributeCategories/Group.conf";
 static const string UNIT_SCRIPTS_TAB = "{4F3669E88510C238}Configs/Editor/AttributeCategories/EUS_UnitScripts.conf";
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
 SCR_AIGroup Group;
 SCR_ChimeraCharacter Leader;
 SCR_ChimeraCharacter Holder;
 SCR_ChimeraCharacter Frozen;
 SCR_ChimeraCharacter Animated;
 vector LeaderStart;
 vector HolderAnchor;
 vector FrozenAnchor;
 vector AnimatedAnchor;
 ECharacterStance FrozenStance;
 float FrozenPerception = -1;
 float HolderMax;
 float FrozenMax;
 float AnimatedMax;
 float LeaderMax;
 float SmokeSince = -1;
 bool StanceRequested;
 bool StanceProbeAdded;
 float StanceProbeAt = -1;
 float CrouchLatency = -1;
 ref SCR_AICharacterStanceSetting HolderStanceProbe;
 ref SCR_AICharacterStanceSetting FrozenStanceProbe;
 bool HolderCrouched;
 bool FrozenStanceChanged;
 bool LoiterLost;

 void EXPG_GarrisonGameplay(IEntitySource src, IEntity parent) { SetEventMask(EntityEvent.INIT | EntityEvent.FRAME); }
 float Now() { return GetGame().GetWorld().GetWorldTime() * 0.001; }

 override void EOnInit(IEntity owner)
 {
  if (!Replication.IsServer()) { ClearEventMask(EntityEvent.FRAME); return; }
  Started = Now();
  Next = Started + 10;
  PrintFormat("[EUS TEST BEGIN] squad=%1 origin=%2 deadline=%3", SQUAD, Origin, FIXTURE_SECONDS);
 }

 bool Check(bool ok, string label)
 {
  Checks++;
  if (!ok) Failures++;
  PrintFormat("[EUS TEST CHECK] pass=%1 %2", ok, label);
  return ok;
 }

 void Finish(string reason)
 {
  if (Finished) return;
  Finished = true;
  ClearEventMask(EntityEvent.FRAME);
  PrintFormat("[EUS TEST RESULT] checks=%1 failures=%2 reason=%3", Checks, Failures, reason);
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

 SCR_AIUtilityComponent Utility(SCR_ChimeraCharacter actor)
 {
  if (!actor || !actor.GetAIControlComponent() || !actor.GetAIControlComponent().GetAIAgent()) return null;
  return SCR_AIUtilityComponent.Cast(actor.GetAIControlComponent().GetAIAgent().FindComponent(SCR_AIUtilityComponent));
 }

 bool HasOwnSpeed(SCR_ChimeraCharacter actor)
 {
  SCR_AICharacterSettingsComponent settings = SCR_AICharacterSettingsComponent.FindOnControlledEntity(actor);
  return settings && EUS_SpeedSetting.Cast(settings.GetCurrentSetting(SCR_AICharacterMovementSpeedSettingBase)) != null;
 }

 bool HasOwnStance(SCR_ChimeraCharacter actor)
 {
  SCR_AICharacterSettingsComponent settings = SCR_AICharacterSettingsComponent.FindOnControlledEntity(actor);
  return settings && EUS_StanceSetting.Cast(settings.GetCurrentSetting(SCR_AICharacterStanceSettingBase)) != null;
 }

 bool HasOwnLights(SCR_ChimeraCharacter actor)
 {
  SCR_AICharacterSettingsComponent settings = SCR_AICharacterSettingsComponent.FindOnControlledEntity(actor);
  return settings && EUS_LightSetting.Cast(settings.GetCurrentSetting(SCR_AICharacterLightInteractionSettingBase)) != null;
 }

 float Moved(SCR_ChimeraCharacter actor, vector anchor)
 {
  if (!actor) return -1;
  return vector.DistanceXZ(actor.GetOrigin(), anchor);
 }

 bool Smoking(SCR_ChimeraCharacter actor)
 {
  if (!actor) return false;
  SCR_CharacterControllerComponent controller = SCR_CharacterControllerComponent.Cast(actor.GetCharacterController());
  if (!controller) return false;
  SCR_CharacterCommandHandlerComponent handler = SCR_CharacterCommandHandlerComponent.Cast(controller.GetAnimationComponent().GetCommandHandler());
  ELoiteringType type;
  return handler && handler.IsLoitering(type) && type == ELoiteringType.SMOKING;
 }

 // Members of the squad that are alive; counts slotted flashlights and lit ones.
 int Lights(out int total, out int lit, out int held)
 {
  total = 0;
  lit = 0;
  held = 0;
  int living;
  array<AIAgent> agents = {};
  Group.GetAgents(agents);
  foreach (AIAgent agent : agents)
  {
   SCR_ChimeraCharacter member = SCR_ChimeraCharacter.Cast(agent.GetControlledEntity());
   if (!member || member.GetCharacterController().IsDead()) continue;
   living++;
   int memberTotal;
   int memberLit;
   EUS_DisciplineRecord.CountLights(member, memberTotal, memberLit);
   total += memberTotal;
   lit += memberLit;
   if (HasOwnLights(member)) held++;
  }
  return living;
 }

 // The packed editor configs, i.e. the shared lists every module appends to. Each
 // Resource stays in a local while its containers are read.
 void CheckEditorConfigs()
 {
  int attributeCount = 0;
  int scriptAttributesInTab = 0;
  string disciplineTab = string.Empty;
  string disciplineEntries = string.Empty;
  Resource attributeResource = Resource.Load(ATTRIBUTE_LIST);
  BaseResourceObject attributeObject;
  if (attributeResource && attributeResource.IsValid()) attributeObject = attributeResource.GetResource();
  BaseContainer attributeRoot;
  if (attributeObject) attributeRoot = attributeObject.ToBaseContainer();
  BaseContainerList attributeEntries;
  if (attributeRoot) attributeEntries = attributeRoot.GetObjectArray("m_aAttributes");
  if (attributeEntries) attributeCount = attributeEntries.Count();
  if (attributeCount > 4096) attributeCount = 4096;
  for (int attributeIndex = 0; attributeIndex < attributeCount; attributeIndex++)
  {
   BaseContainer attributeEntry = attributeEntries.Get(attributeIndex);
   if (!attributeEntry) continue;
   string attributeClass = attributeEntry.GetClassName();
   ResourceName attributeTab = ResourceName.Empty;
   attributeEntry.Get("m_CategoryConfig", attributeTab);
   string attributeTabPath = attributeTab;
   if (attributeClass == "EUS_UnitScriptAttribute" || attributeClass == "EUS_SquadScriptAttribute")
   {
    if (attributeTabPath == UNIT_SCRIPTS_TAB) scriptAttributesInTab++;
    continue;
   }
   if (attributeClass != "EUS_DisciplineAttribute") continue;
   disciplineTab = attributeTabPath;
   BaseContainerList modeEntries = attributeEntry.GetObjectArray("m_aValues");
   int modeCount = 0;
   if (modeEntries) modeCount = modeEntries.Count();
   if (modeCount > 16) modeCount = 16;
   for (int modeIndex = 0; modeIndex < modeCount; modeIndex++)
   {
    BaseContainer modeEntry = modeEntries.Get(modeIndex);
    if (!modeEntry) continue;
    string modeName = string.Empty;
    float modeValue = 0;
    modeEntry.Get("m_sEntryName", modeName);
    modeEntry.Get("m_fEntryFloatValue", modeValue);
    int modeCode = Math.Round(modeValue);
    disciplineEntries += string.Format("%1=%2;", modeName, modeCode);
   }
  }

  int actionCount = 0;
  int quickActions = 0;
  int otherActions = 0;
  Resource actionResource = Resource.Load(CONTEXT_ACTIONS);
  BaseResourceObject actionObject;
  if (actionResource && actionResource.IsValid()) actionObject = actionResource.GetResource();
  BaseContainer actionRoot;
  if (actionObject) actionRoot = actionObject.ToBaseContainer();
  BaseContainerList actionEntries;
  if (actionRoot) actionEntries = actionRoot.GetObjectArray("m_Actions");
  if (actionEntries) actionCount = actionEntries.Count();
  if (actionCount > 4096) actionCount = 4096;
  for (int actionIndex = 0; actionIndex < actionCount; actionIndex++)
  {
   BaseContainer actionEntry = actionEntries.Get(actionIndex);
   if (!actionEntry) continue;
   string actionClass = actionEntry.GetClassName();
   if (actionClass.IndexOf("EUS_") != 0) continue;
   if (actionClass == "EUS_HoldContextAction" || actionClass == "EUS_FreezeContextAction" || actionClass == "EUS_ReleaseContextAction") quickActions++;
   else otherActions++;
  }

  PrintFormat("[EUS TEST CONFIG] attributes=%1 disciplineTab='%2' disciplineEntries='%3' unitScriptAttributesInTab=%4 contextActions=%5 eusQuickActions=%6 otherEusActions=%7", attributeCount, disciplineTab, disciplineEntries, scriptAttributesInTab, actionCount, quickActions, otherActions);
  Check(disciplineTab == GROUP_TAB, "EXPBG Night discipline is an attribute of the vanilla Group tab");
  Check(disciplineEntries == "None=0;Light discipline=1;Terror tactics=2;", "EXPBG Night discipline offers None, Light discipline and Terror tactics");
  Check(scriptAttributesInTab == 2, "unit and squad script attributes (animations) stay in the EXPBG Unit Scripts tab");
  Check(quickActions == 3 && otherActions == 0, "right-click keeps only EXPBG Hold Position, Freeze and Release Unit Scripts");
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
   CheckEditorConfigs();
   // Keep Resource and spawned entity in locals before casting (inline form returned null natively).
   Resource squad = Resource.Load(SQUAD);
   IEntity squadEntity = GetGame().SpawnEntityPrefab(squad, GetGame().GetWorld(), Params(Origin));
   Group = SCR_AIGroup.Cast(squadEntity);
   if (!Check(Group != null, "native US fire team spawned")) { Finish("setup"); return; }
   Advance(1);
   return;
  }
  if (Phase == 1)
  {
   if (Group.GetAgentsCount() < 4)
   {
    if (Now() - PhaseAt > 30) { Check(false, "fire team spawned 4 AI members"); Finish("setup"); }
    return;
   }
   if (Now() - PhaseAt < 4) return;
   Leader = SCR_ChimeraCharacter.Cast(Group.GetLeaderEntity());
   array<AIAgent> agents = {};
   Group.GetAgents(agents);
   foreach (AIAgent agent : agents)
   {
    SCR_ChimeraCharacter member = SCR_ChimeraCharacter.Cast(agent.GetControlledEntity());
    if (!member || member == Leader) continue;
    if (!Holder) Holder = member;
    else if (!Frozen) Frozen = member;
    else if (!Animated) Animated = member;
   }
   if (!Check(Leader != null && Holder != null && Frozen != null && Animated != null, "leader plus three followers identified")) { Finish("setup"); return; }
   Advance(2);
   return;
  }
  if (Phase == 2)
  {
   Manager = EUS_Manager.Get();
   if (!Check(Manager != null, "server unit script manager available")) { Finish("setup"); return; }
   SCR_AIUtilityComponent frozenUtility = Utility(Frozen);
   if (frozenUtility && frozenUtility.m_CombatComponent) FrozenPerception = frozenUtility.m_CombatComponent.GetPerceptionFactor();
   FrozenStance = Frozen.GetCharacterController().GetStance();
   Check(Manager.ApplyUnit(Holder, EUS_Codes.HOLD, Report), "Hold position bound");
   Check(Manager.ApplyUnit(Frozen, EUS_Codes.FREEZE, Report), "Freeze bound");
   Check(Manager.ApplyUnit(Animated, EUS_Codes.ANIMATION + 2, Report), "Animation (Smoke) bound");
   Check(Holder.EUS_Script == EUS_Codes.HOLD && Frozen.EUS_Script == EUS_Codes.FREEZE && Animated.EUS_Script == EUS_Codes.ANIMATION + 2, "replicated script codes set on the server");
   Check(HasOwnSpeed(Holder) && HasOwnSpeed(Frozen) && HasOwnSpeed(Animated), "owned IDLE movement setting is the current native speed setting");
   Check(!HasOwnStance(Holder) && HasOwnStance(Frozen) && HasOwnStance(Animated), "stance locked for Freeze and Animation only");
   if (FrozenPerception > 0) Check(frozenUtility.m_CombatComponent.GetPerceptionFactor() == 0, "Freeze zeroes visual perception");
   Check(Group.EUS_Scripted == 3 && EUS_Manager.Reserves(Group), "squad reports 3 scripted members and is reserved from Unit Caching");
   Check(!Manager.ApplyUnit(Holder, EUS_Codes.MIXED, Report) && Report.Refused == 1, "unknown script code refused");

   // Combat move filter: a hold turns movement into a stance change at the spot;
   // a freeze turns any request into a stop without aiming.
   SCR_AIUtilityComponent holderUtility = Utility(Holder);
   if (!Check(holderUtility != null && frozenUtility != null && holderUtility.m_CombatMoveState != null && frozenUtility.m_CombatMoveState != null, "native combat move states available")) { Finish("setup"); return; }
   SCR_AICombatMoveRequest_Move move = new SCR_AICombatMoveRequest_Move();
   move.m_vMovePos = Holder.GetOrigin() + Vector(30, 0, 0);
   move.m_eDirection = SCR_EAICombatMoveDirection.CUSTOM_POS;
   move.m_eStanceMoving = ECharacterStance.STAND;
   move.m_eStanceEnd = Holder.GetCharacterController().GetStance();
   move.m_eMovementType = EMovementType.RUN;
   holderUtility.m_CombatMoveState.ApplyNewRequest(move);
   Check(SCR_AICombatMoveRequest_ChangeStance.Cast(holderUtility.m_CombatMoveState.GetRequest()) != null && move.m_eState == SCR_EAICombatMoveRequestState.CANCELED, "Hold converts a combat move into a stance change");
   SCR_AICombatMoveRequest_ChangeStance prone = new SCR_AICombatMoveRequest_ChangeStance();
   prone.m_eStance = ECharacterStance.PRONE;
   prone.m_bAimAtTarget = true;
   prone.m_bAimAtTargetEnd = true;
   frozenUtility.m_CombatMoveState.ApplyNewRequest(prone);
   SCR_AICombatMoveRequestBase frozenRequest = frozenUtility.m_CombatMoveState.GetRequest();
   Check(SCR_AICombatMoveRequest_Stop.Cast(frozenRequest) != null && !frozenRequest.m_bAimAtTargetEnd && !frozenUtility.m_CombatMoveState.m_bAimAtTarget, "Freeze converts a stance request into a stop without aiming");

   Resource waypointResource = Resource.Load(MOVE);
   IEntity waypointEntity = GetGame().SpawnEntityPrefab(waypointResource, GetGame().GetWorld(), Params(Origin + Vector(40, 0, 0)));
   AIWaypoint waypoint = AIWaypoint.Cast(waypointEntity);
   if (!Check(waypoint != null, "native Move waypoint spawned 40 m away")) { Finish("waypoint"); return; }
   Group.AddWaypoint(waypoint);
   LeaderStart = Leader.GetOrigin();
   HolderAnchor = Holder.GetOrigin();
   FrozenAnchor = Frozen.GetOrigin();
   AnimatedAnchor = Animated.GetOrigin();
   Advance(3);
   return;
  }
  if (Phase == 3)
  {
   float elapsed = Now() - PhaseAt;
   HolderMax = Math.Max(HolderMax, Moved(Holder, HolderAnchor));
   FrozenMax = Math.Max(FrozenMax, Moved(Frozen, FrozenAnchor));
   AnimatedMax = Math.Max(AnimatedMax, Moved(Animated, AnimatedAnchor));
   LeaderMax = Math.Max(LeaderMax, Moved(Leader, LeaderStart));
   if (Frozen.GetCharacterController().GetStance() != FrozenStance) FrozenStanceChanged = true;
   if (!StanceRequested && elapsed > 4)
   {
    StanceRequested = true;
    // Native stance path: the AI's own SCR_AISetStance node applies the current
    // stance setting. A raw SetStanceChange alone is a one-shot input that a
    // follower's KeepInFormation tree overwrites with STAND every 0.5 s, held or
    // not, so the probe adds the vanilla setting a scenario would add (plus the raw
    // request for a behavior without a stance node). Hold must let it through;
    // Freeze's own lock (priority 7000) must outrank the same setting.
    ECharacterStance frozenProbe = ECharacterStance.CROUCH;
    if (FrozenStance == ECharacterStance.CROUCH) frozenProbe = ECharacterStance.STAND;
    HolderStanceProbe = SCR_AICharacterStanceSetting.Create(SCR_EAISettingOrigin.SCENARIO, SCR_EAIBehaviorCause.ALWAYS, ECharacterStance.CROUCH);
    FrozenStanceProbe = SCR_AICharacterStanceSetting.Create(SCR_EAISettingOrigin.SCENARIO, SCR_EAIBehaviorCause.ALWAYS, frozenProbe);
    SCR_AICharacterSettingsComponent holderSettings = SCR_AICharacterSettingsComponent.FindOnControlledEntity(Holder);
    SCR_AICharacterSettingsComponent frozenSettings = SCR_AICharacterSettingsComponent.FindOnControlledEntity(Frozen);
    StanceProbeAdded = holderSettings && frozenSettings && holderSettings.AddCharacterSetting(HolderStanceProbe, false, false) && frozenSettings.AddCharacterSetting(FrozenStanceProbe, false, false);
    SCR_AIStanceHandling.SetStance(Holder.GetCharacterController(), ECharacterStance.CROUCH);
    StanceProbeAt = Now();
   }
   if (StanceProbeAdded && !HolderCrouched && Holder.GetCharacterController().GetStance() == ECharacterStance.CROUCH)
   {
    HolderCrouched = true;
    CrouchLatency = Now() - StanceProbeAt;
   }
   if (Smoking(Animated))
   {
    if (SmokeSince < 0) SmokeSince = Now();
   }
   else if (SmokeSince >= 0) LoiterLost = true;
   if (elapsed < 30) return;
   PrintFormat("[EUS TEST HOLD] holderMax=%1 frozenMax=%2 animatedMax=%3 leaderMax=%4 crouched=%5 frozenStanceChanged=%6 smokeSince=%7 loiterLost=%8", HolderMax, FrozenMax, AnimatedMax, LeaderMax, HolderCrouched, FrozenStanceChanged, SmokeSince, LoiterLost);
   PrintFormat("[EUS TEST CONTROL] leaderMoved=%1 (free leader walking to the waypoint; below 8 m weakens the retention checks)", LeaderMax);
   Check(HolderMax <= 1.5, "Hold keeps the soldier within 1.5 m for 30 s under a squad move order");
   Check(FrozenMax <= 1.0, "Freeze keeps the soldier within 1.0 m for 30 s under a squad move order");
   Check(AnimatedMax <= 1.5, "Animation keeps the soldier within 1.5 m for 30 s under a squad move order");
   SCR_AIUtilityComponent probeUtility = Utility(Holder);
   SCR_AIBehaviorBase holderBehavior;
   if (probeUtility) holderBehavior = probeUtility.GetCurrentBehavior();
   PrintFormat("[EUS TEST STANCE] probeAdded=%1 holderCrouched=%2 crouchLatency=%3 holderStance=%4 frozenStance=%5 frozenLock=%6 holderBehavior=%7", StanceProbeAdded, HolderCrouched, CrouchLatency, typename.EnumToString(ECharacterStance, Holder.GetCharacterController().GetStance()), typename.EnumToString(ECharacterStance, Frozen.GetCharacterController().GetStance()), typename.EnumToString(ECharacterStance, FrozenStance), holderBehavior);
   Check(StanceProbeAdded && HolderCrouched, "Hold leaves stance native: a vanilla stance setting crouches the held soldier through his own AI");
   Check(!FrozenStanceChanged, "Freeze keeps the frozen stance against a conflicting vanilla stance setting");
   SCR_AICharacterSettingsComponent holderProbeOwner = SCR_AICharacterSettingsComponent.FindOnControlledEntity(Holder);
   SCR_AICharacterSettingsComponent frozenProbeOwner = SCR_AICharacterSettingsComponent.FindOnControlledEntity(Frozen);
   if (holderProbeOwner && HolderStanceProbe) holderProbeOwner.RemoveSetting(HolderStanceProbe);
   if (frozenProbeOwner && FrozenStanceProbe) frozenProbeOwner.RemoveSetting(FrozenStanceProbe);
   Check(SmokeSince >= 0 && !LoiterLost && Now() - SmokeSince >= 10, "Animation plays the vanilla smoking loiter and stays in it");
   Advance(4);
   return;
  }
  if (Phase == 4)
  {
   SCR_CharacterDamageManagerComponent damage = SCR_CharacterDamageManagerComponent.Cast(Animated.GetDamageManager());
   if (!Check(damage != null, "animated soldier has a damage manager")) { Finish("damage"); return; }
   vector hit[3];
   hit[0] = Animated.GetOrigin() + Vector(0, 1.2, 0);
   hit[1] = Vector(1, 0, 0);
   hit[2] = Vector(-1, 0, 0);
   SCR_DamageContext context = new SCR_DamageContext(EDamageType.TRUE, 5, hit, Animated, damage.GetDefaultHitZone(), Instigator.CreateInstigator(null), null, -1, -1);
   damage.HandleDamage(context);
   Advance(5);
   return;
  }
  if (Phase == 5)
  {
   bool released = Animated.EUS_Script == EUS_Codes.NONE && !Manager.FindControl(Animated);
   bool stopped = !Smoking(Animated);
   if (!released || !stopped)
   {
    if (Now() - PhaseAt > 10)
    {
     Check(released, "damage releases the animation script");
     Check(stopped, "damage ends the loiter");
     Advance(6);
    }
    return;
   }
   Check(true, "damage releases the animation script");
   Check(true, "damage ends the loiter");
   Check(!HasOwnSpeed(Animated) && !HasOwnStance(Animated), "released soldier has no owned settings left");
   Advance(6);
   return;
  }
  if (Phase == 6)
  {
   Check(Manager.ApplyUnit(Holder, EUS_Codes.NONE, Report) && Manager.ApplyUnit(Frozen, EUS_Codes.NONE, Report), "Game Master release of Hold and Freeze");
   Check(Holder.EUS_Script == EUS_Codes.NONE && Frozen.EUS_Script == EUS_Codes.NONE, "replicated script codes cleared");
   Check(!HasOwnSpeed(Holder) && !HasOwnSpeed(Frozen) && !HasOwnStance(Frozen), "owned settings removed on release");
   SCR_AIUtilityComponent releasedUtility = Utility(Frozen);
   if (FrozenPerception > 0 && releasedUtility && releasedUtility.m_CombatComponent) Check(releasedUtility.m_CombatComponent.GetPerceptionFactor() == FrozenPerception, "perception restored after Freeze");
   Check(Group.EUS_Scripted == 0 && !EUS_Manager.Reserves(Group), "squad no longer scripted or reserved");
   Check(!Manager.ApplyUnit(Holder, EUS_Codes.NONE, Report), "second release is a no-op");
   HolderAnchor = Holder.GetOrigin();
   FrozenAnchor = Frozen.GetOrigin();
   Advance(7);
   return;
  }
  if (Phase == 7)
  {
   if (Now() - PhaseAt < 25) return;
   float holderAfter = Moved(Holder, HolderAnchor);
   float frozenAfter = Moved(Frozen, FrozenAnchor);
   PrintFormat("[EUS TEST CONTROL] releasedMoved holder=%1 frozen=%2 leaderMoved=%3", holderAfter, frozenAfter, LeaderMax);
   if (LeaderMax > 8) Check(holderAfter > 3 || frozenAfter > 3, "released soldiers resume normal AI movement toward their squad");
   Check(Manager.SetDiscipline(Group, EUS_Codes.DISCIPLINE_LIGHT, Report), "Light Discipline applied");
   Advance(8);
   return;
  }
  if (Phase == 8)
  {
   if (Now() - PhaseAt < 3) return;
   int total;
   int lit;
   int held;
   int living = Lights(total, lit, held);
   PrintFormat("[EUS TEST LIGHTS] mode=light living=%1 flashlights=%2 lit=%3 ownedSettings=%4", living, total, lit, held);
   Check(Group.EUS_Discipline == EUS_Codes.DISCIPLINE_LIGHT && held == living && living > 0, "Light Discipline owns every living member's light setting");
   Check(lit == 0, "Light Discipline keeps flashlights off");
   Check(EUS_Manager.Reserves(Group), "squad with night discipline is reserved from Unit Caching");
   // A multi-group edit with differing values writes every selected group again.
   int appliedBefore = Report.Applied;
   Check(!Manager.SetDiscipline(Group, EUS_Codes.DISCIPLINE_LIGHT, Report) && Report.Applied == appliedBefore && Group.EUS_Discipline == EUS_Codes.DISCIPLINE_LIGHT, "the current night discipline again is ignored (not re-applied or counted)");
   // The Group tab attribute entry refuses a write that does not come from the editing Game Master.
   Manager.DisciplineAttribute(null, 0, Group, EUS_Codes.DISCIPLINE_TERROR);
   Check(Group.EUS_Discipline == EUS_Codes.DISCIPLINE_LIGHT && Manager.FindDiscipline(Group) != null, "EXPBG Night discipline attribute write without the editing Game Master is refused");
   Check(Manager.SetDiscipline(Group, EUS_Codes.DISCIPLINE_TERROR, Report), "Terror Tactics applied");
   Advance(9);
   return;
  }
  if (Phase == 9)
  {
   if (Now() - PhaseAt < 3) return;
   int terrorTotal;
   int terrorLit;
   int terrorOwned;
   int terrorLiving = Lights(terrorTotal, terrorLit, terrorOwned);
   PrintFormat("[EUS TEST LIGHTS] mode=terror living=%1 flashlights=%2 lit=%3 ownedSettings=%4", terrorLiving, terrorTotal, terrorLit, terrorOwned);
   Check(Group.EUS_Discipline == EUS_Codes.DISCIPLINE_TERROR && terrorOwned == terrorLiving, "Terror Tactics owns every living member's light setting");
   if (terrorTotal > 0) Check(terrorLit == terrorTotal, "Terror Tactics switches slotted flashlights on");
   else PrintFormat("[EUS TEST LIGHTS] knownLimitation=no-slotted-flashlights-in-loadout");
   Check(Manager.SetDiscipline(Group, EUS_Codes.DISCIPLINE_OFF, Report), "night discipline released");
   int offTotal;
   int offLit;
   int offOwned;
   Lights(offTotal, offLit, offOwned);
   Check(Group.EUS_Discipline == EUS_Codes.DISCIPLINE_OFF && offOwned == 0 && !EUS_Manager.Reserves(Group), "discipline release removes every owned setting and the reservation");
   Finish("completed");
   return;
  }
 }
}

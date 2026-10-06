// TEST ONLY. Vanilla suppress behaviour vs AI Surrender and AI Global Skills (dedicated
// server, no players, no GM UI). A GM session logged the script VM exception "No
// suppression volume provided!" (SuppressBehavior.bt, SCR_AIGetSuppressionVolumeCenterPosition)
// while AI Surrender was taking soldiers in a fight.
// pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/ESR_SuppressGameplay.c -TimeoutSeconds 300 -OrchestratorSlotGranted -ExpectResult '\[ESR SUPPRESS RESULT\] checks=[1-9]\d* failures=0 .*reason=complete'
// The runner copies this file to EXPG_GarrisonGameplay.c; the class names are fixed. The
// -ExpectResult verdict needs the one passing result line, a clean shutdown and no "Virtual
// Machine Exception" or SCRIPT (E) before it: that is the no-VME assertion. Each guard count
// below is a stale tree where vanilla would have raised that exception on every AI tick
// until the soldier's next decision; the guard counts it once per guarded node and fails
// the repeated reads uncounted (AbsorbedReadCount, printed as absorbed=).
// One native USSR squad; suppress behaviours enter through the vanilla utility API.
//  B: vanilla suppress behaviour with a real volume, running; the soldier surrenders and his
//     AI is then woken the way vanilla wakes a casualty. Surrender fails the behaviour, the
//     woken prisoner never resumes it (a stale tree, if any, is caught by the guard and
//     ends, bounded); upkeep switches the AI off again.
//  C: EXPBG warning shots running; the burst is stopped the way a ROE change stops it.
//  D: EXPBG warning shots running; the shooter surrenders and is woken.
//  A: reproduction of the logged state: a suppress behaviour without a volume. Vanilla
//     raises NodeError there (the VM exception); the guard fails the behaviour instead.
class EXPG_GarrisonGameplayClass : GenericEntityClass {}
class EXPG_GarrisonGameplay : GenericEntity
{
 static const float FIXTURE_SECONDS = 200;
 static const float WAIT_EXECUTED = 20;
 static const float SETTLE = 3;
 // Guard counts for one suppress behaviour: once per guarded node (centre position, line)
 // and executed action. Its tree keeps reading on every AI tick until the next decision
 // (0.55-2 s; the first native run counted 6 reads 83 ms apart for one behaviour), and
 // those repeats are absorbed, not counted. A count alone therefore no longer bounds how
 // long a stale tree ran; Ended() does (see there).
 static const int MAX_CATCHES_PER_BEHAVIOUR = 2;
 static const ResourceName MODULE = "{7F2E668385984EA1}PrefabsEditable/EXPSR/ESR_SurrenderModule.et";
 static const ResourceName SQUAD = "{E552DABF3636C2AD}Prefabs/Groups/OPFOR/Group_USSR_RifleSquad.et";
 static const int SIZE = 6;
 vector Origin = "4773.46 0 7094.57";
 // Suppressed point and warning-shot target: open ground in front of the squad, about 60 m
 // from every shooter, so vanilla's line timing (a division by the shooter's distance to
 // the volume centre) stays finite. The fixture's own math has no division or root.
 vector AimOffset = "0 0 60";
 ESR_SurrenderModule Surrender;
 SCR_AIGroup TestSquad;
 ref array<SCR_ChimeraCharacter> Members = {};
 SCR_ChimeraCharacter Subject;
 // Weak: the utility alone owns the behaviour and, through it, the volume.
 SCR_AISuppressBehavior Watched;
 int MissingAtStart = -1;
 int MissingBefore;
 int Phase;
 int Checks;
 int Failures;
 float Started;
 float PhaseAt;
 float Next;
 bool SawExecuted;
 bool Finished;

 void EXPG_GarrisonGameplay(IEntitySource src, IEntity parent) { SetEventMask(EntityEvent.INIT | EntityEvent.FRAME); }
 float Now() { return GetGame().GetWorld().GetWorldTime() * 0.001; }

 override void EOnInit(IEntity owner)
 {
  if (!Replication.IsServer()) { ClearEventMask(EntityEvent.FRAME); return; }
  Started = Now(); Next = Started + 10; PhaseAt = Started;
  PrintFormat("[ESR SUPPRESS BEGIN] origin=%1 aim=%2 deadline=%3", Origin, AimOffset, FIXTURE_SECONDS);
 }

 bool Check(bool ok, string label)
 {
  Checks++;
  if (!ok) Failures++;
  PrintFormat("[ESR SUPPRESS CHECK] pass=%1 %2", ok, label);
  return ok;
 }

 void Finish(string reason)
 {
  if (Finished) return;
  Finished = true;
  ClearEventMask(EntityEvent.FRAME);
  int caught = EGS_SuppressionGuard.MissingVolumeCount();
  PrintFormat("[ESR SUPPRESS RESULT] checks=%1 failures=%2 missingVolumes=%3 missingAtStart=%4 prisoners=%5 reason=%6", Checks, Failures, caught, MissingAtStart, ESR_SurrenderManager.PrisonerCount(), reason);
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

 AIControlComponent ControlOf(SCR_ChimeraCharacter character)
 {
  if (!character) return null;
  return character.GetAIControlComponent();
 }

 SCR_AIUtilityComponent UtilityOf(SCR_ChimeraCharacter character)
 {
  AIControlComponent control = ControlOf(character);
  if (!control) return null;
  AIAgent agent = control.GetControlAIAgent();
  if (!agent) return null;
  return SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
 }

 bool AiOn(SCR_ChimeraCharacter character)
 {
  AIControlComponent control = ControlOf(character);
  return control != null && control.IsAIActivated();
 }

 // Retired: failed, completed, or already dropped (and freed) by the utility.
 bool Retired(SCR_AISuppressBehavior behavior)
 {
  if (!behavior) return true;
  EAIActionState state = behavior.GetActionState();
  return state == EAIActionState.FAILED || state == EAIActionState.COMPLETED;
 }

 bool Executing(SCR_ChimeraCharacter character, SCR_AISuppressBehavior behavior)
 {
  SCR_AIUtilityComponent utility = UtilityOf(character);
  if (!utility || !behavior) return false;
  return utility.GetExecutedAction() == behavior;
 }

 // Still running it: executed and neither failed nor completed. A failed behaviour stays
 // the executed one until the soldier's next decision drops it; that is not a resume.
 bool Running(SCR_ChimeraCharacter character, SCR_AISuppressBehavior behavior)
 {
  return Executing(character, behavior) && !Retired(behavior);
 }

 // The behaviour's tree has ended: a decision moved the soldier to another action, or his
 // AI is off (no tree ticks). The guard absorbs a stale tree's repeated reads only while
 // the action it counted stays the executed one; once that changes, any further read is
 // counted again. So a bounded count plus Ended() shows a stale tree stopped, which a
 // count alone (one per node and action) no longer does. Running() is not enough: a
 // failed behaviour stays executed, and its tree keeps reading, until the next decision.
 bool Ended(SCR_ChimeraCharacter character, SCR_AISuppressBehavior behavior)
 {
  return !Executing(character, behavior) || !AiOn(character);
 }

 // Suppress behaviours of the soldier that could still be selected.
 int LiveSuppress(SCR_ChimeraCharacter character)
 {
  SCR_AIUtilityComponent utility = UtilityOf(character);
  if (!utility) return 0;
  array<ref AIActionBase> actions = {};
  utility.FindActionsOfInheritedType(SCR_AISuppressBehavior, actions);
  int live;
  foreach (AIActionBase action : actions)
  {
   if (!action) continue;
   EAIActionState state = action.GetActionState();
   if (state != EAIActionState.COMPLETED && state != EAIActionState.FAILED) live++;
  }
  return live;
 }

 string Describe(SCR_ChimeraCharacter character)
 {
  SCR_AIUtilityComponent utility = UtilityOf(character);
  AIActionBase executed;
  if (utility) executed = utility.GetExecutedAction();
  return string.Format("aiOn=%1 executed=%2 watched=%3 retired=%4 missing=%5 absorbed=%6", AiOn(character), executed, Watched, Retired(Watched), EGS_SuppressionGuard.MissingVolumeCount(), EGS_SuppressionGuard.AbsorbedReadCount());
 }

 // The vanilla constructor and AddAction, as the base game's goal reaction does it. The
 // volume is created here and handed over: only the behaviour keeps it. From the squad
 // (63-66 m in the first run) vanilla's first aim line starts at the centre and ends at
 // least tan(2 deg) * 63 m = 2.2 m away, beyond this 2 m sphere's rim: B (like the 1 m
 // warning spheres of C and D) exercises the global sphere aim-height clamp on purpose
 // instead of avoiding it here.
 SCR_AISuppressBehavior AddSuppress(SCR_ChimeraCharacter character, bool withVolume)
 {
  SCR_AIUtilityComponent utility = UtilityOf(character);
  if (!utility) return null;
  SCR_AISuppressionVolumeSphere volume;
  if (withVolume) volume = new SCR_AISuppressionVolumeSphere(Ground(Origin + AimOffset, 1.0), 2.0);
  SCR_AISuppressBehavior behavior = new SCR_AISuppressBehavior(utility, null, volume, 0, 1.0, SCR_AIActionBase.PRIORITY_LEVEL_PLAYER);
  utility.AddAction(behavior);
  return behavior;
 }

 // EXPBG AI Global Skills' own entry point for a warning burst (target: the module entity
 // standing on the suppressed ground, never a squad mate).
 SCR_AISuppressBehavior StartWarning(SCR_ChimeraCharacter character)
 {
  SCR_AICombatComponent combat = SCR_AICombatComponent.Cast(character.FindComponent(SCR_AICombatComponent));
  SCR_AIUtilityComponent utility = UtilityOf(character);
  if (!combat || !utility || !combat.EGS_StartWarningShots(Surrender, null, 1)) return null;
  return SCR_AISuppressBehavior.Cast(utility.FindActionOfType(SCR_AISuppressBehavior));
 }

 // Waits until the soldier runs the watched behaviour's tree; false while waiting.
 bool WaitExecuted(string label)
 {
  if (Executing(Subject, Watched)) { SawExecuted = true; return true; }
  // A warning burst may end on its own before the check sees it run.
  if (Retired(Watched)) { SawExecuted = false; return true; }
  if (Now() - PhaseAt > WAIT_EXECUTED)
  {
   PrintFormat("[ESR SUPPRESS WAIT] case=%1 %2", label, Describe(Subject));
   Check(false, label + ": the soldier ran the suppress tree");
   Finish("not-executed");
  }
  return false;
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
   if (!Check(GetGame().GetPlayerManager().GetPlayerCount() == 0 && ESR_SurrenderManager.PrisonerCount() == 0, "isolated server: no players, no prisoners")) { Finish("setup"); return; }
   Resource modulePrefab = Resource.Load(MODULE);
   IEntity moduleEntity = GetGame().SpawnEntityPrefab(modulePrefab, GetGame().GetWorld(), Params(Origin + AimOffset));
   Surrender = ESR_SurrenderModule.Cast(moduleEntity);
   if (!Check(Surrender != null, "surrender module prefab spawned")) { Finish("setup"); return; }
   // No casualty rolls: every surrender here is the production call on one soldier.
   Surrender.ApplySetting(ESR_Settings.ENABLED, 0);
   Surrender.ApplySetting(ESR_Settings.DIAGNOSTICS, 1);
   Resource squadPrefab = Resource.Load(SQUAD);
   IEntity squadEntity = GetGame().SpawnEntityPrefab(squadPrefab, GetGame().GetWorld(), Params(Origin));
   TestSquad = SCR_AIGroup.Cast(squadEntity);
   if (!Check(TestSquad != null, "native USSR squad spawned")) { Finish("setup"); return; }
   Phase = 1; PhaseAt = Now(); return;
  }
  if (Phase == 1)
  {
   if (!TestSquad || TestSquad.GetAgentsCount() != SIZE)
   {
    if (Now() - PhaseAt > 30) { Check(false, "squad completed its initial spawn"); Finish("setup"); }
    return;
   }
   if (Now() - PhaseAt < 3) return;
   array<AIAgent> agents = {};
   TestSquad.GetAgents(agents);
   IEntity leader = TestSquad.GetLeaderEntity();
   int active;
   foreach (AIAgent agent : agents)
   {
    SCR_ChimeraCharacter member = SCR_ChimeraCharacter.Cast(agent.GetControlledEntity());
    if (!member) continue;
    if (AiOn(member)) active++;
    if (member != leader && Members.Count() < 4) Members.Insert(member);
   }
   MissingAtStart = EGS_SuppressionGuard.MissingVolumeCount();
   PrintFormat("[ESR SUPPRESS SQUAD] members=%1 aiOn=%2 picked=%3 missingAtStart=%4", agents.Count(), active, Members.Count(), MissingAtStart);
   if (!Check(Members.Count() == 4, "four non-leader soldiers picked")) { Finish("setup"); return; }
   Check(MissingAtStart == 0, "no suppress tree ran without a volume before the cases");
   Phase = 10; PhaseAt = Now(); return;
  }

  // B: vanilla suppress behaviour with a volume, surrender mid-suppress, vanilla wake-up.
  if (Phase == 10)
  {
   Subject = Members[0];
   Watched = AddSuppress(Subject, true);
   if (!Check(Watched != null && Watched.m_SuppressionVolume.m_Value != null, "B: vanilla suppress behaviour with a volume added")) { Finish("case-b"); return; }
   Phase = 11; PhaseAt = Now(); return;
  }
  if (Phase == 11)
  {
   if (!WaitExecuted("B")) return;
   Check(SawExecuted, "B: the soldier runs the vanilla suppress tree");
   MissingBefore = EGS_SuppressionGuard.MissingVolumeCount();
   bool taken = ESR_SurrenderManager.Surrender(Subject, ESR_SurrenderManager.GroupOf(Subject));
   PrintFormat("[ESR SUPPRESS B SURRENDER] taken=%1 %2", taken, Describe(Subject));
   Check(taken && Retired(Watched), "B: surrender failed the running suppress behaviour");
   Check(!AiOn(Subject), "B: surrender switched the prisoner's AI off");
   // Vanilla wakes a casualty's AI (SCR_ChimeraAIAgent.OnLifeStateChanged).
   AIControlComponent control = ControlOf(Subject);
   if (control) control.ActivateAI();
   Phase = 12; PhaseAt = Now(); return;
  }
  if (Phase == 12)
  {
   if (Now() - PhaseAt < SETTLE) return;
   int wokenHits = EGS_SuppressionGuard.MissingVolumeCount() - MissingBefore;
   PrintFormat("[ESR SUPPRESS B WOKEN] guardHits=%1 %2", wokenHits, Describe(Subject));
   Check(wokenHits <= MAX_CATCHES_PER_BEHAVIOUR && (wokenHits == 0 || Ended(Subject, Watched)), "B: any stale suppress tree of the woken prisoner was caught by the guard and ended (bounded)");
   Check(!Running(Subject, Watched) && LiveSuppress(Subject) == 0, "B: the woken prisoner does not resume suppressing");
   Phase = 20; PhaseAt = Now(); return;
  }

  // C: EXPBG warning shots stopped mid-burst, as a ROE change stops them.
  if (Phase == 20)
  {
   Subject = Members[1];
   Watched = StartWarning(Subject);
   if (!Check(Watched != null && Watched.m_SuppressionVolume.m_Value != null, "C: warning shots added a suppress behaviour with a volume")) { Finish("case-c"); return; }
   Phase = 21; PhaseAt = Now(); return;
  }
  if (Phase == 21)
  {
   if (!WaitExecuted("C")) return;
   MissingBefore = EGS_SuppressionGuard.MissingVolumeCount();
   SCR_AICombatComponent combat = SCR_AICombatComponent.Cast(Subject.FindComponent(SCR_AICombatComponent));
   if (combat) combat.EGS_EndWarningShots(false);
   PrintFormat("[ESR SUPPRESS C STOPPED] sawRunning=%1 %2", SawExecuted, Describe(Subject));
   Check(Retired(Watched), "C: stopping the burst retires the warning behaviour");
   Phase = 22; PhaseAt = Now(); return;
  }
  if (Phase == 22)
  {
   if (Now() - PhaseAt < SETTLE) return;
   int stopHits = EGS_SuppressionGuard.MissingVolumeCount() - MissingBefore;
   PrintFormat("[ESR SUPPRESS C AFTER] guardHits=%1 %2", stopHits, Describe(Subject));
   Check(stopHits <= MAX_CATCHES_PER_BEHAVIOUR && (stopHits == 0 || Ended(Subject, Watched)), "C: any stale suppress tree after the stop was caught by the guard and ended (bounded)");
   Check(!Running(Subject, Watched), "C: the soldier left the warning behaviour");
   Phase = 30; PhaseAt = Now(); return;
  }

  // D: EXPBG warning shots running, the shooter surrenders and is woken.
  if (Phase == 30)
  {
   Subject = Members[2];
   Watched = StartWarning(Subject);
   if (!Check(Watched != null && Watched.m_SuppressionVolume.m_Value != null, "D: warning shots added a suppress behaviour with a volume")) { Finish("case-d"); return; }
   Phase = 31; PhaseAt = Now(); return;
  }
  if (Phase == 31)
  {
   if (!WaitExecuted("D")) return;
   MissingBefore = EGS_SuppressionGuard.MissingVolumeCount();
   bool shooterTaken = ESR_SurrenderManager.Surrender(Subject, ESR_SurrenderManager.GroupOf(Subject));
   PrintFormat("[ESR SUPPRESS D SURRENDER] taken=%1 sawRunning=%2 %3", shooterTaken, SawExecuted, Describe(Subject));
   Check(shooterTaken && Retired(Watched) && !AiOn(Subject), "D: the shooter surrendered with his warning behaviour retired and AI off");
   AIControlComponent shooterControl = ControlOf(Subject);
   if (shooterControl) shooterControl.ActivateAI();
   Phase = 32; PhaseAt = Now(); return;
  }
  if (Phase == 32)
  {
   if (Now() - PhaseAt < SETTLE) return;
   int shooterHits = EGS_SuppressionGuard.MissingVolumeCount() - MissingBefore;
   PrintFormat("[ESR SUPPRESS D WOKEN] guardHits=%1 %2", shooterHits, Describe(Subject));
   Check(shooterHits <= MAX_CATCHES_PER_BEHAVIOUR && (shooterHits == 0 || Ended(Subject, Watched)), "D: any stale suppress tree of the woken shooter was caught by the guard and ended (bounded)");
   Check(!Running(Subject, Watched) && LiveSuppress(Subject) == 0, "D: the woken shooter does not resume suppressing");
   Phase = 40; PhaseAt = Now(); return;
  }

  // A: the logged state itself, a suppress behaviour without a volume.
  if (Phase == 40)
  {
   Subject = Members[3];
   MissingBefore = EGS_SuppressionGuard.MissingVolumeCount();
   Watched = AddSuppress(Subject, false);
   if (!Check(Watched != null && Watched.m_SuppressionVolume.m_Value == null, "A: suppress behaviour without a volume added (the logged state)")) { Finish("case-a"); return; }
   Phase = 41; PhaseAt = Now(); return;
  }
  if (Phase == 41)
  {
   if (EGS_SuppressionGuard.MissingVolumeCount() == MissingBefore)
   {
    if (Now() - PhaseAt > WAIT_EXECUTED)
    {
     PrintFormat("[ESR SUPPRESS WAIT] case=A %1", Describe(Subject));
     Check(false, "A: the soldier reached the suppress tree without a volume");
     Finish("not-executed");
    }
    return;
   }
   PrintFormat("[ESR SUPPRESS A CAUGHT] %1", Describe(Subject));
   Check(Retired(Watched), "A: the guard failed the volume-less behaviour instead of raising a VM exception");
   Phase = 42; PhaseAt = Now(); return;
  }
  if (Phase == 42)
  {
   if (Now() - PhaseAt < SETTLE) return;
   int caught = EGS_SuppressionGuard.MissingVolumeCount() - MissingBefore;
   PrintFormat("[ESR SUPPRESS A AFTER] caught=%1 %2", caught, Describe(Subject));
   // Not executing it (not merely failed): with his AI on that is Ended(), so the stale tree
   // stopped (first run: executed=SCR_AIIdleBehavior, watched=NULL 3.2 s after the catch).
   Check(!Executing(Subject, Watched) && AiOn(Subject), "A: the soldier moved on to another behaviour with his AI on");
   // Without a volume only the centre-position node is reached: Fire_Suppressive (the line
   // node) runs for FireTreeId 1, which SCR_AIUpdateTargetSuppressionData never writes
   // without a volume. So one behaviour is one count; the repeats until the next decision
   // are absorbed.
   Check(caught == 1, "A: the guard counted the volume-less behaviour once");
   // Upkeep runs every 5 s; by now both woken prisoners had at least one pass.
   Check(!AiOn(Members[0]) && !AiOn(Members[2]), "upkeep switched the woken prisoners' AI off again");
   Check(ESR_SurrenderManager.PrisonerCount() == 2, "exactly the two surrendered soldiers are prisoners");
   Finish("complete");
  }
 }
}

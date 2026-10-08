// TEST ONLY. EXPBG Unit Scripts native fixture: continuous ambient animations, Freeze
// and Hold through damage, the Unit Caching pause and the versioned state API.
// pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/EUS_StateLoopGameplay.c -TimeoutSeconds 480 -ExpectResult '\[EUS STATE TEST RESULT\] checks=[1-9]\d* failures=0 reason=completed' -OrchestratorSlotGranted
// The runner copies this file to EXPG_GarrisonGameplay.c; the class names are fixed.
// -ExpectResult judges the run by exactly one passing result line and no script errors.
// Real US fire team on GM Eden through the production EUS_Manager entry points.
//  1. Smoke and Stand at ease play for LOOP_SECONDS without a single sample out of the
//     loiter command once it started, stay within POSE_DRIFT of their spot and stay bound;
//     pose cycles played inside the command are printed (EUS_PoseCycles; a sequence
//     that never ends on this server prints 0, which is not a failure).
//  2. Freeze and Hold take real damage (TRUE, 5) and stay bound (GetEnd NONE) on their
//     spots; an animation hit by the same damage ends with EUS_EEndReason.DAMAGE.
//  3. SetCachePaused: a pose stopped while paused is not re-issued and spends no
//     attempt; un-paused it plays again; EUS_Manager.ScriptsOnly is true for the squad.
//  4. EUS_UnitState: Capture/Encode/Decode round trip of every script; invalid payloads
//     (other version, unknown code, a non-horizontal heading, garbage) are refused.
//  5. Restore onto a fresh soldier of a second squad standing 1 m off the saved spot
//     and turned away: after the restore wait he runs the same script on the saved
//     spot (within 0.4 m) and heading (dot >= 0.95) - what a native or CDF load does.
// No players, GM UI, possession, native save files or CDF: the CDF round trip is
// tests/EUS_CDFScriptsRoundTrip.c in EXPBG CDF Compat.
class EXPG_GarrisonGameplayClass : GenericEntityClass {}
class EXPG_GarrisonGameplay : GenericEntity
{
 static const float FIXTURE_SECONDS = 400;
 static const float LOOP_SECONDS = 150;
 static const float POSE_DRIFT = 0.3;
 static const ResourceName SQUAD = "{84E5BBAB25EA23E5}Prefabs/Groups/BLUFOR/Group_US_FireTeam.et";
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
 SCR_AIGroup Spare;
 SCR_ChimeraCharacter Holder;
 SCR_ChimeraCharacter Frozen;
 SCR_ChimeraCharacter Smoker;
 SCR_ChimeraCharacter Stander;
 SCR_ChimeraCharacter Fresh;
 EUS_UnitControl HolderControl;
 EUS_UnitControl FrozenControl;
 EUS_UnitControl SmokerControl;
 EUS_UnitControl StanderControl;
 vector SmokerAnchor;
 vector StanderAnchor;
 vector HolderAnchor;
 vector FrozenAnchor;
 float SmokerSince = -1;
 float StanderSince = -1;
 int SmokerGaps;
 int StanderGaps;
 float SmokerDrift;
 float StanderDrift;
 float FrozenDrift;
 float HolderDrift;
 bool Damaged;
 ref EUS_UnitState Saved;

 void EXPG_GarrisonGameplay(IEntitySource src, IEntity parent) { SetEventMask(EntityEvent.INIT | EntityEvent.FRAME); }
 float Now() { return GetGame().GetWorld().GetWorldTime() * 0.001; }

 override void EOnInit(IEntity owner)
 {
  if (!Replication.IsServer())
  {
   ClearEventMask(EntityEvent.FRAME);
   return;
  }
  Started = Now();
  Next = Started + 10;
  PrintFormat("[EUS STATE TEST BEGIN] squad=%1 origin=%2 deadline=%3", SQUAD, Origin, FIXTURE_SECONDS);
 }

 bool Check(bool ok, string label)
 {
  Checks++;
  if (!ok) Failures++;
  PrintFormat("[EUS STATE TEST CHECK] pass=%1 %2", ok, label);
  return ok;
 }

 void Finish(string reason)
 {
  if (Finished)
  {
   return;
  }
  Finished = true;
  ClearEventMask(EntityEvent.FRAME);
  PrintFormat("[EUS STATE TEST RESULT] checks=%1 failures=%2 reason=%3", Checks, Failures, reason);
  GetGame().RequestClose();
 }

 void Advance(int phase)
 {
  Phase = phase;
  PhaseAt = Now();
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

 SCR_AIGroup SpawnTeam(vector p)
 {
  // Keep Resource and spawned entity in locals before casting (inline form returned null natively).
  Resource squad = Resource.Load(SQUAD);
  IEntity squadEntity = GetGame().SpawnEntityPrefab(squad, GetGame().GetWorld(), Params(p));
  return SCR_AIGroup.Cast(squadEntity);
 }

 bool Loitering(SCR_ChimeraCharacter actor)
 {
  if (!actor)
  {
   return false;
  }
  SCR_CharacterControllerComponent controller = SCR_CharacterControllerComponent.Cast(actor.GetCharacterController());
  return controller && controller.IsLoitering();
 }

 float Moved(SCR_ChimeraCharacter actor, vector anchor)
 {
  if (!actor)
  {
   return 100;
  }
  return vector.DistanceXZ(actor.GetOrigin(), anchor);
 }

 void Hit(SCR_ChimeraCharacter actor)
 {
  SCR_CharacterDamageManagerComponent damage = SCR_CharacterDamageManagerComponent.Cast(actor.GetDamageManager());
  if (!damage)
  {
   Check(false, "soldier has a damage manager");
   return;
  }
  vector hit[3];
  hit[0] = actor.GetOrigin() + Vector(0, 1.2, 0);
  hit[1] = Vector(1, 0, 0);
  hit[2] = Vector(-1, 0, 0);
  SCR_DamageContext context = new SCR_DamageContext(EDamageType.TRUE, 5, hit, actor, damage.GetDefaultHitZone(), Instigator.CreateInstigator(null), null, -1, -1);
  damage.HandleDamage(context);
 }

 // A payload as EUS_UnitState writes it, with chosen values.
 string Payload(int version, int code, float fx, float fz)
 {
  return string.Format("{\"version\":%1,\"code\":%2,\"ax\":1,\"ay\":2,\"az\":3,\"fx\":%3,\"fz\":%4}", version, code, fx, fz);
 }

 override void EOnFrame(IEntity owner, float timeSlice)
 {
  if (Finished || Now() < Next)
  {
   return;
  }
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
   Group = SpawnTeam(Origin);
   Spare = SpawnTeam(Origin + Vector(0, 0, 40));
   if (!Check(Group != null && Spare != null, "two native US fire teams spawned"))
   {
    Finish("setup");
    return;
   }
   Advance(1);
   return;
  }
  if (Phase == 1)
  {
   if (Group.GetAgentsCount() < 4 || Spare.GetAgentsCount() < 1)
   {
    if (Now() - PhaseAt > 30)
    {
     Check(false, "fire teams spawned their AI members");
     Finish("setup");
    }
    return;
   }
   if (Now() - PhaseAt < 4)
   {
    return;
   }
   array<AIAgent> agents = {};
   Group.GetAgents(agents);
   foreach (AIAgent agent : agents)
   {
    SCR_ChimeraCharacter member = SCR_ChimeraCharacter.Cast(agent.GetControlledEntity());
    if (!member) continue;
    if (!Holder) Holder = member;
    else if (!Frozen) Frozen = member;
    else if (!Smoker) Smoker = member;
    else if (!Stander) Stander = member;
   }
   array<AIAgent> spares = {};
   Spare.GetAgents(spares);
   if (!spares.IsEmpty()) Fresh = SCR_ChimeraCharacter.Cast(spares[0].GetControlledEntity());
   Manager = EUS_Manager.Get();
   if (!Check(Holder && Frozen && Smoker && Stander && Fresh && Manager, "four soldiers, a spare soldier and the manager"))
   {
    Finish("setup");
    return;
   }
   Check(Manager.ApplyUnit(Holder, EUS_Codes.HOLD, Report), "Hold bound");
   Check(Manager.ApplyUnit(Frozen, EUS_Codes.FREEZE, Report), "Freeze bound");
   Check(Manager.ApplyUnit(Smoker, EUS_Codes.ANIMATION + 2, Report), "Smoke bound");
   Check(Manager.ApplyUnit(Stander, EUS_Codes.ANIMATION + 3, Report), "Stand at ease bound");
   HolderControl = Manager.FindControl(Holder);
   FrozenControl = Manager.FindControl(Frozen);
   SmokerControl = Manager.FindControl(Smoker);
   StanderControl = Manager.FindControl(Stander);
   Check(EUS_Manager.ScriptsOnly(Group), "ScriptsOnly: per-soldier scripts are the only Unit Scripts hold (no night discipline)");
   Advance(2);
   return;
  }
  if (Phase == 2)
  {
   // Settle (1.5 s) and the pose entries.
   if (Now() - PhaseAt < 4)
   {
    return;
   }
   SmokerAnchor = SmokerControl.GetAnchor();
   StanderAnchor = StanderControl.GetAnchor();
   HolderAnchor = HolderControl.GetAnchor();
   FrozenAnchor = FrozenControl.GetAnchor();
   Advance(3);
   return;
  }
  if (Phase == 3)
  {
   float elapsed = Now() - PhaseAt;
   if (Loitering(Smoker))
   {
    if (SmokerSince < 0) SmokerSince = Now();
   }
   else if (SmokerSince >= 0)
   {
    SmokerGaps++;
   }
   if (Loitering(Stander))
   {
    if (StanderSince < 0) StanderSince = Now();
   }
   else if (StanderSince >= 0)
   {
    StanderGaps++;
   }
   SmokerDrift = Math.Max(SmokerDrift, Moved(Smoker, SmokerAnchor));
   StanderDrift = Math.Max(StanderDrift, Moved(Stander, StanderAnchor));
   HolderDrift = Math.Max(HolderDrift, Moved(Holder, HolderAnchor));
   FrozenDrift = Math.Max(FrozenDrift, Moved(Frozen, FrozenAnchor));
   if (!Damaged && elapsed > 20)
   {
    Damaged = true;
    Hit(Frozen);
    Hit(Holder);
   }
   if (elapsed < LOOP_SECONDS)
   {
    return;
   }
   PrintFormat("[EUS STATE TEST LOOP] smokeSince=%1 smokeGaps=%2 smokeDrift=%3 smokeCycles=%4 standSince=%5 standGaps=%6 standDrift=%7 standCycles=%8", SmokerSince - PhaseAt, SmokerGaps, SmokerDrift, Smoker.EUS_PoseCycles, StanderSince - PhaseAt, StanderGaps, StanderDrift, Stander.EUS_PoseCycles);
   PrintFormat("[EUS STATE TEST HOLD] frozenDrift=%1 holderDrift=%2 frozenEnd=%3 holderEnd=%4 frozenCorrections=%5", FrozenDrift, HolderDrift, typename.EnumToString(EUS_EEndReason, FrozenControl.GetEnd()), typename.EnumToString(EUS_EEndReason, HolderControl.GetEnd()), FrozenControl.GetCorrections());
   Check(SmokerSince >= 0 && SmokerGaps == 0 && SmokerControl.IsBound(), "Smoke plays without a single sample out of the loiter command and stays bound");
   Check(StanderSince >= 0 && StanderGaps == 0 && StanderControl.IsBound(), "Stand at ease plays without a single sample out of the loiter command and stays bound");
   Check(SmokerDrift <= POSE_DRIFT && StanderDrift <= POSE_DRIFT, "the poses never drift off their spots");
   Check(FrozenControl.IsBound() && FrozenControl.GetEnd() == EUS_EEndReason.NONE && Frozen.EUS_Script == EUS_Codes.FREEZE, "Freeze holds through damage");
   Check(HolderControl.IsBound() && HolderControl.GetEnd() == EUS_EEndReason.NONE && Holder.EUS_Script == EUS_Codes.HOLD, "Hold holds through damage");
   Check(FrozenDrift <= 1.0 && HolderDrift <= 1.5, "Freeze and Hold stay on their spots through damage");
   Advance(4);
   return;
  }
  if (Phase == 4)
  {
   // Unit Caching pause: the pose ends while paused and is not re-issued.
   StanderControl.SetCachePaused(true);
   SCR_CharacterControllerComponent controller = SCR_CharacterControllerComponent.Cast(Stander.GetCharacterController());
   if (controller.IsLoitering()) controller.StopLoitering(true);
   Advance(5);
   return;
  }
  if (Phase == 5)
  {
   if (Now() - PhaseAt < 8)
   {
    return;
   }
   SCR_CharacterControllerComponent paused = SCR_CharacterControllerComponent.Cast(Stander.GetCharacterController());
   SCR_ScriptedCharacterInputContext input = paused.GetScrInputContext();
   Check(StanderControl.IsBound() && StanderControl.IsCachePaused(), "a paused pose stays bound");
   Check(!paused.IsLoitering() && (!input || input.m_iLoiteringType < 0), "a paused pose is not re-issued");
   StanderControl.SetCachePaused(false);
   Advance(6);
   return;
  }
  if (Phase == 6)
  {
   if (!Loitering(Stander))
   {
    if (Now() - PhaseAt > 12)
    {
     Check(false, "an un-paused pose plays again within 12 s");
     Advance(7);
    }
    return;
   }
   Check(StanderControl.IsBound() && !StanderControl.IsCachePaused(), "an un-paused pose plays again and stays bound");
   Advance(7);
   return;
  }
  if (Phase == 7)
  {
   // State API round trip of every script.
   array<SCR_ChimeraCharacter> actors = {Holder, Frozen, Smoker, Stander};
   int exact;
   foreach (SCR_ChimeraCharacter actor : actors)
   {
    EUS_UnitState state = EUS_UnitState.Capture(actor);
    string text;
    string reason;
    EUS_UnitState back;
    if (state) text = state.Encode();
    if (!text.IsEmpty()) back = EUS_UnitState.Decode(text, reason);
    if (back && back.Code == actor.EUS_Script && vector.Distance(back.Anchor, state.Anchor) < 0.01 && vector.Dot(back.Forward, state.Forward) > 0.999) exact++;
    else PrintFormat("[EUS STATE TEST API] actor=%1 payload=%2 reason=%3", actor, text, reason);
   }
   Check(exact == 4, string.Format("Capture, Encode and Decode return every script exactly (%1 of 4)", exact));
   Check(!EUS_UnitState.Capture(Fresh), "an unscripted soldier has no state");
   string why;
   Check(EUS_UnitState.Decode(Payload(1, EUS_Codes.FREEZE, 0, 1), why) != null, "a valid version-1 payload decodes");
   Check(!EUS_UnitState.Decode(Payload(2, EUS_Codes.FREEZE, 0, 1), why), "another payload version is refused");
   Check(!EUS_UnitState.Decode(Payload(1, 99, 0, 1), why), "an unknown script code is refused");
   Check(!EUS_UnitState.Decode(Payload(1, EUS_Codes.HOLD, 0, 0), why), "a heading that is not a direction is refused");
   Check(!EUS_UnitState.Decode("not json", why), "garbage is refused");
   // 5. A restore onto a fresh soldier 1 m off the saved spot, turned away.
   Saved = EUS_UnitState.Capture(Smoker);
   Check(Manager.ApplyUnit(Smoker, EUS_Codes.NONE, Report), "the saved smoker is released");
   vector spot[4];
   Math3D.AnglesToMatrix(Vector(90, 0, 0), spot);
   spot[3] = Saved.Anchor + Vector(1, 0, 0);
   // The original stands aside, as a cleared scene would.
   vector aside[4];
   Math3D.AnglesToMatrix(vector.Zero, aside);
   aside[3] = Ground(Saved.Anchor + Vector(0, 0, 15), 0);
   Smoker.Teleport(aside);
   Fresh.Teleport(spot);
   string refused;
   Check(Saved.Restore(Fresh, "fixture", refused), "the saved smoke is queued onto the fresh soldier: " + refused);
   Advance(8);
   return;
  }
  if (Phase == 8)
  {
   EUS_UnitControl restored = Manager.FindControl(Fresh);
   if (!restored || Manager.CountPendingRestores() > 0)
   {
    if (Now() - PhaseAt > EUS_Manager.RESTORE_WAIT + 5)
    {
     Check(false, "the restore bound within the restore wait");
     Finish("restore");
    }
    return;
   }
   float off = vector.DistanceXZ(restored.GetAnchor(), Saved.Anchor);
   float turn = vector.Dot(restored.GetForward(), Saved.Forward);
   PrintFormat("[EUS STATE TEST RESTORE] code=%1 anchorOff=%2 headingDot=%3 pending=%4", restored.GetCode(), off, turn, Manager.CountPendingRestores());
   Check(restored.GetCode() == Saved.Code && Fresh.EUS_Script == Saved.Code, "the fresh soldier runs the saved script");
   Check(off <= 0.4 && turn >= 0.95, "on the saved spot and heading");
   Advance(9);
   return;
  }
  if (Phase == 9)
  {
   if (Now() - PhaseAt < 8)
   {
    return;
   }
   Check(Loitering(Fresh), "the restored pose plays");
   // Damage ends an animation (and only an animation).
   Hit(Stander);
   Advance(10);
   return;
  }
  if (Phase == 10)
  {
   if (StanderControl.IsBound())
   {
    if (Now() - PhaseAt > 10)
    {
     Check(false, "damage ends the animation within 10 s");
     Advance(11);
    }
    return;
   }
   Check(StanderControl.GetEnd() == EUS_EEndReason.DAMAGE, "an animation hit by damage ends with EUS_EEndReason.DAMAGE");
   Advance(11);
   return;
  }
  if (Phase == 11)
  {
   Check(Manager.ApplyUnit(Holder, EUS_Codes.NONE, Report) && Manager.ApplyUnit(Frozen, EUS_Codes.NONE, Report) && Manager.ApplyUnit(Fresh, EUS_Codes.NONE, Report), "Game Master release of the rest");
   Check(HolderControl.GetEnd() == EUS_EEndReason.GAME_MASTER && FrozenControl.GetEnd() == EUS_EEndReason.GAME_MASTER, "a Game Master release ends with EUS_EEndReason.GAME_MASTER");
   Check(Group.EUS_Scripted == 0 && !EUS_Manager.ScriptsOnly(Group), "no scripted member left");
   Finish("completed");
   return;
  }
 }
}

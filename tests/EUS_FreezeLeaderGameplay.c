// TEST ONLY. EXPBG Unit Scripts native fixture: Freeze holds a squad leader, Game
// Master moves set a new spot, the chair pose needs room.
// pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/EUS_FreezeLeaderGameplay.c -TimeoutSeconds 420 -ExpectResult '\[EUS FREEZE TEST RESULT\] checks=[1-9]\d* failures=0 reason=completed' -OrchestratorSlotGranted
// The runner copies this file to EXPG_GarrisonGameplay.c; the class names are fixed.
// Production 2026-10-07: a frozen lone officer drifted 0.78 m in 35 s, a non-Game
// Master displacement was logged as "re-anchored", and "Sit on a chair" next to a
// GM-placed table coincided with the server falling from 240 to 70-145 FPS.
// tests/EUS_UnitScriptsGameplay.c keeps the leader free and freezes a follower
// only; this fixture covers:
//  - Freeze on the fire team LEADER under a squad Move order (30 s), then idle with
//    no waypoint (45 s): within 0.5 m of his spot, a body turn beyond the Freeze
//    limit (EUS_UnitControl.FREEZE_TURN_DOT, about 75 degrees) never lasts longer
//    than one manager tick, and no correction loop. The largest turn is printed so
//    the native run shows whether the limit is loose in practice.
//  - Hold on a follower under the same order: within 1.5 m.
//  - A Game Master move (SCR_EditableCharacterComponent.SetTransform, the editor's
//    own path) sets the new spot; he stays there.
//  - A raw teleport (not the editor) is put back within 3 s and counted; a raw
//    180 degree turn in place is turned back within 3 s and never changes the held
//    heading.
//  - "Sit on a chair" in the open binds and loiters; a Game Master move keeps it and
//    seats him again at the new spot. With a vanilla military table (the same
//    PropFireView collider layer as the production GM table) in front of a frozen
//    soldier it is refused ("no room"), he stays frozen, and for 18 s after the
//    table appears he is put back at most twice and never in the second half.
//  - "Sit on the ground": a Game Master move keeps it bound; a raw 3 m push ends it.
// Frame cost cannot be measured here (the runner caps the server at 60 FPS); the
// chair FPS A/B is a manual server check with the production furniture.
// No players, no GM UI: head tracking and the client view stay in-game checks.
class EXPG_GarrisonGameplayClass : GenericEntityClass {}
class EXPG_GarrisonGameplay : GenericEntity
{
 static const float FIXTURE_SECONDS = 280;
 static const ResourceName SQUAD = "{84E5BBAB25EA23E5}Prefabs/Groups/BLUFOR/Group_US_FireTeam.et";
 static const ResourceName MOVE = "{750A8D1695BD6998}Prefabs/AI/Waypoints/AIWaypoint_Move.et";
 static const ResourceName TABLE = "{7762F50A860DD074}Prefabs/Props/Military/Furniture/TableMilitary_US_01.et";
 // Samples (0.25 s apart) a turn beyond the Freeze limit may last: the manager
 // ticks every scripted unit of this fixture every 0.25 s, plus timing slack.
 static const int TURN_SAMPLES = 3;
 // Corrections per idle or order phase above this are a correction loop.
 static const int LOOP_CORRECTIONS = 10;
 // The table phase: corrections allowed in the whole window, and its length.
 static const int TABLE_CORRECTIONS = 2;
 static const float TABLE_SECONDS = 18;
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
 SCR_ChimeraCharacter Seated;
 SCR_ChimeraCharacter Blocked;
 AIWaypoint Waypoint;
 IEntity Table;
 ref EUS_UnitControl Frozen;
 ref EUS_UnitControl Desk;
 ref EUS_UnitControl Pose;
 vector LeaderAnchor;
 vector LeaderForward;
 vector HeldForward;
 vector HolderAnchor;
 vector MoveTarget;
 vector ChairTarget;
 vector PoseTarget;
 float LeaderMax;
 float LeaderTurn;
 float HolderMax;
 int TurnRun;
 int TurnRunMax;
 int CorrectionsBefore;
 int CorrectionsHalf;
 float TableAt;

 void EXPG_GarrisonGameplay(IEntitySource src, IEntity parent) { SetEventMask(EntityEvent.INIT | EntityEvent.FRAME); }
 float Now() { return GetGame().GetWorld().GetWorldTime() * 0.001; }

 override void EOnInit(IEntity owner)
 {
  if (!Replication.IsServer()) { ClearEventMask(EntityEvent.FRAME); return; }
  Started = Now();
  Next = Started + 10;
  PrintFormat("[EUS FREEZE TEST BEGIN] squad=%1 origin=%2 deadline=%3", SQUAD, Origin, FIXTURE_SECONDS);
 }

 bool Check(bool ok, string label)
 {
  Checks++;
  if (!ok) Failures++;
  PrintFormat("[EUS FREEZE TEST CHECK] pass=%1 %2", ok, label);
  return ok;
 }

 void Finish(string reason)
 {
  if (Finished) return;
  Finished = true;
  ClearEventMask(EntityEvent.FRAME);
  PrintFormat("[EUS FREEZE TEST RESULT] checks=%1 failures=%2 reason=%3", Checks, Failures, reason);
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

 float Moved(SCR_ChimeraCharacter actor, vector anchor)
 {
  if (!actor) return -1;
  return vector.DistanceXZ(actor.GetOrigin(), anchor);
 }

 // Degrees between the soldier's heading and a held heading.
 float Turned(SCR_ChimeraCharacter actor, vector forward)
 {
  if (!actor) return 180;
  return Math.Acos(Math.Clamp(vector.Dot(EUS_Codes.Forward(actor), forward), -1, 1)) * Math.RAD2DEG;
 }

 // The body turn Freeze tolerates before it turns him back (the code's own limit).
 float TurnLimit() { return Math.Acos(EUS_UnitControl.FREEZE_TURN_DOT) * Math.RAD2DEG; }

 // A Game Master move through the editor's own server path.
 bool EditorMove(SCR_ChimeraCharacter actor, vector to)
 {
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(actor);
  if (!editable) return false;
  vector transform[4];
  actor.GetWorldTransform(transform);
  transform[3] = to;
  return editable.SetTransform(transform, true);
 }

 // Not the editor: what physics, another mod or a script teleport looks like.
 void RawMove(SCR_ChimeraCharacter actor, vector to)
 {
  vector transform[4];
  actor.GetWorldTransform(transform);
  transform[3] = to;
  actor.Teleport(transform);
 }

 // Not the editor: another system turning him 180 degrees where he stands.
 void RawTurn(SCR_ChimeraCharacter actor)
 {
  vector transform[4];
  actor.GetWorldTransform(transform);
  transform[0] = vector.Zero - transform[0];
  transform[2] = vector.Zero - transform[2];
  actor.Teleport(transform);
 }

 bool Loitering(SCR_ChimeraCharacter actor)
 {
  SCR_CharacterControllerComponent controller = SCR_CharacterControllerComponent.Cast(actor.GetCharacterController());
  if (!controller) return false;
  if (controller.IsLoitering()) return true;
  SCR_ScriptedCharacterInputContext input = controller.GetScrInputContext();
  return input && input.m_iLoiteringType >= 0;
 }

 void Sample()
 {
  LeaderMax = Math.Max(LeaderMax, Moved(Leader, LeaderAnchor));
  float turn = Turned(Leader, LeaderForward);
  LeaderTurn = Math.Max(LeaderTurn, turn);
  if (turn > TurnLimit()) TurnRun++;
  else TurnRun = 0;
  if (TurnRun > TurnRunMax) TurnRunMax = TurnRun;
  HolderMax = Math.Max(HolderMax, Moved(Holder, HolderAnchor));
 }

 void ResetSamples()
 {
  LeaderMax = 0;
  LeaderTurn = 0;
  HolderMax = 0;
  TurnRun = 0;
  TurnRunMax = 0;
  CorrectionsBefore = Frozen.GetCorrections();
 }

 override void EOnFrame(IEntity owner, float timeSlice)
 {
  if (Finished || Now() < Next) return;
  Next = Now() + 0.25;
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
    else if (!Seated) Seated = member;
    else if (!Blocked) Blocked = member;
   }
   if (!Check(Leader != null && Holder != null && Seated != null && Blocked != null, "leader plus three followers identified")) { Finish("setup"); return; }
   Manager = EUS_Manager.Get();
   if (!Check(Manager != null, "server unit script manager available")) { Finish("setup"); return; }
   Check(Manager.ApplyUnit(Leader, EUS_Codes.FREEZE, Report), "Freeze bound on the squad leader");
   Check(Manager.ApplyUnit(Holder, EUS_Codes.HOLD, Report), "Hold bound on a follower");
   Frozen = Manager.FindControl(Leader);
   if (!Check(Frozen != null, "the leader's control is registered")) { Finish("setup"); return; }
   Advance(2);
   return;
  }
  if (Phase == 2)
  {
   // Past the settle window: the spot and heading are where he came to rest.
   if (Now() - PhaseAt < 3) return;
   LeaderAnchor = Frozen.GetAnchor();
   LeaderForward = Frozen.GetForward();
   HolderAnchor = Holder.GetOrigin();
   Check(Moved(Leader, LeaderAnchor) <= 0.35, "the leader's spot is where he settled after the bind");
   Resource waypointResource = Resource.Load(MOVE);
   IEntity waypointEntity = GetGame().SpawnEntityPrefab(waypointResource, GetGame().GetWorld(), Params(Origin + Vector(40, 0, 0)));
   Waypoint = AIWaypoint.Cast(waypointEntity);
   if (!Check(Waypoint != null, "native Move waypoint spawned 40 m away")) { Finish("waypoint"); return; }
   Group.AddWaypoint(Waypoint);
   ResetSamples();
   Advance(3);
   return;
  }
  if (Phase == 3)
  {
   Sample();
   if (Now() - PhaseAt < 30) return;
   int ordered = Frozen.GetCorrections() - CorrectionsBefore;
   PrintFormat("[EUS FREEZE TEST ORDER] leaderMax=%1 leaderTurn=%2 turnSamples=%3 limit=%4 holderMax=%5 corrections=%6", LeaderMax, LeaderTurn, TurnRunMax, TurnLimit(), HolderMax, ordered);
   Check(Frozen.IsBound() && Leader.EUS_Script == EUS_Codes.FREEZE, "the frozen leader stays bound under a squad Move order");
   Check(LeaderMax <= 0.5, "Freeze keeps the squad leader within 0.5 m for 30 s under a squad Move order");
   Check(TurnRunMax <= TURN_SAMPLES, "under a squad Move order a body turn beyond the Freeze limit is turned back within one manager tick");
   Check(ordered <= LOOP_CORRECTIONS, "Freeze needs no correction loop to hold the squad leader under a squad Move order");
   Check(HolderMax <= 1.5, "Hold keeps a follower within 1.5 m for 30 s under a squad Move order");
   Check(vector.DistanceXZ(Frozen.GetAnchor(), LeaderAnchor) <= 0.01, "the leader's spot never moved without a Game Master move");
   Group.RemoveWaypoint(Waypoint);
   ResetSamples();
   Advance(4);
   return;
  }
  if (Phase == 4)
  {
   // Idle without a waypoint: the vanilla idle tree (Observe glances, take cover) runs.
   Sample();
   if (Now() - PhaseAt < 45) return;
   int idle = Frozen.GetCorrections() - CorrectionsBefore;
   PrintFormat("[EUS FREEZE TEST IDLE] leaderMax=%1 leaderTurn=%2 turnSamples=%3 limit=%4 holderMax=%5 corrections=%6", LeaderMax, LeaderTurn, TurnRunMax, TurnLimit(), HolderMax, idle);
   Check(LeaderMax <= 0.5, "Freeze keeps an idle squad leader within 0.5 m for 45 s");
   Check(TurnRunMax <= TURN_SAMPLES, "an idle frozen leader's body turn beyond the Freeze limit is turned back within one manager tick");
   Check(idle <= LOOP_CORRECTIONS, "Freeze needs no correction loop to hold an idle squad leader");
   MoveTarget = Ground(LeaderAnchor + Vector(5, 0, 0), 0);
   Check(EditorMove(Leader, MoveTarget), "Game Master move of the frozen leader accepted by the editor");
   Advance(5);
   return;
  }
  if (Phase == 5)
  {
   if (Now() - PhaseAt < 4) return;
   float arrived = Moved(Leader, MoveTarget);
   PrintFormat("[EUS FREEZE TEST GM MOVE] distance=%1 anchor=%2 target=%3", arrived, Frozen.GetAnchor(), MoveTarget);
   Check(Frozen.IsBound(), "a Game Master move keeps Freeze bound");
   Check(arrived <= 0.5, "the Game Master move put the leader on the new spot");
   Check(vector.DistanceXZ(Frozen.GetAnchor(), MoveTarget) <= 0.5, "the Game Master move set the leader's new spot");
   LeaderMax = 0;
   Advance(6);
   return;
  }
  if (Phase == 6)
  {
   if (Moved(Leader, MoveTarget) > LeaderMax) LeaderMax = Moved(Leader, MoveTarget);
   if (Now() - PhaseAt < 10) return;
   Check(LeaderMax <= 0.5, "after the Game Master move he stays on the new spot (never pulled back)");
   CorrectionsBefore = Frozen.GetCorrections();
   RawMove(Leader, Ground(MoveTarget + Vector(0, 0, 3), 0));
   Advance(7);
   return;
  }
  if (Phase == 7)
  {
   bool back = Moved(Leader, MoveTarget) <= 0.5 && Frozen.GetCorrections() > CorrectionsBefore;
   if (!back && Now() - PhaseAt < 3) return;
   PrintFormat("[EUS FREEZE TEST RAW MOVE] distance=%1 corrections=%2 before=%3", Moved(Leader, MoveTarget), Frozen.GetCorrections(), CorrectionsBefore);
   Check(back, "a teleport that is not a Game Master move is put back within 3 s");
   Check(vector.DistanceXZ(Frozen.GetAnchor(), MoveTarget) <= 0.5, "a raw teleport never moves the spot");
   HeldForward = Frozen.GetForward();
   CorrectionsBefore = Frozen.GetCorrections();
   RawTurn(Leader);
   Advance(8);
   return;
  }
  if (Phase == 8)
  {
   bool turnedBack = Turned(Leader, HeldForward) <= 15 && Frozen.GetCorrections() > CorrectionsBefore;
   if (!turnedBack && Now() - PhaseAt < 3) return;
   PrintFormat("[EUS FREEZE TEST RAW TURN] turn=%1 corrections=%2 before=%3", Turned(Leader, HeldForward), Frozen.GetCorrections(), CorrectionsBefore);
   Check(turnedBack, "a 180 degree turn that is not a Game Master move is turned back within 3 s");
   Check(vector.Dot(Frozen.GetForward(), HeldForward) > 0.999 && vector.DistanceXZ(Frozen.GetAnchor(), MoveTarget) <= 0.5, "a raw turn never changes the held heading or spot");
   // Chair pose in the open must bind (no false 'no room').
   Report = new EUS_Report();
   bool seated = Manager.ApplyUnit(Seated, EUS_Codes.ANIMATION + 1, Report);
   PrintFormat("[EUS FREEZE TEST CHAIR OPEN] applied=%1 reason='%2'", seated, Report.LastReason);
   Check(seated, "'Sit on a chair' binds on a soldier standing in the open");
   Advance(9);
   return;
  }
  if (Phase == 9)
  {
   if (Now() - PhaseAt < 12) return;
   EUS_UnitControl chair = Manager.FindControl(Seated);
   PrintFormat("[EUS FREEZE TEST CHAIR HELD] bound=%1 loitering=%2", chair != null, Loitering(Seated));
   Check(chair != null, "'Sit on a chair' in the open is still bound after 12 s (no false room or push release)");
   Check(Loitering(Seated), "'Sit on a chair' in the open plays its vanilla loiter");
   ChairTarget = Ground(Seated.GetOrigin() + Vector(0, 0, 4), 0);
   Check(EditorMove(Seated, ChairTarget), "Game Master move of the seated soldier accepted by the editor");
   Advance(10);
   return;
  }
  if (Phase == 10)
  {
   if (Now() - PhaseAt < 12) return;
   EUS_UnitControl moved = Manager.FindControl(Seated);
   PrintFormat("[EUS FREEZE TEST CHAIR MOVE] bound=%1 loitering=%2 distance=%3", moved != null, Loitering(Seated), Moved(Seated, ChairTarget));
   Check(moved != null && Moved(Seated, ChairTarget) <= 1.5, "a Game Master move keeps the chair pose bound on the new spot");
   Check(Loitering(Seated), "the chair pose sits down again at the new spot");
   Check(Manager.ApplyUnit(Seated, EUS_Codes.NONE, Report), "Game Master release of the chair pose");
   Check(Manager.ApplyUnit(Blocked, EUS_Codes.FREEZE, Report), "Freeze bound on the follower for the table test");
   Desk = Manager.FindControl(Blocked);
   if (!Check(Desk != null, "the table follower's control is registered")) { Finish("table"); return; }
   Advance(11);
   return;
  }
  if (Phase == 11)
  {
   // Past the settle window: a vanilla military table 0.7 m in front of his spot.
   if (Now() - PhaseAt < 3) return;
   vector anchor = Desk.GetAnchor();
   vector forward = EUS_Codes.Forward(Blocked);
   EntitySpawnParams spawn = new EntitySpawnParams();
   spawn.TransformMode = ETransformMode.WORLD;
   Math3D.AnglesToMatrix(Vector(forward.ToYaw(), 0, 0), spawn.Transform);
   spawn.Transform[3] = Ground(anchor + forward * 0.7, 0);
   Resource tableResource = Resource.Load(TABLE);
   Table = GetGame().SpawnEntityPrefab(tableResource, GetGame().GetWorld(), spawn);
   Check(Table != null, "vanilla military table spawned in front of a frozen follower");
   TableAt = Now();
   CorrectionsBefore = Desk.GetCorrections();
   CorrectionsHalf = -1;
   Advance(12);
   return;
  }
  if (Phase == 12)
  {
   if (Now() - PhaseAt < 2) return;
   Report = new EUS_Report();
   bool refused = !Manager.ApplyUnit(Blocked, EUS_Codes.ANIMATION + 1, Report);
   PrintFormat("[EUS FREEZE TEST CHAIR BLOCKED] refused=%1 reason='%2' corrections=%3 before=%4", refused, Report.LastReason, Desk.GetCorrections(), CorrectionsBefore);
   Check(refused && Report.LastReason.Contains("no room"), "'Sit on a chair' with a table in front of the soldier is refused with 'no room'");
   Check(Blocked.EUS_Script == EUS_Codes.FREEZE && Desk.IsBound(), "a refused pose keeps the soldier's running Freeze");
   Advance(13);
   return;
  }
  if (Phase == 13)
  {
   // A slow loop (one teleport every second or two) shows only over a longer window.
   if (CorrectionsHalf < 0 && Now() - TableAt >= TABLE_SECONDS * 0.5) CorrectionsHalf = Desk.GetCorrections();
   if (Now() - TableAt < TABLE_SECONDS) return;
   int total = Desk.GetCorrections() - CorrectionsBefore;
   int late = Desk.GetCorrections() - CorrectionsHalf;
   PrintFormat("[EUS FREEZE TEST TABLE] seconds=%1 corrections=%2 secondHalf=%3 bound=%4", TABLE_SECONDS, total, late, Desk.IsBound());
   Check(Desk.IsBound() && Blocked.EUS_Script == EUS_Codes.FREEZE, "the soldier next to the table stays frozen");
   Check(total <= TABLE_CORRECTIONS, "a table next to a frozen soldier never starts a correction loop (18 s window)");
   Check(CorrectionsHalf >= 0 && late == 0, "no correction at all in the second half of the table window");
   if (Table) SCR_EntityHelper.DeleteEntityAndChildren(Table);
   Advance(14);
   return;
  }
  if (Phase == 14)
  {
   if (Now() - PhaseAt < 1) return;
   Check(Manager.ApplyUnit(Blocked, EUS_Codes.ANIMATION, Report), "'Sit on the ground' replaces Freeze once the table is gone");
   Pose = Manager.FindControl(Blocked);
   if (!Check(Pose != null, "the pose control is registered")) { Finish("pose"); return; }
   Advance(15);
   return;
  }
  if (Phase == 15)
  {
   if (Now() - PhaseAt < 5) return;
   PoseTarget = Ground(Blocked.GetOrigin() + Vector(-4, 0, 0), 0);
   Check(EditorMove(Blocked, PoseTarget), "Game Master move of the sitting soldier accepted by the editor");
   Advance(16);
   return;
  }
  if (Phase == 16)
  {
   if (Now() - PhaseAt < 5) return;
   PrintFormat("[EUS FREEZE TEST POSE MOVE] bound=%1 distance=%2 reason='%3'", Pose.IsBound(), Moved(Blocked, PoseTarget), Pose.GetEndReason());
   Check(Pose.IsBound(), "a Game Master move keeps an animation bound");
   RawMove(Blocked, Ground(Blocked.GetOrigin() + Vector(0, 0, -3), 0));
   Advance(17);
   return;
  }
  if (Phase == 17)
  {
   if (Pose.IsBound() && Now() - PhaseAt < 3) return;
   PrintFormat("[EUS FREEZE TEST POSE PUSH] bound=%1 reason='%2'", Pose.IsBound(), Pose.GetEndReason());
   Check(!Pose.IsBound() && Pose.GetEndReason().Contains("pushed"), "a 3 m push that is not a Game Master move ends the animation");
   Check(Blocked.EUS_Script == EUS_Codes.NONE, "the pushed soldier's script code is cleared");
   Check(Manager.ApplyUnit(Leader, EUS_Codes.NONE, Report) && Manager.ApplyUnit(Holder, EUS_Codes.NONE, Report), "Game Master release of Freeze and Hold");
   Check(Leader.EUS_Script == EUS_Codes.NONE && Holder.EUS_Script == EUS_Codes.NONE && !Frozen.IsBound(), "released leader and follower carry no script");
   Finish("completed");
   return;
  }
 }
}

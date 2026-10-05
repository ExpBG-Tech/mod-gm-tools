// All-vanilla stationary pose. No third-party addon, no vehicle compartment, no
// helper prefab. The character is put into an authored SCR_AIAnimation_Base at a
// verified transform through the native AI animation path.
//
// This is a measurement spike, not a finished feature. Three native rungs exist
// between "ask" and "posed" and inspection cannot tell which of them a plain
// civilian group actually honours, so all three are wired in descending order of
// how sanctioned they are and every transition is counted. One campaign then
// says exactly where the route dies:
//
//   rung 0  group animation waypoint  - SCR_AIAnimationWaypoint carrying the
//           script, added to the resident's own group. The most native route:
//           the group activity owns it, combat interrupts it properly.
//   rung 1  direct animate message    - SCR_AIMessageHandling.SendAnimateMessage
//           to the single agent. Same behaviour tree, no waypoint entity.
//   rung 2  direct StartAnimation     - the exact call SCR_AIPlayAnimation makes
//           on the agent's behalf. No AI orchestration at all.
//
// A rung that builds but produces no pose inside its window is a timeout, not a
// construction failure, and the counters separate the two. Nothing here guesses:
// the script is interrogated with the native accessors immediately after it is
// built, so a parameter that silently failed to attach is caught in-campaign
// rather than read as "the AI ignored us".
class EAC_VanillaSeat
{
 // An authored SCR_AIAnimationWaypoint prefab in this addon. Empty means the
 // waypoint rung is skipped; see IssueWaypoint.
 static const ResourceName WAYPOINT_PREFAB = "";
 // See IssueMessage: the message now carries a real SCR_AIAnimateActivity.
 static const bool MESSAGE_RUNG_ENABLED = true;

 // Module setting values. 1 is the ground sit, 2 is the standing table pose.
 static const int MODE_OFF = 0;
 static const int MODE_SIT = 1;
 static const int MODE_TABLE = 2;

 static const int RUNG_WAYPOINT = 0;
 static const int RUNG_MESSAGE = 1;
 static const int RUNG_DIRECT = 2;
 static const int RUNG_DONE = 3;

 // Each rung gets its own window. A native entry animation plus the group's
 // evaluation tick is seconds, not tens of seconds, so a rung that has produced
 // nothing in this long has not merely been slow.
 static const float RUNG_SECONDS = 14;

 protected SCR_ChimeraCharacter m_Actor;
 protected SCR_AIGroup m_Group;
 protected IEntity m_Root;
 protected AIAgent m_Agent;
 protected ref SCR_AIAnimation_Base m_Animation;
 protected ref SCR_AIAnimationScript m_Script;
 // The token vanilla's animate behaviour dereferences. SCR_AIAnimateBehavior.c:32
 // detects a null related activity, calls Fail(), and falls through to line 37
 // with no return - so a null here is unconditionally fatal, not merely ignored.
 // A real SCR_AIAnimateActivity built on the group's utility component, with a
 // null waypoint (InitParameters accepts one), is the smallest thing that is
 // safe there. Held by ref: SetRelatedGroupActivity stores a plain pointer.
 protected ref SCR_AIAnimateActivity m_Activity;
 // When the cancel message was sent. The agent's behaviour tree processes it
 // on its own tick, and Behavior_Animate.bt re-reads the animation's root
 // entity there; run 61 deleted the root the moment the loiter ended and got
 // "Missing root entity to find world position for animation!" from vanilla's
 // GetAnimationScriptParameters node. The root is held this long after the
 // cancel so the tree has left the behaviour before the point goes.
 protected float m_CancelSentAt;
 static const float ROOT_GRACE = 3.0;
 // Campaign 118 (22:48:40, phase 7): vanilla's Behavior_Animate.bt raised
 // "Missing root entity to find world position for animation!" from
 // SCR_AIGetAnimationScriptParameters - a Debug.Error with no WORKBENCH guard,
 // which halts the script VM on the Diag executables.
 //
 // The ROOT_GRACE wait above was gated on m_CancelSentAt > 0, and m_CancelSentAt
 // is only stamped by SendCancel, which RequestStop reaches ONLY when the
 // character is reporting IsLoitering() at that instant. The character
 // controller's loiter flag, the queued loiter type in the input context and the
 // behaviour tree's own state are three different clocks: once the animate
 // message has been sent, the tree holds m_Root until it has actually left the
 // behaviour, whether or not the character still reports loitering and whether or
 // not a cancel was ever sent. A teardown that arrived while the pose was
 // momentarily not reporting therefore skipped the grace entirely and let the
 // activity delete the root on that same tick. The 'lost=1' in the run's seatv
 // line is the same event seen from this side.
 //
 // So the obligation now begins where the BINDING begins, not where the cancel
 // does: m_RootBound is set the moment vanilla is handed m_Root, and every
 // release path below must send a cancel and wait out ROOT_GRACE before the root
 // may go, even if the character never reported a pose at all.
 protected bool m_RootBound;
 protected SCR_AIAnimationWaypoint m_Waypoint;
 protected vector m_Transform[4];
 protected int m_Mode;
 protected int m_Rung = RUNG_WAYPOINT;
 protected float m_Dwell;
 protected float m_RungUntil;
 protected bool m_ScriptUsable, m_Issued, m_Stopping, m_Posed, m_WasPosed;
 protected bool m_StopIssued, m_FastStopIssued, m_Retired;
 protected float m_NextCleanupCheck;

 static bool Enabled(int mode) { return mode == MODE_SIT || mode == MODE_TABLE; }

 // Nothing is asked of the world here beyond building and interrogating the
 // native animation script. Placement was already validated by the station.
 bool Prepare(SCR_ChimeraCharacter actor, SCR_AIGroup group, IEntity root, vector transform[4], int mode, float dwell)
 {
  if (!Replication.IsServer() || !Enabled(mode) || !actor || !group || !root) return false;
  EAC_RoutineStats.RecordVanillaSeat(EAC_RoutineStats.SEATV_START);
  m_Actor = actor; m_Group = group; m_Root = root; m_Mode = mode;
  m_Dwell = Math.Clamp(dwell, 30, 600);
  Math3D.MatrixCopy(transform, m_Transform);
  AIControlComponent control = AIControlComponent.Cast(actor.FindComponent(AIControlComponent));
  if (control) m_Agent = control.GetAIAgent();
  if (!m_Agent) EAC_RoutineStats.RecordVanillaSeat(EAC_RoutineStats.SEATV_NOAGENT);
  if (mode == MODE_TABLE) m_Animation = new SCR_AIAnimation_OfficerMission_Table();
  else m_Animation = new SCR_AIAnimation_Sitting();
  if (!m_Animation) { EAC_RoutineStats.RecordVanillaSeat(EAC_RoutineStats.SEATV_NOSCRIPT); return false; }
  BuildScript();
  // The direct rung needs only the animation class and the actor, so a lost
  // script degrades the route instead of cancelling the routine.
  if (!m_ScriptUsable) m_Rung = RUNG_DIRECT;
  else if (!m_Agent) m_Rung = RUNG_DIRECT;
  return true;
 }

 // Build the native script and then ask the native accessors whether the
 // parameters actually attached. Three campaigns were spent on predicates that
 // were assumed to have taken effect; this one reports.
 protected void BuildScript()
 {
  m_Script = new SCR_AIAnimationScript();
  if (!m_Script) { EAC_RoutineStats.RecordVanillaSeat(EAC_RoutineStats.SEATV_NOSCRIPT); return; }
  // The root entity carries the pose transform, so the script's own offset is
  // identity and the agent is not named: this group holds exactly one agent.
  m_Script.SetAnimationActorName("");
  SCR_AIAnimationWaypointParameters parameters = new SCR_AIAnimationWaypointParameters();
  if (!parameters) { EAC_RoutineStats.RecordVanillaSeat(EAC_RoutineStats.SEATV_NOSCRIPT); return; }
  vector local[4];
  Math3D.MatrixIdentity4(local);
  parameters.SetParameters(local, m_Dwell, m_Animation);
  m_Script.AddAnimationWaypointParameter(parameters, 0);
  if (!m_Script.IsAnimationIndexValid(0) || !m_Script.GetAnimationClass(0) || m_Script.GetAnimationDuration(0) <= 0)
  {
   EAC_RoutineStats.RecordVanillaSeat(EAC_RoutineStats.SEATV_BADINDEX);
   return;
  }
  // Confirm the native root/local composition lands on the seat this activity
  // actually reserved. A silently world-space or dropped transform would pose
  // the resident somewhere else entirely and read as "the AI ignored us".
  vector composed[4];
  m_Script.GetAnimationWorldTransform(m_Root, 0, composed);
  if (vector.Distance(composed[3], m_Transform[3]) > 1)
  {
   EAC_RoutineStats.RecordVanillaSeat(EAC_RoutineStats.SEATV_BADXFORM);
   return;
  }
  m_ScriptUsable = true;
 }

 bool Begin(float now)
 {
  if (!Replication.IsServer() || !m_Actor || m_Stopping) return false;
  return EnterRung(now);
 }

 protected AICommunicationComponent Comms()
 {
  AICommunicationComponent comms;
  if (m_Group) comms = AICommunicationComponent.Cast(m_Group.FindComponent(AICommunicationComponent));
  if (!comms && m_Agent) comms = AICommunicationComponent.Cast(m_Agent.FindComponent(AICommunicationComponent));
  return comms;
 }

 // Issue whatever the current rung asks for. Returns false only when no rung
 // remains; a rung that cannot be issued advances rather than failing the whole
 // occupation, because the point of the spike is to reach the last rung.
 protected bool EnterRung(float now)
 {
  while (m_Rung < RUNG_DONE)
  {
   m_RungUntil = now + RUNG_SECONDS;
   if (m_Rung == RUNG_WAYPOINT && IssueWaypoint()) { m_Issued = true; return true; }
   if (m_Rung == RUNG_MESSAGE && IssueMessage()) { m_Issued = true; return true; }
   if (m_Rung == RUNG_DIRECT && IssueDirect()) { m_Issued = true; return true; }
   m_Rung++;
  }
  return false;
 }

 protected bool IssueWaypoint()
 {
  if (!m_ScriptUsable || !m_Group) return false;
  EntitySpawnParams params = new EntitySpawnParams();
  params.TransformMode = ETransformMode.WORLD;
  Math3D.MatrixCopy(m_Transform, params.Transform);
  // Spawning a scripted waypoint by class with no prefab throws a Virtual
  // Machine Exception (run 41, EAC_IntegratedQa.c:816), and Enforce has no way
  // to catch one. Until this addon ships an authored animation-waypoint prefab,
  // the waypoint rung is recorded as unavailable and the ladder moves on to the
  // two rungs that need no spawned entity.
  if (WAYPOINT_PREFAB.IsEmpty()) { EAC_RoutineStats.RecordVanillaSeat(EAC_RoutineStats.SEATV_WP_SPAWN); return false; }
  m_Waypoint = SCR_AIAnimationWaypoint.Cast(GetGame().SpawnEntityPrefab(Resource.Load(WAYPOINT_PREFAB), GetGame().GetWorld(), params));
  if (!m_Waypoint) { EAC_RoutineStats.RecordVanillaSeat(EAC_RoutineStats.SEATV_WP_SPAWN); return false; }
  // A waypoint spawned without a prefab has no authored script list. Insert the
  // container before the native adder touches it.
  if (!m_Waypoint.m_aAnimationScripts) m_Waypoint.m_aAnimationScripts = {};
  m_Waypoint.SetCompletionRadius(2);
  m_Waypoint.AddAnimationScript(m_Script, 0);
  if (m_Waypoint.m_aAnimationScripts.IsEmpty())
  {
   EAC_RoutineStats.RecordVanillaSeat(EAC_RoutineStats.SEATV_WP_SPAWN);
   SCR_EntityHelper.DeleteEntityAndChildren(m_Waypoint);
   return false;
  }
  m_Group.AddWaypoint(m_Waypoint);
  // The waypoint carries the animation script, whose root is resolved from this
  // seat's entity: vanilla may reference it from this moment on.
  m_RootBound = true;
  EAC_RoutineStats.RecordVanillaSeat(EAC_RoutineStats.SEATV_WP_ADDED);
  return true;
 }

 protected bool IssueMessage()
 {
  // Run 44: SendAnimateMessage with the nullable activity/invoker left null
  // reached SCR_AIAnimateBehavior.OnActionSelected, which threw a Virtual
  // Machine Exception (NULL pointer to instance) at its own line 37 and killed
  // the AI script frame. Off until the requirement at that line is known.
  if (!MESSAGE_RUNG_ENABLED) return false;

  if (!m_ScriptUsable || !m_Agent || !m_Root || !m_Group) return false;
  AICommunicationComponent comms = Comms();
  if (!comms) return false;
  // The activity's own failure path dereferences its utility component
  // (SCR_AIActivity.c:53), so a group without one must not get this rung.
  SCR_AIGroupUtilityComponent utility = SCR_AIGroupUtilityComponent.Cast(m_Group.FindComponent(SCR_AIGroupUtilityComponent));
  if (!utility) return false;
  // Never added to the group utility: with no SCR_AIAnimationWaypoint its own
  // OnActionSelected would early-return anyway. It exists to be non-null and
  // to keep the animated-agent bookkeeping vanilla expects.
  m_Activity = new SCR_AIAnimateActivity(utility, null);
  SCR_AIMessageHandling.SendAnimateMessage(m_Agent, m_Root, m_Script, m_Activity, null, comms, "EXPAC");
  // m_Root is now inside vanilla's animate behaviour. Everything downstream must
  // treat the root as borrowed until a cancel has been sent AND ROOT_GRACE has
  // elapsed - this is the binding campaign 118 deleted out from under.
  m_RootBound = true;
  EAC_RoutineStats.RecordVanillaSeat(EAC_RoutineStats.SEATV_MSG_SENT);
  return true;
 }

 protected bool IssueDirect()
 {
  if (!m_Animation || !m_Actor) return false;
  vector target[4];
  Math3D.MatrixCopy(m_Transform, target);
  if (!m_Animation.StartAnimation(m_Actor, target)) return false;
  EAC_RoutineStats.RecordVanillaSeat(EAC_RoutineStats.SEATV_DIRECT_SENT);
  return true;
 }

 protected void RecordPosed()
 {
  if (m_Rung == RUNG_WAYPOINT) EAC_RoutineStats.RecordVanillaSeat(EAC_RoutineStats.SEATV_WP_POSED);
  else if (m_Rung == RUNG_MESSAGE) EAC_RoutineStats.RecordVanillaSeat(EAC_RoutineStats.SEATV_MSG_POSED);
  else EAC_RoutineStats.RecordVanillaSeat(EAC_RoutineStats.SEATV_DIRECT_POSED);
 }

 protected void RecordTimeout()
 {
  if (m_Rung == RUNG_WAYPOINT) EAC_RoutineStats.RecordVanillaSeat(EAC_RoutineStats.SEATV_WP_TIMEOUT);
  else if (m_Rung == RUNG_MESSAGE) EAC_RoutineStats.RecordVanillaSeat(EAC_RoutineStats.SEATV_MSG_TIMEOUT);
  else EAC_RoutineStats.RecordVanillaSeat(EAC_RoutineStats.SEATV_DIRECT_TIMEOUT);
 }

 // The pose itself, read from the character rather than from what was asked for.
 // Native loiter is the only state every SCR_AIAnimation_Base subclass shares,
 // and proximity keeps an unrelated loiter elsewhere from counting.
 bool IsPosed()
 {
  if (!m_Issued || !m_Actor) return false;
  SCR_CharacterControllerComponent controller = SCR_CharacterControllerComponent.Cast(m_Actor.GetCharacterController());
  if (!controller || !controller.IsLoitering()) return false;
  return vector.Distance(m_Actor.GetOrigin(), m_Transform[3]) < 2.5;
 }

 // Drives the rung ladder. Called once per activity monitor tick.
 void Tick(float now)
 {
  if (!Replication.IsServer() || m_Stopping) return;
  bool posed = IsPosed();
  if (posed)
  {
   if (!m_Posed) { m_Posed = true; m_WasPosed = true; RecordPosed(); }
   return;
  }
  // A pose that existed and then vanished is a different failure from one that
  // never arrived, and only the first says the native route works but does not
  // hold. Do not re-ladder in that case; the occupation ends instead.
  if (m_Posed)
  {
   m_Posed = false;
   EAC_RoutineStats.RecordVanillaSeat(EAC_RoutineStats.SEATV_LOST);
   // Retire the ladder without renaming the rung that worked, so the counters
   // keep attributing the pose to the route that actually produced it.
   m_Retired = true;
   return;
  }
  if (m_Retired || !m_Issued || now < m_RungUntil) return;
  RecordTimeout();
  RetireRung();
  m_Rung++;
  m_Issued = false;
  EnterRung(now);
 }

 // Remove only what this rung put into the world, so the next rung starts from
 // an unencumbered character.
 protected void RetireRung()
 {
  if (m_Rung == RUNG_WAYPOINT) { ClearWaypoint(); return; }
  if (m_Rung == RUNG_MESSAGE) { SendCancel(); return; }
  if (m_Animation && m_Actor) m_Animation.StopAnimation(m_Actor, true);
 }

 protected void ClearWaypoint()
 {
  if (!m_Waypoint) return;
  if (m_Group) m_Group.RemoveWaypoint(m_Waypoint);
  SCR_EntityHelper.DeleteEntityAndChildren(m_Waypoint);
 }

 protected void SendCancel()
 {
  if (!m_Agent) return;
  AICommunicationComponent comms = Comms();
  if (!comms) return;
  SCR_AIMessageHandling.SendCancelMessage(m_Agent, m_Activity, comms, "EXPAC");
  if (m_CancelSentAt <= 0) m_CancelSentAt = GetGame().GetWorld().GetWorldTime() * 0.001;
 }

 // Idempotent. Never touches a possessed actor or a character this machine does
 // not own, matching the rest of the ambient system's stop policy.
 void RequestStop(bool fast = false)
 {
  if (!Replication.IsServer()) return;
  m_Stopping = true;
  ClearWaypoint();
  if (!m_Actor) return;
  CharacterControllerComponent controllerBase = m_Actor.GetCharacterController();
  // A possessed actor or a character this machine does not own keeps whatever
  // pose native code gave it; never write character input in that case.
  if (!controllerBase || controllerBase.IsPlayerControlled()) return;
  RplComponent replication = m_Actor.GetRplComponent();
  if (replication && !replication.IsOwner()) return;
  SCR_CharacterControllerComponent controller = SCR_CharacterControllerComponent.Cast(controllerBase);
  if (!controller) return;
  SCR_ScriptedCharacterInputContext input = controller.GetScrInputContext();
  if (!controller.IsLoitering())
  {
   // A queued entry is not stopped by StopLoitering; clear the pending request
   // exactly the way the loiter-point path does.
   if (input && !input.GetLoiterEntity()) input.m_iLoiteringType = -1;
   return;
  }
  if (m_StopIssued && (!fast || m_FastStopIssued)) return;
  SendCancel();
  if (m_Animation) m_Animation.StopAnimation(m_Actor, fast);
  controller.StopLoitering(fast);
  m_StopIssued = true;
  if (fast) m_FastStopIssued = true;
 }

 // True while vanilla may still resolve the animation root. Once the root has
 // been bound this is false only after a cancel has actually been sent and
 // ROOT_GRACE has elapsed since; a root that was bound and never cancelled holds
 // for ever, which is why both release paths below send the cancel themselves
 // rather than assuming RequestStop managed it.
 bool HoldsRoot(float now)
 {
  if (!m_RootBound) return false;
  if (m_CancelSentAt <= 0) return true;
  return now < m_CancelSentAt + ROOT_GRACE;
 }

 bool RootBound() { return m_RootBound; }

 // Start the release of a bound root exactly once. SendCancel stamps the clock
 // only when the agent and its comms still exist; when they do not, the tree
 // cannot be inside the behaviour either, so the clock is stamped here instead.
 // Without this the hold would be permanent for a character that was cached or
 // deleted mid-pose, and the activity would never release its point.
 protected void BeginRootRelease(float now)
 {
  if (!m_RootBound || m_CancelSentAt > 0) return;
  SendCancel();
  if (m_CancelSentAt <= 0) m_CancelSentAt = now;
 }

 // True once the character is out of the pose and nothing of ours is left in
 // the world. The activity keeps ownership of the point until then.
 bool Cleanup(float now)
 {
  if (!Replication.IsServer()) return false;
  RequestStop();
  if (m_Waypoint) return false;
  // Nothing of ours may be released while the agent's tree can still be inside
  // the animate behaviour that references the root entity. RequestStop only
  // reaches SendCancel when the character happens to report loitering, so send
  // it here for a bound root that has not been cancelled: that both starts the
  // grace clock and tells the tree to leave the behaviour.
  BeginRootRelease(now);
  if (HoldsRoot(now)) return false;
  if (!m_Actor) { m_Activity = null; return true; }
  if (now < m_NextCleanupCheck) return false;
  m_NextCleanupCheck = now + 1;
  SCR_CharacterControllerComponent controller = SCR_CharacterControllerComponent.Cast(m_Actor.GetCharacterController());
  if (!controller) { m_Activity = null; return true; }
  if (controller.IsPlayerControlled()) { m_Activity = null; return true; }
  if (controller.IsLoitering()) return false;
  SCR_ScriptedCharacterInputContext input = controller.GetScrInputContext();
  if (input && !input.GetLoiterEntity() && input.m_iLoiteringType >= 0) return false;
  // Terminal: the loiter has ended and the cancel has been sent. Only now may
  // the activity vanilla's behaviour still points at be allowed to go.
  m_Activity = null;
  return true;
 }

 // The activity's hard drain deadline passed with the pose still reporting
 // (EAC_CivilianActivity.DRAIN_WINDOW_SECONDS, audit item 9). Cleanup returns
 // false for as long as the character reports loitering, so a pose that never
 // ends holds the activity slot for ever; this is the escape hatch. Everything
 // Cleanup does is still done - a fast stop, the cancel message, the waypoint,
 // the animation - and the ONE wait that is kept is ROOT_GRACE, because vanilla's
 // animate behaviour re-reads the root entity on its own tick and releasing
 // inside that window is what produced run 61's "Missing root entity" error.
 bool Drain(float now)
 {
  if (!Replication.IsServer()) return true;
  RequestStop(true);
  if (m_Animation && m_Actor) m_Animation.StopAnimation(m_Actor, true);
  ClearWaypoint();
  // Same unconditional obligation as Cleanup. The drain may bypass the pose
  // wait, but never the root wait: ROOT_GRACE is three seconds against a
  // sixty-second drain window, so this cannot extend a teardown meaningfully and
  // is the only thing standing between a forced release and vanilla's fatal
  // NodeError.
  BeginRootRelease(now);
  if (HoldsRoot(now)) return false;
  m_Activity = null;
  return true;
 }

 int GetMode() { return m_Mode; }
 int GetRung() { return m_Rung; }
 bool EverPosed() { return m_WasPosed; }
 bool ScriptUsable() { return m_ScriptUsable; }

 static string RungName(int rung)
 {
  if (rung == RUNG_WAYPOINT) return "waypoint";
  if (rung == RUNG_MESSAGE) return "message";
  if (rung == RUNG_DIRECT) return "direct";
  return "done";
 }

 bool EAC_ContainsSessionHelper(SCR_EditableEntityComponent candidate) { return EAC_SessionLifecycle.ContainsOwned(candidate, m_Waypoint); }
}

// One Game Master request's outcome, flushed to that Game Master once.
class EUS_Report
{
 // Unit scripts are saved with the mission (EUS_Persistence.c, and the CDF bridge of
 // EXPBG CDF Compat); night discipline is not.
 static const string SAVED = " Saved with the mission (native saves; CDF saves with EXPBG CDF Compat).";
 static const string NOT_SAVED = " Mission-only; not saved.";
 static const string RELEASE_SAVED = " Saves keep the released state; night discipline is mission-only.";

 string Action;
 string Persistence = NOT_SAVED;
 int Applied;
 int Released;
 int Refused;
 int Skipped;
 string LastReason;

 void Refuse(string reason)
 {
  Refused++;
  LastReason = reason;
 }

 string Text()
 {
  string text = Action + ":";
  bool any;
  if (Applied > 0) { text += string.Format(" %1 applied", Applied); any = true; }
  if (Released > 0)
  {
   if (any) text += ",";
   text += string.Format(" %1 released", Released);
   any = true;
  }
  if (Refused > 0)
  {
   if (any) text += ",";
   text += string.Format(" %1 refused (%2)", Refused, LastReason);
   any = true;
  }
  if (Skipped > 0)
  {
   if (any) text += ",";
   text += string.Format(" %1 skipped (request limit %2)", Skipped, EUS_Manager.REQUEST_LIMIT);
   any = true;
  }
  if (!any) text += " nothing to change";
  return text + "." + Persistence;
 }
}

// One saved unit script waiting for its restored character (EUS_UnitState.Restore).
class EUS_PendingRestore
{
 SCR_ChimeraCharacter Actor;
 ref EUS_UnitState State;
 float Deadline;
 string Source;
 string LastReason;
}

// Server-only owner of every unit control and discipline record. One shared,
// budgeted pump runs only while records exist; it stops when the last record
// ends, so an idle mission does no work. Releases are event-driven (damage,
// possession, Game Master) and the pump only drops records whose actor died,
// left AI control or was deleted.
class EUS_Manager
{
 static const int PUMP_MS = 250;
 static const int UNIT_BUDGET = 12;
 static const int GROUP_BUDGET = 2;
 static const int REQUEST_LIMIT = 96;
 // Saved scripts waiting for their characters' AI: at most this many binds per pump,
 // this many rows at once, and this long per row.
 static const int RESTORE_BUDGET = 4;
 static const int RESTORE_LIMIT = 512;
 static const float RESTORE_WAIT = 30;

 protected static ref EUS_Manager s_Instance;
 protected BaseWorld m_World;
 protected ref array<ref EUS_UnitControl> m_Units = {};
 protected ref array<ref EUS_DisciplineRecord> m_Groups = {};
 protected ref array<ref EUS_PendingRestore> m_Restores = {};
 protected int m_RestoreCursor;
 // Current restore batch, logged once when the queue drains.
 protected int m_Restored;
 protected int m_RestoreFailed;
 protected string m_RestoreFailure;
 protected string m_RestoreSource;
 protected ref array<IEntity> m_Players = {};
 protected ref map<int, ref EUS_Report> m_Reports = new map<int, ref EUS_Report>();
 protected int m_UnitCursor;
 protected int m_GroupCursor;
 protected float m_NextPlayers;
 protected bool m_Pumping;
 protected bool m_FlushQueued;

 static EUS_Manager Get()
 {
  if (!Replication.IsServer() || !GetGame() || !GetGame().InPlayMode()) return null;
  BaseWorld world = GetGame().GetWorld();
  if (!world) return null;
  if (!s_Instance || s_Instance.m_World != world)
  {
   if (s_Instance) s_Instance.Stop();
   s_Instance = new EUS_Manager();
   s_Instance.m_World = world;
  }
  return s_Instance;
 }

 // Existing manager of the current world, never created on demand.
 static EUS_Manager Current()
 {
  if (!s_Instance || !GetGame() || s_Instance.m_World != GetGame().GetWorld()) return null;
  return s_Instance;
 }

 // Unit Caching must not enroll squads whose scenes this module holds: a Full
 // cache cycle would delete the actors and silently drop their scripts.
 static bool Reserves(SCR_AIGroup group)
 {
  if (!group || !Current()) return false;
  return group.EUS_Discipline != EUS_Codes.DISCIPLINE_OFF || group.EUS_Scripted > 0;
 }

 // Unit Caching: true when per-soldier scripts are the only reason Unit Scripts
 // reserves or holds this squad (scripted members, night discipline off). Such a
 // squad may be paused in a Simulation zone (EUS_UnitControl.SetCachePaused); night
 // discipline is never paused. Same inputs as Reserves.
 static bool ScriptsOnly(SCR_AIGroup group)
 {
  if (!group || !Current())
  {
   return false;
  }
  return group.EUS_Discipline == EUS_Codes.DISCIPLINE_OFF && group.EUS_Scripted > 0;
 }

 // Unit Caching keeps a squad it already manages awake while any of its living
 // members runs a unit script or the squad has night discipline (see
 // EBG_CacheManager.KeepAwakeReason). Read on every cache tick, so releasing the
 // last script and the discipline ends the hold on the next tick. Empty: no hold.
 static string HoldReason(SCR_AIGroup group)
 {
  EUS_Manager manager = Current();
  if (!group || !manager) return string.Empty;
  int scripted;
  if (!manager.m_Units.IsEmpty())
  {
   array<AIAgent> agents = {};
   group.GetAgents(agents);
   foreach (AIAgent agent : agents)
   {
    SCR_ChimeraCharacter member = SCR_ChimeraCharacter.Cast(agent.GetControlledEntity());
    if (member && member.EUS_Script != EUS_Codes.NONE) scripted++;
   }
  }
  bool disciplined = group.EUS_Discipline != EUS_Codes.DISCIPLINE_OFF;
  if (scripted == 0 && !disciplined) return string.Empty;
  string reason = "Held awake by EXPBG Unit Scripts:";
  if (scripted > 0) reason += string.Format(" %1 scripted", scripted);
  if (scripted > 0 && disciplined) reason += ",";
  if (disciplined) reason += " " + EUS_Codes.DescribeDiscipline(group.EUS_Discipline);
  return reason;
 }

 float Now()
 {
  return m_World.GetWorldTime() * 0.001;
 }

 int CountUnits()
 {
  int count;
  foreach (EUS_UnitControl control : m_Units)
  {
   if (control && control.IsBound()) count++;
  }
  return count;
 }

 EUS_UnitControl FindControl(SCR_ChimeraCharacter actor)
 {
  if (!actor) return null;
  foreach (EUS_UnitControl control : m_Units)
  {
   if (control && control.IsBound() && control.GetActor() == actor) return control;
  }
  return null;
 }

 // Every bound control (saves); returns the count.
 int GetControls(notnull array<EUS_UnitControl> controls)
 {
  controls.Clear();
  foreach (EUS_UnitControl control : m_Units)
  {
   if (control && control.IsBound()) controls.Insert(control);
  }
  return controls.Count();
 }

 EUS_DisciplineRecord FindDiscipline(SCR_AIGroup group)
 {
  if (!group) return null;
  foreach (EUS_DisciplineRecord record : m_Groups)
  {
   if (record && !record.Ended && record.Group == group) return record;
  }
  return null;
 }

 //------------------------------------------------------------------------------------------------
 // Core operations (server). Reports may be null for internal callers.

 bool ApplyUnit(SCR_ChimeraCharacter actor, int code, EUS_Report report)
 {
  EUS_UnitControl existing = FindControl(actor);
  if (code == EUS_Codes.NONE)
  {
   if (!existing) return false;
   existing.ReleaseBy(EUS_EEndReason.GAME_MASTER, "released by the Game Master");
   RefreshScripted(existing.GetGroup());
   if (report) report.Released++;
   return true;
  }
  if (!EUS_Codes.IsScript(code))
  {
   if (report) report.Refuse("unknown unit script");
   return false;
  }
  // A pose without room here is refused before the running script is replaced,
  // so the soldier keeps it (for example Freeze at a desk, then "Sit on a chair").
  // RoomFor checks where he stands and faces now, exactly as Bind does next.
  string reason;
  if (existing) reason = existing.RoomFor(code);
  EUS_UnitControl control;
  if (reason.IsEmpty())
  {
   if (existing)
   {
    existing.ReleaseBy(EUS_EEndReason.REPLACED, "replaced by " + EUS_Codes.Describe(code));
    RefreshScripted(existing.GetGroup());
   }
   control = new EUS_UnitControl();
   if (!control.Bind(actor, code, Now(), reason)) control = null;
  }
  if (!control)
  {
   if (report) report.Refuse(reason);
   PrintFormat("[EUS] refused unit=%1 script='%2' reason='%3'", actor, EUS_Codes.Describe(code), reason);
   return false;
  }
  m_Units.Insert(control);
  RefreshScripted(control.GetGroup());
  if (report) report.Applied++;
  Wake();
  return true;
 }

 bool SetDiscipline(SCR_AIGroup group, int mode, EUS_Report report)
 {
  EUS_DisciplineRecord record = FindDiscipline(group);
  if (mode == EUS_Codes.DISCIPLINE_OFF)
  {
   if (!record) return false;
   record.Release("released by the Game Master");
   m_Groups.RemoveItem(record);
   if (report) report.Released++;
   return true;
  }
  if (mode != EUS_Codes.DISCIPLINE_LIGHT && mode != EUS_Codes.DISCIPLINE_TERROR)
  {
   if (report) report.Refuse("unknown night discipline");
   return false;
  }
  // The current mode again (an edit of several groups with differing values writes
  // every selected group): nothing is re-applied, logged or reported.
  if (record && record.Mode == mode) return false;
  string reason = EUS_DisciplineRecord.Eligibility(group);
  if (!reason.IsEmpty())
  {
   if (report) report.Refuse(reason);
   return false;
  }
  if (!record)
  {
   record = new EUS_DisciplineRecord();
   record.Group = group;
   m_Groups.Insert(record);
  }
  record.Apply(mode);
  if (report) report.Applied++;
  Wake();
  return true;
 }

 // Every living AI member of the squad; budgeted by the request limit.
 int ApplyGroup(SCR_AIGroup group, int code, EUS_Report report)
 {
  if (!group) return 0;
  array<AIAgent> agents = {};
  group.GetAgents(agents);
  int applied;
  foreach (AIAgent agent : agents)
  {
   SCR_ChimeraCharacter member = SCR_ChimeraCharacter.Cast(agent.GetControlledEntity());
   if (member && ApplyUnit(member, code, report)) applied++;
  }
  return applied;
 }

 //------------------------------------------------------------------------------------------------
 // Saved scripts (EUS_UnitState.Restore from a native or CDF load). A save system
 // respawns the character first; his AI agent and group can follow a few frames
 // later, so each row waits in the budgeted pump (at most RESTORE_WAIT seconds)
 // until the soldier can take a script, then binds through ApplyUnit like a Game
 // Master request. One summary line per drained batch.

 // False with the reason when the row is invalid or the queue is full. A second row
 // for the same character replaces the first.
 bool QueueRestore(SCR_ChimeraCharacter actor, EUS_UnitState state, string source, out string reason)
 {
  if (!actor || !state)
  {
   reason = "nothing to restore";
   return false;
  }
  reason = state.Validate();
  if (!reason.IsEmpty())
  {
   return false;
  }
  float now = Now();
  foreach (EUS_PendingRestore queued : m_Restores)
  {
   if (queued.Actor != actor) continue;
   queued.State = state;
   queued.Source = source;
   queued.Deadline = now + RESTORE_WAIT;
   return true;
  }
  if (m_Restores.Count() >= RESTORE_LIMIT)
  {
   reason = string.Format("more than %1 saved unit scripts wait at once", RESTORE_LIMIT);
   return false;
  }
  EUS_PendingRestore pending = new EUS_PendingRestore();
  pending.Actor = actor;
  pending.State = state;
  pending.Source = source;
  pending.Deadline = now + RESTORE_WAIT;
  m_Restores.Insert(pending);
  if (m_RestoreSource.IsEmpty()) m_RestoreSource = source;
  Wake();
  return true;
 }

 int CountPendingRestores()
 {
  return m_Restores.Count();
 }

 // 1 bound, 0 still waiting for the AI, -1 given up (LastReason).
 protected int TryRestore(EUS_PendingRestore pending, float now)
 {
  SCR_ChimeraCharacter actor = pending.Actor;
  if (!actor || actor.IsDeleted())
  {
   pending.LastReason = "the character is gone";
   return -1;
  }
  string reason = EUS_UnitControl.Readiness(actor);
  if (!reason.IsEmpty())
  {
   pending.LastReason = reason;
   if (now < pending.Deadline)
   {
    return 0;
   }
   return -1;
  }
  EUS_UnitState state = pending.State;
  EUS_UnitControl existing = FindControl(actor);
  if (existing && existing.GetCode() == state.Code)
  {
   return 1;
  }
  // Saved where the pose or a correction left him: back on the held spot when the
  // save put him near it, so the script holds exactly the saved spot and heading.
  vector origin = actor.GetOrigin();
  bool placed = vector.DistanceXZ(origin, state.Anchor) <= EUS_UnitState.RESTORE_RADIUS && Math.AbsFloat(origin[1] - state.Anchor[1]) <= EUS_UnitState.RESTORE_RADIUS;
  if (placed) EUS_UnitControl.PlaceAt(actor, state.Anchor, state.Forward);
  EUS_Report report = new EUS_Report();
  if (ApplyUnit(actor, state.Code, report))
  {
   // The teleport lands a frame later, so the bind read the old origin and heading:
   // hold the saved spot and heading explicitly (the editor-move path settles first).
   EUS_UnitControl bound = FindControl(actor);
   if (placed && bound)
   {
    vector held[4];
    held[0] = Vector(state.Forward[2], 0, -state.Forward[0]);
    held[1] = Vector(0, 1, 0);
    held[2] = state.Forward;
    held[3] = state.Anchor;
    bound.OnEditorMoved(held, now);
   }
   return 1;
  }
  pending.LastReason = report.LastReason;
  return -1;
 }

 protected void TickRestores(float now)
 {
  int budget = Math.Min(RESTORE_BUDGET, m_Restores.Count());
  for (int step = 0; step < budget; step++)
  {
   if (m_Restores.IsEmpty()) break;
   if (m_RestoreCursor >= m_Restores.Count()) m_RestoreCursor = 0;
   EUS_PendingRestore pending = m_Restores[m_RestoreCursor];
   int outcome = TryRestore(pending, now);
   if (outcome == 0)
   {
    m_RestoreCursor++;
    continue;
   }
   if (outcome > 0)
   {
    m_Restored++;
   }
   else
   {
    m_RestoreFailed++;
    m_RestoreFailure = pending.State.Describe() + ": " + pending.LastReason;
   }
   m_Restores.RemoveOrdered(m_RestoreCursor);
  }
  if (!m_Restores.IsEmpty() || m_Restored + m_RestoreFailed == 0)
  {
   return;
  }
  string line = string.Format("restored %1 saved unit script(s) from the %2", m_Restored, m_RestoreSource);
  if (m_RestoreFailed > 0) line += string.Format("; %1 not restored (last: %2)", m_RestoreFailed, m_RestoreFailure);
  EUS_UnitControl.Log(line);
  m_Restored = 0;
  m_RestoreFailed = 0;
  m_RestoreFailure = string.Empty;
  m_RestoreSource = string.Empty;
 }

 //------------------------------------------------------------------------------------------------
 // Editor entry points (server).

 void PerformEditorAction(int action, notnull set<SCR_EditableEntityComponent> selection, int playerId)
 {
  string persistence = EUS_Report.SAVED;
  if (action == EUS_Codes.ACTION_RELEASE) persistence = EUS_Report.RELEASE_SAVED;
  EUS_Report report = Report(playerId, EUS_Codes.ActionName(action), persistence);
  string failure = EUS_Authority.Failure(playerId);
  if (!failure.IsEmpty())
  {
   report.Refuse(failure);
   return;
  }
  set<SCR_ChimeraCharacter> actors = new set<SCR_ChimeraCharacter>();
  array<SCR_AIGroup> groups = {};
  foreach (SCR_EditableEntityComponent editable : selection) Collect(editable, actors, groups);

  int code = EUS_Codes.NONE;
  if (action == EUS_Codes.ACTION_HOLD) code = EUS_Codes.HOLD;
  else if (action == EUS_Codes.ACTION_FREEZE) code = EUS_Codes.FREEZE;
  else if (action != EUS_Codes.ACTION_RELEASE)
  {
   report.Refuse("unknown action");
   return;
  }
  int processed;
  foreach (SCR_ChimeraCharacter actor : actors)
  {
   if (processed >= REQUEST_LIMIT) { report.Skipped++; continue; }
   processed++;
   ApplyUnit(actor, code, report);
  }
  if (action == EUS_Codes.ACTION_RELEASE)
  {
   foreach (SCR_AIGroup released : groups) SetDiscipline(released, EUS_Codes.DISCIPLINE_OFF, report);
  }
 }

 void ApplyAttribute(SCR_AttributesManagerEditorComponent attributes, int playerId, SCR_ChimeraCharacter actor, SCR_AIGroup group, int code)
 {
  EUS_Report report = Report(playerId, "EXPBG Unit script: " + EUS_Codes.Describe(code), EUS_Report.SAVED);
  string failure = EUS_Authority.AttributeFailure(attributes, playerId);
  if (!failure.IsEmpty())
  {
   report.Refuse(failure);
   return;
  }
  if (actor) ApplyUnit(actor, code, report);
  if (group) ApplyGroup(group, code, report);
 }

 // "EXPBG Night discipline" in the group's Group tab (EUS_DisciplineAttribute).
 void DisciplineAttribute(SCR_AttributesManagerEditorComponent attributes, int playerId, SCR_AIGroup group, int mode)
 {
  EUS_Report report = Report(playerId, "EXPBG Night discipline: " + EUS_Codes.DescribeDiscipline(mode), EUS_Report.NOT_SAVED);
  string failure = EUS_Authority.AttributeFailure(attributes, playerId);
  if (!failure.IsEmpty())
  {
   report.Refuse(failure);
   return;
  }
  SetDiscipline(group, mode, report);
 }

 protected void Collect(SCR_EditableEntityComponent editable, notnull set<SCR_ChimeraCharacter> actors, notnull array<SCR_AIGroup> groups)
 {
  if (!editable) return;
  IEntity owner = editable.GetOwner();
  SCR_ChimeraCharacter actor = SCR_ChimeraCharacter.Cast(owner);
  if (actor)
  {
   actors.Insert(actor);
   return;
  }
  SCR_AIGroup group = SCR_AIGroup.Cast(owner);
  if (!group) return;
  if (!groups.Contains(group)) groups.Insert(group);
  array<AIAgent> agents = {};
  group.GetAgents(agents);
  foreach (AIAgent agent : agents)
  {
   SCR_ChimeraCharacter member = SCR_ChimeraCharacter.Cast(agent.GetControlledEntity());
   if (member) actors.Insert(member);
  }
 }

 // Attribute writes arrive once per edited entity; one summary per player and frame.
 // The first request of the frame names the action and its save note.
 protected EUS_Report Report(int playerId, string action, string persistence)
 {
  EUS_Report report;
  if (!m_Reports.Find(playerId, report))
  {
   report = new EUS_Report();
   report.Action = action;
   report.Persistence = persistence;
   m_Reports.Insert(playerId, report);
  }
  if (!m_FlushQueued)
  {
   m_FlushQueued = true;
   GetGame().GetCallqueue().CallLater(FlushReports, 100, false);
  }
  return report;
 }

 protected void FlushReports()
 {
  m_FlushQueued = false;
  foreach (int playerId, EUS_Report report : m_Reports) EUS_Feedback.Send(playerId, report.Text());
  m_Reports.Clear();
 }

 // Replicated member count for client-side context menu visibility.
 protected void RefreshScripted(SCR_AIGroup group)
 {
  if (!group) return;
  int count;
  foreach (EUS_UnitControl control : m_Units)
  {
   if (control && control.IsBound() && control.GetGroup() == group) count++;
  }
  group.EUS_SetScripted(count);
 }

 //------------------------------------------------------------------------------------------------
 // Budgeted pump.

 protected void Wake()
 {
  if (m_Pumping) return;
  m_Pumping = true;
  GetGame().GetCallqueue().CallLater(Pump, PUMP_MS, true);
 }

 protected void Stop()
 {
  if (!m_Pumping) return;
  m_Pumping = false;
  GetGame().GetCallqueue().Remove(Pump);
 }

 protected void RefreshPlayers(float now)
 {
  m_NextPlayers = now + 1;
  m_Players.Clear();
  PlayerManager playerManager = GetGame().GetPlayerManager();
  if (!playerManager) return;
  array<int> ids = {};
  playerManager.GetPlayers(ids);
  foreach (int id : ids)
  {
   SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(playerManager.GetPlayerControlledEntity(id));
   if (!character || !character.GetCharacterController() || character.GetCharacterController().IsDead()) continue;
   m_Players.Insert(character);
  }
 }

 protected void Pump()
 {
  if (!GetGame() || GetGame().GetWorld() != m_World)
  {
   Stop();
   return;
  }
  float now = Now();
  if (now >= m_NextPlayers) RefreshPlayers(now);
  TickUnits(now);
  TickGroups();
  TickRestores(now);
  if (m_Units.IsEmpty() && m_Groups.IsEmpty() && m_Restores.IsEmpty()) Stop();
 }

 protected void TickUnits(float now)
 {
  int budget = Math.Min(UNIT_BUDGET, m_Units.Count());
  for (int step = 0; step < budget; step++)
  {
   if (m_Units.IsEmpty()) return;
   if (m_UnitCursor >= m_Units.Count()) m_UnitCursor = 0;
   EUS_UnitControl control = m_Units[m_UnitCursor];
   if (control && control.Tick(now, m_Players))
   {
    m_UnitCursor++;
    continue;
   }
   SCR_AIGroup group;
   if (control)
   {
    group = control.GetGroup();
    control.ReleaseBy(EUS_EEndReason.REMOVED, "the unit died, left AI control or was removed");
   }
   m_Units.RemoveOrdered(m_UnitCursor);
   RefreshScripted(group);
  }
 }

 protected void TickGroups()
 {
  int budget = Math.Min(GROUP_BUDGET, m_Groups.Count());
  for (int step = 0; step < budget; step++)
  {
   if (m_Groups.IsEmpty()) return;
   if (m_GroupCursor >= m_Groups.Count()) m_GroupCursor = 0;
   EUS_DisciplineRecord record = m_Groups[m_GroupCursor];
   if (record && record.Tick(m_Players))
   {
    m_GroupCursor++;
    continue;
   }
   m_Groups.RemoveOrdered(m_GroupCursor);
  }
 }
}

// Unit Caching must not enroll scripted squads (see EUS_Manager.Reserves) and
// keeps a squad it already manages awake while scripted (EUS_Manager.HoldReason).
modded class EBG_CacheManager
{
 override bool IsReserved(SCR_AIGroup group)
 {
  if (EUS_Manager.Reserves(group)) return true;
  return super.IsReserved(group);
 }

 override string KeepAwakeReason(SCR_AIGroup group)
 {
  string held = EUS_Manager.HoldReason(group);
  if (!held.IsEmpty()) return held;
  return super.KeepAwakeReason(group);
 }
}

// One Game Master request's outcome, flushed to that Game Master once.
class EUS_Report
{
 string Action;
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
  return text + ". Mission-only; not saved.";
 }
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

 protected static ref EUS_Manager s_Instance;
 protected BaseWorld m_World;
 protected ref array<ref EUS_UnitControl> m_Units = {};
 protected ref array<ref EUS_DisciplineRecord> m_Groups = {};
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
   existing.Release("released by the Game Master");
   RefreshScripted(existing.GetGroup());
   if (report) report.Released++;
   return true;
  }
  if (!EUS_Codes.IsScript(code))
  {
   if (report) report.Refuse("unknown unit script");
   return false;
  }
  if (existing)
  {
   existing.Release("replaced by " + EUS_Codes.Describe(code));
   RefreshScripted(existing.GetGroup());
  }
  EUS_UnitControl control = new EUS_UnitControl();
  string reason;
  if (!control.Bind(actor, code, Now(), reason))
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
 // Editor entry points (server).

 void PerformEditorAction(int action, notnull set<SCR_EditableEntityComponent> selection, int playerId)
 {
  EUS_Report report = Report(playerId, EUS_Codes.ActionName(action));
  string failure = EUS_Authority.Failure(playerId);
  if (!failure.IsEmpty())
  {
   report.Refuse(failure);
   return;
  }
  set<SCR_ChimeraCharacter> actors = new set<SCR_ChimeraCharacter>();
  array<SCR_AIGroup> groups = {};
  foreach (SCR_EditableEntityComponent editable : selection) Collect(editable, actors, groups);

  if (action == EUS_Codes.ACTION_LIGHT || action == EUS_Codes.ACTION_TERROR)
  {
   // Discipline is per squad; a selected soldier stands for his squad.
   foreach (SCR_ChimeraCharacter soldier : actors)
   {
    SCR_AIGroup squad = soldier.GetCharacterGroup();
    if (squad && !groups.Contains(squad)) groups.Insert(squad);
   }
   int mode = EUS_Codes.DISCIPLINE_LIGHT;
   if (action == EUS_Codes.ACTION_TERROR) mode = EUS_Codes.DISCIPLINE_TERROR;
   foreach (SCR_AIGroup disciplined : groups) SetDiscipline(disciplined, mode, report);
   return;
  }

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
  EUS_Report report = Report(playerId, "EXPBG Unit script: " + EUS_Codes.Describe(code));
  string failure = EUS_Authority.AttributeFailure(attributes, playerId);
  if (!failure.IsEmpty())
  {
   report.Refuse(failure);
   return;
  }
  if (actor) ApplyUnit(actor, code, report);
  if (group) ApplyGroup(group, code, report);
 }

 void DisciplineAttribute(SCR_AttributesManagerEditorComponent attributes, int playerId, SCR_AIGroup group, int mode)
 {
  EUS_Report report = Report(playerId, "EXPBG Night discipline: " + EUS_Codes.DescribeDiscipline(mode));
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
 protected EUS_Report Report(int playerId, string action)
 {
  EUS_Report report;
  if (!m_Reports.Find(playerId, report))
  {
   report = new EUS_Report();
   report.Action = action;
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
  if (m_Units.IsEmpty() && m_Groups.IsEmpty()) Stop();
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
    control.Release("the unit died, left AI control or was removed");
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

// Unit Caching must not enroll scripted squads (see EUS_Manager.Reserves).
modded class EBG_CacheManager
{
 override bool IsReserved(SCR_AIGroup group)
 {
  if (EUS_Manager.Reserves(group)) return true;
  return super.IsReserved(group);
 }
}

// Server-only provenance for the editor's exact newly returned group. Initial
// synchronous members are captured before editor placement callbacks run; delayed
// members are admitted only inside vanilla SpawnGroupMember's synchronous AddAI.
modded class SCR_AIGroup
{
 protected ref array<IEntity> m_EXPG_FreshActors;
 protected int m_EXPG_FreshExpected;
 protected bool m_EXPG_FreshInvalid;
 protected bool m_EXPG_InMemberSpawn;
 protected IEntity m_EXPG_ExpectedRemoval;

 bool EXPG_BeginFreshRoster(int expected)
 {
  if (!Replication.IsServer() || m_EXPG_FreshActors || expected < 1 || expected > 32) { return false; }
  m_EXPG_FreshActors = {};
  m_EXPG_FreshExpected = expected;
  m_EXPG_FreshInvalid = false;
  array<AIAgent> agents = {};
  GetAgents(agents);
  foreach (AIAgent agent : agents)
  {
   IEntity actor;
   if (agent) { actor = agent.GetControlledEntity(); }
   if (!actor || m_EXPG_FreshActors.Contains(actor)) { m_EXPG_FreshInvalid = true; }
   else { m_EXPG_FreshActors.Insert(actor); }
  }
  return EXPG_FreshRosterMatches();
 }

 bool EXPG_FreshRosterMatches(int count = -1)
 {
  if (!m_EXPG_FreshActors || m_EXPG_FreshInvalid || m_EXPG_ExpectedRemoval) { return false; }
  array<AIAgent> agents = {};
  GetAgents(agents);
  if (agents.Count() != m_EXPG_FreshActors.Count() || agents.Count() > m_EXPG_FreshExpected) { return false; }
  if (count >= 0 && agents.Count() != count) { return false; }
  foreach (AIAgent agent : agents)
  {
   if (!agent || !agent.GetControlledEntity() || !m_EXPG_FreshActors.Contains(agent.GetControlledEntity())) { return false; }
  }
  return true;
 }

 bool EXPG_ExpectFreshRemoval(IEntity actor)
 {
  if (!actor || !EXPG_FreshRosterMatches() || !m_EXPG_FreshActors.Contains(actor)) { return false; }
  m_EXPG_ExpectedRemoval = actor;
  return true;
 }

 bool EXPG_FreshRequestMatches(int count)
 {
  return count == m_EXPG_FreshExpected && EXPG_FreshRosterMatches();
 }

 void EXPG_EndFreshRoster()
 {
  m_EXPG_FreshActors = null;
  m_EXPG_ExpectedRemoval = null;
  m_EXPG_InMemberSpawn = false;
  m_EXPG_FreshExpected = 0;
 }

 protected override bool SpawnGroupMember(bool snapToTerrain, int index, ResourceName res, bool editMode, bool isLast)
 {
  if (!m_EXPG_FreshActors) { return super.SpawnGroupMember(snapToTerrain, index, res, editMode, isLast); }
  if (m_EXPG_InMemberSpawn) { m_EXPG_FreshInvalid = true; }
  m_EXPG_InMemberSpawn = true;
  bool result = super.SpawnGroupMember(snapToTerrain, index, res, editMode, isLast);
  m_EXPG_InMemberSpawn = false;
  return result;
 }

 override void OnAgentAdded(AIAgent child)
 {
  if (m_EXPG_FreshActors)
  {
   IEntity actor;
   if (child) { actor = child.GetControlledEntity(); }
   if (!m_EXPG_InMemberSpawn || !actor || m_EXPG_FreshActors.Contains(actor) || m_EXPG_FreshActors.Count() >= m_EXPG_FreshExpected)
    { m_EXPG_FreshInvalid = true; }
   else { m_EXPG_FreshActors.Insert(actor); }
   // Close admission before native invokers: reentrant listener additions are
   // external changes, even while the outer SpawnGroupMember is still on stack.
   m_EXPG_InMemberSpawn = false;
  }
  super.OnAgentAdded(child);
 }

 override void OnAgentRemoved(AIAgent child)
 {
  if (m_EXPG_FreshActors)
  {
   IEntity actor;
   if (child) { actor = child.GetControlledEntity(); }
   if (!actor || actor != m_EXPG_ExpectedRemoval) { m_EXPG_FreshInvalid = true; }
   else { m_EXPG_FreshActors.RemoveItem(actor); m_EXPG_ExpectedRemoval = null; }
  }
  super.OnAgentRemoved(child);
 }
}

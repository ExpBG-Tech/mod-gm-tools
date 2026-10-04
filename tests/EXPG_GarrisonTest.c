// Test-only native smoke entry. Compile in the isolated fixture, not the addon.
class EXPG_GarrisonTest
{
 static bool CheckRecord(EXPG_GarrisonRecord record, int expectedAlive)
 {
  if (!record || !record.Group || !record.Ready || record.ReleaseRequested) { return false; }
  array<AIAgent> agents = {};
  record.Group.GetAgents(agents);
  int live;
  foreach (EXPG_GarrisonMember member : record.Members)
  {
   if (member.CacheMember.Dead) { continue; }
   SCR_ChimeraCharacter actor = member.CacheMember.Entity;
   if (!actor || actor.GetCharacterGroup() != record.Group) { return false; }
   if (!record.Plan.Inside(actor.GetOrigin())) { return false; }
   if (member.Fixed && vector.DistanceSq(actor.GetOrigin(), record.Plan.Nodes[member.NodeIndex].Position) > 0.16) { return false; }
   live++;
  }
  bool passed = live == expectedAlive && agents.Count() == expectedAlive;
  PrintFormat("[EXPG ROSTER RESULT] pass=%1 alive=%2 expected=%3", passed, live, expectedAlive);
  return passed;
 }
}

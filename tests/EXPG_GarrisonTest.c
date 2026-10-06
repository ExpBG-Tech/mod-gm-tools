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
   // NodeIndex -1: an added squad's post around the building, outside the plan.
   if (member.NodeIndex >= 0 && !record.Plan.Inside(actor.GetOrigin())) { return false; }
   if (member.Fixed && vector.DistanceSq(actor.GetOrigin(), member.PostPoint()) > 0.16) { return false; }
   live++;
  }
  bool passed = live == expectedAlive && agents.Count() == expectedAlive;
  PrintFormat("[EXPG ROSTER RESULT] pass=%1 alive=%2 expected=%3", passed, live, expectedAlive);
  return passed;
 }
}

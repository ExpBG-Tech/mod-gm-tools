// Native behavior fixture helpers. NOT RUN; use freshly spawned disposable AI.
// A real physical plan must be supplied (do not fabricate reachable slots).
class EXPG_TrimTest
{
 // Disposable native groups only. Exercise equal-count replacement rather than
 // changing a counter: the foreign actor must survive and adoption must refuse.
 static bool RejectSameCountReplacement(SCR_AIGroup fresh, SCR_ChimeraCharacter original, SCR_ChimeraCharacter foreign, IEntity building)
 {
  if (!fresh || !original || !foreign || foreign.GetCharacterGroup() == fresh) { return false; }
  int count = fresh.GetAgentsCount();
  if (!fresh.EXPG_BeginFreshRoster(count) || !fresh.EXPG_FreshRosterMatches(count)) { return false; }
  fresh.RemoveAIEntityFromGroup(original);
  fresh.AddAIEntityToGroup(foreign);
  EXPG_GarrisonManager manager = EXPG_GarrisonManager.Get();
  bool refused = manager && !manager.AdoptFresh(fresh, building, 0, count);
  bool intact = fresh.GetAgentsCount() == count && foreign.GetCharacterGroup() == fresh && !foreign.GetCharacterController().IsDead() && original;
  bool invalid = !fresh.EXPG_FreshRosterMatches(count);
  fresh.EXPG_EndFreshRoster();
  return refused && intact && invalid;
 }

 static bool RejectRemoveAndRejoin(SCR_AIGroup fresh, SCR_ChimeraCharacter original)
 {
  if (!fresh || !original || original.GetCharacterGroup() != fresh) { return false; }
  int count = fresh.GetAgentsCount();
  if (!fresh.EXPG_BeginFreshRoster(count)) { return false; }
  fresh.RemoveAIEntityFromGroup(original);
  fresh.AddAIEntityToGroup(original);
  bool refused = fresh.GetAgentsCount() == count && !fresh.EXPG_FreshRosterMatches(count);
  fresh.EXPG_EndFreshRoster();
  return refused;
 }

 static bool KeptFreshLeader(EXPG_GarrisonRecord record, IEntity originalLeader, int safeSlots)
 {
  if (!record || !record.Ready || record.Members.Count() != safeSlots || record.FreshRequested != 0) { return false; }
  if (!record.Group || record.Group.GetAgentsCount() != safeSlots) { return false; }
  foreach (EXPG_GarrisonMember member : record.Members)
  {
   if (member.CacheMember.Entity == originalLeader) { return true; }
  }
  return false;
 }

 static bool ExistingRosterUntouched(SCR_AIGroup group, array<IEntity> originals)
 {
  if (!group || group.GetAgentsCount() != originals.Count()) { return false; }
  foreach (IEntity original : originals)
  {
   SCR_ChimeraCharacter actor = SCR_ChimeraCharacter.Cast(original);
   if (!actor || actor.GetCharacterGroup() != group || actor.GetCharacterController().IsDead()) { return false; }
  }
  return true;
 }
 // Run Adopt on an oversized existing squad: refused before deletion/placement.
 // Run AdoptFresh on the same prefab freshly spawned: min(requested, slots),
 // original leader kept, native deletion acknowledged. Zero slots, protected
 // surplus, death, late possession and changed membership must refuse safely;
 // a later scheduler tick/cache wake must never trim again or refill the roster.
}

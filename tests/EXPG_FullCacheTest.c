// Authored before implementation. Native execution NOT RUN by the source agent.
// Run in an authority gameplay fixture; the scalar contract alone proves no AI timing.
class EXPG_FullLedgerFixture : EXPG_FullCache
{
 void AddAbsent(EBG_CacheMember member, bool created, bool retainedOriginal = false)
 {
  EBG_PrefabSurvivor row = new EBG_PrefabSurvivor();
  row.Member = member;
  row.Created = created;
  m_Survivors.Insert(row);
  if (retainedOriginal)
  {
   // Model an original observed alive immediately after refused deletion,
   // then externally deleted before retry: the engine has nulled its identity.
   EXPG_FullOriginal original = new EXPG_FullOriginal();
   original.Member = member;
   m_Originals.Insert(original);
  }
 }
}

class EXPG_FullCacheTest
{
 static bool CreatedSlotContract()
 {
  EXPG_FullLedgerFixture ledger = new EXPG_FullLedgerFixture();
  EBG_CacheMember sleeping = new EBG_CacheMember();
  EBG_CacheMember removedAfterWake = new EBG_CacheMember();
  EBG_CacheMember removedRetainedOriginal = new EBG_CacheMember();
  ledger.AddAbsent(sleeping, false);
  ledger.AddAbsent(removedAfterWake, true);
  ledger.AddAbsent(removedRetainedOriginal, false, true);
  ledger.ObserveAndHold();
  if (sleeping.Dead || !removedAfterWake.Dead || !removedRetainedOriginal.Dead) { return false; }
  ledger.ObserveAndHold();
  return !sleeping.Dead && removedAfterWake.Dead && removedRetainedOriginal.Dead && ledger.GetMemberCount() == 3;
 }

 static bool CapacityContract()
 {
  return EXPG_GarrisonManager.PlacementCount(4, 1) == 1
   && EXPG_GarrisonManager.PlacementCount(4, 0) == 0
   && EXPG_GarrisonManager.PlacementCount(2, 8) == 2
   && EXPG_GarrisonManager.PlacementCount(33, 8) == 0;
 }

 // Invoke after an injected native first AddAIEntityToGroup failure. Observation
 // must keep the exact newborn and its slot pinned, rather than retire/respawn it.
 static bool FailedFirstJoinRetained(EXPG_FullCache full, EBG_CacheMember member, SCR_ChimeraCharacter newborn)
 {
  if (!full || !member || !newborn || newborn.GetCharacterGroup() || member.Entity != newborn) { return false; }
  int before = full.GetMemberCount();
  if (full.ObserveAndHold()) { return false; }
  if (full.GetState() == EBG_FullGroupPhase.FAILED) { full.BeginWake(); }
  full.Poll(); // Retry must not reclaim an actor removed by a native listener.
  return full.GetMemberCount() == before && member.Entity == newborn && !newborn.GetCharacterGroup() && !member.Dead && full.FindHold(member) && !full.ReleaseRestored();
 }

 static bool FirstJoinRecovered(EXPG_FullCache full, EBG_CacheMember member, SCR_ChimeraCharacter newborn, SCR_AIGroup originalGroup)
 {
  // The fixture/operator must explicitly regroup this SAME actor first. The
  // adapter never issues its own retry because removal provenance is ambiguous.
  if (!full || !member || !newborn || !originalGroup || newborn.GetCharacterGroup() != originalGroup) { return false; }
  int before = full.GetMemberCount();
  if (full.GetState() == EBG_FullGroupPhase.FAILED) { full.BeginWake(); }
  full.Poll();
  return full.GetMemberCount() == before && member.Entity == newborn && newborn.GetCharacterGroup() == originalGroup && full.FindHold(member);
 }

 // Native partial-deletion fixture: transfer one retained original to another
 // real group before retry. Observation must permanently retire that slot;
 // a subsequent base Poll must not AddAIEntityToGroup it back to the garrison.
 static bool RetainedTransferContract(EXPG_FullCache full, EBG_CacheMember member, SCR_ChimeraCharacter original, SCR_AIGroup receivingGroup)
 {
  if (!full || !member || !original || !receivingGroup || original.GetCharacterGroup() != receivingGroup) { return false; }
  int before = full.GetMemberCount();
  full.ObserveAndHold();
  if (full.GetMemberCount() != before - 1 || member.Dead || member.Entity != original) { return false; }
  if (full.GetState() == EBG_FullGroupPhase.FAILED) { full.BeginWake(); }
  full.Poll();
  return original && original.GetCharacterGroup() == receivingGroup && member.Entity == original && !member.Dead && full.GetMemberCount() == before - 1;
 }

 static bool RestoredAssignments(EXPG_GarrisonRecord record, array<vector> savedPositions, SCR_AIGroup originalGroup)
 {
  if (!record || record.Full || record.Group != originalGroup || savedPositions.Count() != record.Members.Count()) { return false; }
  foreach (int i, EXPG_GarrisonMember member : record.Members)
  {
   if (member.CacheMember.Dead || member.CacheMember.WasPlayer) { continue; }
   SCR_ChimeraCharacter actor = member.CacheMember.Entity;
   if (!actor || actor.GetCharacterGroup() != originalGroup || vector.DistanceSq(actor.GetOrigin(), savedPositions[i]) > 0.0025) { return false; }
   if (member.Fixed && !member.Post) { return false; }
   if (!member.Fixed && !member.Patrol) { return false; }
  }
  return true;
 }

 // Native sequence: save transforms+assignments mid-edge, capture, wake one
 // survivor, kill/delete it, finish wake, repeat a second Full cycle. The first
 // CREATED slot must remain dead and total survivors must never increase.
 // Also exercise possession/regroup, refused deletion, missing group, Force
 // Move/save preparation while restoring, overlapping Optimizer zones, and a
 // rebind failure: ownership/AI holds must remain until recovery or handoff.
 static bool NoReplenishment(EXPG_GarrisonRecord record, int maximumAlive)
 {
  if (!record || !record.Group) { return false; }
  int alive;
  foreach (EXPG_GarrisonMember member : record.Members)
  {
   if (member.CacheMember.Dead)
   {
    if (member.CacheMember.Entity && !member.CacheMember.Entity.GetCharacterController().IsDead()) { return false; }
    continue;
   }
   if (member.CacheMember.Entity) { alive++; }
  }
  return alive <= maximumAlive && record.Group.GetAgentsCount() <= maximumAlive;
 }
}

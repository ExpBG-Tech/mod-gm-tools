// Counts reserved and active residents together, including drivers/passengers.
class EAC_PopulationBudget
{
 protected ref map<int, int> m_Parties = new map<int, int>();
 protected ref map<EAC_ResidentClaim, int> m_Residents = new map<EAC_ResidentClaim, int>();
 protected int m_Used;

 bool TryReserve(int partyId, int residents, int limit)
 {
  if (partyId <= 0 || residents <= 0 || limit < 0 || m_Parties.Contains(partyId)) return false;
  // Subtraction avoids overflow, and lowering the limit never deletes a party.
  if (m_Used > limit || residents > limit - m_Used) return false;
  m_Parties.Insert(partyId, residents);
  m_Used += residents;
  return true;
 }

 bool Release(int partyId)
 {
  if (!m_Parties.Contains(partyId)) return false;
  m_Used -= m_Parties.Get(partyId);
  m_Parties.Remove(partyId);
  return true;
 }

 int GetUsed() { return m_Used; }

 // Claim identity is separate from numeric party IDs; both consume the same cap.
 bool TryReserveResident(EAC_ResidentClaim claim, int limit)
 {
  if (!claim || limit <= 0 || m_Residents.Contains(claim) || m_Used >= limit) return false;
  m_Residents.Insert(claim, 1);
  m_Used++;
  return true;
 }

 bool ReleaseResident(EAC_ResidentClaim claim)
 {
  if (!claim || !m_Residents.Contains(claim)) return false;
  m_Used -= m_Residents.Get(claim);
  m_Residents.Remove(claim);
  return true;
 }

}

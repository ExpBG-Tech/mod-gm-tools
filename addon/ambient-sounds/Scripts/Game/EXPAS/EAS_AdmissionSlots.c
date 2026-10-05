// Per-world bounded admission. Authority leases a free slot; replicas accept the
// newest generation even when another entity's removal has not arrived yet.
class EAS_AdmissionSlots
{
 protected ref array<Managed> m_Owners = {};
 protected ref array<int> m_Generations = {};
 protected int m_NextGeneration;
 void EAS_AdmissionSlots() { m_Owners.Resize(32); m_Generations.Resize(32); }
 Managed Get(int slot)
 {
  if (slot < 0 || slot >= 32) return null;
  return m_Owners[slot];
 }
 int Generation(int slot)
 {
  if (slot < 0 || slot >= 32) return 0;
  return m_Generations[slot];
 }
 int FirstFree()
 {
  for (int i = 0; i < 32; i++) if (!m_Owners[i]) return i;
  return -1;
 }
 bool Empty()
 {
  for (int i = 0; i < 32; i++) if (m_Owners[i]) return false;
  return true;
 }
 int Claim(Managed owner)
 {
  if (!owner) return -1;
  int existing = m_Owners.Find(owner);
  if (existing >= 0) return existing;
  int slot = FirstFree();
  if (slot < 0) return -1;
  m_NextGeneration++;
  m_Owners[slot] = owner; m_Generations[slot] = m_NextGeneration;
  return slot;
 }
 bool Accept(Managed owner, int slot, int generation)
 {
  if (!owner || slot < 0 || slot >= 32 || generation <= 0 || generation < m_Generations[slot]) return false;
  if (generation == m_Generations[slot]) return m_Owners[slot] == owner;
  m_Owners[slot] = owner; m_Generations[slot] = generation;
  m_NextGeneration = Math.Max(m_NextGeneration, generation);
  return true;
 }
 bool Remove(Managed owner)
 {
  if (!owner) return false;
  int slot = m_Owners.Find(owner);
  if (slot < 0) return false;
  // Keep the generation tombstone through same-world idle periods.
  m_Owners[slot] = null;
  return true;
 }
}

class EAC_ResidentRecord
{
 int Id;
 int HomeId;
 int Slot;
 bool Wanted;
 bool Dead;
 // Unexpected entity removal is terminal for admission, without claiming death.
 bool Removed;
 ResourceName CharacterPrefab;
 // Consecutive indoor placements this slot lost to geometry or navmesh. At
 // EAC_PedestrianSpawner.INDOOR_FALLBACK_FAILURES the slot is admitted outdoors
 // instead; any activation resets it. Mission-only, never saved.
 int IndoorFailures;
}

class EAC_HouseholdRecord
{
 int Id;
 IEntity BuildingEntity;
 bool Large;
 vector Position;
 // Cached isolation verdict, owned by EAC_HomeIndex. EnrolStamp 0 on a fresh
 // record is always stale against the index's stamp, which starts at 1, so a new
 // household is never served an uninitialised verdict. Position above is what the
 // verdict is evaluated against, so it never dereferences BuildingEntity.
 bool Enrolled;
 int EnrolStamp;
 ref array<ref EAC_ResidentRecord> Residents = {};
}

// Mission-only logical records. Entity lifecycle is owned elsewhere.
class EAC_HouseholdRegistry
{
 protected static const int MAX_HOMES = 2048;
 protected static const int MAX_RESIDENTS = 8192;
 protected static const int MAX_OCCUPANCY = 20;

 protected ref map<IEntity, int> m_BuildingIndexes = new map<IEntity, int>();
 protected ref array<ref EAC_HouseholdRecord> m_Homes = {};
 protected int m_ResidentCount;
 protected int m_NextResidentId = 1;
 protected ref EAC_WeightedPool m_CharacterPool;

 // Logical slots persist; every new physical activation may draw a fresh prefab.
 bool SetCharacterPool(EAC_WeightedPool pool)
 {
  if (!pool || pool.GetCount() == 0) return false;
  m_CharacterPool = pool;
  return true;
 }

 ResourceName PickCharacter(ResourceName fallback)
 {
  if (m_CharacterPool) return m_CharacterPool.Pick();
  return fallback;
 }

 EAC_HouseholdRecord Register(IEntity building, bool large, int occupancy, ResourceName characterPrefab)
 {
  if (!building || characterPrefab == "" || occupancy < 0 || occupancy > MAX_OCCUPANCY) return null;

  if (m_BuildingIndexes.Contains(building))
  {
   EAC_HouseholdRecord existing = m_Homes[m_BuildingIndexes.Get(building)];
   if (!SetOccupancy(existing, occupancy, characterPrefab)) return null;
   return existing;
  }

  if (m_Homes.Count() >= MAX_HOMES || occupancy > MAX_RESIDENTS - m_ResidentCount) return null;

  EAC_HouseholdRecord home = new EAC_HouseholdRecord();
  home.Id = m_Homes.Count() + 1;
  home.BuildingEntity = building;
  home.Large = large;
  home.Position = building.GetOrigin();
  AddResidents(home, occupancy, characterPrefab);
  m_BuildingIndexes.Insert(building, m_Homes.Count());
  m_Homes.Insert(home);
  return home;
 }

 bool SetOccupancy(EAC_HouseholdRecord home, int count, ResourceName characterPrefab)
 {
  if (!Contains(home) || characterPrefab == "" || count < 0 || count > MAX_OCCUPANCY) return false;

  int growth = count - home.Residents.Count();
  if (growth > 0 && growth > MAX_RESIDENTS - m_ResidentCount) return false;
  if (growth > 0) AddResidents(home, growth, characterPrefab);

  for (int i = 0; i < home.Residents.Count(); i++)
   home.Residents[i].Wanted = i < count;
  return true;
 }

 protected bool Contains(EAC_HouseholdRecord home)
 {
  if (!home || home.Id <= 0 || home.Id > m_Homes.Count()) return false;
  return m_Homes[home.Id - 1] == home;
 }

 bool OwnsResident(EAC_HouseholdRecord home, EAC_ResidentRecord resident)
 {
  if (!Contains(home) || !resident || resident.HomeId != home.Id || resident.Slot < 0 || resident.Slot >= home.Residents.Count()) return false;
  return home.Residents[resident.Slot] == resident;
 }

 protected void AddResidents(EAC_HouseholdRecord home, int count, ResourceName characterPrefab)
 {
  for (int i = 0; i < count; i++)
  {
   EAC_ResidentRecord resident = new EAC_ResidentRecord();
   resident.Id = m_NextResidentId++;
   resident.HomeId = home.Id;
   resident.Slot = home.Residents.Count();
   resident.Wanted = true;
   resident.CharacterPrefab = characterPrefab;
   if (m_CharacterPool) resident.CharacterPrefab = m_CharacterPool.Pick();
   home.Residents.Insert(resident);
   m_ResidentCount++;
  }
 }

 int GetHomeCount() { return m_Homes.Count(); }
 int GetResidentCount() { return m_ResidentCount; }

 EAC_HouseholdRecord GetHome(int index)
 {
  if (index < 0 || index >= m_Homes.Count()) return null;
  return m_Homes[index];
 }
}

// Bounded building census of one Random Garrison generation (server). The bounding
// square of the circle is read in 64 m cells, one spatial query per cell with at most
// 512 callbacks; a saturated cell is split into quadrants (the Ambient Destruction
// pattern). Up to 6 cells per tick, then at most 32 eligibility checks per tick, both
// within the director's deadline (at least one per tick).
// Results: the supported, structurally eligible buildings (at most 2048).
class EXPG_RGCell
{
 vector Min;
 int Size;
}

class EXPG_RandomGarrisonCensus
{
 static const int CELL_SIZE = 64;
 static const int MIN_CELL = 8;
 static const int MAX_CALLBACKS = 512;
 static const int MAX_HITS = 4096;
 static const int MAX_ELIGIBLE = 2048;
 static const int CELLS_PER_TICK = 6;
 static const int CHECKS_PER_TICK = 32;
 // The planner's limits (EXPG_BuildingPlan.Begin): sampling columns and height.
 static const int MAX_COLUMNS = 6144;
 static const float MAX_HEIGHT = 60;

 ref array<IEntity> Eligible = {};
 bool Done;
 // Diagnostics: cells read (with splits), raw hits, rejected buildings, saturation.
 int CellsTotal;
 int CellsDone;
 int Rejected;
 int Truncated;
 int Saturated;
 protected vector m_Center;
 protected float m_Radius;
 protected int m_MinX;
 protected int m_MinZ;
 protected int m_MaxX;
 protected int m_MaxZ;
 protected int m_X;
 protected int m_Z;
 protected int m_Phase;
 protected int m_Callbacks;
 protected bool m_Aborted;
 protected int m_Checked;
 protected ref array<ref EXPG_RGCell> m_Cells = {};
 protected ref map<IEntity, bool> m_Seen = new map<IEntity, bool>();
 protected ref array<IEntity> m_Hits = {};

 void Begin(vector center, float radius)
 {
  m_Center = center;
  m_Radius = radius;
  m_MinX = Math.Floor((center[0] - radius) / CELL_SIZE);
  m_MaxX = Math.Floor((center[0] + radius) / CELL_SIZE);
  m_MinZ = Math.Floor((center[2] - radius) / CELL_SIZE);
  m_MaxZ = Math.Floor((center[2] + radius) / CELL_SIZE);
  m_X = m_MinX;
  m_Z = m_MinZ;
  CellsTotal = (m_MaxX - m_MinX + 1) * (m_MaxZ - m_MinZ + 1);
  CellsDone = 0;
  m_Phase = 0;
  Done = false;
 }

 int HitCount()
 {
  return m_Hits.Count();
 }

 int CheckedCount()
 {
  return m_Checked;
 }

 bool Reading()
 {
  return m_Phase == 0;
 }

 // One tick of work within the director's deadline. True when the census is done.
 bool Step(int deadline)
 {
  if (Done)
  {
   return true;
  }
  if (m_Phase == 0)
  {
   int cells;
   while (cells < CELLS_PER_TICK && (cells == 0 || System.GetTickCount() < deadline))
   {
    if (!NextCell())
    {
     m_Phase = 1;
     break;
    }
    cells++;
   }
   return false;
  }
  int checks;
  while (m_Checked < m_Hits.Count() && checks < CHECKS_PER_TICK && (checks == 0 || System.GetTickCount() < deadline))
  {
   IEntity candidate = m_Hits[m_Checked];
   m_Checked++;
   checks++;
   string reason;
   if (!Structural(candidate, reason))
   {
    Rejected++;
    continue;
   }
   if (Eligible.Count() >= MAX_ELIGIBLE)
   {
    Truncated++;
    continue;
   }
   Eligible.Insert(candidate);
  }
  if (m_Checked < m_Hits.Count())
  {
   return false;
  }
  Done = true;
  m_Seen.Clear();
  PrintFormat("[EXPG RANDOM] census at %1 radius %2: cells=%3 saturated=%4 buildings=%5 eligible=%6 rejected=%7 truncated=%8", m_Center, m_Radius, CellsDone, Saturated, m_Hits.Count(), Eligible.Count(), Rejected, Truncated);
  return true;
 }

 // Reads the next cell (or a quadrant of a saturated one). False when none is left.
 protected bool NextCell()
 {
  EXPG_RGCell cell;
  if (!m_Cells.IsEmpty())
  {
   cell = m_Cells[m_Cells.Count() - 1];
   m_Cells.Remove(m_Cells.Count() - 1);
  }
  else
  {
   while (m_Z <= m_MaxZ)
   {
    vector corner = Vector(m_X * CELL_SIZE, -1000, m_Z * CELL_SIZE);
    m_X++;
    if (m_X > m_MaxX)
    {
     m_X = m_MinX;
     m_Z++;
    }
    if (!TouchesCircle(corner, CELL_SIZE))
    {
     CellsDone++;
     continue;
    }
    cell = new EXPG_RGCell();
    cell.Min = corner;
    cell.Size = CELL_SIZE;
    break;
   }
   if (!cell)
   {
    return false;
   }
  }
  CellsDone++;
  m_Callbacks = 0;
  m_Aborted = false;
  GetGame().GetWorld().QueryEntitiesByAABB(cell.Min, cell.Min + Vector(cell.Size, 11000, cell.Size), Visit, null, EQueryEntitiesFlags.ALL);
  if (m_Aborted && cell.Size > MIN_CELL)
  {
   Saturated++;
   int half = cell.Size / 2;
   for (int x = 0; x < 2; x++)
   {
    for (int z = 0; z < 2; z++)
    {
     EXPG_RGCell child = new EXPG_RGCell();
     child.Min = cell.Min + Vector(x * half, 0, z * half);
     child.Size = half;
     m_Cells.Insert(child);
     CellsTotal++;
    }
   }
  }
  else if (m_Aborted)
  {
   Saturated++;
  }
  return true;
 }

 protected bool TouchesCircle(vector corner, int size)
 {
  float nearX = Math.Clamp(m_Center[0], corner[0], corner[0] + size);
  float nearZ = Math.Clamp(m_Center[2], corner[2], corner[2] + size);
  float dx = nearX - m_Center[0];
  float dz = nearZ - m_Center[2];
  return dx * dx + dz * dz <= m_Radius * m_Radius;
 }

 protected bool Visit(IEntity entity)
 {
  m_Callbacks++;
  if (m_Callbacks > MAX_CALLBACKS)
  {
   m_Aborted = true;
   return false;
  }
  if (!entity || !SCR_DestructibleBuildingEntity.Cast(entity))
  {
   return true;
  }
  if (vector.DistanceSqXZ(entity.GetOrigin(), m_Center) > m_Radius * m_Radius || m_Seen.Contains(entity))
  {
   return true;
  }
  m_Seen.Insert(entity, true);
  if (m_Hits.Count() >= MAX_HITS)
  {
   Truncated++;
   return true;
  }
  m_Hits.Insert(entity);
  return true;
 }

 // A whole, intact, supported building with an interior, inside the planner's
 // limits and not a ruin, a part, furniture, a wall, a pier or a stand.
 static bool Structural(IEntity entity, out string reason)
 {
  reason = "not a building";
  SCR_DestructibleBuildingEntity structure = SCR_DestructibleBuildingEntity.Cast(entity);
  if (!structure || structure.IsDeleted())
  {
   return false;
  }
  reason = "part of another building";
  IEntity parent = entity.GetParent();
  int parentDepth;
  while (parent && parentDepth < 16)
  {
   if (SCR_DestructibleBuildingEntity.Cast(parent))
   {
    return false;
   }
   parent = parent.GetParent();
   parentDepth++;
  }
  if (parent)
  {
   return false;
  }
  reason = "ruined or damaged";
  if (EAC_HomeIndex.IsRuined(entity))
  {
   return false;
  }
  reason = "an Ambient Destruction zone may collapse it";
  // Without a destruction zone WallMayCollapse is false: its ancestry walk is skipped.
  if (EAD_World.ZoneCount() > 0 && EAD_World.WallMayCollapse(entity))
  {
   return false;
  }
  reason = "too small or too large to analyse";
  vector mins, maxs;
  entity.GetBounds(mins, maxs);
  float width = maxs[0] - mins[0];
  float length = maxs[2] - mins[2];
  float height = maxs[1] - mins[1];
  if (width < 3 || length < 3 || height < 2.2 || height > MAX_HEIGHT)
  {
   return false;
  }
  int columns = Math.Ceil(width / EXPG_BuildingPlan.GRID);
  int rows = Math.Ceil(length / EXPG_BuildingPlan.GRID);
  if (columns * rows > MAX_COLUMNS)
  {
   return false;
  }
  reason = "not a building type that can be garrisoned";
  EntityPrefabData data = entity.GetPrefabData();
  if (!data || data.GetPrefabName().IsEmpty() || RejectedPath(data.GetPrefabName()))
  {
   return false;
  }
  BaseContainer prefab = data.GetPrefab();
  if (prefab) { prefab = prefab.GetAncestor(); }
  for (int level = 0; prefab && level < 16; level++)
  {
   if (RejectedPath(prefab.GetResourceName()))
   {
    return false;
   }
   prefab = prefab.GetAncestor();
  }
  reason = "no doors and no interior";
  if (!HasInterior(structure))
  {
   return false;
  }
  reason = "";
  return true;
 }

 static bool RejectedPath(string prefabPath)
 {
  string path = prefabPath;
  path.ToLower();
  // One chain of tests: no list is allocated for each prefab and ancestor.
  if (path.Contains("/dst/") || path.Contains("ruin") || path.Contains("destroyed") || path.Contains("/furniture/") || path.Contains("/buildingparts/") || path.Contains("/buildingaddons/"))
  {
   return true;
  }
  return path.Contains("/cemeteries/") || path.Contains("/walls/") || path.Contains("/piers/") || path.Contains("hotbed") || path.Contains("calvar") || path.Contains("deerstand");
 }

 // An authored interior volume, or a door within the first 128 hierarchy parts.
 static bool HasInterior(IEntity structure)
 {
  SCR_DestructibleBuildingComponent damage = SCR_DestructibleBuildingComponent.Cast(structure.FindComponent(SCR_DestructibleBuildingComponent));
  if (damage)
  {
   SCR_DestructibleBuildingComponentClass data = SCR_DestructibleBuildingComponentClass.Cast(damage.GetComponentData(structure));
   if (data && data.m_aInteriorQueryBoundingBoxes && !data.m_aInteriorQueryBoundingBoxes.IsEmpty())
   {
    return true;
   }
  }
  array<IEntity> parts = {structure};
  for (int cursor = 0; cursor < parts.Count() && cursor < 128; cursor++)
  {
   IEntity part = parts[cursor];
   if (part != structure && part.FindComponent(DoorComponent))
   {
    return true;
   }
   for (IEntity child = part.GetChildren(); child && parts.Count() < 128; child = child.GetSibling()) { parts.Insert(child); }
  }
  return false;
 }
}

[EntityEditorProps(category: "EXPBG/Ambient", description: "Permanent building destruction and cached script-free scenery")]
class EAD_ZoneClass : GenericEntityClass {}
class EAD_Zone : GenericEntity
{
 [Attribute("250", UIWidgets.EditBox, "Zone radius (m)", "25 1000 1"), RplProp()] int Radius;
 [Attribute("30", UIWidgets.EditBox, "Supported buildings selected for destruction (%)", "0 100 1"), RplProp()] int Destruction;
 // Keep the serialized field/key for existing modules; loose clutter is retired.
 [Attribute("0", UIWidgets.EditBox, "Legacy clutter (unused)", "0 100 1"), RplProp()] int Clutter;
 [Attribute("50", UIWidgets.EditBox, "Wreck density (%)", "0 100 1"), RplProp()] int Wrecks;
 [Attribute("50", UIWidgets.EditBox, "Body density (%)", "0 100 1"), RplProp()] int Bodies;
 [Attribute("600", UIWidgets.EditBox, "Wake margin beyond zone edge (m)", "100 3000 1"), RplProp()] int WakeMargin;
 [Attribute("800", UIWidgets.EditBox, "Sleep margin beyond zone edge (m)", "101 4000 1"), RplProp()] int SleepMargin;
 [Attribute("1", UIWidgets.EditBox, "Stable random seed", "1 1000000 1"), RplProp()] int Seed;
 [Attribute("0", UIWidgets.CheckBox, "Enable zone"), RplProp()] int Enabled;
 [Attribute("0", UIWidgets.EditBox, "Server diagnostics: 0 off, 1 lifecycle, 2 periodic metrics", "0 2 1"), RplProp()] int DebugLevel;
 [Attribute("0", UIWidgets.EditBox, "GM debug radius rings", "0 1 1"), RplProp()] int DebugDraw;
 [Attribute("2", UIWidgets.EditBox, "Body type: 0 fallen, 1 burned, 2 random", "0 2 1"), RplProp()] int BodyMode;
 ref array<ref EAD_PropRecord> Records = {};
 ref array<IEntity> Live = {};
 protected ref EAD_Random m_Random;
 protected ref EAD_WreckBag m_WreckBag;
 protected ref array<int> m_BodyVariants = {};
 protected ref EAD_PropRecord m_DressingBody;
 protected ref EAD_Buildings m_Buildings;
 protected int m_Revision = 1;
 protected int m_Target;
 protected int m_Slot, m_Attempt;
 protected int m_WreckCount, m_BodyCount, m_GroupCount, m_BareMember, m_CurrentAsset;
 protected ref array<int> m_BodySites = {};
 protected bool m_SitesReady;
 protected bool m_Ready, m_Rebuild = true, m_Wanted;
 protected float m_EditAfter, m_EmptySince = -1;
 protected vector m_LayoutOrigin;
 protected float m_FailureRetry;
 protected int m_SpawnFailures;
 protected float m_NextDiagnostics;
 protected int m_GenerationAttempts, m_PlacementRejected, m_SpawnedTotal, m_DeletedTotal;
 protected int m_WreckRejected, m_BodyRejected, m_WreckSkipped, m_BodySkipped;
 protected bool m_GenerationMetrics;
 protected ref EAD_DebugView m_DebugView;
 protected bool m_ImportedSnapshot;
 protected int m_WallCursor;
 void EAD_Zone(IEntitySource src, IEntity parent) { SetEventMask(EntityEvent.INIT); }
 override void EOnInit(IEntity owner)
 {
  if (!GetGame() || !GetGame().InPlayMode()) return;
  if (Replication.IsServer())
  {
   Normalize();
   m_EditAfter = GetWorld().GetWorldTime() * 0.001 + 5;
   // RplComponent inserts last during init. Pin the controller afterwards:
   // disabling spatial relevancy removes it from ordinary players entirely.
   GetGame().GetCallqueue().CallLater(KeepReplicated, 1, false);
  }
  EAD_World.Register(this);
  Diagnostics("init");
 }
 protected void KeepReplicated()
 {
  RplComponent replication = RplComponent.Cast(FindComponent(RplComponent));
  if (replication) replication.EnableStreaming(false);
 }
 void ~EAD_Zone()
 {
  Diagnostics("delete");
  m_DebugView = null;
  if (GetGame()) GetGame().GetCallqueue().Remove(KeepReplicated);
  // A deleted replicated module must not leave locally spawned collision behind.
  foreach (IEntity entity : Live) { if (entity) SCR_EntityHelper.DeleteEntityAndChildren(entity); }
  Live.Clear();
  EAD_World.Unregister(this);
 }
 void Normalize()
 {
  Radius = Math.Clamp(Radius, 25, 1000);
  Destruction = Math.Clamp(Destruction, 0, 100);
  Clutter = Math.Clamp(Clutter, 0, 100);
  Wrecks = Math.Clamp(Wrecks, 0, 100);
  Bodies = Math.Clamp(Bodies, 0, 100);
  WakeMargin = Math.Clamp(WakeMargin, 100, 3000);
  SleepMargin = Math.Clamp(SleepMargin, WakeMargin + 1, 4000);
  Seed = Math.Clamp(Seed, 1, 1000000);
  Enabled = Math.Clamp(Enabled, 0, 1);
  DebugLevel = Math.Clamp(DebugLevel, 0, 2);
  DebugDraw = Math.Clamp(DebugDraw, 0, 1);
  BodyMode = Math.Clamp(BodyMode, 0, 2);
 }
 int GetSetting(int key)
 {
  switch (key)
  {
   case 0: return Radius;
   case 1: return Destruction;
   case 2: return Clutter;
   case 3: return Wrecks;
   case 4: return Bodies;
   case 5: return WakeMargin;
   case 6: return SleepMargin;
   case 7: return Seed;
   case 8: return Enabled;
   case 9: return DebugLevel;
   case 10: return DebugDraw;
   case 11: return BodyMode;
  }
  return 0;
 }
 void SetSetting(int key, int value)
 {
  if (!Replication.IsServer()) return;
  int previous = GetSetting(key);
  switch (key)
  {
   case 0: Radius = value; break;
   case 1: Destruction = value; break;
   case 2: Clutter = value; break;
   case 3: Wrecks = value; break;
   case 4: Bodies = value; break;
   case 5: WakeMargin = value; break;
   case 6: SleepMargin = value; break;
   case 7: Seed = value; break;
   case 8: Enabled = value; break;
   case 9: DebugLevel = value; break;
   case 10: DebugDraw = value; break;
   case 11: BodyMode = value; break;
   default: return;
  }
  Normalize();
  if (GetSetting(key) == previous) return;
  Replication.BumpMe();
  if (key <= 4 || key == 7 || key == 11) RequestRebuild();
  // Enabled is operational state, never a reason to discard an existing layout.
  if (key == 8)
  {
   m_Wanted = Enabled == 1;
   m_EmptySince = -1;
   m_EditAfter = 0;
  }
  Diagnostics("settings");
 }
 void RequestRebuild()
 {
  Diagnostics("rebuild-request");
  m_Rebuild = true;
  m_Ready = false;
  m_EditAfter = GetWorld().GetWorldTime() * 0.001 + 5;
 }
 void Proximity(array<IEntity> observers, float now)
 {
  if (!Replication.IsServer()) return;
  bool wasWanted = m_Wanted;
  if (!m_Rebuild && EAD_Policy.DistanceSq(GetOrigin(), m_LayoutOrigin) > 1) RequestRebuild();
  bool nearWake, nearSleep;
  foreach (IEntity observer : observers)
  {
   if (!observer) continue;
   vector point = observer.GetOrigin();
   if (EAD_Policy.Near(point, GetOrigin(), Radius, WakeMargin)) nearWake = true;
   if (EAD_Policy.Near(point, GetOrigin(), Radius, SleepMargin)) nearSleep = true;
  }
  if (Enabled == 0 || m_Rebuild)
  {
   m_Wanted = false;
   if (wasWanted) Diagnostics("disabled-or-rebuilding");
   return;
  }
  if (nearWake) { m_Wanted = true; m_EmptySince = -1; }
  else if (!nearSleep)
  {
   if (m_EmptySince < 0) m_EmptySince = now;
   if (m_Wanted && now - m_EmptySince >= 30 && !Occupied()) m_Wanted = false;
  }
  else m_EmptySince = -1;
  if (wasWanted != m_Wanted)
  {
   if (m_Wanted) Diagnostics("wake");
   else Diagnostics("sleep");
  }
 }
 protected bool m_Occupied;
 protected int m_OccupancyVisits;
 bool Occupied()
 {
  m_Occupied = false;
  m_OccupancyVisits = 0;
  GetWorld().QueryEntitiesBySphere(GetOrigin(), Radius + 15, Occupant, null, EQueryEntitiesFlags.DYNAMIC);
  return m_Occupied;
 }
 protected bool Occupant(IEntity entity)
 {
  m_OccupancyVisits++;
  if (m_OccupancyVisits > 512) { m_Occupied = true; return false; }
  if (ChimeraCharacter.Cast(entity) || Vehicle.Cast(entity)) { m_Occupied = true; return false; }
  return true;
 }
 // One generation attempt per world work token. Never reroll existing records on wake.
 bool Generate(float now)
 {
  if (!Replication.IsServer() || EAD_Snapshot.Loading || Enabled == 0) return false;
  if (m_Rebuild)
  {
   if (!Live.IsEmpty() || now < m_EditAfter) return false;
   Records.Clear();
   m_Revision++;
   m_Target = 0;
   Rpc(RPC_Reset, m_Revision);
   vector origin = GetOrigin();
   int positionSeed = Math.AbsInt(Math.Floor(origin[0]) * 31 + Math.Floor(origin[2]) * 17);
   m_Random = new EAD_Random(Seed + positionSeed);
   m_WreckBag = new EAD_WreckBag(); m_BodyVariants.Clear(); m_DressingBody = null;
   m_WreckCount = EAD_Policy.Count(Wrecks, 50);
   m_BodyCount = 0; m_BodySites.Clear(); m_SitesReady = false;
   m_Slot = 0; m_Attempt = 0; m_GroupCount = 0;
   m_GenerationAttempts = 0; m_PlacementRejected = 0;
   m_WreckRejected = 0; m_BodyRejected = 0; m_WreckSkipped = 0; m_BodySkipped = 0;
   m_GenerationMetrics = true; m_ImportedSnapshot = false;
   m_LayoutOrigin = GetOrigin();
   m_Rebuild = false;
   m_Buildings = new EAD_Buildings();
   m_Buildings.Begin(GetWorld(), m_LayoutOrigin, Radius, Destruction, Seed);
   Diagnostics("generation-start");
   return true;
  }
  if (m_Ready) return false;
  if (m_DressingBody)
  {
   EAD_PropRecord dressing = EAD_Placement.Dressing(this, m_Random, m_DressingBody);
   m_DressingBody = null;
   if (dressing) AddRecord(dressing);
   return true;
  }
  // Reuse the bounded building census, including ruined buildings. Try each site
  // once before revisiting it; no body-to-body anchors or evenly spaced rows.
  if (m_Slot >= m_WreckCount && !m_SitesReady)
  {
   if (Bodies > 0 && !m_Buildings.IsFinished()) return false;
   array<int> sites = {};
   for (int site = 0; site < m_Buildings.Sites.Count(); site++) sites.Insert(site);
   while (!sites.IsEmpty())
   {
    int pick = Math.Min(sites.Count() - 1, Math.Floor(m_Random.Next() * sites.Count()));
    m_BodySites.Insert(sites[pick]); sites.RemoveOrdered(pick);
   }
   m_BodyCount = EAD_Policy.Count(Bodies, Math.Min(60, m_BodySites.Count() * 2));
   m_SitesReady = true;
  }
  int desired = m_WreckCount + m_BodyCount;
  if (m_Slot >= desired) { m_Ready = true; m_Wanted = Enabled == 1; m_EmptySince = -1; Diagnostics("generation-ready"); return false; }
  bool body = m_Slot >= m_WreckCount && m_Slot < m_WreckCount + m_BodyCount;
  if (m_Attempt == 0)
  {
   if (m_Slot < m_WreckCount) m_CurrentAsset = m_WreckBag.Next(m_Random);
   else if (body)
   {
    if (m_BodyVariants.IsEmpty())
    {
     EAD_Policy.BodyVariants(BodyMode, m_BodyVariants);
    }
    int choice = Math.Min(m_BodyVariants.Count() - 1, Math.Floor(m_Random.Next() * m_BodyVariants.Count()));
    m_CurrentAsset = m_BodyVariants[choice]; m_BodyVariants.RemoveOrdered(choice);
    if (m_GroupCount == 0) m_BareMember = Math.Min(2, Math.Floor(m_Random.Next() * 3));
   }
  }
  int lane = Math.Min(2, Math.Floor(m_Random.Next() * 3));
  EAD_PropRecord record;
  if (body)
  {
   int siteIndex = m_BodySites[Math.Mod(m_Slot - m_WreckCount, m_BodySites.Count())];
   record = EAD_Placement.AtBuilding(this, m_Random, m_CurrentAsset, m_Buildings.Sites[siteIndex]);
  }
  else record = EAD_Placement.Candidate(this, m_Random, m_CurrentAsset, false, vector.Zero, lane);
  m_Attempt++;
  m_GenerationAttempts++;
  if (!record)
  {
   m_PlacementRejected++;
   if (body) m_BodyRejected++;
   else m_WreckRejected++;
  }
  if (record)
  {
   if (body)
   {
    if (EAD_Catalog.IsSeated(record.Asset) && EAD_Policy.DressBody(m_GroupCount, m_BareMember, m_Random.Next())) m_DressingBody = record;
    m_GroupCount++;
    if (m_GroupCount == 3) m_GroupCount = 0;
   }
   AddRecord(record);
  }
  if (record || m_Attempt >= 12)
  {
   if (!record)
   {
    if (body) m_BodySkipped++;
    else m_WreckSkipped++;
   }
   m_Slot++; m_Attempt = 0;
  }
  return true;
 }
 protected void AddRecord(EAD_PropRecord record)
 {
  int index = Records.Count();
  Records.Insert(record);
  Rpc(RPC_Record, m_Revision, index, record.Asset, record.Transform[0], record.Transform[1], record.Transform[2], record.Transform[3]);
 }
 bool BuildingsStep()
 {
  if (!Replication.IsServer() || EAD_Snapshot.Loading || Enabled == 0 || !m_Buildings || m_Rebuild) return false;
  m_Buildings.DebugLevel = DebugLevel;
  return m_Buildings.Step();
 }
 // True means native work was attempted (or a slot advanced), including failures.
 // The shared scheduler must charge failed work too; cooldown/idle visits are free.
 bool Reconcile(float now, bool allowSpawn)
 {
  if (EAD_Snapshot.Loading) return false;
  bool authority = Replication.IsServer();
  // Live wall-loss cleanup uses the ordinary one-entity change token.
  for (int suppressed = 0; suppressed < Live.Count() && suppressed < Records.Count(); suppressed++)
  {
   if (!Records[suppressed].Suppressed || !Live[suppressed]) continue;
   IEntity lost = Live[suppressed];
   SCR_EntityHelper.DeleteEntityAndChildren(lost);
   if (lost) return true;
   Live[suppressed] = null;
   if (authority) m_DeletedTotal++;
   return true;
  }
  int wanted = m_Target;
  if (authority)
  {
   wanted = 0;
   if (m_Wanted && m_Ready && !m_Rebuild && Enabled == 1) wanted = Records.Count();
  }
  if (Live.Count() > wanted)
  {
   int last = Live.Count() - 1;
   IEntity entity = Live[last];
   bool removedObject = entity != null;
   if (entity) SCR_EntityHelper.DeleteEntityAndChildren(entity);
   // Don't discard the reference before confirming synchronous local deletion.
   if (entity) return true;
   Live.RemoveOrdered(last);
   if (authority && removedObject) m_DeletedTotal++;
   if (authority) PublishCount();
   if (Live.IsEmpty()) Diagnostics("cached");
   return true;
  }
  if (Live.Count() >= wanted || !allowSpawn || now < m_FailureRetry) return false;
  if (Live.Count() >= Records.Count()) return false;
  int slot = Live.Count();
  EAD_PropRecord next = Records[slot];
  if (authority && !next.Suppressed && EAD_Catalog.IsSeated(next.Asset))
  {
   int support = EAD_Placement.WallSupport(GetWorld(), next);
   if (support < 0) { m_FailureRetry = now + 5; return true; }
   if (support == 0)
   {
    // Preserve saved transforms. A vanished wall cannot restore a floating seated body.
    SuppressRecord(slot);
    if (slot + 1 < Records.Count() && EAD_Catalog.IsLitter(Records[slot + 1].Asset)) SuppressRecord(slot + 1);
   }
  }
  if (next.Suppressed)
  {
   // A tombstone retains the reliable indexed stream and cannot block later props.
   Live.Insert(null);
   if (authority) PublishCount();
   return true;
  }
  IEntity created = EAD_Catalog.Spawn(Records[Live.Count()], GetWorld());
  if (!created)
  {
   m_SpawnFailures++;
   m_FailureRetry = now + 5;
   if (m_SpawnFailures <= 3) PrintFormat("[EAD] prop spawn failed zone=%1 asset=%2", GetOrigin(), Records[Live.Count()].Asset);
   return true;
  }
  Live.Insert(created);
  if (authority) m_SpawnedTotal++;
  if (authority) PublishCount();
  if (Live.Count() == Records.Count()) Diagnostics("restored");
  return true;
 }
 protected void PublishCount()
 {
  m_Target = Live.Count();
  Rpc(RPC_Count, m_Revision, m_Target);
 }
 protected void SuppressRecord(int index)
 {
  Records[index].Suppressed = true;
  Rpc(RPC_Suppress, m_Revision, index);
  Diagnostics("wall-lost");
 }
 void SupportStep()
 {
  if (!Replication.IsServer() || EAD_Snapshot.Loading || Enabled == 0 || !m_Ready || m_Rebuild) return;
  // One seated prop per global scheduler turn, at most nine short traces.
  for (int visited = 0; visited < Records.Count(); visited++)
  {
   if (m_WallCursor >= Records.Count()) m_WallCursor = 0;
   int index = m_WallCursor++;
   EAD_PropRecord record = Records[index];
   if (record.Suppressed || !EAD_Catalog.IsSeated(record.Asset) || index >= Live.Count() || !Live[index]) continue;
   if (EAD_Placement.WallSupport(GetWorld(), record, Live[index]) == 0)
   {
    SuppressRecord(index);
    if (index + 1 < Records.Count() && EAD_Catalog.IsLitter(Records[index + 1].Asset)) SuppressRecord(index + 1);
   }
   return;
  }
 }
 [RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
 protected void RPC_Suppress(int revision, int index)
 {
  if (Replication.IsServer() || revision != m_Revision || index < 0 || index >= Records.Count()) return;
  Records[index].Suppressed = true;
  if (index < Live.Count() && Live[index]) SCR_EntityHelper.DeleteEntityAndChildren(Live[index]);
 }
 [RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
 protected void RPC_Reset(int revision)
 {
  if (Replication.IsServer() || revision <= m_Revision) return;
  foreach (IEntity entity : Live) { if (entity) SCR_EntityHelper.DeleteEntityAndChildren(entity); }
  Live.Clear(); Records.Clear();
  m_Target = 0; m_Revision = revision;
 }
 [RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
 protected void RPC_Record(int revision, int index, int asset, vector right, vector up, vector forward, vector origin)
 {
  if (Replication.IsServer() || revision != m_Revision || index != Records.Count() || index >= 170 || EAD_Catalog.Prefab(asset) == "") return;
  EAD_PropRecord record = new EAD_PropRecord();
  record.Asset = asset;
  record.Transform[0] = right; record.Transform[1] = up;
  record.Transform[2] = forward; record.Transform[3] = origin;
  Records.Insert(record);
 }
 [RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
 protected void RPC_Count(int revision, int count)
 {
  if (Replication.IsServer() || revision != m_Revision) return;
  m_Target = Math.Clamp(count, 0, 170);
 }
 override bool RplSave(ScriptBitWriter writer)
 {
  writer.WriteInt(m_Revision);
  writer.WriteInt(Live.Count());
  writer.WriteInt(Records.Count());
  foreach (EAD_PropRecord record : Records)
  {
   writer.WriteInt(record.Asset);
   writer.WriteBool(record.Suppressed);
   for (int axis = 0; axis < 4; axis++) writer.WriteVector(record.Transform[axis]);
  }
  return true;
 }
 override bool RplLoad(ScriptBitReader reader)
 {
  int revision, target, count;
  if (!reader.ReadInt(revision) || !reader.ReadInt(target) || !reader.ReadInt(count)) return false;
  if (count < 0 || count > 170 || target < 0 || target > count) return false;
  foreach (IEntity entity : Live) { if (entity) SCR_EntityHelper.DeleteEntityAndChildren(entity); }
  Live.Clear(); Records.Clear();
  m_Revision = revision; m_Target = target;
  for (int i = 0; i < count; i++)
  {
   EAD_PropRecord record = new EAD_PropRecord();
   if (!reader.ReadInt(record.Asset)) return false;
   if (!reader.ReadBool(record.Suppressed)) return false;
   if (EAD_Catalog.Prefab(record.Asset) == "") return false;
   for (int axis = 0; axis < 4; axis++)
   {
    vector transformAxis;
    if (!reader.ReadVector(transformAxis)) return false;
    record.Transform[axis] = transformAxis;
   }
   Records.Insert(record);
  }
  return true;
 }
 int GetRevision() { return m_Revision; }
 bool IsReady() { return m_Ready; }
 int GetSpawnFailures() { return m_SpawnFailures; }
 bool CanCapture()
 {
  if (Enabled == 0 && m_Rebuild && Live.IsEmpty()) return true;
  // Scenery must be complete. The global ledger preserves decisions during a building scan.
  return !m_Rebuild && m_Ready;
 }
 bool HasSavedLayout() { return !m_Rebuild && m_Ready; }
 bool HasImportedSnapshot() { return m_ImportedSnapshot; }
 bool ImportSnapshot(EAD_ZoneSnapshot data)
 {
  if (!Replication.IsServer() || !EAD_Snapshot.Loading || !data || !Live.IsEmpty() || vector.DistanceSq(GetOrigin(), data.Origin) > 0.01) return false;
  for (int key = 0; key < data.Settings.Count(); key++) SetSetting(key, data.Settings[key]);
  Records.Clear(); m_Revision++; m_Target = 0;
  Rpc(RPC_Reset, m_Revision);
  foreach (EAD_PropRecord record : data.Records)
  {
   AddRecord(record);
   if (record.Suppressed) Rpc(RPC_Suppress, m_Revision, Records.Count() - 1);
  }
  m_LayoutOrigin = data.Origin; m_Rebuild = !data.Generated; m_Ready = data.Generated;
  m_Wanted = Enabled == 1; m_EmptySince = -1; m_EditAfter = 0;
  m_Buildings = new EAD_Buildings();
  m_Buildings.Begin(GetWorld(), m_LayoutOrigin, Radius, Destruction, Seed);
  m_DressingBody = null; m_ImportedSnapshot = true;
  // Generation attempts/site coverage are not part of the saved layout format.
  // Never present a prior layout's counters as evidence about this imported one.
  m_GenerationMetrics = false;
  m_GenerationAttempts = 0; m_PlacementRejected = 0;
  m_WreckRejected = 0; m_BodyRejected = 0; m_WreckSkipped = 0; m_BodySkipped = 0;
  Diagnostics("load-snapshot");
  Replication.BumpMe();
  return true;
 }
 void Diagnostics(string reason)
 {
  if (!Replication.IsServer() || DebugLevel == 0) return;
  int actual, suppressed, destroyed, placedWrecks, placedBodies;
  foreach (IEntity entity : Live) { if (entity) actual++; }
  foreach (EAD_PropRecord record : Records)
  {
   if (record.Suppressed) suppressed++;
   if (EAD_Catalog.IsWreck(record.Asset)) placedWrecks++;
   else if ((record.Asset >= 4 && record.Asset <= 7) || (record.Asset >= 31 && record.Asset <= 33)) placedBodies++;
  }
  int wreckTarget = -1; int bodyTarget = -1; int sites = -1;
  if (m_GenerationMetrics)
  {
   wreckTarget = m_WreckCount;
   if (m_SitesReady) bodyTarget = m_BodyCount;
   if (m_Buildings) sites = m_Buildings.Sites.Count();
  }
  if (m_Buildings) destroyed = m_Buildings.GetDestroyedCount();
  PrintFormat("[EAD STATE] reason=%1 origin=%2 revision=%3 enabled=%4 ready=%5 wanted=%6", reason, GetOrigin(), m_Revision, Enabled, m_Ready, m_Wanted);
  PrintFormat("[EAD COUNTS] records=%1 live=%2 slots=%3 suppressed=%4 created=%5 deleted=%6 failures=%7", Records.Count(), actual, Live.Count(), suppressed, m_SpawnedTotal, m_DeletedTotal, m_SpawnFailures);
  PrintFormat("[EAD CONFIG] radius=%1 wake=%2 sleep=%3 destruction=%4 wrecks=%5 bodies=%6 seed=%7", Radius, WakeMargin, SleepMargin, Destruction, Wrecks, Bodies, Seed);
  PrintFormat("[EAD WORK] attempts=%1 rejected=%2 destroyed=%3 globalSlots=%4 lastTickMs=%5 peakTickMs=%6", m_GenerationAttempts, m_PlacementRejected, destroyed, EAD_World.LiveCount(), EAD_World.TickLastMs(), EAD_World.TickPeakMs());
  PrintFormat("[EAD DENSITY] wreckTarget=%1 wreckPlaced=%2 bodyTarget=%3 bodyPlaced=%4 discoveredSites=%5 sitesReady=%6 bodyMode=%7 metricsKnown=%8", wreckTarget, placedWrecks, bodyTarget, placedBodies, sites, m_SitesReady && m_GenerationMetrics, BodyMode, m_GenerationMetrics);
  PrintFormat("[EAD PLACEMENT] wreckRejectedAttempts=%1 bodyRejectedAttempts=%2 wreckSkippedSlots=%3 bodySkippedSlots=%4 attemptsPerSlot=12 wreckCap=50 bodyCap=60 siteCap=256", m_WreckRejected, m_BodyRejected, m_WreckSkipped, m_BodySkipped);
 }
 void DiagnosticTick(float now)
 {
  if (Replication.IsServer() && DebugLevel >= 2 && now >= m_NextDiagnostics)
  {
   m_NextDiagnostics = now + 10;
   Diagnostics("periodic");
  }
  if (!m_DebugView && (DebugDraw != 0 || DebugLevel != 0) && !System.IsConsoleApp()) m_DebugView = new EAD_DebugView();
  if (m_DebugView) m_DebugView.Update(this);
 }
}

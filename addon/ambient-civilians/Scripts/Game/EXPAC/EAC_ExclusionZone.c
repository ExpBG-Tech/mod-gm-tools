[EntityEditorProps(category: "EXPBG/Ambient", description: "Prevents ambient civilian population and optionally transit within a ground-level radius")]
class EAC_ExclusionZoneClass : GenericEntityClass {}

class EAC_ExclusionZone : GenericEntity
{
 protected static ref array<EAC_ExclusionZone> s_Zones;
 protected static BaseWorld s_CapWarningWorld;
 protected bool m_Registered;
 // A Game Master may place zones freely, so registration is capped exactly like
 // every other record this addon keeps. Past the cap a zone is inert rather than
 // unbounded: the predicates below run once per route point of every resident.
 static const int MAX_ZONES = 64;
 // Metres added to every radius by the route bounding-box pre-tests, so float
 // rounding can only send a route to the exact per-segment test, never past it.
 static const float ROUTE_BOX_MARGIN = 1;
 // NOTE (fixture v6b-exclusions, 46/1): this was a cached count, recounted on
 // registration changes. It went stale the moment a zone ENTITY was deleted:
 // s_Zones holds weak pointers, so the engine nulls the element immediately but
 // ~EAC_ExclusionZone - and the RemoveItem/recount in it - runs deferred, leaving
 // a count of 1 against a list whose only entry was already dead. The predicates
 // themselves stayed correct because they skip a null zone, which is why only the
 // flag check failed. There is no cache any more: AnyTransitBlocked derives from
 // the live list, which is bounded by MAX_ZONES and asked ONCE per route scan,
 // never once per route point - so it still replaces up to 64 IsTransitAllowed
 // calls with one short pass.

 [Attribute("100", UIWidgets.EditBox, "Population exclusion radius in metres", "5 5000 1", category: "Exclusion"), RplProp(onRplName: "EAC_OnRadiusReplicated")]
 int RadiusMeters;
 [Attribute("1", UIWidgets.EditBox, "Block ambient civilian transit through this zone", "0 1 1", category: "Exclusion"), RplProp()]
 int BlockTransit;

 void EAC_ExclusionZone(IEntitySource src, IEntity parent)
 {
  SetEventMask(EntityEvent.INIT);
 }

 override void EOnInit(IEntity owner)
 {
		EXPBG_LazyStatics_EAC_ExclusionZone();
  super.EOnInit(owner);
  if (!GetGame() || !GetGame().InPlayMode() || !Replication.IsServer()) return;
  NormalizeSettings();
  // Past the cap the zone stays in the world as a marker but takes no part in the
  // predicates. Refusing here rather than in the query keeps the hot path free of
  // a bound check per zone per route point.
  if (s_Zones.Count() >= MAX_ZONES)
  {
   if (s_CapWarningWorld != GetWorld())
   {
    s_CapWarningWorld = GetWorld();
    PrintFormat("[EAC exclusion] WARNING zone cap reached (%1); additional zones are inert; first refused position=%2", MAX_ZONES, GetOrigin());
   }
   return;
  }
  s_Zones.Insert(this);
  m_Registered = true;
  LogConfiguration("created");
  Replication.BumpMe();
 }

 void ~EAC_ExclusionZone()
 {
		EXPBG_LazyStatics_EAC_ExclusionZone();
  LogConfiguration("removed");
  s_Zones.RemoveItem(this);
 }

 protected void LogConfiguration(string action)
 {
  if (!Replication.IsServer() || EAC_AmbientModule.GetDebugLevelMirror() < 1) return;
  PrintFormat("[EAC exclusion] %1 position=%2 radius=%3 transit=%4 registered=%5", action, GetOrigin(), RadiusMeters, BlockTransit, m_Registered);
 }

 // True when at least one LIVE zone blocks transit. Callers that sweep a native
 // route point by point ask this once and skip the whole scan when it is false;
 // the per-segment predicate stays authoritative for every real test.
 //
 // Deleted zones are dropped here rather than waited for: the destructor is not
 // guaranteed to have run by the time the next query arrives, and a nulled slot
 // would otherwise keep s_Zones non-empty for ever and defeat the empty-list fast
 // path in both predicates.
 static bool AnyTransitBlocked()
 {
  if (EAC_AmbientModule.GetActive()) return true; // Route segments must stay inside its area.
  return AnyJourneyTransitBlocked();
 }

 // The same question for a car journey, which may leave the module area: only
 // automatic geography and manual transit-blocking zones can stop it.
 static bool AnyJourneyTransitBlocked()
 {
		EXPBG_LazyStatics_EAC_ExclusionZone();
  if (EAC_AutoExclusions.AnyTransitBlocked()) return true;
  if (s_Zones.IsEmpty()) return false;
  bool blocked = false;
  for (int index = s_Zones.Count() - 1; index >= 0; index--)
  {
   EAC_ExclusionZone zone = s_Zones[index];
   if (!zone) { s_Zones.RemoveOrdered(index); continue; }
   if (zone.BlockTransit == 1) blocked = true;
  }
  return blocked;
 }

 protected void NormalizeSettings()
 {
  RadiusMeters = Math.Clamp(RadiusMeters, 5, 5000);
  BlockTransit = Math.Clamp(BlockTransit, 0, 1);
 }

 // Replication callback for RadiusMeters. Rebuilds the visualization mesh where one already
 // exists; it never builds one, so a plain client and a dedicated server still allocate nothing.
 void EAC_OnRadiusReplicated()
 {
  EAC_ExclusionAreaComponent area = EAC_ExclusionAreaComponent.Cast(FindComponent(EAC_ExclusionAreaComponent));
  if (!area)
  {
   return;
  }
  area.Refresh();
 }

 int GetSetting(int key)
 {
  switch (key)
  {
   case 0: return RadiusMeters;
   case 1: return BlockTransit;
  }
  return 0;
 }

 void SetSetting(int key, int value)
 {
  if (!Replication.IsServer()) return;
  int previous = GetSetting(key);
  switch (key)
  {
   case 0: RadiusMeters = Math.Clamp(value, 5, 5000); break;
   case 1: BlockTransit = Math.Clamp(value, 0, 1); break;
   default: return;
  }
  if (previous != GetSetting(key)) LogConfiguration("changed");
  Replication.BumpMe();
  // The authority never receives its own RplProp callback. Without this line the mesh would
  // resize correctly on a remote client and silently fail on a listen server and in
  // Play-in-Workbench, the two environments most likely to be used for QA.
  if (key == 0)
  {
   EAC_OnRadiusReplicated();
  }
 }

 static bool IsPopulationAllowed(vector position)
 {
  if (EAC_AmbientModule.IsOutsidePopulationArea(position)) return false;
  return IsJourneyPointAllowed(position);
 }

 // Civilian cars are placed inside the module area, then drive out of town and
 // are removed once far away and unseen (one-way traffic). The module disc is an
 // admission boundary for people and parked cars, not a wall for a departing car:
 // with the default 400 m radius every other town is outside it, and requiring the
 // destination inside the disc left no legal journey at all (live run 2026-10-06:
 // `traffic admission last=town_goal`, trips=0/0 for 40 minutes in Morton).
 // Automatic geography and manual zones still apply in full.
 static bool IsJourneyPointAllowed(vector position)
 {
		EXPBG_LazyStatics_EAC_ExclusionZone();
  if (!EAC_AutoExclusions.IsPopulationAllowed(position)) return false;
  // After automatic geography, an empty manual-zone list needs no extra work.
  if (s_Zones.IsEmpty()) return true;
  if (!GetGame() || !Replication.IsServer()) return false;
  if (!GetGame().GetWorld()) return false;
  return !IsManualPopulationExcluded(position);
 }

 // Explicit GM exclusion may remove owned population even in view of players.
 // Automatic terrain metadata remains an admission policy, never a delete order.
 static bool IsManualPopulationExcluded(vector position, bool transitOnly = false)
 {
		EXPBG_LazyStatics_EAC_ExclusionZone();
  if (s_Zones.IsEmpty() || !GetGame() || !Replication.IsServer()) return false;
  BaseWorld world = GetGame().GetWorld();
  if (!world) return false;
  foreach (EAC_ExclusionZone zone : s_Zones)
  {
   if (!zone || zone.GetWorld() != world) continue;
   if (transitOnly && zone.BlockTransit != 1) continue;
   vector origin = zone.GetOrigin();
   float deltaX = position[0] - origin[0];
   float deltaZ = position[2] - origin[2];
   float radius = zone.RadiusMeters;
   if (deltaX * deltaX + deltaZ * deltaZ <= radius * radius) return true;
  }
  return false;
 }

 // Bounded boundary samples share the same 64-point diagnostic packet as homes.
 static void AppendDebugDrawData(array<vector> positions, array<int> kinds)
 {
		EXPBG_LazyStatics_EAC_ExclusionZone();
  if (!positions || !kinds || !GetGame() || !Replication.IsServer()) return;
  BaseWorld world = GetGame().GetWorld();
  int zones;
  foreach (EAC_ExclusionZone zone : s_Zones)
  {
   if (!zone || zone.GetWorld() != world) continue;
   if (zones++ >= 2) return;
   for (int i = 0; i < 8 && positions.Count() < 64; i++)
   {
    float angle = i * Math.PI2 / 8;
    vector point = zone.GetOrigin() + Vector(Math.Cos(angle) * zone.RadiusMeters, 0, Math.Sin(angle) * zone.RadiusMeters);
    point[1] = world.GetSurfaceY(point[0], point[2]);
    positions.Insert(point); kinds.Insert(5);
   }
  }
 }

 static bool IsTransitAllowed(vector from, vector to)
 {
  // A disc is convex: both endpoints inside means the straight segment stays inside.
  if (EAC_AmbientModule.IsOutsidePopulationArea(from) || EAC_AmbientModule.IsOutsidePopulationArea(to)) return false;
  return IsJourneyTransitAllowed(from, to);
 }

 // Segment test for a departing car: see IsJourneyPointAllowed.
 static bool IsJourneyTransitAllowed(vector from, vector to)
 {
		EXPBG_LazyStatics_EAC_ExclusionZone();
  if (!EAC_AutoExclusions.IsTransitAllowed(from, to)) return false;
  // Same fast path as IsPopulationAllowed. The population-only case is handled by
  // the BlockTransit != 1 skip in the loop below rather than by a second cached
  // flag - one short pass, no state that can go stale.
  if (s_Zones.IsEmpty()) return true;
  if (!GetGame() || !Replication.IsServer()) return false;
  BaseWorld world = GetGame().GetWorld();
  if (!world) return false;
  foreach (EAC_ExclusionZone zone : s_Zones)
  {
   if (!zone || zone.GetWorld() != world || zone.BlockTransit != 1) continue;
   if (SegmentIntersectsDisc(from, to, zone.GetOrigin(), zone.RadiusMeters)) return false;
  }
  return true;
 }

 // Route pre-test for IsRouteAllowed, the manual-zone half of
 // EAC_AutoExclusions.AnyDiscTouchesBox. False proves IsJourneyTransitAllowed's
 // zone loop passes every segment inside the XZ box. The states in which that
 // loop refuses outright (no game, not the server, no world) answer true so the
 // caller reaches it. Dead slots are skipped, as the loop skips them.
 static bool AnyBlockingZoneTouchesBox(float minX, float minZ, float maxX, float maxZ)
 {
		EXPBG_LazyStatics_EAC_ExclusionZone();
  if (s_Zones.IsEmpty())
   return false;
  if (!GetGame() || !Replication.IsServer())
   return true;
  BaseWorld world = GetGame().GetWorld();
  if (!world)
   return true;
  foreach (EAC_ExclusionZone zone : s_Zones)
  {
   if (!zone || zone.GetWorld() != world || zone.BlockTransit != 1) continue;
   vector origin = zone.GetOrigin();
   float deltaX = Math.Clamp(origin[0], minX, maxX) - origin[0];
   float deltaZ = Math.Clamp(origin[2], minZ, maxZ) - origin[2];
   float radius = zone.RadiusMeters;
   float reach = Math.AbsFloat(radius) + ROUTE_BOX_MARGIN;
   if (deltaX * deltaX + deltaZ * deltaZ <= reach * reach)
    return true;
  }
  return false;
 }

 // One verdict for a whole native route (perf plan WP6, civilians-a-01). Equal to
 // IsTransitAllowed over every segment origin -> route[0] -> ... -> route[last],
 // ANDed, which is what the per-point sweeps in the walker, the activity approach
 // and the shelter route used to compute. Every term is pure, so the conjunction
 // is regrouped:
 // - the module-area half of IsTransitAllowed tests the two endpoints of each
 //   segment, so it is the area test once per point, skipped with no active
 //   module exactly as IsOutsidePopulationArea skips it;
 // - the zone half runs per segment only when an automatic disc or a
 //   transit-blocking zone touches the route's XZ bounding box. A segment lies
 //   inside that box, so a shape clear of the box is clear of every segment.
 //   Auto exclusions that are not ready still refuse: the pre-test answers true
 //   and IsJourneyTransitAllowed refuses the first segment.
 // An empty route passes, as the old loop did by running zero times.
 // AnyTransitBlocked, IsTransitAllowed and the traffic sweep are unchanged.
 // While a native fixture sets EBG_DebugChecks.Enabled, the old per-segment loop
 // also runs and any difference is reported there (perf plan rule 3).
 static bool IsRouteAllowed(vector origin, notnull array<vector> route)
 {
  bool allowed = RouteVerdict(origin, route);
  if (EBG_DebugChecks.Enabled && allowed != SegmentRouteAllowed(origin, route))
   EBG_DebugChecks.Mismatch(string.Format("EAC route verdict origin=%1 points=%2 regrouped=%3", origin, route.Count(), allowed));
  return allowed;
 }

 // The loop IsRouteAllowed replaced: IsTransitAllowed per segment, first refusal wins.
 protected static bool SegmentRouteAllowed(vector origin, array<vector> route)
 {
  vector previous = origin;
  foreach (vector point : route)
  {
   if (!IsTransitAllowed(previous, point))
    return false;
   previous = point;
  }
  return true;
 }

 protected static bool RouteVerdict(vector origin, array<vector> route)
 {
  if (route.IsEmpty())
   return true;
  EAC_AmbientModule active = EAC_AmbientModule.GetActive();
  if (active)
  {
   if (!active.ContainsPopulationPosition(origin))
    return false;
   foreach (vector inside : route)
   {
    if (!active.ContainsPopulationPosition(inside))
     return false;
   }
  }
  float minX = origin[0];
  float maxX = origin[0];
  float minZ = origin[2];
  float maxZ = origin[2];
  foreach (vector corner : route)
  {
   if (corner[0] < minX) minX = corner[0];
   if (corner[0] > maxX) maxX = corner[0];
   if (corner[2] < minZ) minZ = corner[2];
   if (corner[2] > maxZ) maxZ = corner[2];
  }
  if (!EAC_AutoExclusions.AnyDiscTouchesBox(minX, minZ, maxX, maxZ) && !AnyBlockingZoneTouchesBox(minX, minZ, maxX, maxZ))
   return true;
  vector previous = origin;
  foreach (vector next : route)
  {
   if (!IsJourneyTransitAllowed(previous, next))
    return false;
   previous = next;
  }
  return true;
 }

 static bool SegmentIntersectsDisc(vector from, vector to, vector origin, float radius)
 {
  float segmentX = to[0] - from[0];
  float segmentZ = to[2] - from[2];
  float lengthSquared = segmentX * segmentX + segmentZ * segmentZ;
  float factor = 0;
  if (lengthSquared > 0)
  {
   factor = ((origin[0] - from[0]) * segmentX + (origin[2] - from[2]) * segmentZ) / lengthSquared;
   factor = Math.Clamp(factor, 0, 1);
  }
  float closestX = from[0] + factor * segmentX;
  float closestZ = from[2] + factor * segmentZ;
  float deltaX = closestX - origin[0];
  float deltaZ = closestZ - origin[2];
  return deltaX * deltaX + deltaZ * deltaZ <= radius * radius;
 }

	//------------------------------------------------------------------------------------------------
	//! Creates the collections on first use (not in the global static initializer, which has a
	//! per-function instruction limit that large modsets exceed on Windows).
	protected static void EXPBG_LazyStatics_EAC_ExclusionZone()
	{
		if (!s_Zones)
			s_Zones = new array<EAC_ExclusionZone>();
	}
}

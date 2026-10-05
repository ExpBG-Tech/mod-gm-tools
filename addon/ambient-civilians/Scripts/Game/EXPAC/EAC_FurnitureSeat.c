// Geometry for ordinary map furniture. This class never creates or owns the prop.
class EAC_FurnitureSeat
{
 // Vanilla PlayBedAnimation(true): enter the seated loop without its turning
 // approach clip. Uses the game's normal alignment, root motion and exit.
 static const int LOITER_TYPE = 8;
 static bool Family(string stem, string name)
 {
  return stem == name || stem.IndexOf(name + "_") == 0;
 }

 // Local seat surface and facing, not the top of the model's backrest.
 // ponytail: support inspected conventional seats; unknown shapes remain standing spots.
 static int Profile(ResourceName prefab, out vector seat, out vector facing, out float spacing)
 {
  seat = "0 0.48 0"; facing = "0 0 1"; spacing = 0;
  string path = prefab; path.ToLower();
  if (path.Contains("/dst/") || path.Contains("_dst") || path.Contains("ruin") || path.Contains("_base.et") || path.Contains("/compositions/")) return 0;
  string stem = EAC_SceneVocabulary.FamilyOf(prefab);
  if (Family(stem, "benchwooden_01")) { seat[1] = 0.51; spacing = 0.35; return 2; }
  if (Family(stem, "benchwooden_02")) { seat[1] = 0.50; spacing = 0.55; return 2; }
  if (Family(stem, "benchstreet_01") || Family(stem, "benchstreet_02")) { spacing = 0.45; return 2; }
  if (Family(stem, "chairold_01") || Family(stem, "chairold_02") || Family(stem, "chair_01")) { seat[1] = 0.46; return 1; }
  return 0;
 }

 static bool Geometry(IEntity furniture, int slot, out vector approach[4], out vector pose[4], out vector surface)
 {
  if (!furniture || !furniture.GetPrefabData()) return false;
  vector seat, facing; float spacing;
  int count = Profile(furniture.GetPrefabData().GetPrefabName(), seat, facing, spacing);
  if (slot < 0 || slot >= count) return false;
  vector matrix[4]; furniture.GetWorldTransform(matrix);
  if (vector.Dot(matrix[1].Normalized(), "0 1 0") < 0.98) return false;
  if (count == 2) { seat[0] = -spacing; if (slot == 1) seat[0] = spacing; }
  surface = seat.Multiply4(matrix);
  vector forward = facing.Multiply3(matrix); forward[1] = 0;
  if (forward.LengthSq() < 0.1) return false;
  forward.Normalize();
  // sit_bed's pelvis is above/behind the character root; the approach stays clear
  // of the furniture so native pathfinding does not need a navmesh on the seat.
  // Keep the native character capsule ahead of the cushion. At 0.20 m its
  // collision support climbs onto the bench and raises the entire seated pose.
  vector root = surface + forward * 0.60 - "0 0.48 0";
  EAC_RoutineAnchors.Face(forward, root, pose);
  EAC_RoutineAnchors.Face(forward, root + forward * 0.75, approach);
  return true;
 }

 static bool Present(EAC_SceneSpot spot)
 {
  if (!spot || !spot.Furniture) return false;
  vector matrix[4]; spot.Furniture.GetWorldTransform(matrix);
  if (vector.Distance(matrix[3], spot.FurnitureTransform[3]) > 0.10) return false;
  if (vector.Dot(matrix[2], spot.FurnitureTransform[2]) < 0.99 || vector.Dot(matrix[1].Normalized(), "0 1 0") < 0.98) return false;
  DamageManagerComponent damage = DamageManagerComponent.Cast(spot.Furniture.FindComponent(DamageManagerComponent));
  return !damage || !damage.IsDestroyed();
 }

 static bool SurfaceClear(EAC_SceneSpot spot, IEntity actor)
 {
  if (!Present(spot) || !EAC_ExclusionZone.IsPopulationAllowed(spot.PoseTransform[3])) return false;
  if (!EAC_ActivityStation.OffRoad(spot.PoseTransform[3])) return false;
  TraceParam trace = new TraceParam();
  trace.Start = spot.SeatSurface + "0 0.18 0"; trace.End = spot.SeatSurface - "0 0.18 0";
  trace.Flags = TraceFlags.WORLD | TraceFlags.ENTS; trace.Exclude = actor;
  float hit = spot.Furniture.GetWorld().TraceMove(trace, null);
  if (hit >= 1 || trace.TraceEnt != spot.Furniture) return false;
  // Check the seated torso/head as well as the separate standing approach.
  // Ignore the seat itself and its assigned actor, not unrelated obstructions.
  array<IEntity> excluded = {spot.Furniture};
  if (actor) excluded.Insert(actor);
  TraceBox body = new TraceBox();
  body.Start = spot.SeatSurface + "0 0.22 0";
  body.End = spot.SeatSurface + "0 0.95 0";
  body.Mins = "-0.24 0 -0.24"; body.Maxs = "0.24 0.1 0.24";
  body.Flags = TraceFlags.WORLD | TraceFlags.ENTS; body.ExcludeArray = excluded;
  return spot.Furniture.GetWorld().TraceMove(body, null) == 1;
 }
}

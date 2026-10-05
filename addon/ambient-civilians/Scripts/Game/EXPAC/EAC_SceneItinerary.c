// The chain that makes a resident's day read as a shape rather than a loop.
//
// A fixed rotation of N routines always reveals itself to anyone watching long
// enough. This does three things instead: it walks between scenes rather than
// re-using one, it sometimes stays where it is, and it varies which of several
// verified positions it picks. Nothing enumerates or stores a sequence.
modded class EAC_ResidentRecord
{
 int SceneHomeId;   // record currently targeted, 0 none
 int SceneHops;     // 0..MAX_HOPS, the shape of the excursion
 int SceneSeed;     // derived once from Id, never rerolled
 // Audit S11. SceneFace, SceneSpot, AnimHistory and WantsTravel were written on
 // every commit and read by nothing. They are gone rather than left as retained
 // state that looks load-bearing; EAC_SceneIndex owns which spot a resident holds
 // (OccupantId), so nothing needed a second copy of it here.
}

class EAC_SceneItinerary
{
 static const int MAX_HOPS = 4;
 static const int MAX_CANDIDATE_RECORDS = 16;
 // How often a resident simply stays around rather than moving on. Without this
 // the chain is a strict tour and reads as scripted; with it, somebody sometimes
 // just keeps sitting.
 static const int LINGER_PERCENT = 28;
 // A drift goal has to be far enough away for a BEARING to it to mean anything.
 //
 // Peek returns record.Centre, which is the BUILDING ORIGIN. Hop 0 and the
 // closing hop are the resident's own home, so the goal was the middle of the
 // house the resident was standing beside: every goal-biased wander bearing
 // pointed into its own wall and the candidate 10 m along it landed inside the
 // footprint, failing the navmesh projection or the body box. Nearer than ~2 m
 // the fan did not even arm, because EAC_PedestrianWalk requires
 // toGoal.LengthSq() > 4, and EAC_PedestrianSpawner.ServiceOne then calls
 // ClearGoal(). That is why fan_goal has been 0-2 across every campaign that
 // measured it. Matched to the walker's own 6-20 m leg band.
 static const float MIN_GOAL_DISTANCE = 20;

 static void EnsureSeeded(EAC_ResidentRecord resident)
 {
  if (!resident || resident.SceneSeed != 0) return;
  resident.SceneSeed = (resident.Id * 31) % 997 + 1;
 }

 // Where the resident should drift while it has no reservation. Pure arithmetic
 // and no reservation is taken, so calling this every tick is free.
 static bool Peek(EAC_AmbientModule module, EAC_ResidentClaim claim, out vector goal)
 {
  goal = vector.Zero;
  if (!module || !claim || !claim.Home || !claim.Resident) return false;
  EAC_ResidentRecord resident = claim.Resident;
  EnsureSeeded(resident);
  if (resident.SceneHomeId == 0) return false;
  EAC_SceneRecord record = EAC_SceneIndex.Find(resident.SceneHomeId);
  if (!record || record.State != 2) return false;
  if (!claim.Character) return false;
  // Refuse a goal the resident is effectively already standing on, rather than
  // handing the walker a bearing into the nearest wall.
  if (vector.DistanceXZ(claim.Character.GetOrigin(), record.Centre) < MIN_GOAL_DISTANCE) return false;
  goal = record.Centre;
  return true;
 }

 // Choose which surveyed record this beat belongs to, then let the index pick a
 // position inside it. Hop 0 is the resident's own home - step out and use the
 // yard or the wall behind it. The middle hops must be a different record, which
 // is the "walk to the neighbour's wall, the bench in the square" part. The last
 // hop forces home again, so the excursion has a shape and returns.
 //
 // Audit S1. Reserve no longer commits. It reports the record it took a position
 // from in `homeId` and the caller calls Commit only on the branch that actually
 // keeps the reservation - EAC_CivilianActivity.OutdoorAnchor offers up to three
 // spots and CanApproach may reject the first two, and committing inside here
 // retargeted the resident's whole excursion on a rejection.
 static bool Reserve(EAC_AmbientModule module, EAC_ResidentClaim claim, EAC_RoutineDefinition routine, float now, out vector transform[4], out int homeId)
 {
  Math3D.MatrixIdentity4(transform);
  homeId = 0;
  if (!Replication.IsServer() || !module || module.SceneSurvey <= 0) return false;
  if (!claim || !claim.Home || !claim.Resident || !claim.Character || !routine) return false;
  // The leash, the candidate collection and the fallback below all dereference
  // the home building; a household whose building was removed under a live
  // resident must refuse here rather than at the first GetOrigin (audit nit).
  if (!claim.Home.BuildingEntity) return false;
  EAC_ResidentRecord resident = claim.Resident;
  EnsureSeeded(resident);
  int hop = resident.SceneHops;
  int ownId = claim.Home.Id;

  // Hop 0 and the closing hop belong to the resident's own home.
  if (hop == 0 || hop >= MAX_HOPS)
  {
   EAC_SceneRecord own = EAC_SceneIndex.Find(ownId);
   if (own && EAC_SceneIndex.ReserveFrom(own, module, claim, routine, now, transform))
   {
    homeId = ownId;
    return true;
   }
   // Fall through: a barren or full own record must not stall the chain.
  }

  array<EAC_SceneRecord> candidates = {};
  // Record centres are pulled in so their spots, which sit further out than the
  // centre, still fall inside the same leash CanApproach enforces.
  float reach = module.RoutineRange - 20;
  if (reach < 20) reach = 20;
  EAC_SceneIndex.CollectWithin(claim.Home.BuildingEntity.GetOrigin(), reach, resident.SceneSeed + hop, MAX_CANDIDATE_RECORDS, candidates);
  foreach (EAC_SceneRecord record : candidates)
  {
   // The middle of an excursion must move somewhere else, or the resident
   // simply cycles animations on one plot.
   if (hop > 0 && hop < MAX_HOPS && record.HomeId == resident.SceneHomeId) continue;
   if (!EAC_SceneIndex.ReserveFrom(record, module, claim, routine, now, transform)) continue;
   homeId = record.HomeId;
   return true;
  }
  return false;
 }

 // Called by the owner of the reservation, once, on the branch that keeps it.
 static void Commit(EAC_ResidentRecord resident, int homeId)
 {
  if (!resident) return;
  resident.SceneHomeId = homeId;
 }

 // Called once per genuinely entered occupation, so a repeatedly failing beat
 // can never reroll the chain.
 static void Advance(EAC_ResidentRecord resident)
 {
  if (!resident) return;
  EnsureSeeded(resident);
  // Sometimes just stay around. This is the difference between a tour and a
  // person: the same place twice running, with a different animation and a
  // fresh dwell, is ordinary behaviour and breaks any visible period.
  // Identity-derived, so a wake cycle reproduces the same chain. Same shape as
  // the TableBias roll in EAC_RoutineCatalog - docs/ROUTINES.md rule 7. SceneHops
  // is in the roll because Advance now fires two to four times per routine and
  // consecutive stops must not roll identically. EAC_RoutineCatalog.Advance runs
  // first inside EAC_ActivityProfiles.Advance, so ActivityStep has already moved
  // when this roll is taken: deterministic and intended. Int arithmetic only -
  // '%' is invalid in a float expression in Enforce.
  int roll = (resident.Id * 23 + resident.ActivityStep * 13 + resident.SceneHops * 7) % 100;
  if (roll < LINGER_PERCENT) return;
  resident.SceneHops++;
  if (resident.SceneHops > MAX_HOPS) resident.SceneHops = 0;
 }
}

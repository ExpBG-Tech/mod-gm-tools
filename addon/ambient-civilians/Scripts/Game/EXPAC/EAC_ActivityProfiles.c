// Shared across this live actor's activity objects; reset after complete despawn.
modded class EAC_ResidentRecord
{
 int ActivityStep;
 int FailedTableAttempts;
 float NextRoutineAt;
 bool HasActivityPosition;
 vector LastActivityPosition;
 // Stable danger reaction during a live activation: 0 unset, then 1..4.
 int DangerResponse;
 // A town where everyone starts on the same routine is neither believable nor
 // testable: the later catalog slots are never reached in a session.
 bool RoutineSeeded;
 // Audit B2. Where the bounded anchor and station probe sweeps resume from. They
 // live on the retained resident, not on EAC_CivilianActivity: a start refused
 // for want of supply discards the activity object, so an object-local cursor
 // would restart both sweeps at the same bearings on every retry and never reach
 // the later probes at all.
 int AnchorCursor;
 int StationCursor;
}

// Thin policy layer over the routine catalog. It keeps the anchor/table rules;
// the believable routine slots themselves live in EAC_RoutineCatalog and the
// geometry they need lives in EAC_RoutineAnchors.
//
// This addon carries no third-party dependency: nothing but the base game and
// EXPBG GM Optimizer is in the project file, and nothing from another mod is
// bundled. EAC_VanillaSeat is still the only seat machine. The one permitted
// runtime look at the loaded addon list is EAC_AceAnimations, which may adopt an
// already-loaded ACE animation through the vanilla CUSTOM loitering route and is
// never required for anything: with ACE absent it adds nothing.
class EAC_ActivityProfiles
{
 static EAC_RoutineDefinition Select(EAC_ResidentRecord resident)
 {
  return EAC_RoutineCatalog.Select(resident);
 }

 static ELoiteringType Animation(EAC_RoutineDefinition routine)
 {
  return EAC_RoutineCatalog.Animation(routine);
 }

 static ResourceName Point(EAC_RoutineDefinition routine)
 {
  return EAC_RoutineCatalog.Point(routine);
 }

 static EAC_EAnchorKind Anchor(EAC_RoutineDefinition routine)
 {
  if (!routine) return EAC_EAnchorKind.YARD_OPEN;
  return routine.Anchor;
 }

 static string Name(EAC_RoutineDefinition routine)
 {
  if (!routine) return "Idle";
  return routine.Name;
 }

 static string Role(EAC_ResidentRecord resident)
 {
  return EAC_RoutineCatalog.RoleName(EAC_RoutineCatalog.RoleOf(resident));
 }

 static bool NeedsTable(EAC_RoutineDefinition routine)
 {
  return routine && routine.NeedsTable;
 }

 static float Dwell(EAC_RoutineDefinition routine)
 {
  return EAC_RoutineCatalog.Dwell(routine);
 }

 // Consecutive occupations must be visibly apart, so a routine reads as travel
 // between places rather than repetition on one spot.
 static bool DifferentAnchor(EAC_ResidentRecord resident, vector position)
 {
  return !resident || !resident.HasActivityPosition || vector.DistanceXZ(resident.LastActivityPosition, position) >= 8;
 }

 // How many stops this routine plans. Deterministic in retained identity, the
 // same shape as the TableBias roll in EAC_RoutineCatalog - docs/ROUTINES.md
 // rule 7. span 1 restores single-occupation routines byte for byte, which is
 // the A/B control the RoutineStops setting exists for.
 static int RoutineStops(EAC_ResidentRecord resident, int span)
 {
  if (!resident) return 1;
  span = Math.Clamp(span, 1, 4);
  if (span < 2) return 1;
  int range = span - 1;                       // span 4 -> {2,3,4}
  // Enforce's '%' keeps the sign of the dividend, so a negative retained Id
  // would index below the band. Fold the mix positive first; Math.AbsInt is not
  // in the offline API dump, so this is the explicit form.
  int mix = resident.Id * 17 + resident.ActivityStep * 7;
  if (mix < 0) mix = -mix;
  int spread = mix % range;
  return 2 + spread;
 }

 // Which stop carries the catalog's authored dwell. Every other stop is short,
 // so a routine reads as "went out, did a few things, settled somewhere".
 static int LongStop(EAC_ResidentRecord resident, int stops)
 {
  if (!resident || stops < 2) return 0;
  // Same sign rule as RoutineStops: a negative index here would pick no long
  // stop at all and every stop in the chain would be short.
  int mix = resident.Id * 7 + resident.ActivityStep * 13;
  if (mix < 0) mix = -mix;
  return mix % stops;
 }

 // A table routine always keeps its authored band: it drives the proven seat
 // ladder, whose Prepare clamps dwell to 30..600, and its timing is measured.
 //
 // Audit S14. Every dwell here is now derived from (Id, ActivityStep, leg)
 // instead of Math.RandomFloat, so design rule 7 holds without the "only dwell
 // jitter varies" exception: a cached resident that wakes and replays the same
 // step gets the same dwell, and a campaign replaying a seed sees the same
 // timings. The bands are unchanged - 25..70 s for an ordinary stop, the catalog
 // slot's own MinDwell..MaxDwell for the long stop and for every table stop - so
 // the spread across a town is what it was; it is the same value twice for the
 // same person on the same step that is new. The leg term is what keeps the
 // stops of one chain from all drawing the identical dwell.
 static float StopDwell(EAC_RoutineDefinition routine, EAC_ResidentRecord resident, int leg, int longLeg)
 {
  if (NeedsTable(routine)) return EAC_RoutineCatalog.DwellFor(routine, resident, leg);
  if (leg == longLeg) return EAC_RoutineCatalog.DwellFor(routine, resident, leg);
  return EAC_RoutineCatalog.Spread(resident, leg, 3, 25, 70);
 }

 // Leg-aware form of DifferentAnchor. DifferentAnchor itself is deliberately
 // untouched: the routine fixture pins its 3 m / 9 m behaviour.
 static bool SeparatedBy(EAC_ResidentRecord resident, vector position, float minimum)
 {
  return !resident || !resident.HasActivityPosition || vector.DistanceXZ(resident.LastActivityPosition, position) >= minimum;
 }

 static void Advance(EAC_ResidentRecord resident)
 {
  EAC_RoutineCatalog.Advance(resident);
  // Move the excursion on - or sometimes deliberately not, so the same place
  // twice running is possible and no visible period exists.
  EAC_SceneItinerary.Advance(resident);
 }

 static void TableAttemptFailed(EAC_ResidentRecord resident)
 {
  if (!resident) return;
  EAC_RoutineDefinition current = EAC_RoutineCatalog.Select(resident);
  if (!current || !current.NeedsTable) return;
  resident.FailedTableAttempts++;
  if (resident.FailedTableAttempts < 3) return;
  // Switch the retained purpose only. Ordinary admission still enforces its
  // configured interval, off-road anchor, exclusions and native approach.
  int count = EAC_RoutineCatalog.Count();
  for (int attempt = 0; attempt < count; attempt++)
  {
   EAC_RoutineCatalog.Advance(resident);
   EAC_RoutineDefinition next = EAC_RoutineCatalog.Select(resident);
   if (next && !next.NeedsTable) break;
  }
  resident.FailedTableAttempts = 0;
 }

 // Printed by the GM debug HUD. Reports "vanilla" unless the optional probe
 // validated at least one already-loaded ACE animation, in which case it names
 // how many. It never says ACE is a dependency, because it is not one.
 static string CompatibilityStatus()
 {
  string status = "Native routine catalog (";
  status += EAC_RoutineCatalog.NativeCount().ToString();
  status += " native";
  int optional = EAC_RoutineCatalog.OptionalCount();
  if (optional > 0)
  {
   status += " + ";
   status += optional.ToString();
   status += " detected";
  }
  status += " slots); ";
  status += EAC_AceAnimations.Status();
  return status;
 }
}

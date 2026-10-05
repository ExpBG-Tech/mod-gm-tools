// Believable routine slots. Each entry is one stationary occupation at one
// verified anchor. Longer behaviour emerges from consecutive admissions: the
// resident walks between anchors under the ordinary walking rules, so a routine
// reads as "walk, do something, walk somewhere else, do something else".
//
// Only native civilian loiter animations are referenced. PUSHUPS is deliberately
// excluded as military-looking, and this addon still ships no authored animation
// data of its own. CUSTOM_ANIM slots are never authored here: they exist only
// when EAC_AceAnimations has validated an animation that some other addon
// already loaded on this server owns, and there are none in the vanilla case.
enum EAC_ERoutineAnim
{
 IDLE,
 SMOKE,
 SIT_GROUND,
 LEAN_LEFT,
 LEAN_RIGHT,
 SEATED,
 CUSTOM_ANIM
}

class EAC_RoutineDefinition
{
 string Name;
 int Role;      // -1: any role
 int TimeMask;
 EAC_EAnchorKind Anchor;
 EAC_ERoutineAnim Anim;
 float MinDwell, MaxDwell;
 bool NeedsTable;
 // Null for every vanilla slot. Set only on a runtime-detected slot, and only
 // after the probe validated the graph, the instance and the bound command.
 ref EAC_CustomAnim Custom;
}

class EAC_RoutineCatalog
{
 static const int DAWN = 1;
 static const int DAY = 2;
 static const int DUSK = 4;
 static const int NIGHT = 8;
 static const int ANY = 15;

 static const int ROLE_HOUSEHOLDER = 0;
 static const int ROLE_NEIGHBOUR = 1;
 static const int ROLE_WORKER = 2;
 static const int ROLE_IDLER = 3;

 protected static ref array<ref EAC_RoutineDefinition> s_Entries;
 // Runtime-detected slots are held apart from the authored ones, so the vanilla
 // catalog array is never rewritten and a world change can drop the optional
 // slots without rebuilding anything.
 protected static ref array<ref EAC_RoutineDefinition> s_Optional = {};
 protected static BaseWorld s_OptionalWorld;
 protected static int s_OptionalSource = -1;

 protected static void Add(string name, int role, int timeMask, EAC_EAnchorKind anchor, EAC_ERoutineAnim anim, float minDwell, float maxDwell, bool needsTable = false)
 {
  EAC_RoutineDefinition entry = new EAC_RoutineDefinition();
  entry.Name = name; entry.Role = role; entry.TimeMask = timeMask;
  entry.Anchor = anchor; entry.Anim = anim;
  entry.MinDwell = minDwell; entry.MaxDwell = maxDwell; entry.NeedsTable = needsTable;
  s_Entries.Insert(entry);
 }

 protected static void Build()
 {
  if (s_Entries) return;
  s_Entries = {};

  // Householder: life immediately around the assigned home.
  Add("Step out for air", ROLE_HOUSEHOLDER, ANY, EAC_EAnchorKind.DOORSTEP, EAC_ERoutineAnim.IDLE, 60, 120);
  Add("Smoke by the door", ROLE_HOUSEHOLDER, ANY, EAC_EAnchorKind.DOORSTEP, EAC_ERoutineAnim.SMOKE, 90, 150);
  Add("Lean on the house wall", ROLE_HOUSEHOLDER, DAY | DUSK, EAC_EAnchorKind.WALL_BACK, EAC_ERoutineAnim.LEAN_LEFT, 120, 200);
  Add("Check the yard", ROLE_HOUSEHOLDER, DAY, EAC_EAnchorKind.YARD_OPEN, EAC_ERoutineAnim.IDLE, 40, 80);
  Add("Sit on the step", ROLE_HOUSEHOLDER, DAY, EAC_EAnchorKind.DOORSTEP, EAC_ERoutineAnim.SIT_GROUND, 150, 260);
  Add("Rest at the fence", ROLE_HOUSEHOLDER, DAY, EAC_EAnchorKind.LOW_EDGE, EAC_ERoutineAnim.IDLE, 45, 90);
  Add("Rest under the eaves", ROLE_HOUSEHOLDER, ANY, EAC_EAnchorKind.OVERHEAD, EAC_ERoutineAnim.IDLE, 150, 240);
  Add("Shelter from the sun", ROLE_HOUSEHOLDER, DAY, EAC_EAnchorKind.OVERHEAD, EAC_ERoutineAnim.SIT_GROUND, 180, 300);
  Add("Stand out of the wind", ROLE_HOUSEHOLDER, ANY, EAC_EAnchorKind.CORNER, EAC_ERoutineAnim.LEAN_RIGHT, 90, 150);
  Add("Smoke at the gate", ROLE_HOUSEHOLDER, DAY | DUSK, EAC_EAnchorKind.DOORSTEP, EAC_ERoutineAnim.SMOKE, 80, 140);
  Add("Linger in the doorway", ROLE_HOUSEHOLDER, DUSK | NIGHT, EAC_EAnchorKind.DOORSTEP, EAC_ERoutineAnim.IDLE, 100, 180);
  Add("Out at first light", ROLE_HOUSEHOLDER, DAWN, EAC_EAnchorKind.YARD_OPEN, EAC_ERoutineAnim.IDLE, 60, 120);
  Add("Lean beside the door", ROLE_HOUSEHOLDER, ANY, EAC_EAnchorKind.WALL_BACK, EAC_ERoutineAnim.LEAN_RIGHT, 110, 190);
  Add("Sit indoors at the table", ROLE_HOUSEHOLDER, ANY, EAC_EAnchorKind.YARD_OPEN, EAC_ERoutineAnim.SEATED, 180, 300, true);
  Add("Smoke at the bench outside", ROLE_HOUSEHOLDER, DAY | DUSK, EAC_EAnchorKind.SEAT, EAC_ERoutineAnim.SMOKE, 90, 160);

  // Neighbour: life between homes, at walls, corners and roadside edges.
  Add("Lean on a neighbour's wall", ROLE_NEIGHBOUR, DAY, EAC_EAnchorKind.WALL_BACK, EAC_ERoutineAnim.LEAN_LEFT, 120, 200);
  Add("Stand and talk", ROLE_NEIGHBOUR, DAY, EAC_EAnchorKind.YARD_OPEN, EAC_ERoutineAnim.IDLE, 120, 240);
  Add("Share a smoke", ROLE_NEIGHBOUR, ANY, EAC_EAnchorKind.WALL_BACK, EAC_ERoutineAnim.SMOKE, 90, 150);
  Add("Watch from the corner", ROLE_NEIGHBOUR, DAY, EAC_EAnchorKind.CORNER, EAC_ERoutineAnim.LEAN_LEFT, 120, 200);
  Add("Pause at the roadside", ROLE_NEIGHBOUR, DAY, EAC_EAnchorKind.LOW_EDGE, EAC_ERoutineAnim.IDLE, 40, 70);
  Add("Sit on the low wall", ROLE_NEIGHBOUR, DAY, EAC_EAnchorKind.LOW_EDGE, EAC_ERoutineAnim.SIT_GROUND, 180, 280);
  Add("Wait at a gate", ROLE_NEIGHBOUR, DAY, EAC_EAnchorKind.DOORSTEP, EAC_ERoutineAnim.IDLE, 150, 250);
  Add("Stop across the road", ROLE_NEIGHBOUR, DAY, EAC_EAnchorKind.WALL_BACK, EAC_ERoutineAnim.LEAN_RIGHT, 100, 180);
  Add("Loiter under a tree", ROLE_NEIGHBOUR, DAY, EAC_EAnchorKind.OVERHEAD, EAC_ERoutineAnim.IDLE, 150, 250);
  Add("Sit under a tree", ROLE_NEIGHBOUR, DAY, EAC_EAnchorKind.OVERHEAD, EAC_ERoutineAnim.SIT_GROUND, 200, 320);
  Add("Smoke under cover", ROLE_NEIGHBOUR, ANY, EAC_EAnchorKind.OVERHEAD, EAC_ERoutineAnim.SMOKE, 90, 160);
  Add("Stand in the square", ROLE_NEIGHBOUR, DAY, EAC_EAnchorKind.YARD_OPEN, EAC_ERoutineAnim.IDLE, 90, 170);
  Add("Go quiet at dusk", ROLE_NEIGHBOUR, DUSK, EAC_EAnchorKind.DOORSTEP, EAC_ERoutineAnim.IDLE, 90, 150);
  Add("Evening smoke on the corner", ROLE_NEIGHBOUR, DUSK | NIGHT, EAC_EAnchorKind.CORNER, EAC_ERoutineAnim.SMOKE, 100, 170);
  Add("Neighbours at the table", ROLE_NEIGHBOUR, ANY, EAC_EAnchorKind.YARD_OPEN, EAC_ERoutineAnim.SEATED, 180, 300, true);
  Add("Read the notice board", ROLE_NEIGHBOUR, DAWN | DAY, EAC_EAnchorKind.PROP_FACE, EAC_ERoutineAnim.IDLE, 60, 120);

  // Worker: routines with a task shape, further from the home.
  Add("Out to the work spot", ROLE_WORKER, DAY, EAC_EAnchorKind.YARD_OPEN, EAC_ERoutineAnim.IDLE, 180, 300);
  Add("Inspect the fence", ROLE_WORKER, DAY, EAC_EAnchorKind.LOW_EDGE, EAC_ERoutineAnim.IDLE, 45, 90);
  Add("Break under the overhang", ROLE_WORKER, DAY, EAC_EAnchorKind.OVERHEAD, EAC_ERoutineAnim.SMOKE, 90, 150);
  Add("Sit down for a break", ROLE_WORKER, DAY, EAC_EAnchorKind.LOW_EDGE, EAC_ERoutineAnim.SIT_GROUND, 200, 300);
  Add("Back to the house", ROLE_WORKER, DAY, EAC_EAnchorKind.DOORSTEP, EAC_ERoutineAnim.IDLE, 40, 80);
  Add("Long shift at the table", ROLE_WORKER, DAY, EAC_EAnchorKind.YARD_OPEN, EAC_ERoutineAnim.SEATED, 240, 360, true);
  Add("Break by the well", ROLE_WORKER, DAY, EAC_EAnchorKind.PROP_FACE, EAC_ERoutineAnim.SMOKE, 80, 150);
  Add("Two-man job", ROLE_WORKER, DAY, EAC_EAnchorKind.LOW_EDGE, EAC_ERoutineAnim.IDLE, 180, 280);
  Add("Lean on the wall, resting", ROLE_WORKER, DAY, EAC_EAnchorKind.WALL_BACK, EAC_ERoutineAnim.LEAN_RIGHT, 140, 230);
  Add("Stand and supervise", ROLE_WORKER, DAY, EAC_EAnchorKind.YARD_OPEN, EAC_ERoutineAnim.IDLE, 150, 250);
  Add("Knock off at dusk", ROLE_WORKER, DUSK, EAC_EAnchorKind.DOORSTEP, EAC_ERoutineAnim.SMOKE, 90, 160);
  Add("Early start", ROLE_WORKER, DAWN, EAC_EAnchorKind.YARD_OPEN, EAC_ERoutineAnim.IDLE, 60, 120);
  Add("Wait for a lift", ROLE_WORKER, DAY, EAC_EAnchorKind.LOW_EDGE, EAC_ERoutineAnim.SMOKE, 120, 200);
  Add("Shelter and wait", ROLE_WORKER, ANY, EAC_EAnchorKind.CORNER, EAC_ERoutineAnim.LEAN_LEFT, 110, 190);
  Add("Rest against the corner", ROLE_WORKER, ANY, EAC_EAnchorKind.CORNER, EAC_ERoutineAnim.IDLE, 100, 170);

  // Idler: ambient texture that fills gaps when richer anchors are unavailable.
  Add("Stand and look at nothing", ROLE_IDLER, ANY, EAC_EAnchorKind.YARD_OPEN, EAC_ERoutineAnim.IDLE, 120, 200);
  Add("Watch the sky", ROLE_IDLER, DAY, EAC_EAnchorKind.YARD_OPEN, EAC_ERoutineAnim.IDLE, 60, 110);
  Add("Sit on the ground", ROLE_IDLER, DAY, EAC_EAnchorKind.YARD_OPEN, EAC_ERoutineAnim.SIT_GROUND, 200, 300);
  Add("Smoke in the open", ROLE_IDLER, ANY, EAC_EAnchorKind.YARD_OPEN, EAC_ERoutineAnim.SMOKE, 90, 150);
  Add("Lean and watch the lane", ROLE_IDLER, ANY, EAC_EAnchorKind.WALL_BACK, EAC_ERoutineAnim.LEAN_LEFT, 130, 210);
  Add("Idle in the doorway", ROLE_IDLER, ANY, EAC_EAnchorKind.DOORSTEP, EAC_ERoutineAnim.IDLE, 150, 250);
  Add("Shelter in a doorway", ROLE_IDLER, ANY, EAC_EAnchorKind.DOORSTEP, EAC_ERoutineAnim.LEAN_RIGHT, 150, 250);
  Add("Rest under cover", ROLE_IDLER, ANY, EAC_EAnchorKind.OVERHEAD, EAC_ERoutineAnim.IDLE, 160, 260);
  Add("Sit at the kerb", ROLE_IDLER, DAY, EAC_EAnchorKind.LOW_EDGE, EAC_ERoutineAnim.SIT_GROUND, 190, 290);
  Add("Night smoke by the wall", ROLE_IDLER, NIGHT, EAC_EAnchorKind.WALL_BACK, EAC_ERoutineAnim.SMOKE, 100, 170);
  Add("Stand in the corner", ROLE_IDLER, ANY, EAC_EAnchorKind.CORNER, EAC_ERoutineAnim.IDLE, 110, 180);
  Add("Quiet spot at the edge", ROLE_IDLER, ANY, EAC_EAnchorKind.LOW_EDGE, EAC_ERoutineAnim.IDLE, 100, 170);
  Add("Dawn stretch outside", ROLE_IDLER, DAWN, EAC_EAnchorKind.DOORSTEP, EAC_ERoutineAnim.IDLE, 70, 130);
  Add("Sit indoors alone", ROLE_IDLER, ANY, EAC_EAnchorKind.YARD_OPEN, EAC_ERoutineAnim.SEATED, 200, 320, true);
  Add("Linger at the picnic table", ROLE_IDLER, DAY | DUSK, EAC_EAnchorKind.SEAT, EAC_ERoutineAnim.IDLE, 110, 200);
 }

 // Spread the population across the catalog at birth. Without this every
 // resident starts at slot 0 and walks the list in step, so neighbours do the
 // same thing at the same time and the later slots are never reached in a
 // session. Derived from retained identity, so cache/wake never rerolls it.
 static void EnsureSeeded(EAC_ResidentRecord resident)
 {
  Build();
  if (!resident || resident.RoutineSeeded || s_Entries.IsEmpty()) return;
  resident.RoutineSeeded = true;
  // Deliberately the authored count, not Count(): the birth spread must be the
  // same number on a vanilla server and on one that happens to run ACE, so a
  // resident's identity resolves to the same starting routine either way.
  resident.ActivityStep = (resident.Id * 13) % s_Entries.Count();
 }

 // The catalog a resident may actually be assigned from: the authored vanilla
 // slots, plus any runtime-detected slot the ACE probe validated. With no ACE
 // loaded - or with ACE loaded but nothing usable in it, which is the measured
 // case today - SyncOptional adds nothing and this is byte-for-byte the 61
 // authored entries.
 static void Catalog()
 {
  Build();
  SyncOptional();
 }

 // Rebuild the optional tail when the world changed or the probe's answer
 // changed. Never called per frame: ValidCount() is a cached read and the
 // source count only moves once, when the probe first resolves.
 protected static void SyncOptional()
 {
  BaseWorld world = GetGame().GetWorld();
  int source = EAC_AceAnimations.ValidCount();
  if (world == s_OptionalWorld && source == s_OptionalSource) return;
  s_OptionalWorld = world;
  s_OptionalSource = source;
  s_Optional.Clear();
  if (source < 1) return;
  for (int i = 0; i < source; i++)
  {
   EAC_CustomAnim anim = EAC_AceAnimations.Valid(i);
   if (!anim) continue;
   EAC_RoutineDefinition entry = new EAC_RoutineDefinition();
   entry.Name = anim.Name;
   entry.Role = -1;
   entry.TimeMask = ANY;
   // A detected pose is a standalone occupation on ordinary open ground: it
   // reserves nothing, needs no furniture and creates no prop.
   entry.Anchor = EAC_EAnchorKind.YARD_OPEN;
   entry.Anim = EAC_ERoutineAnim.CUSTOM_ANIM;
   entry.MinDwell = anim.MinSeconds;
   entry.MaxDwell = anim.MaxSeconds;
   entry.NeedsTable = false;
   entry.Custom = anim;
   s_Optional.Insert(entry);
  }
 }

 static int Count()
 {
  Catalog();
  return s_Entries.Count() + s_Optional.Count();
 }

 // How many of Count() are the authored vanilla slots. Stays 61 on every server.
 static int NativeCount() { Build(); return s_Entries.Count(); }

 static int OptionalCount() { Catalog(); return s_Optional.Count(); }

 // Build() only, deliberately: the optional tail is resynchronised by Count() /
 // Catalog(), which every caller reaches first, so a per-element Get() in a
 // selection loop costs no extra world lookup.
 static EAC_RoutineDefinition Get(int index)
 {
  Build();
  if (index < 0) return null;
  int authored = s_Entries.Count();
  if (index < authored) return s_Entries[index];
  int tail = index - authored;
  if (tail >= s_Optional.Count()) return null;
  return s_Optional[tail];
 }

 // Current time bucket. Falls back to DAY when no manager is available, so a
 // missing time source never suspends routines.
 static int TimeBucket()
 {
  ChimeraWorld world = ChimeraWorld.CastFrom(GetGame().GetWorld());
  if (!world) return DAY;
  TimeAndWeatherManagerEntity manager = world.GetTimeAndWeatherManager();
  if (!manager) return DAY;
  float hour = manager.GetTimeOfTheDay();
  if (hour >= 5 && hour < 8) return DAWN;
  if (hour >= 8 && hour < 18) return DAY;
  if (hour >= 18 && hour < 21) return DUSK;
  return NIGHT;
 }

 static int RoleOf(EAC_ResidentRecord resident)
 {
  if (!resident) return ROLE_IDLER;
  return resident.Id % 4;
 }

 static string RoleName(int role)
 {
  if (role == ROLE_HOUSEHOLDER) return "Householder";
  if (role == ROLE_NEIGHBOUR) return "Neighbour";
  if (role == ROLE_WORKER) return "Worker";
  return "Resident";
 }

 static bool Matches(EAC_RoutineDefinition entry, int role, int bucket)
 {
  if (!entry) return false;
  if (entry.Role != -1 && entry.Role != role) return false;
  return (entry.TimeMask & bucket) != 0;
 }

 // The slots this resident may use right now, in catalog order. Relaxes to any
 // time-eligible slot, then to the first slot, so the set is never empty.
 protected static void CollectEligible(EAC_ResidentRecord resident, out array<EAC_RoutineDefinition> eligible)
 {
  eligible = {};
  int role = RoleOf(resident);
  int bucket = TimeBucket();
  // Count()/Get() rather than s_Entries, so a runtime-detected slot is reachable
  // by every role. On a vanilla server the two are the same list.
  int count = Count();
  for (int i = 0; i < count; i++)
  {
   EAC_RoutineDefinition entry = Get(i);
   if (Matches(entry, role, bucket)) eligible.Insert(entry);
  }
  if (!eligible.IsEmpty()) return;
  for (int j = 0; j < count; j++)
  {
   EAC_RoutineDefinition relaxed = Get(j);
   if (relaxed && (relaxed.TimeMask & bucket) != 0) eligible.Insert(relaxed);
  }
  if (eligible.IsEmpty()) eligible.Insert(s_Entries[0]);
 }

 // Index into the resident's OWN eligible set rather than scanning the catalog
 // forward to the first match. Each role's slots are a contiguous block, so a
 // forward scan landed every resident of a role on that block's first slot no
 // matter where it started - which is why only three anchor kinds were ever
 // chosen across five campaigns, and why seeding the start alone changed nothing.
 // Deterministic per resident, so cache/wake never rerolls the routine.
 // The table routine this resident's role and time of day permit, if any.
 // Used only when a housemate is already seated, never in ordinary selection,
 // so the catalog's distribution is unchanged for everyone else.
 static EAC_RoutineDefinition SelectTable(EAC_ResidentRecord resident)
 {
  EnsureSeeded(resident);
  if (s_Entries.IsEmpty()) return null;
  array<EAC_RoutineDefinition> eligible;
  CollectEligible(resident, eligible);
  foreach (EAC_RoutineDefinition entry : eligible)
   if (entry.NeedsTable) return entry;
  return null;
 }

 static EAC_RoutineDefinition Select(EAC_ResidentRecord resident)
 {
  EnsureSeeded(resident);
  if (s_Entries.IsEmpty()) return null;
  array<EAC_RoutineDefinition> eligible;
  CollectEligible(resident, eligible);
  // Table routines are one slot in roughly fifteen per role. At that rate a
  // twelve-minute window often exercises the seating path zero times, and the
  // sample was the noise, not the mechanism. The bias applies only when the
  // eligible set holds a table slot, so it never invents a routine.
  // Deterministic in (identity, step), never random: the same resident at the
  // same step always resolves the same slot, which the routine fixture asserts
  // and which keeps a day reproducible. int arithmetic only - no float modulo.
  EAC_AmbientModule module = EAC_AmbientModule.GetActive();
  int bias;
  if (module) bias = module.TableBias;
  int roll = 100;
  if (resident) roll = (resident.Id * 37 + resident.ActivityStep * 11) % 100;
  if (bias > 0 && roll < bias)
  {
   foreach (EAC_RoutineDefinition table : eligible)
    if (table.NeedsTable) return table;
  }
  int step;
  if (resident) step = resident.ActivityStep;
  int index = step % eligible.Count();
  if (index < 0) index = 0;
  return eligible[index];
 }

 // Advance one place through the resident's eligible set, retained on the
 // mission resident.
 static void Advance(EAC_ResidentRecord resident)
 {
  EnsureSeeded(resident);
  if (!resident || s_Entries.IsEmpty()) return;
  int total = Count();
  if (total < 1) return;
  resident.ActivityStep = (resident.ActivityStep + 1) % total;
 }

 // The validated runtime animation this slot carries, or null. Every vanilla
 // slot returns null, so a caller that ignores this is exactly as correct as it
 // was before the probe existed.
 static EAC_CustomAnim Custom(EAC_RoutineDefinition entry)
 {
  if (!entry) return null;
  if (entry.Anim != EAC_ERoutineAnim.CUSTOM_ANIM) return null;
  return entry.Custom;
 }

 // The vanilla payload for a runtime-detected slot, or null. Built from values
 // the probe already validated; null here means the caller must fall back.
 static SCR_LoiterCustomAnimData CustomPayload(EAC_RoutineDefinition entry)
 {
  EAC_CustomAnim anim = Custom(entry);
  if (!anim) return null;
  return EAC_AceAnimations.Payload(anim);
 }

 static ELoiteringType Animation(EAC_RoutineDefinition entry)
 {
  if (!entry) return ELoiteringType.LOITERING;
  // Only a slot that actually carries a validated animation may ask for CUSTOM.
  // A CUSTOM_ANIM slot whose data went away degrades to plain loitering rather
  // than asking the engine for a command nothing is bound to.
  if (entry.Anim == EAC_ERoutineAnim.CUSTOM_ANIM)
  {
   EAC_CustomAnim carried = entry.Custom;
   if (carried && carried.CommandId >= 0) return ELoiteringType.CUSTOM;
   return ELoiteringType.LOITERING;
  }
  if (entry.Anim == EAC_ERoutineAnim.SMOKE) return ELoiteringType.SMOKING;
  if (entry.Anim == EAC_ERoutineAnim.SIT_GROUND) return ELoiteringType.SIT;
  if (entry.Anim == EAC_ERoutineAnim.LEAN_LEFT) return ELoiteringType.LEAN_LEFT;
  if (entry.Anim == EAC_ERoutineAnim.LEAN_RIGHT) return ELoiteringType.LEAN_RIGHT;
  return ELoiteringType.LOITERING;
 }

 static ResourceName Point(EAC_RoutineDefinition entry)
 {
  if (!entry) return "{CA1A000000000060}Prefabs/EXPAC/EAC_LoiterPoint.et";
  if (entry.Anim == EAC_ERoutineAnim.SMOKE) return EAC_CivilianActivity.SMOKE_POINT;
  if (entry.Anim == EAC_ERoutineAnim.SIT_GROUND) return "{CA1A000000000050}Prefabs/EXPAC/EAC_RestPoint.et";
  if (entry.Anim == EAC_ERoutineAnim.LEAN_LEFT) return "{CA1A000000000090}Prefabs/EXPAC/EAC_LeanLeftPoint.et";
  if (entry.Anim == EAC_ERoutineAnim.LEAN_RIGHT) return "{CA1A0000000000A0}Prefabs/EXPAC/EAC_LeanRightPoint.et";
  return "{CA1A000000000060}Prefabs/EXPAC/EAC_LoiterPoint.et";
 }

 static float Dwell(EAC_RoutineDefinition entry)
 {
  if (!entry) return Math.RandomFloat(90, 150);
  return Math.RandomFloat(entry.MinDwell, entry.MaxDwell);
 }

 // Audit S14. A value in [low, high] derived from retained identity rather than
 // rolled. `salt` separates independent draws that share (Id, ActivityStep, leg).
 // Integer arithmetic throughout: '%' is invalid in a float expression in
 // Enforce, and the retained Id may be large, so the mix is folded positive
 // first (Math.AbsInt is not in the offline API dump) - the same shape as
 // EAC_ActivityProfiles.RoutineStops and the EAC_SceneItinerary linger roll.
 static float Spread(EAC_ResidentRecord resident, int leg, int salt, float low, float high)
 {
  if (high < low) high = low;
  if (!resident) return (low + high) * 0.5;
  int mix = resident.Id * 29 + resident.ActivityStep * 11 + leg * 5 + salt * 97;
  if (mix < 0) mix = -mix;
  int step = mix % 101;
  return low + (high - low) * step * 0.01;
 }

 // The catalog slot's authored band, drawn deterministically for this resident,
 // step and leg. Same band as Dwell, without the roll.
 static float DwellFor(EAC_RoutineDefinition entry, EAC_ResidentRecord resident, int leg)
 {
  if (!entry) return Spread(resident, leg, 1, 90, 150);
  return Spread(resident, leg, 1, entry.MinDwell, entry.MaxDwell);
 }

 // The fallback slot when a chosen animation is unsupported by this character.
 // Authored slots only: the fallback must never itself be a detected slot, or a
 // character that cannot play the optional animation has nowhere to degrade to.
 static EAC_RoutineDefinition Fallback()
 {
  Build();
  int count = s_Entries.Count();
  for (int i = 0; i < count; i++)
  {
   EAC_RoutineDefinition entry = s_Entries[i];
   if (!entry.NeedsTable && entry.Anim == EAC_ERoutineAnim.IDLE && entry.Anchor == EAC_EAnchorKind.YARD_OPEN) return entry;
  }
  return Get(0);
 }
}

// Line of sight for one protester to one player. The base answers from data so a
// fixture can inject which candidates are visible; the zone uses EAU_WorldSight.
class EAU_Sight
{
 // index: candidate in the list handed to EAU_Facing.Pick; from/to: eye positions.
 bool Sees(int index, vector from, vector to)
 {
  return true;
 }
}

// One world trace with the vanilla AI visibility settings (fire geometry, terrain,
// objects and the sea surface, first contact ends it). Bodies never block: neither
// neighbours in the crowd nor the watcher and the player themselves.
class EAU_WorldSight : EAU_Sight
{
 protected BaseWorld m_World;
 protected ref TraceParam m_Trace;

 void EAU_WorldSight(BaseWorld world)
 {
  m_World = world;
  m_Trace = new TraceParam();
  m_Trace.TargetLayers = EPhysicsLayerDefs.FireGeometry;
  m_Trace.Flags = TraceFlags.ENTS | TraceFlags.OCEAN | TraceFlags.WORLD | TraceFlags.ANY_CONTACT;
 }

 override bool Sees(int index, vector from, vector to)
 {
  if (!m_World) return false;
  m_Trace.Start = from;
  m_Trace.End = to;
  m_Trace.TraceEnt = null;
  return m_World.TraceMove(m_Trace, IgnoreBodies) >= 1;
 }

 protected bool IgnoreBodies(notnull IEntity entity)
 {
  IEntity walk = entity;
  while (walk)
  {
   if (ChimeraCharacter.Cast(walk)) return false;
   walk = walk.GetParent();
  }
  return true;
 }
}

// Which player a protester turns to. Server only; the turn itself is a vanilla
// character heading request, simulated by the server and replicated natively.
class EAU_Facing
{
 // Players farther than this from a protester's eyes are never looked at (m).
 static const float RANGE = 40;
 // Sight traces per pick; a crowd near many players stays cheap.
 static const int MAX_SIGHT_CHECKS = 3;

 // Candidates beyond the range are skipped; the rest are tried in random order and
 // the first one in sight wins, so every visible player is equally likely.
 // Returns the candidate index, or -1 when none in range is seen.
 static int Pick(vector eye, notnull array<vector> eyes, float range, notnull EAU_Sight sight)
 {
  array<int> order = {};
  float reach = range * range;
  foreach (int index, vector other : eyes)
  {
   if (vector.DistanceSq(eye, other) <= reach) order.Insert(index);
  }
  int checks;
  while (!order.IsEmpty() && checks < MAX_SIGHT_CHECKS)
  {
   int slot = Math.RandomInt(0, order.Count());
   int candidate = order[slot];
   order.Remove(slot);
   checks++;
   if (sight.Sees(candidate, eye, eyes[candidate])) return candidate;
  }
  return -1;
 }

 // Entity yaw (GetYawPitchRoll convention, degrees 0-360) that faces from -> to.
 static float YawTo(vector from, vector to)
 {
  vector direction = to - from;
  direction[1] = 0;
  if (direction.LengthSq() < 0.0001) return -1;
  vector basis[4];
  Math3D.DirectionAndUpMatrix(direction.Normalized(), "0 1 0", basis);
  vector rotation[3];
  rotation[0] = basis[0];
  rotation[1] = basis[1];
  rotation[2] = basis[2];
  vector angles = Math3D.MatrixToAngles(rotation);
  return Math.Repeat(angles[0], 360);
 }

 // Smallest difference between two yaws (degrees, 0-180).
 static float Gap(float a, float b)
 {
  float difference = Math.Repeat(a - b, 360);
  if (difference > 180) difference = 360 - difference;
  return difference;
 }

 static vector Eye(IEntity entity)
 {
  ChimeraCharacter character = ChimeraCharacter.Cast(entity);
  if (character) return character.EyePosition();
  return entity.GetOrigin() + "0 1.6 0";
 }
}

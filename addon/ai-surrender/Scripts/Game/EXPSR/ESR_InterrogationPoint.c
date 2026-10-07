// Runtime interaction point for one prisoner, spawned by the server and replicated to
// every client (JIP included). Characters cannot gain user actions at runtime and the
// shared Character_Base override belongs to another module, so the "Interrogate" action
// lives here. Players find actions only on entities their interaction cast hits; a small
// sphere on the Interaction physics layer (no mesh, no movement or bullet collision) is
// that target. It sits just in front of the prisoner's face and follows his head bone on
// every machine (seated, standing in ACE's surrender pose, moved or carried): looking at
// his face selects Interrogate, while the medical contexts on his torso and limbs
// (vanilla, ACE Medical) stay clear of it. Every machine follows in its frame tick: ten
// times a second within 20 m of the local player, once a second farther away. A dedicated
// server has no local player, so it follows once a second; the manager's upkeep also moves it.
[EntityEditorProps(category: "EXPBG/AI Surrender", description: "Runtime interrogation point of a surrendered soldier; spawned by the server")]
class ESR_InterrogationPointClass : GenericEntityClass {}

class ESR_InterrogationPoint : GenericEntity
{
 static const float COLLIDER_RADIUS = 0.15;
 static const string CONTEXT_NAME = "face";
 static const string HEAD_BONE = "Head";
 // Head bone frame as vanilla's AI eyes use it (Character_Base PerceptionComponent:
 // Head + <-0.03 0.07 -0.09>, yawed 180): +Y runs up through the head, -Z out of the
 // face. The point sits at eye height, a few centimetres in front of the nose.
 static const float FACE_UP = 0.07;
 static const float FACE_FORWARD = 0.16;
 static const float FOLLOW_INTERVAL = 0.1;
 // Farther than this from the local player the point follows once a second: the
 // interaction cast reaches 3 m, and closing 20 m to 3 m takes a sprinting player over 2 s.
 static const float NEAR_DISTANCE = 20;
 static const float FAR_FOLLOW_INTERVAL = 1;
 static const float FOLLOW_TOLERANCE = 0.02;
 // A bone reading farther than this from the prisoner's origin is not trusted.
 static const float MAX_FACE_DISTANCE = 2.5;
 // Server: a move this long is reported to the replication scheduler.
 static const float STREAM_STEP = 1;
 static const string TITLE = "EXPBG Interrogation";

 [RplProp()] protected RplId m_PrisonerId;
 [RplProp()] protected string m_sNameFormat;
 [RplProp()] protected string m_sName;
 [RplProp()] protected string m_sAlias;
 [RplProp()] protected string m_sSurname;
 [RplProp()] protected string m_sBio;
 [RplProp()] protected string m_sOrigin;
 [RplProp()] protected int m_iAge = -1;
 [RplProp()] protected bool m_bLeader;
 [RplProp()] protected string m_sLeaderFormat;
 [RplProp()] protected string m_sLeaderName;
 [RplProp()] protected string m_sLeaderAlias;
 [RplProp()] protected string m_sLeaderSurname;

 protected bool m_bCollider;
 protected float m_fFollowIn;
 protected vector m_vStreamAnchor;
 protected bool m_bAnchored;
 // Follow's head bone, looked up once per prisoner entity: a bone index is fixed for his
 // skeleton, and a prisoner streamed in again is a new entity that looks it up anew.
 protected SCR_ChimeraCharacter m_HeadOwner; // weak
 protected TNodeId m_iHeadBone;

 void ESR_InterrogationPoint(IEntitySource src, IEntity parent)
 {
  m_PrisonerId = RplId.Invalid();
  SetEventMask(EntityEvent.INIT | EntityEvent.FRAME);
 }

 override void EOnInit(IEntity owner)
 {
  super.EOnInit(owner);
  if (!GetGame().InPlayMode())
  {
   ClearEventMask(EntityEvent.FRAME);
   return;
  }
  // Dedicated server: no local player, so FollowInterval keeps the point within one second
  // of his face (a sit-down or GM move never leaves the server copy stale for long);
  // ESR_SurrenderManager.Upkeep also moves it.
  if (m_bCollider) return;
  autoptr PhysicsGeomDef geoms[] = {PhysicsGeomDef("", PhysicsGeom.CreateSphere(COLLIDER_RADIUS), "material/default", EPhysicsLayerDefs.Interaction)};
  m_bCollider = Physics.CreateStaticEx(this, geoms) != null;
 }

 override void EOnFrame(IEntity owner, float timeSlice)
 {
  m_fFollowIn -= timeSlice;
  if (m_fFollowIn > 0) return;
  m_fFollowIn = FollowInterval();
  Follow();
 }

 // Clients and a listen-server host: ten times a second while the local player's
 // controlled entity is within NEAR_DISTANCE of the point, once a second otherwise.
 protected float FollowInterval()
 {
  IEntity viewer = SCR_PlayerController.GetLocalControlledEntity();
  if (viewer && vector.DistanceSq(viewer.GetOrigin(), GetOrigin()) < NEAR_DISTANCE * NEAR_DISTANCE)
   return FOLLOW_INTERVAL;
  return FAR_FOLLOW_INTERVAL;
 }

 // The prisoner's face from his animated head bone: eye height, just in front of the
 // nose, following head tilt and turn in every pose. False without a usable bone.
 static bool FacePosition(IEntity character, out vector face)
 {
  if (!character) return false;
  Animation animation = character.GetAnimation();
  if (!animation) return false;
  vector candidate;
  if (!FaceFromBone(character, animation, animation.GetBoneIndex(HEAD_BONE), candidate))
   return false;
  face = candidate;
  return true;
 }

 // FacePosition with the head bone already looked up.
 protected static bool FaceFromBone(IEntity character, Animation animation, TNodeId bone, out vector face)
 {
  if (bone < 0)
   return false;
  vector head[4];
  if (!animation.GetBoneMatrix(bone, head))
   return false;
  vector world[4];
  character.GetWorldTransform(world);
  Math3D.MatrixMultiply4(world, head, head);
  vector candidate = head[3] + head[1] * FACE_UP - head[2] * FACE_FORWARD;
  // A NaN reading fails this comparison too.
  float distanceSq = vector.DistanceSq(candidate, character.GetOrigin());
  if (!(distanceSq <= MAX_FACE_DISTANCE * MAX_FACE_DISTANCE))
   return false;
  face = candidate;
  return true;
 }

 // FacePosition(GetPrisoner()) for Follow, with the head bone kept per prisoner entity.
 protected bool PrisonerFace(out vector face)
 {
  SCR_ChimeraCharacter prisoner = GetPrisoner();
  if (!prisoner)
   return false;
  Animation animation = prisoner.GetAnimation();
  if (!animation)
   return false;
  if (prisoner != m_HeadOwner)
  {
   TNodeId bone = animation.GetBoneIndex(HEAD_BONE);
   // Only a bone that was found is kept; a missing one is looked up again next time.
   if (bone < 0)
    return false;
   m_iHeadBone = bone;
   m_HeadOwner = prisoner;
  }
  vector candidate;
  if (!FaceFromBone(prisoner, animation, m_iHeadBone, candidate))
   return false;
  face = candidate;
  return true;
 }

 // Keep the point on the prisoner's face while he sits down, stands, is moved by a Game
 // Master or carried: clients from EOnFrame, a dedicated server from the manager's
 // upkeep. False while his head cannot be read (not streamed in here); the point then
 // stays where it was.
 bool Follow()
 {
  vector face;
  if (!PrisonerFace(face))
   return false;
  vector previous = GetOrigin();
  if (vector.DistanceSq(previous, face) <= FOLLOW_TOLERANCE * FOLLOW_TOLERANCE) return true;
  vector transform[4];
  Math3D.MatrixIdentity4(transform);
  transform[3] = face;
  SetWorldTransform(transform);
  // Commits the move to the sphere collider before the next interaction cast.
  Update();
  if (Replication.IsServer()) Restream(previous);
  return true;
 }

 // Server: the point has no networked movement, so a long move (carried, moved by a Game
 // Master) is reported to the replication scheduler for clients near the new spot.
 protected void Restream(vector previous)
 {
  if (!m_bAnchored)
  {
   m_vStreamAnchor = previous;
   m_bAnchored = true;
  }
  if (vector.DistanceSq(GetOrigin(), m_vStreamAnchor) < STREAM_STEP * STREAM_STEP) return;
  RplComponent rpl = RplComponent.Cast(FindComponent(RplComponent));
  if (rpl) rpl.ForceNodeMovement(m_vStreamAnchor);
  m_vStreamAnchor = GetOrigin();
 }

 // Server only, right after spawn.
 void Setup(RplId prisonerId, ESR_Dossier dossier)
 {
  if (!Replication.IsServer()) return;
  m_PrisonerId = prisonerId;
  if (dossier)
  {
   m_sNameFormat = dossier.NameFormat; m_sName = dossier.Name; m_sAlias = dossier.Alias; m_sSurname = dossier.Surname;
   m_sBio = dossier.Bio; m_sOrigin = dossier.Origin; m_iAge = dossier.Age; m_bLeader = dossier.Leader;
   m_sLeaderFormat = dossier.LeaderFormat; m_sLeaderName = dossier.LeaderName; m_sLeaderAlias = dossier.LeaderAlias; m_sLeaderSurname = dossier.LeaderSurname;
  }
  Replication.BumpMe();
 }

 SCR_ChimeraCharacter GetPrisoner()
 {
  if (!m_PrisonerId.IsValid()) return null;
  RplComponent rpl = RplComponent.Cast(Replication.FindItem(m_PrisonerId));
  if (!rpl) return null;
  return SCR_ChimeraCharacter.Cast(rpl.GetEntity());
 }

 // Any machine: the prisoner is present, alive and awake.
 bool CanInterrogate()
 {
  SCR_ChimeraCharacter prisoner = GetPrisoner();
  if (!prisoner) return false;
  CharacterControllerComponent controller = prisoner.GetCharacterController();
  return controller && controller.GetLifeState() == ECharacterLifeState.ALIVE && !controller.IsUnconscious();
 }

 // Server: deliver the answer to the interrogating player only. Transient by design. The
 // intel items he pointed out travel as two parallel lists (25 m distance, compass sector).
 void SendResult(int playerId, int outcome, int count, int distance, int bearing, int attemptsLeft, notnull array<int> intelDistances, notnull array<int> intelBearings)
 {
  if (!Replication.IsServer() || playerId <= 0) return;
  Rpc(RpcDo_Result, playerId, outcome, count, distance, bearing, attemptsLeft, intelDistances, intelBearings);
  // A listen-server host does not receive its own broadcast.
  RpcDo_Result(playerId, outcome, count, distance, bearing, attemptsLeft, intelDistances, intelBearings);
 }

 [RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
 protected void RpcDo_Result(int playerId, int outcome, int count, int distance, int bearing, int attemptsLeft, array<int> intelDistances, array<int> intelBearings)
 {
  if (System.IsConsoleApp() || SCR_PlayerController.GetLocalPlayerId() != playerId) return;
  ESR_ResultDialog.Open(TITLE, Describe(outcome, count, distance, bearing, attemptsLeft, intelDistances, intelBearings));
 }

 protected static string Localize(string value)
 {
  if (value.IsEmpty()) return value;
  return WidgetManager.Translate(value);
 }

 protected static string FullName(string format, string name, string alias, string surname)
 {
  string first = Localize(name);
  string nick = Localize(alias);
  string last = Localize(surname);
  if (!format.IsEmpty()) return WidgetManager.Translate(format, first, nick, last);
  if (first.IsEmpty()) return last;
  if (last.IsEmpty()) return first;
  return first + " " + last;
 }

 protected static string Compass(int bearing)
 {
  if (bearing == 0) return "north";
  if (bearing == 1) return "north-east";
  if (bearing == 2) return "east";
  if (bearing == 3) return "south-east";
  if (bearing == 4) return "south";
  if (bearing == 5) return "south-west";
  if (bearing == 6) return "west";
  return "north-west";
 }

 protected string IdentityText()
 {
  string text;
  string name = FullName(m_sNameFormat, m_sName, m_sAlias, m_sSurname);
  if (name.IsEmpty()) name = "He will not give his name.";
  else name = "Name: " + name;
  text = name;
  if (m_iAge > 0) text += string.Format("\nAge: %1", m_iAge);
  string origin = Localize(m_sOrigin);
  if (!origin.IsEmpty()) text += "\nFrom: " + origin;
  string bio = Localize(m_sBio);
  if (!bio.IsEmpty()) text += "\n\n" + bio;
  text += "\n\n";
  if (m_bLeader) text += "He was leading the squad himself.";
  else
  {
   string leader = FullName(m_sLeaderFormat, m_sLeaderName, m_sLeaderAlias, m_sLeaderSurname);
   if (leader.IsEmpty()) text += "He will not name his squad leader.";
   else text += "Squad leader: " + leader;
  }
  return text;
 }

 // One intel place as he gives it: "about 75 m north-east", or "a few metres north" when it
 // rounds to 0 m.
 protected static string IntelPlace(int distance, int bearing)
 {
  if (distance <= 0) return "a few metres " + Compass(bearing);
  return string.Format("about %1 m %2", distance, Compass(bearing));
 }

 // "He also points out 2 intel items: about 75 m north-east, about 150 m south." Empty
 // without intel items.
 static string IntelText(array<int> intelDistances, array<int> intelBearings)
 {
  if (!intelDistances || !intelBearings) return string.Empty;
  int count = intelDistances.Count();
  if (intelBearings.Count() < count) count = intelBearings.Count();
  if (count <= 0) return string.Empty;
  string places;
  for (int i = 0; i < count; i++)
  {
   if (i > 0) places += ", ";
   places += IntelPlace(intelDistances[i], intelBearings[i]);
  }
  if (count == 1) return "He also points out an intel item: " + places + ". It is marked on your map.";
  return string.Format("He also points out %1 intel items: %2. Each is marked on your map.", count, places);
 }

 string Describe(int outcome, int count, int distance, int bearing, int attemptsLeft, array<int> intelDistances = null, array<int> intelBearings = null)
 {
  // Only an answer carries intel items (the server rolls them with his first answer).
  string intel = IntelText(intelDistances, intelBearings);
  if (!intel.IsEmpty()) intel = "\n\n" + intel;
  if (outcome == ESR_SurrenderManager.OUTCOME_REVEAL)
   return string.Format("He gives up his comrades: a squad of %1 soldiers about %2 m %3 of here.\n\nThe position is marked on your map. Delete the marker from the map once it is stale.", count, distance, Compass(bearing)) + intel;
  if (outcome == ESR_SurrenderManager.OUTCOME_IDENTITY) return IdentityText() + intel;
  if (outcome == ESR_SurrenderManager.OUTCOME_NO_SQUAD) return "He insists there is nobody else out here.\n\n" + IdentityText() + intel;
  if (outcome == ESR_SurrenderManager.OUTCOME_SILENT) return "He stares at the ground. He has nothing more to say.";
  if (attemptsLeft <= 0) return "He refuses to talk. He will not say anything more.";
  return string.Format("He refuses to talk. (%1 more attempts)", attemptsLeft);
 }
}

// "Interrogate" on a prisoner. The server decides and answers the asking player alone.
class ESR_InterrogateAction : ScriptedUserAction
{
 static const float RANGE = 3;

 override bool HasLocalEffectOnlyScript() { return false; }
 override bool CanBroadcastScript() { return false; }

 override bool CanBeShownScript(IEntity user)
 {
  ESR_InterrogationPoint point = ESR_InterrogationPoint.Cast(GetOwner());
  return user && point && point.CanInterrogate();
 }

 override bool CanBePerformedScript(IEntity user)
 {
  ESR_InterrogationPoint point = ESR_InterrogationPoint.Cast(GetOwner());
  if (!user || !point)
  {
   SetCannotPerformReason("Too far");
   return false;
  }
  // Feet to feet, as the server measures: the point is at his face, head height above.
  IEntity anchor = point.GetPrisoner();
  if (!anchor) anchor = point;
  if (vector.DistanceSq(user.GetOrigin(), anchor.GetOrigin()) > RANGE * RANGE)
  {
   SetCannotPerformReason("Too far");
   return false;
  }
  return true;
 }

 override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
 {
  // The performing client has no local effect; the answer arrives from the server.
  if (!Replication.IsServer()) return;
  ESR_InterrogationPoint point = ESR_InterrogationPoint.Cast(pOwnerEntity);
  if (!point || !pUserEntity) return;
  int playerId = GetGame().GetPlayerManager().GetPlayerIdFromControlledEntity(pUserEntity);
  if (playerId <= 0) return;
  ESR_SurrenderManager.Interrogate(point, pUserEntity, playerId);
 }
}

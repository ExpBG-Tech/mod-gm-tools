// Runtime interaction point for one prisoner, spawned by the server at his chest and
// replicated to every client (JIP included). Characters cannot gain user actions at
// runtime and the shared Character_Base override belongs to another module, so the
// "Interrogate" action lives here. Players find actions only on entities their
// interaction cast hits; a small sphere on the Interaction physics layer (no mesh,
// no movement or bullet collision) is that target.
[EntityEditorProps(category: "EXPBG/AI Surrender", description: "Runtime interrogation point of a surrendered soldier; spawned by the server")]
class ESR_InterrogationPointClass : GenericEntityClass {}

class ESR_InterrogationPoint : GenericEntity
{
 static const float COLLIDER_RADIUS = 0.45;
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

 void ESR_InterrogationPoint(IEntitySource src, IEntity parent)
 {
  m_PrisonerId = RplId.Invalid();
  SetEventMask(EntityEvent.INIT);
 }

 override void EOnInit(IEntity owner)
 {
  super.EOnInit(owner);
  if (!GetGame().InPlayMode() || m_bCollider) return;
  autoptr PhysicsGeomDef geoms[] = {PhysicsGeomDef("", PhysicsGeom.CreateSphere(COLLIDER_RADIUS), "material/default", EPhysicsLayerDefs.Interaction)};
  m_bCollider = Physics.CreateStaticEx(this, geoms) != null;
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

 // Server: deliver the answer to the interrogating player only. Transient by design.
 void SendResult(int playerId, int outcome, int count, int distance, int bearing, int attemptsLeft)
 {
  if (!Replication.IsServer() || playerId <= 0) return;
  Rpc(RpcDo_Result, playerId, outcome, count, distance, bearing, attemptsLeft);
  // A listen-server host does not receive its own broadcast.
  RpcDo_Result(playerId, outcome, count, distance, bearing, attemptsLeft);
 }

 [RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
 protected void RpcDo_Result(int playerId, int outcome, int count, int distance, int bearing, int attemptsLeft)
 {
  if (System.IsConsoleApp() || SCR_PlayerController.GetLocalPlayerId() != playerId) return;
  ESR_ResultDialog.Open(TITLE, Describe(outcome, count, distance, bearing, attemptsLeft));
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

 string Describe(int outcome, int count, int distance, int bearing, int attemptsLeft)
 {
  if (outcome == ESR_SurrenderManager.OUTCOME_REVEAL)
   return string.Format("He gives up his comrades: a squad of %1 soldiers about %2 m %3 of here.\n\nThe position is marked on your map. Delete the marker from the map once it is stale.", count, distance, Compass(bearing));
  if (outcome == ESR_SurrenderManager.OUTCOME_IDENTITY) return IdentityText();
  if (outcome == ESR_SurrenderManager.OUTCOME_NO_SQUAD) return "He insists there is nobody else out here.\n\n" + IdentityText();
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
  if (!user || !point || vector.DistanceSq(user.GetOrigin(), point.GetOrigin()) > RANGE * RANGE)
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

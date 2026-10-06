// TEST ONLY. Unit Caching as a locally hosted or single-player Game Master uses it: one
// player character (the host's own, injected through EBG_CacheManager.UpdatePlayers) and
// GM-placed AI next to a Full cache zone with per-group activation and the module's
// default radii (affected 300 m, wake 700 m, sleep 900 m); clear delay 5 s.
// pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/EBG_LocalCacheGameplay.c -ExpectResult '\[EBG LOCAL CACHE RESULT\] checks=[1-9]\d* failures=0 reason=complete' -OrchestratorSlotGranted
// The runner copies this file to EXPG_GarrisonGameplay.c; the class names are fixed.
// Cases, in order:
//  1. Enrollment and its explanation: a free USSR squad enrolls; a squad marked Exclude
//     and a soldier who left his squad (AI Surrender's path: RemoveAgent, AI off,
//     EBG_MarkLeftSquad) stay out, and the zone status names both.
//  2. Host character 780 m away (outside wake, inside sleep): the squad never caches;
//     status and notice say a player character keeps it awake inside the 900 m radius.
//  3. Host character 1200 m away: the squad Full-caches (GameMasterSystems present, as in
//     a GM mission started from the main menu) and the awake note clears.
//  4. Host character back at the squad: it wakes with all six soldiers.
//  5. An empty Simulation zone says no AI group is inside its affected radius.
// The engine is the dedicated diagnostic server, not a listen host or single player;
// Unit Caching runs the same Replication.IsServer() authority path in all three. No GM
// UI, no hint rendering (the notice is logged as "[EBG ZONE NOTICE]"), no save/load.
class EXPG_GarrisonGameplayClass : GenericEntityClass {}
class EXPG_GarrisonGameplay : GenericEntity
{
 static const float FIXTURE_SECONDS = 260;
 static const float AWAKE_SECONDS = 20;
 static const float HOST_INSIDE_SLEEP = 780;
 static const float HOST_BEYOND_SLEEP = 1200;
 static const string SQUAD_PREFAB = "{E552DABF3636C2AD}Prefabs/Groups/OPFOR/Group_USSR_RifleSquad.et";
 static const string ZONE_PREFAB = "{7E1080ED8F0633FD}PrefabsEditable/EXPBG/EBG_CacheZone.et";
 static const string AWAKE_NOTE = "inside the 900 m sleep radius";
 static ref array<vector> s_Presence;
 SCR_AIGroup FreeSquad;
 SCR_AIGroup ExcludedSquad;
 SCR_ChimeraCharacter Prisoner;
 EBG_CacheZone FullZone;
 EBG_CacheZone EmptyZone;
 EBG_CacheGroup Record;
 int Phase;
 int Checks;
 int Failures;
 float Started;
 float Next;
 float PhaseAt;
 bool Finished;
 bool AwakeSeen;
 float AwakeDistance = -1;
 vector Origin = "4773.46 0 7094.57";

 void EXPG_GarrisonGameplay(IEntitySource src, IEntity parent) { SetEventMask(EntityEvent.INIT | EntityEvent.FRAME); }
 float Now() { return GetGame().GetWorld().GetWorldTime() * 0.001; }
 override void EOnInit(IEntity owner)
 {
  if (!Replication.IsServer()) { ClearEventMask(EntityEvent.FRAME); return; }
  s_Presence = {};
  Started = Now();
  Next = Started + 10;
  PrintFormat("[EBG LOCAL CACHE BEGIN] hostInside=%1 hostBeyond=%2 awakeSeconds=%3 connectedPlayers=0 presence=injected deadline=%4", HOST_INSIDE_SLEEP, HOST_BEYOND_SLEEP, AWAKE_SECONDS, FIXTURE_SECONDS);
 }
 bool Check(bool ok, string label)
 {
  Checks++;
  if (!ok) Failures++;
  PrintFormat("[EBG LOCAL CACHE CHECK] pass=%1 %2", ok, label);
  return ok;
 }
 void Finish(string reason)
 {
  if (Finished) return;
  Finished = true;
  if (s_Presence) s_Presence.Clear();
  ClearEventMask(EntityEvent.FRAME);
  PrintFormat("[EBG LOCAL CACHE RESULT] checks=%1 failures=%2 reason=%3", Checks, Failures, reason);
  GetGame().RequestClose();
 }
 vector Ground(vector p, float lift) { p[1] = GetGame().GetWorld().GetSurfaceY(p[0], p[2]) + lift; return p; }
 IEntity Spawn(string prefab, vector point)
 {
  EntitySpawnParams params = new EntitySpawnParams();
  params.TransformMode = ETransformMode.WORLD;
  Math3D.MatrixIdentity4(params.Transform);
  params.Transform[3] = Ground(point, 0.3);
  // Keep the Resource and the spawned entity in locals (inline temporaries returned null spawns).
  Resource resource = Resource.Load(prefab);
  IEntity spawned = GetGame().SpawnEntityPrefab(resource, GetGame().GetWorld(), params);
  return spawned;
 }
 vector AtOffset(float east, float north)
 {
  vector point = Origin;
  point[0] = point[0] + east;
  point[2] = point[2] + north;
  return point;
 }
 // The host's character east of the squad, at the squad's ground height so the
 // zone's height band never decides the case; only XZ distance does.
 void PlaceHost(float east)
 {
  vector point = AtOffset(east, 0);
  point[1] = GetGame().GetWorld().GetSurfaceY(Origin[0], Origin[2]) + 1.8;
  s_Presence.Clear();
  s_Presence.Insert(point);
 }
 // Module defaults (affected 300, wake 700, sleep 900), per-group activation,
 // 5 s clear delay, no cleanup or overlays, debug log on.
 EBG_CacheZone SpawnZone(vector point, int mode)
 {
  EBG_CacheZone zone = EBG_CacheZone.Cast(Spawn(ZONE_PREFAB, point));
  if (!zone) return null;
  zone.SetValue(1, mode); zone.SetValue(2, 1); zone.SetValue(3, 300);
  zone.SetValue(4, 700); zone.SetValue(5, 900); zone.SetValue(12, 5);
  zone.SetValue(15, 0); zone.SetValue(18, 0); zone.SetValue(21, 1);
  zone.SetValue(0, 1);
  return zone;
 }
 void Advance(int phase) { Phase = phase; PhaseAt = Now(); }
 bool Waited(float seconds, string label)
 {
  if (Now() - PhaseAt <= seconds) return false;
  Check(false, label);
  Finish("phase " + Phase.ToString());
  return true;
 }
 void LogZone(string stage, EBG_CacheZone zone)
 {
  if (!zone) return;
  PrintFormat("[EBG LOCAL CACHE STATUS] stage=%1 managed=%2 cached=%3 awake=%4 distance=%5 note='%6' status='%7'", stage, zone.ManagedCount, zone.CachedCount, zone.PlayerAwakeCount, zone.PlayerAwakeDistance, zone.EnrollmentNote, zone.Status);
  PrintFormat("[EBG LOCAL CACHE NOTICE] stage=%1 text='%2'", stage, EBG_CacheManager.Get().ZoneNoticeText(zone));
 }

 override void EOnFrame(IEntity owner, float timeSlice)
 {
  if (Finished || Now() < Next) return;
  Next = Now() + 0.25;
  if (Now() - Started > FIXTURE_SECONDS) { Check(false, "all phases finished before the fixture deadline"); Finish("timeout"); return; }
  Step();
 }

 void Step()
 {
  if (Phase == 0)
  {
   string fullReason;
   bool fullReady = EBG_FullSaveGate.Available(fullReason);
   PrintFormat("[EBG LOCAL CACHE RUNTIME] systems='%1' players=%2 zones=%3 rplMode=%4 server=%5 full=%6 reason='%7' standalone='%8'", GetGame().GetSystemsConfig(), GetGame().GetPlayerManager().GetPlayerCount(), EBG_CacheZone.Zones.Count(), RplSession.Mode(), Replication.IsServer(), fullReady, fullReason, EBG_CacheManager.StandaloneNote());
   if (!Check(GetGame().GetPlayerManager().GetPlayerCount() == 0 && EBG_CacheZone.Zones.IsEmpty(), "isolated server: no players, no zones")) { Finish("setup"); return; }
   Check(EBG_CacheManager.StandaloneNote().IsEmpty(), "no old standalone EXPBG mod loaded, so no standalone warning");
   FreeSquad = SCR_AIGroup.Cast(Spawn(SQUAD_PREFAB, AtOffset(12, 0)));
   ExcludedSquad = SCR_AIGroup.Cast(Spawn(SQUAD_PREFAB, AtOffset(-12, 14)));
   if (!Check(FreeSquad != null && ExcludedSquad != null, "two USSR rifle squads spawned next to the zone centre")) { Finish("setup"); return; }
   Advance(10); return;
  }
  if (Phase == 10)
  {
   if (!FreeSquad || !ExcludedSquad) { Check(false, "both squads survive their initial spawn"); Finish("setup"); return; }
   if (!FreeSquad.EBG_HasCompletedInitialSpawn() || FreeSquad.GetAgentsCount() != 6 || !ExcludedSquad.EBG_HasCompletedInitialSpawn() || ExcludedSquad.GetAgentsCount() != 6) { Waited(60, "both squads finished their initial spawn with six members"); return; }
   // AI Surrender's prisoner path on the excluded squad's last soldier.
   array<AIAgent> agents = {};
   ExcludedSquad.GetAgents(agents);
   AIAgent prisonerAgent = agents[agents.Count() - 1];
   Prisoner = SCR_ChimeraCharacter.Cast(prisonerAgent.GetControlledEntity());
   if (!Check(Prisoner != null, "prisoner chosen from the second squad")) { Finish("setup"); return; }
   ExcludedSquad.RemoveAgent(prisonerAgent);
   AIControlComponent control = AIControlComponent.Cast(Prisoner.FindComponent(AIControlComponent));
   if (control && control.IsAIActivated()) control.DeactivateAI();
   Prisoner.EBG_MarkLeftSquad();
   ExcludedSquad.EBG_Exclude = true;
   Check(Prisoner.EBG_HasLeftSquad() && !Prisoner.GetCharacterGroup() && ExcludedSquad.GetAgentsCount() == 5, "prisoner left his squad, AI off, marked as having left it");
   PlaceHost(HOST_INSIDE_SLEEP);
   FullZone = SpawnZone(Origin, 1);
   if (!Check(FullZone != null, "Full cache zone placed over both squads")) { Finish("setup"); return; }
   // The Game Master who saved the zone gets one notice after its next pass (logged).
   FullZone.EBG_RequestNotice(1);
   Advance(20); return;
  }
  if (!FullZone) { Check(false, "Full zone kept for the whole test"); Finish("zone"); return; }
  if (Phase == 20)
  {
   Record = EBG_CacheManager.Get().FindGroup(FreeSquad);
   if (!Record || Record.Members.Count() != 6 || FullZone.EnrollmentPasses < 1) { Waited(30, "free squad enrolled by the Full zone"); return; }
   LogZone("enrolled", FullZone);
   Check(EBG_CacheManager.Get().FindGroup(ExcludedSquad) == null, "excluded squad not enrolled");
   Check(FullZone.EnrollmentNote.Contains("1 group marked Exclude from EXPBG optimization"), "zone status names the excluded squad");
   Check(FullZone.EnrollmentNote.Contains("1 prisoners or soldiers who left their squad nearby are never cached"), "zone status counts the soldier who left his squad (AI off)");
   string gateReason;
   bool gateReady = EBG_FullSaveGate.Available(gateReason);
   Check(gateReady && gateReason.IsEmpty() && EBG_CacheManager.Get().FullUnavailable(FullZone).IsEmpty(), "Full cache available and no Full refusal in the status: GameMasterSystems systems config and the GM Tools addon identity, as in a GM mission started from the main menu; reason='" + gateReason + "'");
   Advance(21); return;
  }
  if (!Record) { Check(false, "logical cache record retained across the test"); Finish("record"); return; }
  // Host character inside the sleep radius: awake for well past the clear delay.
  if (Phase == 21)
  {
   if (Record.Full || Record.Simulation)
   {
    Check(false, "a host character 780 m away keeps the squad awake; reason='" + Record.Reason + "'");
    Finish("awake"); return;
   }
   if (FullZone.PlayerAwakeCount == 1 && FullZone.Status.Contains(AWAKE_NOTE))
   {
    AwakeSeen = true;
    AwakeDistance = FullZone.PlayerAwakeDistance;
   }
   if (Now() - PhaseAt < AWAKE_SECONDS) return;
   LogZone("host-inside-sleep", FullZone);
   Check(AwakeSeen, "zone status says a player character keeps one group awake inside the 900 m sleep radius");
   Check(AwakeDistance >= 700 && AwakeDistance < 900, "reported host distance lies between the wake and sleep radii: " + AwakeDistance.ToString());
   string awakeNotice = EBG_CacheManager.Get().ZoneNoticeText(FullZone);
   Check(awakeNotice.Contains("1 group enrolled, 0 cached.") && awakeNotice.Contains("a player character is") && awakeNotice.Contains(" m away,") && awakeNotice.Contains(AWAKE_NOTE), "notice explains why nothing caches, with the host distance");
   Check(!FullZone.Status.Contains(" m away"), "replicated zone status leaves out the host distance, so player movement does not re-replicate it");
   Check(FullZone.ManagedCount == 1 && FullZone.CachedCount == 0, "one group managed, none cached while the host is inside the sleep radius");
   PlaceHost(HOST_BEYOND_SLEEP);
   Advance(22); return;
  }
  // Host character beyond the sleep radius: production clear delay, then Full capture.
  if (Phase == 22)
  {
   // The zone counters follow on the next scheduler tick after the capture.
   if (!Record.Full || Record.Full.GetState() != EBG_FullGroupPhase.CACHED || FullZone.CachedCount != 1) { Waited(90, "squad Full cached and counted with the host beyond the sleep radius; reason='" + Record.Reason + "' recovery='" + Record.Recovery + "'"); return; }
   LogZone("host-beyond-sleep", FullZone);
   PrintFormat("[EBG LOCAL CACHE CACHED] latency=%1", Now() - PhaseAt);
   Check(FullZone.PlayerAwakeCount == 0 && !FullZone.Status.Contains(AWAKE_NOTE), "awake note cleared once the host left the sleep radius");
   string cachedNotice = EBG_CacheManager.Get().ZoneNoticeText(FullZone);
   Check(cachedNotice.Contains("1 group enrolled, 1 cached."), "notice reports the cached group");
   PlaceHost(0);
   Advance(23); return;
  }
  // Host character back at the squad: it wakes with every survivor.
  if (Phase == 23)
  {
   if (Record.Full || !Record.Group || Record.Group.GetAgentsCount() != 6 || Record.Recovery != "") { Waited(60, "squad woke from Full with six members; reason='" + Record.Reason + "' recovery='" + Record.Recovery + "'"); return; }
   PrintFormat("[EBG LOCAL CACHE WAKE] latency=%1", Now() - PhaseAt);
   Check(true, "squad restored with six members when the host returned");
   EmptyZone = SpawnZone(AtOffset(0, -1500), 0);
   if (!Check(EmptyZone != null, "Simulation zone placed where no AI stands")) { Finish("setup"); return; }
   Advance(24); return;
  }
  if (Phase == 24)
  {
   if (!EmptyZone) { Check(false, "empty zone kept"); Finish("empty"); return; }
   if (EmptyZone.EnrollmentPasses < 1) { Waited(30, "empty zone completed an enrollment pass"); return; }
   LogZone("empty-zone", EmptyZone);
   Check(EmptyZone.ManagedCount == 0 && EmptyZone.EnrollmentNote == "No AI group with a free living soldier inside the 300 m affected radius", "empty zone explains that no AI group is inside its affected radius");
   Check(EBG_CacheManager.Get().FullUnavailable(EmptyZone).IsEmpty(), "a Simulation zone never reports a Full refusal");
   Check(EBG_CacheManager.Get().ZoneNoticeText(EmptyZone).Contains("0 groups enrolled, 0 cached."), "empty zone notice reports zero groups");
   Finish("complete"); return;
  }
 }
}
// Presence seam, as in the wake budget, unit-cleanup and cache-hold fixtures.
modded class EBG_CacheManager
{
 override protected void UpdatePlayers()
 {
  super.UpdatePlayers();
  if (EXPG_GarrisonGameplay.s_Presence)
  {
   foreach (vector presence : EXPG_GarrisonGameplay.s_Presence) Players.Insert(presence);
  }
 }
}

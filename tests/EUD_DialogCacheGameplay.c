// TEST ONLY. EXPBG Unit Dialog across Unit Caching Full caching (two sleep/wake cycles).
// pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/EUD_DialogCacheGameplay.c -ExpectResult '\[EXPG DIALOG CACHE RESULT\] checks=[1-9]\d* failures=0 cycles=2 reason=complete' -OrchestratorSlotGranted
// The runner copies this file to EXPG_GarrisonGameplay.c; the class names are fixed.
// Real USSR rifle squad and Unit Caching zone prefab, production dialog writes, the
// production Full capture/respawn (EBG_PrefabFullCache with the EBG_SurvivorCarry hook
// that Unit Dialog extends) and the Character_Base "Speak to" action. Presence is
// injected through EBG_CacheManager.UpdatePlayers; the server has no players.
// Garrison Full uses the same EBG_PrefabFullCache capture/respawn but is not run here.
// No GM UI, no clients or JIP stream-in, no save/load (portable CDF Full snapshots do
// not carry dialog; native saves wait until every Full group is awake).
class EXPG_GarrisonGameplayClass : GenericEntityClass {}
class EXPG_GarrisonGameplay : GenericEntity
{
 static const float FIXTURE_SECONDS = 240;
 static const string SQUAD_PREFAB = "{E552DABF3636C2AD}Prefabs/Groups/OPFOR/Group_USSR_RifleSquad.et";
 static const string ZONE_PREFAB = "{7E1080ED8F0633FD}PrefabsEditable/EXPBG/EBG_CacheZone.et";
 static const string SPEAKER_NAME = "Sgt. Cache";
 static const string LINE_ONE = "Hold here.";
 static const string LINE_FIVE = "Fifth[br]slot";
 static const string LINE_TWO_EDIT = "Second cycle.";
 static const int GESTURE = 2;
 static const int CYCLES = 2;
 static ref array<vector> s_Presence;
 SCR_AIGroup Squad;
 EBG_CacheZone FullZone;
 EBG_CacheGroup Record;
 EBG_CacheMember SpeakerMember;
 EBG_CacheMember PlainMember;
 // Entities of the current cycle: the originals, then each respawn.
 SCR_ChimeraCharacter Speaker;
 SCR_ChimeraCharacter Plain;
 EntityID SpeakerId;
 EntityID PlainId;
 string ExpectedLineTwo;
 int Cycle;
 int Phase;
 int Checks;
 int Failures;
 float Started;
 float Next;
 float PhaseAt;
 float PresenceAt;
 bool Finished;
 vector Origin = "4773.46 0 7094.57";

 void EXPG_GarrisonGameplay(IEntitySource src, IEntity parent) { SetEventMask(EntityEvent.INIT | EntityEvent.FRAME); }
 float Now() { return GetGame().GetWorld().GetWorldTime() * 0.001; }
 override void EOnInit(IEntity owner)
 {
  if (!Replication.IsServer()) { ClearEventMask(EntityEvent.FRAME); return; }
  s_Presence = {};
  Started = Now();
  Next = Started + 10;
  PrintFormat("[EXPG DIALOG CACHE BEGIN] cycles=%1 connectedPlayers=0 presence=injected deadline=%2", CYCLES, FIXTURE_SECONDS);
 }
 bool Check(bool ok, string label)
 {
  Checks++;
  if (!ok) Failures++;
  PrintFormat("[EXPG DIALOG CACHE CHECK] pass=%1 cycle=%2 %3", ok, Cycle + 1, label);
  return ok;
 }
 void Finish(string reason)
 {
  if (Finished) return;
  Finished = true;
  if (s_Presence) s_Presence.Clear();
  ClearEventMask(EntityEvent.FRAME);
  PrintFormat("[EXPG DIALOG CACHE RESULT] checks=%1 failures=%2 cycles=%3 reason=%4", Checks, Failures, Cycle, reason);
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
 // Full mode (1), 60 m zone, 5 s sleep delay (MinimumActive follows it), debug messages on.
 EBG_CacheZone SpawnZone(vector point)
 {
  EBG_CacheZone zone = EBG_CacheZone.Cast(Spawn(ZONE_PREFAB, point));
  if (!zone) return null;
  zone.SetValue(1, 1); zone.SetValue(2, 0); zone.SetValue(3, 60);
  zone.SetValue(4, 100); zone.SetValue(5, 300); zone.SetValue(12, 5);
  zone.SetValue(15, 0); zone.SetValue(18, 0); zone.SetValue(21, 1);
  zone.SetValue(0, 1);
  return zone;
 }
 void Presence(vector point)
 {
  s_Presence.Insert(Ground(point, 1.8));
  PresenceAt = Now();
 }
 void Advance(int phase) { Phase = phase; PhaseAt = Now(); }
 bool Waited(float seconds, string label)
 {
  if (Now() - PhaseAt <= seconds) return false;
  Check(false, label);
  Finish("phase " + Phase.ToString());
  return true;
 }

 EUD_SpeakAction FindSpeakAction(IEntity owner)
 {
  if (!owner) return null;
  ActionsManagerComponent manager = ActionsManagerComponent.Cast(owner.FindComponent(ActionsManagerComponent));
  if (!manager) return null;
  array<BaseUserAction> actions = {};
  manager.GetActionsList(actions);
  foreach (BaseUserAction candidate : actions)
  {
   EUD_SpeakAction speak = EUD_SpeakAction.Cast(candidate);
   if (speak) return speak;
  }
  return null;
 }
 string Label(IEntity owner)
 {
  EUD_SpeakAction action = FindSpeakAction(owner);
  string label;
  if (!action || !action.GetActionNameScript(label)) return string.Empty;
  return label;
 }
 int ConfiguredCount()
 {
  array<SCR_EditableCharacterComponent> units = {};
  return EUD_Dialog.GetConfigured(units);
 }
 bool RegistryHoldsOnly(SCR_EditableCharacterComponent unit)
 {
  array<SCR_EditableCharacterComponent> units = {};
  EUD_Dialog.GetConfigured(units);
  return unit && units.Count() == 1 && units[0] == unit;
 }
 // The configured dialog of this cycle, slot by slot.
 bool UnitMatches(SCR_EditableCharacterComponent unit)
 {
  if (!unit || unit.EUD_GetName() != SPEAKER_NAME || unit.EUD_GetGesture() != GESTURE) return false;
  if (unit.EUD_GetLine(0) != LINE_ONE || unit.EUD_GetLine(1) != ExpectedLineTwo || unit.EUD_GetLine(4) != LINE_FIVE) return false;
  return unit.EUD_GetLine(2).IsEmpty() && unit.EUD_GetLine(3).IsEmpty() && unit.EUD_GetLine(9).IsEmpty();
 }
 bool RecordMatches(EUD_DialogRecord record)
 {
  if (!record || !record.Valid() || record.name != SPEAKER_NAME || record.gesture != GESTURE || record.lines.Count() != EUD_Dialog.MAX_LINES) return false;
  return record.lines[0] == LINE_ONE && record.lines[1] == ExpectedLineTwo && record.lines[4] == LINE_FIVE && record.lines[2].IsEmpty();
 }

 override void EOnFrame(IEntity owner, float timeSlice)
 {
  if (Finished || Now() < Next) return;
  Next = Now() + 0.25;
  if (Now() - Started > FIXTURE_SECONDS) { Check(false, "all cycles finished before the fixture deadline"); Finish("timeout"); return; }
  Step();
 }

 void Step()
 {
  if (Phase == 0)
  {
   PrintFormat("[EXPG DIALOG CACHE RUNTIME] systems='%1' players=%2 zones=%3", GetGame().GetSystemsConfig(), GetGame().GetPlayerManager().GetPlayerCount(), EBG_CacheZone.Zones.Count());
   if (!Check(GetGame().GetPlayerManager().GetPlayerCount() == 0 && EBG_CacheZone.Zones.IsEmpty() && ConfiguredCount() == 0, "isolated server: no players, no zones, no dialog units")) { Finish("setup"); return; }
   Squad = SCR_AIGroup.Cast(Spawn(SQUAD_PREFAB, Origin));
   if (!Check(Squad != null, "USSR rifle squad spawned")) { Finish("setup"); return; }
   Advance(10); return;
  }
  // Configure dialog while no zone exists, so the squad cannot be cached first.
  if (Phase == 10)
  {
   if (!Squad || !Squad.EBG_HasCompletedInitialSpawn() || Squad.GetAgentsCount() != 6) { Waited(60, "squad finished its initial spawn with six members"); return; }
   ConfigureDialog();
   return;
  }
  // Bind the logical members while the enrolled squad is still awake.
  if (Phase == 20)
  {
   if (!Squad) { Check(false, "squad enrolled before it was cached"); Finish("enroll"); return; }
   Record = EBG_CacheManager.Get().FindGroup(Squad);
   if (!Record || Record.Members.Count() != 6) { Waited(60, "squad enrolled by the Full zone"); return; }
   SpeakerMember = EBG_CacheManager.Get().FindMember(Speaker);
   PlainMember = EBG_CacheManager.Get().FindMember(Plain);
   if (!Check(SpeakerMember && PlainMember && Record.Members.Contains(SpeakerMember) && Record.Members.Contains(PlainMember), "speaker and plain member bound to the zone's logical members")) { Finish("enroll"); return; }
   Advance(21); return;
  }
  if (!Record) { Check(false, "logical cache record retained across the cycle"); Finish("record"); return; }
  if (Phase == 21)
  {
   if (!Record.Full || Record.Full.GetState() != EBG_FullGroupPhase.CACHED) { Waited(90, "squad Full cached; reason='" + Record.Reason + "' recovery='" + Record.Recovery + "'"); return; }
   CachedChecks();
   Advance(22); return;
  }
  if (Phase == 22)
  {
   // Stay cached for a few seconds before a player arrives.
   if (Now() - PhaseAt < 3) return;
   Presence(Origin);
   Advance(23); return;
  }
  if (Phase == 23)
  {
   if (Record.Full || !Record.Group || Record.Group.GetAgentsCount() != 6 || Record.Recovery != "") { Waited(60, "squad woke from Full with six members; reason='" + Record.Reason + "' recovery='" + Record.Recovery + "'"); return; }
   PrintFormat("[EXPG DIALOG CACHE WAKE] cycle=%1 latency=%2", Cycle + 1, Now() - PresenceAt);
   RespawnChecks();
   return;
  }
 }

 void ConfigureDialog()
 {
  array<AIAgent> agents = {};
  Squad.GetAgents(agents);
  Speaker = SCR_ChimeraCharacter.Cast(agents[agents.Count() - 1].GetControlledEntity());
  Plain = SCR_ChimeraCharacter.Cast(agents[agents.Count() - 2].GetControlledEntity());
  if (!Check(Speaker && Plain && Speaker != Plain, "two distinct squad members chosen")) { Finish("setup"); return; }
  SpeakerId = Speaker.GetID();
  PlainId = Plain.GetID();
  SCR_EditableCharacterComponent unit = EUD_Dialog.Find(Speaker);
  ExpectedLineTwo = string.Empty;
  // Production server writes, as the GM attribute path performs them.
  bool written = unit && unit.EUD_SetName(SPEAKER_NAME) && unit.EUD_SetLine(0, LINE_ONE) && unit.EUD_SetLine(4, LINE_FIVE) && unit.EUD_SetGesture(GESTURE);
  Check(written && UnitMatches(unit) && RegistryHoldsOnly(unit), "speaker configured (name, lines 1 and 5, gesture) and registered");
  SCR_EditableCharacterComponent plainUnit = EUD_Dialog.Find(Plain);
  Check(plainUnit && !plainUnit.EUD_IsConfigured(), "plain member carries no dialog");
  string label = Label(Speaker);
  PrintFormat("[EXPG DIALOG CACHE LABEL] stage=original label='%1'", label);
  Check(label == "Speak to " + SPEAKER_NAME, "original speaker action label is Speak to <Name>");
  FullZone = SpawnZone(Origin);
  if (!Check(FullZone != null, "Full cache zone spawned over the squad")) { Finish("setup"); return; }
  Advance(20);
 }

 void CachedChecks()
 {
  EBG_PrefabFullCache full = EBG_PrefabFullCache.Cast(Record.Full);
  IEntity oldSpeaker = GetGame().GetWorld().FindEntityByID(SpeakerId);
  IEntity oldPlain = GetGame().GetWorld().FindEntityByID(PlainId);
  Check(!oldSpeaker && !oldPlain && !Speaker && !Plain, "Full capture deleted the original characters");
  Check(ConfiguredCount() == 0, "deleted speaker left the dialog registry while cached");
  if (!Check(full && full.GetMemberCount() == 6, "Full transaction holds six survivor rows")) { Finish("cached"); return; }
  int carried;
  EUD_DialogRecord found;
  for (int i = 0; i < full.GetMemberCount(); i++)
  {
   EBG_SurvivorCarry carry = full.GetSurvivorCarry(i);
   if (!carry || !carry.EUD_GetDialog()) continue;
   carried++;
   found = carry.EUD_GetDialog();
  }
  PrintFormat("[EXPG DIALOG CACHE CACHED] cycle=%1 rows=%2 carriedDialogs=%3", Cycle + 1, full.GetMemberCount(), carried);
  Check(carried == 1, "exactly one survivor row carries dialog");
  Check(RecordMatches(found), "carried record equals the speaker's dialog at capture time");
 }

 void RespawnChecks()
 {
  Speaker = SpeakerMember.Entity;
  Plain = PlainMember.Entity;
  bool alive = Speaker && Speaker.GetCharacterController() && !Speaker.GetCharacterController().IsDead();
  Check(alive && Speaker.GetCharacterGroup() == Record.Group, "speaker member bound to a live respawned character in the restored group");
  SCR_EditableCharacterComponent unit = EUD_Dialog.Find(Speaker);
  Check(UnitMatches(unit), "respawned speaker has the same name, lines and gesture");
  Check(unit && unit.EUD_HasDialog() && unit.EUD_IsAvailableSpeaker(), "respawned speaker is an available speaker with dialog");
  Check(RegistryHoldsOnly(unit), "registry holds exactly the respawned speaker");
  string label = Label(Speaker);
  PrintFormat("[EXPG DIALOG CACHE LABEL] stage=respawn%1 label='%2'", Cycle + 1, label);
  Check(label == "Speak to " + SPEAKER_NAME, "respawned speaker action label is Speak to <Name>");
  EUD_SpeakAction action = FindSpeakAction(Speaker);
  Check(action && Plain && action.CanBeShownScript(Plain), "action shown to a squadmate on the respawned speaker");
  SCR_EditableCharacterComponent plainUnit = EUD_Dialog.Find(Plain);
  Check(plainUnit && !plainUnit.EUD_IsConfigured(), "plain member respawned without dialog");
  if (!Speaker || !Plain || !unit) { Finish("respawn"); return; }
  PrintFormat("[EXPG DIALOG CACHE CYCLE] cycle=%1 originalSpeaker=%2 respawnedSpeaker=%3 name='%4'", Cycle + 1, SpeakerId, Speaker.GetID(), unit.EUD_GetName());
  SpeakerId = Speaker.GetID();
  PlainId = Plain.GetID();
  Cycle++;
  if (Cycle >= CYCLES) { Finish("complete"); return; }
  // The next capture must read the respawned unit's current state, not the first record.
  ExpectedLineTwo = LINE_TWO_EDIT;
  Check(unit.EUD_SetLine(1, LINE_TWO_EDIT) && UnitMatches(unit), "dialog edited on the respawned speaker before the next cycle");
  s_Presence.Clear();
  Advance(21);
 }
}
// Presence seam, as in the wake budget and unit-cleanup fixtures.
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

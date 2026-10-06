// TEST ONLY. EXPBG Civil Protest Zone (addon/ambient-unrest) native server fixture.
// pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/EAU_ProtestGameplay.c -TimeoutSeconds 420 -OrchestratorSlotGranted -ExpectResult '\[EXPG EAU RESULT\] checks=95 failures=0 reason=complete'
// The runner copies this file to EXPG_GarrisonGameplay.c, so the driver class name is fixed.
// Without -ExpectResult the runner's own verdict checks Garrison evidence and reports FAIL
// for this fixture; accept on exactly one "[EXPG EAU RESULT] checks=95 failures=0
// reason=complete" line, no script errors, and "Game destroyed" in the retained logs.
// Real zone prefab, public SetSetting/RestoreSettings, real director ticks, native Kill,
// the real 1.8 before-save event and IsTracked (no save is written or loaded).
// No players: a spawned civilian with AI off stands in for a player character through
// EAU_Director.SetTestObservers (the fixture seam). Without it the zone must sleep; with
// it the zone wakes, and the crowd turns to face it. No GM UI, no client: replication,
// JIP and the visible look of gestures and turns need the separate client test.
class EXPG_EAUFixedSight : EAU_Sight
{
 ref array<bool> Visible = {};
 int Calls;

 override bool Sees(int index, vector from, vector to)
 {
  Calls++;
  return index >= 0 && index < Visible.Count() && Visible[index];
 }
}

class EXPG_GarrisonGameplayClass : GenericEntityClass {}
class EXPG_GarrisonGameplay : GenericEntity
{
 static const float FIXTURE_SECONDS = 360;
 static const ResourceName ZONE_PREFAB = "{B0D011A7D85EC526}PrefabsEditable/EXPAU/EAU_ProtestZone.et";
 static const ResourceName WATCHER_PREFAB = "{8C7093AF368F496A}Prefabs/Characters/Factions/CIV/GenericCivilians/Character_CIV_CottonShirt_1.et";
 // A protester counts as facing the stand-in player within this many degrees.
 static const float FACING_TOLERANCE = 40;
 EAU_ProtestZone Zone;
 IEntity Watcher;
 int Phase;
 int Checks;
 int Failures;
 float Started;
 float PhaseAt;
 bool Finished;
 vector Centre;
 ref array<EntityID> Ids = {};
 ref array<vector> Spots = {};
 ref array<EntityID> Gestured = {};
 ref array<EntityID> PreviousAlive = {};
 EntityID GroupId;
 EntityID SoundId;
 EntityID CasualtyId;
 int Target;
 float MaxDrift;
 int StanceChanges;
 int MaxRemovedPerFrame;
 // Sleep cycle
 int LivingBeforeSleep;
 int WakesBeforeSleep;
 bool DelayChecked;
 float SleptAfter = -1;
 float AsleepCheckedAt = -1;

 void EXPG_GarrisonGameplay(IEntitySource src, IEntity parent) { SetEventMask(EntityEvent.INIT | EntityEvent.FRAME); }
 float Now() { return GetGame().GetWorld().GetWorldTime() * 0.001; }

 override void EOnInit(IEntity owner)
 {
  if (!Replication.IsServer()) { ClearEventMask(EntityEvent.FRAME); return; }
  Started = Now();
  PhaseAt = Started + 5;
  PrintFormat("[EXPG EAU BEGIN] deadline=%1 driver=%2", FIXTURE_SECONDS, GetOrigin());
 }

 bool Check(bool ok, string label)
 {
  Checks++;
  if (!ok) Failures++;
  PrintFormat("[EXPG EAU CHECK] pass=%1 %2", ok, label);
  return ok;
 }

 void Finish(string reason)
 {
  if (Finished) return;
  Finished = true;
  ClearEventMask(EntityEvent.FRAME);
  EAU_Director.SetTestObservers(null);
  if (Watcher) SCR_EntityHelper.DeleteEntityAndChildren(Watcher);
  PrintFormat("[EXPG EAU RESULT] checks=%1 failures=%2 reason=%3", Checks, Failures, reason);
  GetGame().RequestClose();
 }

 void Advance(int phase)
 {
  Phase = phase;
  PhaseAt = Now();
 }

 // First candidate centre with mostly clear, dry, level ground inside 8 m.
 vector ChooseCentre()
 {
  BaseWorld world = GetGame().GetWorld();
  array<vector> offsets = {Vector(60, 0, 0), Vector(-60, 0, 0), Vector(0, 0, 60), Vector(0, 0, -60), Vector(90, 0, 90), Vector(-90, 0, -90), Vector(120, 0, 0), Vector(0, 0, 120)};
  foreach (vector offset : offsets)
  {
   vector centre = GetOrigin() + offset;
   centre[1] = world.GetSurfaceY(centre[0], centre[2]);
   int clear;
   for (int x = -2; x <= 2; x++)
   {
    for (int z = -2; z <= 2; z++)
    {
     vector spot;
     if (EAU_CrowdCast.GroundSpot(world, centre + Vector(x * 3, 0, z * 3), centre[1], spot)) clear++;
    }
   }
   PrintFormat("[EXPG EAU CENTRE] candidate=%1 clear=%2/25", centre, clear);
   if (clear >= 22) return centre;
  }
  return vector.Zero;
 }

 // Stand-in player behind or beside the crowd (at least 90 degrees from the way
 // it faces), on open ground and in sight of the centre with the zone's own trace.
 vector ChooseWatcherSpot()
 {
  BaseWorld world = GetGame().GetWorld();
  EAU_WorldSight sight = new EAU_WorldSight(world);
  vector forward = Zone.GetWorldTransformAxis(2); forward[1] = 0; forward.Normalize();
  vector right = Zone.GetWorldTransformAxis(0); right[1] = 0; right.Normalize();
  array<vector> directions = {-forward, (right - forward).Normalized(), (-right - forward).Normalized(), right, -right};
  array<float> distances = {14.0, 18.0, 11.0};
  foreach (float distance : distances)
  {
   foreach (vector direction : directions)
   {
    vector spot;
    if (!EAU_CrowdCast.GroundSpot(world, Centre + direction * distance, Centre[1], spot)) continue;
    if (!sight.Sees(0, Centre + "0 1.6 0", spot + "0 1.6 0")) continue;
    PrintFormat("[EXPG EAU WATCHER SPOT] spot=%1 distance=%2 direction=%3", spot, distance, direction);
    return spot;
   }
  }
  return vector.Zero;
 }

 IEntity SpawnWatcher(vector spot)
 {
  // Keep the Resource and the spawned entity in locals before casting.
  Resource prefab = Resource.Load(WATCHER_PREFAB);
  EntitySpawnParams params = new EntitySpawnParams();
  params.TransformMode = ETransformMode.WORLD;
  Math3D.MatrixIdentity4(params.Transform);
  params.Transform[3] = spot;
  IEntity spawned = GetGame().SpawnEntityPrefab(prefab, GetGame().GetWorld(), params);
  SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(spawned);
  if (!character) return null;
  AIControlComponent control = AIControlComponent.Cast(character.FindComponent(AIControlComponent));
  if (control) control.DeactivateAI();
  return character;
 }

 void Collect(notnull array<SCR_ChimeraCharacter> actors)
 {
  actors.Clear();
  array<IEntity> entities = {};
  if (Zone) Zone.GetActors(entities);
  foreach (IEntity entity : entities)
  {
   SCR_ChimeraCharacter actor = SCR_ChimeraCharacter.Cast(entity);
   if (actor) actors.Insert(actor);
  }
 }

 bool Alive(SCR_ChimeraCharacter actor)
 {
  return actor && actor.GetCharacterController() && !actor.GetCharacterController().IsDead();
 }

 bool Flagged(IEntity entity)
 {
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(entity);
  return !editable || editable.HasEntityFlag(EEditableEntityFlag.NON_SERIALIZABLE);
 }

 int Present(array<EntityID> ids)
 {
  int present;
  foreach (EntityID id : ids)
  {
   if (GetGame().GetWorld().FindEntityByID(id)) present++;
  }
  return present;
 }

 // Removal must stay bounded: never more of the tracked entities gone in one frame
 // than the director's per-tick delete allowance.
 void TrackRemovals()
 {
  int removed;
  foreach (EntityID id : PreviousAlive)
  {
   if (!GetGame().GetWorld().FindEntityByID(id)) removed++;
  }
  if (removed > MaxRemovedPerFrame) MaxRemovedPerFrame = removed;
  PreviousAlive.Clear();
  foreach (EntityID id : Ids)
  {
   if (GetGame().GetWorld().FindEntityByID(id)) PreviousAlive.Insert(id);
  }
 }

 void Remember()
 {
  Ids.Clear(); Spots.Clear(); PreviousAlive.Clear();
  array<SCR_ChimeraCharacter> actors = {};
  Collect(actors);
  foreach (SCR_ChimeraCharacter actor : actors)
  {
   Ids.Insert(actor.GetID());
   Spots.Insert(actor.GetOrigin());
  }
  if (Zone.GetGroup()) { GroupId = Zone.GetGroup().GetID(); Ids.Insert(GroupId); }
  if (Zone.GetSound()) { SoundId = Zone.GetSound().GetID(); Ids.Insert(SoundId); }
  PreviousAlive.Copy(Ids);
 }

 void Observe(bool present)
 {
  array<IEntity> observers = {};
  if (present && Watcher) observers.Insert(Watcher);
  EAU_Director.SetTestObservers(observers);
 }

 override void EOnFrame(IEntity owner, float timeSlice)
 {
  if (Finished) return;
  float now = Now();
  if (now - Started > FIXTURE_SECONDS) { Check(false, string.Format("phase %1 finished before the fixture deadline", Phase)); Finish("timeout"); return; }
  if (Phase == 0) { if (now >= PhaseAt) Setup(); return; }
  if (Phase == 1) { IdleSleep(now); return; }
  if (Phase == 2) { Gathered(now); return; }
  if (Phase == 3) { Hold(now); return; }
  if (Phase == 4) { AfterCasualty(now); return; }
  if (Phase == 5) { SleepCycle(now); return; }
  if (Phase == 6) { Woken(now); return; }
  if (Phase == 7) { Shrink(now); return; }
  if (Phase == 8) { Disabled(now); return; }
  if (Phase == 9) { Regathered(now); return; }
  if (Phase == 10) { Deleted(now); return; }
 }

 void Setup()
 {
  Centre = ChooseCentre();
  if (!Check(Centre != vector.Zero, "open ground for an 8 m crowd found near the driver")) { Finish("setup"); return; }
  // Keep the Resource and the spawned entity in locals before casting.
  Resource prefab = Resource.Load(ZONE_PREFAB);
  EntitySpawnParams params = new EntitySpawnParams();
  params.TransformMode = ETransformMode.WORLD;
  Math3D.AnglesToMatrix(Vector(45, 0, 0), params.Transform);
  params.Transform[3] = Centre;
  IEntity spawned = GetGame().SpawnEntityPrefab(prefab, GetGame().GetWorld(), params);
  Zone = EAU_ProtestZone.Cast(spawned);
  if (!Check(Zone != null, "real zone prefab spawned")) { Finish("setup"); return; }
  Check(Zone.GetSetting(EAU_ProtestZone.KEY_ENABLED) == 0 && !Zone.IsRunning(), "new zone starts Off and idle");
  Check(Zone.GetSetting(EAU_ProtestZone.KEY_CROWD_MIN) == 10 && Zone.GetSetting(EAU_ProtestZone.KEY_CROWD_MAX) == 15, "default crowd size 10-15");
  Check(Zone.GetSetting(EAU_ProtestZone.KEY_SOUND) == EAU_ProtestZone.SOUND_ALTERNATE, "new zone crowd sound is Alternate");
  Check(Zone.GetSetting(EAU_ProtestZone.KEY_WAKE) == 300, "new zone wake distance is 300 m");
  CrowdBankContract();
  // Persistence batch contract: complete valid batches only.
  Check(!Zone.RestoreSettings({0, 8, 12, 11, 0}), "restore refuses maximum below minimum");
  Check(!Zone.RestoreSettings({0, 99, 10, 15, 0}), "restore refuses an out-of-range radius");
  Check(!Zone.RestoreSettings({0, 8, 10}), "restore refuses a partial batch");
  Check(!Zone.RestoreSettings({0, 8, 10, 15, 0, 3}), "restore refuses an unknown crowd sound");
  Check(!Zone.RestoreSettings({0, 8, 10, 15, 0, 1, 40}), "restore refuses a wake distance below 50 m");
  Check(Zone.RestoreSettings({0, 8, 10, 15, 0, 2, 500}) && Zone.GetSetting(EAU_ProtestZone.KEY_WAKE) == 500, "restore accepts a full seven-setting batch (wake 500 m)");
  // Crowd sound 1 is Rioting crowd; a save from before Wake distance holds six values.
  Check(Zone.RestoreSettings({0, 8, 10, 15, 0, 1}) && Zone.GetSetting(EAU_ProtestZone.KEY_SOUND) == EAU_ProtestZone.SOUND_RIOTING && Zone.GetSetting(EAU_ProtestZone.KEY_WAKE) == 300, "a six-setting batch restores with Rioting crowd and the 300 m wake distance");
  // A save from before the Crowd sound setting holds five values.
  Check(Zone.RestoreSettings({0, 8, 10, 15, 1}) && Zone.GetSetting(EAU_ProtestZone.KEY_SOUND) == EAU_ProtestZone.SOUND_ALTERNATE && Zone.GetSetting(EAU_ProtestZone.KEY_WAKE) == 300, "a five-setting batch from an older save restores with Alternate and 300 m");
  Zone.SetSetting(EAU_ProtestZone.KEY_WAKE, 10);
  Check(Zone.GetSetting(EAU_ProtestZone.KEY_WAKE) == EAU_ProtestZone.WAKE_MIN, "a GM wake distance below the minimum clamps to 50 m");
  Zone.SetSetting(EAU_ProtestZone.KEY_WAKE, 300);
  Zone.SetSetting(EAU_ProtestZone.KEY_CROWD_MIN, 20);
  Check(Zone.GetSetting(EAU_ProtestZone.KEY_CROWD_MAX) == 20, "raising the minimum lifts the maximum");
  Zone.SetSetting(EAU_ProtestZone.KEY_CROWD_MAX, 15);
  Check(Zone.GetSetting(EAU_ProtestZone.KEY_CROWD_MIN) == 15, "lowering the maximum lowers the minimum");
  Zone.SetSetting(EAU_ProtestZone.KEY_CROWD_MIN, 10);
  PresenceContract();
  FacingContract();
  vector spot = ChooseWatcherSpot();
  if (!Check(spot != vector.Zero, "open ground in sight of the centre for the stand-in player")) { Finish("setup"); return; }
  Watcher = SpawnWatcher(spot);
  if (!Check(Watcher != null, "stand-in player character spawned (AI off)")) { Finish("setup"); return; }
  // No player character anywhere: an enabled zone must wait asleep, not gather.
  Observe(false);
  Zone.SetSetting(EAU_ProtestZone.KEY_ENABLED, 1);
  Advance(1);
 }

 // Ambient Sounds crowd bank: Rioting crowd (4) at every range with its duration,
 // and the Alternate group 101 playing angry (0) and rioting (4) in turn.
 void CrowdBankContract()
 {
  int riotRanges;
  for (int range = 10; range <= 150; range += 10)
  {
   if (EAS_CrowdBank.Event(EAU_ProtestZone.RECORDING_RIOTING, range) == "SOUND_EAS_CROWD_RIOTING_R" + range.ToString()) riotRanges++;
  }
  Check(riotRanges == 15, string.Format("rioting crowd event at every range 10-150 (%1/15)", riotRanges));
  // 1810994 frames at 44100 Hz.
  Check(Math.AbsFloat(EAS_CrowdBank.Duration(EAU_ProtestZone.RECORDING_RIOTING) - 41.0656236) < 0.0001, string.Format("rioting crowd duration %1 s", EAS_CrowdBank.Duration(EAU_ProtestZone.RECORDING_RIOTING)));
  Check(EAS_CrowdBank.ValidSelection(EAU_ProtestZone.RECORDING_RIOTING) && EAS_CrowdBank.ValidSelection(EAU_ProtestZone.SELECTION_ALTERNATE) && EAS_CrowdBank.Event(EAU_ProtestZone.SELECTION_ALTERNATE).IsEmpty(), "rioting is a recording, alternate a selection group");
  Check(EAU_ProtestZone.SoundSelection(EAU_ProtestZone.SOUND_ANGRY) == 0 && EAU_ProtestZone.SoundSelection(EAU_ProtestZone.SOUND_RIOTING) == 4 && EAU_ProtestZone.SoundSelection(EAU_ProtestZone.SOUND_ALTERNATE) == 101, "crowd sound settings map to recordings 0 and 4 and group 101");
  Check(EAS_CrowdBank.Resolve(EAU_ProtestZone.SELECTION_ALTERNATE, 0) == 4 && EAS_CrowdBank.Resolve(EAU_ProtestZone.SELECTION_ALTERNATE, 4) == 0, "alternate follows angry with rioting and rioting with angry");
  // Eight loops from a fresh start, as the runtime passes the last recording.
  int previous = -1;
  int angry, rioting, repeats;
  for (int loop = 0; loop < 8; loop++)
  {
   int picked = EAS_CrowdBank.Resolve(EAU_ProtestZone.SELECTION_ALTERNATE, previous);
   if (picked == 0) angry++;
   if (picked == 4) rioting++;
   if (picked == previous) repeats++;
   previous = picked;
  }
  Check(angry == 4 && rioting == 4 && repeats == 0, string.Format("alternate over 8 loops: angry %1 rioting %2 repeats %3", angry, rioting, repeats));
  int randomRioting;
  for (int draw = 0; draw < 64; draw++)
  {
   if (EAS_CrowdBank.Resolve(100, draw % 4) == 4) randomRioting++;
  }
  Check(randomRioting > 0, string.Format("Random crowd draws the rioting crowd (%1/64)", randomRioting));
 }

 // Wake within the wake distance; a gathered crowd stays until 50 m beyond it.
 void PresenceContract()
 {
  Check(!EAU_ProtestZone.PlayerNear(false, -1, 300) && !EAU_ProtestZone.PlayerNear(true, -1, 300), "no player character: never near");
  Check(EAU_ProtestZone.PlayerNear(false, 300, 300) && !EAU_ProtestZone.PlayerNear(false, 320, 300), "a sleeping zone wakes only within the wake distance");
  Check(EAU_ProtestZone.PlayerNear(true, 320, 300) && EAU_ProtestZone.PlayerNear(true, 350, 300) && !EAU_ProtestZone.PlayerNear(true, 351, 300), "an awake crowd stays until 50 m beyond the wake distance");
 }

 // Selection with injected sight: only players in range and in sight, uniformly
 // among them, at most MAX_SIGHT_CHECKS traces per pick.
 void FacingContract()
 {
  vector eye = "0 1.6 0";
  // 0: 10 m seen, 1: 20 m blocked, 2: 60 m seen but out of range, 3: 30 m seen.
  array<vector> eyes = {Vector(10, 1.6, 0), Vector(0, 1.6, 20), Vector(60, 1.6, 0), Vector(-30, 1.6, 0)};
  EXPG_EAUFixedSight sight = new EXPG_EAUFixedSight();
  sight.Visible = {true, false, true, true};
  array<int> counts = {0, 0, 0, 0};
  int none, maxCalls;
  for (int draw = 0; draw < 200; draw++)
  {
   sight.Calls = 0;
   int pick = EAU_Facing.Pick(eye, eyes, EAU_Facing.RANGE, sight);
   if (pick < 0) none++;
   else counts[pick] = counts[pick] + 1;
   if (sight.Calls > maxCalls) maxCalls = sight.Calls;
  }
  PrintFormat("[EXPG EAU FACING PICK] near=%1 blocked=%2 far=%3 other=%4 none=%5 maxCalls=%6", counts[0], counts[1], counts[2], counts[3], none, maxCalls);
  Check(counts[1] == 0 && counts[2] == 0, "never a blocked or out-of-range player");
  Check(none == 0, "a visible player in range is always found");
  Check(counts[0] >= 60 && counts[3] >= 60, string.Format("random among the visible players (%1/%2 of 200)", counts[0], counts[3]));
  Check(maxCalls <= EAU_Facing.MAX_SIGHT_CHECKS, string.Format("at most %1 sight traces per pick (%2)", EAU_Facing.MAX_SIGHT_CHECKS, maxCalls));
  sight.Visible = {false, false, true, false};
  int found;
  for (int blocked = 0; blocked < 20; blocked++)
  {
   if (EAU_Facing.Pick(eye, eyes, EAU_Facing.RANGE, sight) >= 0) found++;
  }
  array<vector> nobody = {};
  Check(found == 0 && EAU_Facing.Pick(eye, nobody, EAU_Facing.RANGE, sight) < 0, "nobody in sight within range: no target");
  // Six in range, only the last in sight: the trace bound holds and it is still found.
  array<vector> crowd = {Vector(5, 1.6, 0), Vector(6, 1.6, 0), Vector(7, 1.6, 0), Vector(8, 1.6, 0), Vector(9, 1.6, 0), Vector(10, 1.6, 0)};
  sight.Visible = {false, false, false, false, false, true};
  int lastFound, wrong, crowdCalls;
  for (int attempt = 0; attempt < 100; attempt++)
  {
   sight.Calls = 0;
   int choice = EAU_Facing.Pick(eye, crowd, EAU_Facing.RANGE, sight);
   if (choice == 5) lastFound++;
   else if (choice >= 0) wrong++;
   if (sight.Calls > crowdCalls) crowdCalls = sight.Calls;
  }
  Check(wrong == 0 && lastFound > 0 && crowdCalls <= EAU_Facing.MAX_SIGHT_CHECKS, string.Format("bounded search among six: found %1/100, max %2 traces", lastFound, crowdCalls));
  // Facing math in the entity yaw convention the zone spawns with.
  float zoneYaw = Math.Repeat(Zone.GetYawPitchRoll()[0], 360);
  float forwardYaw = EAU_Facing.YawTo(Zone.GetOrigin(), Zone.GetOrigin() + Zone.GetWorldTransformAxis(2) * 10);
  Check(EAU_Facing.Gap(zoneYaw, forwardYaw) < 1, string.Format("YawTo agrees with entity yaw (%1 vs %2)", zoneYaw, forwardYaw));
  Check(EAU_Facing.Gap(350, 10) == 20 && EAU_Facing.Gap(10, 350) == 20 && EAU_Facing.Gap(90, 270) == 180, "yaw gap wraps around north");
 }

 void IdleSleep(float now)
 {
  if (!Zone.IsAsleep())
  {
   if (now - PhaseAt > 6) { Check(false, "an enabled zone with no player near went to sleep within 6 s"); Finish("idle-sleep"); }
   return;
  }
  // Give the director a few ticks to prove nothing is gathered for nobody.
  if (now - PhaseAt < 2) return;
  Check(Zone.GetSetting(EAU_ProtestZone.KEY_ENABLED) == 1 && Zone.GetRunState() == EAU_ProtestZone.STATE_SLEEPING, "zone stays On and reports Sleeping");
  Check(!Zone.IsRunning() && Zone.GetPlaced() == 0 && Zone.GetGroup() == null && Zone.GetSound() == null, "no crowd, group or sound gathered while nobody is near");
  // A player character within the wake distance.
  Observe(true);
  Advance(2);
 }

 void Gathered(float now)
 {
  TrackGestures();
  bool ready = Zone.IsSpawnFinished() && Zone.GetSound() != null;
  if (!ready)
  {
   if (now - PhaseAt > 45) { Check(false, string.Format("crowd gathered in 45 s (placed %1/%2)", Zone.GetPlaced(), Zone.GetTarget())); Finish("gather"); }
   return;
  }
  Target = Zone.GetTarget();
  array<SCR_ChimeraCharacter> actors = {};
  Collect(actors);
  SCR_AIGroup group = Zone.GetGroup();
  PrintFormat("[EXPG EAU GATHER] seconds=%1 target=%2 placed=%3 living=%4 group=%5", now - PhaseAt, Target, Zone.GetPlaced(), Zone.GetLivingCount(), group);
  Check(!Zone.IsAsleep() && Zone.GetWakes() == 1, "a player character within the wake distance woke the zone");
  Check(Target >= 10 && Target <= 15, "crowd size rolled inside 10-15");
  Check(Zone.GetPlaced() == Target && Zone.GetLivingCount() == Target && actors.Count() == Target, "every protester placed and alive");
  Check(group != null && group.GetAgentsCount() == Target, "one group holds the whole crowd");
  Check(group && group.GetFaction() && group.GetFaction().GetFactionKey() == EAU_ProtestZone.CIV_FACTION, "group is CIV");
  Check(group && EAU_Director.ReservesGroup(group), "director reserves the group");
  // Counted either way, so the RESULT check total does not depend on the world.
  Check(!EBG_CacheManager.Instance || EBG_CacheManager.Instance.IsReserved(group), string.Format("Unit Caching treats the crowd group as reserved (cache manager present %1)", EBG_CacheManager.Instance != null));
  Check(Flagged(group), "group kept out of saves");
  int inside, unarmed, sameGroup, staticAi, flagged;
  float radius = Zone.GetSetting(EAU_ProtestZone.KEY_RADIUS) + 0.3;
  foreach (SCR_ChimeraCharacter actor : actors)
  {
   if (vector.DistanceXZ(actor.GetOrigin(), Zone.GetAnchor()) <= radius) inside++;
   if (EAU_CrowdCast.IsUnarmed(actor)) unarmed++;
   if (actor.GetCharacterGroup() == group) sameGroup++;
   AIControlComponent control = AIControlComponent.Cast(actor.FindComponent(AIControlComponent));
   if (control && !control.IsAIActivated()) staticAi++;
   if (Flagged(actor)) flagged++;
  }
  Check(inside == Target, string.Format("protesters inside the radius %1/%2", inside, Target));
  Check(unarmed == Target, string.Format("protesters unarmed %1/%2", unarmed, Target));
  Check(sameGroup == Target, string.Format("protesters in the zone group %1/%2", sameGroup, Target));
  Check(staticAi == Target, string.Format("protester AI deactivated %1/%2", staticAi, Target));
  Check(flagged == Target, string.Format("protesters kept out of saves %1/%2", flagged, Target));
  EAS_CrowdModule sound = Zone.GetSound();
  Check(sound.Enabled == 1 && sound.Recording == EAU_ProtestZone.SELECTION_ALTERNATE && sound.Loop == 1 && sound.Range == Zone.AudibleRange(), string.Format("crowd emitter on, alternating angry and rioting, looping, range %1", sound.Range));
  Check(sound.DebugEnabled == 1, "emitter follows zone debug");
  SaveContract(group, sound, actors);
  Remember();
  // A GM change of Crowd sound reaches the running emitter (checked in Hold).
  Zone.SetSetting(EAU_ProtestZone.KEY_SOUND, EAU_ProtestZone.SOUND_RIOTING);
  Advance(3);
 }

 int Tracked(PersistenceSystem persistence, notnull array<IEntity> entities)
 {
  int tracked;
  foreach (IEntity entity : entities)
  {
   if (entity && persistence.IsTracked(entity)) tracked++;
  }
  return tracked;
 }

 // Vanilla 1.8 saves what IsTracked reports; GetId may still answer for a
 // released entity, so ids prove nothing. One protester is tracked again first,
 // as a late or foreign StartTracking would, then the real before-save event
 // runs: no crowd entity may stay tracked, the zone must be, with its settings.
 void SaveContract(SCR_AIGroup group, EAS_CrowdModule sound, notnull array<SCR_ChimeraCharacter> actors)
 {
  SCR_PersistenceSystem persistence = SCR_PersistenceSystem.GetByEntityWorld(Zone);
  EPersistenceSystemState state = EPersistenceSystemState.FAILURE;
  if (persistence) state = persistence.GetState();
  if (!Check(state == EPersistenceSystemState.ACTIVE, string.Format("native persistence active in the fixture world (state %1)", typename.EnumToString(EPersistenceSystemState, state)))) return;
  array<IEntity> crowd = {};
  if (group) crowd.Insert(group);
  if (sound) crowd.Insert(sound);
  foreach (SCR_ChimeraCharacter actor : actors) crowd.Insert(actor);
  int trackedBefore = Tracked(persistence, crowd);
  // StartTracking may answer false for an entity that is still tracked; IsTracked decides.
  if (!actors.IsEmpty()) persistence.StartTracking(actors[0], false);
  bool retracked = !actors.IsEmpty() && persistence.IsTracked(actors[0]);
  // The sweep must have something to release, or the check below proves nothing.
  int trackedPreSave = Tracked(persistence, crowd);
  persistence.GetOnBeforeSave().Invoke(ESaveGameType.MANUAL);
  int trackedAtSave = Tracked(persistence, crowd);
  bool zoneTracked = persistence.IsTracked(Zone);
  bool serialized;
  string sample;
  if (zoneTracked)
  {
   SCR_PersistenceJsonSaveContext probe = new SCR_PersistenceJsonSaveContext();
   serialized = persistence.Serialize(Zone, probe) == ESerializeResult.OK && probe.IsValid();
   sample = probe.SaveToString();
  }
  PrintFormat("[EXPG EAU PERSISTENCE] crowd=%1 trackedBefore=%2 retracked=%3 trackedPreSave=%4 trackedAtSave=%5 zoneTracked=%6 zoneId=%7 firstActorIdNull=%8 zoneJson=%9", crowd.Count(), trackedBefore, retracked, trackedPreSave, trackedAtSave, zoneTracked, persistence.GetId(Zone), actors.IsEmpty() || persistence.GetId(actors[0]).IsEmpty(), sample);
  Check(trackedPreSave > 0 && trackedAtSave == 0, string.Format("no crowd entity tracked when a save reads its data (%1/%2 tracked going into the save event, %3 after)", trackedPreSave, crowd.Count(), trackedAtSave));
  Check(zoneTracked, "zone module tracked for saves");
  Check(serialized && sample.Contains("eauVersion") && sample.Contains("settings"), "zone settings serialize with the zone");
 }

 void TrackGestures()
 {
  array<SCR_ChimeraCharacter> actors = {};
  Collect(actors);
  foreach (SCR_ChimeraCharacter actor : actors)
  {
   CharacterControllerComponent controller = actor.GetCharacterController();
   if (controller && controller.IsPlayingGesture() && !Gestured.Contains(actor.GetID())) Gestured.Insert(actor.GetID());
  }
 }

 // Living protesters whose body faces the stand-in player.
 int CountFacing(notnull array<SCR_ChimeraCharacter> actors, out int living)
 {
  living = 0;
  int facing;
  vector eye = EAU_Facing.Eye(Watcher);
  foreach (SCR_ChimeraCharacter actor : actors)
  {
   if (!Alive(actor)) continue;
   living++;
   float bearing = EAU_Facing.YawTo(actor.GetOrigin(), eye);
   vector angles = actor.GetYawPitchRoll();
   if (bearing >= 0 && EAU_Facing.Gap(angles[0], bearing) <= FACING_TOLERANCE) facing++;
  }
  return facing;
 }

 void Hold(float now)
 {
  TrackGestures();
  array<SCR_ChimeraCharacter> actors = {};
  Collect(actors);
  foreach (SCR_ChimeraCharacter actor : actors)
  {
   int known = Ids.Find(actor.GetID());
   if (known < 0 || known >= Spots.Count()) continue;
   MaxDrift = Math.Max(MaxDrift, vector.DistanceXZ(actor.GetOrigin(), Spots[known]));
   CharacterControllerComponent controller = actor.GetCharacterController();
   if (controller && controller.GetStance() != ECharacterStance.STAND) StanceChanges++;
  }
  if (now - PhaseAt < 15) return;
  PrintFormat("[EXPG EAU HOLD] drift=%1 stanceFrames=%2 gestured=%3/%4 started=%5 refused=%6", MaxDrift, StanceChanges, Gestured.Count(), Target, Zone.GetGesturesStarted(), Zone.GetGesturesRefused());
  Check(MaxDrift < 0.3, string.Format("static crowd: max drift %1 m over 15 s", MaxDrift));
  Check(StanceChanges == 0, "no stance change while protesting");
  Check(Zone.GetGesturesStarted() >= Target, "protest gestures started for the crowd");
  Check(Gestured.Count() * 10 >= Target * 6, string.Format("at least 60 percent seen gesturing (%1/%2)", Gestured.Count(), Target));
  // The stand-in player stands behind or beside the crowd: facing it takes a real turn.
  int living;
  int facing = CountFacing(actors, living);
  int requested = Zone.GetTurnsRequested();
  int snapped = Zone.GetTurnsSnapped();
  PrintFormat("[EXPG EAU FACING] facing=%1/%2 picks=%3 turns=%4 snapped=%5", facing, living, Zone.GetFacePicks(), requested, snapped);
  Check(living > 0 && facing * 10 >= living * 6, string.Format("at least 60 percent face the visible player (%1/%2 within %3 degrees)", facing, living, FACING_TOLERANCE));
  Check(Zone.GetFacePicks() >= living && requested > 0, string.Format("each protester picked the visible player (%1 picks, %2 turns)", Zone.GetFacePicks(), requested));
  Check(snapped * 2 <= requested, string.Format("native heading turns (snap fallback for %1 of %2 turns)", snapped, requested));
  EAS_CrowdModule held = Zone.GetSound();
  Check(held && held.GetID() == SoundId && held.Recording == EAU_ProtestZone.RECORDING_RIOTING, "Crowd sound Rioting applied to the same running emitter");
  Zone.SetSetting(EAU_ProtestZone.KEY_SOUND, EAU_ProtestZone.SOUND_ALTERNATE);
  // Native kill of one protester: a casualty is never replaced.
  if (actors.IsEmpty()) { Check(false, "a protester is left to kill"); Finish("hold"); return; }
  SCR_ChimeraCharacter victim = actors[0];
  CasualtyId = victim.GetID();
  SCR_DamageManagerComponent damage = victim.GetDamageManager();
  if (damage) damage.Kill(Instigator.CreateInstigator(null));
  Advance(4);
 }

 void AfterCasualty(float now)
 {
  if (now - PhaseAt < 5) return;
  SCR_ChimeraCharacter victim = SCR_ChimeraCharacter.Cast(GetGame().GetWorld().FindEntityByID(CasualtyId));
  Check(victim == null || !Alive(victim), "native kill took effect");
  Check(Zone.GetPlaced() == Target && Zone.GetLivingCount() == Target - 1, string.Format("casualty not replaced (placed %1 living %2)", Zone.GetPlaced(), Zone.GetLivingCount()));
  Check(Zone.GetSound() && Zone.GetSound().Recording == EAU_ProtestZone.SELECTION_ALTERNATE, "Crowd sound back to Alternate on the emitter");
  // Every player leaves: the zone must sleep after the delay, not before.
  LivingBeforeSleep = Zone.GetLivingCount();
  WakesBeforeSleep = Zone.GetWakes();
  Remember();
  MaxRemovedPerFrame = 0;
  Observe(false);
  Advance(5);
 }

 void SleepCycle(float now)
 {
  TrackRemovals();
  float elapsed = now - PhaseAt;
  if (!DelayChecked && elapsed >= EAU_ProtestZone.SLEEP_DELAY_SECONDS - 2)
  {
   DelayChecked = true;
   Check(Zone.IsRunning() && !Zone.IsAsleep() && Zone.GetLivingCount() == LivingBeforeSleep, string.Format("crowd stays through the sleep delay (%1 s without players)", elapsed));
  }
  if (SleptAfter < 0 && Zone.IsAsleep()) SleptAfter = elapsed;
  if (SleptAfter < 0 || Present(Ids) > 0)
  {
   if (elapsed > 30) { Check(false, string.Format("zone slept and removed its crowd within 30 s (asleep %1, %2 left)", Zone.IsAsleep(), Present(Ids))); Finish("sleep"); }
   return;
  }
  if (AsleepCheckedAt < 0)
  {
   AsleepCheckedAt = now;
   float latest = EAU_ProtestZone.SLEEP_DELAY_SECONDS + 2 * EAU_ProtestZone.PRESENCE_SECONDS + 1;
   PrintFormat("[EXPG EAU SLEEP] sleptAfter=%1 removedAfter=%2 cached=%3 living=%4 maxRemoved=%5", SleptAfter, elapsed, Zone.GetCachedCount(), LivingBeforeSleep, MaxRemovedPerFrame);
   Check(SleptAfter >= EAU_ProtestZone.SLEEP_DELAY_SECONDS - 0.5 && SleptAfter <= latest, string.Format("slept %1 s after the last player left (delay %2 s)", SleptAfter, EAU_ProtestZone.SLEEP_DELAY_SECONDS));
   Check(Zone.GetSetting(EAU_ProtestZone.KEY_ENABLED) == 1 && Zone.GetRunState() == EAU_ProtestZone.STATE_SLEEPING, "zone stays On and reports Sleeping");
   Check(true, string.Format("sleep removed every protester, the casualty's body, the group and the crowd sound in %1 s", elapsed));
   Check(!Zone.IsRunning() && Zone.GetGroup() == null && Zone.GetSound() == null && Zone.GetTrackedCount() == 0, "zone holds no entity while asleep");
   Check(MaxRemovedPerFrame <= EAU_Director.DELETES_PER_TICK, string.Format("bounded sleep: max %1 removed in one frame", MaxRemovedPerFrame));
   Check(Zone.GetCachedCount() == LivingBeforeSleep, string.Format("the %1 survivors are remembered (%2 cached)", LivingBeforeSleep, Zone.GetCachedCount()));
   return;
  }
  // Still nobody near: the zone must stay asleep.
  if (now - AsleepCheckedAt < 4) return;
  Check(Zone.IsAsleep() && Zone.GetPlaced() == 0 && Zone.GetGroup() == null, "no player: the zone stays asleep");
  Observe(true);
  Advance(6);
 }

 void Woken(float now)
 {
  if (!Zone.IsSpawnFinished() || !Zone.GetSound())
  {
   if (now - PhaseAt > 45) { Check(false, string.Format("crowd came back within 45 s (placed %1/%2)", Zone.GetPlaced(), Zone.GetTarget())); Finish("wake"); }
   return;
  }
  array<SCR_ChimeraCharacter> actors = {};
  Collect(actors);
  int fresh, home;
  foreach (SCR_ChimeraCharacter actor : actors)
  {
   if (!Ids.Contains(actor.GetID())) fresh++;
   foreach (vector spot : Spots)
   {
    if (vector.DistanceXZ(actor.GetOrigin(), spot) <= 0.6) { home++; break; }
   }
  }
  PrintFormat("[EXPG EAU WAKE] seconds=%1 target=%2 placed=%3 living=%4 fresh=%5 home=%6 wakes=%7", now - PhaseAt, Zone.GetTarget(), Zone.GetPlaced(), Zone.GetLivingCount(), fresh, home, Zone.GetWakes());
  Check(!Zone.IsAsleep() && Zone.GetWakes() == WakesBeforeSleep + 1 && Zone.GetRunState() == EAU_ProtestZone.STATE_PROTESTING, "a returning player woke the zone");
  Check(Zone.GetTarget() == Target && Zone.GetPlaced() == Target && Zone.GetLivingCount() == LivingBeforeSleep, string.Format("survivors back, casualty not replaced (living %1 of %2, before sleep %3)", Zone.GetLivingCount(), Target, LivingBeforeSleep));
  Check(fresh == actors.Count(), "fresh entities after the wake");
  Check(home * 10 >= LivingBeforeSleep * 8, string.Format("survivors back at their own spots (%1/%2)", home, LivingBeforeSleep));
  Check(Zone.GetGroup() != null && Zone.GetGroup().GetAgentsCount() == LivingBeforeSleep, "one new group holds the returned crowd");
  Check(Zone.GetSound().Recording == EAU_ProtestZone.SELECTION_ALTERNATE && Zone.GetSound().Range == Zone.AudibleRange(), "crowd sound back with the zone settings");
  Zone.SetSetting(EAU_ProtestZone.KEY_CROWD_MAX, 5);
  Check(Zone.GetSetting(EAU_ProtestZone.KEY_CROWD_MIN) == 5, "crowd size 5-5 applied");
  MaxRemovedPerFrame = 0;
  Remember();
  Advance(7);
 }

 void Shrink(float now)
 {
  TrackRemovals();
  bool settled = Zone.GetRetiring() == 0 && Zone.GetPlaced() <= 5;
  if (!settled)
  {
   if (now - PhaseAt > 15) { Check(false, string.Format("shrink settled in 15 s (placed %1 retiring %2)", Zone.GetPlaced(), Zone.GetRetiring())); Finish("shrink"); }
   return;
  }
  if (now - PhaseAt < 3) return;
  // The casualty still counts as placed: five places, four living.
  Check(Zone.GetPlaced() == 5 && Zone.GetLivingCount() == 4 && Zone.GetTrackedCount() == 4, string.Format("crowd shrank to five places (living %1, tracked %2)", Zone.GetLivingCount(), Zone.GetTrackedCount()));
  Check(MaxRemovedPerFrame <= EAU_Director.DELETES_PER_TICK, string.Format("bounded shrink: max %1 removed in one frame", MaxRemovedPerFrame));
  Remember();
  MaxRemovedPerFrame = 0;
  Zone.SetSetting(EAU_ProtestZone.KEY_ENABLED, 0);
  Advance(8);
 }

 void Disabled(float now)
 {
  TrackRemovals();
  if (Present(Ids) > 0 || Zone.IsRunning())
  {
   if (now - PhaseAt > 15) { Check(false, string.Format("disable cleaned up in 15 s (%1 left)", Present(Ids))); Finish("disable"); }
   return;
  }
  Check(true, string.Format("disable removed crowd, group and sound in %1 s", now - PhaseAt));
  Check(MaxRemovedPerFrame <= EAU_Director.DELETES_PER_TICK, string.Format("bounded disable: max %1 removed in one frame", MaxRemovedPerFrame));
  Check(Zone.GetGroup() == null && Zone.GetSound() == null && Zone.GetTrackedCount() == 0, "zone holds nothing while Off");
  Check(!Zone.IsAsleep() && Zone.GetCachedCount() == 0 && Zone.GetRunState() == EAU_ProtestZone.STATE_OFF, "Off forgets the crowd and reports Off");
  Zone.SetSetting(EAU_ProtestZone.KEY_CROWD_MIN, 10);
  Zone.SetSetting(EAU_ProtestZone.KEY_CROWD_MAX, 15);
  Zone.SetSetting(EAU_ProtestZone.KEY_ENABLED, 1);
  Advance(9);
 }

 void Regathered(float now)
 {
  if (!Zone.IsSpawnFinished() || !Zone.GetSound())
  {
   if (now - PhaseAt > 45) { Check(false, "crowd regathered after re-enable"); Finish("regather"); }
   return;
  }
  Check(Zone.GetLivingCount() >= 10 && Zone.GetLivingCount() <= 15, string.Format("fresh crowd of %1 after re-enable", Zone.GetLivingCount()));
  Remember();
  MaxRemovedPerFrame = 0;
  // GM deletion of the module: the director must remove what the zone owned.
  SCR_EntityHelper.DeleteEntityAndChildren(Zone);
  Advance(10);
 }

 void Deleted(float now)
 {
  TrackRemovals();
  if (Present(Ids) > 0 || EAU_Director.GetOrphanCount() > 0)
  {
   if (now - PhaseAt > 15) { Check(false, string.Format("module deletion cleaned up in 15 s (%1 left)", Present(Ids))); Finish("delete"); }
   return;
  }
  Check(Zone == null, "zone entity deleted");
  Check(true, string.Format("module deletion removed crowd, group and sound in %1 s", now - PhaseAt));
  Check(MaxRemovedPerFrame <= EAU_Director.DELETES_PER_TICK, string.Format("bounded orphan cleanup: max %1 removed in one frame", MaxRemovedPerFrame));
  Check(EAU_Director.GetZoneCount() == 0, "director forgot the deleted zone");
  Finish("complete");
 }
}

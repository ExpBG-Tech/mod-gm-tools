// TEST ONLY. EXPBG Civil Protest Zone (addon/ambient-unrest) native server fixture.
// pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/EAU_ProtestGameplay.c -TimeoutSeconds 360 -OrchestratorSlotGranted
// The runner copies this file to EXPG_GarrisonGameplay.c, so the driver class name is fixed.
// The runner's own verdict checks Garrison evidence and reports FAIL for this fixture;
// accept on exactly one "[EXPG EAU RESULT] checks=N failures=0 reason=complete" line,
// no script errors, and "Game destroyed" in the retained logs.
// Real zone prefab, public SetSetting/RestoreSettings, real director ticks, native Kill.
// No players, no GM UI, no client: replication, JIP and the visible look of the
// gestures need the separate client test.
class EXPG_GarrisonGameplayClass : GenericEntityClass {}
class EXPG_GarrisonGameplay : GenericEntity
{
 static const float FIXTURE_SECONDS = 300;
 static const ResourceName ZONE_PREFAB = "{B0D011A7D85EC526}PrefabsEditable/EXPAU/EAU_ProtestZone.et";
 EAU_ProtestZone Zone;
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
  MaxRemovedPerFrame = Math.Max(MaxRemovedPerFrame, removed);
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

 override void EOnFrame(IEntity owner, float timeSlice)
 {
  if (Finished) return;
  float now = Now();
  if (now - Started > FIXTURE_SECONDS) { Check(false, string.Format("phase %1 finished before the fixture deadline", Phase)); Finish("timeout"); return; }
  if (Phase == 0) { if (now >= PhaseAt) Setup(); return; }
  if (Phase == 1) { Gathered(now); return; }
  if (Phase == 2) { Hold(now); return; }
  if (Phase == 3) { AfterCasualty(now); return; }
  if (Phase == 4) { Shrink(now); return; }
  if (Phase == 5) { Disabled(now); return; }
  if (Phase == 6) { Regathered(now); return; }
  if (Phase == 7) { Deleted(now); return; }
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
  // Persistence batch contract: complete valid batches only.
  Check(!Zone.RestoreSettings({0, 8, 12, 11, 0}), "restore refuses maximum below minimum");
  Check(!Zone.RestoreSettings({0, 99, 10, 15, 0}), "restore refuses an out-of-range radius");
  Check(!Zone.RestoreSettings({0, 8, 10}), "restore refuses a partial batch");
  Check(Zone.RestoreSettings({0, 8, 10, 15, 1}), "restore accepts a valid batch");
  Zone.SetSetting(EAU_ProtestZone.KEY_CROWD_MIN, 20);
  Check(Zone.GetSetting(EAU_ProtestZone.KEY_CROWD_MAX) == 20, "raising the minimum lifts the maximum");
  Zone.SetSetting(EAU_ProtestZone.KEY_CROWD_MAX, 15);
  Check(Zone.GetSetting(EAU_ProtestZone.KEY_CROWD_MIN) == 15, "lowering the maximum lowers the minimum");
  Zone.SetSetting(EAU_ProtestZone.KEY_CROWD_MIN, 10);
  Zone.SetSetting(EAU_ProtestZone.KEY_ENABLED, 1);
  Advance(1);
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
  Check(Target >= 10 && Target <= 15, "crowd size rolled inside 10-15");
  Check(Zone.GetPlaced() == Target && Zone.GetLivingCount() == Target && actors.Count() == Target, "every protester placed and alive");
  Check(group != null && group.GetAgentsCount() == Target, "one group holds the whole crowd");
  Check(group && group.GetFaction() && group.GetFaction().GetFactionKey() == EAU_ProtestZone.CIV_FACTION, "group is CIV");
  Check(group && EAU_Director.ReservesGroup(group), "director reserves the group");
  if (EBG_CacheManager.Instance) Check(EBG_CacheManager.Instance.IsReserved(group), "Unit Caching treats the crowd group as reserved");
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
  Check(sound.Enabled == 1 && sound.Recording == 0 && sound.Loop == 1 && sound.Range == Zone.AudibleRange(), string.Format("angry crowd emitter on, looping, range %1", sound.Range));
  Check(sound.DebugEnabled == 1, "emitter follows zone debug");
  // Informational: StopTracking should leave no persistence identity behind.
  PersistenceSystem persistence = PersistenceSystem.GetInstance();
  if (persistence) PrintFormat("[EXPG EAU PERSISTENCE] groupIdNull=%1 soundIdNull=%2 firstActorIdNull=%3", persistence.GetId(group).IsNull(), persistence.GetId(sound).IsNull(), actors.IsEmpty() || persistence.GetId(actors[0]).IsNull());
  Remember();
  Advance(2);
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
  // Native kill of one protester: a casualty is never replaced.
  if (actors.IsEmpty()) { Check(false, "a protester is left to kill"); Finish("hold"); return; }
  SCR_ChimeraCharacter victim = actors[0];
  CasualtyId = victim.GetID();
  SCR_DamageManagerComponent damage = victim.GetDamageManager();
  if (damage) damage.Kill(Instigator.CreateInstigator(null));
  Advance(3);
 }

 void AfterCasualty(float now)
 {
  if (now - PhaseAt < 5) return;
  SCR_ChimeraCharacter victim = SCR_ChimeraCharacter.Cast(GetGame().GetWorld().FindEntityByID(CasualtyId));
  Check(victim == null || !Alive(victim), "native kill took effect");
  Check(Zone.GetPlaced() == Target && Zone.GetLivingCount() == Target - 1, string.Format("casualty not replaced (placed %1 living %2)", Zone.GetPlaced(), Zone.GetLivingCount()));
  Zone.SetSetting(EAU_ProtestZone.KEY_CROWD_MAX, 5);
  Check(Zone.GetSetting(EAU_ProtestZone.KEY_CROWD_MIN) == 5, "crowd size 5-5 applied");
  MaxRemovedPerFrame = 0;
  Advance(4);
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
  Check(Zone.GetPlaced() == 5 && Zone.GetTrackedCount() == 5, string.Format("crowd shrank to five (tracked %1)", Zone.GetTrackedCount()));
  Check(MaxRemovedPerFrame <= EAU_Director.DELETES_PER_TICK, string.Format("bounded shrink: max %1 removed in one frame", MaxRemovedPerFrame));
  Remember();
  MaxRemovedPerFrame = 0;
  Zone.SetSetting(EAU_ProtestZone.KEY_ENABLED, 0);
  Advance(5);
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
  Zone.SetSetting(EAU_ProtestZone.KEY_CROWD_MIN, 10);
  Zone.SetSetting(EAU_ProtestZone.KEY_CROWD_MAX, 15);
  Zone.SetSetting(EAU_ProtestZone.KEY_ENABLED, 1);
  Advance(6);
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
  Advance(7);
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

// TEST ONLY. Garrison guards hold their posts. Live report 2026-10-07 (0.1.9, ACE,
// EXPBG RO AI, own dedicated server): "they leave their spots and run around" and
// "caching did not work again for garrison units". Cause: any one guard displaced
// more than 1.5 m (ACE ragdoll or unconsciousness, a blast, a push through a
// doorway, a carry, a Game Master move), possessed, regrouped or deleted released
// the whole squad silently; released soldiers are ordinary AI and never garrison
// cached again.
// pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/EXPG_HoldGameplay.c -ExpectResult '\[EXPG HOLD RESULT\] checks=[1-9]\d* failures=0 guards=4 fixed=[34] pushHeld=1 knockedOut=1 knockHeld=1 othersKept=1 deleted=1 respawned=0 fullRestored=3 simRestored=3 premature=0 released=1 reasons=1 reason=completed' -TimeoutSeconds 600 -OrchestratorSlotGranted
// The runner copies this file to EXPG_GarrisonGameplay.c; the class names are fixed.
// One US fire team garrisons the vanilla House_Town_E_2I01 on the generated GM_Eden
// world through the production Add Garrison server calls (CanFit, fresh roster,
// AdoptFresh; caching Off at first). Cases, in order:
//  1. One fixed guard is moved 2-3 m by a Game Master transform (editable
//     SetTransform) and another is knocked unconscious (SetUnconscious, the Scenario
//     Framework's call) for 10 s. No release: the moved guard holds where he came to
//     rest (his post moves to him, controls bound, he never walks back), the knocked
//     guard is bound again within 0.6 m of his post after waking and never left 2 m
//     of where he stood, and every other fixed guard keeps his exact post (same node,
//     never re-anchored, within 0.5 m, controls bound).
//  2. A third fixed guard is deleted (editable Delete): he is marked dead and never
//     respawned; the others keep their posts.
//  3. Cache mode Full (the fixture modset has no CDF): the garrison Full-caches (its
//     soldiers removed) and wakes when set to Off; every survivor is restored within
//     0.5 m of where he slept and, if fixed, within 0.6 m of his (anchored) post with
//     controls bound; the deleted guard stays dead, three soldiers in the squad.
//     Then the same with Simulation (original soldiers kept, AI paused).
//  4. The Game Master's Release: exactly one release, and it names its reason (the
//     release hook, "[EXPG Garrison] group=... released (Released by the Game
//     Master)", status "Released: Released by the Game Master"). Any release before
//     it, or any refused / retained-as-normal-AI status, fails the run.
// No players, GM UI, ACE, real blasts or possession (that needs a player). Every
// "Cache held: ..." status is logged as [EXPG HOLD STATUS] for diagnosis.
// Deadline 540 s.
class EXPG_HoldGuard
{
 ref EXPG_GarrisonMember Member;
 // Where he was placed, his node and post at placement, and where he last stood awake.
 vector Placed;
 int PlacedNode;
 vector Post;
 vector LastAwake;
}

// Release and status observations; any refusal or retention fails the run.
modded class EXPG_GarrisonRecord
{
 static int EXPG_TestRefusals;
 static int EXPG_TestReleases;
 static string EXPG_TestReleaseReason;
 override void Report(string message)
 {
  bool changed = Status != message;
  super.Report(message);
  if (!changed) return;
  if (message.Contains("ache held")) PrintFormat("[EXPG HOLD STATUS] group=%1 status=%2", Group, message);
  if (message.Contains("retained") || message.Contains("refused") || message.Contains("releasing survivors") || message.Contains("cannot") || message.Contains("unavailable"))
  {
   EXPG_TestRefusals++;
   PrintFormat("[EXPG HOLD REFUSAL] group=%1 status=%2", Group, message);
  }
 }
 override void FinishRelease()
 {
  EXPG_TestReleases++;
  EXPG_TestReleaseReason = ReleaseReason;
  PrintFormat("[EXPG HOLD RELEASE] group=%1 reason=%2 status=%3", Group, ReleaseReason, Status);
  super.FinishRelease();
 }
}

modded class EXPG_GarrisonMember
{
 static int EXPG_TestAnchors;
 override void Anchor(vector at)
 {
  super.Anchor(at);
  EXPG_TestAnchors++;
  int id = -1;
  if (CacheMember) id = CacheMember.Id;
  PrintFormat("[EXPG HOLD ANCHOR] member=%1 at=%2 node=%3", id, at, NodeIndex);
 }
}

class EXPG_GarrisonGameplayClass : GenericEntityClass {}
class EXPG_GarrisonGameplay : GenericEntity
{
 static const float FIXTURE_SECONDS = 540;
 static const int SQUAD = 4;
 static const int RELEASE_PHASE = 12;
 IEntity Structure;
 EXPG_GarrisonManager Manager;
 EXPG_BuildingPlan Plan;
 SCR_AIGroup Group;
 EXPG_GarrisonRecord Record;
 ref array<ref EXPG_HoldGuard> Guards = {};
 // Fixed guards that nothing touches (the deleted one until his deletion).
 ref array<ref EXPG_HoldGuard> Untouched = {};
 ref EXPG_HoldGuard Pushed;
 ref EXPG_HoldGuard Knocked;
 ref EXPG_HoldGuard Deleted;
 vector PushTarget;
 ObserversSystem Observers;
 int ObserverKey;
 int Phase;
 int Checks;
 int Failures;
 int FixedCount;
 int PushHeld;
 int PushWalked;
 int KnockedOut;
 int KnockHeld;
 int KeepViolations;
 int OthersKept;
 int DeletedCount;
 int Respawned;
 int FullRestored;
 int SimRestored;
 int Premature;
 int Released;
 int Reasons;
 bool Woken;
 float Started;
 float Next;
 float PhaseStarted;
 bool Finished;
 vector Point = "4773.46 0 7094.57";

 void EXPG_GarrisonGameplay(IEntitySource src, IEntity parent) { SetEventMask(EntityEvent.INIT | EntityEvent.FRAME); }
 float Now() { return GetGame().GetWorld().GetWorldTime() * 0.001; }
 override void EOnInit(IEntity owner)
 {
  if (!Replication.IsServer()) { ClearEventMask(EntityEvent.FRAME); return; }
  Started = Now();
  Next = Started + 15;
  ObserverKey = "EXPG_Hold.Observer".Hash();
  PrintFormat("[EXPG HOLD BEGIN] squad=%1 deadline=%2 connectedPlayer=0", SQUAD, FIXTURE_SECONDS);
 }
 bool Check(bool value, string label)
 {
  Checks++;
  if (!value) Failures++;
  PrintFormat("[EXPG HOLD CHECK] pass=%1 %2", value, label);
  return value;
 }
 void RemoveObserver()
 {
  if (Observers && GetGame() && GetGame().GetWorld() && GetGame().GetWorld().FindSystem(ObserversSystem) == Observers)
   Observers.RemoveObserverSP(ObserverKey);
 }
 void ~EXPG_GarrisonGameplay() { RemoveObserver(); }
 void Finish(string reason)
 {
  if (Finished) return;
  Finished = true;
  RemoveObserver();
  ClearEventMask(EntityEvent.FRAME);
  // The record is gone once released (the manager drops it): count the guards
  // collected at placement.
  int guards = Guards.Count();
  string first = string.Format("checks=%1 failures=%2 guards=%3 fixed=%4 pushHeld=%5 knockedOut=%6 knockHeld=%7 othersKept=%8 deleted=%9", Checks, Failures, guards, FixedCount, PushHeld, KnockedOut, KnockHeld, OthersKept, DeletedCount);
  string second = string.Format("respawned=%1 fullRestored=%2 simRestored=%3 premature=%4 released=%5 reasons=%6 reason=%7", Respawned, FullRestored, SimRestored, Premature, Released, Reasons, reason);
  PrintFormat("[EXPG HOLD RESULT] %1 %2", first, second);
  GetGame().RequestClose();
 }
 void Advance(int phase)
 {
  Phase = phase;
  PhaseStarted = Now();
  string status = "none";
  if (Record) status = Record.Status;
  PrintFormat("[EXPG HOLD PHASE] phase=%1 elapsed=%2 status=%3", Phase, Now() - Started, status);
 }
 bool Waited(float seconds, string label)
 {
  if (Now() - PhaseStarted <= seconds) return false;
  Check(false, label);
  LogGuards("timeout");
  Finish("phase " + Phase.ToString());
  return true;
 }
 EntitySpawnParams Params(vector point, float elevation = 0)
 {
  EntitySpawnParams spawn = new EntitySpawnParams();
  spawn.TransformMode = ETransformMode.WORLD;
  Math3D.AnglesToMatrix(vector.Zero, spawn.Transform);
  point[1] = GetGame().GetWorld().GetSurfaceY(point[0], point[2]) + elevation;
  spawn.Transform[3] = point;
  return spawn;
 }
 vector EXPG_Origin(IEntity entity)
 {
  vector origin;
  if (entity) origin = entity.GetOrigin();
  return origin;
 }
 // Durable Full (0.1.11): a Full-cached garrison has no squad until it wakes; the
 // record recreates it and Group follows Record.Group (EOnFrame).
 bool Active()
 {
  if (!Record || Record.Finished || Record.ReleaseRequested) return false;
  if (Record.Full && !Record.Group) return true;
  return Group && Group.EXPG_Active && Manager.Find(Group) == Record;
 }
 bool Awake() { return Record && !Record.Full && !Record.Simulation; }
 bool FullAsleep() { return Record && Record.Full && Record.Full.GetState() == EBG_FullGroupPhase.CACHED; }
 bool SimulationAsleep() { return Record && !Record.Full && Record.Simulation && Record.Simulation.Suspended; }
 // A Full-cached garrison has no squad to edit: the record's setting stands in for it.
 void SetCacheMode(int mode)
 {
  if (Record && Record.Group) Record.Group.EXPG_SetSetting(0, mode);
  else if (Record) Record.CacheMode = mode;
 }
 int Agents()
 {
  if (!Group) return 0;
  return Group.GetAgentsCount();
 }
 bool Living(EXPG_HoldGuard guard)
 {
  SCR_ChimeraCharacter actor = guard.Member.CacheMember.Entity;
  return !guard.Member.CacheMember.Dead && actor && !EXPG_GarrisonManager.IsDeadActor(actor);
 }
 int LivingCount()
 {
  int count = 0;
  foreach (EXPG_HoldGuard guard : Guards) if (Living(guard)) count++;
  return count;
 }
 void LogGuards(string stage)
 {
  foreach (EXPG_HoldGuard guard : Guards)
  {
   EXPG_GarrisonMember member = guard.Member;
   SCR_ChimeraCharacter actor = member.CacheMember.Entity;
   PrintFormat("[EXPG HOLD GUARD] stage=%1 member=%2 fixed=%3 dead=%4 post=%5 node=%6 bound=%7 actor=%8 placed=%9", stage, member.CacheMember.Id, member.Fixed, member.CacheMember.Dead, member.PostPoint(), member.NodeIndex, member.Post != null || member.Patrol != null, EXPG_Origin(actor), guard.Placed);
  }
  if (Record) PrintFormat("[EXPG HOLD RECORD] stage=%1 active=%2 members=%3 agents=%4 status=%5", stage, Active(), Record.Members.Count(), Agents(), Record.Status);
 }
 // Awake, every living guard in the squad and under garrison control.
 bool AllAwake()
 {
  if (!Active() || !Awake()) return false;
  foreach (EXPG_HoldGuard guard : Guards)
  {
   EXPG_GarrisonMember member = guard.Member;
   if (member.CacheMember.Dead) continue;
   SCR_ChimeraCharacter actor = member.CacheMember.Entity;
   if (!actor || actor.GetCharacterGroup() != Group) return false;
   if (member.Fixed && !member.Post) return false;
   if (!member.Fixed && !member.Patrol) return false;
  }
  return true;
 }
 // Untouched fixed guards: same node and post, never re-anchored, within 0.5 m of
 // where they were placed, controls bound. Returns how many broke that.
 int UntouchedMoved(string stage)
 {
  int moved = 0;
  foreach (EXPG_HoldGuard guard : Untouched)
  {
   EXPG_GarrisonMember member = guard.Member;
   SCR_ChimeraCharacter actor = member.CacheMember.Entity;
   bool kept = Living(guard) && member.Post && member.NodeIndex == guard.PlacedNode && vector.DistanceSq(member.PostPoint(), guard.Post) < 0.0001 && vector.DistanceXZ(actor.GetOrigin(), guard.Placed) <= 0.5;
   if (kept) continue;
   moved++;
   PrintFormat("[EXPG HOLD MOVED] stage=%1 member=%2 node=%3/%4 post=%5 placed=%6 actor=%7 bound=%8", stage, member.CacheMember.Id, member.NodeIndex, guard.PlacedNode, member.PostPoint(), guard.Placed, EXPG_Origin(actor), member.Post != null);
  }
  return moved;
 }
 // A free indoor floor node on the pushed guard's storey, 2-3 m (else 2-4 m) from
 // his post and 1.6 m from every other guard's post, with room for a body.
 int FindPushSpot(float nearest, float farthest)
 {
  SCR_ChimeraCharacter actor = Pushed.Member.CacheMember.Entity;
  foreach (int index, EXPG_BuildingNode node : Plan.Nodes)
  {
   if (!node.Reachable || !node.Interior || !node.IndoorWalk || node.Stair || node.DoorBlock) continue;
   if (Math.AbsFloat(node.Position[1] - Pushed.Post[1]) > 0.4) continue;
   float distance = vector.DistanceXZ(node.Position, Pushed.Post);
   if (distance < nearest || distance > farthest) continue;
   bool spaced = true;
   foreach (EXPG_HoldGuard other : Guards)
   {
    if (other == Pushed) continue;
    if (Math.AbsFloat(other.Post[1] - node.Position[1]) < 2 && vector.DistanceXZ(other.Post, node.Position) < 1.6) spaced = false;
    vector at = EXPG_Origin(other.Member.CacheMember.Entity);
    if (Math.AbsFloat(at[1] - node.Position[1]) < 2 && vector.DistanceXZ(at, node.Position) < 1.6) spaced = false;
   }
   if (!spaced) continue;
   if (!Plan.Supported(node.Position, 0.2, actor) || !Plan.ClearBody(node.Position, node.Position + "0 0.01 0", actor)) continue;
   return index;
  }
  return -1;
 }
 // A Game Master move: the editable transform, keeping his heading.
 bool MoveGuard(SCR_ChimeraCharacter actor, vector point)
 {
  vector transform[4];
  actor.GetWorldTransform(transform);
  transform[3] = point;
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(actor);
  if (editable) return editable.SetTransform(transform);
  actor.SetWorldTransform(transform);
  return true;
 }
 void TrackAwake()
 {
  foreach (EXPG_HoldGuard guard : Guards)
  {
   if (Living(guard)) guard.LastAwake = guard.Member.CacheMember.Entity.GetOrigin();
  }
 }
 // Survivors back where they slept and, if fixed, on their (anchored) posts with
 // controls bound; the deleted guard stays dead and nobody is replenished.
 int CheckRestored(string label)
 {
  int restored = 0;
  foreach (EXPG_HoldGuard guard : Guards)
  {
   EXPG_GarrisonMember member = guard.Member;
   if (guard == Deleted)
   {
    Check(member.CacheMember.Dead, label + ": the deleted guard stays dead");
    continue;
   }
   if (member.CacheMember.Dead) continue;
   SCR_ChimeraCharacter actor = member.CacheMember.Entity;
   bool back = actor && actor.GetCharacterGroup() == Group;
   if (back && member.Fixed) back = member.Post && vector.DistanceXZ(actor.GetOrigin(), guard.LastAwake) <= 0.5 && vector.DistanceXZ(actor.GetOrigin(), member.PostPoint()) <= 0.6;
   else if (back) back = member.Patrol != null;
   PrintFormat("[EXPG HOLD RESTORED] cycle=%1 member=%2 fixed=%3 back=%4 actor=%5 slept=%6 post=%7", label, member.CacheMember.Id, member.Fixed, back, EXPG_Origin(actor), guard.LastAwake, member.PostPoint());
   if (back) restored++;
  }
  int living = 0;
  foreach (EXPG_GarrisonMember counted : Record.Members) if (!counted.CacheMember.Dead) living++;
  if (Group.GetAgentsCount() > living || Record.Members.Count() != SQUAD) Respawned++;
  Check(restored == SQUAD - 1, string.Format("%1: every survivor restored where he slept, on his post: %2 of %3", label, restored, SQUAD - 1));
  Check(Respawned == 0 && Group.GetAgentsCount() == SQUAD - 1, string.Format("%1: nobody respawned or replenished: agents=%2 members=%3", label, Group.GetAgentsCount(), Record.Members.Count()));
  return restored;
 }
 bool AddSquad()
 {
  string reason;
  bool fits = Manager.CanFit(Structure, SQUAD, reason);
  if (!Check(fits && reason.IsEmpty() && !Manager.HasGarrison(Structure), "Add Garrison accepted by CanFit on an empty house: " + reason)) return false;
  ResourceName team = "{84E5BBAB25EA23E5}Prefabs/Groups/BLUFOR/Group_US_FireTeam.et";
  Resource teamResource = Resource.Load(team);
  IEntity spawnedTeam = GetGame().SpawnEntityPrefab(teamResource, GetGame().GetWorld(), Params(Point + "22 0 4", 0.3));
  Group = SCR_AIGroup.Cast(spawnedTeam);
  bool fresh = Group && Group.EXPG_BeginFreshRoster(SQUAD);
  bool adopted = fresh && Manager.AdoptFresh(Group, Structure, 0, SQUAD);
  if (!Check(adopted, "freshly spawned fire team adopted by the house")) return false;
  // Caching Off until case 3: no players, so the garrison would sleep otherwise.
  Group.EXPG_CacheMode = 0;
  Record = Manager.Find(Group);
  return Check(Record != null, "garrison record created");
 }
 // Placed: every guard in his own group; pick the pushed, knocked and deleted guards
 // among the fixed ones and remember every post.
 bool CollectGuards()
 {
  if (!Check(Active() && Record.Members.Count() == SQUAD && Group.GetAgentsCount() == SQUAD, "all four soldiers placed in their own group")) return false;
  foreach (EXPG_GarrisonMember member : Record.Members)
  {
   EXPG_HoldGuard guard = new EXPG_HoldGuard();
   guard.Member = member;
   guard.Placed = EXPG_Origin(member.CacheMember.Entity);
   guard.PlacedNode = member.NodeIndex;
   guard.Post = member.PostPoint();
   guard.LastAwake = guard.Placed;
   Guards.Insert(guard);
   if (!member.Fixed) continue;
   FixedCount++;
   if (!Pushed) Pushed = guard;
   else if (!Knocked) Knocked = guard;
   else
   {
    if (!Deleted) Deleted = guard;
    Untouched.Insert(guard);
   }
  }
  LogGuards("placed");
  return Check(FixedCount >= 3 && Pushed && Knocked && Deleted, string.Format("at least three fixed guards: %1", FixedCount));
 }
 override void EOnFrame(IEntity owner, float timeSlice)
 {
  if (Finished || Now() < Next) return;
  Next = Now() + 0.5;
  if (Record && Record.Group) Group = Record.Group;
  if (Now() - Started > FIXTURE_SECONDS) { Check(false, string.Format("%1 second deadline; last phase %2", FIXTURE_SECONDS, Phase)); LogGuards("deadline"); Finish("timeout"); return; }
  if (EXPG_GarrisonRecord.EXPG_TestRefusals > 0) { Check(false, "no garrison status said refused, retained or cannot"); Finish("refusal"); return; }
  if (Phase < RELEASE_PHASE && (EXPG_GarrisonRecord.EXPG_TestReleases > 0 || (Record && Record.Ready && !Active())))
  {
   Premature = 1;
   Check(false, "no release before the Game Master's Release: " + EXPG_GarrisonRecord.EXPG_TestReleaseReason);
   LogGuards("premature");
   Finish("premature release");
   return;
  }
  if (Phase == 0)
  {
   array<int> players = {};
   GetGame().GetPlayerManager().GetPlayers(players);
   Manager = EXPG_GarrisonManager.Get();
   Observers = ObserversSystem.Cast(GetGame().GetWorld().FindSystem(ObserversSystem));
   if (!Check(Manager && Observers && players.IsEmpty() && EBG_CacheZone.Zones.IsEmpty(), "isolated server with no connected players or Unit Caching zones")) { Finish("setup"); return; }
   Observers.InsertObserverSP(ObserverKey, Point[0], Point[2], null);
   ResourceName house = "{38A5F3E4578087AB}Prefabs/Structures/Houses/Town/House_Town_E_2I01/House_Town_E_2I01.et";
   Resource houseResource = Resource.Load(house);
   Structure = GetGame().SpawnEntityPrefab(houseResource, GetGame().GetWorld(), Params(Point));
   if (!Check(SCR_DestructibleBuildingEntity.Cast(Structure) != null, "vanilla two-storey town house spawned")) { Finish("building"); return; }
   Manager.Prepare(Structure);
   Advance(1);
   return;
  }
  if (Phase == 1)
  {
   Plan = Manager.FindPlan(Structure);
   if (!Plan || !Plan.Done) { Waited(150, "production analysis finished within 150 seconds"); return; }
   PrintFormat("[EXPG HOLD PLAN] nodes=%1 slots=%2 error=%3", Plan.Nodes.Count(), Plan.Slots.Count(), Plan.Error);
   if (!Check(Plan.Error.IsEmpty() && Plan.Slots.Count() >= SQUAD, "production plan has room for the fire team")) { Finish("plan"); return; }
   if (!AddSquad()) { Finish("add"); return; }
   Advance(2);
   return;
  }
  if (Phase == 2)
  {
   if (!Record.Ready) { Waited(30, "fire team placed within 30 seconds: " + Record.Status); return; }
   if (Now() - PhaseStarted < 5) return;
   if (!CollectGuards()) { Finish("placement"); return; }
   Advance(3);
   return;
  }
  if (Phase == 3)
  {
   // Case 1: a Game Master moves one guard 2-3 m; another is knocked unconscious.
   int spot = FindPushSpot(2.2, 3.0);
   if (spot < 0) spot = FindPushSpot(2.0, 4.0);
   if (!Check(spot >= 0, "a free indoor floor spot 2-4 m from the first fixed guard's post")) { Finish("push spot"); return; }
   PushTarget = Plan.Nodes[spot].Position;
   SCR_ChimeraCharacter pushedActor = Pushed.Member.CacheMember.Entity;
   if (!Check(MoveGuard(pushedActor, PushTarget), string.Format("guard %1 moved %2 m by a Game Master transform", Pushed.Member.CacheMember.Id, vector.DistanceXZ(Pushed.Post, PushTarget)))) { Finish("push"); return; }
   SCR_ChimeraCharacter knockedActor = Knocked.Member.CacheMember.Entity;
   knockedActor.GetCharacterController().SetUnconscious(true);
   Advance(4);
   return;
  }
  if (Phase == 4)
  {
   SCR_ChimeraCharacter pushedNow = Pushed.Member.CacheMember.Entity;
   SCR_ChimeraCharacter knockedNow = Knocked.Member.CacheMember.Entity;
   if (!pushedNow || !knockedNow) { Check(false, "the moved and the knocked guard stay alive"); Finish("case 1"); return; }
   if (knockedNow.GetCharacterController().IsUnconscious()) KnockedOut = 1;
   if (!Woken && Now() - PhaseStarted >= 10)
   {
    Woken = true;
    knockedNow.GetCharacterController().SetUnconscious(false);
   }
   if (Now() - PhaseStarted >= 3 && vector.DistanceXZ(pushedNow.GetOrigin(), PushTarget) > 0.4)
   {
    PushWalked++;
    PrintFormat("[EXPG HOLD WALKED] member=%1 actor=%2 target=%3", Pushed.Member.CacheMember.Id, pushedNow.GetOrigin(), PushTarget);
   }
   KeepViolations += UntouchedMoved("case 1");
   if (Now() - PhaseStarted < 25 || knockedNow.GetCharacterController().IsUnconscious())
   {
    Waited(60, "the knocked guard woke within 60 seconds");
    return;
   }
   EXPG_GarrisonMember pushedMember = Pushed.Member;
   bool pushedBound = pushedMember.Fixed && pushedMember.Post != null;
   bool pushedPost = vector.DistanceXZ(pushedMember.PostPoint(), pushedNow.GetOrigin()) <= 0.6 && vector.DistanceXZ(pushedMember.PostPoint(), Pushed.Post) >= 1.5;
   if (Check(pushedBound && pushedPost && PushWalked == 0, string.Format("the moved guard holds where he came to rest: bound=%1 post=%2 walked=%3 anchors=%4", pushedBound, pushedMember.PostPoint(), PushWalked, EXPG_GarrisonMember.EXPG_TestAnchors))) PushHeld = 1;
   Check(KnockedOut == 1, "the knocked guard was unconscious");
   EXPG_GarrisonMember knockedMember = Knocked.Member;
   bool knockedBound = knockedMember.Fixed && knockedMember.Post != null && !knockedMember.CacheMember.Dead;
   bool knockedPost = vector.DistanceXZ(knockedMember.PostPoint(), knockedNow.GetOrigin()) <= 0.6 && vector.DistanceXZ(knockedNow.GetOrigin(), Knocked.Placed) <= 2.0;
   if (Check(knockedBound && knockedPost, string.Format("the knocked guard is bound again on his post after waking: bound=%1 post=%2 actor=%3", knockedBound, knockedMember.PostPoint(), knockedNow.GetOrigin()))) KnockHeld = 1;
   Check(KeepViolations == 0, string.Format("every other fixed guard kept his exact post: %1 violations", KeepViolations));
   LogGuards("case 1");
   Advance(5);
   return;
  }
  if (Phase == 5)
  {
   // Case 2: a Game Master deletes a third fixed guard.
   Untouched.RemoveItem(Deleted);
   SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(Deleted.Member.CacheMember.Entity);
   if (!Check(editable && editable.Delete(false, false), "a fixed guard deleted by the editor")) { Finish("delete"); return; }
   Advance(6);
   return;
  }
  if (Phase == 6)
  {
   KeepViolations += UntouchedMoved("case 2");
   if (!Deleted.Member.CacheMember.Dead || Now() - PhaseStarted < 5) { Waited(15, "the garrison noticed the deletion within 15 seconds"); return; }
   bool kept = Active() && Record.Members.Count() == SQUAD && Group.GetAgentsCount() == SQUAD - 1;
   if (Check(kept, string.Format("the squad stays garrisoned without the deleted guard: members=%1 agents=%2", Record.Members.Count(), Group.GetAgentsCount()))) DeletedCount = 1;
   if (Check(KeepViolations == 0, string.Format("the other fixed guards kept their exact posts through cases 1 and 2: %1 violations", KeepViolations))) OthersKept = 1;
   // Case 3: Full caching (no CDF in the fixture modset).
   Check(Manager.CacheModeInUse(Group) == 0, "caching was Off for cases 1 and 2");
   SetCacheMode(2);
   Advance(7);
   return;
  }
  if (Phase == 7)
  {
   if (!FullAsleep())
   {
    if (Awake()) TrackAwake();
    Waited(150, "garrison Full-cached within 150 seconds: " + Record.Status);
    return;
   }
   Check(!Record.Group && Record.Status.Contains("Full cached"), "the garrison Full-cached: its soldiers and squad are removed until wake");
   SetCacheMode(0);
   Advance(8);
   return;
  }
  if (Phase == 8)
  {
   if (!AllAwake()) { Waited(60, "every survivor restored from Full within 60 seconds: " + Record.Status); return; }
   FullRestored = CheckRestored("Full");
   TrackAwake();
   Advance(9);
   return;
  }
  if (Phase == 9)
  {
   if (!AllAwake()) { Check(false, "the restored garrison stays bound for five seconds"); LogGuards("full hold"); Finish("full hold"); return; }
   foreach (EXPG_HoldGuard settled : Guards)
   {
    if (!Living(settled) || !settled.Member.Fixed) continue;
    if (vector.DistanceXZ(settled.Member.CacheMember.Entity.GetOrigin(), settled.LastAwake) > 0.3) { Check(false, string.Format("restored guard %1 stays where he was restored", settled.Member.CacheMember.Id)); LogGuards("full drift"); Finish("full drift"); return; }
   }
   if (Now() - PhaseStarted < 5) return;
   SetCacheMode(1);
   Advance(10);
   return;
  }
  if (Phase == 10)
  {
   if (!SimulationAsleep())
   {
    if (Awake()) TrackAwake();
    Waited(150, "garrison Simulation-cached within 150 seconds: " + Record.Status);
    return;
   }
   TrackAwake();
   Check(Group.GetAgentsCount() == SQUAD - 1 && Record.Status.Contains("Simulation cached"), "the garrison Simulation-cached with its three original soldiers");
   SetCacheMode(0);
   Advance(11);
   return;
  }
  if (Phase == 11)
  {
   if (!AllAwake()) { Waited(30, "every survivor awake from Simulation within 30 seconds: " + Record.Status); return; }
   SimRestored = CheckRestored("Simulation");
   // Case 4: the Game Master's Release names its reason.
   Advance(RELEASE_PHASE);
   Manager.Release(Group);
   return;
  }
  if (Phase == RELEASE_PHASE)
  {
   if (Group.EXPG_Active || EXPG_GarrisonRecord.EXPG_TestReleases == 0) { Waited(10, "the Game Master's Release finished within 10 seconds"); return; }
   if (Check(EXPG_GarrisonRecord.EXPG_TestReleases == 1, string.Format("exactly one release: %1", EXPG_GarrisonRecord.EXPG_TestReleases))) Released = 1;
   bool named = EXPG_GarrisonRecord.EXPG_TestReleaseReason == "Released by the Game Master" && Group.EXPG_Status == "Released: Released by the Game Master";
   if (Check(named, string.Format("the release names its reason: reason='%1' status='%2'", EXPG_GarrisonRecord.EXPG_TestReleaseReason, Group.EXPG_Status))) Reasons = 1;
   Finish("completed");
  }
 }
}

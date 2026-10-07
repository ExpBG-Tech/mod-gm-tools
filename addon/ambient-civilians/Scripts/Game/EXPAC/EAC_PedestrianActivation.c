// Retained through commit/recovery and module replacement. No per-actor timer.
class EAC_PedestrianActivation
{
 ref EAC_ResidentClaim Claim;
 vector Position;
 float Deadline, SettleUntil;
 float ClearSince, LastClearSample;
 // Log hygiene (readiness plan S1). Console lines already emitted about this
 // resident's cache. A resident whose recovery keeps failing is polled every
 // tick, so the DebugLevel gate alone is not enough: the cap is what stops one
 // stuck resident owning the log even when an admin did ask for diagnostics.
 int CacheLogCount;
 float NextActivity;
 float LastVoiceAt, NextVoiceAt;
 int VoiceInterval = 15;
 float LastBusyAt;
 // LastBusyAt is the watchdog's own clock and RecoverIdle re-stamps it on both
 // its branches, so "now - LastBusyAt" is structurally clamped to
 // IdleRecoverySeconds and can never report a real gap. IdleSince is the honest
 // measurement of "how long has this resident had no order at all". Written only
 // by EAC_PedestrianSpawner.MonitorOrders; RecoverIdle must never touch it.
 float IdleSince;
 // When the forced-movement watchdog last intervened on this resident. The
 // ordinary maintenance batch runs immediately after that pass on the same tick
 // and would otherwise clear, through SetGoal, the walk backoff a refused forced
 // leg had just armed. Zero means "never forced".
 float ForcedAt;
 // Why this resident's last routine attempt was refused, and when. Campaign
 // evidence kept arriving as "activities=0 and the resident stood there", with
 // the refusing gate reconstructible only by reading the source next to a
 // timeline; the fixture v6d-danger-005152 sat on one resident refused every 20 s
 // for 90 s with no line naming the cause. Written at every refusing return in
 // EAC_PedestrianSpawner.TryActivity and cleared when a routine actually starts.
 // Diagnostics only: nothing reads these back for a decision.
 static const int REFUSED_NONE = 0;
 static const int REFUSED_BUSY = 1;      // already holds a routine, player-touched, or load-paused
 static const int REFUSED_LIMIT = 2;     // ActivityLimit is zero, or every slot is occupied
 static const int REFUSED_ALARM = 3;     // danger cooldown owns the actor
 static const int REFUSED_ARMING = 4;    // first arming: the stagger delay was just stamped
 static const int REFUSED_COOLDOWN = 5;  // NextActivity has not come round yet
 static const int REFUSED_RELEVANCE = 6; // no observer within wake range of the home
 static const int REFUSED_EMERGE = 7;    // an emergence ORDER is standing on the group
 static const int REFUSED_START = 8;     // the routine began and its own start refused
 int LastRefusal;
 float LastRefusalAt;

 void RecordRefusal(int reason, float now) { LastRefusal = reason; LastRefusalAt = now; }

 static string RefusalName(int reason)
 {
  if (reason == REFUSED_BUSY) return "busy";
  if (reason == REFUSED_LIMIT) return "activity_limit";
  if (reason == REFUSED_ALARM) return "alarm";
  if (reason == REFUSED_ARMING) return "arming";
  if (reason == REFUSED_COOLDOWN) return "cooldown";
  if (reason == REFUSED_RELEVANCE) return "relevance";
  if (reason == REFUSED_EMERGE) return "emerge_order";
  if (reason == REFUSED_START) return "start_refused";
  return "none";
 }

 // One call for a diagnostic line, so every reader formats it the same way.
 // Age is -1 when nothing has been refused yet, which reads differently from 0.
 string DescribeRefusal(float now)
 {
  string name = RefusalName(LastRefusal);
  float age = -1;
  if (LastRefusal != REFUSED_NONE) age = now - LastRefusalAt;
  string line = "last_refusal=" + name;
  line += " age=" + age.ToString();
  return line;
 }

 ref EAC_CivilianActivity Activity;
 bool Aborting, CleanupRequested, PlayerTouched;
 bool ExclusionRemoval, SessionRemoval;
 string FailureReason;
 bool SpawnedIndoors;
 ref EAC_PedestrianWalk Walking = new EAC_PedestrianWalk();
 ref EAC_CivilianShelter Shelter = new EAC_CivilianShelter();
 ref EAC_RoutineEmerge Emerge = new EAC_RoutineEmerge();
 protected SCR_CharacterControllerComponent m_Controller;
 protected SCR_CharacterDamageManagerComponent m_Damage;
 // Perf plan WP6 (civilians-b-07). The bound character's components, resolved
 // once in Bind with exactly the FindComponent calls the monitors made on every
 // visit, and dropped in Detach. A character's components never change while it
 // exists, so the Cached* accessors return the same objects those lookups did;
 // for any other actor, or a lookup that found nothing at Bind, they run the
 // FindComponent again. Only lookups are shared: every caller still reads every
 // live condition (life state, faction, agent, group) itself.
 protected IEntity m_CachedActor;
 protected CharacterControllerComponent m_CachedController;
 protected FactionAffiliationComponent m_CachedAffiliation;
 protected AIControlComponent m_CachedAIControl;
 protected AICharacterMovementComponent m_CachedMovement;

 CharacterControllerComponent CachedController(IEntity actor)
 {
  if (!actor)
   return null;
  if (actor == m_CachedActor && m_CachedController)
   return m_CachedController;
  return CharacterControllerComponent.Cast(actor.FindComponent(CharacterControllerComponent));
 }

 FactionAffiliationComponent CachedAffiliation(IEntity actor)
 {
  if (!actor)
   return null;
  if (actor == m_CachedActor && m_CachedAffiliation)
   return m_CachedAffiliation;
  return FactionAffiliationComponent.Cast(actor.FindComponent(FactionAffiliationComponent));
 }

 AIControlComponent CachedAIControl(IEntity actor)
 {
  if (!actor)
   return null;
  if (actor == m_CachedActor && m_CachedAIControl)
   return m_CachedAIControl;
  return AIControlComponent.Cast(actor.FindComponent(AIControlComponent));
 }

 AICharacterMovementComponent CachedMovement(IEntity actor)
 {
  if (!actor)
   return null;
  if (actor == m_CachedActor && m_CachedMovement)
   return m_CachedMovement;
  return AICharacterMovementComponent.Cast(actor.FindComponent(AICharacterMovementComponent));
 }

 bool Bind()
 {
  Detach();
  if (!Claim || !Claim.Character) return false;
  m_CachedActor = Claim.Character;
  m_CachedController = CharacterControllerComponent.Cast(Claim.Character.FindComponent(CharacterControllerComponent));
  m_CachedAffiliation = FactionAffiliationComponent.Cast(Claim.Character.FindComponent(FactionAffiliationComponent));
  m_CachedAIControl = AIControlComponent.Cast(Claim.Character.FindComponent(AIControlComponent));
  m_CachedMovement = AICharacterMovementComponent.Cast(Claim.Character.FindComponent(AICharacterMovementComponent));
  m_Controller = SCR_CharacterControllerComponent.Cast(Claim.Character.FindComponent(SCR_CharacterControllerComponent));
  m_Damage = SCR_CharacterDamageManagerComponent.Cast(Claim.Character.FindComponent(SCR_CharacterDamageManagerComponent));
  if (m_Controller)
  {
   m_Controller.m_OnLifeStateChanged.Insert(OnLifeState);
   m_Controller.m_OnControlledByPlayer.Insert(OnControl);
  }
  else PlayerTouched = true; // Cannot establish control history: protect until external removal.
  if (m_Damage) m_Damage.GetOnDamageStateChanged().Insert(OnDamageState);
  Observe();
  return m_Controller && m_Damage;
 }

 void Detach()
 {
  Shelter.Stop();
  Walking.Stop();
  Emerge.Stop();
  if (Activity) Activity.RequestStop(true);
  IdleSince = 0;
  if (m_Controller)
  {
   m_Controller.m_OnLifeStateChanged.Remove(OnLifeState);
   m_Controller.m_OnControlledByPlayer.Remove(OnControl);
  }
  if (m_Damage) m_Damage.GetOnDamageStateChanged().Remove(OnDamageState);
  m_Controller = null; m_Damage = null;
  m_CachedActor = null; m_CachedController = null; m_CachedAffiliation = null; m_CachedAIControl = null; m_CachedMovement = null;
 }

 void RecordDeath()
 {
  if (!Replication.IsServer() || !Claim) return;
  Shelter.Stop();
  Claim.Resident.Dead = true;
  EAC_SceneIndex.ReleaseAllFor(Claim.Resident.Id);
  if (Activity) Activity.RequestStop(true);
  IdleSince = 0;
  if (Claim.OptimizerMember) Claim.OptimizerMember.Dead = true;
  Walking.Stop();
  if (!Claim.Committed) Fail("death");
 }

 void Fail(string reason) { Aborting = true; FailureReason = reason; }

 void OnLifeState(ECharacterLifeState previous, ECharacterLifeState current, bool isJIP)
 {
  if (current == ECharacterLifeState.DEAD) RecordDeath();
 }

 void OnDamageState(EDamageState state)
 {
  if (state == EDamageState.DESTROYED) RecordDeath();
 }

 void OnControl(IEntity actor, bool controlled)
 {
  if (!Replication.IsServer() || !controlled || !Claim || actor != Claim.Character) return;
  PlayerTouched = true;
  EAC_SceneIndex.ReleaseAllFor(Claim.Resident.Id);
  Shelter.Stop();
  if (Activity) Activity.RequestStop(true);
  IdleSince = 0;
  if (Claim.OptimizerMember) Claim.OptimizerMember.WasPlayer = true;
  Walking.Stop();
  if (!Claim.Committed) Fail("player control");
 }

 void Observe()
 {
  if (!Claim) return;
  // Losing an empty pending group externally is also a terminal removal; a
  // later zone cleanup must not reopen that slot as an intentional despawn.
  if (!Claim.Group && !Claim.Cache && !CleanupRequested && !Aborting && !Claim.Character)
  {
   Claim.Resident.Removed = true;
   if (!Claim.Committed) Fail("external group removal");
  }
  if (Claim.Cache && Claim.OptimizerMember)
  {
   if (Claim.OptimizerMember.Dead) RecordDeath();
   if (Claim.OptimizerMember.WasPlayer) { PlayerTouched = true; Walking.Stop(); }
  }
  if (Claim.Character)
  {
   if ((m_Controller && m_Controller.GetLifeState() == ECharacterLifeState.DEAD) || (m_Damage && m_Damage.IsDestroyed())) RecordDeath();
   if (m_Controller && m_Controller.IsPlayerControlled()) OnControl(Claim.Character, true);
  }
  else if (SettleUntil > 0 && !CleanupRequested && (!Claim.Cache || !Claim.Cache.HasDeletionAttempted()))
  {
   if (!Claim.Resident.Dead) Claim.Resident.Removed = true;
   if (!Claim.Committed) Fail("external removal");
  }
 }
}

// Shared scheduler policy; no per-resident timer and no adoption of external actors.
class EAC_PedestrianCaching
{
 static const int MAX_CACHE_LOGS = 3;
 protected static ref array<ref SCR_PersistentDamageEffect> s_EffectScratch = {};
 protected static ref array<AIWaypoint> s_OrderScratch = {};

 static bool CanLogCache(EAC_AmbientModule module, EAC_PedestrianActivation activation)
 {
  if (!module || !activation || module.DebugLevel < 1 || activation.CacheLogCount >= MAX_CACHE_LOGS) return false;
  activation.CacheLogCount++;
  return true;
 }

 static bool KnownObservers(BaseWorld world, array<IEntity> observers)
 {
  if (!world || !observers || observers.Count() > 64) return false;
  foreach (IEntity observer : observers)
   if (!ChimeraCharacter.Cast(observer) || observer.GetWorld() != world) return false;
  return true;
 }

 static bool Distant(EAC_AmbientModule module, IEntity actor, array<IEntity> observers)
 {
  // Each owned group has one member: use its live position, never its home or
  // the AI group's potentially stale entity origin.
  if (!module || !actor || !KnownObservers(module.GetWorld(), observers)) return false;
  float sleepDistanceSq = module.SleepDistance * module.SleepDistance;
  vector position = actor.GetOrigin();
  foreach (IEntity observer : observers)
   if (vector.DistanceSq(observer.GetOrigin(), position) <= sleepDistanceSq) return false;
  return true;
 }

 static void Sample(EAC_AmbientModule module, EAC_PedestrianActivation activation, array<IEntity> observers, float now, bool distant)
 {
  EAC_ResidentClaim claim = activation.Claim;
  if (!claim || !claim.Committed || activation.PlayerTouched || claim.Resident.Dead || !distant)
  { activation.ClearSince = 0; activation.LastClearSample = now; return; }
  // A long pause cannot stand in for continuously observed distance clearance.
  if (activation.ClearSince == 0 || now - activation.LastClearSample > 1.5) activation.ClearSince = now;
  activation.LastClearSample = now;
 }

 protected static bool CanRemove(EAC_AmbientModule module, EAC_PedestrianActivation activation, array<IEntity> observers, float now)
 {
  EAC_ResidentClaim claim = activation.Claim;
  if (!claim || !claim.Committed || !Distant(module, claim.Character, observers)) return false;
  if (activation.Activity) return Blocked(module, "activity cleanup pending");
  if (claim.AlarmUntil > now) return Blocked(module, "danger cooldown");
  if (!activation.ClearSince || now - activation.ClearSince < module.ClearDelay) return false;
  SCR_ChimeraCharacter actor = SCR_ChimeraCharacter.Cast(claim.Character);
  if (activation.PlayerTouched || !EAC_PedestrianSpawner.IsCivilian(actor, claim.Group) || SCR_PossessingManagerComponent.GetPlayerIdFromControlledEntity(actor) != 0 || CompartmentAccessComponent.GetVehicleIn(actor)) return Blocked(module, "control, faction or compartment protection");
  if (!observers.IsEmpty() && !EAC_PedestrianSpawner.IsHidden(module.GetWorld(), actor.GetOrigin(), observers, actor)) return Blocked(module, "still visible to controlled character");
  string unsupported = EBG_SimulationCache.Unsupported(actor, false);
  if (!unsupported.IsEmpty()) return Blocked(module, unsupported);
  SCR_CharacterDamageManagerComponent damage = SCR_CharacterDamageManagerComponent.Cast(actor.GetDamageManager());
  if (!damage) return Blocked(module, "missing damage state");
  s_EffectScratch.Clear(); damage.GetPersistentEffects(s_EffectScratch);
  if (s_EffectScratch.Count() > 64) { s_EffectScratch.Clear(); return Blocked(module, "damage effect bound"); }
  foreach (SCR_PersistentDamageEffect effect : s_EffectScratch)
  {
   if (effect && effect.IsActive()) { s_EffectScratch.Clear(); return Blocked(module, "active damage effect"); }
  }
  s_EffectScratch.Clear();
  activation.Walking.Stop();
  s_OrderScratch.Clear(); claim.Group.GetWaypoints(s_OrderScratch);
  if (!s_OrderScratch.IsEmpty()) return Blocked(module, "external group orders");
  return true;
 }

 static bool TrySleep(EAC_AmbientModule module, EAC_PedestrianActivation activation, array<IEntity> observers, float now)
 {
  if (!CanRemove(module, activation, observers, now)) return false;
  EAC_ResidentClaim claim = activation.Claim;
  EAC_PedestrianCache cache = module.BeginResidentCache(claim);
  if (!cache) return Blocked(module, "resident cleanup reservation rejected");
  if (!module.GetSpawner().TakeRemoval()) { module.FinishResidentCache(claim); return false; }
  EAC_SceneIndex.ReleaseAllFor(claim.Resident.Id);
  if (!cache.BeginManagedSleep(claim.Group))
  {
   Blocked(module, cache.GetError());
   if (!cache.HasDeletionAttempted()) module.FinishResidentCache(claim);
  }
  if (!claim.Character) activation.Detach();
  activation.ClearSince = 0;
  return true;
 }

 static bool Blocked(EAC_AmbientModule module, string reason)
 {
  module.GetSpawner().GetDiagnostics().Record(EAC_ESpawnReason.CACHE_BLOCKED, reason);
  if (IsCleanupError(reason) && module.GetSpawner().GetDiagnostics().TakeFailureLog("cleanup: " + reason))
   Print("[EAC cleanup] " + reason, LogLevel.WARNING);
  return false;
 }

 static bool IsCleanupError(string reason)
 {
  // A delete was attempted but its actor survived. Native protection, danger,
  // visibility and player-control refusals remain expected counters.
  return reason == "Ambient actor deletion remains pending";
 }

 // Existing live routine relevance remains independent of indoor birth policy.
 static bool Relevant(EAC_AmbientModule module, EAC_PedestrianActivation activation, array<IEntity> observers)
 {
  EAC_ResidentClaim claim = activation.Claim;
  if (!claim || !claim.Home.BuildingEntity || !claim.Resident.Wanted || claim.Resident.Dead || claim.Resident.Removed || activation.PlayerTouched) return false;
  return EAC_PedestrianSpawner.IsRelevant(module, claim.Home.BuildingEntity.GetOrigin(), observers);
 }

 static bool Advance(EAC_AmbientModule module, EAC_PedestrianActivation activation, array<IEntity> observers, float now)
 {
  EAC_ResidentClaim claim = activation.Claim;
  EAC_PedestrianCache cache = claim.Cache;
  if (!cache) return false;
  activation.Observe();
  if (!cache.HasDeletionAttempted()) return module.FinishResidentCache(claim);
  if (claim.Character && !CanRemove(module, activation, observers, now)) return false;
  if (!cache.IsComplete())
  {
   if (!module.GetSpawner().TakeRemoval()) return false;
   if (!cache.RemoveNext())
   {
    Blocked(module, cache.GetError());
    if (CanLogCache(module, activation)) PrintFormat("[EAC] Resident %1 cleanup retained: %2", claim.Resident.Id, cache.GetError());
   }
  }
  if (!claim.Character) activation.Detach();
  if (cache.IsComplete()) module.FinishResidentCache(claim);
  return true;
 }
}

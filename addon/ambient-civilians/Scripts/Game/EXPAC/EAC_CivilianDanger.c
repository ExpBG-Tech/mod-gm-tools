// Shared native threat sampling. No scans, artificial threat decay or actor timer.
class EAC_CivilianDanger
{
 protected static EAC_AmbientModule s_TraceModule;
 protected static float s_NextTrace;

 static bool CombatAlarm(bool bleeding, float nativeThreatWithoutInjury)
 {
  return bleeding || nativeThreatWithoutInjury > SCR_AIThreatSystem.VIGILANT_THRESHOLD;
 }

 // Cache/destruction safety deliberately remains broader than combat response.
 static bool HasThreat(IEntity actor)
 {
  if (HasCombatThreat(actor)) return true;
  AIControlComponent control = AIControlComponent.Cast(actor.FindComponent(AIControlComponent));
  AIAgent agent = control.GetAIAgent();
  return EBG_CacheManager.HasBlockingDanger(agent);
 }

 static bool HasCombatThreat(IEntity actor)
 {
  if (!actor) return true;
  AIControlComponent control = AIControlComponent.Cast(actor.FindComponent(AIControlComponent));
  if (!control || !control.GetAIAgent()) return true;
  AIAgent agent = control.GetAIAgent();
  SCR_AIUtilityComponent utility = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
  SCR_CharacterDamageManagerComponent damage = SCR_CharacterDamageManagerComponent.Cast(actor.FindComponent(SCR_CharacterDamageManagerComponent));
  // Native injury threat can outlive treatment. Active bleeding remains unsafe,
  // while stable old wounds must not create a permanent routine/cache veto.
  // The optimizer's queue veto also covers doors, horns and vehicle avoidance.
  // Let native danger reactions apply hearing/faction/distance filters and update
  // measured threat. Merely queued events must not impose prone or abort a trip.
  return !utility || !utility.m_ThreatSystem || !damage || CombatAlarm(damage.IsBleeding(), utility.m_ThreatSystem.GetThreatMeasureWithoutInjuryFactor());
 }

 static bool CanTraceAlarm(int debugLevel, float now, float nextTrace)
 {
  return debugLevel >= 2 && now >= nextTrace;
 }

 // Called only when an alarm starts. One shared limit, no resident timers or
 // retained actor references; at most four queue entries from one actor per 10s.
 static void TraceAlarm(EAC_AmbientModule module, IEntity actor, float now)
 {
  if (!Replication.IsServer() || !module || !actor) return;
  if (s_TraceModule != module) { s_TraceModule = module; s_NextTrace = 0; }
  if (!CanTraceAlarm(module.DebugLevel, now, s_NextTrace)) return;
  s_NextTrace = now + 10;
  AIControlComponent control = AIControlComponent.Cast(actor.FindComponent(AIControlComponent));
  AIAgent agent; if (control) agent = control.GetAIAgent();
  SCR_AIUtilityComponent utility; if (agent) utility = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
  SCR_CharacterDamageManagerComponent damage = SCR_CharacterDamageManagerComponent.Cast(actor.FindComponent(SCR_CharacterDamageManagerComponent));
  if (!utility || !utility.m_ThreatSystem || !damage)
  {
   PrintFormat("[EAC ALARM] actor=%1 pos=%2 reason=missing_threat_or_damage_component", actor, actor.GetOrigin());
   return;
  }
  float suppression, shots, injury, endangered;
  utility.m_ThreatSystem.GetThreatValues(suppression, shots, injury, endangered);
  string behavior = "none";
  if (utility.m_CurrentBehavior) behavior = string.Format("%1 cause=%2 threat=%3", utility.m_CurrentBehavior.Type().ToString(), utility.m_CurrentBehavior.GetCause(), utility.m_CurrentBehavior.m_fThreat);
  string events;
  int count = agent.GetDangerEventsCount();
  int limit = Math.Min(count, 4);
  IEntity actorRoot = actor.GetRootParent();
  for (int i = 0; i < limit; i++)
  {
   int aggregated;
   AIDangerEvent danger = agent.GetDangerEvent(i, aggregated);
   if (!danger) { events += " null"; continue; }
   bool ownVehicle = actorRoot && actorRoot != actor && (danger.GetObject() == actorRoot || danger.GetVictim() == actorRoot);
   events += string.Format(" [%1 count=%2 object=%3 victim=%4 own_vehicle=%5]", typename.EnumToString(EAIDangerEventType, danger.GetDangerType()), aggregated, danger.GetObject(), danger.GetVictim(), ownVehicle);
  }
  string values = string.Format("native=%1 suppression=%2 shots=%3 injury=%4 endangered=%5 bleeding=%6", utility.m_ThreatSystem.GetThreatMeasureWithoutInjuryFactor(), suppression, shots, injury, endangered, damage.IsBleeding());
  PrintFormat("[EAC ALARM] actor=%1 pos=%2 %3 behavior=%4 queued=%5 sampled=%6%7", actor, actor.GetOrigin(), values, behavior, count, limit, events);
 }

 static void Monitor(EAC_AmbientModule module, EAC_PedestrianActivation activation, float now)
 {
  if (!Replication.IsServer() || !activation) return;
  if (!module || module != EAC_AmbientModule.GetActive()) { activation.Shelter.Stop("danger module guard"); return; }
  EAC_ResidentClaim claim = activation.Claim;
  if (!claim || !claim.Committed || claim.Cache || activation.PlayerTouched || module.GetResidentActivation(claim.Home, claim.Resident) != claim || !EAC_PedestrianSpawner.HasCivilianControl(claim.Character, claim.Group)) { activation.Shelter.Stop("danger claim guard"); return; }
  CharacterControllerComponent controller = CharacterControllerComponent.Cast(claim.Character.FindComponent(CharacterControllerComponent));
  if (controller.IsPlayerControlled() || SCR_PossessingManagerComponent.GetPlayerIdFromControlledEntity(claim.Character) != 0 || !claim.OptimizerMember || claim.OptimizerMember.WasPlayer) { activation.Shelter.Stop("danger actor guard"); return; }
  if (HasCombatThreat(claim.Character))
  {
   if (claim.AlarmUntil <= now) TraceAlarm(module, claim.Character, now);
   claim.AlarmUntil = now + module.CalmDelay;
   activation.ClearSince = 0;
   activation.Walking.Stop();
   if (activation.Activity) activation.Activity.RequestStop(true);
  }
  activation.Shelter.Monitor(module, activation, now);
 }
}

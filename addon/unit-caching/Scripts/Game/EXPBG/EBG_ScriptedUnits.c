// EXPBG Unit Scripts soldiers (Hold position, Freeze, an ambient animation) in Unit
// Caching. A unit script is a live EUS_UnitControl on the original actor; an animation
// is a vanilla loiter command (SCR_CharacterControllerComponent.StartLoitering), not a
// vehicle or compartment (EUS_UnitControl releases at once if he is ever IsInVehicle).
//
// Unit Scripts reserves a scripted squad (IsReserved: never enrolled) and keeps an
// enrolled one awake (KeepAwakeReason), because a Full cycle deletes and respawns the
// soldiers and so drops their scripts. Simulation keeps the same actors, their AI
// settings and their bound controls; only the presentation and AI LOD are paused. So:
// - every zone enrolls and suspends such a squad in Simulation when the per-soldier
//   scripts are the only reason it is held (night discipline off, no other module
//   reserves it): a Full zone falls back to Simulation for it (UsesSimulation) and
//   Full-caches its other squads; a squad whose scripts all ended goes Full again at
//   its next sleep;
// - on wake every scripted soldier is checked: his control must still be bound with
//   the same script, an animation must still be playing (a loiter that ended while
//   he was paused is started again through Unit Scripts' own ApplyUnit, which gives
//   it a fresh retry allowance and room check at the same spot).
//
// Only existing public Unit Scripts members are used (EUS_Manager, EUS_Codes,
// EUS_UnitControl getters, the replicated EUS_Script / EUS_Scripted / EUS_Discipline).
// No per-frame work: everything runs inside the existing bounded cache scheduler.
class EBG_ScriptedUnits
{
 static const string FALLBACK_NOTE = "Simulation cached instead of Full: Unit Scripts soldiers in an animation (a Full cycle would respawn them without their pose)";

 // Simulation for this record: a Simulation zone, or a Full zone's squad with a Unit
 // Scripts animation (Full would drop the pose). Hold and Freeze soldiers Full cache:
 // their script rides the survivor carry (EUS_FullCacheCarry.c) and binds again on
 // the respawned soldier.
 static bool UsesSimulation(EBG_CacheGroup record)
 {
  if (!record || !record.Zone)
  {
   return false;
  }
  return record.Zone.Mode == 0 || CountAnimated(record.Group) > 0;
 }

 // Living AI members of the squad running a Unit Scripts animation.
 static int CountAnimated(SCR_AIGroup group)
 {
  if (!group)
  {
   return 0;
  }
  array<AIAgent> agents = {};
  group.GetAgents(agents);
  int animated;
  foreach (AIAgent agent : agents)
  {
   SCR_ChimeraCharacter member = SCR_ChimeraCharacter.Cast(agent.GetControlledEntity());
   if (member && EUS_Codes.IsAnimation(member.EUS_Script)) animated++;
  }
  return animated;
 }

 // Living AI members of the squad running a unit script (replicated EUS_Script).
 static int CountScripted(SCR_AIGroup group)
 {
  if (!group)
  {
   return 0;
  }
  array<AIAgent> agents = {};
  group.GetAgents(agents);
  int scripted;
  foreach (AIAgent agent : agents)
  {
   SCR_ChimeraCharacter member = SCR_ChimeraCharacter.Cast(agent.GetControlledEntity());
   if (member && member.EUS_Script != EUS_Codes.NONE) scripted++;
  }
  return scripted;
 }

 // Enrollment: true when Unit Scripts reserves this squad only for its per-soldier
 // scripts and no other module reserves it. Unit Scripts answers IsReserved first
 // (EUS_Manager.Reserves: discipline or a scripted count) and hides the rest of the
 // chain, so the chain is asked once more with the squad's replicated scripted count
 // masked. The count is restored before returning; nothing is replicated (no BumpMe)
 // and nothing runs in between. Night discipline is never masked: it stays a hold.
 static bool SimulationOnly(EBG_CacheManager manager, SCR_AIGroup group)
 {
  if (!manager || !group || group.EUS_Discipline != EUS_Codes.DISCIPLINE_OFF)
  {
   return false;
  }
  // Without a scripted count Unit Scripts does not reserve the squad: someone else does.
  int count = group.EUS_Scripted;
  if (count <= 0)
  {
   return false;
  }
  group.EUS_Scripted = 0;
  bool other = manager.IsReserved(group);
  group.EUS_Scripted = count;
  return !other;
 }

 // The scheduler's keep-awake reason for a record. Any zone drops the Unit Scripts hold
 // when it is only per-soldier scripts (the effective reason is exactly Unit Scripts'
 // own HoldReason and night discipline is off): the record then sleeps in Simulation.
 static string KeepAwake(EBG_CacheGroup record, string reason)
 {
  if (reason.IsEmpty() || !record || !record.Group || !record.Zone)
  {
   return reason;
  }
  if (CountScripted(record.Group) == 0)
  {
   return reason;
  }
  if (record.Group.EUS_Discipline == EUS_Codes.DISCIPLINE_OFF && reason == EUS_Manager.HoldReason(record.Group))
  {
   return string.Empty;
  }
  return reason;
 }


 // Wake reason suffix: how many scripted soldiers kept or restarted their script and
 // how many had it released while paused. Empty without scripted soldiers.
 static string Summary(EBG_SimulationState state)
 {
  if (!state)
  {
   return string.Empty;
  }
  int kept;
  int restarted;
  int released;
  foreach (EBG_SimulationAgent member : state.Members)
  {
   if (!member.Script) continue;
   string outcome = member.Script.Outcome;
   if (outcome.StartsWith("kept")) kept++;
   else if (outcome == "animation restarted") restarted++;
   else released++;
  }
  if (kept + restarted + released == 0)
  {
   return string.Empty;
  }
  return string.Format(" | Unit Scripts: %1 kept, %2 animation(s) restarted, %3 not resumed", kept, restarted, released);
 }

 // Simulation suspension refusal for a scripted soldier whose animation is between
 // states (entry queued, or ended and not yet re-issued by Unit Scripts). Transient:
 // the next attempt caches him once the pose plays. Empty when he may be paused.
 static string Unsettled(SCR_ChimeraCharacter character)
 {
  if (!character || !EUS_Codes.IsAnimation(character.EUS_Script))
  {
   return string.Empty;
  }
  SCR_CharacterControllerComponent controller = SCR_CharacterControllerComponent.Cast(character.GetCharacterController());
  if (!controller)
  {
   return "Unit Scripts animation without a character controller";
  }
  if (controller.IsLoitering())
  {
   return string.Empty;
  }
  return "Unit Scripts animation still starting (cached once the pose plays)";
 }
}

// One scripted soldier's unit script across a Simulation suspension.
class EBG_ScriptedMember
{
 int Code;
 // Diagnostics and fixtures: the pose played when he was paused.
 bool Loitering;
 // Strong: the outcome needs the end reason of a control released while cached.
 // Released controls are inert; a bound one stays owned by EUS_Manager as well.
 ref EUS_UnitControl Control;
 // Diagnostics and fixtures: what the wake found and did.
 string Outcome;

 static EBG_ScriptedMember Capture(SCR_ChimeraCharacter character)
 {
  if (!character || character.EUS_Script == EUS_Codes.NONE)
  {
   return null;
  }
  EBG_ScriptedMember member = new EBG_ScriptedMember();
  member.Code = character.EUS_Script;
  EUS_Manager manager = EUS_Manager.Current();
  if (manager) member.Control = manager.FindControl(character);
  SCR_CharacterControllerComponent controller = SCR_CharacterControllerComponent.Cast(character.GetCharacterController());
  if (controller) member.Loitering = controller.IsLoitering();
  member.Outcome = "suspended";
  return member;
 }

 // After the original actor's presentation, physics and AI LOD were restored. Never
 // re-binds a script the Game Master, damage, death or possession released.
 void Resume(SCR_ChimeraCharacter character)
 {
  Outcome = "resumed";
  if (!character)
  {
   Outcome = "soldier missing";
   return;
  }
  SCR_CharacterControllerComponent controller = SCR_CharacterControllerComponent.Cast(character.GetCharacterController());
  if (!controller || controller.IsDead() || character.EBG_WasPlayerControlled())
  {
   Outcome = "soldier dead or possessed; script not resumed";
   return;
  }
  bool bound = Control && Control.IsBound() && Control.GetActor() == character;
  if (bound && Control.GetCode() != Code)
  {
   Outcome = "kept; script changed while cached";
   return;
  }
  if (!EUS_Codes.IsAnimation(Code))
  {
   if (bound) Outcome = "kept";
   else Outcome = "released while cached; not re-applied";
   return;
  }
  if (bound && controller.IsLoitering())
  {
   Outcome = "kept; animation playing";
   return;
  }
  SCR_ScriptedCharacterInputContext input = controller.GetScrInputContext();
  if (bound && input && input.m_iLoiteringType >= 0)
  {
   Outcome = "kept; animation entry pending";
   return;
  }
  // Re-apply only after the animation ended while he was paused: either the control is
  // still bound (its own retries may be spent) or it gave up for that reason alone.
  if (!bound)
  {
   string ended;
   if (Control) ended = Control.GetEndReason();
   if (!ended.StartsWith("the animation could not be kept"))
   {
    Outcome = "released while cached (" + ended + "); not re-applied";
    return;
   }
  }
  EUS_Manager manager = EUS_Manager.Current();
  if (!manager)
  {
   Outcome = "Unit Scripts manager unavailable; animation not restarted";
   return;
  }
  if (manager.ApplyUnit(character, Code, null))
  {
   Outcome = "animation restarted";
   Control = manager.FindControl(character);
   return;
  }
  Outcome = "animation restart refused by Unit Scripts";
 }
}

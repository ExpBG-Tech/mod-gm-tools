// Unit Caching Full caching deletes living survivors and respawns them from their
// prefab. A Hold or Freeze soldier's script rides in the Unit Caching survivor
// carry: the versioned EUS_UnitState row (code, held spot, held heading) is copied
// on the server before deletion and queued again on the respawned character, which
// the manager binds once his AI is ready (the same path as native and CDF loads).
// Squads with an animation stay in Simulation (EBG_ScriptedUnits.UsesSimulation),
// so only Hold and Freeze come through here. The row also travels in the portable
// (CDF) Full snapshot as "eusScript" (EUS_UnitState JSON), so a Full-cached squad keeps
// its scripts through a save and a server restart.
modded class EBG_SurvivorCarry
{
 protected ref EUS_UnitState m_EUS_State;

 // The captured script of this survivor, or null when he had none.
 EUS_UnitState EUS_GetState() { return m_EUS_State; }

 override void Capture(SCR_ChimeraCharacter entity)
 {
  super.Capture(entity);
  m_EUS_State = EUS_UnitState.Capture(entity);
  if (m_EUS_State && !m_EUS_State.Validate().IsEmpty())
  {
   Print("[EUS] Full cache: survivor script failed validation and is not carried", LogLevel.WARNING);
   m_EUS_State = null;
  }
 }

 override bool Write(SaveContext context)
 {
  if (!super.Write(context)) return false;
  if (!m_EUS_State) return true;
  return context.WriteValue("eusScript", m_EUS_State.Encode());
 }

 override void Read(LoadContext context)
 {
  super.Read(context);
  m_EUS_State = null;
  string payload;
  if (!context.ReadValue("eusScript", payload) || payload.IsEmpty()) return;
  string reason;
  m_EUS_State = EUS_UnitState.Decode(payload, reason);
  if (!m_EUS_State) Print("[EUS] Full cache snapshot: saved script dropped: " + reason, LogLevel.WARNING);
 }

 override void Apply(SCR_ChimeraCharacter entity)
 {
  super.Apply(entity);
  if (!m_EUS_State) return;
  string reason;
  if (m_EUS_State.Restore(entity, "Full cache", reason)) return;
  Print("[EUS] Full cache: script could not be restored onto the respawned unit: " + reason, LogLevel.WARNING);
 }
}

// Event sources for EXPBG AI Surrender. Both hooks cost one static flag check on every
// machine and do nothing unless a surrender module is active on the server or
// prisoners exist there.
modded class SCR_CharacterControllerComponent
{
 override void OnLifeStateChanged(ECharacterLifeState previousLifeState, ECharacterLifeState newLifeState, bool isJIP)
 {
  IEntity esrOwner;
  SCR_AIGroup esrGroup;
  if (!isJIP && ESR_SurrenderManager.IsWatching())
  {
   // Read the squad before the native chain retires a dead member from it.
   esrOwner = GetOwner();
   esrGroup = ESR_SurrenderManager.GroupOf(esrOwner);
  }
  super.OnLifeStateChanged(previousLifeState, newLifeState, isJIP);
  if (esrOwner) ESR_SurrenderManager.OnLifeState(esrOwner, esrGroup, previousLifeState, newLifeState);
 }
}

modded class SCR_AIGroup
{
 override void OnAgentRemoved(AIAgent child)
 {
  super.OnAgentRemoved(child);
  if (ESR_SurrenderManager.IsListening()) ESR_SurrenderManager.OnAgentRemoved(this, child);
 }
}

// A soldier with his own surrender chance rolls it when he first comes under threat
// (ESR_SurrenderManager.ThreatRoll). Subscribed lazily from the vanilla AI update: one
// flag check per update; the handler runs only on a threat state change.
modded class SCR_AIThreatSystem
{
 protected bool m_bESR_Subscribed;
 protected bool m_bESR_Rolled;
 protected IEntity m_ESR_Owner;

 override void Update(SCR_AIUtilityComponent utility, float timeSlice)
 {
  super.Update(utility, timeSlice);
  if (m_bESR_Subscribed || !utility) return;
  m_bESR_Subscribed = true;
  m_ESR_Owner = utility.m_OwnerEntity;
  GetOnThreatStateChanged().Insert(ESR_OnThreatState);
 }

 protected void ESR_OnThreatState(EAIThreatState previousState, EAIThreatState newState)
 {
  if (m_bESR_Rolled || newState < EAIThreatState.ALERTED || !ESR_SurrenderManager.IsListening()) return;
  SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(m_ESR_Owner);
  if (!character || character.ESR_GetOverride(ESR_Overrides.SURRENDER) < 0) return;
  m_bESR_Rolled = true;
  ESR_SurrenderManager.ThreatRoll(character);
 }
}

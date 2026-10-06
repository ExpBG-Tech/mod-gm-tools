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

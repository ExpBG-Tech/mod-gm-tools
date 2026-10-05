// Ambient claims remain outside geographic zone records. No hidden cache zone.
modded class EBG_CacheManager
{
 override bool IsReserved(SCR_AIGroup group)
 {
  EAC_ResidentClaims claims = EAC_AmbientModule.GetMissionClaims();
  if (claims && claims.ReservesGroup(group)) return true;
  return super.IsReserved(group);
 }

 override EBG_CacheMember FindMember(IEntity entity)
 {
  EBG_CacheMember nativeMember = super.FindMember(entity);
  if (nativeMember) return nativeMember;
  EAC_ResidentClaims claims = EAC_AmbientModule.GetMissionClaims();
  if (claims) return claims.FindOptimizerMember(entity);
  return null;
 }

 // Check native ownership without rejecting this claim's own ambient reservation.
 bool EAC_CanTrack(SCR_AIGroup group, IEntity entity)
 {
  if (!Replication.IsServer() || Unloading || !GetGame() || m_World != GetGame().GetWorld()) return false;
  if (entity)
  {
   if (super.FindMember(entity)) return false;
   SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(entity);
   if (!character || Regroup.ReservesMember(character)) return false;
   SCR_AIGroup parent;
   if (character) parent = character.GetCharacterGroup();
   if (parent && (FindGroup(parent) || super.IsReserved(parent))) return false;
  }
  if (!group) return true;
  if (FindGroup(group) || super.IsReserved(group) || group.GetAgentsCount() > 200) return false;
  array<AIAgent> agents = {}; group.GetAgents(agents);
  foreach (AIAgent agent : agents)
  {
   if (!agent) return false;
   SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(agent.GetControlledEntity());
   if (!character || super.FindMember(character) || Regroup.ReservesMember(character)) return false;
  }
  return true;
 }
}

// Native regroup builds and commits synchronously, bypassing manager.IsReserved.
modded class EBG_CacheRegroup
{
 override protected EBG_RegroupPlan Build(EBG_CacheManager manager, EBG_CacheGroup seed)
 {
  EBG_RegroupPlan plan = super.Build(manager, seed);
  EAC_ResidentClaims claims = EAC_AmbientModule.GetMissionClaims();
  if (claims && plan)
   foreach (SCR_AIGroup group : plan.Groups)
    if (claims.ReservesGroup(group)) { plan.Problem = "Regroup held: ambient resident ownership"; break; }
  return plan;
 }
}

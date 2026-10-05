// Same explicit ownership boundary as pedestrians; never adopt native zone actors.
modded class EBG_CacheManager
{
 override bool IsReserved(SCR_AIGroup group)
 {
  EAC_TrafficDirector traffic = EAC_TrafficDirector.Get();
  return (traffic && traffic.ReservesGroup(group)) || super.IsReserved(group);
 }
 override EBG_CacheMember FindMember(IEntity entity)
 {
  EBG_CacheMember member = super.FindMember(entity);
  if (member) return member;
  EAC_TrafficDirector traffic = EAC_TrafficDirector.Get();
  if (traffic) return traffic.FindMember(entity);
  return null;
 }
}

modded class EBG_CacheRegroup
{
 override protected EBG_RegroupPlan Build(EBG_CacheManager manager, EBG_CacheGroup seed)
 {
  EBG_RegroupPlan plan = super.Build(manager, seed);
  EAC_TrafficDirector traffic = EAC_TrafficDirector.Get();
  if (traffic && plan)
   foreach (SCR_AIGroup group : plan.Groups)
    if (traffic.ReservesGroup(group)) { plan.Problem = "Regroup held: ambient traffic ownership"; break; }
  return plan;
 }
}

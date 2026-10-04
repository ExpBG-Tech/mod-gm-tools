// Ordinary zone enrollment uses IsReserved. Regroup repairs have a separate
// admission/commit path; they must also wait for our original snapshots to wake.
modded class EBG_CacheRegroup
{
 protected bool EXPG_RequestHandoff(EBG_RegroupPlan plan)
 {
  if (!plan) { return false; }
  bool held;
  foreach (SCR_AIGroup group : plan.Groups)
  {
   if (EXPG_GarrisonManager.RequestTransferRelease(group)) { held = true; }
  }
  foreach (EBG_CacheGroup record : plan.Records)
  {
   if (record && EXPG_GarrisonManager.RequestTransferRelease(record.Group)) { held = true; }
   if (!record) { continue; }
   foreach (EBG_CacheMember member : record.Members)
   {
    if (member.Entity && EXPG_GarrisonManager.RequestTransferRelease(member.Entity.GetCharacterGroup())) { held = true; }
   }
  }
  return held;
 }

 override protected EBG_RegroupPlan Build(EBG_CacheManager manager, EBG_CacheGroup seed)
 {
  EBG_RegroupPlan plan = super.Build(manager, seed);
  if (EXPG_RequestHandoff(plan))
  {
   plan.Problem = "Regroup held: waiting for original garrison state and controls to release";
  }
  return plan;
 }

 override protected bool Commit(EBG_CacheManager manager, EBG_RegroupPlan plan)
 {
  // Recheck retained plans at the actual commit boundary; never call the base
  // IsReserved here because that includes this regroup plan's own reservation.
  if (EXPG_RequestHandoff(plan))
  {
   plan.Hold("Regroup held: garrison restoration/release remains pending");
   return false;
  }
  return super.Commit(manager, plan);
 }
}

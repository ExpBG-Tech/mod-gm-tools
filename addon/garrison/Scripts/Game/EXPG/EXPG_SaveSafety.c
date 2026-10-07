// Garrisons save themselves (0.1.11): the garrison ledger (EXPG_Snapshot.c) carries
// every garrison in native saves and, with the EXPBG CDF Compat bridge, in CDF saves;
// garrison-owned entities are kept out of both (EXPG_SaveExclusion.c). Native saves
// are never refused because a garrison is active, and Unit Caching Prepare for Save
// no longer releases garrisons. Only CDF without the bridge (legacy mode,
// EXPG_GarrisonPersistence) keeps the 0.1.8 rules: Prepare for Save restores and
// releases every garrison, and CDF capture is refused while one is active. A load
// that is replacing the garrisons holds Prepare and the save until it has finished.
modded class EBG_OptimizerControl
{
 override static void BeginPreparation()
 {
  if (EXPG_GarrisonPersistence.Legacy()) { EXPG_GarrisonManager.RequestReleaseAll(); }
  super.BeginPreparation();
 }

 override static void Poll(EBG_CacheManager manager)
 {
  if (Replication.IsServer() && manager && Preparing)
  {
   bool legacy = EXPG_GarrisonPersistence.Legacy() && EXPG_GarrisonManager.HasActive();
   bool loading = EXPG_GarrisonPersistence.Importing();
   if (legacy || loading)
   {
    // Base Poll publishes Ready before returning. Do not enter it while this
    // external owner is still releasing or loading; cache restoration keeps running.
    EBG_OptimizerControl.HoldZones();
    if (legacy) { EXPG_GarrisonManager.RequestReleaseAll(); }
    Pending = Math.Max(1, Pending);
    if (State != 3) { State = 1; }
    if (legacy) { Message = "Garrison restoration/release pending (CDF without the EXPBG CDF Compat garrison bridge). Unit Caching readiness will be checked after release."; }
    else { Message = "Garrisons are loading from a save. Unit Caching readiness will be checked once they are placed."; }
    EBG_OptimizerControl.Publish();
    return;
   }
  }
  super.Poll(manager);
 }

 override static bool ResumePreparedSession()
 {
  if (Preparing && ((EXPG_GarrisonPersistence.Legacy() && EXPG_GarrisonManager.HasActive()) || EXPG_GarrisonPersistence.Importing()))
  {
   EBG_OptimizerControl.Poll(EBG_CacheManager.Instance);
   Print("[EXPG SAVE] Resume refused while garrison release or loading remains pending.", LogLevel.WARNING);
   return false;
  }
  return super.ResumePreparedSession();
 }

 override static void Execute(int action)
 {
  if (!Replication.IsServer() || !EBG_CacheManager.IsPortableWorldReady() || EBG_CacheSnapshot.Loading) { return; }
  if (action == 3)
  {
   EBG_CacheManager manager = EBG_CacheManager.Get();
   if (!manager) { return; }
   manager.RestoreAllForSave();
   EBG_OptimizerControl.Poll(manager);
   return;
  }
  // Base Execute uses unqualified static calls; guard commands here rather
  // than relying on its calls dispatching to this addon's static overrides.
  if ((action == 1 || action == 2) && Preparing && ((EXPG_GarrisonPersistence.Legacy() && EXPG_GarrisonManager.HasActive()) || EXPG_GarrisonPersistence.Importing()))
  {
   EBG_OptimizerControl.Poll(EBG_CacheManager.Instance);
   Print("[EXPG SAVE] Enable/Disable refused while garrison release or loading remains pending.", LogLevel.WARNING);
   return;
  }
  super.Execute(action);
 }
}

// The Unit Caching CDF bridge consults this before every CDF capture. Garrisons
// refuse only while a load replaces them, and in CDF legacy mode while active.
modded class EBG_CacheSnapshot
{
 override static bool CanSave(out string reason)
 {
  if (!EXPG_GarrisonPersistence.CanExport(reason)) { return false; }
  return super.CanSave(reason);
 }
}

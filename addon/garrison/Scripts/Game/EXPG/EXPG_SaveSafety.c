// Garrison ownership is mission-only. Save ordinary restored squads after the
// explicit Prepare action has released every building record. Never mutate the
// population from a serializer or share ownership of native saving permission.
modded class EBG_OptimizerControl
{
 override static void BeginPreparation()
 {
  EXPG_GarrisonManager.RequestReleaseAll();
  super.BeginPreparation();
 }

 override static void Poll(EBG_CacheManager manager)
 {
  if (Replication.IsServer() && manager && Preparing && EXPG_GarrisonManager.HasActive())
  {
   // Base Poll publishes Ready before returning. Do not enter it while this
   // external owner is still releasing; normal cache restoration keeps running.
   EBG_OptimizerControl.HoldZones();
   EXPG_GarrisonManager.RequestReleaseAll();
   Pending = Math.Max(1, Pending);
   if (State != 3) { State = 1; }
   Message = "Garrison restoration/release pending. Optimizer readiness will be checked after release.";
   EBG_OptimizerControl.Publish();
   return;
  }
  super.Poll(manager);
 }

 override static bool ResumePreparedSession()
 {
  if (Preparing && EXPG_GarrisonManager.HasActive())
  {
   EBG_OptimizerControl.Poll(EBG_CacheManager.Instance);
   Print("[EXPG SAVE] Resume refused while garrison restoration/release remains pending.", LogLevel.WARNING);
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
  if ((action == 1 || action == 2) && Preparing && EXPG_GarrisonManager.HasActive())
  {
   EBG_OptimizerControl.Poll(EBG_CacheManager.Instance);
   Print("[EXPG SAVE] Enable/Disable refused while garrison restoration/release remains pending.", LogLevel.WARNING);
   return;
  }
  super.Execute(action);
 }
}

// The optional Optimizer CDF companion consults this before entity capture.
// CDF without that companion does not call this hook.
modded class EBG_CacheSnapshot
{
 override static bool CanSave(out string reason)
 {
  if (EXPG_GarrisonManager.HasActive())
  {
   reason = "Use Optimizer Prepare for Save to restore and release all garrisons, then wait for Ready. Garrison assignments are mission-only.";
   return false;
  }
  return super.CanSave(reason);
 }
}

// Guard the serializer itself: its legacy-mission branch bypasses Export().
modded class EBG_MissionPersistenceSerializer
{
 override protected ESerializeResult Serialize(notnull Managed instance, notnull SaveContext context)
 {
  if (EXPG_GarrisonManager.HasActive())
  {
   Print("[EXPG SAVE] Refused active garrison ownership. Use Optimizer Prepare for Save and wait for Ready; assignments are mission-only.", LogLevel.ERROR);
   return ESerializeResult.ERROR;
  }
  return super.Serialize(instance, context);
 }
}

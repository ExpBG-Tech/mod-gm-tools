[WorkbenchPluginAttribute(name: "EXPBG GM Tools contracts", wbModules: {"ResourceManager"})]
class EXPG_ContractPlugin : WorkbenchPlugin
{
 override void RunCommandline()
 {
  bool graph = EXPG_BuildingPlanTest.Run();
  bool editor = EXPG_EditorTest.Run();
  bool attributes = EXPG_AttributesTest.Run();
  bool corridor = EXPG_PatrolControlTest.CorridorChecks();
  bool missingActor = EXPG_PatrolControlTest.RejectMissingActor();
  bool capacity = EXPG_FullCacheTest.CapacityContract();
  bool createdSlot = EXPG_FullCacheTest.CreatedSlotContract();
  bool randomRules = EXPG_RandomGarrisonTest.Rules();
  bool randomShuffle = EXPG_RandomGarrisonTest.Shuffle();
  bool randomPacking = EXPG_RandomGarrisonTest.Packing();
  PrintFormat("[EXPG FULL CONTRACT RESULT] capacity=%1 createdSlot=%2", capacity, createdSlot);
  PrintFormat("[EXPG CONTRACT RESULT] graph=%1 editor=%2 attributes=%3 corridor=%4 missingActor=%5", graph, editor, attributes, corridor, missingActor);
  PrintFormat("[EXPG RANDOM CONTRACT RESULT] rules=%1 shuffle=%2 packing=%3", randomRules, randomShuffle, randomPacking);
  if (graph && editor && attributes && corridor && missingActor && capacity && createdSlot && randomRules && randomShuffle && randomPacking) { Workbench.Exit(0); }
  else { Workbench.Exit(1); }
 }
}

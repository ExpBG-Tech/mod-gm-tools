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
  PrintFormat("[EXPG CONTRACT RESULT] graph=%1 editor=%2 attributes=%3 corridor=%4 missingActor=%5", graph, editor, attributes, corridor, missingActor);
  if (graph && editor && attributes && corridor && missingActor) { Workbench.Exit(0); }
  else { Workbench.Exit(1); }
 }
}

[WorkbenchPluginAttribute(name: "EXPBG Garrison art import", wbModules: {"ResourceManager"})]
class EXPG_ArtImportPlugin : WorkbenchPlugin
{
 override void RunCommandline()
 {
  ResourceManager manager = Workbench.GetModule(ResourceManager);
  string input;
  if (!manager || !Workbench.GetAbsolutePath("$Garrison:UI/Textures/EXPBG_Garrison/EXPG_Card.png", input)) { Workbench.Exit(1); return; }
  if (!manager.RegisterResourceFile(input, true)) { Workbench.Exit(2); return; }
  manager.RebuildResourceFiles({"$Garrison:UI/Textures/EXPBG_Garrison/EXPG_Card.edds"}, "PC");
  Print("[EXPG ART] Native registration and rebuild returned; inspect generated DDS bytes");
  Workbench.Exit(0);
 }
}

// Command-line entry for the EXPBG AI Global Skills scalar contracts (tests/EGS_SkillsTest.c).
// Copied into Scripts/WorkbenchGame of a private snapshot copy only; never packed.
[WorkbenchPluginAttribute(name: "EXPBG AI Global Skills contracts", wbModules: {"ResourceManager"})]
class EGS_ContractPlugin : WorkbenchPlugin
{
	override void RunCommandline()
	{
		if (EGS_SkillsTest.Run())
			Workbench.Exit(0);
		else
			Workbench.Exit(1);
	}
}

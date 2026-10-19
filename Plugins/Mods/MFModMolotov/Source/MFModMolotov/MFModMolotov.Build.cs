using UnrealBuildTool;

public class MFModMolotov : ModuleRules
{
	public MFModMolotov(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "MobileForces" });
	}
}

using UnrealBuildTool;

public class MFModTug : ModuleRules
{
	public MFModTug(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "MobileForces" });
	}
}

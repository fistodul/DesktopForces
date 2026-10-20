using UnrealBuildTool;

public class MFModTripLasers : ModuleRules
{
	public MFModTripLasers(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "MobileForces" });
	}
}

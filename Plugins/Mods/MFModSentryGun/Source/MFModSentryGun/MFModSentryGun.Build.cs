using UnrealBuildTool;

public class MFModSentryGun : ModuleRules
{
	public MFModSentryGun(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "MobileForces" });
	}
}

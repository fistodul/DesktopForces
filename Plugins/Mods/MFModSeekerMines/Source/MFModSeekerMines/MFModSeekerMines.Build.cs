using UnrealBuildTool;

public class MFModSeekerMines : ModuleRules
{
	public MFModSeekerMines(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "MobileForces", "AIModule", "NavigationSystem" });
	}
}

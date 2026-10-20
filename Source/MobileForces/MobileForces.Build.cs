using UnrealBuildTool;

public class MobileForces : ModuleRules
{
	public MobileForces(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"NavigationSystem",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate",
			"SlateCore",
			"NetCore",
			"PhysicsCore",
			"DeveloperSettings",
			"AssetRegistry",
			"EngineSettings"
		});

		PublicIncludePaths.Add(ModuleDirectory);
	}
}

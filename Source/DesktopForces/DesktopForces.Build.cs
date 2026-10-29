using UnrealBuildTool;

public class DesktopForces : ModuleRules
{
	public DesktopForces(ReadOnlyTargetRules Target) : base(Target)
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
			"ApplicationCore",
			"ProceduralMeshComponent",
			"NetCore",
			"PhysicsCore",
			"DeveloperSettings",
			"AssetRegistry",
			"EngineSettings"
		});

		// in-game pack downloads (Core/DFDownloads)
		PrivateDependencyModuleNames.Add("ChunkDownloader");

		PublicIncludePaths.Add(ModuleDirectory);
	}
}

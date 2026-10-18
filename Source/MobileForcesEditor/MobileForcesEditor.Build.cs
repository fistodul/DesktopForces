using UnrealBuildTool;

public class MobileForcesEditor : ModuleRules
{
	public MobileForcesEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"MobileForces"
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"UnrealEd",
			"AssetTools",
			"AssetRegistry",
			"Json",
			"JsonUtilities",
			"MeshDescription",
			"StaticMeshDescription",
			"MaterialEditor",
			"RenderCore",
			"RHI",
			"Slate",
			"SlateCore",
			"LevelEditor",
			"EditorScriptingUtilities",
			"AudioEditor",
			"Landscape",
			"NavigationSystem"
		});

		PublicIncludePaths.Add(ModuleDirectory);
	}
}

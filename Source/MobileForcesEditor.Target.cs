using UnrealBuildTool;
using System.Collections.Generic;

public class MobileForcesEditorTarget : TargetRules
{
	public MobileForcesEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.AddRange(new string[] { "MobileForces", "MobileForcesEditor" });
	}
}

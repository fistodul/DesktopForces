using UnrealBuildTool;
using System.Collections.Generic;

/// A headless server without rendering. Needs an engine with server targets: a source build, or
/// Epic's container image (Docs/Hosting.md); the launcher engine has none.
public class DesktopForcesServerTarget : TargetRules
{
	public DesktopForcesServerTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Server;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("DesktopForces");
	}
}

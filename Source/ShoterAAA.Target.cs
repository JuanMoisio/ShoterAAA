using UnrealBuildTool;
using System.Collections.Generic;

public class ShoterAAATarget : TargetRules
{
	public ShoterAAATarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("ShoterAAA");
	}
}

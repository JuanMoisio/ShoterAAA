using UnrealBuildTool;
using System.Collections.Generic;

public class ShoterAAAEditorTarget : TargetRules
{
	public ShoterAAAEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("ShoterAAA");
	}
}

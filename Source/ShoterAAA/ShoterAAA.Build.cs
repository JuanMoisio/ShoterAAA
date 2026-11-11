using UnrealBuildTool;

public class ShoterAAA : ModuleRules
{
	public ShoterAAA(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Allows includes relative to the module root, e.g. #include "Player/ShoterCharacter.h"
		PublicIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"CoreOnline",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"PhysicsCore",
			"DeveloperSettings",
			"AIModule",
			"NavigationSystem",
			"GameplayTasks",
			"UMG",
			"Slate",
			"SlateCore",
			"OnlineBase",
			"OnlineSubsystem",
			"OnlineSubsystemUtils"
		});

		DynamicallyLoadedModuleNames.AddRange(new string[]
		{
			"OnlineSubsystemNull",
			"OnlineSubsystemSteam"
		});
	}
}

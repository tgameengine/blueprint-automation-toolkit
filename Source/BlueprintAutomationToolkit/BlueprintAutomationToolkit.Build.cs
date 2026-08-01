// Copyright 2026 AkaSoft. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class BlueprintAutomationToolkit : ModuleRules
{
	public BlueprintAutomationToolkit(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false;

		string PublicPath = Path.Combine(ModuleDirectory, "Public");
		string PrivatePath = Path.Combine(ModuleDirectory, "Private");

		PublicIncludePaths.AddRange(new string[]
		{
			PublicPath,
		});

		PrivateIncludePaths.AddRange(new string[]
		{
			PrivatePath,
		});

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"ApplicationCore",
			"AssetRegistry",
			"AssetTools",
			"BlueprintGraph",
			"CoreUObject",
			"DeveloperSettings",
			"DynamicMesh",
			"Engine",
			"GeometryCore",
			"GeometryFramework",
			"GeometryScriptingCore",
			"HTTPServer",
			"InputCore",
			"Json",
			"JsonUtilities",
			"Kismet",
			"KismetCompiler",
			"LevelEditor",
			"MaterialEditor",
			"PCG",
			"Projects",
			"Slate",
			"SlateCore",
			"ToolMenus",
			"UMG",
			"UMGEditor",
			"UnrealEd",
		});

		// Geometry integration dependencies are configured above.
	}
}


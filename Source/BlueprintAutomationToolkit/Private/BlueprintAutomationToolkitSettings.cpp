// Copyright 2026 AkaSoft. All Rights Reserved.

#include "BlueprintAutomationToolkitSettings.h"

UBlueprintAutomationToolkitSettings::UBlueprintAutomationToolkitSettings()
	: bEnableServer(false)
	, Port(9876)
	, bRequireAuthToken(true)
	, bSafeMode(true)
	, AssetImportMaxFileSizeMb(2048)
	, AssetImportMaxBatchSizeMb(4096)
	, AssetPipelineMaxCaptureFrames(3600)
{
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("BlueprintAutomationToolkit");

	AssetImportAllowedExtensions = {
		TEXT("fbx"), TEXT("obj"), TEXT("gltf"), TEXT("glb"),
		TEXT("usd"), TEXT("usda"), TEXT("usdc"), TEXT("abc"),
		TEXT("dae"), TEXT("3ds"), TEXT("stl"), TEXT("ply"),
		TEXT("png"), TEXT("jpg"), TEXT("jpeg"), TEXT("tga"),
		TEXT("tif"), TEXT("tiff"), TEXT("bmp"), TEXT("hdr"),
		TEXT("exr"), TEXT("wav"), TEXT("ogg")
	};
}

FName UBlueprintAutomationToolkitSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

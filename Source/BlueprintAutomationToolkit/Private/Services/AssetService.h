// Copyright 2026 AkaSoft. All Rights Reserved.

#pragma once

#include "Commands/AutomationCommand.h"

struct FBATAssetSaveRequest;
struct FBATAssetDuplicateRequest;
struct FBATAssetCreateRequest;

class FBlueprintAutomationToolkitModule;

class FAssetService
{
public:
	FAutomationResult CreateAsset(FBlueprintAutomationToolkitModule& Module, const FBATAssetCreateRequest& Request) const;
	FAutomationResult DuplicateAssets(FBlueprintAutomationToolkitModule& Module, const FBATAssetDuplicateRequest& Request) const;
	FAutomationResult SaveAssets(FBlueprintAutomationToolkitModule& Module, const FBATAssetSaveRequest& Request) const;
};
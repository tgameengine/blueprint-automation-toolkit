#pragma once

#include "Commands/AutomationCommand.h"

struct FBATAssetSaveRequest;
struct FBATAssetDuplicateRequest;

class FBlueprintAutomationToolkitModule;

class FAssetService
{
public:
	FAutomationResult DuplicateAssets(FBlueprintAutomationToolkitModule& Module, const FBATAssetDuplicateRequest& Request) const;
	FAutomationResult SaveAssets(FBlueprintAutomationToolkitModule& Module, const FBATAssetSaveRequest& Request) const;
};
#pragma once

#include "Commands/AutomationCommand.h"

struct FBATAssetSaveRequest;

class FBlueprintAutomationToolkitModule;

class FAssetService
{
public:
	FAutomationResult SaveAssets(FBlueprintAutomationToolkitModule& Module, const FBATAssetSaveRequest& Request) const;
};
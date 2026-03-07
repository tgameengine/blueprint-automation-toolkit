#pragma once

#include "Commands/AutomationCommand.h"

class FBlueprintAutomationToolkitModule;

class FReflectionPropertyService
{
public:
	FAutomationResult GetObject(FBlueprintAutomationToolkitModule& Module, const FString& RequestId, const TSharedPtr<class FJsonObject>& BodyObj) const;
	FAutomationResult ListProperties(FBlueprintAutomationToolkitModule& Module, const FString& RequestId, const TSharedPtr<class FJsonObject>& BodyObj) const;
	FAutomationResult SetProperty(FBlueprintAutomationToolkitModule& Module, const FString& RequestId, const TSharedPtr<class FJsonObject>& BodyObj) const;
};
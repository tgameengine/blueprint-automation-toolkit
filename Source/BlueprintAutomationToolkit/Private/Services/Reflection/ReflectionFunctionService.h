#pragma once

#include "Commands/AutomationCommand.h"

class FBlueprintAutomationToolkitModule;

class FReflectionFunctionService
{
public:
	FAutomationResult ListFunctions(FBlueprintAutomationToolkitModule& Module, const FString& RequestId, const TSharedPtr<class FJsonObject>& BodyObj) const;
	FAutomationResult CallFunction(FBlueprintAutomationToolkitModule& Module, const FString& RequestId, const TSharedPtr<class FJsonObject>& BodyObj) const;
};
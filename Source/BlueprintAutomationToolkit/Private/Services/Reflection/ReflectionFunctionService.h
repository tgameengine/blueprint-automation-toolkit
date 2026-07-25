// Copyright 2026 AkaSoft. All Rights Reserved.

#pragma once

#include "Commands/AutomationCommand.h"

class FBlueprintAutomationToolkitModule;

class FReflectionFunctionService
{
public:
	TSharedPtr<class FJsonObject> ListFunctions(UObject* Object, FString& OutError) const;

	FAutomationResult ListFunctions(FBlueprintAutomationToolkitModule& Module, const FString& RequestId, const TSharedPtr<class FJsonObject>& BodyObj) const;
	FAutomationResult CallFunction(FBlueprintAutomationToolkitModule& Module, const FString& RequestId, const TSharedPtr<class FJsonObject>& BodyObj) const;
};
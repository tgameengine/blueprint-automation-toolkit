// Copyright 2026 AkaSoft. All Rights Reserved.

#pragma once

#include "Commands/AutomationCommand.h"

class FBlueprintAutomationToolkitModule;

class FObjectAutomationService
{
public:
	FAutomationResult SetProperty(FBlueprintAutomationToolkitModule& Module, const FString& RequestId, const TSharedPtr<class FJsonObject>& BodyObj) const;
	FAutomationResult CallFunction(FBlueprintAutomationToolkitModule& Module, const FString& RequestId, const TSharedPtr<class FJsonObject>& BodyObj) const;
	FAutomationResult SpawnActor(FBlueprintAutomationToolkitModule& Module, const FString& RequestId, const TSharedPtr<class FJsonObject>& BodyObj) const;
};
// Copyright 2026 AkaSoft. All Rights Reserved.

#pragma once

#include "Commands/AutomationCommand.h"

class FBlueprintAutomationToolkitModule;
class FJsonObject;

/** Native, typed gameplay input and runtime verification helpers. */
class FRuntimeAutomationService
{
public:
	FAutomationResult ApplyPieInput(FBlueprintAutomationToolkitModule& Module, const TSharedPtr<FJsonObject>& Request) const;
	FAutomationResult EvaluateAssertions(FBlueprintAutomationToolkitModule& Module, const TSharedPtr<FJsonObject>& Request) const;
};

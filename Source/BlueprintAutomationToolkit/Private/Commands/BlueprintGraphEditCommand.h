// Copyright 2026 AkaSoft. All Rights Reserved.

#pragma once

#include "Commands/AutomationCommand.h"

class FBlueprintService;

class FBlueprintGraphEditCommand final : public FAutomationCommand
{
public:
	explicit FBlueprintGraphEditCommand(const FBlueprintService& InService);

	virtual FAutomationResult Execute(FAutomationContext& Context) override;

private:
	const FBlueprintService& Service;
};

// Copyright 2026 AkaSoft. All Rights Reserved.

#pragma once

#include "Commands/AutomationCommand.h"

class FObjectAutomationService;

class FCallObjectFunctionCommand final : public FAutomationCommand
{
public:
	explicit FCallObjectFunctionCommand(const FObjectAutomationService& InService);

	virtual FAutomationResult Execute(FAutomationContext& Context) override;

private:
	const FObjectAutomationService& Service;
};
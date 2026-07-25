// Copyright 2026 AkaSoft. All Rights Reserved.

#pragma once

#include "Commands/AutomationCommand.h"

class FObjectAutomationService;

class FSetObjectPropertyCommand final : public FAutomationCommand
{
public:
	explicit FSetObjectPropertyCommand(const FObjectAutomationService& InService);

	virtual FAutomationResult Execute(FAutomationContext& Context) override;

private:
	const FObjectAutomationService& Service;
};
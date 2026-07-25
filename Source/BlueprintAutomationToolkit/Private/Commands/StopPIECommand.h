// Copyright 2026 AkaSoft. All Rights Reserved.

#pragma once

#include "Commands/AutomationCommand.h"

class FPIEControlService;

class FStopPIECommand final : public FAutomationCommand
{
public:
	explicit FStopPIECommand(const FPIEControlService& InService);

	virtual FAutomationResult Execute(FAutomationContext& Context) override;

private:
	const FPIEControlService& Service;
};

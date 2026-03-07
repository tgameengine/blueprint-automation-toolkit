#pragma once

#include "Commands/AutomationCommand.h"

class FPIEControlService;

class FStartPIECommand final : public FAutomationCommand
{
public:
	explicit FStartPIECommand(const FPIEControlService& InService);

	virtual FAutomationResult Execute(FAutomationContext& Context) override;

private:
	const FPIEControlService& Service;
};

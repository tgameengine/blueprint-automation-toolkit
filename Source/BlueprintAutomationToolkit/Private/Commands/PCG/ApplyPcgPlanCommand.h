#pragma once

#include "Commands/AutomationCommand.h"

class FApplyPcgPlanCommand final : public FAutomationCommand
{
public:
	virtual FAutomationResult Execute(FAutomationContext& Context) override;
};
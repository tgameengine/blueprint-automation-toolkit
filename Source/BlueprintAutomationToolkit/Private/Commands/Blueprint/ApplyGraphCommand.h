#pragma once

#include "Commands/AutomationCommand.h"

class FApplyGraphCommand final : public FAutomationCommand
{
public:
	virtual FAutomationResult Execute(FAutomationContext& Context) override;
};
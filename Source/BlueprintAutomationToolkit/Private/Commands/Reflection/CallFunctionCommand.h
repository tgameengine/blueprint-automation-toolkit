#pragma once

#include "Commands/AutomationCommand.h"

class FCallFunctionCommand final : public FAutomationCommand
{
public:
	virtual FAutomationResult Execute(FAutomationContext& Context) override;
};
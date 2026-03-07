#pragma once

#include "Commands/AutomationCommand.h"

class FSetPropertyCommand final : public FAutomationCommand
{
public:
	virtual FAutomationResult Execute(FAutomationContext& Context) override;
};
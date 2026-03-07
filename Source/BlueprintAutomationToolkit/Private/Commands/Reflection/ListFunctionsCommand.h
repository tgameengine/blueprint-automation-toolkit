#pragma once

#include "Commands/AutomationCommand.h"

class FListFunctionsCommand final : public FAutomationCommand
{
public:
	virtual FAutomationResult Execute(FAutomationContext& Context) override;
};
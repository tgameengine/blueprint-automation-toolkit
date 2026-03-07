#pragma once

#include "Commands/AutomationCommand.h"

class FReadGraphCommand final : public FAutomationCommand
{
public:
	virtual FAutomationResult Execute(FAutomationContext& Context) override;
};
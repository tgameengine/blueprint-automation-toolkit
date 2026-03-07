#pragma once

#include "Commands/AutomationCommand.h"

class FDescribeObjectCommand : public FAutomationCommand
{
public:
	virtual FAutomationResult Execute(FAutomationContext& Context) override;
};
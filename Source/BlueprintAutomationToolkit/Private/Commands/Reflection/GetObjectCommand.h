#pragma once

#include "Commands/AutomationCommand.h"

class FGetObjectCommand final : public FAutomationCommand
{
public:
	virtual FAutomationResult Execute(FAutomationContext& Context) override;
};
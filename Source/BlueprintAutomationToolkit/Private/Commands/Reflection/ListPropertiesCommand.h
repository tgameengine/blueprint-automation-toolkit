#pragma once

#include "Commands/AutomationCommand.h"

class FListPropertiesCommand final : public FAutomationCommand
{
public:
	virtual FAutomationResult Execute(FAutomationContext& Context) override;
};
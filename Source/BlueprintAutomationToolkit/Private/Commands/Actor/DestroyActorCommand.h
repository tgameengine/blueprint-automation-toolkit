#pragma once

#include "Commands/AutomationCommand.h"

class FDestroyActorCommand final : public FAutomationCommand
{
public:
	virtual FAutomationResult Execute(FAutomationContext& Context) override;
};
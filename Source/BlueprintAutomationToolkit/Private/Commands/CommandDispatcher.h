#pragma once

#include "Commands/AutomationCommand.h"

class FCommandDispatcher
{
public:
	FAutomationResult Dispatch(FAutomationCommand& Command, FAutomationContext& Context) const;
};

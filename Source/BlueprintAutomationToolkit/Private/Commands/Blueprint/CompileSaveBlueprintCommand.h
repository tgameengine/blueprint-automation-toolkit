#pragma once

#include "Commands/AutomationCommand.h"

class FCompileSaveBlueprintCommand final : public FAutomationCommand
{
public:
	virtual FAutomationResult Execute(FAutomationContext& Context) override;
};
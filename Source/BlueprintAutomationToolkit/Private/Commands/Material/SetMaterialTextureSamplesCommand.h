#pragma once

#include "Commands/AutomationCommand.h"

class FSetMaterialTextureSamplesCommand final : public FAutomationCommand
{
public:
	virtual FAutomationResult Execute(FAutomationContext& Context) override;
};

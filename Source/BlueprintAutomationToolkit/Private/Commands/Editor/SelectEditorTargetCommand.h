#pragma once

#include "Commands/AutomationCommand.h"

class FSelectEditorTargetCommand final : public FAutomationCommand
{
public:
	virtual FAutomationResult Execute(FAutomationContext& Context) override;
};
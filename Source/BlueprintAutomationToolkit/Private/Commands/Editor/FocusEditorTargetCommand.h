#pragma once

#include "Commands/AutomationCommand.h"

class FFocusEditorTargetCommand final : public FAutomationCommand
{
public:
	virtual FAutomationResult Execute(FAutomationContext& Context) override;
};
#pragma once

#include "Commands/AutomationCommand.h"

class FBlueprintService
{
public:
	using FOperationFn = TFunction<FAutomationResult(FAutomationContext& Context)>;

	explicit FBlueprintService(FOperationFn InOperation);

	FAutomationResult Execute(FAutomationContext& Context) const;

private:
	FOperationFn Operation;
};

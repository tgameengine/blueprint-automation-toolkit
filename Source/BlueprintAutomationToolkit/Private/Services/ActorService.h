#pragma once

#include "Commands/AutomationCommand.h"

class FActorService
{
public:
	using FOperationFn = TFunction<FAutomationResult(FAutomationContext& Context)>;

	explicit FActorService(FOperationFn InOperation);

	FAutomationResult Execute(FAutomationContext& Context) const;

private:
	FOperationFn Operation;
};

#pragma once

#include "Commands/AutomationCommand.h"

class FActorService;

class FExecCommand final : public FAutomationCommand
{
public:
	explicit FExecCommand(const FActorService& InService);

	virtual FAutomationResult Execute(FAutomationContext& Context) override;

private:
	const FActorService& Service;
};

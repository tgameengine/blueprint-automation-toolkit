#pragma once

#include "Commands/AutomationCommand.h"

class FActorService;

class FGetActorInfoCommand final : public FAutomationCommand
{
public:
	explicit FGetActorInfoCommand(const FActorService& InService);

	virtual FAutomationResult Execute(FAutomationContext& Context) override;

private:
	const FActorService& Service;
};

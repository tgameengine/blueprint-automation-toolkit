#pragma once

#include "Commands/AutomationCommand.h"

class FActorService;

class FTeleportActorCommand final : public FAutomationCommand
{
public:
	explicit FTeleportActorCommand(const FActorService& InService);

	virtual FAutomationResult Execute(FAutomationContext& Context) override;

private:
	const FActorService& Service;
};

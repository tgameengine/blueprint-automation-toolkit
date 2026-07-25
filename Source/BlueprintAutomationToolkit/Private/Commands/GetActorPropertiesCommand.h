// Copyright 2026 AkaSoft. All Rights Reserved.

#pragma once

#include "Commands/AutomationCommand.h"

class FActorService;

class FGetActorPropertiesCommand final : public FAutomationCommand
{
public:
	explicit FGetActorPropertiesCommand(const FActorService& InService);

	virtual FAutomationResult Execute(FAutomationContext& Context) override;

private:
	const FActorService& Service;
};

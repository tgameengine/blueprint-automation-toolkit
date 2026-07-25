// Copyright 2026 AkaSoft. All Rights Reserved.

#pragma once

#include "Commands/AutomationCommand.h"

class FObjectAutomationService;

class FSpawnActorCommand final : public FAutomationCommand
{
public:
	explicit FSpawnActorCommand(const FObjectAutomationService& InService);

	virtual FAutomationResult Execute(FAutomationContext& Context) override;

private:
	const FObjectAutomationService& Service;
};
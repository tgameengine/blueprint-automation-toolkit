// Copyright 2026 AkaSoft. All Rights Reserved.

#pragma once

#include "Commands/AutomationCommand.h"

class FDescribeObjectCommand : public FAutomationCommand
{
public:
	virtual FAutomationResult Execute(FAutomationContext& Context) override;
};
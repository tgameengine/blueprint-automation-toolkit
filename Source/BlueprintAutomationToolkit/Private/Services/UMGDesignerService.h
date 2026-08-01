// Copyright 2026 AkaSoft. All Rights Reserved.

#pragma once

#include "Commands/AutomationCommand.h"

class FJsonObject;
class UWidgetBlueprint;

/**
 * Native, declarative UMG Designer automation.  The service deliberately works
 * on Widget Blueprint templates and never executes Python or arbitrary code.
 */
class FUMGDesignerService
{
public:
	FAutomationResult DescribeSchema() const;
	FAutomationResult CreateWidgetBlueprint(const TSharedPtr<FJsonObject>& Request) const;
	FAutomationResult ReadDesigner(const TSharedPtr<FJsonObject>& Request) const;
	FAutomationResult ApplyDesigner(const TSharedPtr<FJsonObject>& Request) const;

	/** Exposed for focused editor automation tests without creating an asset. */
	FAutomationResult ApplyDesignerToBlueprint(UWidgetBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Request) const;
};

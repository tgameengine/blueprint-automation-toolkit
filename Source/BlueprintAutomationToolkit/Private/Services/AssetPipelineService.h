// Copyright 2026 AkaSoft. All Rights Reserved.

#pragma once

#include "Commands/AutomationCommand.h"

class FBlueprintAutomationToolkitModule;
class FJsonObject;

/**
 * Agent-oriented asset pipeline built on Unreal's editor importers.
 *
 * All public operations marshal to the Game Thread when necessary so the same
 * implementation can be used by synchronous HTTP routes and background jobs.
 */
class FAssetPipelineService
{
public:
	FAutomationResult DescribeImportFormats(FBlueprintAutomationToolkitModule& Module) const;
	FAutomationResult ImportAssets(FBlueprintAutomationToolkitModule& Module, const TSharedPtr<FJsonObject>& Request) const;
	FAutomationResult InspectAssets(FBlueprintAutomationToolkitModule& Module, const TSharedPtr<FJsonObject>& Request) const;
	FAutomationResult ConfigureAssets(FBlueprintAutomationToolkitModule& Module, const TSharedPtr<FJsonObject>& Request) const;
	FAutomationResult ValidateAssets(FBlueprintAutomationToolkitModule& Module, const TSharedPtr<FJsonObject>& Request) const;
	FAutomationResult CreateShowcaseAndCapture(FBlueprintAutomationToolkitModule& Module, const TSharedPtr<FJsonObject>& Request) const;
	FAutomationResult ExecutePipeline(
		FBlueprintAutomationToolkitModule& Module,
		const TSharedPtr<FJsonObject>& Request,
		const FString& JobId = FString()) const;

private:
	FAutomationResult RunOnGameThread(
		FBlueprintAutomationToolkitModule& Module,
		TFunction<FAutomationResult()> Operation,
		double TimeoutSeconds) const;
	FAutomationResult ImportAssetsOnGameThread(FBlueprintAutomationToolkitModule& Module, const TSharedPtr<FJsonObject>& Request) const;
	FAutomationResult InspectAssetsOnGameThread(FBlueprintAutomationToolkitModule& Module, const TSharedPtr<FJsonObject>& Request) const;
	FAutomationResult ConfigureAssetsOnGameThread(FBlueprintAutomationToolkitModule& Module, const TSharedPtr<FJsonObject>& Request) const;
	FAutomationResult ValidateAssetsOnGameThread(FBlueprintAutomationToolkitModule& Module, const TSharedPtr<FJsonObject>& Request) const;
	FAutomationResult CreateShowcaseAndCaptureOnGameThread(FBlueprintAutomationToolkitModule& Module, const TSharedPtr<FJsonObject>& Request) const;
	FAutomationResult ExecutePipelineOnGameThread(
		FBlueprintAutomationToolkitModule& Module,
		const TSharedPtr<FJsonObject>& Request,
		const FString& JobId) const;
};

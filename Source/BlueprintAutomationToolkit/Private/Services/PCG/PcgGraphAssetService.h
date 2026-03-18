#pragma once

#include "CoreMinimal.h"
#include "Automation/AutomationCommand.h"

class UPCGGraph;
class UPCGGraphInterface;

struct FPcgApplyRequest;

struct FPcgGraphAssetHandle
{
	FString GraphObjectPath;
	FString PackagePath;
	FString AssetName;
	UPCGGraphInterface* GraphInterface = nullptr;
	UPCGGraph* Graph = nullptr;
	bool bCreated = false;
	bool bLoadedExisting = false;
	bool bSaved = false;
};

class FPcgGraphAssetService
{
public:
	static FAutomationResult AcquireGraphAsset(const FPcgApplyRequest& Request, FPcgGraphAssetHandle& OutHandle);
	static FAutomationResult SaveGraphAsset(FPcgGraphAssetHandle& Handle);
};
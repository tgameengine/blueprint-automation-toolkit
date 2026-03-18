#pragma once

#include "CoreMinimal.h"
#include "Automation/AutomationCommand.h"

struct FPcgApplyRequest;
struct FPcgGraphAssetHandle;

class FPcgGraphApplyService
{
public:
	static FAutomationResult ApplyOps(const FPcgApplyRequest& Request, FPcgGraphAssetHandle& GraphHandle);
};
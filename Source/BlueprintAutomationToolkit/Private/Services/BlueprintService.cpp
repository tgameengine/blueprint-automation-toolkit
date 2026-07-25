// Copyright 2026 AkaSoft. All Rights Reserved.

#include "Services/BlueprintService.h"

FBlueprintService::FBlueprintService(FOperationFn InOperation)
	: Operation(MoveTemp(InOperation))
{
}

FAutomationResult FBlueprintService::Execute(FAutomationContext& Context) const
{
	if (!Operation)
	{
		return FAutomationResult::Error(TEXT("service_unavailable"), TEXT("Blueprint service is not configured"), 500);
	}
	return Operation(Context);
}

// Copyright 2026 AkaSoft. All Rights Reserved.

#include "Services/ActorService.h"

FActorService::FActorService(FOperationFn InOperation)
	: Operation(MoveTemp(InOperation))
{
}

FAutomationResult FActorService::Execute(FAutomationContext& Context) const
{
	if (!Operation)
	{
		return FAutomationResult::Error(TEXT("service_unavailable"), TEXT("Actor service is not configured"), 500);
	}
	return Operation(Context);
}

// Copyright 2026 AkaSoft. All Rights Reserved.

#include "Commands/BlueprintGraphEditCommand.h"

#include "Services/BlueprintService.h"

FBlueprintGraphEditCommand::FBlueprintGraphEditCommand(const FBlueprintService& InService)
	: Service(InService)
{
}

FAutomationResult FBlueprintGraphEditCommand::Execute(FAutomationContext& Context)
{
	return Service.Execute(Context);
}

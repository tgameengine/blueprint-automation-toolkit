// Copyright 2026 AkaSoft. All Rights Reserved.

#include "Commands/StartPIECommand.h"

#include "Services/PIEControlService.h"

FStartPIECommand::FStartPIECommand(const FPIEControlService& InService)
	: Service(InService)
{
}

FAutomationResult FStartPIECommand::Execute(FAutomationContext& Context)
{
	return Service.QueueStart();
}

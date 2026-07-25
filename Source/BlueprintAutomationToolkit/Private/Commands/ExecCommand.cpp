// Copyright 2026 AkaSoft. All Rights Reserved.

#include "Commands/ExecCommand.h"

#include "Services/ActorService.h"

FExecCommand::FExecCommand(const FActorService& InService)
	: Service(InService)
{
}

FAutomationResult FExecCommand::Execute(FAutomationContext& Context)
{
	return Service.Execute(Context);
}

#include "Commands/StopPIECommand.h"

#include "Services/PIEControlService.h"

FStopPIECommand::FStopPIECommand(const FPIEControlService& InService)
	: Service(InService)
{
}

FAutomationResult FStopPIECommand::Execute(FAutomationContext& Context)
{
	return Service.QueueStop();
}

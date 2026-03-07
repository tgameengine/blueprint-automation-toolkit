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

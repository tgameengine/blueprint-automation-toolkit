#include "Commands/GetActorInfoCommand.h"

#include "Services/ActorService.h"

FGetActorInfoCommand::FGetActorInfoCommand(const FActorService& InService)
	: Service(InService)
{
}

FAutomationResult FGetActorInfoCommand::Execute(FAutomationContext& Context)
{
	return Service.Execute(Context);
}

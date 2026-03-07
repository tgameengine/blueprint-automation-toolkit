#include "Commands/GetActorPropertiesCommand.h"

#include "Services/ActorService.h"

FGetActorPropertiesCommand::FGetActorPropertiesCommand(const FActorService& InService)
	: Service(InService)
{
}

FAutomationResult FGetActorPropertiesCommand::Execute(FAutomationContext& Context)
{
	return Service.Execute(Context);
}

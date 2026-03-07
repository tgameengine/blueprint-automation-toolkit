#include "Commands/TeleportActorCommand.h"

#include "Services/ActorService.h"

FTeleportActorCommand::FTeleportActorCommand(const FActorService& InService)
	: Service(InService)
{
}

FAutomationResult FTeleportActorCommand::Execute(FAutomationContext& Context)
{
	return Service.Execute(Context);
}

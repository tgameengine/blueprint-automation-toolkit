#include "BlueprintAutomationToolkitModule.h"

void FBlueprintAutomationToolkitModule::BindUObjectRoutes()
{
	BindActorSpawnRoute();
	BindActorFindRoute();
}

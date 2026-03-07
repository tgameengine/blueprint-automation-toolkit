#include "BlueprintAutomationToolkitModule.h"

namespace
{
	struct FRouteBinder
	{
		void (FBlueprintAutomationToolkitModule::*Fn)();
	};
}

void FBlueprintAutomationToolkitModule::BindRoutes()
{
	const FRouteBinder RouteBinders[] = {
		{&FBlueprintAutomationToolkitModule::BindDiscoverRoutes},
		{&FBlueprintAutomationToolkitModule::BindEditorRoutes},
		{&FBlueprintAutomationToolkitModule::BindPieControlRoutes},
		{&FBlueprintAutomationToolkitModule::BindAutomationCoreRoutes},
		{&FBlueprintAutomationToolkitModule::BindRegisteredAutomationRoutes},
		{&FBlueprintAutomationToolkitModule::BindActorRoutes},
		{&FBlueprintAutomationToolkitModule::BindActorInfoRoutes},
		{&FBlueprintAutomationToolkitModule::BindBlueprintRoutes},
		{&FBlueprintAutomationToolkitModule::BindAssetRoutes},
		{&FBlueprintAutomationToolkitModule::BindActionsRoutes},
	};

	for (const FRouteBinder& Binder : RouteBinders)
	{
		(this->*Binder.Fn)();
	}

	if (bEnableExecRoute)
	{
		BindExecRoute();
	}
}

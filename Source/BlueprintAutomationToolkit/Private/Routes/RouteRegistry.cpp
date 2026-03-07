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
		{&FBlueprintAutomationToolkitModule::BindEditorRoutes},
		{&FBlueprintAutomationToolkitModule::BindPieControlRoutes},
		{&FBlueprintAutomationToolkitModule::BindAutomationCoreRoutes},
		{&FBlueprintAutomationToolkitModule::BindRegisteredAutomationRoutes},
		{&FBlueprintAutomationToolkitModule::BindActorInfoRoutes},
		{&FBlueprintAutomationToolkitModule::BindBlueprintRoutes},
		{&FBlueprintAutomationToolkitModule::BindUObjectRoutes},
		{&FBlueprintAutomationToolkitModule::BindReflectionRoutes},
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

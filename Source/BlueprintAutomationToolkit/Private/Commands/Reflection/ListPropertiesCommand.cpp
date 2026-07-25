// Copyright 2026 AkaSoft. All Rights Reserved.

#include "Commands/Reflection/ListPropertiesCommand.h"

#include "Services/Reflection/ReflectionPropertyService.h"
#include "Services/Reflection/ReflectionTypes.h"

FAutomationResult FListPropertiesCommand::Execute(FAutomationContext& Context)
{
	if (!Context.Module || !Context.Body.IsValid())
	{
		return BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("bad_args"), TEXT("Missing request context."), 400);
	}

	static const FReflectionPropertyService Service;
	return Service.ListProperties(*Context.Module, Context.RequestId, Context.Body);
}
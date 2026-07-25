// Copyright 2026 AkaSoft. All Rights Reserved.

#include "Commands/Reflection/ListFunctionsCommand.h"

#include "Services/Reflection/ReflectionFunctionService.h"
#include "Services/Reflection/ReflectionTypes.h"

FAutomationResult FListFunctionsCommand::Execute(FAutomationContext& Context)
{
	if (!Context.Module || !Context.Body.IsValid())
	{
		return BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("bad_args"), TEXT("Missing request context."), 400);
	}

	static const FReflectionFunctionService Service;
	return Service.ListFunctions(*Context.Module, Context.RequestId, Context.Body);
}
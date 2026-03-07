#include "Commands/Reflection/CallFunctionCommand.h"

#include "Services/Reflection/ReflectionFunctionService.h"
#include "Services/Reflection/ReflectionTypes.h"

FAutomationResult FCallFunctionCommand::Execute(FAutomationContext& Context)
{
	if (!Context.Module || !Context.Body.IsValid())
	{
		return BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("bad_args"), TEXT("Missing request context."), 400);
	}

	static const FReflectionFunctionService Service;
	return Service.CallFunction(*Context.Module, Context.RequestId, Context.Body);
}
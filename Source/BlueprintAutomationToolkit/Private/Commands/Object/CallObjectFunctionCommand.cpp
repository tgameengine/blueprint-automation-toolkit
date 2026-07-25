// Copyright 2026 AkaSoft. All Rights Reserved.

#include "Commands/Object/CallObjectFunctionCommand.h"

#include "Dom/JsonObject.h"
#include "Services/ObjectAutomationService.h"

FCallObjectFunctionCommand::FCallObjectFunctionCommand(const FObjectAutomationService& InService)
	: Service(InService)
{
}

FAutomationResult FCallObjectFunctionCommand::Execute(FAutomationContext& Context)
{
	if (!Context.Module || !Context.Body.IsValid())
	{
		return FAutomationResult::Error(TEXT("bad_args"), TEXT("Missing request context"), 400);
	}

	FString Path;
	FString FunctionName;
	Context.Body->TryGetStringField(TEXT("path"), Path);
	Context.Body->TryGetStringField(TEXT("function"), FunctionName);
	if (Path.TrimStartAndEnd().IsEmpty() || FunctionName.TrimStartAndEnd().IsEmpty())
	{
		return FAutomationResult::Error(TEXT("bad_args"), TEXT("Body must include non-empty 'path' and 'function'"), 400);
	}

	return Service.CallFunction(*Context.Module, Context.RequestId, Context.Body);
}
// Copyright 2026 AkaSoft. All Rights Reserved.

#include "Commands/Actor/SpawnActorCommand.h"

#include "Dom/JsonObject.h"
#include "Services/ObjectAutomationService.h"

FSpawnActorCommand::FSpawnActorCommand(const FObjectAutomationService& InService)
	: Service(InService)
{
}

FAutomationResult FSpawnActorCommand::Execute(FAutomationContext& Context)
{
	if (!Context.Module || !Context.Body.IsValid())
	{
		return FAutomationResult::Error(TEXT("bad_args"), TEXT("Missing request context"), 400);
	}

	FString ClassPath;
	Context.Body->TryGetStringField(TEXT("class"), ClassPath);
	if (ClassPath.TrimStartAndEnd().IsEmpty())
	{
		return FAutomationResult::Error(TEXT("bad_args"), TEXT("Body must include non-empty 'class'"), 400);
	}

	return Service.SpawnActor(*Context.Module, Context.RequestId, Context.Body);
}
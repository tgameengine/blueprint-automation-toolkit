#include "Commands/Object/SetObjectPropertyCommand.h"

#include "BlueprintAutomationToolkitModule.h"
#include "Dom/JsonObject.h"
#include "Services/ObjectAutomationService.h"

FSetObjectPropertyCommand::FSetObjectPropertyCommand(const FObjectAutomationService& InService)
	: Service(InService)
{
}

FAutomationResult FSetObjectPropertyCommand::Execute(FAutomationContext& Context)
{
	if (!Context.Module || !Context.Body.IsValid())
	{
		return FAutomationResult::Error(TEXT("bad_args"), TEXT("Missing request context"), 400);
	}

	FString Path;
	Context.Body->TryGetStringField(TEXT("path"), Path);
	const TSharedPtr<FJsonObject>* ValuesObj = nullptr;
	Context.Body->TryGetObjectField(TEXT("values"), ValuesObj);
	if (Path.TrimStartAndEnd().IsEmpty() || !ValuesObj || !ValuesObj->IsValid())
	{
		return FAutomationResult::Error(TEXT("bad_args"), TEXT("Body must include non-empty 'path' and object field 'values'"), 400);
	}

	return Service.SetProperty(*Context.Module, Context.RequestId, Context.Body);
}
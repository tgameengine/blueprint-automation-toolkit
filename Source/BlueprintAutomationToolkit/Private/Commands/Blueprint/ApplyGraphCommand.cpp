// Copyright 2026 AkaSoft. All Rights Reserved.

#include "Commands/Blueprint/ApplyGraphCommand.h"

#include "Core/EditorExecution.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Routes/Blueprint/BlueprintGraphApplyRequest.h"
#include "Services/BlueprintGraphService.h"

FAutomationResult FApplyGraphCommand::Execute(FAutomationContext& Context)
{
	if (!Context.Body.IsValid())
	{
		return FAutomationResult::Error(TEXT("bad_json"), TEXT("Invalid JSON body"), 400);
	}

	FBlueprintGraphApplyRequest ParsedRequest;
	TArray<FString> ParseErrors;
	if (!BAT::BlueprintGraphApplyRequest::Parse(Context.Body, ParsedRequest, ParseErrors))
	{
		TSharedRef<FJsonObject> ErrorObj = MakeShared<FJsonObject>();
		ErrorObj->SetBoolField(TEXT("ok"), false);

		TArray<TSharedPtr<FJsonValue>> ErrorArray;
		for (const FString& Error : ParseErrors)
		{
			TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("code"), TEXT("invalid_request"));
			Entry->SetStringField(TEXT("message"), Error);
			ErrorArray.Add(MakeShared<FJsonValueObject>(Entry));
		}
		ErrorObj->SetArrayField(TEXT("errors"), ErrorArray);
		ErrorObj->SetArrayField(TEXT("warnings"), TArray<TSharedPtr<FJsonValue>>());
		ErrorObj->SetObjectField(TEXT("data"), MakeShared<FJsonObject>());

		FAutomationResult Result = FAutomationResult::Error(TEXT("invalid_request"), TEXT("Blueprint graph apply request parsing failed"), 400);
		Result.Data = MakeShared<FJsonValueObject>(ErrorObj);
		return Result;
	}

	TOptional<FAutomationResult> Result;
	const bool bCompleted = BAT::EditorExecution::RunOnGameThreadAndWaitVoid([ParsedRequest, &Result]()
	{
		Result = FBlueprintGraphService::ApplyGraphPatch(ParsedRequest);
	}, 10.0f);

	if (!bCompleted)
	{
		return FAutomationResult::Error(TEXT("game_thread_timeout"), TEXT("Timed out waiting for GameThread execution"), 504);
	}

	return Result.IsSet() ? Result.GetValue() : FAutomationResult::Error(TEXT("internal_error"), TEXT("No result produced by graph apply operation"), 500);
}
#include "Commands/PCG/ApplyPcgPlanCommand.h"

#include "Core/EditorExecution.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Routes/PCG/PcgApplyRequest.h"
#include "Services/PCG/PcgGraphApplyService.h"
#include "Services/PCG/PcgGraphAssetService.h"

namespace
{
	static TSharedPtr<FJsonValueObject> BuildIssueEnvelope(const FString& Code, const FString& Message, const TArray<FString>& ParseErrors, const TSharedPtr<FJsonObject>& Details = nullptr)
	{
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetBoolField(TEXT("ok"), false);
		Root->SetObjectField(TEXT("data"), Details.IsValid() ? Details.ToSharedRef() : MakeShared<FJsonObject>());
		Root->SetArrayField(TEXT("warnings"), TArray<TSharedPtr<FJsonValue>>());

		TArray<TSharedPtr<FJsonValue>> Errors;
		if (ParseErrors.Num() > 0)
		{
			for (const FString& ParseError : ParseErrors)
			{
				TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("code"), Code);
				Entry->SetStringField(TEXT("message"), ParseError);
				Entry->SetBoolField(TEXT("recoverable"), true);
				Entry->SetStringField(TEXT("suggestedAction"), TEXT("fix_request"));
				Entry->SetObjectField(TEXT("details"), MakeShared<FJsonObject>());
				Errors.Add(MakeShared<FJsonValueObject>(Entry));
			}
		}
		else
		{
			TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("code"), Code);
			Entry->SetStringField(TEXT("message"), Message);
			Entry->SetBoolField(TEXT("recoverable"), true);
			Entry->SetStringField(TEXT("suggestedAction"), TEXT("retry_later"));
			Entry->SetObjectField(TEXT("details"), MakeShared<FJsonObject>());
			Errors.Add(MakeShared<FJsonValueObject>(Entry));
		}

		Root->SetArrayField(TEXT("errors"), Errors);
		return MakeShared<FJsonValueObject>(Root);
	}
}

FAutomationResult FApplyPcgPlanCommand::Execute(FAutomationContext& Context)
{
	if (!Context.Body.IsValid())
	{
		return FAutomationResult::ErrorWithData(TEXT("bad_json"), TEXT("Invalid JSON body"), 400, BuildIssueEnvelope(TEXT("invalid_request"), TEXT("Invalid JSON body"), { TEXT("invalid_payload") }));
	}

	FPcgApplyRequest ParsedRequest;
	TArray<FString> ParseErrors;
	if (!BAT::PcgApplyRequest::Parse(Context.Body, ParsedRequest, ParseErrors))
	{
		return FAutomationResult::ErrorWithData(TEXT("invalid_request"), TEXT("PCG apply request parsing failed"), 400, BuildIssueEnvelope(TEXT("invalid_request"), TEXT("PCG apply request parsing failed"), ParseErrors));
	}

	TOptional<FAutomationResult> LifecycleResult;
	const bool bCompleted = BAT::EditorExecution::RunOnGameThreadAndWaitVoid([&ParsedRequest, &LifecycleResult]()
	{
		FPcgGraphAssetHandle Handle;
		const FAutomationResult Result = FPcgGraphAssetService::AcquireGraphAsset(ParsedRequest, Handle);
		if (!Result.bSuccess)
		{
			LifecycleResult = Result;
			return;
		}

		const FAutomationResult ApplyResult = FPcgGraphApplyService::ApplyOps(ParsedRequest, Handle);
		if (!ApplyResult.bSuccess)
		{
			LifecycleResult = ApplyResult;
			return;
		}

		if (ParsedRequest.Options.bSave)
		{
			const FAutomationResult SaveResult = FPcgGraphAssetService::SaveGraphAsset(Handle);
			if (!SaveResult.bSuccess)
			{
				LifecycleResult = SaveResult;
				return;
			}
		}

		TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("ok"), true);
		Data->SetStringField(TEXT("graph"), ParsedRequest.GraphPath);
		Data->SetBoolField(TEXT("created"), Handle.bCreated);
		Data->SetBoolField(TEXT("loadedExisting"), Handle.bLoadedExisting);
		Data->SetBoolField(TEXT("saved"), Handle.bSaved);
		Data->SetBoolField(TEXT("graphReady"), Handle.GraphInterface != nullptr);
		Data->SetArrayField(TEXT("warnings"), TArray<TSharedPtr<FJsonValue>>());
		Data->SetArrayField(TEXT("errors"), TArray<TSharedPtr<FJsonValue>>());
		Data->SetObjectField(TEXT("data"), MakeShared<FJsonObject>());
		LifecycleResult = FAutomationResult::Ok(MakeShared<FJsonValueObject>(Data));
	}, 10.0f);

	if (!bCompleted || !LifecycleResult.IsSet())
	{
		return FAutomationResult::Error(TEXT("game_thread_timeout"), TEXT("Timed out waiting for GameThread execution"), 504);
	}

	return LifecycleResult.GetValue();
}
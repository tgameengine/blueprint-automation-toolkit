#include "Commands/PCG/ApplyPcgPlanCommand.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Routes/PCG/PcgApplyRequest.h"

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

	TSharedRef<FJsonObject> Details = MakeShared<FJsonObject>();
	Details->SetStringField(TEXT("graph"), ParsedRequest.GraphPath);
	Details->SetNumberField(TEXT("opCount"), ParsedRequest.Ops.Num());
	Details->SetNumberField(TEXT("parameterCount"), ParsedRequest.Parameters.Num());

	return FAutomationResult::ErrorWithData(
		TEXT("not_implemented"),
		TEXT("PCG apply executor is not implemented yet."),
		501,
		BuildIssueEnvelope(TEXT("not_implemented"), TEXT("PCG apply executor is not implemented yet."), {}, Details));
}
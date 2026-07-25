// Copyright 2026 AkaSoft. All Rights Reserved.

#include "Services/BlueprintCompileDiagnosticsService.h"

#include "Dom/JsonObject.h"
#include "Engine/Blueprint.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Logging/TokenizedMessage.h"

namespace
{
	static FString BlueprintStatusToString(EBlueprintStatus Status)
	{
		switch (Status)
		{
		case BS_Unknown:
			return TEXT("unknown");
		case BS_Dirty:
			return TEXT("dirty");
		case BS_Error:
			return TEXT("error");
		case BS_UpToDate:
			return TEXT("up_to_date");
		case BS_BeingCreated:
			return TEXT("being_created");
		case BS_UpToDateWithWarnings:
			return TEXT("up_to_date_with_warnings");
		default:
			return TEXT("unknown");
		}
	}

	static FString SeverityToString(EMessageSeverity::Type Severity)
	{
		switch (Severity)
		{
		case EMessageSeverity::Error:
			return TEXT("error");
		case EMessageSeverity::PerformanceWarning:
			return TEXT("performance_warning");
		case EMessageSeverity::Warning:
			return TEXT("warning");
		case EMessageSeverity::Info:
			return TEXT("info");
		default:
			return TEXT("info");
		}
	}

	static FString SeverityToCode(EMessageSeverity::Type Severity)
	{
		switch (Severity)
		{
		case EMessageSeverity::Error:
			return TEXT("compile_error");
		case EMessageSeverity::PerformanceWarning:
			return TEXT("compile_performance_warning");
		case EMessageSeverity::Warning:
			return TEXT("compile_warning");
		case EMessageSeverity::Info:
			return TEXT("compile_info");
		default:
			return TEXT("compile_info");
		}
	}

	static TSharedPtr<FJsonValue> MakeIssueValue(const TSharedRef<FTokenizedMessage>& Message, int32 Index)
	{
		TSharedRef<FJsonObject> Issue = MakeShared<FJsonObject>();
		const EMessageSeverity::Type Severity = Message->GetSeverity();
		Issue->SetStringField(TEXT("code"), SeverityToCode(Severity));
		Issue->SetStringField(TEXT("severity"), SeverityToString(Severity));
		Issue->SetStringField(TEXT("message"), Message->ToText().ToString().TrimStartAndEnd());
		Issue->SetNumberField(TEXT("index"), Index);

		const FName Identifier = Message->GetIdentifier();
		if (!Identifier.IsNone())
		{
			Issue->SetStringField(TEXT("identifier"), Identifier.ToString());
		}

		return MakeShared<FJsonValueObject>(Issue);
	}

	static TSharedPtr<FJsonValue> MakeSyntheticIssue(const FString& Code, const FString& Severity, const FString& Message)
	{
		TSharedRef<FJsonObject> Issue = MakeShared<FJsonObject>();
		Issue->SetStringField(TEXT("code"), Code);
		Issue->SetStringField(TEXT("severity"), Severity);
		Issue->SetStringField(TEXT("message"), Message);
		return MakeShared<FJsonValueObject>(Issue);
	}
}

BAT::BlueprintCompileDiagnostics::FDiagnostics BAT::BlueprintCompileDiagnostics::Compile(UBlueprint* Blueprint)
{
	FDiagnostics Diagnostics;
	if (!Blueprint)
	{
		Diagnostics.CompileStatus = TEXT("not_found");
		Diagnostics.Errors.Add(MakeSyntheticIssue(TEXT("blueprint_not_found"), TEXT("error"), TEXT("Blueprint could not be loaded for compilation.")));
		Diagnostics.ErrorCount = 1;
		return Diagnostics;
	}

	FCompilerResultsLog ResultsLog;
	ResultsLog.SetSilentMode(true);
	ResultsLog.bAnnotateMentionedNodes = false;
	ResultsLog.SetSourcePath(Blueprint->GetPathName());

	FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::None, &ResultsLog);

	Diagnostics.CompileStatus = BlueprintStatusToString(Blueprint->Status);
	Diagnostics.ErrorCount = ResultsLog.NumErrors;
	Diagnostics.WarningCount = ResultsLog.NumWarnings;

	for (int32 MessageIndex = 0; MessageIndex < ResultsLog.Messages.Num(); ++MessageIndex)
	{
		const TSharedRef<FTokenizedMessage>& Message = ResultsLog.Messages[MessageIndex];
		const EMessageSeverity::Type Severity = Message->GetSeverity();
		if (Severity == EMessageSeverity::Error)
		{
			Diagnostics.Errors.Add(MakeIssueValue(Message, MessageIndex));
		}
		else if (Severity == EMessageSeverity::Warning || Severity == EMessageSeverity::PerformanceWarning)
		{
			Diagnostics.Warnings.Add(MakeIssueValue(Message, MessageIndex));
		}
	}

	if (Blueprint->Status == BS_Error && Diagnostics.Errors.Num() == 0)
	{
		Diagnostics.Errors.Add(MakeSyntheticIssue(TEXT("compile_failed"), TEXT("error"), TEXT("Blueprint compile completed with an error status.")));
		Diagnostics.ErrorCount = FMath::Max(Diagnostics.ErrorCount, 1);
	}

	Diagnostics.bCompileSucceeded = Blueprint->Status != BS_Error && Diagnostics.ErrorCount == 0;
	return Diagnostics;
}

TSharedRef<FJsonObject> BAT::BlueprintCompileDiagnostics::MakeDiagnosticsObject(const FDiagnostics& Diagnostics)
{
	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("compileStatus"), Diagnostics.CompileStatus);
	Data->SetBoolField(TEXT("compiled"), Diagnostics.bCompileSucceeded);
	Data->SetNumberField(TEXT("errorCount"), Diagnostics.ErrorCount);
	Data->SetNumberField(TEXT("warningCount"), Diagnostics.WarningCount);
	Data->SetArrayField(TEXT("errors"), Diagnostics.Errors);
	Data->SetArrayField(TEXT("warnings"), Diagnostics.Warnings);
	return Data;
}
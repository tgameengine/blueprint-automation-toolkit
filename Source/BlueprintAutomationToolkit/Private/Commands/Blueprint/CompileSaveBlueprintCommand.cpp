// Copyright 2026 AkaSoft. All Rights Reserved.

#include "Commands/Blueprint/CompileSaveBlueprintCommand.h"

#include "Core/EditorExecution.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "FileHelpers.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Services/BlueprintCompileDiagnosticsService.h"
#include "Services/Reflection/ReflectionTypes.h"

namespace
{
	static FString NormalizeBlueprintObjectPath(const FString& InPath)
	{
		FString Path = InPath;
		Path.TrimStartAndEndInline();
		if (Path.IsEmpty())
		{
			return Path;
		}

		if (!Path.Contains(TEXT(".")))
		{
			const FString AssetName = FPackageName::GetLongPackageAssetName(Path);
			if (!AssetName.IsEmpty())
			{
				Path = FString::Printf(TEXT("%s.%s"), *Path, *AssetName);
			}
		}

		return Path;
	}

}

FAutomationResult FCompileSaveBlueprintCommand::Execute(FAutomationContext& Context)
{
	if (!Context.Body.IsValid())
	{
		return BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("bad_json"), TEXT("Invalid JSON body."), 400);
	}

	FString BlueprintPath;
	if (!Context.Body->TryGetStringField(TEXT("blueprint"), BlueprintPath) || BlueprintPath.TrimStartAndEnd().IsEmpty())
	{
		return BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("missing_blueprint"), TEXT("Body must include non-empty 'blueprint'."), 400);
	}

	bool bCompile = true;
	bool bSave = true;
	Context.Body->TryGetBoolField(TEXT("compile"), bCompile);
	Context.Body->TryGetBoolField(TEXT("save"), bSave);

	TOptional<FAutomationResult> Result;
	const bool bCompleted = BAT::EditorExecution::RunOnGameThreadAndWaitVoid([&Context, BlueprintPath, bCompile, bSave, &Result]()
	{
		const FString ObjectPath = NormalizeBlueprintObjectPath(BlueprintPath);
		UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *ObjectPath);
		if (!Blueprint)
		{
			Result = BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("blueprint_not_found"), TEXT("Blueprint could not be loaded."), 404);
			return;
		}

		BAT::BlueprintCompileDiagnostics::FDiagnostics Diagnostics;
		if (bCompile)
		{
			Diagnostics = BAT::BlueprintCompileDiagnostics::Compile(Blueprint);
		}
		else
		{
			Diagnostics.CompileStatus = TEXT("not_requested");
			Diagnostics.bCompileSucceeded = true;
		}

		const bool bCompileSucceeded = !bCompile || Diagnostics.bCompileSucceeded;

		bool bSaved = false;
		FString SaveStatus = TEXT("not_requested");
		if (bSave && bCompileSucceeded)
		{
			TArray<UPackage*> Packages;
			Packages.Add(Blueprint->GetOutermost());
			bSaved = UEditorLoadingAndSavingUtils::SavePackages(Packages, false);
			SaveStatus = bSaved ? TEXT("saved") : TEXT("save_failed");
		}
		else if (bSave)
		{
			SaveStatus = TEXT("skipped_compile_failed");
		}

		TArray<TSharedPtr<FJsonValue>> Errors = Diagnostics.Errors;
		if (bSave && bCompileSucceeded && !bSaved)
		{
			TSharedRef<FJsonObject> Issue = MakeShared<FJsonObject>();
			Issue->SetStringField(TEXT("code"), TEXT("save_failed"));
			Issue->SetStringField(TEXT("severity"), TEXT("error"));
			Issue->SetStringField(TEXT("message"), TEXT("Blueprint save failed."));
			Errors.Add(MakeShared<FJsonValueObject>(Issue));
		}

		TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("blueprint"), ObjectPath);
		Data->SetStringField(TEXT("target"), ObjectPath);
		Data->SetBoolField(TEXT("compiled"), bCompile && bCompileSucceeded);
		Data->SetBoolField(TEXT("saved"), bSave && bSaved);
		Data->SetStringField(TEXT("compileStatus"), Diagnostics.CompileStatus);
		Data->SetStringField(TEXT("saveStatus"), SaveStatus);
		Data->SetObjectField(TEXT("compileDiagnostics"), BAT::BlueprintCompileDiagnostics::MakeDiagnosticsObject(Diagnostics));
		Data->SetArrayField(TEXT("errors"), Errors);
		Data->SetArrayField(TEXT("warnings"), Diagnostics.Warnings);

		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetBoolField(TEXT("success"), bCompileSucceeded && (!bSave || bSaved));
		Root->SetStringField(TEXT("requestId"), Context.RequestId);
		Root->SetObjectField(TEXT("data"), Data);
		Root->SetArrayField(TEXT("warnings"), Diagnostics.Warnings);
		Root->SetArrayField(TEXT("errors"), Errors);

		if (!bCompileSucceeded)
		{
			Result = FAutomationResult::ErrorWithData(TEXT("compile_failed"), TEXT("Blueprint compile failed."), 409, MakeShared<FJsonValueObject>(Root));
			return;
		}

		if (bSave && !bSaved)
		{
			Result = FAutomationResult::ErrorWithData(TEXT("save_failed"), TEXT("Blueprint save failed."), 500, MakeShared<FJsonValueObject>(Root));
			return;
		}

		Result = FAutomationResult::Ok(MakeShared<FJsonValueObject>(Root));
	}, 10.0f);

	return (bCompleted && Result.IsSet()) ? Result.GetValue() : BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("game_thread_timeout"), TEXT("Timed out waiting for game thread execution."), 504);
}
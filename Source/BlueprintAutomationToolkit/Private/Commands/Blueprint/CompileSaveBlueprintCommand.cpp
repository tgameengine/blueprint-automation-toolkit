#include "Commands/Blueprint/CompileSaveBlueprintCommand.h"

#include "Core/EditorExecution.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "FileHelpers.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
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

		if (bCompile)
		{
			FKismetEditorUtilities::CompileBlueprint(Blueprint);
		}

		const FString CompileStatus = BlueprintStatusToString(Blueprint->Status);
		const bool bCompileSucceeded = !bCompile || Blueprint->Status != BS_Error;

		bool bSaved = false;
		if (bSave && bCompileSucceeded)
		{
			TArray<UPackage*> Packages;
			Packages.Add(Blueprint->GetOutermost());
			bSaved = UEditorLoadingAndSavingUtils::SavePackages(Packages, false);
		}

		TArray<TSharedPtr<FJsonValue>> Errors;
		if (!bCompileSucceeded)
		{
			TSharedRef<FJsonObject> Issue = MakeShared<FJsonObject>();
			Issue->SetStringField(TEXT("code"), TEXT("compile_failed"));
			Issue->SetStringField(TEXT("message"), TEXT("Blueprint compile completed with an error status."));
			Errors.Add(MakeShared<FJsonValueObject>(Issue));
		}
		if (bSave && bCompileSucceeded && !bSaved)
		{
			TSharedRef<FJsonObject> Issue = MakeShared<FJsonObject>();
			Issue->SetStringField(TEXT("code"), TEXT("save_failed"));
			Issue->SetStringField(TEXT("message"), TEXT("Blueprint save failed."));
			Errors.Add(MakeShared<FJsonValueObject>(Issue));
		}

		TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("blueprint"), ObjectPath);
		Data->SetStringField(TEXT("target"), ObjectPath);
		Data->SetBoolField(TEXT("compiled"), bCompile && bCompileSucceeded);
		Data->SetBoolField(TEXT("saved"), bSave && bSaved);
		Data->SetStringField(TEXT("compileStatus"), bCompile ? CompileStatus : TEXT("not_requested"));
		Data->SetStringField(TEXT("saveStatus"), bSave ? (bSaved ? TEXT("saved") : (bCompileSucceeded ? TEXT("save_failed") : TEXT("skipped_compile_failed"))) : TEXT("not_requested"));
		Data->SetArrayField(TEXT("errors"), Errors);

		if (!bCompileSucceeded)
		{
			TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
			Details->SetStringField(TEXT("blueprint"), ObjectPath);
			Details->SetStringField(TEXT("compileStatus"), CompileStatus);
			Details->SetArrayField(TEXT("errors"), Errors);
			Result = BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("compile_failed"), TEXT("Blueprint compile failed."), 409, Details);
			return;
		}

		if (bSave && !bSaved)
		{
			TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
			Details->SetStringField(TEXT("blueprint"), ObjectPath);
			Details->SetArrayField(TEXT("errors"), Errors);
			Result = BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("save_failed"), TEXT("Blueprint save failed."), 500, Details);
			return;
		}

		Result = BAT::Reflection::MakeStructuredSuccess(Context.RequestId, Data);
	}, 10.0f);

	return (bCompleted && Result.IsSet()) ? Result.GetValue() : BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("game_thread_timeout"), TEXT("Timed out waiting for game thread execution."), 504);
}
// Copyright 2026 AkaSoft. All Rights Reserved.

#include "Commands/Material/SetMaterialTextureSamplesCommand.h"

#include "Core/EditorExecution.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "FileHelpers.h"
#include "MaterialEditingLibrary.h"
#include "Engine/Texture.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionTextureBase.h"
#include "Misc/PackageName.h"
#include "Services/Reflection/ReflectionTypes.h"
#include "UObject/UObjectIterator.h"

namespace
{
	struct FTextureExpressionAssignment
	{
		FString ExpressionName;
		FString TexturePath;
	};

	static FString NormalizeAssetObjectPath(const FString& InPath)
	{
		FString Path = InPath;
		Path.TrimStartAndEndInline();
		if (Path.IsEmpty() || Path.Contains(TEXT(".")))
		{
			return Path;
		}

		const FString AssetName = FPackageName::GetLongPackageAssetName(Path);
		return AssetName.IsEmpty() ? Path : FString::Printf(TEXT("%s.%s"), *Path, *AssetName);
	}

	static bool TryParseAssignments(const TSharedPtr<FJsonObject>& Body, TArray<FTextureExpressionAssignment>& OutAssignments, FString& OutError)
	{
		OutAssignments.Reset();
		OutError.Reset();

		const TSharedPtr<FJsonObject>* TexturesObject = nullptr;
		if (Body->TryGetObjectField(TEXT("textures"), TexturesObject) && TexturesObject && TexturesObject->IsValid())
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*TexturesObject)->Values)
			{
				if (!Pair.Value.IsValid() || Pair.Value->Type != EJson::String)
				{
					OutError = TEXT("'textures' values must be texture asset path strings.");
					return false;
				}

				FTextureExpressionAssignment& Assignment = OutAssignments.AddDefaulted_GetRef();
				Assignment.ExpressionName = Pair.Key.TrimStartAndEnd();
				Assignment.TexturePath = Pair.Value->AsString().TrimStartAndEnd();
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* AssignmentValues = nullptr;
		if (Body->TryGetArrayField(TEXT("assignments"), AssignmentValues) && AssignmentValues)
		{
			for (const TSharedPtr<FJsonValue>& Value : *AssignmentValues)
			{
				if (!Value.IsValid() || Value->Type != EJson::Object)
				{
					OutError = TEXT("'assignments' entries must be objects.");
					return false;
				}

				const TSharedPtr<FJsonObject> AssignmentObject = Value->AsObject();
				FTextureExpressionAssignment& Assignment = OutAssignments.AddDefaulted_GetRef();
				AssignmentObject->TryGetStringField(TEXT("expression"), Assignment.ExpressionName);
				AssignmentObject->TryGetStringField(TEXT("texture"), Assignment.TexturePath);
				Assignment.ExpressionName.TrimStartAndEndInline();
				Assignment.TexturePath.TrimStartAndEndInline();
			}
		}

		if (OutAssignments.Num() == 0)
		{
			OutError = TEXT("Body must include a non-empty 'textures' object or 'assignments' array.");
			return false;
		}

		TSet<FString> ExpressionNames;
		for (const FTextureExpressionAssignment& Assignment : OutAssignments)
		{
			if (Assignment.ExpressionName.IsEmpty() || Assignment.TexturePath.IsEmpty())
			{
				OutError = TEXT("Every texture assignment requires non-empty expression and texture values.");
				return false;
			}
			if (ExpressionNames.Contains(Assignment.ExpressionName))
			{
				OutError = FString::Printf(TEXT("Duplicate texture expression assignment: %s"), *Assignment.ExpressionName);
				return false;
			}
			ExpressionNames.Add(Assignment.ExpressionName);
		}

		return true;
	}
}

FAutomationResult FSetMaterialTextureSamplesCommand::Execute(FAutomationContext& Context)
{
	if (!Context.Body.IsValid())
	{
		return BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("bad_json"), TEXT("Invalid JSON body."), 400);
	}

	FString MaterialPath;
	if (!Context.Body->TryGetStringField(TEXT("material"), MaterialPath) || MaterialPath.TrimStartAndEnd().IsEmpty())
	{
		return BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("missing_material"), TEXT("Body must include non-empty 'material'."), 400);
	}

	TArray<FTextureExpressionAssignment> Assignments;
	FString AssignmentError;
	if (!TryParseAssignments(Context.Body, Assignments, AssignmentError))
	{
		return BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("bad_args"), AssignmentError, 400);
	}

	bool bRecompile = true;
	bool bSave = true;
	Context.Body->TryGetBoolField(TEXT("recompile"), bRecompile);
	Context.Body->TryGetBoolField(TEXT("save"), bSave);

	TOptional<FAutomationResult> Result;
	const bool bCompleted = BAT::EditorExecution::RunOnGameThreadAndWaitVoid([&Context, MaterialPath, Assignments, bRecompile, bSave, &Result]()
	{
		const FString MaterialObjectPath = NormalizeAssetObjectPath(MaterialPath);
		UMaterial* Material = LoadObject<UMaterial>(nullptr, *MaterialObjectPath);
		if (!Material)
		{
			Result = BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("material_not_found"), FString::Printf(TEXT("Material could not be loaded: %s"), *MaterialObjectPath), 404);
			return;
		}

		TMap<FString, UMaterialExpressionTextureBase*> ExpressionsByName;
		for (TObjectIterator<UMaterialExpressionTextureBase> It; It; ++It)
		{
			UMaterialExpressionTextureBase* Expression = *It;
			if (Expression && Expression->GetOutermost() == Material->GetOutermost())
			{
				ExpressionsByName.Add(Expression->GetName(), Expression);
			}
		}

		struct FResolvedAssignment
		{
			UMaterialExpressionTextureBase* Expression = nullptr;
			UTexture* Texture = nullptr;
		};
		TArray<FResolvedAssignment> ResolvedAssignments;
		ResolvedAssignments.Reserve(Assignments.Num());

		for (const FTextureExpressionAssignment& Assignment : Assignments)
		{
			UMaterialExpressionTextureBase* const* ExpressionPtr = ExpressionsByName.Find(Assignment.ExpressionName);
			if (!ExpressionPtr || !*ExpressionPtr)
			{
				Result = BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("texture_expression_not_found"), FString::Printf(TEXT("Texture expression '%s' was not found in %s."), *Assignment.ExpressionName, *MaterialObjectPath), 404);
				return;
			}

			const FString TextureObjectPath = NormalizeAssetObjectPath(Assignment.TexturePath);
			UTexture* Texture = LoadObject<UTexture>(nullptr, *TextureObjectPath);
			if (!Texture)
			{
				Result = BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("texture_not_found"), FString::Printf(TEXT("Texture could not be loaded: %s"), *TextureObjectPath), 404);
				return;
			}

			FResolvedAssignment& Resolved = ResolvedAssignments.AddDefaulted_GetRef();
			Resolved.Expression = *ExpressionPtr;
			Resolved.Texture = Texture;
		}

		Material->Modify();
		TArray<TSharedPtr<FJsonValue>> UpdatedSamples;
		for (const FResolvedAssignment& Resolved : ResolvedAssignments)
		{
			Resolved.Expression->Modify();
			Resolved.Expression->Texture = Resolved.Texture;
			Resolved.Expression->AutoSetSampleType();
			Resolved.Expression->PostEditChange();

			TSharedRef<FJsonObject> Updated = MakeShared<FJsonObject>();
			Updated->SetStringField(TEXT("expression"), Resolved.Expression->GetName());
			Updated->SetStringField(TEXT("expressionClass"), Resolved.Expression->GetClass()->GetName());
			Updated->SetStringField(TEXT("texture"), Resolved.Texture->GetPathName());
			UpdatedSamples.Add(MakeShared<FJsonValueObject>(Updated));
		}

		Material->MarkPackageDirty();
		if (bRecompile)
		{
			UMaterialEditingLibrary::RecompileMaterial(Material);
		}

		bool bSaved = false;
		if (bSave)
		{
			TArray<UPackage*> Packages;
			Packages.Add(Material->GetOutermost());
			bSaved = UEditorLoadingAndSavingUtils::SavePackages(Packages, false);
			if (!bSaved)
			{
				Result = BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("save_failed"), FString::Printf(TEXT("Material was updated but could not be saved: %s"), *MaterialObjectPath), 500);
				return;
			}
		}

		TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("material"), MaterialObjectPath);
		Data->SetNumberField(TEXT("updatedCount"), UpdatedSamples.Num());
		Data->SetArrayField(TEXT("updatedSamples"), UpdatedSamples);
		Data->SetBoolField(TEXT("recompiled"), bRecompile);
		Data->SetBoolField(TEXT("saved"), bSave && bSaved);
		Data->SetArrayField(TEXT("warnings"), TArray<TSharedPtr<FJsonValue>>());
		Data->SetArrayField(TEXT("errors"), TArray<TSharedPtr<FJsonValue>>());

		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetBoolField(TEXT("success"), true);
		Root->SetStringField(TEXT("requestId"), Context.RequestId);
		Root->SetObjectField(TEXT("data"), Data);
		Root->SetArrayField(TEXT("warnings"), TArray<TSharedPtr<FJsonValue>>());
		Root->SetArrayField(TEXT("errors"), TArray<TSharedPtr<FJsonValue>>());
		Result = FAutomationResult::Ok(MakeShared<FJsonValueObject>(Root));
	}, 30.0f);

	return (bCompleted && Result.IsSet()) ? Result.GetValue() : BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("game_thread_timeout"), TEXT("Timed out while updating material texture expressions."), 504);
}

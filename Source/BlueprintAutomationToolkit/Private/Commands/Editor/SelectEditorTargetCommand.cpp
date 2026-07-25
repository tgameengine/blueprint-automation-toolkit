// Copyright 2026 AkaSoft. All Rights Reserved.

#include "Commands/Editor/SelectEditorTargetCommand.h"

#include "BlueprintAutomationToolkitModule.h"
#include "Core/EditorExecution.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "GameFramework/Actor.h"
#include "Services/Reflection/ReflectionObjectResolver.h"
#include "Services/Reflection/ReflectionSerializationService.h"
#include "Services/Reflection/ReflectionTypes.h"

namespace
{
	static void BuildSelectionRequests(const TSharedPtr<FJsonObject>& BodyObj, TArray<TSharedPtr<FJsonObject>>& OutRequests)
	{
		OutRequests.Reset();
		if (!BodyObj.IsValid())
		{
			return;
		}

		const TArray<TSharedPtr<FJsonValue>>* TargetsArray = nullptr;
		if (BodyObj->TryGetArrayField(TEXT("targets"), TargetsArray) && TargetsArray)
		{
			for (const TSharedPtr<FJsonValue>& TargetValue : *TargetsArray)
			{
				if (!TargetValue.IsValid())
				{
					continue;
				}

				if (TargetValue->Type == EJson::Object)
				{
					OutRequests.Add(MakeShared<FJsonObject>(*TargetValue->AsObject()));
				}
				else if (TargetValue->Type == EJson::String)
				{
					TSharedRef<FJsonObject> TargetObj = MakeShared<FJsonObject>();
					TargetObj->SetStringField(TEXT("target"), TargetValue->AsString());
					OutRequests.Add(TargetObj);
				}
			}
		}

		if (OutRequests.Num() == 0)
		{
			OutRequests.Add(MakeShared<FJsonObject>(*BodyObj));
		}
	}
}

FAutomationResult FSelectEditorTargetCommand::Execute(FAutomationContext& Context)
{
	if (!Context.Module || !Context.Body.IsValid())
	{
		return BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("bad_args"), TEXT("Missing request context."), 400);
	}

	TOptional<FAutomationResult> Result;
	const bool bCompleted = BAT::EditorExecution::RunOnGameThreadAndWaitVoid([&Context, &Result]()
	{
		if (!GEditor)
		{
			Result = BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("editor_unavailable"), TEXT("Unreal Editor is not available."), 500);
			return;
		}

		bool bReplaceSelection = true;
		Context.Body->TryGetBoolField(TEXT("replace"), bReplaceSelection);

		TArray<TSharedPtr<FJsonObject>> SelectionRequests;
		BuildSelectionRequests(Context.Body, SelectionRequests);

		FReflectionObjectResolver Resolver;
		FReflectionSerializationService Serialization;
		if (bReplaceSelection)
		{
			GEditor->SelectNone(false, true, false);
		}

		TArray<TSharedPtr<FJsonValue>> SelectedActors;
		for (const TSharedPtr<FJsonObject>& SelectionRequest : SelectionRequests)
		{
			BAT::Reflection::FResolvedObject ResolvedObject;
			FAutomationResult Failure;
			if (!Resolver.Resolve(*Context.Module, SelectionRequest, Context.RequestId, ResolvedObject, Failure))
			{
				Result = Failure;
				return;
			}

			AActor* Actor = Cast<AActor>(ResolvedObject.Object);
			if (!Actor)
			{
				Result = BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("unsupported_target_type"), TEXT("Editor selection currently supports actors only."), 400);
				return;
			}

			GEditor->SelectActor(Actor, true, false, true, true);
			SelectedActors.Add(MakeShared<FJsonValueObject>(Serialization.SerializeObjectReference(Actor)));
		}

		GEditor->NoteSelectionChange();

		TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("selectionReplaced"), bReplaceSelection);
		Data->SetArrayField(TEXT("selectedActors"), SelectedActors);
		Data->SetNumberField(TEXT("selectedCount"), SelectedActors.Num());
		Result = BAT::Reflection::MakeStructuredSuccess(Context.RequestId, Data);
	}, 10.0f);

	return (bCompleted && Result.IsSet()) ? Result.GetValue() : BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("game_thread_timeout"), TEXT("Timed out waiting for game thread execution."), 504);
}
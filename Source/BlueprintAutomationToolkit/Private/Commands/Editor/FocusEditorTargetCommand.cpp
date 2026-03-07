#include "Commands/Editor/FocusEditorTargetCommand.h"

#include "BlueprintAutomationToolkitModule.h"
#include "Core/EditorExecution.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "GameFramework/Actor.h"
#include "Services/Reflection/ReflectionObjectResolver.h"
#include "Services/Reflection/ReflectionSerializationService.h"
#include "Services/Reflection/ReflectionTypes.h"
#include "Subsystems/AssetEditorSubsystem.h"

FAutomationResult FFocusEditorTargetCommand::Execute(FAutomationContext& Context)
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

		FReflectionObjectResolver Resolver;
		FReflectionSerializationService Serialization;

		BAT::Reflection::FResolvedObject ResolvedObject;
		FAutomationResult Failure;
		if (!Resolver.Resolve(*Context.Module, Context.Body, Context.RequestId, ResolvedObject, Failure))
		{
			Result = Failure;
			return;
		}

		bool bSelect = true;
		Context.Body->TryGetBoolField(TEXT("select"), bSelect);

		TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetObjectField(TEXT("target"), Serialization.SerializeObjectReference(ResolvedObject.Object));

		if (AActor* Actor = Cast<AActor>(ResolvedObject.Object))
		{
			if (bSelect)
			{
				GEditor->SelectNone(false, true, false);
				GEditor->SelectActor(Actor, true, false, true, true);
				GEditor->NoteSelectionChange();
			}

			GEditor->MoveViewportCamerasToActor(*Actor, false);
			Data->SetStringField(TEXT("focusMode"), TEXT("actor"));
			Result = BAT::Reflection::MakeStructuredSuccess(Context.RequestId, Data);
			return;
		}

		if (UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
		{
			AssetEditorSubsystem->OpenEditorForAsset(ResolvedObject.Object);
			Data->SetStringField(TEXT("focusMode"), TEXT("asset"));
			Result = BAT::Reflection::MakeStructuredSuccess(Context.RequestId, Data);
			return;
		}

		Result = BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("focus_failed"), TEXT("Unable to focus the resolved target."), 500);
	}, 10.0f);

	return (bCompleted && Result.IsSet()) ? Result.GetValue() : BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("game_thread_timeout"), TEXT("Timed out waiting for game thread execution."), 504);
}
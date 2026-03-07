#include "Commands/Actor/DestroyActorCommand.h"

#include "BlueprintAutomationToolkitModule.h"
#include "Core/EditorExecution.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "GameFramework/Actor.h"
#include "ScopedTransaction.h"
#include "Services/Reflection/ReflectionObjectResolver.h"
#include "Services/Reflection/ReflectionSerializationService.h"
#include "Services/Reflection/ReflectionTypes.h"

FAutomationResult FDestroyActorCommand::Execute(FAutomationContext& Context)
{
	if (!Context.Module || !Context.Body.IsValid())
	{
		return BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("bad_args"), TEXT("Missing request context."), 400);
	}

	TOptional<FAutomationResult> Result;
	const bool bCompleted = BAT::EditorExecution::RunOnGameThreadAndWaitVoid([&Context, &Result]()
	{
		FReflectionObjectResolver Resolver;

		BAT::Reflection::FResolvedObject ResolvedObject;
		FAutomationResult Failure;
		if (!Resolver.Resolve(*Context.Module, Context.Body, Context.RequestId, ResolvedObject, Failure))
		{
			Result = Failure;
			return;
		}

		AActor* Actor = Cast<AActor>(ResolvedObject.Object);
		if (!Actor)
		{
			Result = BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("unsupported_target_type"), TEXT("Resolved target is not an actor."), 400);
			return;
		}

		UWorld* World = Actor->GetWorld();
		if (!World)
		{
			Result = BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("world_not_found"), TEXT("Actor world is not available."), 404);
			return;
		}

		const FString ActorPath = Actor->GetPathName();
		const FString ActorClassPath = Actor->GetClass() ? Actor->GetClass()->GetPathName() : FString();

		const FScopedTransaction Transaction(NSLOCTEXT("BlueprintAutomationToolkit", "DestroyActor", "BAT Destroy Actor"));
		Actor->Modify();
		const bool bDestroyed = World->EditorDestroyActor(Actor, true);
		if (!bDestroyed)
		{
			Result = BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("destroy_failed"), TEXT("Unreal Editor refused to destroy the actor."), 500);
			return;
		}

		TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("actorPath"), ActorPath);
		Data->SetStringField(TEXT("classPath"), ActorClassPath);
		Data->SetBoolField(TEXT("destroyed"), true);
		Result = BAT::Reflection::MakeStructuredSuccess(Context.RequestId, Data);
	}, 10.0f);

	return (bCompleted && Result.IsSet()) ? Result.GetValue() : BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("game_thread_timeout"), TEXT("Timed out waiting for game thread execution."), 504);
}
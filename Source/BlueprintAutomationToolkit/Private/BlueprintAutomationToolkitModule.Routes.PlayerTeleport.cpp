#include "BlueprintAutomationToolkitModule.h"

#include "Commands/CommandDispatcher.h"
#include "Commands/TeleportActorCommand.h"
#include "Services/ActorService.h"
#include "Async/Async.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HttpPath.h"
#include "HttpResultCallback.h"
#include "Http/HttpRequestUtils.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "Misc/Guid.h"
#include "IHttpRouter.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	static bool TryParseJsonBody(const TArray<uint8>& Body, TSharedPtr<FJsonObject>& OutObj)
	{
		FString BodyString = FString(Body.Num(), reinterpret_cast<const ANSICHAR*>(Body.GetData()));
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(BodyString);
		return FJsonSerializer::Deserialize(Reader, OutObj) && OutObj.IsValid();
	}

	static bool TryParseVector3(const TSharedPtr<FJsonValue>& Value, FVector& OutVec)
	{
		if (!Value.IsValid())
		{
			return false;
		}

		if (Value->Type == EJson::Array)
		{
			const TArray<TSharedPtr<FJsonValue>>& Arr = Value->AsArray();
			if (Arr.Num() == 3)
			{
				OutVec.X = (float)Arr[0]->AsNumber();
				OutVec.Y = (float)Arr[1]->AsNumber();
				OutVec.Z = (float)Arr[2]->AsNumber();
				return true;
			}
			return false;
		}

		if (Value->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject> Obj = Value->AsObject();
			if (!Obj.IsValid())
			{
				return false;
			}
			double X = 0.0, Y = 0.0, Z = 0.0;
			if (!(Obj->TryGetNumberField(TEXT("x"), X) || Obj->TryGetNumberField(TEXT("X"), X))) return false;
			if (!(Obj->TryGetNumberField(TEXT("y"), Y) || Obj->TryGetNumberField(TEXT("Y"), Y))) return false;
			if (!(Obj->TryGetNumberField(TEXT("z"), Z) || Obj->TryGetNumberField(TEXT("Z"), Z))) return false;
			OutVec = FVector((float)X, (float)Y, (float)Z);
			return true;
		}

		return false;
	}

	enum class EBATExecWorldMode : uint8
	{
		Auto,
		Editor,
		Pie,
	};

	struct FBATExecWorldRequest
	{
		EBATExecWorldMode Mode = EBATExecWorldMode::Auto;
		bool bHasWorldField = false;
		bool bRequireExplicitWorld = false;
		int32 PieIndex = 0;
	};

	static FBATExecWorldRequest ParseExecWorldRequest(const TSharedPtr<FJsonObject>& BodyObj)
	{
		FBATExecWorldRequest Req;
		if (!BodyObj.IsValid())
		{
			return Req;
		}

		BodyObj->TryGetBoolField(TEXT("require_world"), Req.bRequireExplicitWorld);

		double PieIndexD = 0.0;
		if (BodyObj->TryGetNumberField(TEXT("pie_index"), PieIndexD))
		{
			Req.PieIndex = FMath::Max(0, (int32)PieIndexD);
		}

		FString Mode;
		Req.bHasWorldField = BodyObj->TryGetStringField(TEXT("world"), Mode);
		Mode.TrimStartAndEndInline();
		if (Mode.Equals(TEXT("editor"), ESearchCase::IgnoreCase))
		{
			Req.Mode = EBATExecWorldMode::Editor;
		}
		else if (Mode.Equals(TEXT("pie"), ESearchCase::IgnoreCase))
		{
			Req.Mode = EBATExecWorldMode::Pie;
		}
		else
		{
			Req.Mode = EBATExecWorldMode::Auto;
		}

		return Req;
	}

	static UWorld* GetWorldForExecMode(EBATExecWorldMode Mode, int32 PieIndex, bool& bOutPie, int32& OutResolvedPieIndex)
	{
		bOutPie = false;
		OutResolvedPieIndex = -1;
		if (!GEditor)
		{
			return nullptr;
		}

		auto GetPieWorldByIndex = [&](int32 Index) -> UWorld*
		{
			TArray<UWorld*> PieWorlds;
			for (const FWorldContext& Ctx : GEditor->GetWorldContexts())
			{
				if (Ctx.WorldType == EWorldType::PIE && Ctx.World())
				{
					PieWorlds.Add(Ctx.World());
				}
			}

			if (!PieWorlds.IsValidIndex(Index))
			{
				return nullptr;
			}

			OutResolvedPieIndex = Index;
			return PieWorlds[Index];
		};

		switch (Mode)
		{
		case EBATExecWorldMode::Pie:
			bOutPie = true;
			{
				UWorld* Pie = GetPieWorldByIndex(PieIndex);
				if (Pie)
				{
					return Pie;
				}
				if (GEditor->PlayWorld != nullptr)
				{
					OutResolvedPieIndex = 0;
					return GEditor->PlayWorld;
				}
				return nullptr;
			}
		case EBATExecWorldMode::Editor:
			bOutPie = false;
			return GEditor->GetEditorWorldContext().World();
		case EBATExecWorldMode::Auto:
		default:
			if (GEditor->PlayWorld != nullptr)
			{
				bOutPie = true;
				UWorld* Pie = GetPieWorldByIndex(PieIndex);
				if (Pie)
				{
					return Pie;
				}
				OutResolvedPieIndex = 0;
				return GEditor->PlayWorld;
			}
			bOutPie = false;
			return GEditor->GetEditorWorldContext().World();
		}
	}

	static bool ActorMatchesName(const AActor* Actor, const FString& Target)
	{
		if (!Actor)
		{
			return false;
		}

		if (Actor->GetName().Equals(Target, ESearchCase::IgnoreCase))
		{
			return true;
		}

#if WITH_EDITOR
		if (Actor->GetActorLabel().Equals(Target, ESearchCase::IgnoreCase))
		{
			return true;
		}
#endif

		return false;
	}

	static AActor* FindActorByNameOrLabel(UWorld* World, const FString& Target)
	{
		if (!World)
		{
			return nullptr;
		}

		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (ActorMatchesName(Actor, Target))
			{
				return Actor;
			}
		}

		return nullptr;
	}

	static bool ComponentMatchesName(const UActorComponent* Comp, const FString& Target)
	{
		if (!Comp)
		{
			return false;
		}

		return Comp->GetName().Equals(Target, ESearchCase::IgnoreCase);
	}

	static UInstancedStaticMeshComponent* FindInstancedStaticMeshComponentByName(UWorld* World, const FString& Target)
	{
		if (!World)
		{
			return nullptr;
		}

		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!Actor)
			{
				continue;
			}

			TArray<UInstancedStaticMeshComponent*> ISMComps;
			Actor->GetComponents<UInstancedStaticMeshComponent>(ISMComps);
			for (UInstancedStaticMeshComponent* Comp : ISMComps)
			{
				if (ComponentMatchesName(Comp, Target))
				{
					return Comp;
				}
			}
		}

		return nullptr;
	}
}

void FBlueprintAutomationToolkitModule::BindPlayerTeleportRoute()
{
	PlayerTeleportToActorRoute = Router->BindRoute(
		FHttpPath(TEXT("/ai/player/teleport_to_actor")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, TEXT("/ai/player/teleport_to_actor")))
			{
				return true;
			}

			TSharedPtr<FJsonObject> BodyObj;
			if (!TryParseJsonBody(Request.Body, BodyObj))
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("bad_json"), TEXT("Invalid JSON body")));
				return true;
			}

			FString Target;
			if (!BodyObj->TryGetStringField(TEXT("target"), Target) || Target.IsEmpty())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("missing_target"), TEXT("Body must include 'target'")));
				return true;
			}

			bool bFaceTarget = true;
			BodyObj->TryGetBoolField(TEXT("face_target"), bFaceTarget);

			FVector Offset = FVector(-600.0, 0.0, 100.0);
			{
				FVector Parsed = Offset;
				bool bParsedOffset = false;

				const TArray<TSharedPtr<FJsonValue>>* OffsetArrPtr = nullptr;
				if (BodyObj->TryGetArrayField(TEXT("offset"), OffsetArrPtr) && OffsetArrPtr)
				{
					TSharedPtr<FJsonValue> JsonValue = MakeShared<FJsonValueArray>(*OffsetArrPtr);
					bParsedOffset = TryParseVector3(JsonValue, Parsed);
				}
				else
				{
					const TSharedPtr<FJsonObject>* OffsetObjPtr = nullptr;
					if (BodyObj->TryGetObjectField(TEXT("offset"), OffsetObjPtr) && OffsetObjPtr && OffsetObjPtr->IsValid())
					{
						TSharedPtr<FJsonValue> JsonValue = MakeShared<FJsonValueObject>(*OffsetObjPtr);
						bParsedOffset = TryParseVector3(JsonValue, Parsed);
					}
				}

				if (bParsedOffset)
				{
					Offset = Parsed;
				}
			}

			const FString RequestId = ResolveOrCreateRequestId(Request);
			const FBATExecWorldRequest WorldReq = ParseExecWorldRequest(BodyObj);
			if (WorldReq.bRequireExplicitWorld && (!WorldReq.bHasWorldField || WorldReq.Mode == EBATExecWorldMode::Auto))
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("world_required"), TEXT("Body must include 'world' when require_world=true (editor|pie)")));
				return true;
			}

			FActorService Service([this, Target, Offset, bFaceTarget, WorldReq](FAutomationContext& Context) -> FAutomationResult
			{
				TOptional<FAutomationResult> ThreadResult;
				const bool bCompleted = RunOnGameThreadWait([this, Target, Offset, bFaceTarget, WorldReq, &ThreadResult]()
				{
					bool bPie = false;
					int32 ResolvedPieIndex = -1;
					FString WorldMode = TEXT("auto");
					if (WorldReq.Mode == EBATExecWorldMode::Editor)
					{
						WorldMode = TEXT("editor");
					}
					else if (WorldReq.Mode == EBATExecWorldMode::Pie)
					{
						WorldMode = TEXT("pie");
					}
					FString ResolveError;
					UWorld* World = ResolveWorld(WorldMode, WorldReq.PieIndex, bPie, ResolvedPieIndex, ResolveError);
					if (!bPie || !World)
					{
						ThreadResult = FAutomationResult::Error(TEXT("not_in_pie"), ResolveError.IsEmpty() ? TEXT("PIE must be running (and world=pie)") : ResolveError, (int32)EHttpServerResponseCodes::BadRequest);
						return;
					}

					APlayerController* PC = World->GetFirstPlayerController();
					APawn* Pawn = PC ? PC->GetPawn() : nullptr;
					if (!PC || !Pawn)
					{
						ThreadResult = FAutomationResult::Error(TEXT("no_player"), TEXT("No player controller/pawn"), (int32)EHttpServerResponseCodes::BadRequest);
						return;
					}

					UInstancedStaticMeshComponent* TargetISMCompByName = FindInstancedStaticMeshComponentByName(World, Target);
					AActor* TargetActor = ResolveActor(World, Target);
					if (TargetISMCompByName)
					{
						TargetActor = TargetISMCompByName->GetOwner();
					}
					if (!TargetActor)
					{
						TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
						Obj->SetBoolField(TEXT("ok"), false);
						Obj->SetBoolField(TEXT("found"), false);
						Obj->SetStringField(TEXT("target"), Target);
						ThreadResult = FAutomationResult::Ok(MakeShared<FJsonValueObject>(Obj));
						return;
					}

					const FVector TargetLoc = TargetActor->GetActorLocation();
					const FVector Dest = TargetLoc + Offset;
					const FRotator KeepRot = Pawn->GetActorRotation();
					const bool bTeleported = Pawn->TeleportTo(Dest, KeepRot);

					if (bFaceTarget)
					{
						const FRotator AimRot = (TargetLoc - Dest).Rotation();
						PC->SetControlRotation(AimRot);
						Pawn->FaceRotation(AimRot, 0.0f);
					}

					TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
					Obj->SetBoolField(TEXT("ok"), bTeleported);
					Obj->SetBoolField(TEXT("found"), true);
					Obj->SetStringField(TEXT("target"), Target);
					Obj->SetStringField(TEXT("player_actor"), Pawn->GetName());
					Obj->SetBoolField(TEXT("pie"), true);
					Obj->SetNumberField(TEXT("pie_index"), ResolvedPieIndex);

					TSharedRef<FJsonObject> DestObj = MakeShared<FJsonObject>();
					DestObj->SetNumberField(TEXT("x"), Dest.X);
					DestObj->SetNumberField(TEXT("y"), Dest.Y);
					DestObj->SetNumberField(TEXT("z"), Dest.Z);
					Obj->SetObjectField(TEXT("dest"), DestObj);

					ThreadResult = FAutomationResult::Ok(MakeShared<FJsonValueObject>(Obj));
				}, 10.0);

				if (!bCompleted)
				{
					return FAutomationResult::Error(TEXT("game_thread_timeout"), TEXT("Timed out waiting for GameThread execution"), 504);
				}

				if (!ThreadResult.IsSet())
				{
					return FAutomationResult::Error(TEXT("internal_error"), TEXT("No result produced by teleport operation"), 500);
				}

				return ThreadResult.GetValue();
			});

			FTeleportActorCommand Command(Service);
			FCommandDispatcher Dispatcher;
			FAutomationContext Context;
			Context.RequestId = RequestId;
			Context.Body = BodyObj;

			const FAutomationResult Result = Dispatcher.Dispatch(Command, Context);
			if (!Result.bSuccess)
			{
				OnComplete(MakeErrorResponse(Result.StatusCode, RequestId, Result.ErrorCode, Result.ErrorMessage));
				return true;
			}

			BAT::Http::JsonOk(OnComplete, Result.Data, Result.StatusCode, RequestId);

			return true;
		}));
}

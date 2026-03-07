#include "BlueprintAutomationToolkitModule.h"

#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Http/HttpRequestUtils.h"
#include "HttpPath.h"
#include "HttpResultCallback.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"
#include "Math/RotationMatrix.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	static TUniquePtr<FHttpServerResponse> MakeJsonResponse(EHttpServerResponseCodes Code, const FString& JsonString, const FString& RequestId = FString())
	{
		return BAT::Http::MakeJsonResponseFromString(static_cast<int32>(Code), JsonString, RequestId);
	}

	static FString ToJsonString(const TSharedRef<FJsonObject>& Object)
	{
		FString Out;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Object, Writer);
		return Out;
	}

	static bool TryParseJsonBody(const TArray<uint8>& Body, TSharedPtr<FJsonObject>& OutObj)
	{
		FString BodyString = FString(Body.Num(), reinterpret_cast<const ANSICHAR*>(Body.GetData()));
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(BodyString);
		return FJsonSerializer::Deserialize(Reader, OutObj) && OutObj.IsValid();
	}
}

void FBlueprintAutomationToolkitModule::BindPlayerWanderRoute()
{
	PlayerWanderRoute = Router->BindRoute(
		FHttpPath(TEXT("/ai/player/wander")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, TEXT("/ai/player/wander")))
			{
				return true;
			}

			TSharedPtr<FJsonObject> BodyObj;
			float Seconds = 10.0f;
			float Strength = 1.0f;
			if (TryParseJsonBody(Request.Body, BodyObj))
			{
				double SecondsD = 0.0;
				double StrengthD = 0.0;
				if (BodyObj->TryGetNumberField(TEXT("seconds"), SecondsD))
				{
					Seconds = (float)SecondsD;
				}
				if (BodyObj->TryGetNumberField(TEXT("strength"), StrengthD))
				{
					Strength = (float)StrengthD;
				}
			}

			Seconds = FMath::Clamp(Seconds, 0.1f, 300.0f);
			Strength = FMath::Clamp(Strength, 0.0f, 1.0f);

			const FString RequestId = ResolveOrCreateRequestId(Request);
			TSharedRef<TAtomic<bool>> bResponded = MakeShared<TAtomic<bool>>(false);
			FHttpResultCallback Complete = [OnComplete, bResponded](TUniquePtr<FHttpServerResponse> Response) mutable
			{
				if (!bResponded->Exchange(true))
				{
					OnComplete(MoveTemp(Response));
				}
			};

			const bool bCompleted = RunOnGameThreadWait([this, Seconds, Strength, Complete, RequestId]()
			{
				if (!GEditor || GEditor->PlayWorld == nullptr)
				{
					Complete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("not_in_pie"), TEXT("PIE must be running")));
					return;
				}

				if (!Wander)
				{
					Wander = MakeUnique<FWanderState>();
				}
				Wander->RemainingSeconds = Seconds;
				Wander->Strength = Strength;
				Wander->NextChangeSeconds = 0.0f;

				if (!Wander->Handle.IsValid())
				{
					Wander->Handle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this](float DeltaTime)
					{
						if (!Wander)
						{
							return false;
						}

						if (!GEditor || GEditor->PlayWorld == nullptr)
						{
							Wander.Reset();
							return false;
						}

						Wander->RemainingSeconds -= DeltaTime;
						if (Wander->RemainingSeconds <= 0.0f)
						{
							FTSTicker::GetCoreTicker().RemoveTicker(Wander->Handle);
							Wander.Reset();
							return false;
						}

						APlayerController* PC = nullptr;
						if (GEditor->PlayWorld)
						{
							PC = GEditor->PlayWorld->GetFirstPlayerController();
						}
						APawn* Pawn = PC ? PC->GetPawn() : nullptr;
						if (!PC || !Pawn)
						{
							return true;
						}

						Wander->NextChangeSeconds -= DeltaTime;
						if (Wander->NextChangeSeconds <= 0.0f)
						{
							const float Angle = FMath::FRandRange(-PI, PI);
							Wander->Dir = FVector2D(FMath::Cos(Angle), FMath::Sin(Angle));
							Wander->YawRate = FMath::FRandRange(-180.0f, 180.0f);
							Wander->NextChangeSeconds = FMath::FRandRange(0.25f, 1.0f);
						}

						const FRotator ControlRot = PC->GetControlRotation();
						const FVector Forward = FRotationMatrix(ControlRot).GetScaledAxis(EAxis::X);
						const FVector Right = FRotationMatrix(ControlRot).GetScaledAxis(EAxis::Y);

						Pawn->AddMovementInput(Forward, Wander->Dir.X * Wander->Strength);
						Pawn->AddMovementInput(Right, Wander->Dir.Y * Wander->Strength);
						PC->AddYawInput(Wander->YawRate * DeltaTime);

						return true;
					}));
				}

				TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
				Obj->SetBoolField(TEXT("ok"), true);
				Obj->SetNumberField(TEXT("seconds"), Seconds);
				Obj->SetNumberField(TEXT("strength"), Strength);
				Complete(MakeJsonResponse(EHttpServerResponseCodes::Ok, ToJsonString(Obj), RequestId));
			}, 10.0);

			if (!bCompleted && !bResponded->Exchange(true))
			{
				OnComplete(MakeErrorResponse(504, RequestId, TEXT("game_thread_timeout"), TEXT("Timed out waiting for GameThread execution")));
			}

			return true;
		}));
}

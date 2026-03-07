#include "BlueprintAutomationToolkitModule.h"

#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "HttpPath.h"
#include "HttpResultCallback.h"
#include "Http/HttpRequestUtils.h"
#include "HttpServerRequest.h"
#include "IHttpRouter.h"

namespace
{
	static TSharedPtr<FJsonValue> MakeActorSummary(AActor* Actor)
	{
		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("path"), Actor ? Actor->GetPathName() : TEXT(""));
		Obj->SetStringField(TEXT("class"), (Actor && Actor->GetClass()) ? Actor->GetClass()->GetPathName() : TEXT(""));
#if WITH_EDITOR
		Obj->SetStringField(TEXT("label"), Actor ? Actor->GetActorLabel() : TEXT(""));
#else
		Obj->SetStringField(TEXT("label"), Actor ? Actor->GetName() : TEXT(""));
#endif
		return MakeShared<FJsonValueObject>(Obj);
	}
}

void FBlueprintAutomationToolkitModule::BindActorRoutes()
{
	BindActorSpawnRoute();
	BindActorFindRoute();
}

void FBlueprintAutomationToolkitModule::BindActorSpawnRoute()
{
	ActorSpawnRoute = Router->BindRoute(
		FHttpPath(TEXT("/actor/spawn")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, TEXT("/actor/spawn")))
			{
				return true;
			}

			TSharedPtr<FJsonObject> BodyObj;
			if (!BAT::Http::TryParseJsonBody(Request.Body, BodyObj) || !BodyObj.IsValid())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("bad_args"), TEXT("Invalid JSON body")));
				return true;
			}

			return DispatchAutomationCommandRoute(TEXT("/actor/spawn"), Request, OnComplete, BodyObj);
		}));
}

void FBlueprintAutomationToolkitModule::BindActorFindRoute()
{
	ActorFindRoute = Router->BindRoute(
		FHttpPath(TEXT("/actor/find")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, TEXT("/actor/find")))
			{
				return true;
			}

			TSharedPtr<FJsonObject> BodyObj;
			if (!BAT::Http::TryParseJsonBody(Request.Body, BodyObj) || !BodyObj.IsValid())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("bad_args"), TEXT("Invalid JSON body")));
				return true;
			}

			FString By;
			FString Value;
			double LimitRaw = 50.0;
			BodyObj->TryGetStringField(TEXT("by"), By);
			BodyObj->TryGetStringField(TEXT("value"), Value);
			BodyObj->TryGetNumberField(TEXT("limit"), LimitRaw);
			const int32 Limit = FMath::Clamp((int32)LimitRaw, 1, 500);
			const FString RequestId = ResolveOrCreateRequestId(Request);

			AsyncTask(ENamedThreads::GameThread, [this, By, Value, Limit, RequestId, OnComplete]()
			{
				UWorld* EditorWorld = GetEditorWorld();
				if (!EditorWorld)
				{
					OnComplete(MakeErrorResponse(EHttpServerResponseCodes::NotFound, RequestId, TEXT("not_found"), TEXT("Editor world not available")));
					return;
				}

				const FString Mode = By.TrimStartAndEnd().ToLower();
				if (!(Mode == TEXT("tag") || Mode == TEXT("name") || Mode == TEXT("class")))
				{
					OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, RequestId, TEXT("bad_args"), TEXT("'by' must be one of: tag, name, class")));
					return;
				}

				TArray<TSharedPtr<FJsonValue>> Actors;
				for (TActorIterator<AActor> It(EditorWorld); It && Actors.Num() < Limit; ++It)
				{
					AActor* Actor = *It;
					if (!Actor)
					{
						continue;
					}

					bool bMatches = false;
					if (Mode == TEXT("tag"))
					{
						bMatches = Actor->ActorHasTag(FName(*Value));
					}
					else if (Mode == TEXT("name"))
					{
						bMatches = Actor->GetName().Equals(Value, ESearchCase::IgnoreCase);
#if WITH_EDITOR
						bMatches = bMatches || Actor->GetActorLabel().Equals(Value, ESearchCase::IgnoreCase);
#endif
					}
					else if (Mode == TEXT("class"))
					{
						const UClass* Class = Actor->GetClass();
						const FString ClassName = Class ? Class->GetName() : FString();
						const FString ClassPath = Class ? Class->GetPathName() : FString();
						const FString ClassScriptPath = Class ? Class->GetClassPathName().ToString() : FString();
						bMatches = ClassName.Equals(Value, ESearchCase::IgnoreCase)
							|| ClassPath.Equals(Value, ESearchCase::IgnoreCase)
							|| ClassScriptPath.Equals(Value, ESearchCase::IgnoreCase);
					}

					if (bMatches)
					{
						Actors.Add(MakeActorSummary(Actor));
					}
				}

				TSharedRef<FJsonObject> ResponseObj = MakeShared<FJsonObject>();
				ResponseObj->SetBoolField(TEXT("ok"), true);
				ResponseObj->SetStringField(TEXT("request_id"), RequestId);
				ResponseObj->SetArrayField(TEXT("actors"), Actors);
				OnComplete(BAT::Http::MakeJsonResponse(200, ResponseObj, RequestId));
			});

			return true;
		}));
}
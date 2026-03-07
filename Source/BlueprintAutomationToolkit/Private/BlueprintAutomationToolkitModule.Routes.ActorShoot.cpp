#include "BlueprintAutomationToolkitModule.h"

#include "Dom/JsonObject.h"
#include "HttpPath.h"
#include "HttpResultCallback.h"
#include "HttpServerResponse.h"
#include "Misc/Guid.h"
#include "IHttpRouter.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	static TUniquePtr<FHttpServerResponse> MakeJsonResponse(EHttpServerResponseCodes Code, const FString& JsonString)
	{
		TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(JsonString, TEXT("application/json"));
		Response->Code = Code;
		Response->Headers.FindOrAdd(TEXT("Cache-Control")).Add(TEXT("no-store"));
		Response->Headers.FindOrAdd(TEXT("X-Request-Id")).Add(FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower));
		return Response;
	}

	static FString ToJsonString(const TSharedRef<FJsonObject>& Object)
	{
		FString Out;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Object, Writer);
		return Out;
	}
}

void FBlueprintAutomationToolkitModule::BindActorShootRoute()
{
	ActorShootRoute = Router->BindRoute(
		FHttpPath(TEXT("/ai/actor/shoot")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, TEXT("/ai/actor/shoot")))
			{
				return true;
			}

			TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetBoolField(TEXT("ok"), false);
			Obj->SetStringField(TEXT("error"), TEXT("not_supported"));
			Obj->SetStringField(TEXT("message"), TEXT("/ai/actor/shoot requires Geometry integration and is disabled in this build."));
			OnComplete(MakeJsonResponse(EHttpServerResponseCodes::Ok, ToJsonString(Obj)));
			return true;
		}));
}

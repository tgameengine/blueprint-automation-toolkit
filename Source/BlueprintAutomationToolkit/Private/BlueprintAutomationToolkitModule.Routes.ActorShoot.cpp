#include "BlueprintAutomationToolkitModule.h"

#include "Dom/JsonObject.h"
#include "Http/HttpRequestUtils.h"
#include "HttpPath.h"
#include "HttpResultCallback.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"
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

			const FString RequestId = ResolveOrCreateRequestId(Request);

			TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetBoolField(TEXT("ok"), false);
			Obj->SetStringField(TEXT("error"), TEXT("not_supported"));
			Obj->SetStringField(TEXT("message"), TEXT("/ai/actor/shoot requires Geometry integration and is disabled in this build."));
			OnComplete(MakeJsonResponse(EHttpServerResponseCodes::Ok, ToJsonString(Obj), RequestId));
			return true;
		}));
}

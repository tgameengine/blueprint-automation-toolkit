#include "BlueprintAutomationToolkitModule.h"

#include "Domain/Requests/AssetSaveRequest.h"
#include "Services/AssetService.h"
#include "Transport/PolicyMiddleware.h"
#include "Transport/RequestParsing.h"

#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HttpPath.h"
#include "HttpResultCallback.h"
#include "HttpServerRequest.h"
#include "IHttpRouter.h"

namespace
{
	static bool TryBuildAssetSaveRequest(const TSharedPtr<FJsonObject>& BodyObj, FBATAssetSaveRequest& OutRequest)
	{
		OutRequest.Paths.Reset();
		if (!BodyObj.IsValid())
		{
			return false;
		}

		FString SinglePath;
		if (BodyObj->TryGetStringField(TEXT("path"), SinglePath) && !SinglePath.TrimStartAndEnd().IsEmpty())
		{
			OutRequest.Paths.Add(SinglePath.TrimStartAndEnd());
			return true;
		}

		if (BodyObj->TryGetStringField(TEXT("target"), SinglePath) && !SinglePath.TrimStartAndEnd().IsEmpty())
		{
			OutRequest.Paths.Add(SinglePath.TrimStartAndEnd());
			return true;
		}

		const TArray<TSharedPtr<FJsonValue>>* PathsField = nullptr;
		if (!BodyObj->TryGetArrayField(TEXT("paths"), PathsField) || !PathsField)
		{
			return false;
		}

		for (const TSharedPtr<FJsonValue>& PathValue : *PathsField)
		{
			if (!PathValue.IsValid() || PathValue->Type != EJson::String)
			{
				return false;
			}

			const FString Path = PathValue->AsString().TrimStartAndEnd();
			if (Path.IsEmpty())
			{
				return false;
			}
			OutRequest.Paths.Add(Path);
		}

		return OutRequest.Paths.Num() > 0;
	}
}

void FBlueprintAutomationToolkitModule::BindAssetSaveRoute()
{
	AssetSaveRoute = Router->BindRoute(
		FHttpPath(TEXT("/asset/save")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!BAT::Transport::ValidateAndHandleRequest(*this, Request, OnComplete, TEXT("/asset/save")))
			{
				return true;
			}

			TSharedPtr<FJsonObject> BodyObj;
			if (!BAT::Transport::TryParseJsonObjectBody(Request, BodyObj))
			{
				OnComplete(MakeCanonicalErrorResponse(400, ResolveOrCreateRequestId(Request), TEXT("bad_json"), TEXT("Invalid JSON body.")));
				return true;
			}

			FBATAssetSaveRequest SaveRequest;
			if (!TryBuildAssetSaveRequest(BodyObj, SaveRequest))
			{
				OnComplete(MakeCanonicalErrorResponse(400, ResolveOrCreateRequestId(Request), TEXT("bad_args"), TEXT("Body must include 'path', 'target', or non-empty 'paths' array.")));
				return true;
			}

			const FString RequestId = ResolveOrCreateRequestId(Request);
			AsyncTask(ENamedThreads::GameThread, [this, SaveRequest, RequestId, OnComplete]()
			{
				const FAssetService Service;
				const FAutomationResult Result = Service.SaveAssets(*this, SaveRequest);
				OnComplete(MakeCanonicalResponseFromAutomationResult(Result, RequestId));
			});
			return true;
		}));
}
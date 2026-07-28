// Copyright 2026 AkaSoft. All Rights Reserved.

#include "BlueprintAutomationToolkitModule.h"

#include "Services/AssetPipelineService.h"
#include "Transport/PolicyMiddleware.h"
#include "Transport/RequestParsing.h"

#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "HttpPath.h"
#include "HttpResultCallback.h"
#include "HttpServerRequest.h"
#include "IHttpRouter.h"

void FBlueprintAutomationToolkitModule::BindAssetPipelineRoutes()
{
	AssetImportFormatsRoute = Router->BindRoute(
		FHttpPath(TEXT("/asset/import/formats")),
		EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!BAT::Transport::ValidateAndHandleRequest(*this, Request, OnComplete, TEXT("/asset/import/formats")))
			{
				return true;
			}
			const FString RequestId = ResolveOrCreateRequestId(Request);
			const FAssetPipelineService Service;
			OnComplete(MakeCanonicalResponseFromAutomationResult(Service.DescribeImportFormats(*this), RequestId));
			return true;
		}));

	auto BindPost = [this](
		FHttpRouteHandle& Handle,
		const TCHAR* Path,
		TFunction<FAutomationResult(const FAssetPipelineService&, const TSharedPtr<FJsonObject>&)> Operation)
	{
		Handle = Router->BindRoute(
			FHttpPath(Path),
			EHttpServerRequestVerbs::VERB_POST,
			FHttpRequestHandler::CreateLambda([this, PathString = FString(Path), Operation = MoveTemp(Operation)](
				const FHttpServerRequest& Request,
				const FHttpResultCallback& OnComplete)
			{
				if (!BAT::Transport::ValidateAndHandleRequest(*this, Request, OnComplete, *PathString))
				{
					return true;
				}

				TSharedPtr<FJsonObject> BodyObj;
				if (!BAT::Transport::TryParseJsonObjectBody(Request, BodyObj))
				{
					OnComplete(MakeCanonicalErrorResponse(
						400,
						ResolveOrCreateRequestId(Request),
						TEXT("bad_json"),
						TEXT("Invalid JSON body.")));
					return true;
				}

				const FString RequestId = ResolveOrCreateRequestId(Request);
				AsyncTask(ENamedThreads::GameThread, [this, BodyObj, RequestId, OnComplete, Operation]()
				{
					const FAssetPipelineService Service;
					OnComplete(MakeCanonicalResponseFromAutomationResult(Operation(Service, BodyObj), RequestId));
				});
				return true;
			}));
	};

	BindPost(
		AssetImportRoute,
		TEXT("/asset/import"),
		[this](const FAssetPipelineService& Service, const TSharedPtr<FJsonObject>& Body)
		{
			return Service.ImportAssets(*this, Body);
		});

	BindPost(
		AssetInspectRoute,
		TEXT("/asset/inspect"),
		[this](const FAssetPipelineService& Service, const TSharedPtr<FJsonObject>& Body)
		{
			return Service.InspectAssets(*this, Body);
		});

	BindPost(
		AssetConfigureRoute,
		TEXT("/asset/configure"),
		[this](const FAssetPipelineService& Service, const TSharedPtr<FJsonObject>& Body)
		{
			return Service.ConfigureAssets(*this, Body);
		});

	BindPost(
		AssetValidateRoute,
		TEXT("/asset/validate"),
		[this](const FAssetPipelineService& Service, const TSharedPtr<FJsonObject>& Body)
		{
			return Service.ValidateAssets(*this, Body);
		});

	BindPost(
		AssetShowcaseCaptureRoute,
		TEXT("/asset/showcase/capture"),
		[this](const FAssetPipelineService& Service, const TSharedPtr<FJsonObject>& Body)
		{
			return Service.CreateShowcaseAndCapture(*this, Body);
		});

	AssetPipelineExecuteRoute = Router->BindRoute(
		FHttpPath(TEXT("/asset/pipeline/execute")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!BAT::Transport::ValidateAndHandleRequest(*this, Request, OnComplete, TEXT("/asset/pipeline/execute")))
			{
				return true;
			}

			TSharedPtr<FJsonObject> BodyObj;
			if (!BAT::Transport::TryParseJsonObjectBody(Request, BodyObj))
			{
				OnComplete(MakeCanonicalErrorResponse(
					400,
					ResolveOrCreateRequestId(Request),
					TEXT("bad_json"),
					TEXT("Invalid JSON body.")));
				return true;
			}

			const FString RequestId = ResolveOrCreateRequestId(Request);
			bool bAsync = false;
			BodyObj->TryGetBoolField(TEXT("async"), bAsync);
			if (bAsync)
			{
				const FString JobId = SubmitJob(TEXT("asset.pipeline"), RequestId, BodyObj);
				TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
				Data->SetStringField(TEXT("jobId"), JobId);
				Data->SetStringField(TEXT("requestId"), RequestId);
				Data->SetStringField(TEXT("state"), TEXT("queued"));
				OnComplete(MakeCanonicalSuccessResponse(202, RequestId, Data));
				return true;
			}

			AsyncTask(ENamedThreads::GameThread, [this, BodyObj, RequestId, OnComplete]()
			{
				const FAssetPipelineService Service;
				OnComplete(MakeCanonicalResponseFromAutomationResult(Service.ExecutePipeline(*this, BodyObj), RequestId));
			});
			return true;
		}));
}

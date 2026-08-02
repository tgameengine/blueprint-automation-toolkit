// Copyright 2026 AkaSoft. All Rights Reserved.

#include "BlueprintAutomationToolkitModule.h"

#include "Dom/JsonObject.h"
#include "HttpPath.h"
#include "HttpServerRequest.h"
#include "IHttpRouter.h"
#include "Services/LiveCaptureService.h"
#include "Services/RuntimeAutomationService.h"
#include "Transport/RequestParsing.h"
#include "Transport/ResponseWriter.h"

namespace
{
	static TSharedPtr<FJsonObject> ParseBody(const FHttpServerRequest& Request)
	{
		TSharedPtr<FJsonObject> Body;
		return BAT::Transport::TryParseJsonObjectBody(Request, Body) ? Body : nullptr;
	}
}

void FBlueprintAutomationToolkitModule::BindLiveAutomationRoutes()
{
	if (!LiveCaptureService)
	{
		LiveCaptureService = MakeUnique<FLiveCaptureService>();
	}

	auto BindGet = [this](FHttpRouteHandle& Handle, const TCHAR* Path, TFunction<FAutomationResult()> Operation)
	{
		Handle = Router->BindRoute(FHttpPath(Path), EHttpServerRequestVerbs::VERB_GET,
			FHttpRequestHandler::CreateLambda([this, PathString = FString(Path), Operation = MoveTemp(Operation)](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
			{
				if (!ValidateAndHandleRequest(Request, OnComplete, *PathString)) return true;
				const FString RequestId = ResolveOrCreateRequestId(Request);
				TOptional<FAutomationResult> Result = RunOnGameThreadWait<FAutomationResult>([Operation]() { return Operation(); }, 10.0);
				if (!Result.IsSet())
				{
					OnComplete(BAT::Transport::MakeErrorResponse(504, RequestId, TEXT("game_thread_timeout"), TEXT("Timed out waiting for live automation state")));
					return true;
				}
				OnComplete(BAT::Transport::MakeResponseFromAutomationResult(Result.GetValue(), RequestId));
				return true;
			}));
	};

	auto BindPost = [this](FHttpRouteHandle& Handle, const TCHAR* Path, TFunction<FAutomationResult(const TSharedPtr<FJsonObject>&)> Operation, double TimeoutSeconds = 30.0)
	{
		Handle = Router->BindRoute(FHttpPath(Path), EHttpServerRequestVerbs::VERB_POST,
			FHttpRequestHandler::CreateLambda([this, PathString = FString(Path), Operation = MoveTemp(Operation), TimeoutSeconds](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
			{
				if (!ValidateAndHandleRequest(Request, OnComplete, *PathString)) return true;
				const FString RequestId = ResolveOrCreateRequestId(Request);
				const TSharedPtr<FJsonObject> Body = ParseBody(Request);
				if (!Body.IsValid())
				{
					OnComplete(BAT::Transport::MakeErrorResponse(400, RequestId, TEXT("bad_json"), TEXT("Invalid JSON body")));
					return true;
				}
				TOptional<FAutomationResult> Result = RunOnGameThreadWait<FAutomationResult>([Body, Operation]() { return Operation(Body); }, TimeoutSeconds);
				if (!Result.IsSet())
				{
					OnComplete(BAT::Transport::MakeErrorResponse(504, RequestId, TEXT("game_thread_timeout"), TEXT("Timed out waiting for live automation execution")));
					return true;
				}
				OnComplete(BAT::Transport::MakeResponseFromAutomationResult(Result.GetValue(), RequestId));
				return true;
			}));
	};

	BindGet(CaptureSchemaRoute, TEXT("/capture/schema"), [this]() { return LiveCaptureService->DescribeSchema(); });
	BindGet(CaptureSessionStatusRoute, TEXT("/capture/session/status"), [this]() { return LiveCaptureService->Status(); });
	BindPost(CaptureSessionStartRoute, TEXT("/capture/session/start"), [this](const TSharedPtr<FJsonObject>& Body)
	{
		return LiveCaptureService->Start(*this, Body);
	});
	BindPost(CaptureSessionStopRoute, TEXT("/capture/session/stop"), [this](const TSharedPtr<FJsonObject>& Body)
	{
		return LiveCaptureService->Stop(Body);
	});
	BindPost(PieInputRoute, TEXT("/pie/input"), [this](const TSharedPtr<FJsonObject>& Body)
	{
		return FRuntimeAutomationService().ApplyPieInput(*this, Body);
	});
	BindPost(RuntimeAssertRoute, TEXT("/runtime/assert"), [this](const TSharedPtr<FJsonObject>& Body)
	{
		return FRuntimeAutomationService().EvaluateAssertions(*this, Body);
	});
}

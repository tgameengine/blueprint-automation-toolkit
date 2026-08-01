// Copyright 2026 AkaSoft. All Rights Reserved.

#include "BlueprintAutomationToolkitModule.h"

#include "Dom/JsonObject.h"
#include "HttpPath.h"
#include "HttpResultCallback.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"
#include "Services/UMGDesignerService.h"
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

void FBlueprintAutomationToolkitModule::BindUMGDesignerRoutes()
{
	UMGSchemaRoute = Router->BindRoute(
		FHttpPath(TEXT("/umg/schema")),
		EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, TEXT("/umg/schema")))
			{
				return true;
			}
			const FString RequestId = ResolveOrCreateRequestId(Request);
			const FAutomationResult Result = FUMGDesignerService().DescribeSchema();
			OnComplete(BAT::Transport::MakeResponseFromAutomationResult(Result, RequestId));
			return true;
		}));

	auto BindPostRoute = [this](FHttpRouteHandle& Handle, const TCHAR* Path, TFunction<FAutomationResult(const FUMGDesignerService&, const TSharedPtr<FJsonObject>&)> Operation)
	{
		Handle = Router->BindRoute(
			FHttpPath(Path),
			EHttpServerRequestVerbs::VERB_POST,
			FHttpRequestHandler::CreateLambda([this, PathString = FString(Path), Operation = MoveTemp(Operation)](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
			{
				if (!ValidateAndHandleRequest(Request, OnComplete, *PathString))
				{
					return true;
				}

				const TSharedPtr<FJsonObject> Body = ParseBody(Request);
				const FString RequestId = ResolveOrCreateRequestId(Request);
				if (!Body.IsValid())
				{
					OnComplete(BAT::Transport::MakeErrorResponse(400, RequestId, TEXT("bad_json"), TEXT("Invalid JSON body")));
					return true;
				}

				TOptional<FAutomationResult> ThreadResult;
				const bool bCompleted = RunOnGameThreadWait([Body, Operation, &ThreadResult]()
				{
					const FUMGDesignerService Service;
					ThreadResult = Operation(Service, Body);
				}, 30.0);

				if (!bCompleted || !ThreadResult.IsSet())
				{
					OnComplete(BAT::Transport::MakeErrorResponse(504, RequestId, TEXT("game_thread_timeout"), TEXT("Timed out waiting for native UMG Designer execution")));
					return true;
				}

				OnComplete(BAT::Transport::MakeResponseFromAutomationResult(ThreadResult.GetValue(), RequestId));
				return true;
			}));
	};

	BindPostRoute(UMGCreateRoute, TEXT("/umg/create"), [](const FUMGDesignerService& Service, const TSharedPtr<FJsonObject>& Body)
	{
		return Service.CreateWidgetBlueprint(Body);
	});
	BindPostRoute(UMGDesignerReadRoute, TEXT("/umg/designer/read"), [](const FUMGDesignerService& Service, const TSharedPtr<FJsonObject>& Body)
	{
		return Service.ReadDesigner(Body);
	});
	BindPostRoute(UMGDesignerApplyRoute, TEXT("/umg/designer/apply"), [](const FUMGDesignerService& Service, const TSharedPtr<FJsonObject>& Body)
	{
		return Service.ApplyDesigner(Body);
	});
}

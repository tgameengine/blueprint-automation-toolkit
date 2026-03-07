#include "BlueprintAutomationToolkitModule.h"

#include "Commands/CommandDispatcher.h"
#include "Commands/StartPIECommand.h"
#include "Commands/StopPIECommand.h"
#include "Services/PIEControlService.h"
#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "HttpPath.h"
#include "HttpResultCallback.h"
#include "Http/HttpRequestUtils.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"

void FBlueprintAutomationToolkitModule::BindPieControlRoutes()
{
	auto HandlePieStart = [this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete, const TCHAR* Endpoint)
	{
		if (!ValidateAndHandleRequest(Request, OnComplete, Endpoint))
		{
			return true;
		}
		const FString RequestId = ResolveOrCreateRequestId(Request);
		FPIEControlService Service(RequestId, [this, RequestId](const FString& Kind, const TSharedRef<FJsonObject>& Payload)
		{
			return SubmitJob(Kind, RequestId, Payload);
		});
		FStartPIECommand Command(Service);
		FCommandDispatcher Dispatcher;
		FAutomationContext Context;
		Context.RequestId = RequestId;

		const FAutomationResult Result = Dispatcher.Dispatch(Command, Context);
		if (!Result.bSuccess)
		{
			OnComplete(MakeErrorResponse(Result.StatusCode, RequestId, Result.ErrorCode, Result.ErrorMessage));
			return true;
		}

		BAT::Http::JsonOk(OnComplete, Result.Data, Result.StatusCode, RequestId);

		return true;
	};

	auto HandlePieStop = [this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete, const TCHAR* Endpoint)
	{
		if (!ValidateAndHandleRequest(Request, OnComplete, Endpoint))
		{
			return true;
		}
		const FString RequestId = ResolveOrCreateRequestId(Request);
		FPIEControlService Service(RequestId, [this, RequestId](const FString& Kind, const TSharedRef<FJsonObject>& Payload)
		{
			return SubmitJob(Kind, RequestId, Payload);
		});
		FStopPIECommand Command(Service);
		FCommandDispatcher Dispatcher;
		FAutomationContext Context;
		Context.RequestId = RequestId;

		const FAutomationResult Result = Dispatcher.Dispatch(Command, Context);
		if (!Result.bSuccess)
		{
			OnComplete(MakeErrorResponse(Result.StatusCode, RequestId, Result.ErrorCode, Result.ErrorMessage));
			return true;
		}

		BAT::Http::JsonOk(OnComplete, Result.Data, Result.StatusCode, RequestId);

		return true;
	};

	PieStartRoute = Router->BindRoute(
		FHttpPath(TEXT("/pie/start")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda([this, HandlePieStart](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			return HandlePieStart(Request, OnComplete, TEXT("/pie/start"));
		}));

	PieStopRoute = Router->BindRoute(
		FHttpPath(TEXT("/pie/stop")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda([this, HandlePieStop](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			return HandlePieStop(Request, OnComplete, TEXT("/pie/stop"));
		}));
}

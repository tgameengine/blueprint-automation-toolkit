#include "BlueprintAutomationToolkitModule.h"

#include "Commands/AutomationCommand.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Http/HttpRequestUtils.h"
#include "HttpPath.h"
#include "HttpResultCallback.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"
#include "Transport/PolicyMiddleware.h"
#include "Transport/RequestParsing.h"

void FBlueprintAutomationToolkitModule::BindAutomationCoreRoutes()
{
	auto BindPostCommandRoute = [this](FHttpRouteHandle& Handle, const TCHAR* Path, const TCHAR* Endpoint, bool bNormalizeObjectRequest)
	{
		Handle = Router->BindRoute(
			FHttpPath(Path),
			EHttpServerRequestVerbs::VERB_POST,
			FHttpRequestHandler::CreateLambda([this, Endpoint, bNormalizeObjectRequest](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
			{
				if (!BAT::Transport::ValidateAndHandleRequest(*this, Request, OnComplete, Endpoint))
				{
					return true;
				}

				TSharedPtr<FJsonObject> BodyObj;
				if (!BAT::Transport::TryParseJsonObjectBody(Request, BodyObj))
				{
					OnComplete(MakeCanonicalErrorResponse(400, ResolveOrCreateRequestId(Request), TEXT("bad_json"), TEXT("Invalid JSON body.")));
					return true;
				}

				const TSharedPtr<FJsonObject> EffectiveBody = bNormalizeObjectRequest ? NormalizeCanonicalObjectRequest(BodyObj) : BodyObj;
				return DispatchAutomationCommandRoute(Endpoint, Request, OnComplete, EffectiveBody, true);
			}));
	};

	auto BindGetCommandRoute = [this](FHttpRouteHandle& Handle, const TCHAR* Path, const TCHAR* Endpoint, TFunction<TSharedPtr<FJsonObject>(const FHttpServerRequest&)> BodyBuilder, bool bNormalizeObjectRequest)
	{
		Handle = Router->BindRoute(
			FHttpPath(Path),
			EHttpServerRequestVerbs::VERB_GET,
			FHttpRequestHandler::CreateLambda([this, Endpoint, BodyBuilder, bNormalizeObjectRequest](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
			{
				if (!BAT::Transport::ValidateAndHandleRequest(*this, Request, OnComplete, Endpoint))
				{
					return true;
				}

				const TSharedPtr<FJsonObject> BodyObj = BodyBuilder(Request);
				const TSharedPtr<FJsonObject> EffectiveBody = bNormalizeObjectRequest ? NormalizeCanonicalObjectRequest(BodyObj) : BodyObj;
				const FString RequestId = ResolveOrCreateRequestId(Request);
				const FAutomationResult Result = ExecuteAutomationCommand(Endpoint, RequestId, EffectiveBody, true);
				OnComplete(MakeCanonicalResponseFromAutomationResult(Result, RequestId));
				return true;
			}));
	};

	BindPostCommandRoute(ObjectResolveRoute, TEXT("/object/resolve"), TEXT("/object/resolve"), true);
	BindGetCommandRoute(ObjectDescribeGetRoute, TEXT("/object/describe"), TEXT("/object/describe"), BAT::Transport::BuildObjectQueryBody, true);
	BindGetCommandRoute(ObjectGetPropertyRoute, TEXT("/object/get_property"), TEXT("/object/get_property"), BAT::Transport::BuildObjectQueryBody, true);
	BindPostCommandRoute(ObjectSetPropertyAliasRoute, TEXT("/object/set_property"), TEXT("/object/set_property"), true);
	BindPostCommandRoute(ObjectCallFunctionAliasRoute, TEXT("/object/call_function"), TEXT("/object/call_function"), true);
	BindGetCommandRoute(BlueprintGraphReadRoute, TEXT("/blueprint/graph/read"), TEXT("/blueprint/graph/read"), BAT::Transport::BuildBlueprintGraphReadQueryBody, false);
	BindPostCommandRoute(BlueprintCompileSaveRoute, TEXT("/blueprint/compile_save"), TEXT("/blueprint/compile_save"), false);
	BindPostCommandRoute(ActorDestroyRoute, TEXT("/actor/destroy"), TEXT("/actor/destroy"), true);
	BindPostCommandRoute(EditorSelectRoute, TEXT("/editor/select"), TEXT("/editor/select"), true);
	BindPostCommandRoute(EditorFocusRoute, TEXT("/editor/focus"), TEXT("/editor/focus"), true);
}
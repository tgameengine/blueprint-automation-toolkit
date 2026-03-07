#include "BlueprintAutomationToolkitModule.h"

#include "Http/HttpRequestUtils.h"

#include "HttpPath.h"
#include "HttpResultCallback.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"

void FBlueprintAutomationToolkitModule::BindReflectionRoutes()
{
	ObjectResolveRoute = Router->BindRoute(
		FHttpPath(TEXT("/object/resolve")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, TEXT("/object/resolve")))
			{
				return true;
			}

			TSharedPtr<FJsonObject> BodyObj;
			if (!BAT::Http::TryParseJsonBody(Request.Body, BodyObj) || !BodyObj.IsValid())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("bad_json"), TEXT("Invalid JSON body")));
				return true;
			}

			return DispatchAutomationCommandRoute(TEXT("/object/resolve"), Request, OnComplete, BodyObj, true);
		}));

	ObjectGetRoute = Router->BindRoute(
		FHttpPath(TEXT("/object/get")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, TEXT("/object/get")))
			{
				return true;
			}

			TSharedPtr<FJsonObject> BodyObj;
			if (!BAT::Http::TryParseJsonBody(Request.Body, BodyObj) || !BodyObj.IsValid())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("bad_json"), TEXT("Invalid JSON body")));
				return true;
			}

			return DispatchAutomationCommandRoute(TEXT("/object/get"), Request, OnComplete, BodyObj, true);
		}));

	ObjectSetPropertyRoute = Router->BindRoute(
		FHttpPath(TEXT("/object/set-property")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, TEXT("/object/set-property")))
			{
				return true;
			}

			TSharedPtr<FJsonObject> BodyObj;
			if (!BAT::Http::TryParseJsonBody(Request.Body, BodyObj) || !BodyObj.IsValid())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("bad_json"), TEXT("Invalid JSON body")));
				return true;
			}

			return DispatchAutomationCommandRoute(TEXT("/object/set-property"), Request, OnComplete, BodyObj, true);
		}));

	ObjectCallFunctionRoute = Router->BindRoute(
		FHttpPath(TEXT("/object/call-function")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, TEXT("/object/call-function")))
			{
				return true;
			}

			TSharedPtr<FJsonObject> BodyObj;
			if (!BAT::Http::TryParseJsonBody(Request.Body, BodyObj) || !BodyObj.IsValid())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("bad_json"), TEXT("Invalid JSON body")));
				return true;
			}

			return DispatchAutomationCommandRoute(TEXT("/object/call-function"), Request, OnComplete, BodyObj, true);
		}));

	ObjectListPropertiesRoute = Router->BindRoute(
		FHttpPath(TEXT("/object/list-properties")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, TEXT("/object/list-properties")))
			{
				return true;
			}

			TSharedPtr<FJsonObject> BodyObj;
			if (!BAT::Http::TryParseJsonBody(Request.Body, BodyObj) || !BodyObj.IsValid())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("bad_json"), TEXT("Invalid JSON body")));
				return true;
			}

			return DispatchAutomationCommandRoute(TEXT("/object/list-properties"), Request, OnComplete, BodyObj, true);
		}));

	ObjectListFunctionsRoute = Router->BindRoute(
		FHttpPath(TEXT("/object/list-functions")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, TEXT("/object/list-functions")))
			{
				return true;
			}

			TSharedPtr<FJsonObject> BodyObj;
			if (!BAT::Http::TryParseJsonBody(Request.Body, BodyObj) || !BodyObj.IsValid())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("bad_json"), TEXT("Invalid JSON body")));
				return true;
			}

			return DispatchAutomationCommandRoute(TEXT("/object/list-functions"), Request, OnComplete, BodyObj, true);
		}));
}
// Copyright 2026 AkaSoft. All Rights Reserved.

#include "BlueprintAutomationToolkitModule.h"

#include "HttpPath.h"
#include "HttpResultCallback.h"
#include "Http/HttpRequestUtils.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

void FBlueprintAutomationToolkitModule::BindDiscoverRoutes()
{
	HealthRoute = Router->BindRoute(
		FHttpPath(TEXT("/health")),
		EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
			Data->SetNumberField(TEXT("port"), Port);

			const FString RequestId = ResolveOrCreateRequestId(Request);
			OnComplete(MakeCanonicalSuccessResponse(200, RequestId, Data));
			return true;
		}));

	CapabilitiesRoute = Router->BindRoute(
		FHttpPath(TEXT("/ai/capabilities")),
		EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, TEXT("/ai/capabilities")))
			{
				return true;
			}

			const FString RequestId = ResolveOrCreateRequestId(Request);
			OnComplete(MakeCanonicalSuccessResponse(200, RequestId, BuildCapabilitiesSummary()));
			return true;
		}));

	EngineDiscoverRoute = Router->BindRoute(
		FHttpPath(TEXT("/engine/discover")),
		EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, TEXT("/engine/discover")))
			{
				return true;
			}

			const FString RequestId = ResolveOrCreateRequestId(Request);
			OnComplete(MakeCanonicalSuccessResponse(200, RequestId, BuildEngineDiscoverPayload()));
			return true;
		}));

	OpenApiRoute = Router->BindRoute(
		FHttpPath(TEXT("/openapi")),
		EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, TEXT("/openapi")))
			{
				return true;
			}

			TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("BlueprintAutomationToolkit"));
			if (!Plugin.IsValid())
			{
				Plugin = IPluginManager::Get().FindPlugin(TEXT("BlueprintAutomationToolkit"));
			}
			if (!Plugin.IsValid())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::ServerError, TEXT("plugin_not_found"), TEXT("Plugin not found")));
				return true;
			}

			const FString OpenApiPath = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Docs"), TEXT("openapi.yaml"));
			FString Spec;
			if (!FFileHelper::LoadFileToString(Spec, *OpenApiPath))
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::ServerError, TEXT("openapi_missing"), TEXT("OpenAPI spec not found")));
				return true;
			}

			const FString RequestId = ResolveOrCreateRequestId(Request);
			TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(Spec, TEXT("application/yaml"));
			Response->Code = EHttpServerResponseCodes::Ok;
			Response->Headers.FindOrAdd(TEXT("X-Request-Id")).Add(RequestId);
			OnComplete(MoveTemp(Response));
			return true;
		}));
}
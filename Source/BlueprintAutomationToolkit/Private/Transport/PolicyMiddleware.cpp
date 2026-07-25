// Copyright 2026 AkaSoft. All Rights Reserved.

#include "Transport/PolicyMiddleware.h"

#include "BlueprintAutomationToolkitModule.h"

#include "Dom/JsonObject.h"
#include "Http/HttpRequestUtils.h"
#include "HttpResultCallback.h"
#include "HttpServerRequest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace BAT::Transport
{
	bool ValidateAndHandleRequest(FBlueprintAutomationToolkitModule& Module, const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete, const TCHAR* Endpoint)
	{
		const FString EndpointString = Endpoint ? FString(Endpoint) : TEXT("/ai/unknown");
		const FString RequestId = Module.ResolveOrCreateRequestId(Request);
		const FDateTime StartUtc = FDateTime::UtcNow();
		uint32 RequiredPermissions = Module.GetRouteRequiredPermissions(EndpointString);

		auto Fail = [&](int32 StatusCode, const FString& Code, const FString& Message, const FString& StatReason, const TSharedPtr<FJsonObject>& Details = nullptr)
		{
			OnComplete(Module.MakeErrorResponse(StatusCode, RequestId, Code, Message, Details));
			Module.RecordRequestStat(EndpointString, false, StatReason);
			FBlueprintAutomationToolkitModule::FStructuredLogEntry Entry;
			Entry.TimestampUtc = StartUtc;
			Entry.RequestId = RequestId;
			Entry.Route = EndpointString;
			Entry.Subject = TEXT("unknown");
			Entry.Status = StatusCode;
			Entry.DurationMs = 0.0;
			Entry.ErrorCode = Code;
			Module.AppendStructuredLog(Entry);
		};

		if (Request.Body.Num() > Module.MaxRequestBodyBytes)
		{
			Fail(413, TEXT("request_too_large"), FString::Printf(TEXT("Body exceeds max bytes (%d)"), Module.MaxRequestBodyBytes), TEXT("request_too_large"));
			UE_LOG(LogBlueprintAutomationToolkit, Warning, TEXT("Denied request (%s): request_too_large (%d > %d)"), *EndpointString, Request.Body.Num(), Module.MaxRequestBodyBytes);
			return false;
		}

		FString DenyReason;
		FString ClientKey;
		if (!Module.IsRequestAllowed(Request, &DenyReason, &ClientKey))
		{
			const FString AuthCode = DenyReason.IsEmpty() ? TEXT("denied") : DenyReason;
			OnComplete(Module.MakeAuthFailureResponse(RequestId, AuthCode));
			Module.RecordRequestStat(EndpointString, false, AuthCode);
			FBlueprintAutomationToolkitModule::FStructuredLogEntry Entry;
			Entry.TimestampUtc = StartUtc;
			Entry.RequestId = RequestId;
			Entry.Route = EndpointString;
			Entry.Subject = TEXT("unknown");
			Entry.Status = 401;
			Entry.DurationMs = 0.0;
			Entry.ErrorCode = AuthCode;
			Module.AppendStructuredLog(Entry);
			UE_LOG(LogBlueprintAutomationToolkit, Warning, TEXT("Denied request (%s): %s"), *EndpointString, DenyReason.IsEmpty() ? TEXT("denied") : *DenyReason);
			return false;
		}

		if (!Module.ConsumeRateLimitToken(ClientKey))
		{
			Fail(429, TEXT("rate_limited"), TEXT("Rate limit exceeded"), TEXT("rate_limited"));
			UE_LOG(LogBlueprintAutomationToolkit, Warning, TEXT("Denied request (%s): rate_limited"), *EndpointString);
			return false;
		}

		if (Request.Verb == EHttpServerRequestVerbs::VERB_POST)
		{
			const FString ContentType = Module.ReadHeaderValueCaseInsensitive(Request.Headers, TEXT("content-type")).ToLower();
			if (!ContentType.IsEmpty() && !ContentType.StartsWith(TEXT("application/json")))
			{
				Fail(400, TEXT("invalid_content_type"), TEXT("Content-Type must be application/json"), TEXT("invalid_content_type"));
				return false;
			}

			TSharedPtr<FJsonObject> BodyObj;
			if (Request.Body.Num() > 0)
			{
				const FString BodyString(Request.Body.Num(), reinterpret_cast<const ANSICHAR*>(Request.Body.GetData()));
				TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(BodyString);
				if (!FJsonSerializer::Deserialize(Reader, BodyObj) || !BodyObj.IsValid())
				{
					Fail(400, TEXT("bad_json"), TEXT("Invalid JSON body"), TEXT("bad_json"));
					return false;
				}
			}

			RequiredPermissions = Module.GetRequestRequiredPermissions(EndpointString, BodyObj);

			FString SchemaError;
			if (!Module.ValidateRequestSchema(EndpointString, Request, BodyObj, SchemaError))
			{
				Fail(400, TEXT("schema_validation_failed"), SchemaError.IsEmpty() ? TEXT("Schema validation failed") : SchemaError, TEXT("schema_validation_failed"));
				return false;
			}

			FString ResponseExportError;
			if (!BAT::Http::RegisterPendingResponseExport(BodyObj, RequestId, ResponseExportError))
			{
				Fail(400, TEXT("bad_args"), ResponseExportError.IsEmpty() ? TEXT("Invalid responseOutputPath") : ResponseExportError, TEXT("bad_args"));
				return false;
			}
		}

		if (Module.IsPieSessionRunning() && Module.IsEditorAssetMutationBlockedDuringPie(EndpointString))
		{
			TSharedRef<FJsonObject> Details = MakeShared<FJsonObject>();
			Details->SetStringField(TEXT("reason"), TEXT("pie_active"));
			Details->SetBoolField(TEXT("pieActive"), true);
			Details->SetStringField(TEXT("endpoint"), EndpointString);
			Details->SetStringField(TEXT("action"), TEXT("stop_pie_and_retry"));
			Fail(409, TEXT("pie_edit_blocked"), TEXT("Editor asset edits are blocked while PIE is running. Stop PIE and retry."), TEXT("pie_edit_blocked"), Details);
			UE_LOG(LogBlueprintAutomationToolkit, Warning, TEXT("Denied request (%s): pie_edit_blocked while PIE is running"), *EndpointString);
			return false;
		}

		FBlueprintAutomationToolkitModule::FTokenRecord Token;
		if (Module.TryResolveToken(ClientKey, Token))
		{
			FString PermReason;
			if (!Module.IsPermissionAllowed(RequiredPermissions, Token.Token, PermReason))
			{
				Fail(403, TEXT("forbidden"), PermReason.IsEmpty() ? TEXT("Missing required permission") : PermReason, TEXT("forbidden"));
				return false;
			}
		}

		Module.RecordRequestStat(EndpointString, true, TEXT("ok"));
		return true;
	}
}
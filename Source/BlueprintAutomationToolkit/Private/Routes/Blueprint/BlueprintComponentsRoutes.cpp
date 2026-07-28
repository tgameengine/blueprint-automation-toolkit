// Copyright 2026 AkaSoft. All Rights Reserved.

#include "BlueprintAutomationToolkitModule.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HttpPath.h"
#include "HttpResultCallback.h"
#include "Http/HttpRequestUtils.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"

void FBlueprintAutomationToolkitModule::BindBlueprintComponentsRoutes()
{
	BlueprintComponentsRemoveRoute = Router->BindRoute(
		FHttpPath(TEXT("/blueprint/components/remove")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, TEXT("/blueprint/components/remove")))
			{
				return true;
			}

			TSharedPtr<FJsonObject> BodyObj;
			if (!BAT::Http::TryParseJsonBody(Request.Body, BodyObj) || !BodyObj.IsValid())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("bad_json"), TEXT("Invalid JSON body")));
				return true;
			}

			FString BlueprintPath;
			FString Name;
			if (!BodyObj->TryGetStringField(TEXT("blueprint"), BlueprintPath) || BlueprintPath.TrimStartAndEnd().IsEmpty())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("missing_blueprint"), TEXT("Body must include 'blueprint'")));
				return true;
			}
			if (!BodyObj->TryGetStringField(TEXT("name"), Name) || Name.TrimStartAndEnd().IsEmpty())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("missing_name"), TEXT("Body must include 'name'")));
				return true;
			}

			bool bCompile = false;
			BodyObj->TryGetBoolField(TEXT("compile"), bCompile);
			bool bMissingOk = false;
			BodyObj->TryGetBoolField(TEXT("missing_ok"), bMissingOk);

			TSharedRef<FJsonObject> PatchBody = MakeShared<FJsonObject>();
			PatchBody->SetStringField(TEXT("blueprint"), BlueprintPath);

			TArray<TSharedPtr<FJsonValue>> Ops;
			{
				TSharedRef<FJsonObject> RemoveOp = MakeShared<FJsonObject>();
				RemoveOp->SetStringField(TEXT("op"), TEXT("components.remove"));
				RemoveOp->SetStringField(TEXT("name"), Name);
				RemoveOp->SetBoolField(TEXT("missing_ok"), bMissingOk);
				Ops.Add(MakeShared<FJsonValueObject>(RemoveOp));
			}
			if (bCompile)
			{
				TSharedRef<FJsonObject> CompileOp = MakeShared<FJsonObject>();
				CompileOp->SetStringField(TEXT("op"), TEXT("compile"));
				Ops.Add(MakeShared<FJsonValueObject>(CompileOp));
			}
			PatchBody->SetArrayField(TEXT("ops"), Ops);

			TSharedPtr<FJsonValue> ResponseData;
			const bool bCompleted = RunOnGameThreadWait([this, PatchBody, &ResponseData]()
			{
				int32 TotalInstances = 0;
				TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
				TArray<TSharedPtr<FJsonValue>> Errors;
				ExecuteBlueprintPatch(PatchBody, true, TotalInstances, Result, Errors);
				Result->SetArrayField(TEXT("errors"), Errors);
				ResponseData = MakeShared<FJsonValueObject>(Result);
			}, 10.0);

			if (!bCompleted || !ResponseData.IsValid())
			{
				OnComplete(MakeErrorResponse(504, TEXT("game_thread_timeout"), TEXT("Timed out waiting for GameThread execution")));
				return true;
			}

			const FString RequestId = ResolveOrCreateRequestId(Request);
			BAT::Http::JsonOk(OnComplete, ResponseData, 200, RequestId);
			return true;
		}));

	BlueprintComponentsReplaceRoute = Router->BindRoute(
		FHttpPath(TEXT("/blueprint/components/replace")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, TEXT("/blueprint/components/replace")))
			{
				return true;
			}

			TSharedPtr<FJsonObject> BodyObj;
			if (!BAT::Http::TryParseJsonBody(Request.Body, BodyObj) || !BodyObj.IsValid())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("bad_json"), TEXT("Invalid JSON body")));
				return true;
			}

			FString BlueprintPath;
			FString Name;
			FString ClassPath;
			if (!BodyObj->TryGetStringField(TEXT("blueprint"), BlueprintPath) || BlueprintPath.TrimStartAndEnd().IsEmpty())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("missing_blueprint"), TEXT("Body must include 'blueprint'")));
				return true;
			}
			if (!BodyObj->TryGetStringField(TEXT("name"), Name) || Name.TrimStartAndEnd().IsEmpty())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("missing_name"), TEXT("Body must include 'name'")));
				return true;
			}
			if (!BodyObj->TryGetStringField(TEXT("class"), ClassPath) || ClassPath.TrimStartAndEnd().IsEmpty())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("missing_class"), TEXT("Body must include 'class'")));
				return true;
			}

			bool bCompile = false;
			BodyObj->TryGetBoolField(TEXT("compile"), bCompile);

			TSharedRef<FJsonObject> PatchBody = MakeShared<FJsonObject>();
			PatchBody->SetStringField(TEXT("blueprint"), BlueprintPath);

			TArray<TSharedPtr<FJsonValue>> Ops;
			{
				TSharedRef<FJsonObject> RemoveOp = MakeShared<FJsonObject>();
				RemoveOp->SetStringField(TEXT("op"), TEXT("components.remove"));
				RemoveOp->SetStringField(TEXT("name"), Name);
				RemoveOp->SetBoolField(TEXT("missing_ok"), true);
				Ops.Add(MakeShared<FJsonValueObject>(RemoveOp));
			}
			{
				TSharedRef<FJsonObject> AddOp = MakeShared<FJsonObject>();
				AddOp->SetStringField(TEXT("op"), TEXT("components.add"));
				AddOp->SetStringField(TEXT("name"), Name);
				AddOp->SetStringField(TEXT("class"), ClassPath);
				Ops.Add(MakeShared<FJsonValueObject>(AddOp));
			}

			TSharedRef<FJsonObject> SetOp = MakeShared<FJsonObject>();
			SetOp->SetStringField(TEXT("op"), TEXT("components.set"));
			SetOp->SetStringField(TEXT("name"), Name);

			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : BodyObj->Values)
			{
				const FString& Key = Pair.Key;
				if (Key.Equals(TEXT("blueprint"), ESearchCase::CaseSensitive)
					|| Key.Equals(TEXT("name"), ESearchCase::CaseSensitive)
					|| Key.Equals(TEXT("class"), ESearchCase::CaseSensitive)
					|| Key.Equals(TEXT("compile"), ESearchCase::CaseSensitive))
				{
					continue;
				}

				const bool bAllowed =
					Key.Equals(TEXT("static_mesh"), ESearchCase::IgnoreCase)
					|| Key.Equals(TEXT("material0"), ESearchCase::IgnoreCase)
					|| Key.Equals(TEXT("mobility"), ESearchCase::IgnoreCase)
					|| Key.Equals(TEXT("relative_location"), ESearchCase::IgnoreCase)
					|| Key.Equals(TEXT("relative_rotation"), ESearchCase::IgnoreCase)
					|| Key.Equals(TEXT("relative_scale3d"), ESearchCase::IgnoreCase)
					|| Key.Equals(TEXT("num_custom_data_floats"), ESearchCase::IgnoreCase)
					|| Key.Equals(TEXT("start_position"), ESearchCase::IgnoreCase)
					|| Key.Equals(TEXT("start_tangent"), ESearchCase::IgnoreCase)
					|| Key.Equals(TEXT("end_position"), ESearchCase::IgnoreCase)
					|| Key.Equals(TEXT("end_tangent"), ESearchCase::IgnoreCase)
					|| Key.Equals(TEXT("from_spline"), ESearchCase::IgnoreCase)
					|| Key.Equals(TEXT("start_scale"), ESearchCase::IgnoreCase)
					|| Key.Equals(TEXT("end_scale"), ESearchCase::IgnoreCase)
					|| Key.Equals(TEXT("start_roll"), ESearchCase::IgnoreCase)
					|| Key.Equals(TEXT("start_roll_degrees"), ESearchCase::IgnoreCase)
					|| Key.Equals(TEXT("end_roll"), ESearchCase::IgnoreCase)
					|| Key.Equals(TEXT("end_roll_degrees"), ESearchCase::IgnoreCase)
					|| Key.Equals(TEXT("start_offset"), ESearchCase::IgnoreCase)
					|| Key.Equals(TEXT("end_offset"), ESearchCase::IgnoreCase)
					|| Key.Equals(TEXT("forward_axis"), ESearchCase::IgnoreCase)
					|| Key.Equals(TEXT("spline_up_dir"), ESearchCase::IgnoreCase)
					|| Key.Equals(TEXT("smooth_interp_roll_scale"), ESearchCase::IgnoreCase)
					|| Key.Equals(TEXT("allow_spline_editing_per_instance"), ESearchCase::IgnoreCase)
					|| Key.Equals(TEXT("collision_enabled"), ESearchCase::IgnoreCase)
					|| Key.Equals(TEXT("generate_overlap_events"), ESearchCase::IgnoreCase)
					|| Key.Equals(TEXT("cast_shadow"), ESearchCase::IgnoreCase);

				if (bAllowed && Pair.Value.IsValid())
				{
					SetOp->SetField(Key, Pair.Value);
				}
			}
			Ops.Add(MakeShared<FJsonValueObject>(SetOp));

			if (bCompile)
			{
				TSharedRef<FJsonObject> CompileOp = MakeShared<FJsonObject>();
				CompileOp->SetStringField(TEXT("op"), TEXT("compile"));
				Ops.Add(MakeShared<FJsonValueObject>(CompileOp));
			}

			PatchBody->SetArrayField(TEXT("ops"), Ops);

			TSharedPtr<FJsonValue> ResponseData;
			const bool bCompleted = RunOnGameThreadWait([this, PatchBody, &ResponseData]()
			{
				int32 TotalInstances = 0;
				TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
				TArray<TSharedPtr<FJsonValue>> Errors;
				ExecuteBlueprintPatch(PatchBody, true, TotalInstances, Result, Errors);
				Result->SetArrayField(TEXT("errors"), Errors);
				ResponseData = MakeShared<FJsonValueObject>(Result);
			}, 10.0);

			if (!bCompleted || !ResponseData.IsValid())
			{
				OnComplete(MakeErrorResponse(504, TEXT("game_thread_timeout"), TEXT("Timed out waiting for GameThread execution")));
				return true;
			}

			const FString RequestId = ResolveOrCreateRequestId(Request);
			BAT::Http::JsonOk(OnComplete, ResponseData, 200, RequestId);
			return true;
		}));
}

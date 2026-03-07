#include "BlueprintAutomationToolkitModule.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HttpPath.h"
#include "HttpResultCallback.h"
#include "Http/HttpRequestUtils.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/ScopeLock.h"
#include "Misc/PackageName.h"
#include "FileHelpers.h"
#include "Engine/Blueprint.h"

namespace
{
	static FString NormalizeBlueprintObjectPathLocal(const FString& InPath)
	{
		FString Path = InPath;
		Path.TrimStartAndEndInline();
		if (Path.IsEmpty())
		{
			return Path;
		}

		if (!Path.Contains(TEXT(".")))
		{
			const FString AssetName = FPackageName::GetLongPackageAssetName(Path);
			if (!AssetName.IsEmpty())
			{
				Path = FString::Printf(TEXT("%s.%s"), *Path, *AssetName);
			}
		}

		return Path;
	}
}

void FBlueprintAutomationToolkitModule::BindBlueprintCompileRoutes()
{
	BlueprintCompileRoute = Router->BindRoute(
		FHttpPath(TEXT("/blueprint/compile")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, TEXT("/blueprint/compile")))
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
			if (!BodyObj->TryGetStringField(TEXT("blueprint"), BlueprintPath) || BlueprintPath.IsEmpty())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("missing_blueprint"), TEXT("Body must include 'blueprint'")));
				return true;
			}

			TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
			Payload->SetStringField(TEXT("blueprint"), BlueprintPath);
			const FString RequestId = ResolveOrCreateRequestId(Request);
			const FString JobId = SubmitJob(TEXT("blueprint.compile"), RequestId, Payload);

			TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("jobId"), JobId);
			Obj->SetStringField(TEXT("requestId"), RequestId);
			BAT::Http::JsonOk(OnComplete, MakeShared<FJsonValueObject>(Obj), 202, RequestId);

			return true;
		}));

	BlueprintSaveRoute = Router->BindRoute(
		FHttpPath(TEXT("/blueprint/save")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, TEXT("/blueprint/save")))
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
			if (!BodyObj->TryGetStringField(TEXT("blueprint"), BlueprintPath) || BlueprintPath.IsEmpty())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("missing_blueprint"), TEXT("Body must include 'blueprint'")));
				return true;
			}

			TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
			Payload->SetStringField(TEXT("blueprint"), BlueprintPath);
			const FString RequestId = ResolveOrCreateRequestId(Request);
			const FString IdempotencyKey = ReadHeaderValueCaseInsensitive(Request.Headers, TEXT("Idempotency-Key"));
			const FString ScopedKey = IdempotencyKey.IsEmpty() ? FString() : FString::Printf(TEXT("/blueprint/save:%s"), *IdempotencyKey);
			if (!ScopedKey.IsEmpty())
			{
				FScopeLock Lock(&JobMutex);
				if (const FString* ExistingJobId = IdempotencyToJob.Find(ScopedKey))
				{
					TSharedRef<FJsonObject> Cached = MakeShared<FJsonObject>();
					Cached->SetStringField(TEXT("jobId"), *ExistingJobId);
					Cached->SetStringField(TEXT("requestId"), RequestId);
					Cached->SetBoolField(TEXT("idempotent_replay"), true);
					BAT::Http::JsonOk(OnComplete, MakeShared<FJsonValueObject>(Cached), 202, RequestId);
					return true;
				}
			}

			const FString JobId = SubmitJob(TEXT("blueprint.save"), RequestId, Payload);
			if (!ScopedKey.IsEmpty())
			{
				FScopeLock Lock(&JobMutex);
				IdempotencyToJob.Add(ScopedKey, JobId);
			}

			TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("jobId"), JobId);
			Obj->SetStringField(TEXT("requestId"), RequestId);
			BAT::Http::JsonOk(OnComplete, MakeShared<FJsonValueObject>(Obj), 202, RequestId);

			return true;
		}));

	BlueprintCompileSaveRoute = Router->BindRoute(
		FHttpPath(TEXT("/blueprint/compile_save")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, TEXT("/blueprint/compile_save")))
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
			if (!BodyObj->TryGetStringField(TEXT("blueprint"), BlueprintPath) || BlueprintPath.TrimStartAndEnd().IsEmpty())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("missing_blueprint"), TEXT("Body must include 'blueprint'")));
				return true;
			}

			const FString RequestId = ResolveOrCreateRequestId(Request);
			const FString ObjectPath = NormalizeBlueprintObjectPathLocal(BlueprintPath);
			TSharedRef<FJsonObject> DataObj = MakeShared<FJsonObject>();
			TArray<TSharedPtr<FJsonValue>> Warnings;

			const bool bCompleted = RunOnGameThreadWait([&DataObj, &ObjectPath, &Warnings]()
			{
				UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *ObjectPath);
				if (!Blueprint)
				{
					return;
				}

				FKismetEditorUtilities::CompileBlueprint(Blueprint);
				bool bSaved = false;
				if (UPackage* Package = Blueprint->GetOutermost())
				{
					TArray<UPackage*> Packages;
					Packages.Add(Package);
					bSaved = UEditorLoadingAndSavingUtils::SavePackages(Packages, false);
				}

				if (!bSaved)
				{
					TSharedRef<FJsonObject> WarnObj = MakeShared<FJsonObject>();
					WarnObj->SetStringField(TEXT("code"), TEXT("save_failed"));
					WarnObj->SetStringField(TEXT("message"), TEXT("Compile succeeded but package save failed"));
					Warnings.Add(MakeShared<FJsonValueObject>(WarnObj));
				}

				DataObj->SetStringField(TEXT("blueprint"), ObjectPath);
				DataObj->SetBoolField(TEXT("compiled"), true);
				DataObj->SetBoolField(TEXT("saved"), bSaved);
			});

			if (!bCompleted)
			{
				OnComplete(MakeErrorResponse(504, RequestId, TEXT("game_thread_timeout"), TEXT("Timed out waiting for GameThread execution")));
				return true;
			}

			if (!DataObj->HasField(TEXT("compiled")))
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, RequestId, TEXT("not_found"), TEXT("Blueprint not found")));
				return true;
			}

			TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
			Root->SetBoolField(TEXT("ok"), true);
			Root->SetArrayField(TEXT("errors"), TArray<TSharedPtr<FJsonValue>>());
			Root->SetArrayField(TEXT("warnings"), Warnings);
			Root->SetObjectField(TEXT("data"), DataObj);
			OnComplete(BAT::Http::MakeJsonResponse(200, Root, RequestId));
			return true;
		}));
}

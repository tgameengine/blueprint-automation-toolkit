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

			const FString RequestId = ResolveOrCreateRequestId(Request);
			const FString ObjectPath = NormalizeBlueprintObjectPathLocal(BlueprintPath);

			TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
			TArray<TSharedPtr<FJsonValue>> Warnings;
			const bool bCompleted = RunOnGameThreadWait([&Data, &ObjectPath]()
			{
				UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *ObjectPath);
				if (!Blueprint)
				{
					return;
				}

				FKismetEditorUtilities::CompileBlueprint(Blueprint);
				Data->SetStringField(TEXT("blueprint"), ObjectPath);
				Data->SetStringField(TEXT("target"), ObjectPath);
				Data->SetBoolField(TEXT("compiled"), true);
				Data->SetBoolField(TEXT("saved"), false);
				Data->SetStringField(TEXT("compileStatus"), TEXT("compiled"));
				Data->SetStringField(TEXT("saveStatus"), TEXT("not_requested"));
				Data->SetArrayField(TEXT("errors"), TArray<TSharedPtr<FJsonValue>>());
			});

			if (!bCompleted)
			{
				OnComplete(MakeCanonicalErrorResponse(504, RequestId, TEXT("game_thread_timeout"), TEXT("Timed out waiting for GameThread execution.")));
				return true;
			}

			if (!Data->HasField(TEXT("compiled")))
			{
				OnComplete(MakeCanonicalErrorResponse(404, RequestId, TEXT("not_found"), TEXT("Blueprint not found.")));
				return true;
			}

			OnComplete(MakeCanonicalSuccessResponse(200, RequestId, Data, Warnings));
			return true;
		}));
}

#include "BlueprintAutomationToolkitModule.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HttpPath.h"
#include "HttpResultCallback.h"
#include "Http/HttpRequestUtils.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "BATAction.h"

namespace
{
	static bool IsPathInAllowedPrefixes(const FString& ObjectPath, const TArray<FString>& AllowedPrefixes)
	{
		for (const FString& Prefix : AllowedPrefixes)
		{
			if (!Prefix.IsEmpty() && ObjectPath.StartsWith(Prefix, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	}

	static TArray<UBATAction*> CollectActionAssets(const TArray<FString>& PackagePrefixes)
	{
		TArray<UBATAction*> Actions;
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

		FARFilter Filter;
		Filter.ClassPaths.Add(UBATAction::StaticClass()->GetClassPathName());
		Filter.bRecursiveClasses = true;
		Filter.bRecursivePaths = true;
		for (const FString& Prefix : PackagePrefixes)
		{
			if (!Prefix.IsEmpty())
			{
				Filter.PackagePaths.Add(FName(*Prefix));
			}
		}

		TArray<FAssetData> Assets;
		AssetRegistryModule.Get().GetAssets(Filter, Assets);
		for (const FAssetData& AssetData : Assets)
		{
			if (UBATAction* Action = Cast<UBATAction>(AssetData.GetAsset()))
			{
				Actions.Add(Action);
			}
		}
		return Actions;
	}
}

void FBlueprintAutomationToolkitModule::BindActionsRoutes()
{
	ActionsListRoute = Router->BindRoute(
		FHttpPath(TEXT("/actions/list")),
		EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, TEXT("/actions/list")))
			{
				return true;
			}

			const FString RequestId = ResolveOrCreateRequestId(Request);
			AsyncTask(ENamedThreads::GameThread, [this, RequestId, OnComplete]()
			{
				const TArray<UBATAction*> Actions = CollectActionAssets(AllowedActionAssetPrefixes);
				TArray<TSharedPtr<FJsonValue>> ActionRows;
				for (UBATAction* Action : Actions)
				{
					if (!Action)
					{
						continue;
					}
					TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
					Row->SetStringField(TEXT("name"), Action->ActionName.ToString());
					Row->SetStringField(TEXT("kind"), TEXT("blueprint"));
					Row->SetStringField(TEXT("asset"), Action->GetPathName());
					ActionRows.Add(MakeShared<FJsonValueObject>(Row));
				}

				TSharedRef<FJsonObject> ResponseObj = MakeShared<FJsonObject>();
				ResponseObj->SetBoolField(TEXT("ok"), true);
				ResponseObj->SetStringField(TEXT("request_id"), RequestId);
				ResponseObj->SetArrayField(TEXT("actions"), ActionRows);
				OnComplete(BAT::Http::MakeJsonResponse(200, ResponseObj, RequestId));
			});
			return true;
		}));

	ActionsRunRoute = Router->BindRoute(
		FHttpPath(TEXT("/actions/run")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, TEXT("/actions/run")))
			{
				return true;
			}

			TSharedPtr<FJsonObject> BodyObj;
			if (!BAT::Http::TryParseJsonBody(Request.Body, BodyObj) || !BodyObj.IsValid())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("bad_args"), TEXT("Invalid JSON body")));
				return true;
			}

			FString ActionName;
			BodyObj->TryGetStringField(TEXT("name"), ActionName);
			const TSharedPtr<FJsonObject>* ArgsField = nullptr;
			BodyObj->TryGetObjectField(TEXT("args"), ArgsField);
			const TSharedPtr<FJsonObject> ArgsObj = (ArgsField && ArgsField->IsValid()) ? *ArgsField : MakeShared<FJsonObject>();
			const FString RequestId = ResolveOrCreateRequestId(Request);

			AsyncTask(ENamedThreads::GameThread, [this, ActionName, ArgsObj, RequestId, OnComplete]()
			{
				const TArray<UBATAction*> Actions = CollectActionAssets(AllowedActionAssetPrefixes);
				UBATAction* Selected = nullptr;
				for (UBATAction* Candidate : Actions)
				{
					if (Candidate && Candidate->ActionName.ToString().Equals(ActionName, ESearchCase::IgnoreCase))
					{
						Selected = Candidate;
						break;
					}
				}

				if (!Selected)
				{
					OnComplete(MakeErrorResponse(EHttpServerResponseCodes::NotFound, RequestId, TEXT("not_found"), TEXT("Action not found")));
					return;
				}

				if (bSafeModeEnabled && !IsPathInAllowedPrefixes(Selected->GetPathName(), AllowedActionAssetPrefixes))
				{
					OnComplete(MakeErrorResponse(EHttpServerResponseCodes::Denied, RequestId, TEXT("safe_mode_denied"), TEXT("Action asset path is not allowlisted in safe mode")));
					return;
				}

				FString OutResultJson;
				FString OutError;
				bool bOk = false;
				Selected->Execute(BAT::Http::ToJsonString(ArgsObj.ToSharedRef()), OutResultJson, bOk, OutError);
				if (!bOk)
				{
					OnComplete(MakeErrorResponse(EHttpServerResponseCodes::ServerError, RequestId, TEXT("call_denied"), OutError.IsEmpty() ? TEXT("Action execution failed") : OutError));
					return;
				}

				TSharedPtr<FJsonValue> ResultValue = MakeShared<FJsonValueNull>();
				if (!OutResultJson.IsEmpty())
				{
					TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(OutResultJson);
					TSharedPtr<FJsonValue> Parsed;
					if (FJsonSerializer::Deserialize(Reader, Parsed) && Parsed.IsValid())
					{
						ResultValue = Parsed;
					}
					else
					{
						ResultValue = MakeShared<FJsonValueString>(OutResultJson);
					}
				}

				TSharedRef<FJsonObject> ResponseObj = MakeShared<FJsonObject>();
				ResponseObj->SetBoolField(TEXT("ok"), true);
				ResponseObj->SetStringField(TEXT("request_id"), RequestId);
				ResponseObj->SetField(TEXT("result"), ResultValue);
				OnComplete(BAT::Http::MakeJsonResponse(200, ResponseObj, RequestId));
			});
			return true;
		}));
}

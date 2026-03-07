#include "BlueprintAutomationToolkitModule.h"

#include "Domain/Requests/AssetSaveRequest.h"
#include "Services/AssetService.h"
#include "Transport/PolicyMiddleware.h"
#include "Transport/RequestParsing.h"

#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HttpPath.h"
#include "HttpResultCallback.h"
#include "HttpServerRequest.h"
#include "IHttpRouter.h"

namespace
{
	static bool TryBuildAssetDuplicateRequest(const TSharedPtr<FJsonObject>& BodyObj, FBATAssetDuplicateRequest& OutRequest)
	{
		OutRequest.Entries.Reset();
		OutRequest.bSave = BodyObj.IsValid() && BodyObj->HasTypedField<EJson::Boolean>(TEXT("save")) ? BodyObj->GetBoolField(TEXT("save")) : false;
		if (!BodyObj.IsValid())
		{
			return false;
		}

		FString SourcePath;
		FString DestinationPath;
		if (BodyObj->TryGetStringField(TEXT("src"), SourcePath) && BodyObj->TryGetStringField(TEXT("dst"), DestinationPath))
		{
			SourcePath.TrimStartAndEndInline();
			DestinationPath.TrimStartAndEndInline();
			if (!SourcePath.IsEmpty() && !DestinationPath.IsEmpty())
			{
				FBATAssetDuplicateEntry& Entry = OutRequest.Entries.AddDefaulted_GetRef();
				Entry.SourcePath = SourcePath;
				Entry.DestinationPath = DestinationPath;
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* DuplicatesField = nullptr;
		if (BodyObj->TryGetArrayField(TEXT("duplicates"), DuplicatesField) && DuplicatesField)
		{
			for (const TSharedPtr<FJsonValue>& EntryValue : *DuplicatesField)
			{
				if (!EntryValue.IsValid() || EntryValue->Type != EJson::Object)
				{
					continue;
				}

				const TSharedPtr<FJsonObject> EntryObject = EntryValue->AsObject();
				FString EntrySourcePath;
				FString EntryDestinationPath;
				if (!EntryObject.IsValid()
					|| !EntryObject->TryGetStringField(TEXT("src"), EntrySourcePath)
					|| !EntryObject->TryGetStringField(TEXT("dst"), EntryDestinationPath))
				{
					continue;
				}

				EntrySourcePath.TrimStartAndEndInline();
				EntryDestinationPath.TrimStartAndEndInline();
				if (EntrySourcePath.IsEmpty() || EntryDestinationPath.IsEmpty())
				{
					continue;
				}

				FBATAssetDuplicateEntry& Entry = OutRequest.Entries.AddDefaulted_GetRef();
				Entry.SourcePath = EntrySourcePath;
				Entry.DestinationPath = EntryDestinationPath;
			}
		}

		return OutRequest.Entries.Num() > 0;
	}

	static bool TryBuildAssetCreateRequest(const TSharedPtr<FJsonObject>& BodyObj, FBATAssetCreateRequest& OutRequest)
	{
		OutRequest = FBATAssetCreateRequest();
		if (!BodyObj.IsValid())
		{
			return false;
		}

		BodyObj->TryGetStringField(TEXT("class"), OutRequest.ClassPath);
		BodyObj->TryGetStringField(TEXT("path"), OutRequest.AssetPath);
		BodyObj->TryGetStringField(TEXT("outer"), OutRequest.OuterPath);
		BodyObj->TryGetBoolField(TEXT("save"), OutRequest.bSave);
		OutRequest.Body = BodyObj;
		OutRequest.ClassPath.TrimStartAndEndInline();
		OutRequest.AssetPath.TrimStartAndEndInline();
		OutRequest.OuterPath.TrimStartAndEndInline();
		return !OutRequest.ClassPath.IsEmpty();
	}

	static bool TryBuildAssetSaveRequest(const TSharedPtr<FJsonObject>& BodyObj, FBATAssetSaveRequest& OutRequest)
	{
		OutRequest.Paths.Reset();
		if (!BodyObj.IsValid())
		{
			return false;
		}

		FString SinglePath;
		if (BodyObj->TryGetStringField(TEXT("path"), SinglePath) && !SinglePath.TrimStartAndEnd().IsEmpty())
		{
			OutRequest.Paths.Add(SinglePath.TrimStartAndEnd());
			return true;
		}

		if (BodyObj->TryGetStringField(TEXT("target"), SinglePath) && !SinglePath.TrimStartAndEnd().IsEmpty())
		{
			OutRequest.Paths.Add(SinglePath.TrimStartAndEnd());
			return true;
		}

		const TArray<TSharedPtr<FJsonValue>>* PathsField = nullptr;
		if (!BodyObj->TryGetArrayField(TEXT("paths"), PathsField) || !PathsField)
		{
			return false;
		}

		for (const TSharedPtr<FJsonValue>& PathValue : *PathsField)
		{
			if (!PathValue.IsValid() || PathValue->Type != EJson::String)
			{
				return false;
			}

			const FString Path = PathValue->AsString().TrimStartAndEnd();
			if (Path.IsEmpty())
			{
				return false;
			}
			OutRequest.Paths.Add(Path);
		}

		return OutRequest.Paths.Num() > 0;
	}
}

void FBlueprintAutomationToolkitModule::BindAssetDuplicateRoute()
{
	AssetDuplicateRoute = Router->BindRoute(
		FHttpPath(TEXT("/asset/duplicate")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!BAT::Transport::ValidateAndHandleRequest(*this, Request, OnComplete, TEXT("/asset/duplicate")))
			{
				return true;
			}

			TSharedPtr<FJsonObject> BodyObj;
			if (!BAT::Transport::TryParseJsonObjectBody(Request, BodyObj))
			{
				OnComplete(MakeCanonicalErrorResponse(400, ResolveOrCreateRequestId(Request), TEXT("bad_json"), TEXT("Invalid JSON body.")));
				return true;
			}

			FBATAssetDuplicateRequest DuplicateRequest;
			if (!TryBuildAssetDuplicateRequest(BodyObj, DuplicateRequest))
			{
				OnComplete(MakeCanonicalErrorResponse(400, ResolveOrCreateRequestId(Request), TEXT("bad_args"), TEXT("Body must include 'src'/'dst' or non-empty 'duplicates' array.")));
				return true;
			}

			const FString RequestId = ResolveOrCreateRequestId(Request);
			AsyncTask(ENamedThreads::GameThread, [this, DuplicateRequest, RequestId, OnComplete]()
			{
				const FAssetService Service;
				const FAutomationResult Result = Service.DuplicateAssets(*this, DuplicateRequest);
				OnComplete(MakeCanonicalResponseFromAutomationResult(Result, RequestId));
			});
			return true;
		}));
}

void FBlueprintAutomationToolkitModule::BindAssetCreateRoute()
{
	AssetCreateRoute = Router->BindRoute(
		FHttpPath(TEXT("/asset/create")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!BAT::Transport::ValidateAndHandleRequest(*this, Request, OnComplete, TEXT("/asset/create")))
			{
				return true;
			}

			TSharedPtr<FJsonObject> BodyObj;
			if (!BAT::Transport::TryParseJsonObjectBody(Request, BodyObj))
			{
				OnComplete(MakeCanonicalErrorResponse(400, ResolveOrCreateRequestId(Request), TEXT("bad_json"), TEXT("Invalid JSON body.")));
				return true;
			}

			FBATAssetCreateRequest CreateRequest;
			if (!TryBuildAssetCreateRequest(BodyObj, CreateRequest))
			{
				OnComplete(MakeCanonicalErrorResponse(400, ResolveOrCreateRequestId(Request), TEXT("bad_args"), TEXT("Asset creation requires a non-empty 'class' field.")));
				return true;
			}

			const FString RequestId = ResolveOrCreateRequestId(Request);
			AsyncTask(ENamedThreads::GameThread, [this, CreateRequest, RequestId, OnComplete]()
			{
				const FAssetService Service;
				const FAutomationResult Result = Service.CreateAsset(*this, CreateRequest);
				OnComplete(MakeCanonicalResponseFromAutomationResult(Result, RequestId));
			});
			return true;
		}));
}

void FBlueprintAutomationToolkitModule::BindAssetSaveRoute()
{
	AssetSaveRoute = Router->BindRoute(
		FHttpPath(TEXT("/asset/save")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!BAT::Transport::ValidateAndHandleRequest(*this, Request, OnComplete, TEXT("/asset/save")))
			{
				return true;
			}

			TSharedPtr<FJsonObject> BodyObj;
			if (!BAT::Transport::TryParseJsonObjectBody(Request, BodyObj))
			{
				OnComplete(MakeCanonicalErrorResponse(400, ResolveOrCreateRequestId(Request), TEXT("bad_json"), TEXT("Invalid JSON body.")));
				return true;
			}

			FBATAssetSaveRequest SaveRequest;
			if (!TryBuildAssetSaveRequest(BodyObj, SaveRequest))
			{
				OnComplete(MakeCanonicalErrorResponse(400, ResolveOrCreateRequestId(Request), TEXT("bad_args"), TEXT("Body must include 'path', 'target', or non-empty 'paths' array.")));
				return true;
			}

			const FString RequestId = ResolveOrCreateRequestId(Request);
			AsyncTask(ENamedThreads::GameThread, [this, SaveRequest, RequestId, OnComplete]()
			{
				const FAssetService Service;
				const FAutomationResult Result = Service.SaveAssets(*this, SaveRequest);
				OnComplete(MakeCanonicalResponseFromAutomationResult(Result, RequestId));
			});
			return true;
		}));
}
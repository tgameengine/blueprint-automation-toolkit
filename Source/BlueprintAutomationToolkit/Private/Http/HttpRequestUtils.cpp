#include "Http/HttpRequestUtils.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Transport/ErrorMapping.h"

namespace
{
	static FCriticalSection GPendingResponseExportMutex;
	static TMap<FString, FString> GPendingResponseExports;

	static FString ResolveRequestId(const FString& RequestId)
	{
		return RequestId.IsEmpty()
			? FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower)
			: RequestId;
	}

	static FString GetResponseExportRoot()
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("BlueprintAutomationToolkit"), TEXT("Responses"));
	}

	static bool IsUnsafeResponsePath(const FString& RelativePath)
	{
		if (RelativePath.IsEmpty() || RelativePath.StartsWith(TEXT("/")) || RelativePath.StartsWith(TEXT("\\")) || RelativePath.Contains(TEXT(":")))
		{
			return true;
		}

		TArray<FString> Segments;
		RelativePath.ParseIntoArray(Segments, TEXT("/"), true);
		for (const FString& Segment : Segments)
		{
			if (Segment.Equals(TEXT(".."), ESearchCase::CaseSensitive) || Segment.Equals(TEXT("."), ESearchCase::CaseSensitive))
			{
				return true;
			}
		}

		return false;
	}

	static bool WriteResponseFile(const FString& AbsolutePath, const FHttpServerResponse& Response, FString& OutError)
	{
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(AbsolutePath), true);
		if (!FFileHelper::SaveArrayToFile(Response.Body, *AbsolutePath))
		{
			OutError = FString::Printf(TEXT("Failed to write response to '%s'."), *AbsolutePath);
			return false;
		}

		return true;
	}

	static void TryFlushPendingResponseExport(const FString& RequestId, const FHttpServerResponse& Response)
	{
		if (RequestId.IsEmpty())
		{
			return;
		}

		FString AbsolutePath;
		{
			FScopeLock Lock(&GPendingResponseExportMutex);
			if (FString* PendingPath = GPendingResponseExports.Find(RequestId))
			{
				AbsolutePath = *PendingPath;
				GPendingResponseExports.Remove(RequestId);
			}
		}

		if (AbsolutePath.IsEmpty())
		{
			return;
		}

		FString Error;
		if (!WriteResponseFile(AbsolutePath, Response, Error))
		{
			UE_LOG(LogTemp, Warning, TEXT("BlueprintAutomationToolkit response export failed for request '%s': %s"), *RequestId, *Error);
		}
	}
}

namespace BAT::Http
{
	FString ToJsonString(const TSharedRef<FJsonObject>& Object)
	{
		FString Out;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Object, Writer);
		return Out;
	}

	bool TryParseJsonBody(const TArray<uint8>& Body, TSharedPtr<FJsonObject>& OutObj)
	{
		if (Body.Num() <= 0)
		{
			OutObj.Reset();
			return true;
		}

		const FString BodyString(Body.Num(), reinterpret_cast<const ANSICHAR*>(Body.GetData()));
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(BodyString);
		return FJsonSerializer::Deserialize(Reader, OutObj) && OutObj.IsValid();
	}

	bool HasResponseOutputPath(const TSharedPtr<FJsonObject>& BodyObj)
	{
		if (!BodyObj.IsValid())
		{
			return false;
		}

		FString OutputPath;
		if (!BodyObj->TryGetStringField(TEXT("responseOutputPath"), OutputPath))
		{
			return false;
		}

		OutputPath.TrimStartAndEndInline();
		return !OutputPath.IsEmpty();
	}

	bool TryResolveResponseOutputPath(const TSharedPtr<FJsonObject>& BodyObj, const FString& RequestId, FString& OutAbsolutePath, FString& OutError)
	{
		OutAbsolutePath.Reset();
		OutError.Reset();

		if (!HasResponseOutputPath(BodyObj))
		{
			return true;
		}

		FString RelativePath;
		BodyObj->TryGetStringField(TEXT("responseOutputPath"), RelativePath);
		RelativePath.TrimStartAndEndInline();
		FPaths::NormalizeFilename(RelativePath);

		if (IsUnsafeResponsePath(RelativePath))
		{
			OutError = TEXT("responseOutputPath must be a safe relative path without traversal or drive prefixes.");
			return false;
		}

		if (RelativePath.EndsWith(TEXT("/"), ESearchCase::CaseSensitive))
		{
			RelativePath /= RequestId + TEXT(".json");
		}
		else if (FPaths::GetExtension(RelativePath).IsEmpty())
		{
			RelativePath += TEXT(".json");
		}

		FString ExportRoot = GetResponseExportRoot();
		FPaths::NormalizeDirectoryName(ExportRoot);
		OutAbsolutePath = FPaths::Combine(ExportRoot, RelativePath);
		FPaths::NormalizeFilename(OutAbsolutePath);
		FPaths::CollapseRelativeDirectories(OutAbsolutePath);

		FString RootPrefix = ExportRoot;
		if (!RootPrefix.EndsWith(TEXT("/"), ESearchCase::CaseSensitive))
		{
			RootPrefix += TEXT("/");
		}

		if (!OutAbsolutePath.StartsWith(RootPrefix, ESearchCase::IgnoreCase))
		{
			OutError = TEXT("responseOutputPath resolved outside the allowed response export directory.");
			OutAbsolutePath.Reset();
			return false;
		}

		return true;
	}

	bool TryWriteResponseToDisk(const TSharedPtr<FJsonObject>& BodyObj, const FString& RequestId, const FHttpServerResponse& Response, FString& OutAbsolutePath, FString& OutError)
	{
		if (!TryResolveResponseOutputPath(BodyObj, RequestId, OutAbsolutePath, OutError) || OutAbsolutePath.IsEmpty())
		{
			return OutError.IsEmpty();
		}

		return WriteResponseFile(OutAbsolutePath, Response, OutError);
	}

	bool RegisterPendingResponseExport(const TSharedPtr<FJsonObject>& BodyObj, const FString& RequestId, FString& OutError)
	{
		OutError.Reset();
		if (!HasResponseOutputPath(BodyObj))
		{
			return true;
		}

		FString AbsolutePath;
		if (!TryResolveResponseOutputPath(BodyObj, RequestId, AbsolutePath, OutError))
		{
			return false;
		}

		FScopeLock Lock(&GPendingResponseExportMutex);
		GPendingResponseExports.Add(RequestId, AbsolutePath);
		return true;
	}

	TUniquePtr<FHttpServerResponse> MakeJsonResponseFromString(int32 StatusCode, const FString& JsonString, const FString& RequestId)
	{
		const FString ResolvedRequestId = ResolveRequestId(RequestId);
		TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(JsonString, TEXT("application/json"));
		Response->Code = static_cast<EHttpServerResponseCodes>(StatusCode);
		Response->Headers.FindOrAdd(TEXT("Cache-Control")).Add(TEXT("no-store"));
		Response->Headers.FindOrAdd(TEXT("X-Request-Id")).Add(ResolvedRequestId);
		TryFlushPendingResponseExport(ResolvedRequestId, *Response);
		return Response;
	}

	TUniquePtr<FHttpServerResponse> MakeJsonResponse(int32 StatusCode, const TSharedRef<FJsonObject>& Object, const FString& RequestId)
	{
		const FString ResolvedRequestId = ResolveRequestId(RequestId);
		TSharedRef<FJsonObject> ResponseObject = MakeShared<FJsonObject>(*Object);
		if (!ResolvedRequestId.IsEmpty() && !ResponseObject->HasField(TEXT("requestId")))
		{
			ResponseObject->SetStringField(TEXT("requestId"), ResolvedRequestId);
		}

		const FString Json = ToJsonString(ResponseObject);
		return MakeJsonResponseFromString(StatusCode, Json, ResolvedRequestId);
	}

	TUniquePtr<FHttpServerResponse> MakeJsonOk(const TSharedPtr<FJsonValue>& Data, int32 StatusCode, const FString& RequestId)
	{
		const FString ResolvedRequestId = ResolveRequestId(RequestId);
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetBoolField(TEXT("ok"), true);
		Root->SetStringField(TEXT("requestId"), ResolvedRequestId);

		TArray<TSharedPtr<FJsonValue>> Errors;
		TArray<TSharedPtr<FJsonValue>> Warnings;
		Root->SetArrayField(TEXT("errors"), Errors);
		Root->SetArrayField(TEXT("warnings"), Warnings);
		Root->SetField(TEXT("data"), Data.IsValid() ? Data : MakeShared<FJsonValueObject>(MakeShared<FJsonObject>()));
		return MakeJsonResponse(StatusCode, Root, ResolvedRequestId);
	}

	TUniquePtr<FHttpServerResponse> MakeJsonError(int32 StatusCode, const FString& Code, const FString& Message, const FString& RequestId)
	{
		const FString ResolvedRequestId = ResolveRequestId(RequestId);
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetBoolField(TEXT("ok"), false);
		Root->SetStringField(TEXT("requestId"), ResolvedRequestId);

		TArray<TSharedPtr<FJsonValue>> Errors;
		Errors.Add(MakeShared<FJsonValueObject>(BAT::Transport::MakeIssueObject(Code, Message)));
		TArray<TSharedPtr<FJsonValue>> Warnings;

		Root->SetArrayField(TEXT("errors"), Errors);
		Root->SetArrayField(TEXT("warnings"), Warnings);
		Root->SetObjectField(TEXT("data"), MakeShared<FJsonObject>());
		return MakeJsonResponse(StatusCode, Root, ResolvedRequestId);
	}

	void JsonOk(const FHttpResultCallback& OnComplete, const TSharedPtr<FJsonValue>& Data, int32 StatusCode, const FString& RequestId)
	{
		OnComplete(MakeJsonOk(Data, StatusCode, RequestId));
	}

	void JsonError(const FHttpResultCallback& OnComplete, const FString& Code, const FString& Message, int32 StatusCode, const FString& RequestId)
	{
		OnComplete(MakeJsonError(StatusCode, Code, Message, RequestId));
	}
}

#include "Http/HttpRequestUtils.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
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

		IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutAbsolutePath), true);
		if (!FFileHelper::SaveArrayToFile(Response.Body, *OutAbsolutePath))
		{
			OutError = FString::Printf(TEXT("Failed to write response to '%s'."), *OutAbsolutePath);
			return false;
		}

		return true;
	}

	TUniquePtr<FHttpServerResponse> MakeJsonResponse(int32 StatusCode, const TSharedRef<FJsonObject>& Object, const FString& RequestId)
	{
		const FString Json = ToJsonString(Object);
		TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(Json, TEXT("application/json"));
		Response->Code = static_cast<EHttpServerResponseCodes>(StatusCode);
		Response->Headers.FindOrAdd(TEXT("Cache-Control")).Add(TEXT("no-store"));
		Response->Headers.FindOrAdd(TEXT("X-Request-Id")).Add(ResolveRequestId(RequestId));
		return Response;
	}

	TUniquePtr<FHttpServerResponse> MakeJsonOk(const TSharedPtr<FJsonValue>& Data, int32 StatusCode, const FString& RequestId)
	{
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetBoolField(TEXT("ok"), true);

		TArray<TSharedPtr<FJsonValue>> Errors;
		TArray<TSharedPtr<FJsonValue>> Warnings;
		Root->SetArrayField(TEXT("errors"), Errors);
		Root->SetArrayField(TEXT("warnings"), Warnings);
		Root->SetField(TEXT("data"), Data.IsValid() ? Data : MakeShared<FJsonValueObject>(MakeShared<FJsonObject>()));
		return MakeJsonResponse(StatusCode, Root, RequestId);
	}

	TUniquePtr<FHttpServerResponse> MakeJsonError(int32 StatusCode, const FString& Code, const FString& Message, const FString& RequestId)
	{
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetBoolField(TEXT("ok"), false);

		TSharedRef<FJsonObject> Error = MakeShared<FJsonObject>();
		Error->SetStringField(TEXT("code"), Code);
		Error->SetStringField(TEXT("message"), Message);

		TArray<TSharedPtr<FJsonValue>> Errors;
		Errors.Add(MakeShared<FJsonValueObject>(Error));
		TArray<TSharedPtr<FJsonValue>> Warnings;

		Root->SetArrayField(TEXT("errors"), Errors);
		Root->SetArrayField(TEXT("warnings"), Warnings);
		Root->SetObjectField(TEXT("data"), MakeShared<FJsonObject>());
		return MakeJsonResponse(StatusCode, Root, RequestId);
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

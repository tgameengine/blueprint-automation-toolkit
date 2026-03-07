#include "Http/HttpRequestUtils.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/Guid.h"
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

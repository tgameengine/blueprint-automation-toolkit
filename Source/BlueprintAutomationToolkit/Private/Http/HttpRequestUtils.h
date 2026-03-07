#pragma once

#include "CoreMinimal.h"
#include "HttpResultCallback.h"
#include "HttpServerResponse.h"

class FJsonObject;
class FJsonValue;

namespace BAT::Http
{
	FString ToJsonString(const TSharedRef<FJsonObject>& Object);
	bool TryParseJsonBody(const TArray<uint8>& Body, TSharedPtr<FJsonObject>& OutObj);

	TUniquePtr<FHttpServerResponse> MakeJsonResponse(int32 StatusCode, const TSharedRef<FJsonObject>& Object, const FString& RequestId = FString());
	TUniquePtr<FHttpServerResponse> MakeJsonOk(const TSharedPtr<FJsonValue>& Data, int32 StatusCode = 200, const FString& RequestId = FString());
	TUniquePtr<FHttpServerResponse> MakeJsonError(int32 StatusCode, const FString& Code, const FString& Message, const FString& RequestId = FString());

	void JsonOk(const FHttpResultCallback& OnComplete, const TSharedPtr<FJsonValue>& Data, int32 StatusCode = 200, const FString& RequestId = FString());
	void JsonError(const FHttpResultCallback& OnComplete, const FString& Code, const FString& Message, int32 StatusCode, const FString& RequestId = FString());
}

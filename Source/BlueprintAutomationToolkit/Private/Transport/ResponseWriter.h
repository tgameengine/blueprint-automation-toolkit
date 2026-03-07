#pragma once

#include "CoreMinimal.h"
#include "Templates/SharedPointer.h"

struct FAutomationResult;
struct FHttpServerResponse;
class FJsonObject;
class FJsonValue;

namespace BAT::Transport
{
	TSharedRef<FJsonObject> MakeEnvelope(
		const FString& RequestId,
		bool bOk,
		const TSharedPtr<FJsonObject>& Data,
		const TArray<TSharedPtr<FJsonValue>>& Warnings,
		const TArray<TSharedPtr<FJsonValue>>& Errors);

	TUniquePtr<FHttpServerResponse> MakeSuccessResponse(
		int32 HttpCode,
		const FString& RequestId,
		const TSharedPtr<FJsonObject>& Data,
		const TArray<TSharedPtr<FJsonValue>>& Warnings = {});

	TUniquePtr<FHttpServerResponse> MakeErrorResponse(
		int32 HttpCode,
		const FString& RequestId,
		const FString& Code,
		const FString& Message,
		const TSharedPtr<FJsonObject>& Details = nullptr,
		const TArray<TSharedPtr<FJsonValue>>& Warnings = {},
		const FString& SuggestedAction = FString(),
		const TOptional<bool>& RecoverableOverride = TOptional<bool>(),
		const FString& Target = FString());

	TUniquePtr<FHttpServerResponse> MakeResponseFromAutomationResult(const FAutomationResult& Result, const FString& RequestId);
}
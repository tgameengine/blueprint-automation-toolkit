#pragma once

#include "CoreMinimal.h"
#include "Templates/SharedPointer.h"

class FJsonObject;
class FJsonValue;

namespace BAT::Transport
{
	struct FErrorDisposition
	{
		bool bRecoverable = false;
		FString SuggestedAction;
	};

	FString NormalizeErrorCode(const FString& InCode);
	FErrorDisposition DescribeError(const FString& InCode, int32 StatusCode);
	TSharedRef<FJsonObject> MakeIssueObject(
		const FString& RawCode,
		const FString& Message,
		const TSharedPtr<FJsonObject>& Details = nullptr,
		const TOptional<bool>& RecoverableOverride = TOptional<bool>(),
		const FString& Target = FString(),
		const FString& SuggestedActionOverride = FString());
	TArray<TSharedPtr<FJsonValue>> NormalizeIssueArray(const TArray<TSharedPtr<FJsonValue>>& RawIssues, const FString& DefaultCode);
}
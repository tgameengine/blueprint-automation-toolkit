#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Templates/SharedPointer.h"

class FBlueprintAutomationToolkitModule;

struct FAutomationContext
{
	FString RequestId;
	FString Endpoint;
	TSharedPtr<class FJsonObject> Body;
	TMap<FString, FString> StringParams;
	FBlueprintAutomationToolkitModule* Module = nullptr;
	bool bReturnRawObject = false;
};

struct FAutomationResult
{
	bool bSuccess = false;
	int32 StatusCode = 200;
	TSharedPtr<FJsonValue> Data;
	TSharedPtr<FJsonValue> ErrorData;
	FString ErrorCode;
	FString ErrorMessage;

	static FAutomationResult Ok(const TSharedPtr<FJsonValue>& InData, int32 InStatusCode = 200)
	{
		FAutomationResult Result;
		Result.bSuccess = true;
		Result.StatusCode = InStatusCode;
		Result.Data = InData;
		return Result;
	}

	static FAutomationResult Error(const FString& InCode, const FString& InMessage, int32 InStatusCode)
	{
		FAutomationResult Result;
		Result.bSuccess = false;
		Result.StatusCode = InStatusCode;
		Result.ErrorCode = InCode;
		Result.ErrorMessage = InMessage;
		return Result;
	}

	static FAutomationResult ErrorWithData(const FString& InCode, const FString& InMessage, int32 InStatusCode, const TSharedPtr<FJsonValue>& InErrorData)
	{
		FAutomationResult Result = Error(InCode, InMessage, InStatusCode);
		Result.ErrorData = InErrorData;
		return Result;
	}
};

struct FAutomationCommand
{
	virtual ~FAutomationCommand() = default;
	virtual FAutomationResult Execute(FAutomationContext& Context) = 0;
};

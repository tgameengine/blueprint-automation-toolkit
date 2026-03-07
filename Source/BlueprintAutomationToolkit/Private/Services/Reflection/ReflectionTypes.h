#pragma once

#include "Commands/AutomationCommand.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

class UObject;
class UClass;
class FProperty;
class UStruct;

namespace BAT::Reflection
{
	struct FResolvedObject
	{
		UObject* Object = nullptr;
		UClass* RequiredClass = nullptr;
		FString ResolutionSource;
		FString ResolvedObjectPath;
	};

	struct FResolvedProperty
	{
		FProperty* Property = nullptr;
		void* ValuePtr = nullptr;
		UStruct* OwnerStruct = nullptr;
		UObject* OwnerObject = nullptr;
		FString PropertyPath;
	};

	inline FAutomationResult MakeStructuredSuccess(const FString& RequestId, const TSharedRef<FJsonObject>& Data, int32 StatusCode = 200)
	{
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetBoolField(TEXT("success"), true);
		Root->SetStringField(TEXT("requestId"), RequestId);
		Root->SetObjectField(TEXT("data"), Data);
		return FAutomationResult::Ok(MakeShared<FJsonValueObject>(Root), StatusCode);
	}

	inline FAutomationResult MakeStructuredError(const FString& RequestId, const FString& Code, const FString& Message, int32 StatusCode, const TSharedPtr<FJsonObject>& Details = nullptr)
	{
		TSharedRef<FJsonObject> Error = MakeShared<FJsonObject>();
		Error->SetStringField(TEXT("code"), Code);
		Error->SetStringField(TEXT("message"), Message);
		if (Details.IsValid())
		{
			Error->SetObjectField(TEXT("details"), Details.ToSharedRef());
		}

		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetBoolField(TEXT("success"), false);
		Root->SetStringField(TEXT("requestId"), RequestId);
		Root->SetObjectField(TEXT("error"), Error);
		return FAutomationResult::ErrorWithData(Code, Message, StatusCode, MakeShared<FJsonValueObject>(Root));
	}
}
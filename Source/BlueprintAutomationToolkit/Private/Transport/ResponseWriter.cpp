// Copyright 2026 AkaSoft. All Rights Reserved.

#include "Transport/ResponseWriter.h"

#include "Commands/AutomationCommand.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Http/HttpRequestUtils.h"
#include "Transport/ErrorMapping.h"

namespace
{
	static TSharedPtr<FJsonObject> JsonValueToObject(const TSharedPtr<FJsonValue>& Value)
	{
		return Value.IsValid() && Value->Type == EJson::Object ? Value->AsObject() : nullptr;
	}

	static FString DeriveObjectNameFromPath(const FString& ObjectPath)
	{
		int32 DotIndex = INDEX_NONE;
		if (ObjectPath.FindLastChar(TEXT('.'), DotIndex) && DotIndex + 1 < ObjectPath.Len())
		{
			return ObjectPath.Mid(DotIndex + 1);
		}

		int32 SlashIndex = INDEX_NONE;
		if (ObjectPath.FindLastChar(TEXT('/'), SlashIndex) && SlashIndex + 1 < ObjectPath.Len())
		{
			return ObjectPath.Mid(SlashIndex + 1);
		}

		return ObjectPath;
	}

	static void PromoteObjectReferenceFields(TSharedPtr<FJsonObject>& Data)
	{
		if (!Data.IsValid())
		{
			return;
		}

		const TSharedPtr<FJsonObject>* ObjectPtr = nullptr;
		if (!Data->TryGetObjectField(TEXT("object"), ObjectPtr) || !ObjectPtr || !ObjectPtr->IsValid())
		{
			if (Data->HasTypedField<EJson::Object>(TEXT("target")))
			{
				return;
			}
			return;
		}

		const TSharedPtr<FJsonObject> Object = *ObjectPtr;
		Data->SetObjectField(TEXT("target"), Object.ToSharedRef());

		FString ObjectPath;
		if (Object->TryGetStringField(TEXT("objectPath"), ObjectPath) && !ObjectPath.IsEmpty())
		{
			Data->SetStringField(TEXT("objectPath"), ObjectPath);
			if (!Data->HasField(TEXT("objectName")))
			{
				Data->SetStringField(TEXT("objectName"), DeriveObjectNameFromPath(ObjectPath));
			}
		}

		FString ClassPath;
		if (Object->TryGetStringField(TEXT("classPath"), ClassPath) && !ClassPath.IsEmpty())
		{
			Data->SetStringField(TEXT("classPath"), ClassPath);
		}

		FString ClassName;
		if (Object->TryGetStringField(TEXT("className"), ClassName) && !ClassName.IsEmpty())
		{
			Data->SetStringField(TEXT("className"), ClassName);
		}
	}
}

namespace BAT::Transport
{
	TSharedRef<FJsonObject> MakeEnvelope(
		const FString& RequestId,
		bool bOk,
		const TSharedPtr<FJsonObject>& Data,
		const TArray<TSharedPtr<FJsonValue>>& Warnings,
		const TArray<TSharedPtr<FJsonValue>>& Errors)
	{
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetBoolField(TEXT("ok"), bOk);
		if (!RequestId.IsEmpty())
		{
			Root->SetStringField(TEXT("requestId"), RequestId);
		}
		Root->SetObjectField(TEXT("data"), Data.IsValid() ? Data.ToSharedRef() : MakeShared<FJsonObject>());
		Root->SetArrayField(TEXT("warnings"), Warnings);
		Root->SetArrayField(TEXT("errors"), Errors);
		return Root;
	}

	TUniquePtr<FHttpServerResponse> MakeSuccessResponse(
		int32 HttpCode,
		const FString& RequestId,
		const TSharedPtr<FJsonObject>& Data,
		const TArray<TSharedPtr<FJsonValue>>& Warnings)
	{
		return BAT::Http::MakeJsonResponse(HttpCode, MakeEnvelope(RequestId, true, Data, NormalizeIssueArray(Warnings, TEXT("warning")), {}), RequestId);
	}

	TUniquePtr<FHttpServerResponse> MakeErrorResponse(
		int32 HttpCode,
		const FString& RequestId,
		const FString& Code,
		const FString& Message,
		const TSharedPtr<FJsonObject>& Details,
		const TArray<TSharedPtr<FJsonValue>>& Warnings,
		const FString& SuggestedAction,
		const TOptional<bool>& RecoverableOverride,
		const FString& Target)
	{
		TArray<TSharedPtr<FJsonValue>> Errors;
		Errors.Add(MakeShared<FJsonValueObject>(MakeIssueObject(Code, Message, Details, RecoverableOverride, Target, SuggestedAction)));
		return BAT::Http::MakeJsonResponse(HttpCode, MakeEnvelope(RequestId, false, MakeShared<FJsonObject>(), NormalizeIssueArray(Warnings, TEXT("warning")), Errors), RequestId);
	}

	TUniquePtr<FHttpServerResponse> MakeResponseFromAutomationResult(const FAutomationResult& Result, const FString& RequestId)
	{
		TArray<TSharedPtr<FJsonValue>> Warnings;
		TSharedPtr<FJsonObject> Data;
		TSharedPtr<FJsonObject> Details;
		FString ErrorCode = Result.ErrorCode;
		FString ErrorMessage = Result.ErrorMessage;

		const TSharedPtr<FJsonObject> PrimaryObject = JsonValueToObject(Result.bSuccess ? Result.Data : (Result.ErrorData.IsValid() ? Result.ErrorData : Result.Data));
		bool bTreatAsFailure = !Result.bSuccess;
		if (PrimaryObject.IsValid())
		{
			bool bStructuredSuccess = true;
			if ((PrimaryObject->TryGetBoolField(TEXT("success"), bStructuredSuccess) || PrimaryObject->TryGetBoolField(TEXT("ok"), bStructuredSuccess)) && !bStructuredSuccess)
			{
				bTreatAsFailure = true;
			}
		}

		if (PrimaryObject.IsValid())
		{
			const TArray<TSharedPtr<FJsonValue>>* WarningArray = nullptr;
			if (PrimaryObject->TryGetArrayField(TEXT("warnings"), WarningArray) && WarningArray)
			{
				Warnings = NormalizeIssueArray(*WarningArray, TEXT("warning"));
			}

			const TSharedPtr<FJsonObject>* DataPtr = nullptr;
			if (PrimaryObject->TryGetObjectField(TEXT("data"), DataPtr) && DataPtr && DataPtr->IsValid())
			{
				Data = MakeShared<FJsonObject>(**DataPtr);
			}
			else if (Result.bSuccess)
			{
				Data = MakeShared<FJsonObject>(*PrimaryObject);
			}

			if (bTreatAsFailure)
			{
				const TArray<TSharedPtr<FJsonValue>>* ErrorArray = nullptr;
				if (PrimaryObject->TryGetArrayField(TEXT("errors"), ErrorArray) && ErrorArray)
				{
					const TArray<TSharedPtr<FJsonValue>> NormalizedErrors = NormalizeIssueArray(*ErrorArray, TEXT("error"));
					if (NormalizedErrors.Num() > 0)
					{
						if (!Details.IsValid())
						{
							Details = MakeShared<FJsonObject>();
						}
						Details->SetArrayField(TEXT("errors"), NormalizedErrors);

						const TSharedPtr<FJsonObject> FirstError = JsonValueToObject(NormalizedErrors[0]);
						if (FirstError.IsValid())
						{
							FirstError->TryGetStringField(TEXT("code"), ErrorCode);
							FirstError->TryGetStringField(TEXT("message"), ErrorMessage);
						}
					}
				}

				const TSharedPtr<FJsonObject>* ErrorPtr = nullptr;
				if (PrimaryObject->TryGetObjectField(TEXT("error"), ErrorPtr) && ErrorPtr && ErrorPtr->IsValid())
				{
					const TSharedPtr<FJsonObject>& RawError = *ErrorPtr;
					RawError->TryGetStringField(TEXT("code"), ErrorCode);
					RawError->TryGetStringField(TEXT("message"), ErrorMessage);
					const TSharedPtr<FJsonObject>* DetailPtr = nullptr;
					if (RawError->TryGetObjectField(TEXT("details"), DetailPtr) && DetailPtr && DetailPtr->IsValid())
					{
						Details = MakeShared<FJsonObject>(**DetailPtr);
					}
				}

				if (Data.IsValid())
				{
					if (!Details.IsValid())
					{
						Details = MakeShared<FJsonObject>();
					}
					PromoteObjectReferenceFields(Data);
					Details->SetObjectField(TEXT("result"), Data.ToSharedRef());
				}
			}
		}

		if (!bTreatAsFailure)
		{
			if (!Data.IsValid())
			{
				Data = MakeShared<FJsonObject>();
				if (Result.Data.IsValid())
				{
					Data->SetField(TEXT("value"), Result.Data);
				}
			}

			PromoteObjectReferenceFields(Data);
			return MakeSuccessResponse(Result.StatusCode, RequestId, Data, Warnings);
		}

		return MakeErrorResponse(Result.StatusCode, RequestId, ErrorCode, ErrorMessage.IsEmpty() ? TEXT("Request failed") : ErrorMessage, Details, Warnings);
	}
}
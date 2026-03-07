#include "Transport/ErrorMapping.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
	static TSharedPtr<FJsonObject> JsonValueToObject(const TSharedPtr<FJsonValue>& Value)
	{
		return Value.IsValid() && Value->Type == EJson::Object ? Value->AsObject() : nullptr;
	}
}

namespace BAT::Transport
{
	FString NormalizeErrorCode(const FString& InCode)
	{
		FString Code = InCode.TrimStartAndEnd();
		if (Code.IsEmpty())
		{
			return TEXT("route_internal_error");
		}

		FString Out;
		Out.Reserve(Code.Len() + 8);
		for (int32 Index = 0; Index < Code.Len(); ++Index)
		{
			const TCHAR Char = Code[Index];
			if (Char == TEXT(' ') || Char == TEXT('-') || Char == TEXT('.'))
			{
				if (!Out.IsEmpty() && !Out.EndsWith(TEXT("_"), ESearchCase::CaseSensitive))
				{
					Out.AppendChar(TEXT('_'));
				}
				continue;
			}

			const bool bIsUpper = FChar::IsUpper(Char);
			const bool bHasPrev = Index > 0;
			const TCHAR Prev = bHasPrev ? Code[Index - 1] : 0;
			if (bIsUpper && bHasPrev && (FChar::IsLower(Prev) || FChar::IsDigit(Prev)) && !Out.EndsWith(TEXT("_"), ESearchCase::CaseSensitive))
			{
				Out.AppendChar(TEXT('_'));
			}

			Out.AppendChar(FChar::ToLower(Char));
		}

		while (Out.ReplaceInline(TEXT("__"), TEXT("_"), ESearchCase::CaseSensitive) > 0)
		{
		}

		return Out.TrimStartAndEnd();
	}

	FErrorDisposition DescribeError(const FString& InCode, int32 StatusCode)
	{
		FErrorDisposition Disposition;
		const FString Code = NormalizeErrorCode(InCode);

		if (Code == TEXT("pie_edit_blocked"))
		{
			Disposition.bRecoverable = true;
			Disposition.SuggestedAction = TEXT("stop_pie");
		}
		else if (Code == TEXT("game_thread_timeout"))
		{
			Disposition.bRecoverable = true;
			Disposition.SuggestedAction = TEXT("retry");
		}
		else if (Code == TEXT("request_too_large"))
		{
			Disposition.SuggestedAction = TEXT("reduce_payload");
		}
		else if (Code == TEXT("bad_json") || Code == TEXT("invalid_request") || Code.StartsWith(TEXT("missing_")) || Code == TEXT("schema_validation_failed") || Code == TEXT("invalid_arguments") || Code == TEXT("bad_args"))
		{
			Disposition.SuggestedAction = TEXT("fix_request");
		}
		else if (Code.Contains(TEXT("not_found")) || Code == TEXT("property_not_found") || Code == TEXT("function_not_found"))
		{
			Disposition.SuggestedAction = TEXT("inspect_target");
		}
		else if (Code == TEXT("safe_mode_denied") || Code == TEXT("forbidden_by_policy"))
		{
			Disposition.SuggestedAction = TEXT("inspect_policy");
		}
		else if (Code == TEXT("exec_route_disabled"))
		{
			Disposition.SuggestedAction = TEXT("enable_exec");
		}
		else if (Code == TEXT("python_disabled"))
		{
			Disposition.SuggestedAction = TEXT("enable_python");
		}
		else if (Code == TEXT("forbidden"))
		{
			Disposition.SuggestedAction = TEXT("grant_permission");
		}
		else if (Code.StartsWith(TEXT("auth_")) || Code == TEXT("unauthorized") || StatusCode == 401)
		{
			Disposition.bRecoverable = true;
			Disposition.SuggestedAction = TEXT("authenticate");
		}
		else if (StatusCode >= 500)
		{
			Disposition.bRecoverable = true;
			Disposition.SuggestedAction = TEXT("retry");
		}

		return Disposition;
	}

	TSharedRef<FJsonObject> MakeIssueObject(
		const FString& RawCode,
		const FString& Message,
		const TSharedPtr<FJsonObject>& Details,
		const TOptional<bool>& RecoverableOverride,
		const FString& Target,
		const FString& SuggestedActionOverride)
	{
		const FString Code = NormalizeErrorCode(RawCode.IsEmpty() ? TEXT("unknown") : RawCode);
		const FErrorDisposition Disposition = DescribeError(Code, 0);

		TSharedRef<FJsonObject> Issue = MakeShared<FJsonObject>();
		Issue->SetStringField(TEXT("code"), Code);
		Issue->SetStringField(TEXT("message"), Message.IsEmpty() ? Code : Message);
		Issue->SetBoolField(TEXT("recoverable"), RecoverableOverride.IsSet() ? RecoverableOverride.GetValue() : Disposition.bRecoverable);

		const FString EffectiveSuggestedAction = !SuggestedActionOverride.IsEmpty() ? SuggestedActionOverride : Disposition.SuggestedAction;
		if (!EffectiveSuggestedAction.IsEmpty())
		{
			Issue->SetStringField(TEXT("suggestedAction"), EffectiveSuggestedAction);
		}

		if (!Target.IsEmpty())
		{
			Issue->SetStringField(TEXT("target"), Target);
		}

		if (Details.IsValid())
		{
			Issue->SetObjectField(TEXT("details"), Details.ToSharedRef());
		}

		return Issue;
	}

	TArray<TSharedPtr<FJsonValue>> NormalizeIssueArray(const TArray<TSharedPtr<FJsonValue>>& RawIssues, const FString& DefaultCode)
	{
		TArray<TSharedPtr<FJsonValue>> Issues;
		for (const TSharedPtr<FJsonValue>& RawIssue : RawIssues)
		{
			if (!RawIssue.IsValid())
			{
				continue;
			}

			if (RawIssue->Type == EJson::Object)
			{
				const TSharedPtr<FJsonObject> RawObject = RawIssue->AsObject();
				if (!RawObject.IsValid())
				{
					continue;
				}

				FString Code;
				RawObject->TryGetStringField(TEXT("code"), Code);
				FString Message;
				RawObject->TryGetStringField(TEXT("message"), Message);

				FString Target;
				RawObject->TryGetStringField(TEXT("target"), Target);
				FString SuggestedAction;
				RawObject->TryGetStringField(TEXT("suggestedAction"), SuggestedAction);

				const TSharedPtr<FJsonObject>* DetailPtr = nullptr;
				TSharedPtr<FJsonObject> Details;
				if (RawObject->TryGetObjectField(TEXT("details"), DetailPtr) && DetailPtr && DetailPtr->IsValid())
				{
					Details = MakeShared<FJsonObject>(**DetailPtr);
				}

				bool bRecoverable = false;
				const TOptional<bool> RecoverableOverride = RawObject->TryGetBoolField(TEXT("recoverable"), bRecoverable) ? TOptional<bool>(bRecoverable) : TOptional<bool>();

				Issues.Add(MakeShared<FJsonValueObject>(MakeIssueObject(
					Code.IsEmpty() ? DefaultCode : Code,
					Message,
					Details,
					RecoverableOverride,
					Target,
					SuggestedAction)));
				continue;
			}

			const FString Message = RawIssue->Type == EJson::String ? RawIssue->AsString() : TEXT("Issue reported");
			Issues.Add(MakeShared<FJsonValueObject>(MakeIssueObject(DefaultCode, Message)));
		}

		return Issues;
	}
}
#include "BlueprintAutomationToolkitModule.h"

#include "Auth/TokenAuthMiddleware.h"
#include "Commands/AutomationCommand.h"
#include "Commands/CommandDispatcher.h"
#include "Async/Async.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Core/ForwardAxis.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "EngineUtils.h"
#include "GenericPlatform/GenericPlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HttpResultCallback.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "Http/HttpRequestUtils.h"
#include "Core/EditorExecution.h"
#include "Misc/Base64.h"
#include "Misc/DateTime.h"
#include "Misc/EngineVersion.h"
#include "Misc/Guid.h"
#include "Misc/SecureHash.h"
#include "Misc/ScopeLock.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"
#include "Serialization/JsonSerializer.h"
#include "FileHelpers.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Transport/PolicyMiddleware.h"
#include "Transport/ResponseWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogBlueprintAutomationToolkitAuth, Log, All);

namespace
{
	static bool IsJsonVerb(EHttpServerRequestVerbs Verb)
	{
		return Verb == EHttpServerRequestVerbs::VERB_POST;
	}
		static const TCHAR* ToPermissionTierString(EBATAutomationPermissionTier Tier)
		{
			switch (Tier)
			{
			case EBATAutomationPermissionTier::Read:
				return TEXT("read");
			case EBATAutomationPermissionTier::Edit:
				return TEXT("edit");
			case EBATAutomationPermissionTier::Admin:
				return TEXT("admin");
			default:
				return TEXT("read");
			}
		}


	static const TArray<FString>* FindHeaderCaseInsensitive(const TMap<FString, TArray<FString>>& Headers, const TCHAR* Name)
	{
		for (const TPair<FString, TArray<FString>>& Pair : Headers)
		{
			if (Pair.Key.Equals(Name, ESearchCase::IgnoreCase))
			{
				return &Pair.Value;
			}
		}
		return nullptr;
	}

	static FString ParseBearerToken(const FString& HeaderValue)
	{
		const FString Prefix(TEXT("Bearer "));
		if (!HeaderValue.StartsWith(Prefix, ESearchCase::CaseSensitive))
		{
			return FString();
		}
		FString Token = HeaderValue.RightChop(Prefix.Len());
		Token.TrimStartAndEndInline();
		return Token;
	}

	static FString NormalizeBlueprintObjectPath(const FString& InPath)
	{
		FString Path = InPath;
		Path.TrimStartAndEndInline();
		if (Path.IsEmpty())
		{
			return Path;
		}

		if (!Path.Contains(TEXT(".")))
		{
			const FString AssetName = FPackageName::GetLongPackageAssetName(Path);
			if (!AssetName.IsEmpty())
			{
				Path = FString::Printf(TEXT("%s.%s"), *Path, *AssetName);
			}
		}

		return Path;
	}

		static FString NormalizeCanonicalErrorCode(const FString& InCode)
		{
			FString Code = InCode.TrimStartAndEnd();
			if (Code.IsEmpty())
			{
				return TEXT("internal_error");
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

		static TSharedRef<FJsonObject> MakeIssueObject(const FString& RawCode, const FString& Message)
		{
			TSharedRef<FJsonObject> Issue = MakeShared<FJsonObject>();
			Issue->SetStringField(TEXT("code"), NormalizeCanonicalErrorCode(RawCode.IsEmpty() ? TEXT("unknown") : RawCode));
			Issue->SetStringField(TEXT("message"), Message);
			return Issue;
		}

		static TSharedPtr<FJsonObject> JsonValueToObject(const TSharedPtr<FJsonValue>& Value)
		{
			return Value.IsValid() && Value->Type == EJson::Object ? Value->AsObject() : nullptr;
		}

		static TArray<TSharedPtr<FJsonValue>> NormalizeIssueArray(const TArray<TSharedPtr<FJsonValue>>& RawIssues, const FString& DefaultCode)
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

					TSharedRef<FJsonObject> Normalized = MakeShared<FJsonObject>(*RawObject);
					FString Code;
					if (!Normalized->TryGetStringField(TEXT("code"), Code) || Code.IsEmpty())
					{
						Code = DefaultCode;
					}
					Normalized->SetStringField(TEXT("code"), NormalizeCanonicalErrorCode(Code));

					FString Message;
					if (!Normalized->TryGetStringField(TEXT("message"), Message) || Message.IsEmpty())
					{
						Message = Code;
						Normalized->SetStringField(TEXT("message"), Message);
					}

					Issues.Add(MakeShared<FJsonValueObject>(Normalized));
					continue;
				}

				const FString Message = RawIssue->Type == EJson::String ? RawIssue->AsString() : TEXT("Issue reported");
				Issues.Add(MakeShared<FJsonValueObject>(MakeIssueObject(DefaultCode, Message)));
			}

			return Issues;
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

		static void DescribeCanonicalError(const FString& InCode, int32 StatusCode, bool& OutRetryable, FString& OutSuggestedAction)
		{
			const FString Code = NormalizeCanonicalErrorCode(InCode);
			OutRetryable = false;
			OutSuggestedAction.Reset();

			if (Code == TEXT("pie_edit_blocked"))
			{
				OutRetryable = true;
				OutSuggestedAction = TEXT("stop_pie");
			}
			else if (Code == TEXT("game_thread_timeout"))
			{
				OutRetryable = true;
				OutSuggestedAction = TEXT("retry");
			}
			else if (Code == TEXT("request_too_large"))
			{
				OutSuggestedAction = TEXT("reduce_payload");
			}
			else if (Code == TEXT("bad_json") || Code == TEXT("invalid_request") || Code.StartsWith(TEXT("missing_")) || Code == TEXT("schema_validation_failed") || Code == TEXT("invalid_arguments"))
			{
				OutSuggestedAction = TEXT("fix_request");
			}
			else if (Code.Contains(TEXT("not_found")) || Code == TEXT("object_not_found") || Code == TEXT("property_not_found") || Code == TEXT("function_not_found"))
			{
				OutSuggestedAction = TEXT("inspect_target");
			}
			else if (Code == TEXT("safe_mode_denied"))
			{
				OutSuggestedAction = TEXT("inspect_policy");
			}
			else if (Code == TEXT("exec_route_disabled"))
			{
				OutSuggestedAction = TEXT("enable_exec");
			}
			else if (Code == TEXT("python_disabled"))
			{
				OutSuggestedAction = TEXT("enable_python");
			}
			else if (Code == TEXT("forbidden"))
			{
				OutSuggestedAction = TEXT("grant_permission");
			}
			else if (Code.StartsWith(TEXT("auth_")) || StatusCode == 401)
			{
				OutRetryable = true;
				OutSuggestedAction = TEXT("authenticate");
			}
			else if (StatusCode >= 500)
			{
				OutRetryable = true;
				OutSuggestedAction = TEXT("retry");
			}
		}
	static bool TryLoadBlueprintByPath(const FString& InPath, UBlueprint*& OutBlueprint, FString& OutObjectPath)
	{
		OutBlueprint = nullptr;
		OutObjectPath = NormalizeBlueprintObjectPath(InPath);
		if (OutObjectPath.IsEmpty())
		{
			return false;
		}

		OutBlueprint = LoadObject<UBlueprint>(nullptr, *OutObjectPath);
		return OutBlueprint != nullptr;
	}
}

bool FBlueprintAutomationToolkitModule::IsRequestAllowed(const FHttpServerRequest& Request, FString* OutDenyReason, FString* OutClientKey) const
{
	if (TokenAuthMiddleware == nullptr)
	{
		if (OutDenyReason)
		{
			*OutDenyReason = TEXT("auth_middleware_unavailable");
		}
		return false;
	}

	return TokenAuthMiddleware->Authorize(*this, Request, OutDenyReason, OutClientKey);
}

FString FBlueprintAutomationToolkitModule::ReadHeaderValueCaseInsensitive(const TMap<FString, TArray<FString>>& Headers, const TCHAR* Name) const
{
	if (const TArray<FString>* Values = FindHeaderCaseInsensitive(Headers, Name))
	{
		if (Values->Num() > 0)
		{
			return (*Values)[0];
		}
	}
	return FString();
}

FString FBlueprintAutomationToolkitModule::ResolveOrCreateRequestId(const FHttpServerRequest& Request) const
{
	FString Id = ReadHeaderValueCaseInsensitive(Request.Headers, TEXT("x-request-id"));
	Id.TrimStartAndEndInline();
	if (Id.IsEmpty())
	{
		Id = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
	}
	return Id;
}

void FBlueprintAutomationToolkitModule::FillAuthFailure(const FString& Code, FString& OutMessage, TSharedPtr<FJsonObject>& OutDetails, FString& OutWwwAuthenticate) const
{
	OutMessage = TEXT("Request denied by auth policy");
	OutDetails = MakeShared<FJsonObject>();
	OutDetails->SetStringField(TEXT("header"), TEXT("Authorization"));
	OutDetails->SetStringField(TEXT("scheme"), TEXT("Bearer"));
	OutDetails->SetStringField(TEXT("format"), TEXT("Bearer <token>"));
	OutDetails->SetBoolField(TEXT("tokenConfigured"), !RuntimeAuthToken.IsEmpty());
	OutDetails->SetBoolField(TEXT("projectTokenConfigured"), bProjectConfigTokenAvailable);
	OutDetails->SetStringField(TEXT("tokenSource"), bAuthTokenFromEnv ? TEXT("environment") : (bProjectConfigTokenAvailable ? TEXT("project_config") : (!RuntimeAuthToken.IsEmpty() ? TEXT("runtime") : TEXT("none"))));
	if (bProjectConfigTokenAvailable)
	{
		OutDetails->SetStringField(TEXT("tokenHint"), TEXT("Use the token already configured in project config, copy it from the Blueprint Automation Toolkit panel, or provide it explicitly via your client configuration."));
	}
	else
	{
		OutDetails->SetStringField(TEXT("tokenHint"), TEXT("Copy the token from the Blueprint Automation Toolkit panel or provide a token explicitly via your client configuration."));
	}

	FString ChallengeError = TEXT("invalid_request");
	FString ChallengeDescription = TEXT("Authorization: Bearer <token> is required.");

	if (Code.Equals(TEXT("auth_missing"), ESearchCase::CaseSensitive))
	{
		OutMessage = TEXT("Missing Authorization header. Send Authorization: Bearer <token>.");
		OutDetails->SetStringField(TEXT("reason"), TEXT("missing_authorization_header"));
	}
	else if (Code.Equals(TEXT("auth_invalid_format"), ESearchCase::CaseSensitive))
	{
		OutMessage = TEXT("Invalid Authorization header format. Expected Authorization: Bearer <token>.");
		OutDetails->SetStringField(TEXT("reason"), TEXT("invalid_authorization_header_format"));
		ChallengeDescription = TEXT("Authorization header must use Bearer <token>.");
	}
	else if (Code.Equals(TEXT("auth_invalid"), ESearchCase::CaseSensitive))
	{
		OutMessage = TEXT("Bearer token was not recognized. Copy the current token from the Blueprint Automation Toolkit panel and retry.");
		OutDetails->SetStringField(TEXT("reason"), TEXT("token_not_recognized"));
		ChallengeError = TEXT("invalid_token");
		ChallengeDescription = TEXT("Bearer token was not recognized.");
	}
	else if (Code.Equals(TEXT("auth_expired"), ESearchCase::CaseSensitive))
	{
		OutMessage = TEXT("Bearer token has expired. Generate or copy a fresh token from the Blueprint Automation Toolkit panel and retry.");
		OutDetails->SetStringField(TEXT("reason"), TEXT("token_expired"));
		ChallengeError = TEXT("invalid_token");
		ChallengeDescription = TEXT("Bearer token has expired.");
	}
	else if (Code.StartsWith(TEXT("auth_signature_"), ESearchCase::CaseSensitive))
	{
		OutMessage = TEXT("Signed token request is missing or invalid. Check x-timestamp/x-signature headers and retry.");
		OutDetails->SetStringField(TEXT("reason"), TEXT("signature_validation_failed"));
		ChallengeError = TEXT("invalid_token");
		ChallengeDescription = TEXT("Signed token request is missing or invalid.");
	}
	else
	{
		OutDetails->SetStringField(TEXT("reason"), TEXT("auth_denied"));
	}

	OutWwwAuthenticate = FString::Printf(TEXT("Bearer realm=\"BlueprintAutomationToolkit\", error=\"%s\", error_description=\"%s\""), *ChallengeError, *ChallengeDescription);
}

TUniquePtr<FHttpServerResponse> FBlueprintAutomationToolkitModule::MakeAuthFailureResponse(const FString& RequestId, const FString& Code) const
{
	FString Message;
	TSharedPtr<FJsonObject> Details;
	FString WwwAuthenticate;
	FillAuthFailure(Code, Message, Details, WwwAuthenticate);

	TUniquePtr<FHttpServerResponse> Response = MakeErrorResponse(401, RequestId, Code, Message, Details);
	Response->Headers.FindOrAdd(TEXT("WWW-Authenticate")).Add(WwwAuthenticate);
	return Response;
}

TUniquePtr<FHttpServerResponse> FBlueprintAutomationToolkitModule::MakeErrorResponse(int32 HttpCode, const FString& RequestId, const FString& Code, const FString& Message, const TSharedPtr<FJsonObject>& Details) const
{
	return BAT::Transport::MakeErrorResponse(HttpCode, RequestId, Code, Message, Details);
}

TUniquePtr<FHttpServerResponse> FBlueprintAutomationToolkitModule::MakeErrorResponse(EHttpServerResponseCodes HttpCode, const FString& RequestId, const FString& Code, const FString& Message, const TSharedPtr<FJsonObject>& Details) const
{
	return MakeErrorResponse((int32)HttpCode, RequestId, Code, Message, Details);
}

TUniquePtr<FHttpServerResponse> FBlueprintAutomationToolkitModule::MakeErrorResponse(int32 HttpCode, const FString& Code, const FString& Message, const TSharedPtr<FJsonObject>& Details) const
{
	const FString RequestId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
	return MakeErrorResponse(HttpCode, RequestId, Code, Message, Details);
}

TUniquePtr<FHttpServerResponse> FBlueprintAutomationToolkitModule::MakeErrorResponse(EHttpServerResponseCodes HttpCode, const FString& Code, const FString& Message, const TSharedPtr<FJsonObject>& Details) const
{
	return MakeErrorResponse((int32)HttpCode, Code, Message, Details);
}

TUniquePtr<FHttpServerResponse> FBlueprintAutomationToolkitModule::MakeCanonicalSuccessResponse(int32 HttpCode, const FString& RequestId, const TSharedPtr<FJsonObject>& Data, const TArray<TSharedPtr<FJsonValue>>& Warnings) const
{
	return BAT::Transport::MakeSuccessResponse(HttpCode, RequestId, Data, Warnings);
}

TUniquePtr<FHttpServerResponse> FBlueprintAutomationToolkitModule::MakeCanonicalErrorResponse(int32 HttpCode, const FString& RequestId, const FString& Code, const FString& Message, const TSharedPtr<FJsonObject>& Details, const TArray<TSharedPtr<FJsonValue>>& Warnings, const FString& SuggestedAction, const TOptional<bool>& RetryableOverride) const
{
	return BAT::Transport::MakeErrorResponse(HttpCode, RequestId, Code, Message, Details, Warnings, SuggestedAction, RetryableOverride);
}

TUniquePtr<FHttpServerResponse> FBlueprintAutomationToolkitModule::MakeCanonicalResponseFromAutomationResult(const FAutomationResult& Result, const FString& RequestId) const
{
	return BAT::Transport::MakeResponseFromAutomationResult(Result, RequestId);
}

TSharedPtr<FJsonObject> FBlueprintAutomationToolkitModule::NormalizeCanonicalObjectRequest(const TSharedPtr<FJsonObject>& BodyObj) const
{
	if (!BodyObj.IsValid())
	{
		return BodyObj;
	}

	TSharedPtr<FJsonObject> Normalized = MakeShared<FJsonObject>(*BodyObj);

	FString Path;
	if (Normalized->TryGetStringField(TEXT("path"), Path) && !Path.TrimStartAndEnd().IsEmpty() && !Normalized->HasField(TEXT("objectPath")))
	{
		Normalized->SetStringField(TEXT("objectPath"), Path);
	}

	FString World;
	if (Normalized->TryGetStringField(TEXT("world"), World) && !World.TrimStartAndEnd().IsEmpty() && !Normalized->HasField(TEXT("worldContext")))
	{
		Normalized->SetStringField(TEXT("worldContext"), World);
	}

	double PieIndex = 0.0;
	if (Normalized->TryGetNumberField(TEXT("pie_index"), PieIndex) && !Normalized->HasField(TEXT("pieIndex")))
	{
		Normalized->SetNumberField(TEXT("pieIndex"), PieIndex);
	}

	const TSharedPtr<FJsonObject>* ArgsPtr = nullptr;
	if (!Normalized->HasField(TEXT("arguments")) && Normalized->TryGetObjectField(TEXT("args"), ArgsPtr) && ArgsPtr && ArgsPtr->IsValid())
	{
		Normalized->SetObjectField(TEXT("arguments"), (*ArgsPtr).ToSharedRef());
	}

	FString Target;
	if (Normalized->TryGetStringField(TEXT("target"), Target)
		&& !Target.TrimStartAndEnd().IsEmpty()
		&& !Normalized->HasField(TEXT("objectPath"))
		&& !Normalized->HasField(TEXT("actorName"))
		&& !Normalized->HasField(TEXT("blueprintAssetPath"))
		&& !Normalized->HasField(TEXT("classPath")))
	{
		Target.TrimStartAndEndInline();
		if (Target.StartsWith(TEXT("/Script/"), ESearchCase::CaseSensitive))
		{
			Normalized->SetStringField(TEXT("classPath"), Target);
		}
		else if (Target.StartsWith(TEXT("/"), ESearchCase::CaseSensitive) || Target.Contains(TEXT(".")))
		{
			Normalized->SetStringField(TEXT("objectPath"), Target);
		}
		else
		{
			Normalized->SetStringField(TEXT("actorName"), Target);
		}
	}

	return Normalized;
}

uint32 FBlueprintAutomationToolkitModule::ToInternalPermissionMask(EBATAutomationPermission Permissions) const
{
	uint32 Mask = 0u;
	if (EnumHasAnyFlags(Permissions, EBATAutomationPermission::Editor))
	{
		Mask |= static_cast<uint32>(EBATPermission::Editor);
	}
	if (EnumHasAnyFlags(Permissions, EBATAutomationPermission::Blueprint))
	{
		Mask |= static_cast<uint32>(EBATPermission::Blueprint);
	}
	if (EnumHasAnyFlags(Permissions, EBATAutomationPermission::Pie))
	{
		Mask |= static_cast<uint32>(EBATPermission::Pie);
	}
	if (EnumHasAnyFlags(Permissions, EBATAutomationPermission::Exec))
	{
		Mask |= static_cast<uint32>(EBATPermission::Exec);
	}
	if (EnumHasAnyFlags(Permissions, EBATAutomationPermission::Python))
	{
		Mask |= static_cast<uint32>(EBATPermission::Python);
	}
	if (EnumHasAnyFlags(Permissions, EBATAutomationPermission::Filesystem))
	{
		Mask |= static_cast<uint32>(EBATPermission::Filesystem);
	}
	return Mask;
}

TArray<FString> FBlueprintAutomationToolkitModule::DescribePermissionMask(uint32 PermissionMask) const
{
	TArray<FString> Permissions;
	if ((PermissionMask & static_cast<uint32>(EBATPermission::Editor)) != 0u)
	{
		Permissions.Add(TEXT("editor"));
	}
	if ((PermissionMask & static_cast<uint32>(EBATPermission::Blueprint)) != 0u)
	{
		Permissions.Add(TEXT("blueprint"));
	}
	if ((PermissionMask & static_cast<uint32>(EBATPermission::Pie)) != 0u)
	{
		Permissions.Add(TEXT("pie"));
	}
	if ((PermissionMask & static_cast<uint32>(EBATPermission::Exec)) != 0u)
	{
		Permissions.Add(TEXT("exec"));
	}
	if ((PermissionMask & static_cast<uint32>(EBATPermission::Python)) != 0u)
	{
		Permissions.Add(TEXT("python"));
	}
	if ((PermissionMask & static_cast<uint32>(EBATPermission::Filesystem)) != 0u)
	{
		Permissions.Add(TEXT("filesystem"));
	}
	return Permissions;
}

TSharedPtr<FJsonObject> FBlueprintAutomationToolkitModule::BuildCapabilitiesSummary() const
{
	const bool bPythonEnabled = bEnableExecRoute && bAllowPythonExec && !bSafeModeEnabled;

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("protocolVersion"), TEXT("1.0"));
	Data->SetBoolField(TEXT("safeMode"), bSafeModeEnabled);
	Data->SetBoolField(TEXT("pieRunning"), IsPieSessionRunning());
	Data->SetBoolField(TEXT("execEnabled"), bEnableExecRoute);
	Data->SetBoolField(TEXT("pythonEnabled"), bPythonEnabled);
	Data->SetBoolField(TEXT("localhostOnly"), true);
	Data->SetBoolField(TEXT("tokenAuthRequired"), bRequireAuthToken);

	TSharedRef<FJsonObject> Capabilities = MakeShared<FJsonObject>();
	Capabilities->SetBoolField(TEXT("reflection"), true);
	Capabilities->SetBoolField(TEXT("objectDescribe"), true);
	Capabilities->SetBoolField(TEXT("objectGet"), true);
	Capabilities->SetBoolField(TEXT("objectGetProperty"), true);
	Capabilities->SetBoolField(TEXT("objectSetProperty"), true);
	Capabilities->SetBoolField(TEXT("objectCallFunction"), true);
	Capabilities->SetBoolField(TEXT("actorSpawn"), true);
	Capabilities->SetBoolField(TEXT("actorDestroy"), true);
	Capabilities->SetBoolField(TEXT("editorSelect"), true);
	Capabilities->SetBoolField(TEXT("editorFocus"), true);
	Capabilities->SetBoolField(TEXT("pieControl"), true);
	Capabilities->SetBoolField(TEXT("blueprintGraphRead"), true);
	Capabilities->SetBoolField(TEXT("blueprintGraphApply"), true);
	Capabilities->SetBoolField(TEXT("compileSaveBlueprint"), true);
	Capabilities->SetBoolField(TEXT("saveAsset"), true);
	Capabilities->SetBoolField(TEXT("exec"), bEnableExecRoute);
	Capabilities->SetBoolField(TEXT("python"), bPythonEnabled);
	Capabilities->SetBoolField(TEXT("extensibleCommands"), true);
	Data->SetObjectField(TEXT("capabilities"), Capabilities);

	TSharedRef<FJsonObject> Limits = MakeShared<FJsonObject>();
	Limits->SetNumberField(TEXT("bodySizeBytes"), MaxRequestBodyBytes);
	Limits->SetNumberField(TEXT("rateLimitPerSecond"), RateLimitPerSecond);
	Limits->SetNumberField(TEXT("rateLimitBurst"), RateLimitBurst);
	Limits->SetNumberField(TEXT("maxOpsPerPlan"), MaxOpsPerPlan);
	Limits->SetNumberField(TEXT("maxActorsPerLayout"), MaxActorsPerLayout);
	Limits->SetNumberField(TEXT("maxInstancesPerOp"), MaxInstancesPerOp);
	Limits->SetNumberField(TEXT("maxTotalInstancesPerPlan"), MaxTotalInstancesPerPlan);
	Data->SetObjectField(TEXT("limits"), Limits);

	TSharedRef<FJsonObject> Permissions = MakeShared<FJsonObject>();
	Permissions->SetBoolField(TEXT("editor"), bPermissionEditor);
	Permissions->SetBoolField(TEXT("blueprint"), bPermissionBlueprint);
	Permissions->SetBoolField(TEXT("pie"), bPermissionPie);
	Permissions->SetBoolField(TEXT("exec"), bPermissionExec);
	Permissions->SetBoolField(TEXT("python"), bPermissionPython);
	Permissions->SetBoolField(TEXT("filesystem"), bPermissionFilesystem);
	Data->SetObjectField(TEXT("permissions"), Permissions);

	TArray<TSharedPtr<FJsonValue>> Routes;
	Routes.Add(MakeShared<FJsonValueString>(TEXT("/engine/discover")));
	Routes.Add(MakeShared<FJsonValueString>(TEXT("/health")));
	Routes.Add(MakeShared<FJsonValueString>(TEXT("/object/resolve")));
	Routes.Add(MakeShared<FJsonValueString>(TEXT("/object/describe")));
	Routes.Add(MakeShared<FJsonValueString>(TEXT("/object/get_property")));
	Routes.Add(MakeShared<FJsonValueString>(TEXT("/object/set_property")));
	Routes.Add(MakeShared<FJsonValueString>(TEXT("/object/call_function")));
	Routes.Add(MakeShared<FJsonValueString>(TEXT("/actor/spawn")));
	Routes.Add(MakeShared<FJsonValueString>(TEXT("/actor/destroy")));
	Routes.Add(MakeShared<FJsonValueString>(TEXT("/editor/select")));
	Routes.Add(MakeShared<FJsonValueString>(TEXT("/editor/focus")));
	Routes.Add(MakeShared<FJsonValueString>(TEXT("/pie/start")));
	Routes.Add(MakeShared<FJsonValueString>(TEXT("/pie/stop")));
	Routes.Add(MakeShared<FJsonValueString>(TEXT("/blueprint/graph/read")));
	Routes.Add(MakeShared<FJsonValueString>(TEXT("/blueprint/graph/apply")));
	Routes.Add(MakeShared<FJsonValueString>(TEXT("/blueprint/compile_save")));
	Data->SetArrayField(TEXT("canonicalRoutes"), Routes);

	TArray<FBATAutomationCommandInfo> CommandInfos;
	GetAutomationCommandInfos(CommandInfos);

	TArray<TSharedPtr<FJsonValue>> ExtensionRoutes;
	TArray<TSharedPtr<FJsonValue>> RegisteredCommands;
	for (const FBATAutomationCommandInfo& Info : CommandInfos)
	{
		TSharedRef<FJsonObject> Command = MakeShared<FJsonObject>();
		Command->SetStringField(TEXT("endpoint"), Info.Endpoint);
		Command->SetStringField(TEXT("permissionTier"), ToPermissionTierString(Info.PermissionTier));
		Command->SetBoolField(TEXT("bindRoute"), Info.bBindRoute);
		Command->SetBoolField(TEXT("blockDuringPie"), Info.bBlockDuringPie);
		Command->SetBoolField(TEXT("builtIn"), Info.bBuiltIn);

		TArray<TSharedPtr<FJsonValue>> PermissionValues;
		for (const FString& Permission : DescribePermissionMask(ToInternalPermissionMask(Info.RequiredPermissions)))
		{
			PermissionValues.Add(MakeShared<FJsonValueString>(Permission));
		}
		Command->SetArrayField(TEXT("requiredPermissions"), PermissionValues);
		RegisteredCommands.Add(MakeShared<FJsonValueObject>(Command));

		if (!Info.bBuiltIn && Info.bBindRoute)
		{
			ExtensionRoutes.Add(MakeShared<FJsonValueString>(Info.Endpoint));
		}
	}

	Data->SetArrayField(TEXT("registeredCommands"), RegisteredCommands);
	if (ExtensionRoutes.Num() > 0)
	{
		Data->SetArrayField(TEXT("extensionRoutes"), ExtensionRoutes);
	}

	return Data;
}

TSharedPtr<FJsonObject> FBlueprintAutomationToolkitModule::BuildEngineDiscoverPayload() const
{
	const bool bPythonEnabled = bEnableExecRoute && bAllowPythonExec && !bSafeModeEnabled;
	TSharedPtr<FJsonObject> Data = BuildCapabilitiesSummary();
	Data->SetStringField(TEXT("pluginName"), TEXT("Blueprint Automation Toolkit"));
	Data->SetStringField(TEXT("moduleName"), TEXT("BlueprintAutomationToolkit"));
	Data->SetStringField(TEXT("engineVersion"), FEngineVersion::Current().ToString());

	TSharedRef<FJsonObject> PreferredRoutes = MakeShared<FJsonObject>();
	PreferredRoutes->SetStringField(TEXT("resolveObject"), TEXT("/object/resolve"));
	PreferredRoutes->SetStringField(TEXT("describeObject"), TEXT("/object/describe"));
	PreferredRoutes->SetStringField(TEXT("getProperty"), TEXT("/object/get_property"));
	PreferredRoutes->SetStringField(TEXT("setProperty"), TEXT("/object/set_property"));
	PreferredRoutes->SetStringField(TEXT("callFunction"), TEXT("/object/call_function"));
	PreferredRoutes->SetStringField(TEXT("spawnActor"), TEXT("/actor/spawn"));
	PreferredRoutes->SetStringField(TEXT("destroyActor"), TEXT("/actor/destroy"));
	PreferredRoutes->SetStringField(TEXT("editorSelect"), TEXT("/editor/select"));
	PreferredRoutes->SetStringField(TEXT("editorFocus"), TEXT("/editor/focus"));
	PreferredRoutes->SetStringField(TEXT("pieStart"), TEXT("/pie/start"));
	PreferredRoutes->SetStringField(TEXT("pieStop"), TEXT("/pie/stop"));
	PreferredRoutes->SetStringField(TEXT("readGraph"), TEXT("/blueprint/graph/read"));
	PreferredRoutes->SetStringField(TEXT("applyGraph"), TEXT("/blueprint/graph/apply"));
	PreferredRoutes->SetStringField(TEXT("compileSaveBlueprint"), TEXT("/blueprint/compile_save"));
	Data->SetObjectField(TEXT("preferredRoutes"), PreferredRoutes);

	TSharedPtr<FJsonObject> Capabilities;
	if (const TSharedPtr<FJsonObject>* CapabilitiesPtr = nullptr; Data->TryGetObjectField(TEXT("capabilities"), CapabilitiesPtr) && CapabilitiesPtr && CapabilitiesPtr->IsValid())
	{
		Capabilities = *CapabilitiesPtr;
		Capabilities->SetBoolField(TEXT("exec"), bEnableExecRoute);
		Capabilities->SetBoolField(TEXT("python"), bPythonEnabled);
	}

	Data->SetBoolField(TEXT("blueprintEditsDuringPie"), false);
	Data->SetBoolField(TEXT("execEnabled"), bEnableExecRoute);
	Data->SetBoolField(TEXT("pythonEnabled"), bPythonEnabled);
	return Data;
}

bool FBlueprintAutomationToolkitModule::ConsumeRateLimitToken(const FString& ClientKey)
{
	if (RateLimitPerSecond <= 0 || RateLimitBurst <= 0)
	{
		return true;
	}

	const double NowSeconds = FPlatformTime::Seconds();
	FScopeLock Lock(&SecurityStateMutex);
	FRateLimitState& State = RateLimitStates.FindOrAdd(ClientKey);
	if (State.LastUpdatedSeconds <= 0.0)
	{
		State.Tokens = (double)RateLimitBurst;
		State.LastUpdatedSeconds = NowSeconds;
	}

	const double Elapsed = FMath::Max(0.0, NowSeconds - State.LastUpdatedSeconds);
	State.LastUpdatedSeconds = NowSeconds;
	State.Tokens = FMath::Min((double)RateLimitBurst, State.Tokens + Elapsed * (double)RateLimitPerSecond);

	if (State.Tokens < 1.0)
	{
		return false;
	}

	State.Tokens -= 1.0;
	return true;
}

void FBlueprintAutomationToolkitModule::RecordRequestStat(const FString& Endpoint, bool bAllowed, const FString& Reason)
{
	FRequestStatEntry Entry;
	Entry.Endpoint = Endpoint;
	Entry.Timestamp = FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M:%S"));
	Entry.bAllowed = bAllowed;
	Entry.Reason = Reason;

	FScopeLock Lock(&SecurityStateMutex);
	RequestStats.Add(MoveTemp(Entry));
	if (RequestStats.Num() > 20)
	{
		RequestStats.RemoveAt(0, RequestStats.Num() - 20, EAllowShrinking::No);
	}
}

FString FBlueprintAutomationToolkitModule::BuildRequestStatsText() const
{
	FScopeLock Lock(&SecurityStateMutex);
	if (RequestStats.Num() == 0)
	{
		return TEXT("No requests yet.");
	}

	FString Out;
	for (int32 i = RequestStats.Num() - 1; i >= 0; --i)
	{
		const FRequestStatEntry& Entry = RequestStats[i];
		Out += FString::Printf(TEXT("[%s] %s | %s | %s\n"), *Entry.Timestamp, *Entry.Endpoint, Entry.bAllowed ? TEXT("allowed") : TEXT("denied"), *Entry.Reason);
	}
	return Out;
}

bool FBlueprintAutomationToolkitModule::ValidateAndHandleRequest(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete, const TCHAR* Endpoint)
{
	return BAT::Transport::ValidateAndHandleRequest(*this, Request, OnComplete, Endpoint);
}

bool FBlueprintAutomationToolkitModule::IsEditorAssetMutationBlockedDuringPie(const FString& Endpoint) const
{
	if (Endpoint.Equals(TEXT("/asset/duplicate"), ESearchCase::CaseSensitive)
		|| Endpoint.Equals(TEXT("/asset/create"), ESearchCase::CaseSensitive)
		|| Endpoint.Equals(TEXT("/asset/delete"), ESearchCase::CaseSensitive)
		|| Endpoint.Equals(TEXT("/asset/save"), ESearchCase::CaseSensitive)
		|| Endpoint.Equals(TEXT("/object/set_property"), ESearchCase::CaseSensitive)
		|| Endpoint.Equals(TEXT("/actor/destroy"), ESearchCase::CaseSensitive)
		|| Endpoint.Equals(TEXT("/blueprint/create"), ESearchCase::CaseSensitive)
		|| Endpoint.Equals(TEXT("/blueprint/apply"), ESearchCase::CaseSensitive)
		|| Endpoint.Equals(TEXT("/blueprint/set-defaults"), ESearchCase::CaseSensitive)
		|| Endpoint.Equals(TEXT("/blueprint/graph/apply"), ESearchCase::CaseSensitive)
		|| Endpoint.Equals(TEXT("/blueprint/compile_save"), ESearchCase::CaseSensitive)
		|| Endpoint.Equals(TEXT("/blueprint/components/remove"), ESearchCase::CaseSensitive)
		|| Endpoint.Equals(TEXT("/blueprint/components/replace"), ESearchCase::CaseSensitive)
		|| Endpoint.Equals(TEXT("/blueprint/node/delete"), ESearchCase::CaseSensitive))
	{
		return true;
	}

	if (Endpoint.StartsWith(TEXT("/blueprint/node/add-"), ESearchCase::CaseSensitive)
		|| Endpoint.StartsWith(TEXT("/blueprint/pin/"), ESearchCase::CaseSensitive))
	{
		return true;
	}

	if (CommandDispatcher)
	{
		FCommandDispatcher::FRegistration Registration;
		if (CommandDispatcher->TryGetRegistration(Endpoint, Registration))
		{
			return Registration.bBlockDuringPie;
		}
	}

	return false;
}

bool FBlueprintAutomationToolkitModule::IsPieSessionRunning() const
{
	return GEditor && GEditor->PlayWorld != nullptr;
}

uint32 FBlueprintAutomationToolkitModule::GetRequestRequiredPermissions(const FString& Endpoint, const TSharedPtr<FJsonObject>& BodyObj) const
{
	uint32 RequiredPermissions = GetRouteRequiredPermissions(Endpoint);
	if (BAT::Http::HasResponseOutputPath(BodyObj))
	{
		RequiredPermissions |= static_cast<uint32>(EBATPermission::Filesystem);
	}

	if ((Endpoint.Equals(TEXT("/ai/exec"), ESearchCase::CaseSensitive) || Endpoint.Equals(TEXT("/exec"), ESearchCase::CaseSensitive)) && BodyObj.IsValid())
	{
		FString PythonCode;
		BodyObj->TryGetStringField(TEXT("python"), PythonCode);
		PythonCode.TrimStartAndEndInline();
		if (!PythonCode.IsEmpty())
		{
			RequiredPermissions |= static_cast<uint32>(EBATPermission::Python);
		}
	}

	return RequiredPermissions;
}

uint32 FBlueprintAutomationToolkitModule::GetRouteRequiredPermissions(const FString& Endpoint) const
{
	auto PM = [](EBATPermission P) { return static_cast<uint32>(P); };
	auto PM2 = [&](EBATPermission A, EBATPermission B) { return PM(A) | PM(B); };

	if (Endpoint.StartsWith(TEXT("/blueprint/")))
	{
		if (Endpoint.Contains(TEXT("/save")) || Endpoint.EndsWith(TEXT("_save"), ESearchCase::CaseSensitive))
		{
			return PM2(EBATPermission::Blueprint, EBATPermission::Filesystem);
		}
		return PM(EBATPermission::Blueprint);
	}
	if (Endpoint.Equals(TEXT("/object/resolve"), ESearchCase::CaseSensitive)
		|| Endpoint.Equals(TEXT("/object/describe"), ESearchCase::CaseSensitive)
		|| Endpoint.Equals(TEXT("/object/get_property"), ESearchCase::CaseSensitive)
		|| Endpoint.Equals(TEXT("/object/set_property"), ESearchCase::CaseSensitive)
		|| Endpoint.Equals(TEXT("/object/call_function"), ESearchCase::CaseSensitive)
		|| Endpoint.Equals(TEXT("/editor/select"), ESearchCase::CaseSensitive)
		|| Endpoint.Equals(TEXT("/editor/focus"), ESearchCase::CaseSensitive))
	{
		return PM(EBATPermission::Editor);
	}
	if (Endpoint.Equals(TEXT("/actor/spawn"), ESearchCase::CaseSensitive)
		|| Endpoint.Equals(TEXT("/actor/destroy"), ESearchCase::CaseSensitive)
		|| Endpoint.Equals(TEXT("/actor/find"), ESearchCase::CaseSensitive))
	{
		return PM(EBATPermission::Editor);
	}
	if (Endpoint.Equals(TEXT("/blueprint/compile_save"), ESearchCase::CaseSensitive))
	{
		return PM2(EBATPermission::Blueprint, EBATPermission::Filesystem);
	}
	if (Endpoint.Equals(TEXT("/asset/duplicate"), ESearchCase::CaseSensitive)
		|| Endpoint.Equals(TEXT("/asset/create"), ESearchCase::CaseSensitive)
		|| Endpoint.Equals(TEXT("/asset/delete"), ESearchCase::CaseSensitive)
		|| Endpoint.Equals(TEXT("/asset/save"), ESearchCase::CaseSensitive)
		|| Endpoint.Equals(TEXT("/pcg/spawn_spheres"), ESearchCase::CaseSensitive))
	{
		return PM2(EBATPermission::Editor, EBATPermission::Filesystem);
	}
	if (Endpoint.Equals(TEXT("/actions/list"), ESearchCase::CaseSensitive)
		|| Endpoint.Equals(TEXT("/actions/run"), ESearchCase::CaseSensitive))
	{
		return PM(EBATPermission::Editor);
	}
	if (Endpoint.StartsWith(TEXT("/pie/")))
	{
		return PM(EBATPermission::Pie);
	}
	if (Endpoint.Equals(TEXT("/ai/exec"), ESearchCase::CaseSensitive) || Endpoint.Equals(TEXT("/exec"), ESearchCase::CaseSensitive))
	{
		return PM(EBATPermission::Exec);
	}
	if (Endpoint.StartsWith(TEXT("/ai/editor/")) || Endpoint.StartsWith(TEXT("/ai/plan/")))
	{
		return PM(EBATPermission::Editor);
	}
	if (Endpoint.StartsWith(TEXT("/jobs")) || Endpoint.Equals(TEXT("/openapi"), ESearchCase::CaseSensitive) || Endpoint.StartsWith(TEXT("/logs/tail")) || Endpoint.Equals(TEXT("/engine/discover"), ESearchCase::CaseSensitive))
	{
		return PM(EBATPermission::Editor);
	}
	if (CommandDispatcher)
	{
		FCommandDispatcher::FRegistration Registration;
		if (CommandDispatcher->TryGetRegistration(Endpoint, Registration))
		{
			return ToInternalPermissionMask(Registration.RequiredPermissions);
		}
	}
	return 0u;
}

uint32 FBlueprintAutomationToolkitModule::GetEffectiveGlobalPermissionsMask() const
{
	auto PM = [](EBATPermission P) { return static_cast<uint32>(P); };

	uint32 Mask = 0u;
	if (bPermissionEditor)
	{
		Mask |= PM(EBATPermission::Editor);
	}
	if (bPermissionBlueprint)
	{
		Mask |= PM(EBATPermission::Blueprint);
	}
	if (bPermissionPie)
	{
		Mask |= PM(EBATPermission::Pie);
	}
	if (bPermissionExec)
	{
		Mask |= PM(EBATPermission::Exec);
	}
	if (bPermissionPython)
	{
		Mask |= PM(EBATPermission::Python);
	}
	if (bPermissionFilesystem)
	{
		Mask |= PM(EBATPermission::Filesystem);
	}

	if (bSafeModeEnabled)
	{
		Mask &= ~PM(EBATPermission::Exec);
		Mask &= ~PM(EBATPermission::Python);
		if (!bAllowFilesystemInSafeMode)
		{
			Mask &= ~PM(EBATPermission::Filesystem);
		}
	}

	return Mask;
}

bool FBlueprintAutomationToolkitModule::TryResolveToken(const FString& RawToken, FTokenRecord& OutToken) const
{
	if (RawToken.IsEmpty())
	{
		return false;
	}

	if (!RuntimeAuthToken.IsEmpty() && RawToken.Equals(RuntimeAuthToken, ESearchCase::CaseSensitive))
	{
		OutToken.Name = TEXT("default");
		OutToken.Token = RawToken;
		OutToken.PermissionsMask = 0xFFFFFFFFu;
		OutToken.bHasExpiry = false;
		return true;
	}

	for (const FTokenRecord& Token : ScopedTokens)
	{
		if (RawToken.Equals(Token.Token, ESearchCase::CaseSensitive))
		{
			OutToken = Token;
			return true;
		}
	}

	return false;
}

bool FBlueprintAutomationToolkitModule::IsPermissionAllowed(uint32 RequiredMask, const FString& SubjectToken, FString& OutReason) const
{
	OutReason.Reset();
	if (RequiredMask == 0u)
	{
		return true;
	}

	const uint32 GlobalMask = GetEffectiveGlobalPermissionsMask();
	if ((GlobalMask & RequiredMask) != RequiredMask)
	{
		OutReason = TEXT("Permission disabled by server policy");
		return false;
	}

	FTokenRecord Token;
	if (!TryResolveToken(SubjectToken, Token))
	{
		OutReason = TEXT("Unknown token subject");
		return false;
	}

	if ((Token.PermissionsMask & RequiredMask) != RequiredMask)
	{
		OutReason = TEXT("Token missing required scope");
		return false;
	}

	return true;
}

bool FBlueprintAutomationToolkitModule::ValidateRequestSchema(const FString& Endpoint, const FHttpServerRequest& Request, const TSharedPtr<FJsonObject>& BodyObj, FString& OutError) const
{
	OutError.Reset();
	if (Request.Verb == EHttpServerRequestVerbs::VERB_GET)
	{
		return true;
	}

	if (!BodyObj.IsValid())
	{
		OutError = TEXT("Request body must be a JSON object");
		return false;
	}

	if (Endpoint.Equals(TEXT("/ai/editor/layout/apply"), ESearchCase::CaseSensitive))
	{
		const TArray<TSharedPtr<FJsonValue>>* Actors = nullptr;
		if (!BodyObj->TryGetArrayField(TEXT("actors"), Actors))
		{
			OutError = TEXT("'actors' array is required");
			return false;
		}
	}
	else if (Endpoint.Equals(TEXT("/ai/plan/validate"), ESearchCase::CaseSensitive) || Endpoint.Equals(TEXT("/ai/plan/apply"), ESearchCase::CaseSensitive))
	{
		const TArray<TSharedPtr<FJsonValue>>* Ops = nullptr;
		if (!BodyObj->TryGetArrayField(TEXT("ops"), Ops))
		{
			OutError = TEXT("'ops' array is required");
			return false;
		}

		if (bSafeModeEnabled)
		{
			for (const TSharedPtr<FJsonValue>& OpVal : *Ops)
			{
				if (!OpVal.IsValid() || OpVal->Type != EJson::Object)
				{
					continue;
				}
				const TSharedPtr<FJsonObject> OpObj = OpVal->AsObject();
				if (!OpObj.IsValid())
				{
					continue;
				}
				bool bUndoable = true;
				OpObj->TryGetBoolField(TEXT("undoable"), bUndoable);
				if (!bUndoable)
				{
					OutError = TEXT("Safe mode rejects non-undoable plan ops");
					return false;
				}
			}
		}
	}
	else if (Endpoint.Equals(TEXT("/blueprint/create"), ESearchCase::CaseSensitive))
	{
		FString Path;
		FString Name;
		if (!BodyObj->TryGetStringField(TEXT("path"), Path) || Path.TrimStartAndEnd().IsEmpty())
		{
			OutError = TEXT("'path' is required");
			return false;
		}
		if (!BodyObj->TryGetStringField(TEXT("name"), Name) || Name.TrimStartAndEnd().IsEmpty())
		{
			OutError = TEXT("'name' is required");
			return false;
		}
		if (Name.Len() > 128)
		{
			OutError = TEXT("'name' length must be <= 128");
			return false;
		}
	}
	else if (Endpoint.Equals(TEXT("/blueprint/compile_save"), ESearchCase::CaseSensitive))
	{
		FString Blueprint;
		if (!BodyObj->TryGetStringField(TEXT("blueprint"), Blueprint) || Blueprint.TrimStartAndEnd().IsEmpty())
		{
			OutError = TEXT("'blueprint' is required");
			return false;
		}
		if (Blueprint.Len() > 512)
		{
			OutError = TEXT("'blueprint' length must be <= 512");
			return false;
		}
	}
	else if (Endpoint.Equals(TEXT("/blueprint/schema"), ESearchCase::CaseSensitive))
	{
		FString Blueprint;
		if (!BodyObj->TryGetStringField(TEXT("blueprint"), Blueprint) || Blueprint.TrimStartAndEnd().IsEmpty())
		{
			OutError = TEXT("'blueprint' is required");
			return false;
		}
		if (Blueprint.Len() > 512)
		{
			OutError = TEXT("'blueprint' length must be <= 512");
			return false;
		}

		FString Graph;
		if (BodyObj->TryGetStringField(TEXT("graph"), Graph) && Graph.Len() > 128)
		{
			OutError = TEXT("'graph' length must be <= 128");
			return false;
		}
	}
	else if (Endpoint.Equals(TEXT("/jobs/submit"), ESearchCase::CaseSensitive))
	{
		FString Kind;
		if (!BodyObj->TryGetStringField(TEXT("kind"), Kind) || Kind.TrimStartAndEnd().IsEmpty())
		{
			OutError = TEXT("'kind' is required");
			return false;
		}

		const TSharedPtr<FJsonObject>* PayloadField = nullptr;
		if (!BodyObj->TryGetObjectField(TEXT("payload"), PayloadField) || !PayloadField || !PayloadField->IsValid())
		{
			OutError = TEXT("'payload' object is required");
			return false;
		}

		static const TArray<FString> AllowedKinds = {
			TEXT("pie.start"),
			TEXT("pie.stop"),
			TEXT("editor.layout.apply"),
			TEXT("plan.apply"),
			TEXT("blueprint.create"),
			TEXT("blueprint.save")
		};
		if (!AllowedKinds.Contains(Kind))
		{
			OutError = TEXT("'kind' is not supported");
			return false;
		}
	}
	else if (Endpoint.Equals(TEXT("/ai/exec"), ESearchCase::CaseSensitive) || Endpoint.Equals(TEXT("/exec"), ESearchCase::CaseSensitive))
	{
		FString Command;
		FString Python;
		const TArray<TSharedPtr<FJsonValue>>* Commands = nullptr;
		const bool bHasCommand = BodyObj->TryGetStringField(TEXT("command"), Command) && !Command.TrimStartAndEnd().IsEmpty();
		const bool bHasPython = BodyObj->TryGetStringField(TEXT("python"), Python) && !Python.TrimStartAndEnd().IsEmpty();
		const bool bHasCommands = BodyObj->TryGetArrayField(TEXT("commands"), Commands) && Commands && Commands->Num() > 0;
		if (!(bHasCommand || bHasPython || bHasCommands))
		{
			OutError = TEXT("One of 'command', 'commands', or 'python' is required");
			return false;
		}
	}
	else if (Endpoint.Equals(TEXT("/object/set_property"), ESearchCase::CaseSensitive))
	{
		FString Path;
		FString ObjectPath;
		FString ActorName;
		const bool bHasPath = BodyObj->TryGetStringField(TEXT("path"), Path) && !Path.TrimStartAndEnd().IsEmpty();
		const bool bHasObjectPath = BodyObj->TryGetStringField(TEXT("objectPath"), ObjectPath) && !ObjectPath.TrimStartAndEnd().IsEmpty();
		const bool bHasActorName = BodyObj->TryGetStringField(TEXT("actorName"), ActorName) && !ActorName.TrimStartAndEnd().IsEmpty();
		if (!(bHasPath || bHasObjectPath || bHasActorName))
		{
			OutError = TEXT("One of 'path', 'objectPath', or 'actorName' is required");
			return false;
		}
		const TSharedPtr<FJsonObject>* Values = nullptr;
		if (!BodyObj->TryGetObjectField(TEXT("values"), Values) || !Values || !Values->IsValid())
		{
			OutError = TEXT("'values' object is required");
			return false;
		}
	}
	else if (Endpoint.Equals(TEXT("/object/call_function"), ESearchCase::CaseSensitive))
	{
		FString Path;
		FString ObjectPath;
		FString ActorName;
		FString Function;
		const bool bHasPath = BodyObj->TryGetStringField(TEXT("path"), Path) && !Path.TrimStartAndEnd().IsEmpty();
		const bool bHasObjectPath = BodyObj->TryGetStringField(TEXT("objectPath"), ObjectPath) && !ObjectPath.TrimStartAndEnd().IsEmpty();
		const bool bHasActorName = BodyObj->TryGetStringField(TEXT("actorName"), ActorName) && !ActorName.TrimStartAndEnd().IsEmpty();
		if (!(bHasPath || bHasObjectPath || bHasActorName))
		{
			OutError = TEXT("One of 'path', 'objectPath', or 'actorName' is required");
			return false;
		}
		if (!BodyObj->TryGetStringField(TEXT("function"), Function) || Function.TrimStartAndEnd().IsEmpty())
		{
			OutError = TEXT("'function' is required");
			return false;
		}
	}
	else if (Endpoint.Equals(TEXT("/actor/spawn"), ESearchCase::CaseSensitive))
	{
		FString ClassPath;
		if (!BodyObj->TryGetStringField(TEXT("class"), ClassPath) || ClassPath.TrimStartAndEnd().IsEmpty())
		{
			OutError = TEXT("'class' is required");
			return false;
		}
	}
	else if (Endpoint.Equals(TEXT("/actor/find"), ESearchCase::CaseSensitive))
	{
		FString By;
		FString Value;
		if (!BodyObj->TryGetStringField(TEXT("by"), By) || By.TrimStartAndEnd().IsEmpty())
		{
			OutError = TEXT("'by' is required");
			return false;
		}
		if (!BodyObj->TryGetStringField(TEXT("value"), Value) || Value.TrimStartAndEnd().IsEmpty())
		{
			OutError = TEXT("'value' is required");
			return false;
		}
	}
	else if (Endpoint.Equals(TEXT("/actor/destroy"), ESearchCase::CaseSensitive)
		|| Endpoint.Equals(TEXT("/editor/select"), ESearchCase::CaseSensitive)
		|| Endpoint.Equals(TEXT("/editor/focus"), ESearchCase::CaseSensitive))
	{
		FString Target;
		FString Path;
		FString ObjectPath;
		FString ActorName;
		const bool bHasTarget = BodyObj->TryGetStringField(TEXT("target"), Target) && !Target.TrimStartAndEnd().IsEmpty();
		const bool bHasPath = BodyObj->TryGetStringField(TEXT("path"), Path) && !Path.TrimStartAndEnd().IsEmpty();
		const bool bHasObjectPath = BodyObj->TryGetStringField(TEXT("objectPath"), ObjectPath) && !ObjectPath.TrimStartAndEnd().IsEmpty();
		const bool bHasActorName = BodyObj->TryGetStringField(TEXT("actorName"), ActorName) && !ActorName.TrimStartAndEnd().IsEmpty();
		const TArray<TSharedPtr<FJsonValue>>* Targets = nullptr;
		const bool bHasTargets = BodyObj->TryGetArrayField(TEXT("targets"), Targets) && Targets && Targets->Num() > 0;
		if (!(bHasTarget || bHasPath || bHasObjectPath || bHasActorName || bHasTargets))
		{
			OutError = TEXT("A target reference is required");
			return false;
		}
	}
	else if (Endpoint.Equals(TEXT("/asset/duplicate"), ESearchCase::CaseSensitive))
	{
		FString Src;
		FString Dst;
		const bool bHasSingleSrc = BodyObj->TryGetStringField(TEXT("src"), Src) && !Src.TrimStartAndEnd().IsEmpty();
		const bool bHasSingleDst = BodyObj->TryGetStringField(TEXT("dst"), Dst) && !Dst.TrimStartAndEnd().IsEmpty();
		const TArray<TSharedPtr<FJsonValue>>* Duplicates = nullptr;
		const bool bHasBatch = BodyObj->TryGetArrayField(TEXT("duplicates"), Duplicates) && Duplicates && Duplicates->Num() > 0;
		if (!bHasBatch && !bHasSingleSrc)
		{
			OutError = TEXT("'src' or non-empty 'duplicates' array is required");
			return false;
		}
		if (!bHasBatch && !bHasSingleDst)
		{
			OutError = TEXT("'dst' is required");
			return false;
		}
		if (bHasBatch)
		{
			for (const TSharedPtr<FJsonValue>& Entry : *Duplicates)
			{
				if (!Entry.IsValid() || Entry->Type != EJson::Object)
				{
					OutError = TEXT("'duplicates' entries must be objects");
					return false;
				}

				const TSharedPtr<FJsonObject> EntryObj = Entry->AsObject();
				FString EntrySrc;
				FString EntryDst;
				if (!EntryObj.IsValid()
					|| !EntryObj->TryGetStringField(TEXT("src"), EntrySrc)
					|| EntrySrc.TrimStartAndEnd().IsEmpty()
					|| !EntryObj->TryGetStringField(TEXT("dst"), EntryDst)
					|| EntryDst.TrimStartAndEnd().IsEmpty())
				{
					OutError = TEXT("Each 'duplicates' entry must include non-empty 'src' and 'dst'");
					return false;
				}
			}
		}
	}
	else if (Endpoint.Equals(TEXT("/asset/save"), ESearchCase::CaseSensitive))
	{
		FString SinglePath;
		const bool bHasSinglePath =
			(BodyObj->TryGetStringField(TEXT("path"), SinglePath) && !SinglePath.TrimStartAndEnd().IsEmpty())
			|| (BodyObj->TryGetStringField(TEXT("target"), SinglePath) && !SinglePath.TrimStartAndEnd().IsEmpty());
		const TArray<TSharedPtr<FJsonValue>>* Paths = nullptr;
		if (!bHasSinglePath && (!BodyObj->TryGetArrayField(TEXT("paths"), Paths) || !Paths || Paths->Num() <= 0))
		{
			OutError = TEXT("'path', 'target', or non-empty 'paths' array is required");
			return false;
		}
	}
	else if (Endpoint.Equals(TEXT("/asset/delete"), ESearchCase::CaseSensitive))
	{
		FString Path;
		const bool bHasPath = BodyObj->TryGetStringField(TEXT("path"), Path) && !Path.TrimStartAndEnd().IsEmpty();
		const TArray<TSharedPtr<FJsonValue>>* Paths = nullptr;
		const bool bHasPaths = BodyObj->TryGetArrayField(TEXT("paths"), Paths) && Paths && Paths->Num() > 0;
		if (!bHasPath && !bHasPaths)
		{
			OutError = TEXT("'path' or non-empty 'paths' array is required");
			return false;
		}
		if (bHasPaths)
		{
			for (const TSharedPtr<FJsonValue>& Entry : *Paths)
			{
				if (!Entry.IsValid() || Entry->Type != EJson::String || Entry->AsString().TrimStartAndEnd().IsEmpty())
				{
					OutError = TEXT("'paths' entries must be non-empty strings");
					return false;
				}
			}
		}
		if (BodyObj->HasField(TEXT("force")) && !BodyObj->HasTypedField<EJson::Boolean>(TEXT("force")))
		{
			OutError = TEXT("'force' must be a boolean when provided");
			return false;
		}
	}
	else if (Endpoint.Equals(TEXT("/asset/create"), ESearchCase::CaseSensitive))
	{
		FString ClassPath;
		FString Path;
		FString Outer;
		if (!BodyObj->TryGetStringField(TEXT("class"), ClassPath) || ClassPath.TrimStartAndEnd().IsEmpty())
		{
			OutError = TEXT("'class' is required");
			return false;
		}
		const bool bHasPath = BodyObj->TryGetStringField(TEXT("path"), Path) && !Path.TrimStartAndEnd().IsEmpty();
		const bool bHasOuter = BodyObj->TryGetStringField(TEXT("outer"), Outer) && !Outer.TrimStartAndEnd().IsEmpty();
		if (!bHasPath && !bHasOuter)
		{
			OutError = TEXT("'path' or 'outer' is required");
			return false;
		}
		if (BodyObj->HasField(TEXT("forward_axis")))
		{
			FString ForwardAxis;
			if (!BodyObj->TryGetStringField(TEXT("forward_axis"), ForwardAxis))
			{
				OutError = TEXT("'forward_axis' must be a string");
				return false;
			}

			const FString TrimmedAxis = ForwardAxis.TrimStartAndEnd();
			if (!TrimmedAxis.IsEmpty())
			{
				FString CanonicalForwardAxis;
				if (!BAT::ForwardAxis::TryNormalizeAxis(TrimmedAxis, CanonicalForwardAxis, OutError))
				{
					return false;
				}
			}
		}
	}
	else if (Endpoint.Equals(TEXT("/actions/run"), ESearchCase::CaseSensitive))
	{
		FString Name;
		if (!BodyObj->TryGetStringField(TEXT("name"), Name) || Name.TrimStartAndEnd().IsEmpty())
		{
			OutError = TEXT("'name' is required");
			return false;
		}
	}
	else if (Endpoint.Equals(TEXT("/pcg/spawn_spheres"), ESearchCase::CaseSensitive))
	{
		FString DstGraph;
		if (!BodyObj->TryGetStringField(TEXT("dst_graph"), DstGraph) || DstGraph.TrimStartAndEnd().IsEmpty())
		{
			OutError = TEXT("'dst_graph' is required");
			return false;
		}
	}

	return true;
}

void FBlueprintAutomationToolkitModule::RunOnGameThread(TFunction<void()> Fn) const
{
	BAT::EditorExecution::RunOnGameThread(MoveTemp(Fn));
}

bool FBlueprintAutomationToolkitModule::RunOnGameThreadWait(TFunction<void()> Fn, double TimeoutSeconds) const
{
	return BAT::EditorExecution::RunOnGameThreadAndWaitVoid(MoveTemp(Fn), (float)TimeoutSeconds);
}

void FBlueprintAutomationToolkitModule::AppendStructuredLog(const FStructuredLogEntry& Entry)
{
	FScopeLock Lock(&LogMutex);
	StructuredLogs.Add(Entry);
	if (StructuredLogs.Num() > LogRingSize)
	{
		StructuredLogs.RemoveAt(0, StructuredLogs.Num() - LogRingSize, EAllowShrinking::No);
	}
}

void FBlueprintAutomationToolkitModule::FinalizeRequestLog(const FRequestContext& Context, int32 StatusCode, const FString& ErrorCode)
{
	FStructuredLogEntry Entry;
	Entry.TimestampUtc = Context.StartedUtc;
	Entry.RequestId = Context.RequestId;
	Entry.Route = Context.Endpoint;
	Entry.Subject = Context.Subject;
	Entry.Status = StatusCode;
	Entry.DurationMs = (FDateTime::UtcNow() - Context.StartedUtc).GetTotalMilliseconds();
	Entry.ErrorCode = ErrorCode;
	AppendStructuredLog(Entry);
}

TArray<FBlueprintAutomationToolkitModule::FStructuredLogEntry> FBlueprintAutomationToolkitModule::GetRecentLogs(int32 MaxCount) const
{
	TArray<FStructuredLogEntry> Out;
	FScopeLock Lock(&LogMutex);
	const int32 Count = FMath::Min(MaxCount, StructuredLogs.Num());
	const int32 Start = StructuredLogs.Num() - Count;
	for (int32 i = Start; i < StructuredLogs.Num(); ++i)
	{
		Out.Add(StructuredLogs[i]);
	}
	return Out;
}

FString FBlueprintAutomationToolkitModule::GetTokenStatusText() const
{
	if (bAuthTokenFromEnv)
	{
		return TEXT("From ENV");
	}
	if (RuntimeAuthToken.IsEmpty())
	{
		return TEXT("Not set");
	}
	return bSaveTokenInProjectSettings ? TEXT("Set (saved to project settings)") : TEXT("Set (session only)");
}

void FBlueprintAutomationToolkitModule::RecordPlanExecution(int32 OpCount, bool bSuccess)
{
	FPlanExecutionEntry Entry;
	Entry.Timestamp = FDateTime::UtcNow();
	Entry.OpCount = OpCount;
	Entry.bSuccess = bSuccess;

	FScopeLock Lock(&SecurityStateMutex);
	PlanExecutionLog.Add(MoveTemp(Entry));
	if (PlanExecutionLog.Num() > 50)
	{
		PlanExecutionLog.RemoveAt(0, PlanExecutionLog.Num() - 50, EAllowShrinking::No);
	}
}

FString FBlueprintAutomationToolkitModule::SubmitJob(const FString& Kind, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
	const FString JobId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
	FJobRecord Job;
	Job.JobId = JobId;
	Job.Kind = Kind;
	Job.RequestId = RequestId;
	Job.State = EJobState::Queued;
	Job.Progress = 0.0;
	Job.CreatedUtc = FDateTime::UtcNow();
	Job.UpdatedUtc = Job.CreatedUtc;
	Job.Result = Payload;

	{
		FScopeLock Lock(&JobMutex);
		Jobs.Add(JobId, Job);
	}

	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this, JobId]()
	{
		ExecuteJob(JobId);
	});

	return JobId;
}

bool FBlueprintAutomationToolkitModule::TryGetJob(const FString& JobId, FJobRecord& OutJob) const
{
	FScopeLock Lock(&JobMutex);
	if (const FJobRecord* Found = Jobs.Find(JobId))
	{
		OutJob = *Found;
		return true;
	}
	return false;
}

bool FBlueprintAutomationToolkitModule::CancelJob(const FString& JobId)
{
	FScopeLock Lock(&JobMutex);
	if (FJobRecord* Job = Jobs.Find(JobId))
	{
		Job->bCancelRequested = true;
		if (Job->State == EJobState::Queued)
		{
			Job->State = EJobState::Canceled;
		}
		Job->UpdatedUtc = FDateTime::UtcNow();
		return true;
	}
	return false;
}

void FBlueprintAutomationToolkitModule::UpdateJobState(const FString& JobId, EJobState NewState, double Progress)
{
	FScopeLock Lock(&JobMutex);
	if (FJobRecord* Job = Jobs.Find(JobId))
	{
		Job->State = NewState;
		if (Progress >= 0.0)
		{
			Job->Progress = Progress;
		}
		Job->UpdatedUtc = FDateTime::UtcNow();
	}
}

void FBlueprintAutomationToolkitModule::AppendJobLog(const FString& JobId, const FString& Line)
{
	FScopeLock Lock(&JobMutex);
	if (FJobRecord* Job = Jobs.Find(JobId))
	{
		Job->Logs.Add(Line);
		if (Job->Logs.Num() > 200)
		{
			Job->Logs.RemoveAt(0, Job->Logs.Num() - 200, EAllowShrinking::No);
		}
		Job->UpdatedUtc = FDateTime::UtcNow();
	}
}

void FBlueprintAutomationToolkitModule::CompleteJobSuccess(const FString& JobId, const TSharedPtr<FJsonObject>& Result)
{
	FScopeLock Lock(&JobMutex);
	if (FJobRecord* Job = Jobs.Find(JobId))
	{
		if (Job->bCancelRequested || Job->State == EJobState::Canceled)
		{
			Job->State = EJobState::Canceled;
			Job->Progress = 1.0;
			Job->UpdatedUtc = FDateTime::UtcNow();
			return;
		}
		Job->State = EJobState::Succeeded;
		Job->Progress = 1.0;
		Job->Result = Result;
		Job->Error.Reset();
		Job->UpdatedUtc = FDateTime::UtcNow();
	}
}

void FBlueprintAutomationToolkitModule::CompleteJobFailure(const FString& JobId, const FString& Code, const FString& Message, const TSharedPtr<FJsonObject>& Details)
{
	FScopeLock Lock(&JobMutex);
	if (FJobRecord* Job = Jobs.Find(JobId))
	{
		if (Job->bCancelRequested || Job->State == EJobState::Canceled)
		{
			Job->State = EJobState::Canceled;
			Job->Progress = 1.0;
			Job->UpdatedUtc = FDateTime::UtcNow();
			return;
		}
		Job->State = EJobState::Failed;
		Job->Progress = 1.0;
		TSharedRef<FJsonObject> Err = MakeShared<FJsonObject>();
		Err->SetStringField(TEXT("code"), Code);
		Err->SetStringField(TEXT("message"), Message);
		if (Details.IsValid())
		{
			Err->SetObjectField(TEXT("details"), Details.ToSharedRef());
		}
		Job->Error = Err;
		Job->UpdatedUtc = FDateTime::UtcNow();
	}
}

void FBlueprintAutomationToolkitModule::ExecuteJob(const FString& JobId)
{
	auto IsCancelRequested = [this, &JobId]()
	{
		FJobRecord CurrentJob;
		return TryGetJob(JobId, CurrentJob) && (CurrentJob.bCancelRequested || CurrentJob.State == EJobState::Canceled);
	};

	FJobRecord Job;
	if (!TryGetJob(JobId, Job))
	{
		return;
	}
	if (Job.bCancelRequested || Job.State == EJobState::Canceled)
	{
		UpdateJobState(JobId, EJobState::Canceled, 1.0);
		AppendJobLog(JobId, TEXT("job_canceled"));
		return;
	}

	UpdateJobState(JobId, EJobState::Running, 0.05);
	if (IsCancelRequested())
	{
		UpdateJobState(JobId, EJobState::Canceled, 1.0);
		AppendJobLog(JobId, TEXT("job_canceled"));
		return;
	}
	AppendJobLog(JobId, TEXT("job_started"));

	FString ErrCode;
	FString ErrMessage;
	const TSharedPtr<FJsonObject> Result = ExecuteJobByKind(JobId, Job.Kind, Job.Result, ErrCode, ErrMessage);
	if (IsCancelRequested())
	{
		UpdateJobState(JobId, EJobState::Canceled, 1.0);
		AppendJobLog(JobId, TEXT("job_canceled"));
		return;
	}
	if (!ErrCode.IsEmpty())
	{
		CompleteJobFailure(JobId, ErrCode, ErrMessage);
		AppendJobLog(JobId, FString::Printf(TEXT("job_failed:%s"), *ErrCode));
		return;
	}

	CompleteJobSuccess(JobId, Result);
	AppendJobLog(JobId, TEXT("job_succeeded"));
}

#if WITH_DEV_AUTOMATION_TESTS
uint32 FBlueprintAutomationToolkitModule::Test_GetRouteRequiredPermissions(const FString& Endpoint) const
{
	return GetRouteRequiredPermissions(Endpoint);
}

uint32 FBlueprintAutomationToolkitModule::Test_GetRequestRequiredPermissions(const FString& Endpoint, const TSharedPtr<FJsonObject>& BodyObj) const
{
	return GetRequestRequiredPermissions(Endpoint, BodyObj);
}

bool FBlueprintAutomationToolkitModule::Test_IsEditorAssetMutationBlockedDuringPie(const FString& Endpoint) const
{
	return IsEditorAssetMutationBlockedDuringPie(Endpoint);
}

bool FBlueprintAutomationToolkitModule::Test_BuildAuthFailureResponse(const FString& Code, FString& OutJson, FString& OutWwwAuthenticate) const
{
	OutJson.Reset();
	OutWwwAuthenticate.Reset();

	const FString RequestId = TEXT("test-request-id");
	TUniquePtr<FHttpServerResponse> Response = MakeAuthFailureResponse(RequestId, Code);
	if (!Response.IsValid())
	{
		return false;
	}

	const TArray<FString>* HeaderValues = Response->Headers.Find(TEXT("WWW-Authenticate"));
	if (HeaderValues && HeaderValues->Num() > 0)
	{
		OutWwwAuthenticate = (*HeaderValues)[0];
	}

	OutJson = FString(Response->Body.Num(), reinterpret_cast<const ANSICHAR*>(Response->Body.GetData()));
	return true;
}

	static void NormalizeReflectionClassValues(const TArray<FString>& Source, TSet<FString>& Destination)
	{
		Destination.Reset();
		for (FString Value : Source)
		{
			Value.TrimStartAndEndInline();
			Value = Value.ToLower();
			if (!Value.IsEmpty())
			{
				Destination.Add(Value);
			}
		}
	}

	static void NormalizeReflectionNameValues(const TArray<FString>& Source, TSet<FName>& Destination)
	{
		Destination.Reset();
		for (FString Value : Source)
		{
			Value.TrimStartAndEndInline();
			if (!Value.IsEmpty())
			{
				Destination.Add(FName(*Value));
			}
		}
	}

void FBlueprintAutomationToolkitModule::Test_AddOrUpdateJob(const FString& JobId, const FString& RequestId, const FString& Kind, EAutomationTestJobState State, bool bCancelRequested, const TSharedPtr<FJsonObject>& Payload)
{
	FJobRecord Job;
	Job.JobId = JobId;
	Job.RequestId = RequestId;
	Job.Kind = Kind;
	Job.State = static_cast<EJobState>(State);
	Job.bCancelRequested = bCancelRequested;
	Job.Result = Payload;
	Job.CreatedUtc = FDateTime::UtcNow();
	Job.UpdatedUtc = Job.CreatedUtc;

	FScopeLock Lock(&JobMutex);
	Jobs.Add(JobId, Job);
}

bool FBlueprintAutomationToolkitModule::Test_GetJobSnapshot(const FString& JobId, FAutomationTestJobSnapshot& OutSnapshot) const
{
	FJobRecord Job;
	if (!TryGetJob(JobId, Job))
	{
		return false;
	}

	OutSnapshot.State = static_cast<EAutomationTestJobState>(Job.State);
	OutSnapshot.bCancelRequested = Job.bCancelRequested;
	OutSnapshot.Logs = Job.Logs;
	return true;
}

void FBlueprintAutomationToolkitModule::Test_SetReflectionSafeMode(bool bEnabled)
{
	bSafeModeEnabled = bEnabled;
}

void FBlueprintAutomationToolkitModule::Test_SetReflectionClassAllowList(const TArray<FString>& Values)
{
	NormalizeReflectionClassValues(Values, AllowedReflectionClasses);
}

void FBlueprintAutomationToolkitModule::Test_SetReflectionClassDenyList(const TArray<FString>& Values)
{
	NormalizeReflectionClassValues(Values, DeniedReflectionClasses);
}

void FBlueprintAutomationToolkitModule::Test_SetReflectionFunctionAllowList(const TArray<FString>& Values)
{
	NormalizeReflectionNameValues(Values, AllowedReflectionFunctions);
}

void FBlueprintAutomationToolkitModule::Test_SetReflectionFunctionDenyList(const TArray<FString>& Values)
{
	NormalizeReflectionNameValues(Values, DeniedReflectionFunctions);
}

void FBlueprintAutomationToolkitModule::Test_SetReflectionPropertyAllowList(const TArray<FString>& Values)
{
	NormalizeReflectionNameValues(Values, AllowedReflectionProperties);
}

void FBlueprintAutomationToolkitModule::Test_SetReflectionPropertyDenyList(const TArray<FString>& Values)
{
	NormalizeReflectionNameValues(Values, DeniedReflectionProperties);
}

void FBlueprintAutomationToolkitModule::Test_NotifySettingChanged()
{
	NotifySettingChanged();
}

void FBlueprintAutomationToolkitModule::Test_CompleteJobSuccess(const FString& JobId, const TSharedPtr<FJsonObject>& Result)
{
	CompleteJobSuccess(JobId, Result);
}

void FBlueprintAutomationToolkitModule::Test_CompleteJobFailure(const FString& JobId, const FString& Code, const FString& Message, const TSharedPtr<FJsonObject>& Details)
{
	CompleteJobFailure(JobId, Code, Message, Details);
}

void FBlueprintAutomationToolkitModule::Test_ExecuteJob(const FString& JobId)
{
	ExecuteJob(JobId);
}
#endif

TSharedPtr<FJsonObject> FBlueprintAutomationToolkitModule::ExecuteJobByKind(const FString& JobId, const FString& Kind, const TSharedPtr<FJsonObject>& Payload, FString& OutCode, FString& OutMessage)
{
	OutCode.Reset();
	OutMessage.Reset();

	if (!Payload.IsValid())
	{
		OutCode = TEXT("invalid_payload");
		OutMessage = TEXT("Job payload is missing");
		return nullptr;
	}

	if (Kind.Equals(TEXT("pie.start"), ESearchCase::CaseSensitive))
	{
		bool bStarted = false;
		RunOnGameThreadWait([&bStarted]()
		{
			if (GEditor && GEditor->PlayWorld == nullptr)
			{
				FRequestPlaySessionParams Params;
				GEditor->RequestPlaySession(Params);
				bStarted = true;
			}
		});
		TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
		R->SetBoolField(TEXT("ok"), true);
		R->SetBoolField(TEXT("started"), bStarted);
		return R;
	}

	if (Kind.Equals(TEXT("pie.stop"), ESearchCase::CaseSensitive))
	{
		bool bStopped = false;
		RunOnGameThreadWait([&bStopped]()
		{
			if (GEditor && GEditor->PlayWorld)
			{
				GEditor->RequestEndPlayMap();
				bStopped = true;
			}
		});
		TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
		R->SetBoolField(TEXT("ok"), true);
		R->SetBoolField(TEXT("stopped"), bStopped);
		return R;
	}

	if (Kind.Equals(TEXT("editor.layout.apply"), ESearchCase::CaseSensitive))
	{
		const TArray<TSharedPtr<FJsonValue>>* ActorsArray = nullptr;
		if (!Payload->TryGetArrayField(TEXT("actors"), ActorsArray) || !ActorsArray)
		{
			OutCode = TEXT("missing_actors");
			OutMessage = TEXT("payload.actors is required");
			return nullptr;
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Errors;
		RunOnGameThreadWait([this, ActorsArray, &Result, &Errors]()
		{
			ExecuteEditorLayoutApply(*ActorsArray, true, Result, Errors);
			Result->SetArrayField(TEXT("errors"), Errors);
		});
		if (Errors.Num() > 0)
		{
			OutCode = TEXT("layout_apply_failed");
			OutMessage = TEXT("One or more layout operations failed");
		}
		return Result;
	}

	if (Kind.Equals(TEXT("plan.apply"), ESearchCase::CaseSensitive))
	{
		const TArray<TSharedPtr<FJsonValue>>* OpsArray = nullptr;
		if (!Payload->TryGetArrayField(TEXT("ops"), OpsArray) || !OpsArray)
		{
			OutCode = TEXT("missing_ops");
			OutMessage = TEXT("payload.ops is required");
			return nullptr;
		}

		TArray<TSharedPtr<FJsonValue>> ValidationErrors;
		TArray<TSharedPtr<FJsonValue>> Results;
		int32 TotalInstances = 0;

		RunOnGameThreadWait([this, OpsArray, &ValidationErrors, &Results, &TotalInstances]()
		{
			if (OpsArray->Num() > MaxOpsPerPlan)
			{
				TSharedRef<FJsonObject> Err = MakeShared<FJsonObject>();
				Err->SetNumberField(TEXT("op_index"), -1);
				Err->SetStringField(TEXT("error"), FString::Printf(TEXT("max_ops_per_plan_exceeded:%d"), MaxOpsPerPlan));
				ValidationErrors.Add(MakeShared<FJsonValueObject>(Err));
			}

			for (int32 OpIndex = 0; OpIndex < OpsArray->Num(); ++OpIndex)
			{
				if (!(*OpsArray)[OpIndex].IsValid() || (*OpsArray)[OpIndex]->Type != EJson::Object)
				{
					TSharedRef<FJsonObject> Err = MakeShared<FJsonObject>();
					Err->SetNumberField(TEXT("op_index"), OpIndex);
					Err->SetStringField(TEXT("error"), TEXT("op_entry_must_be_object"));
					ValidationErrors.Add(MakeShared<FJsonValueObject>(Err));
					continue;
				}

				const TSharedPtr<FJsonObject> OpObj = (*OpsArray)[OpIndex]->AsObject();
				if (!OpObj.IsValid())
				{
					continue;
				}

				if (bSafeModeEnabled)
				{
					bool bUndoable = true;
					OpObj->TryGetBoolField(TEXT("undoable"), bUndoable);
					if (!bUndoable)
					{
						TSharedRef<FJsonObject> Err = MakeShared<FJsonObject>();
						Err->SetNumberField(TEXT("op_index"), OpIndex);
						Err->SetStringField(TEXT("error"), TEXT("safe_mode_reject_non_undoable_op"));
						ValidationErrors.Add(MakeShared<FJsonValueObject>(Err));
					}
				}
			}

			if (ValidationErrors.Num() > 0)
			{
				return;
			}

			const FScopedTransaction Tx(FText::FromString(TEXT("BAT: Apply Plan (Job)")));
			for (int32 OpIndex = 0; OpIndex < OpsArray->Num(); ++OpIndex)
			{
				const TSharedPtr<FJsonObject> OpObj = (*OpsArray)[OpIndex]->AsObject();
				FString OpName;
				OpObj->TryGetStringField(TEXT("op"), OpName);
				const TSharedPtr<FJsonObject>* PayloadObjPtr = nullptr;
				OpObj->TryGetObjectField(TEXT("payload"), PayloadObjPtr);

				TSharedRef<FJsonObject> OpResult = MakeShared<FJsonObject>();
				OpResult->SetStringField(TEXT("op"), OpName);
				TArray<TSharedPtr<FJsonValue>> OpErrors;

				if (OpName.Equals(TEXT("editor.layout.apply"), ESearchCase::CaseSensitive))
				{
					const TArray<TSharedPtr<FJsonValue>>* ActorsArray = nullptr;
					if (PayloadObjPtr && *PayloadObjPtr)
					{
						(*PayloadObjPtr)->TryGetArrayField(TEXT("actors"), ActorsArray);
					}
					if (!ActorsArray)
					{
						OpErrors.Add(MakeShared<FJsonValueString>(TEXT("missing_actors")));
					}
					else
					{
						ExecuteEditorLayoutApply(*ActorsArray, true, OpResult, OpErrors);
					}
				}
				else if (OpName.Equals(TEXT("blueprint.apply"), ESearchCase::CaseSensitive))
				{
					if (PayloadObjPtr && *PayloadObjPtr)
					{
						ExecuteBlueprintPatch(*PayloadObjPtr, true, TotalInstances, OpResult, OpErrors);
					}
					else
					{
						OpErrors.Add(MakeShared<FJsonValueString>(TEXT("missing_payload_object")));
					}
				}
				else
				{
					OpErrors.Add(MakeShared<FJsonValueString>(TEXT("unsupported_op")));
				}

				if (OpErrors.Num() > 0)
				{
					if (GEditor)
					{
						GEditor->UndoTransaction();
					}
					TSharedRef<FJsonObject> Err = MakeShared<FJsonObject>();
					Err->SetNumberField(TEXT("op_index"), OpIndex);
					Err->SetStringField(TEXT("error"), TEXT("apply_failed_rolled_back"));
					ValidationErrors.Add(MakeShared<FJsonValueObject>(Err));
					break;
				}

				Results.Add(MakeShared<FJsonValueObject>(OpResult));
			}
		});

		const bool bSuccess = ValidationErrors.Num() == 0;
		RecordPlanExecution(OpsArray->Num(), bSuccess);

		TSharedRef<FJsonObject> ResponseObj = MakeShared<FJsonObject>();
		ResponseObj->SetBoolField(TEXT("ok"), bSuccess);
		ResponseObj->SetArrayField(TEXT("errors"), ValidationErrors);
		ResponseObj->SetArrayField(TEXT("results"), Results);
		if (!bSuccess)
		{
			OutCode = TEXT("plan_apply_failed");
			OutMessage = TEXT("Plan apply failed; transaction rolled back");
		}
		return ResponseObj;
	}

	if (Kind.Equals(TEXT("blueprint.create"), ESearchCase::CaseSensitive))
	{
		FString PackagePath;
		FString Name;
		FString ParentClassPath;
		bool bCompile = false;
		if (!Payload->TryGetStringField(TEXT("path"), PackagePath) || PackagePath.TrimStartAndEnd().IsEmpty()
			|| !Payload->TryGetStringField(TEXT("name"), Name) || Name.TrimStartAndEnd().IsEmpty())
		{
			OutCode = TEXT("missing_fields");
			OutMessage = TEXT("payload.path and payload.name are required");
			return nullptr;
		}
		Payload->TryGetStringField(TEXT("parent"), ParentClassPath);
		Payload->TryGetBoolField(TEXT("compile"), bCompile);

		TSharedPtr<FJsonObject> Response;
		FString LocalCode;
		FString LocalMessage;
		const bool bRan = RunOnGameThreadWait([&]()
		{
			FString CleanPath = PackagePath;
			CleanPath.TrimStartAndEndInline();
			CleanPath.RemoveFromEnd(TEXT("/"));
			if (!FPackageName::IsValidLongPackageName(CleanPath))
			{
				LocalCode = TEXT("bad_path");
				LocalMessage = TEXT("Invalid long package path (example: /Game/Blueprints)");
				return;
			}

			UClass* ParentClass = AActor::StaticClass();
			if (!ParentClassPath.IsEmpty())
			{
				ParentClass = LoadObject<UClass>(nullptr, *ParentClassPath);
				if (!ParentClass)
				{
					ParentClass = FindObject<UClass>(nullptr, *ParentClassPath);
				}
				if (!ParentClass)
				{
					LocalCode = TEXT("bad_parent");
					LocalMessage = TEXT("Unable to load parent class");
					return;
				}
			}

			const FScopedTransaction Tx(FText::FromString(TEXT("BAT: Create Blueprint (Job)")));
			const FString PackageName = FString::Printf(TEXT("%s/%s"), *CleanPath, *Name);
			const FString ExistingObjectPath = FString::Printf(TEXT("%s.%s"), *PackageName, *Name);
			if (UBlueprint* ExistingBlueprint = LoadObject<UBlueprint>(nullptr, *ExistingObjectPath))
			{
				Response = MakeShared<FJsonObject>();
				Response->SetBoolField(TEXT("ok"), true);
				Response->SetBoolField(TEXT("already_exists"), true);
				Response->SetStringField(TEXT("package"), PackageName);
				Response->SetStringField(TEXT("object_path"), ExistingObjectPath);
				return;
			}

			UPackage* Package = CreatePackage(*PackageName);
			if (!Package)
			{
				LocalCode = TEXT("package_failed");
				LocalMessage = TEXT("Failed to create package");
				return;
			}
			Package->Modify();

			UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
				ParentClass,
				Package,
				FName(*Name),
				BPTYPE_Normal,
				UBlueprint::StaticClass(),
				UBlueprintGeneratedClass::StaticClass(),
				NAME_None);

			if (!Blueprint)
			{
				LocalCode = TEXT("create_failed");
				LocalMessage = TEXT("Failed to create Blueprint");
				return;
			}

			Blueprint->Modify();
			FAssetRegistryModule::AssetCreated(Blueprint);
			Package->MarkPackageDirty();
			if (bCompile)
			{
				FKismetEditorUtilities::CompileBlueprint(Blueprint);
			}

			Response = MakeShared<FJsonObject>();
			Response->SetBoolField(TEXT("ok"), true);
			Response->SetStringField(TEXT("package"), PackageName);
			Response->SetStringField(TEXT("object_path"), ExistingObjectPath);
		});

		if (!bRan)
		{
			OutCode = TEXT("game_thread_timeout");
			OutMessage = TEXT("Timed out waiting for GameThread");
			return nullptr;
		}
		if (!LocalCode.IsEmpty())
		{
			OutCode = LocalCode;
			OutMessage = LocalMessage;
			return nullptr;
		}
		return Response;
	}

	if (Kind.Equals(TEXT("blueprint.save"), ESearchCase::CaseSensitive))
	{
		FString BlueprintPath;
		if (!Payload->TryGetStringField(TEXT("blueprint"), BlueprintPath) || BlueprintPath.TrimStartAndEnd().IsEmpty())
		{
			OutCode = TEXT("missing_blueprint");
			OutMessage = TEXT("payload.blueprint is required");
			return nullptr;
		}

		TSharedPtr<FJsonObject> Response;
		FString LocalCode;
		FString LocalMessage;
		const bool bRan = RunOnGameThreadWait([&]()
		{
			UBlueprint* Blueprint = nullptr;
			FString ObjectPath;
			if (!TryLoadBlueprintByPath(BlueprintPath, Blueprint, ObjectPath))
			{
				LocalCode = TEXT("not_found");
				LocalMessage = TEXT("Blueprint not found");
				return;
			}

			UPackage* Package = Blueprint->GetOutermost();
			if (!Package)
			{
				LocalCode = TEXT("save_failed");
				LocalMessage = TEXT("Blueprint package is null");
				return;
			}

			TArray<UPackage*> Packages;
			Packages.Add(Package);
			if (!UEditorLoadingAndSavingUtils::SavePackages(Packages, false))
			{
				LocalCode = TEXT("save_failed");
				LocalMessage = TEXT("SavePackages returned false");
				return;
			}

			Response = MakeShared<FJsonObject>();
			Response->SetBoolField(TEXT("ok"), true);
			Response->SetStringField(TEXT("blueprint"), ObjectPath);
		});

		if (!bRan)
		{
			OutCode = TEXT("game_thread_timeout");
			OutMessage = TEXT("Timed out waiting for GameThread");
			return nullptr;
		}
		if (!LocalCode.IsEmpty())
		{
			OutCode = LocalCode;
			OutMessage = LocalMessage;
			return nullptr;
		}
		return Response;
	}

	OutCode = TEXT("unsupported_job_kind");
	OutMessage = FString::Printf(TEXT("Unsupported job kind: %s"), *Kind);
	return nullptr;
}

UWorld* FBlueprintAutomationToolkitModule::GetEditorWorld() const
{
	return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
}

UWorld* FBlueprintAutomationToolkitModule::GetPIEWorld(int32 PieIndex) const
{
	if (!GEditor)
	{
		return nullptr;
	}

	if (PieIndex <= 0 && GEditor->PlayWorld)
	{
		return GEditor->PlayWorld;
	}

	TArray<UWorld*> PieWorlds;
	for (const FWorldContext& Ctx : GEditor->GetWorldContexts())
	{
		if (Ctx.WorldType == EWorldType::PIE && Ctx.World())
		{
			PieWorlds.Add(Ctx.World());
		}
	}
	return PieWorlds.IsValidIndex(PieIndex) ? PieWorlds[PieIndex] : nullptr;
}

UWorld* FBlueprintAutomationToolkitModule::ResolveWorld(const FString& Mode, int32 PieIndex, bool& bOutIsPie, int32& OutResolvedPieIndex, FString& OutError) const
{
	bOutIsPie = false;
	OutResolvedPieIndex = -1;
	OutError.Reset();

	if (Mode.Equals(TEXT("editor"), ESearchCase::IgnoreCase))
	{
		return GetEditorWorld();
	}

	if (Mode.Equals(TEXT("pie"), ESearchCase::IgnoreCase))
	{
		bOutIsPie = true;
		UWorld* World = GetPIEWorld(PieIndex);
		if (!World)
		{
			OutError = TEXT("PIE world not available");
		}
		else
		{
			OutResolvedPieIndex = PieIndex;
		}
		return World;
	}

	if (UWorld* Pie = GetPIEWorld(PieIndex))
	{
		bOutIsPie = true;
		OutResolvedPieIndex = PieIndex;
		return Pie;
	}
	return GetEditorWorld();
}

AActor* FBlueprintAutomationToolkitModule::ResolveActor(UWorld* World, const FString& NameOrLabelOrPath, const FString& Tag) const
{
	if (!World)
	{
		return nullptr;
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor)
		{
			continue;
		}

		if (!Tag.IsEmpty() && !Actor->ActorHasTag(FName(*Tag)))
		{
			continue;
		}

		if (NameOrLabelOrPath.IsEmpty()
			|| Actor->GetName().Equals(NameOrLabelOrPath, ESearchCase::IgnoreCase)
			|| Actor->GetPathName().Equals(NameOrLabelOrPath, ESearchCase::IgnoreCase)
#if WITH_EDITOR
			|| Actor->GetActorLabel().Equals(NameOrLabelOrPath, ESearchCase::IgnoreCase)
#endif
		)
		{
			return Actor;
		}
	}

	return nullptr;
}

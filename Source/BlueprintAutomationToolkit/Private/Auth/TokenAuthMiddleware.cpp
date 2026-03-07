#include "Auth/TokenAuthMiddleware.h"

#include "BlueprintAutomationToolkitModule.h"
#include "HttpServerRequest.h"
#include "Misc/SecureHash.h"

namespace
{
	static const TArray<FString>* FindHeaderCaseInsensitiveLocal(const TMap<FString, TArray<FString>>& Headers, const TCHAR* Name)
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

	static FString ParseBearerTokenLocal(const FString& HeaderValue)
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
}

bool FTokenAuthMiddleware::Authorize(const FBlueprintAutomationToolkitModule& Module, const FHttpServerRequest& Request, FString* OutDenyReason, FString* OutClientKey) const
{
	if (OutDenyReason)
	{
		OutDenyReason->Reset();
	}

	if (!Request.PeerAddress.IsValid())
	{
		if (OutDenyReason)
		{
			*OutDenyReason = TEXT("peer_address_invalid");
		}
		return false;
	}

	const FString Peer = Request.PeerAddress->ToString(false).ToLower();
	if (OutClientKey)
	{
		*OutClientKey = Peer;
	}

	const bool bIsLoopback =
		(Peer == TEXT("127.0.0.1")) ||
		(Peer == TEXT("localhost")) ||
		(Peer == TEXT("::1")) ||
		Peer.StartsWith(TEXT("::ffff:127.0.0.1"));

	if (!bIsLoopback)
	{
		if (OutDenyReason)
		{
			*OutDenyReason = TEXT("not_loopback");
		}
		return false;
	}

	if (FindHeaderCaseInsensitiveLocal(Request.Headers, TEXT("x-forwarded-for")) != nullptr)
	{
		if (OutDenyReason)
		{
			*OutDenyReason = TEXT("forwarded_header_denied");
		}
		return false;
	}

	const TArray<FString>* HeaderValues = FindHeaderCaseInsensitiveLocal(Request.Headers, TEXT("authorization"));
	if (!Module.bRequireAuthToken && (!HeaderValues || HeaderValues->Num() <= 0))
	{
		return true;
	}

	if (!HeaderValues || HeaderValues->Num() <= 0)
	{
		if (OutDenyReason)
		{
			*OutDenyReason = TEXT("auth_missing");
		}
		return false;
	}

	const FString RawToken = ParseBearerTokenLocal((*HeaderValues)[0]);
	if (RawToken.IsEmpty())
	{
		if (OutDenyReason)
		{
			*OutDenyReason = TEXT("auth_invalid_format");
		}
		return false;
	}

	FBlueprintAutomationToolkitModule::FTokenRecord Token;
	if (!Module.TryResolveToken(RawToken, Token))
	{
		if (OutDenyReason)
		{
			*OutDenyReason = TEXT("auth_invalid");
		}
		return false;
	}

	if (Token.bHasExpiry && FDateTime::UtcNow() > Token.ExpiresUtc)
	{
		if (OutDenyReason)
		{
			*OutDenyReason = TEXT("auth_expired");
		}
		return false;
	}

	if (Module.bEnableHmacAuth && !Token.Secret.IsEmpty())
	{
		const FString Timestamp = Module.ReadHeaderValueCaseInsensitive(Request.Headers, TEXT("x-timestamp"));
		const FString Signature = Module.ReadHeaderValueCaseInsensitive(Request.Headers, TEXT("x-signature"));
		if (Timestamp.IsEmpty() || Signature.IsEmpty())
		{
			if (OutDenyReason)
			{
				*OutDenyReason = TEXT("auth_signature_missing");
			}
			return false;
		}

		FDateTime ParsedTs;
		if (!FDateTime::ParseIso8601(*Timestamp, ParsedTs))
		{
			if (OutDenyReason)
			{
				*OutDenyReason = TEXT("auth_signature_bad_timestamp");
			}
			return false;
		}

		const FTimespan Skew = (FDateTime::UtcNow() - ParsedTs).GetDuration();
		if (Skew.GetTotalSeconds() > (double)Module.MaxClockSkewSeconds)
		{
			if (OutDenyReason)
			{
				*OutDenyReason = TEXT("auth_signature_replay");
			}
			return false;
		}

		const FString BodyHash = FMD5::HashBytes(Request.Body.GetData(), Request.Body.Num());
		const FString Canonical = FString::Printf(TEXT("%s:%s:%s"), *Token.Secret, *Timestamp, *BodyHash);
		const FString Expected = FMD5::HashAnsiString(*Canonical);
		if (!Expected.Equals(Signature, ESearchCase::CaseSensitive))
		{
			if (OutDenyReason)
			{
				*OutDenyReason = TEXT("auth_signature_invalid");
			}
			return false;
		}
	}

	if (OutClientKey)
	{
		*OutClientKey = RawToken;
	}

	return true;
}
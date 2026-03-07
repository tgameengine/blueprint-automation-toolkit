#include "Core/ForwardAxis.h"

namespace
{
	static FString NormalizeAxisToken(const FString& InAxis)
	{
		FString Axis = InAxis;
		Axis.TrimStartAndEndInline();
		Axis.ToUpperInline();
		Axis.ReplaceInline(TEXT(" "), TEXT(""));
		Axis.ReplaceInline(TEXT("_"), TEXT(""));
		return Axis;
	}

	static bool TryGetCanonicalAxisToken(const FString& InToken, FString& OutCanonicalAxis)
	{
		if (InToken.IsEmpty()
			|| InToken.Equals(TEXT("X"), ESearchCase::CaseSensitive)
			|| InToken.Equals(TEXT("+X"), ESearchCase::CaseSensitive)
			|| InToken.Equals(TEXT("X+"), ESearchCase::CaseSensitive)
			|| InToken.Equals(TEXT("FORWARD"), ESearchCase::CaseSensitive)
			|| InToken.Equals(TEXT("+FORWARD"), ESearchCase::CaseSensitive)
			|| InToken.Equals(TEXT("FORWARD+"), ESearchCase::CaseSensitive))
		{
			OutCanonicalAxis = TEXT("X");
			return true;
		}

		if (InToken.Equals(TEXT("-X"), ESearchCase::CaseSensitive)
			|| InToken.Equals(TEXT("X-"), ESearchCase::CaseSensitive)
			|| InToken.Equals(TEXT("-FORWARD"), ESearchCase::CaseSensitive)
			|| InToken.Equals(TEXT("FORWARD-"), ESearchCase::CaseSensitive)
			|| InToken.Equals(TEXT("BACK"), ESearchCase::CaseSensitive)
			|| InToken.Equals(TEXT("BACKWARD"), ESearchCase::CaseSensitive))
		{
			OutCanonicalAxis = TEXT("-X");
			return true;
		}

		if (InToken.Equals(TEXT("Y"), ESearchCase::CaseSensitive)
			|| InToken.Equals(TEXT("+Y"), ESearchCase::CaseSensitive)
			|| InToken.Equals(TEXT("Y+"), ESearchCase::CaseSensitive)
			|| InToken.Equals(TEXT("RIGHT"), ESearchCase::CaseSensitive)
			|| InToken.Equals(TEXT("+RIGHT"), ESearchCase::CaseSensitive)
			|| InToken.Equals(TEXT("RIGHT+"), ESearchCase::CaseSensitive))
		{
			OutCanonicalAxis = TEXT("Y");
			return true;
		}

		if (InToken.Equals(TEXT("-Y"), ESearchCase::CaseSensitive)
			|| InToken.Equals(TEXT("Y-"), ESearchCase::CaseSensitive)
			|| InToken.Equals(TEXT("-RIGHT"), ESearchCase::CaseSensitive)
			|| InToken.Equals(TEXT("RIGHT-"), ESearchCase::CaseSensitive)
			|| InToken.Equals(TEXT("LEFT"), ESearchCase::CaseSensitive))
		{
			OutCanonicalAxis = TEXT("-Y");
			return true;
		}

		if (InToken.Equals(TEXT("Z"), ESearchCase::CaseSensitive)
			|| InToken.Equals(TEXT("+Z"), ESearchCase::CaseSensitive)
			|| InToken.Equals(TEXT("Z+"), ESearchCase::CaseSensitive)
			|| InToken.Equals(TEXT("UP"), ESearchCase::CaseSensitive)
			|| InToken.Equals(TEXT("+UP"), ESearchCase::CaseSensitive)
			|| InToken.Equals(TEXT("UP+"), ESearchCase::CaseSensitive))
		{
			OutCanonicalAxis = TEXT("Z");
			return true;
		}

		if (InToken.Equals(TEXT("-Z"), ESearchCase::CaseSensitive)
			|| InToken.Equals(TEXT("Z-"), ESearchCase::CaseSensitive)
			|| InToken.Equals(TEXT("-UP"), ESearchCase::CaseSensitive)
			|| InToken.Equals(TEXT("UP-"), ESearchCase::CaseSensitive)
			|| InToken.Equals(TEXT("DOWN"), ESearchCase::CaseSensitive))
		{
			OutCanonicalAxis = TEXT("-Z");
			return true;
		}

		return false;
	}

	static bool TryGetForwardVectorForCanonicalAxis(const FString& InCanonicalAxis, FVector& OutForwardVector)
	{
		if (InCanonicalAxis.Equals(TEXT("X"), ESearchCase::CaseSensitive))
		{
			OutForwardVector = FVector::ForwardVector;
			return true;
		}
		if (InCanonicalAxis.Equals(TEXT("-X"), ESearchCase::CaseSensitive))
		{
			OutForwardVector = -FVector::ForwardVector;
			return true;
		}
		if (InCanonicalAxis.Equals(TEXT("Y"), ESearchCase::CaseSensitive))
		{
			OutForwardVector = FVector::RightVector;
			return true;
		}
		if (InCanonicalAxis.Equals(TEXT("-Y"), ESearchCase::CaseSensitive))
		{
			OutForwardVector = -FVector::RightVector;
			return true;
		}
		if (InCanonicalAxis.Equals(TEXT("Z"), ESearchCase::CaseSensitive))
		{
			OutForwardVector = FVector::UpVector;
			return true;
		}
		if (InCanonicalAxis.Equals(TEXT("-Z"), ESearchCase::CaseSensitive))
		{
			OutForwardVector = -FVector::UpVector;
			return true;
		}

		return false;
	}
}

namespace BAT::ForwardAxis
{
	FString GetValidationMessage()
	{
		return TEXT("'forward_axis' must resolve to a forward axis such as 'X', '-Y', '+Z', 'forward', 'left', or 'down'");
	}

	bool TryNormalizeAxis(const FString& InAxis, FString& OutCanonicalAxis, FString& OutError)
	{
		OutCanonicalAxis.Reset();
		OutError.Reset();

		const FString Token = NormalizeAxisToken(InAxis);
		if (TryGetCanonicalAxisToken(Token, OutCanonicalAxis))
		{
			return true;
		}

		OutError = GetValidationMessage();
		return false;
	}

	bool TryBuildAxisToUnrealQuat(const FString& InAxis, FQuat& OutQuat, FString& OutCanonicalAxis, FString& OutError)
	{
		OutQuat = FQuat::Identity;
		if (!TryNormalizeAxis(InAxis, OutCanonicalAxis, OutError))
		{
			return false;
		}

		FVector SourceForwardVector = FVector::ForwardVector;
		if (!TryGetForwardVectorForCanonicalAxis(OutCanonicalAxis, SourceForwardVector))
		{
			OutError = GetValidationMessage();
			return false;
		}

		if (SourceForwardVector.Equals(FVector::ForwardVector))
		{
			return true;
		}

		if (SourceForwardVector.Equals(-FVector::ForwardVector))
		{
			OutQuat = FQuat(FVector::UpVector, PI);
			return true;
		}

		OutQuat = FQuat::FindBetweenNormals(SourceForwardVector, FVector::ForwardVector);
		return true;
	}

	bool TryBuildAxisToUnrealQuat(const FString& InAxis, FQuat& OutQuat, FString& OutError)
	{
		FString IgnoredCanonicalAxis;
		return TryBuildAxisToUnrealQuat(InAxis, OutQuat, IgnoredCanonicalAxis, OutError);
	}
}
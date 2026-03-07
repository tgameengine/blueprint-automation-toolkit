#pragma once

#include "CoreMinimal.h"

namespace BAT::ForwardAxis
{
	FString GetValidationMessage();
	bool TryNormalizeAxis(const FString& InAxis, FString& OutCanonicalAxis, FString& OutError);
	bool TryBuildAxisToUnrealQuat(const FString& InAxis, FQuat& OutQuat, FString& OutCanonicalAxis, FString& OutError);
	bool TryBuildAxisToUnrealQuat(const FString& InAxis, FQuat& OutQuat, FString& OutError);
}
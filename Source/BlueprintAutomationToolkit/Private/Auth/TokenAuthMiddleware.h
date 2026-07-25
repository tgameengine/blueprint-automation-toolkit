// Copyright 2026 AkaSoft. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FBlueprintAutomationToolkitModule;
struct FHttpServerRequest;

class FTokenAuthMiddleware
{
public:
	bool Authorize(const FBlueprintAutomationToolkitModule& Module, const FHttpServerRequest& Request, FString* OutDenyReason, FString* OutClientKey) const;
};
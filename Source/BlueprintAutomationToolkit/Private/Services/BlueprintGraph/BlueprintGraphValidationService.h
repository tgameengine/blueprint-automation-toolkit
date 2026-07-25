// Copyright 2026 AkaSoft. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FBlueprintGraphApplyRequest;
struct FBlueprintGraphApplyResult;
class UBlueprint;
class UEdGraph;
class FJsonValue;

struct FBlueprintGraphResolvedTarget
{
	UBlueprint* Blueprint = nullptr;
	UEdGraph* Graph = nullptr;
	FString BlueprintObjectPath;
};

class FBlueprintGraphValidationService
{
public:
	static bool PrepareApplyTarget(const FBlueprintGraphApplyRequest& Request, FBlueprintGraphResolvedTarget& OutTarget, FBlueprintGraphApplyResult& OutResult);
	static TSharedPtr<FJsonValue> MakeApplyResultData(const FString& BlueprintObjectPath, const FString& GraphName, const FBlueprintGraphApplyResult& Result);
};
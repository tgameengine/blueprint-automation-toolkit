#pragma once

#include "CoreMinimal.h"

struct FAutomationResult;
struct FBlueprintGraphApplyRequest;
class UBlueprint;
class UEdGraph;
class UEdGraphNode;

struct FBlueprintGraphApplyResult
{
	bool bOk = false;
	TArray<FString> CreatedNodes;
	TArray<FString> UpdatedNodes;
	int32 CreatedLinks = 0;
	TArray<FString> Warnings;
	TArray<FString> Errors;
};

class FBlueprintGraphService
{
public:
	static FAutomationResult ApplyGraphPatch(const FBlueprintGraphApplyRequest& Request);
};

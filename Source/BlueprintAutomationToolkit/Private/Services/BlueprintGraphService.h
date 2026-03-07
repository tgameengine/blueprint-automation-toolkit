#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"

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
	FString CompileStatus = TEXT("not_requested");
	FString SaveStatus = TEXT("not_requested");
	int32 CompileErrorCount = 0;
	int32 CompileWarningCount = 0;
	TArray<TSharedPtr<FJsonValue>> CompileErrors;
	TArray<TSharedPtr<FJsonValue>> CompileWarnings;
};

class FBlueprintGraphService
{
public:
	static FAutomationResult ApplyGraphPatch(const FBlueprintGraphApplyRequest& Request);
};

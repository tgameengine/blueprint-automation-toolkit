#pragma once

#include "CoreMinimal.h"

struct FBlueprintGraphApplyLinkSpec;
struct FBlueprintGraphApplyResult;
class UEdGraph;
class UEdGraphNode;

class FBlueprintGraphLinkService
{
public:
	static void ApplyLinks(UEdGraph* Graph, const TArray<FBlueprintGraphApplyLinkSpec>& LinkSpecs, const TMap<FString, UEdGraphNode*>& NodeById, bool bDryRun, FBlueprintGraphApplyResult& InOutResult);
};
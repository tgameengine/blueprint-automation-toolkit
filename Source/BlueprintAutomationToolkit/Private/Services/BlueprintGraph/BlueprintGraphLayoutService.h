#pragma once

#include "CoreMinimal.h"

struct FBlueprintGraphApplyLinkSpec;
struct FBlueprintGraphApplyNodeSpec;
class UEdGraphNode;
class UEdGraph;

class FBlueprintGraphLayoutService
{
public:
	static void ApplyNodeLayout(UEdGraphNode* Node, const FBlueprintGraphApplyNodeSpec& NodeSpec);
	static void AutoArrangeCreatedNodes(UEdGraph* Graph, const TArray<FBlueprintGraphApplyNodeSpec>& NodeSpecs, const TArray<FBlueprintGraphApplyLinkSpec>& LinkSpecs, const TMap<FString, UEdGraphNode*>& NodeById, const TSet<FString>& CreatedNodeIds);
};
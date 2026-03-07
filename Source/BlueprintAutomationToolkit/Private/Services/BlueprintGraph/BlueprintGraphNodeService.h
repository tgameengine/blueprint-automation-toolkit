#pragma once

#include "CoreMinimal.h"

struct FBlueprintGraphApplyNodeSpec;
struct FBlueprintGraphApplyResult;
class UBlueprint;
class UEdGraph;
class UEdGraphNode;
class UEdGraphPin;

class FBlueprintGraphNodeService
{
public:
	static void ApplyNodes(UBlueprint* Blueprint, UEdGraph* Graph, const TArray<FBlueprintGraphApplyNodeSpec>& NodeSpecs, bool bWillMutate, FBlueprintGraphApplyResult& InOutResult, TMap<FString, UEdGraphNode*>& OutNodeById);
	static UEdGraphNode* FindNodeByUasId(UEdGraph* Graph, const FString& NodeId);
	static void SetNodeUasId(UEdGraphNode* Node, const FString& NodeId);
	static UEdGraphNode* ResolveNodeReferenceInGraph(UEdGraph* Graph, const TMap<FString, UEdGraphNode*>& RequestNodeMap, const FString& NodeId);
	static UEdGraphPin* FindPinSmart(UEdGraphNode* Node, const FString& PinName);
};
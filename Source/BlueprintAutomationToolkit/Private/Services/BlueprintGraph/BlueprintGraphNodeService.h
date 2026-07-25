// Copyright 2026 AkaSoft. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Services/BlueprintGraphService.h"

struct FBlueprintGraphApplyNodeSpec;
struct FBlueprintGraphApplyResult;
struct FBlueprintGraphNodeValidationIssue;
class UBlueprint;
class UEdGraph;
class UEdGraphNode;
class UEdGraphPin;

class FBlueprintGraphNodeService
{
public:
	static void ApplyNodes(UBlueprint* Blueprint, UEdGraph* Graph, const TArray<FBlueprintGraphApplyNodeSpec>& NodeSpecs, bool bWillMutate, bool bCreateMissingNodes, FBlueprintGraphApplyResult& InOutResult, TMap<FString, UEdGraphNode*>& OutNodeById, TSet<FString>& OutCreatedNodeIds);
	static UEdGraphNode* FindNodeByUasId(UEdGraph* Graph, const FString& NodeId);
	static bool TryGetNodeUasId(UEdGraphNode* Node, FString& OutNodeId);
	static void SetNodeUasId(UEdGraphNode* Node, const FString& NodeId);
	static UEdGraphNode* ResolveNodeReferenceInGraph(UEdGraph* Graph, const TMap<FString, UEdGraphNode*>& RequestNodeMap, const FString& NodeId);
	static UEdGraphPin* FindPinSmart(UEdGraphNode* Node, const FString& PinName);
	static bool TryExportNodePropertyText(UEdGraphNode* Node, const FString& PropertyPath, FString& OutValue, FString& OutError);
	static void GetDefaultInspectionPropertyPaths(UEdGraphNode* Node, TArray<FString>& OutPropertyPaths);
	static void CollectNodeValidationIssues(UEdGraphNode* Node, const FString& NodeId, TArray<FBlueprintGraphNodeValidationIssue>& OutIssues);
};
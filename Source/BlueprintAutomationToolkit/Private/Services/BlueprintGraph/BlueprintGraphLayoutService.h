// Copyright 2026 AkaSoft. All Rights Reserved.

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
	static void AutoArrangeNodes(UEdGraph* Graph, const TMap<FString, UEdGraphNode*>& NodeById, const TSet<FString>& NodeIdsToArrange, bool bPreserveFeederLanes = false);
};
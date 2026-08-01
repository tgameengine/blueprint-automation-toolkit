// Copyright 2026 AkaSoft. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FBlueprintGraphApplyOptions;
struct FBlueprintGraphApplyLinkSpec;
struct FBlueprintGraphApplyResult;
class UBlueprint;
class UEdGraph;
class UEdGraphNode;

class FBlueprintGraphFinalizeService
{
public:
	static void Finalize(UBlueprint* Blueprint, UEdGraph* Graph, const TArray<FBlueprintGraphApplyLinkSpec>& LinkSpecs, const TMap<FString, UEdGraphNode*>& NodeById, const FBlueprintGraphApplyOptions& Options, FBlueprintGraphApplyResult& InOutResult);
};

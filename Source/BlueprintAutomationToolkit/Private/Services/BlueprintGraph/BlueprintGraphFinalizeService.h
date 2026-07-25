// Copyright 2026 AkaSoft. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FBlueprintGraphApplyOptions;
struct FBlueprintGraphApplyResult;
class UBlueprint;

class FBlueprintGraphFinalizeService
{
public:
	static void Finalize(UBlueprint* Blueprint, const FBlueprintGraphApplyOptions& Options, FBlueprintGraphApplyResult& InOutResult);
};
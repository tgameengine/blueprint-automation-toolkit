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
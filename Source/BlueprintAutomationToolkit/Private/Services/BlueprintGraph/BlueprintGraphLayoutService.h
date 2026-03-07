#pragma once

#include "CoreMinimal.h"

struct FBlueprintGraphApplyNodeSpec;
class UEdGraphNode;

class FBlueprintGraphLayoutService
{
public:
	static void ApplyNodeLayout(UEdGraphNode* Node, const FBlueprintGraphApplyNodeSpec& NodeSpec);
};
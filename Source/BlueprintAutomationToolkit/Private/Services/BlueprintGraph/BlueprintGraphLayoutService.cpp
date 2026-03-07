#include "Services/BlueprintGraph/BlueprintGraphLayoutService.h"

#include "EdGraph/EdGraphNode.h"
#include "Routes/Blueprint/BlueprintGraphApplyRequest.h"

void FBlueprintGraphLayoutService::ApplyNodeLayout(UEdGraphNode* Node, const FBlueprintGraphApplyNodeSpec& NodeSpec)
{
	if (!Node)
	{
		return;
	}

	Node->NodePosX = NodeSpec.X;
	Node->NodePosY = NodeSpec.Y;
}
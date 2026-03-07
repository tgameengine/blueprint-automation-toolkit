#include "Services/BlueprintGraphService.h"

#include "Commands/AutomationCommand.h"
#include "Routes/Blueprint/BlueprintGraphApplyRequest.h"
#include "ScopedTransaction.h"
#include "Services/BlueprintGraph/BlueprintGraphFinalizeService.h"
#include "Services/BlueprintGraph/BlueprintGraphLayoutService.h"
#include "Services/BlueprintGraph/BlueprintGraphLinkService.h"
#include "Services/BlueprintGraph/BlueprintGraphNodeService.h"
#include "Services/BlueprintGraph/BlueprintGraphValidationService.h"

namespace
{
	static void ExpandConnectedBatNodeIds(UEdGraph* Graph, TMap<FString, UEdGraphNode*>& InOutNodeById, TSet<FString>& InOutNodeIds)
	{
		if (!Graph || InOutNodeIds.Num() == 0)
		{
			return;
		}

		TArray<FString> Frontier = InOutNodeIds.Array();
		for (int32 Index = 0; Index < Frontier.Num(); ++Index)
		{
			const FString CurrentNodeId = Frontier[Index];
			UEdGraphNode* CurrentNode = FBlueprintGraphNodeService::ResolveNodeReferenceInGraph(Graph, InOutNodeById, CurrentNodeId);
			if (!CurrentNode)
			{
				continue;
			}

			for (UEdGraphPin* Pin : CurrentNode->Pins)
			{
				if (!Pin)
				{
					continue;
				}

				for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
				{
					UEdGraphNode* NeighborNode = LinkedPin ? LinkedPin->GetOwningNode() : nullptr;
					FString NeighborNodeId;
					if (!NeighborNode || !FBlueprintGraphNodeService::TryGetNodeUasId(NeighborNode, NeighborNodeId) || NeighborNodeId.IsEmpty())
					{
						continue;
					}

					if (!InOutNodeIds.Contains(NeighborNodeId))
					{
						InOutNodeIds.Add(NeighborNodeId);
						InOutNodeById.Add(NeighborNodeId, NeighborNode);
						Frontier.Add(NeighborNodeId);
					}
				}
			}
		}
	}
}

FAutomationResult FBlueprintGraphService::ApplyGraphPatch(const FBlueprintGraphApplyRequest& Request)
{
	FBlueprintGraphApplyResult ApplyResult;
	FBlueprintGraphResolvedTarget Target;
	if (!FBlueprintGraphValidationService::PrepareApplyTarget(Request, Target, ApplyResult))
	{
		return FAutomationResult::Ok(FBlueprintGraphValidationService::MakeApplyResultData(Target.BlueprintObjectPath, Request.GraphName, ApplyResult), 400);
	}

	const bool bWillMutate = !Request.Options.bDryRun;
	TOptional<FScopedTransaction> Transaction;
	if (bWillMutate && Request.Options.bUseTransaction)
	{
		Transaction.Emplace(FText::FromString(TEXT("BAT Apply Blueprint Graph")));
	}

	if (bWillMutate)
	{
		Target.Blueprint->Modify();
		Target.Graph->Modify();
	}

	TMap<FString, UEdGraphNode*> NodeById;
	TSet<FString> CreatedNodeIds;
	FBlueprintGraphNodeService::ApplyNodes(Target.Blueprint, Target.Graph, Request.Nodes, bWillMutate, Request.Options.bCreateMissingNodes, ApplyResult, NodeById, CreatedNodeIds);
	FBlueprintGraphLinkService::ApplyLinks(Target.Graph, Request.Links, NodeById, Request.Options.bDryRun, ApplyResult);
	if (bWillMutate)
	{
		TSet<FString> NodeIdsToArrange = CreatedNodeIds;
		if (Request.Options.bAutoArrangeExistingNodes)
		{
			for (const FBlueprintGraphApplyNodeSpec& NodeSpec : Request.Nodes)
			{
				if (!NodeSpec.bHasExplicitX && !NodeSpec.bHasExplicitY && NodeById.Contains(NodeSpec.Id))
				{
					NodeIdsToArrange.Add(NodeSpec.Id);
				}
			}
		}
		if (Request.Options.bAutoArrangeConnectedNodes)
		{
			ExpandConnectedBatNodeIds(Target.Graph, NodeById, NodeIdsToArrange);
		}
		FBlueprintGraphLayoutService::AutoArrangeNodes(Target.Graph, NodeById, NodeIdsToArrange);
	}
	if (bWillMutate)
	{
		FBlueprintGraphFinalizeService::Finalize(Target.Blueprint, Request.Options, ApplyResult);
	}

	ApplyResult.bOk = ApplyResult.Errors.Num() == 0 && ApplyResult.CompileErrors.Num() == 0;
	return FAutomationResult::Ok(FBlueprintGraphValidationService::MakeApplyResultData(Target.BlueprintObjectPath, Request.GraphName, ApplyResult));
}

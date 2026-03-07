#include "Services/BlueprintGraph/BlueprintGraphLayoutService.h"

#include "EdGraph/EdGraphNode.h"
#include "Math/UnrealMathUtility.h"
#include "Routes/Blueprint/BlueprintGraphApplyRequest.h"
#include "Services/BlueprintGraph/BlueprintGraphNodeService.h"

namespace
{
	static constexpr int32 BatLayoutGridSize = 32;
	static constexpr int32 BatLayoutHorizontalSpacing = 320;
	static constexpr int32 BatLayoutVerticalSpacing = 180;

	static int32 SnapToGrid(const int32 Value)
	{
		return FMath::GridSnap(Value, BatLayoutGridSize);
	}

	static FString MakeAnchorKey(const TCHAR* Prefix, const TArray<FString>& NodeIds)
	{
		TArray<FString> SortedNodeIds = NodeIds;
		SortedNodeIds.Sort();
		return FString::Printf(TEXT("%s:%s"), Prefix, *FString::Join(SortedNodeIds, TEXT("|")));
	}

	static FString GetNodeLayoutId(UEdGraphNode* Node)
	{
		return Node ? Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphensLower) : FString();
	}
}

void FBlueprintGraphLayoutService::ApplyNodeLayout(UEdGraphNode* Node, const FBlueprintGraphApplyNodeSpec& NodeSpec)
{
	if (!Node)
	{
		return;
	}

	if (NodeSpec.bHasExplicitX)
	{
		Node->NodePosX = NodeSpec.X;
	}
	if (NodeSpec.bHasExplicitY)
	{
		Node->NodePosY = NodeSpec.Y;
	}
}

void FBlueprintGraphLayoutService::AutoArrangeNodes(UEdGraph* Graph, const TMap<FString, UEdGraphNode*>& NodeById, const TSet<FString>& NodeIdsToArrange)
{
	if (!Graph || NodeIdsToArrange.Num() == 0)
	{
		return;
	}

	TMap<UEdGraphNode*, FString> IdByNode;
	for (const TPair<FString, UEdGraphNode*>& Pair : NodeById)
	{
		if (Pair.Value)
		{
			IdByNode.Add(Pair.Value, Pair.Key);
		}
	}

	TMap<FString, TArray<FString>> IncomingByNode;
	TMap<FString, TArray<FString>> OutgoingByNode;
	for (UEdGraphNode* GraphNode : Graph->Nodes)
	{
		if (!GraphNode)
		{
			continue;
		}

		const FString* FromNodeId = IdByNode.Find(GraphNode);
		if (!FromNodeId)
		{
			continue;
		}

		for (UEdGraphPin* Pin : GraphNode->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Output)
			{
				continue;
			}

			for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				if (!LinkedPin || !LinkedPin->GetOwningNode())
				{
					continue;
				}

				const FString* ToNodeId = IdByNode.Find(LinkedPin->GetOwningNode());
				if (!ToNodeId)
				{
					continue;
				}

				OutgoingByNode.FindOrAdd(*FromNodeId).Add(*ToNodeId);
				IncomingByNode.FindOrAdd(*ToNodeId).Add(*FromNodeId);
			}
		}
	}

	TSet<FString> PositionedIds;
	int32 MaxNodePosX = 0;
	int32 MaxNodePosY = 0;
	for (UEdGraphNode* GraphNode : Graph->Nodes)
	{
		if (!GraphNode)
		{
			continue;
		}

		MaxNodePosX = FMath::Max(MaxNodePosX, GraphNode->NodePosX);
		MaxNodePosY = FMath::Max(MaxNodePosY, GraphNode->NodePosY);
		if (const FString* StableId = IdByNode.Find(GraphNode))
		{
			if (!NodeIdsToArrange.Contains(*StableId))
			{
				PositionedIds.Add(*StableId);
			}
		}
	}

	TMap<FString, int32> AnchorUsageCounts;
	for (int32 PassIndex = 0; PassIndex < NodeIdsToArrange.Num(); ++PassIndex)
	{
		bool bPlacedNodeInPass = false;
		for (const FString& NodeId : NodeIdsToArrange)
		{
			if (PositionedIds.Contains(NodeId))
			{
				continue;
			}

			UEdGraphNode* const* NodePtr = NodeById.Find(NodeId);
			if (!NodePtr || !*NodePtr)
			{
				continue;
			}

			TArray<UEdGraphNode*> IncomingAnchors;
			TArray<UEdGraphNode*> OutgoingAnchors;
			TArray<FString> IncomingAnchorIds;
			TArray<FString> OutgoingAnchorIds;

			if (const TArray<FString>* IncomingIds = IncomingByNode.Find(NodeId))
			{
				for (const FString& IncomingId : *IncomingIds)
				{
					if (!PositionedIds.Contains(IncomingId))
					{
						continue;
					}

					if (UEdGraphNode* AnchorNode = FBlueprintGraphNodeService::ResolveNodeReferenceInGraph(Graph, NodeById, IncomingId))
					{
						IncomingAnchors.Add(AnchorNode);
						IncomingAnchorIds.Add(IncomingId);
					}
				}
			}

			if (const TArray<FString>* OutgoingIds = OutgoingByNode.Find(NodeId))
			{
				for (const FString& OutgoingId : *OutgoingIds)
				{
					if (!PositionedIds.Contains(OutgoingId))
					{
						continue;
					}

					if (UEdGraphNode* AnchorNode = FBlueprintGraphNodeService::ResolveNodeReferenceInGraph(Graph, NodeById, OutgoingId))
					{
						OutgoingAnchors.Add(AnchorNode);
						OutgoingAnchorIds.Add(OutgoingId);
					}
				}
			}

			if (IncomingAnchors.Num() == 0 && OutgoingAnchors.Num() == 0)
			{
				continue;
			}

			int32 TargetX = (*NodePtr)->NodePosX;
			int32 TargetY = (*NodePtr)->NodePosY;
			FString AnchorKey;

			if (IncomingAnchors.Num() > 0)
			{
				int32 SumX = 0;
				int32 SumY = 0;
				for (UEdGraphNode* AnchorNode : IncomingAnchors)
				{
					SumX += AnchorNode->NodePosX;
					SumY += AnchorNode->NodePosY;
				}
				TargetX = FMath::RoundToInt((double)SumX / IncomingAnchors.Num()) + BatLayoutHorizontalSpacing;
				TargetY = FMath::RoundToInt((double)SumY / IncomingAnchors.Num());
				AnchorKey = MakeAnchorKey(TEXT("in"), IncomingAnchorIds);
			}
			else if (OutgoingAnchors.Num() > 0)
			{
				int32 SumX = 0;
				int32 SumY = 0;
				for (UEdGraphNode* AnchorNode : OutgoingAnchors)
				{
					SumX += AnchorNode->NodePosX;
					SumY += AnchorNode->NodePosY;
				}
				TargetX = FMath::RoundToInt((double)SumX / OutgoingAnchors.Num()) - BatLayoutHorizontalSpacing;
				TargetY = FMath::RoundToInt((double)SumY / OutgoingAnchors.Num());
				AnchorKey = MakeAnchorKey(TEXT("out"), OutgoingAnchorIds);
			}

			const int32 LaneIndex = AnchorUsageCounts.FindOrAdd(AnchorKey)++;
			TargetY += LaneIndex * BatLayoutVerticalSpacing;
			(*NodePtr)->NodePosX = SnapToGrid(TargetX);
			(*NodePtr)->NodePosY = SnapToGrid(TargetY);
			PositionedIds.Add(NodeId);
			bPlacedNodeInPass = true;
		}

		if (!bPlacedNodeInPass)
		{
			break;
		}
	}

	int32 FallbackIndex = 0;
	const int32 FallbackX = SnapToGrid(MaxNodePosX + BatLayoutHorizontalSpacing);
	for (const FString& NodeId : NodeIdsToArrange)
	{
		if (PositionedIds.Contains(NodeId))
		{
			continue;
		}

		if (UEdGraphNode* const* NodePtr = NodeById.Find(NodeId))
		{
			if (*NodePtr)
			{
				(*NodePtr)->NodePosX = FallbackX;
				(*NodePtr)->NodePosY = SnapToGrid(MaxNodePosY + (FallbackIndex * BatLayoutVerticalSpacing));
				++FallbackIndex;
			}
		}
	}
}
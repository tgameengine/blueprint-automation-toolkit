#include "Services/BlueprintGraph/BlueprintGraphLayoutService.h"

#include "Algo/Sort.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
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

	struct FNodeAnchor
	{
		int32 X = 0;
		int32 Y = 0;
	};

	static void AddAnchor(TMap<FString, TArray<FNodeAnchor>>& AnchorMap, const FString& NodeId, UEdGraphNode* AnchorNode)
	{
		if (!AnchorNode)
		{
			return;
		}

		FNodeAnchor& Anchor = AnchorMap.FindOrAdd(NodeId).AddDefaulted_GetRef();
		Anchor.X = AnchorNode->NodePosX;
		Anchor.Y = AnchorNode->NodePosY;
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
	TMap<FString, TArray<FNodeAnchor>> ExternalIncomingAnchors;
	TMap<FString, TArray<FNodeAnchor>> ExternalOutgoingAnchors;
	for (UEdGraphNode* GraphNode : Graph->Nodes)
	{
		if (!GraphNode)
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

				const FString* FromNodeId = IdByNode.Find(GraphNode);
				const FString* ToNodeId = IdByNode.Find(LinkedPin->GetOwningNode());
				const bool bFromArranged = FromNodeId && NodeIdsToArrange.Contains(*FromNodeId);
				const bool bToArranged = ToNodeId && NodeIdsToArrange.Contains(*ToNodeId);
				if (!bFromArranged && !bToArranged)
				{
					continue;
				}

				if (bFromArranged && bToArranged)
				{
					OutgoingByNode.FindOrAdd(*FromNodeId).Add(*ToNodeId);
					IncomingByNode.FindOrAdd(*ToNodeId).Add(*FromNodeId);
				}
				else if (bFromArranged)
				{
					AddAnchor(ExternalOutgoingAnchors, *FromNodeId, LinkedPin->GetOwningNode());
				}
				else if (bToArranged)
				{
					AddAnchor(ExternalIncomingAnchors, *ToNodeId, GraphNode);
				}
			}
		}
	}

	TMap<FString, int32> CurrentXById;
	TMap<FString, int32> CurrentYById;
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
			CurrentXById.Add(*StableId, GraphNode->NodePosX);
			CurrentYById.Add(*StableId, GraphNode->NodePosY);
			if (!NodeIdsToArrange.Contains(*StableId))
			{
				PositionedIds.Add(*StableId);
			}
		}
	}

	TMap<FString, int32> InternalIndegree;
	for (const FString& NodeId : NodeIdsToArrange)
	{
		InternalIndegree.Add(NodeId, IncomingByNode.FindRef(NodeId).Num());
	}

	TArray<FString> PendingRoots;
	for (const FString& NodeId : NodeIdsToArrange)
	{
		if (InternalIndegree.FindRef(NodeId) == 0)
		{
			PendingRoots.Add(NodeId);
		}
	}

	PendingRoots.Sort([&CurrentYById](const FString& A, const FString& B)
	{
		return CurrentYById.FindRef(A) < CurrentYById.FindRef(B);
	});

	TMap<FString, int32> DepthByNode;
	TArray<FString> Frontier = PendingRoots;
	while (Frontier.Num() > 0)
	{
		const FString NodeId = Frontier[0];
		Frontier.RemoveAt(0);
		const int32 NodeDepth = DepthByNode.FindRef(NodeId);
		for (const FString& ChildId : OutgoingByNode.FindRef(NodeId))
		{
			if (!NodeIdsToArrange.Contains(ChildId))
			{
				continue;
			}
			DepthByNode.FindOrAdd(ChildId) = FMath::Max(DepthByNode.FindRef(ChildId), NodeDepth + 1);
			int32& ChildIndegree = InternalIndegree.FindOrAdd(ChildId);
			ChildIndegree = FMath::Max(0, ChildIndegree - 1);
			if (ChildIndegree == 0)
			{
				Frontier.Add(ChildId);
			}
		}
	}

		for (const FString& NodeId : NodeIdsToArrange)
		{
			if (!DepthByNode.Contains(NodeId))
			{
				DepthByNode.Add(NodeId, 0);
			}
		}

		int32 MaxDepth = 0;
		for (const TPair<FString, int32>& Pair : DepthByNode)
		{
			MaxDepth = FMath::Max(MaxDepth, Pair.Value);
		}

		bool bHasIncomingExternalAnchor = false;
		double IncomingExternalOriginX = 0.0;
		int32 IncomingExternalCount = 0;
		for (const FString& NodeId : NodeIdsToArrange)
		{
			for (const FNodeAnchor& Anchor : ExternalIncomingAnchors.FindRef(NodeId))
			{
				IncomingExternalOriginX += Anchor.X;
				++IncomingExternalCount;
				bHasIncomingExternalAnchor = true;
			}
		}

		bool bHasOutgoingExternalAnchor = false;
		double OutgoingExternalOriginX = 0.0;
		int32 OutgoingExternalCount = 0;
		for (const FString& NodeId : NodeIdsToArrange)
		{
			for (const FNodeAnchor& Anchor : ExternalOutgoingAnchors.FindRef(NodeId))
			{
				OutgoingExternalOriginX += Anchor.X;
				++OutgoingExternalCount;
				bHasOutgoingExternalAnchor = true;
			}
		}

		int32 OriginX = 0;
		if (bHasIncomingExternalAnchor && IncomingExternalCount > 0)
		{
			OriginX = SnapToGrid(FMath::RoundToInt(IncomingExternalOriginX / IncomingExternalCount) + BatLayoutHorizontalSpacing);
		}
		else if (bHasOutgoingExternalAnchor && OutgoingExternalCount > 0)
		{
			OriginX = SnapToGrid(FMath::RoundToInt(OutgoingExternalOriginX / OutgoingExternalCount) - ((MaxDepth + 1) * BatLayoutHorizontalSpacing));
		}
		else
		{
			int32 MinCurrentX = TNumericLimits<int32>::Max();
			for (const FString& NodeId : NodeIdsToArrange)
			{
				MinCurrentX = FMath::Min(MinCurrentX, CurrentXById.FindRef(NodeId));
			}
			OriginX = SnapToGrid(MinCurrentX == TNumericLimits<int32>::Max() ? 0 : MinCurrentX);
		}

		TMap<int32, TArray<FString>> NodesByDepth;
		for (const FString& NodeId : NodeIdsToArrange)
		{
			NodesByDepth.FindOrAdd(DepthByNode.FindRef(NodeId)).Add(NodeId);
		}

		TMap<FString, int32> FinalYById;
		for (int32 Depth = 0; Depth <= MaxDepth; ++Depth)
		{
			TArray<FString>& NodesAtDepth = NodesByDepth.FindOrAdd(Depth);
			if (NodesAtDepth.Num() == 0)
			{
				continue;
			}

			TMap<FString, double> DesiredYById;
			for (const FString& NodeId : NodesAtDepth)
			{
				double SumY = 0.0;
				int32 Count = 0;
				for (const FString& IncomingId : IncomingByNode.FindRef(NodeId))
				{
					if (FinalYById.Contains(IncomingId))
					{
						SumY += FinalYById.FindRef(IncomingId);
						++Count;
					}
				}
				if (Count == 0)
				{
					for (const FNodeAnchor& Anchor : ExternalIncomingAnchors.FindRef(NodeId))
					{
						SumY += Anchor.Y;
						++Count;
					}
				}
				if (Count == 0)
				{
					for (const FNodeAnchor& Anchor : ExternalOutgoingAnchors.FindRef(NodeId))
					{
						SumY += Anchor.Y;
						++Count;
					}
				}
				DesiredYById.Add(NodeId, Count > 0 ? (SumY / Count) : CurrentYById.FindRef(NodeId));
			}

			Algo::Sort(NodesAtDepth, [&DesiredYById](const FString& A, const FString& B)
			{
				return DesiredYById.FindRef(A) < DesiredYById.FindRef(B);
			});

			double MeanDesiredY = 0.0;
			for (const FString& NodeId : NodesAtDepth)
			{
				MeanDesiredY += DesiredYById.FindRef(NodeId);
			}
			MeanDesiredY /= NodesAtDepth.Num();
			const double StartY = MeanDesiredY - ((NodesAtDepth.Num() - 1) * BatLayoutVerticalSpacing * 0.5);

			for (int32 Index = 0; Index < NodesAtDepth.Num(); ++Index)
			{
				const FString& NodeId = NodesAtDepth[Index];
				if (UEdGraphNode* const* NodePtr = NodeById.Find(NodeId))
				{
					if (*NodePtr)
					{
						(*NodePtr)->NodePosX = OriginX + (Depth * BatLayoutHorizontalSpacing);
						(*NodePtr)->NodePosY = SnapToGrid(FMath::RoundToInt(StartY + (Index * BatLayoutVerticalSpacing)));
						FinalYById.Add(NodeId, (*NodePtr)->NodePosY);
					}
				}
			}
		}
}
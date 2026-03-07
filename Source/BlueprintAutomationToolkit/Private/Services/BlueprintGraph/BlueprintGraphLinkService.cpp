#include "Services/BlueprintGraph/BlueprintGraphLinkService.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "Routes/Blueprint/BlueprintGraphApplyRequest.h"
#include "Services/BlueprintGraph/BlueprintGraphNodeService.h"
#include "Services/BlueprintGraphService.h"

namespace
{
	static bool TrySplitPinAddress(const FString& Address, FString& OutNodeId, FString& OutPinName)
	{
		int32 DotIndex = INDEX_NONE;
		if (!Address.FindLastChar(TEXT('.'), DotIndex) || DotIndex <= 0 || DotIndex >= Address.Len() - 1)
		{
			return false;
		}
		OutNodeId = Address.Left(DotIndex);
		OutPinName = Address.Mid(DotIndex + 1);
		OutNodeId.TrimStartAndEndInline();
		OutPinName.TrimStartAndEndInline();
		return !OutNodeId.IsEmpty() && !OutPinName.IsEmpty();
	}
}

void FBlueprintGraphLinkService::ApplyLinks(UEdGraph* Graph, const TArray<FBlueprintGraphApplyLinkSpec>& LinkSpecs, const TMap<FString, UEdGraphNode*>& NodeById, bool bDryRun, FBlueprintGraphApplyResult& InOutResult)
{
	if (!Graph)
	{
		return;
	}

	if (bDryRun)
	{
		for (const FBlueprintGraphApplyLinkSpec& LinkSpec : LinkSpecs)
		{
			FString FromNodeId;
			FString FromPinName;
			FString ToNodeId;
			FString ToPinName;
			if (!TrySplitPinAddress(LinkSpec.From, FromNodeId, FromPinName) || !TrySplitPinAddress(LinkSpec.To, ToNodeId, ToPinName))
			{
				InOutResult.Errors.Add(TEXT("invalid_link_address"));
				continue;
			}

			if (!FBlueprintGraphNodeService::ResolveNodeReferenceInGraph(Graph, NodeById, FromNodeId)
				|| !FBlueprintGraphNodeService::ResolveNodeReferenceInGraph(Graph, NodeById, ToNodeId))
			{
				InOutResult.Errors.Add(FString::Printf(TEXT("link_node_not_found:%s->%s"), *LinkSpec.From, *LinkSpec.To));
				continue;
			}

			++InOutResult.CreatedLinks;
		}
		return;
	}

	const UEdGraphSchema* Schema = Graph->GetSchema();
	for (const FBlueprintGraphApplyLinkSpec& LinkSpec : LinkSpecs)
	{
		FString FromNodeId;
		FString FromPinName;
		FString ToNodeId;
		FString ToPinName;
		if (!TrySplitPinAddress(LinkSpec.From, FromNodeId, FromPinName) || !TrySplitPinAddress(LinkSpec.To, ToNodeId, ToPinName))
		{
			InOutResult.Errors.Add(FString::Printf(TEXT("invalid_link_address:%s->%s"), *LinkSpec.From, *LinkSpec.To));
			continue;
		}

		UEdGraphNode* FromNode = FBlueprintGraphNodeService::ResolveNodeReferenceInGraph(Graph, NodeById, FromNodeId);
		UEdGraphNode* ToNode = FBlueprintGraphNodeService::ResolveNodeReferenceInGraph(Graph, NodeById, ToNodeId);
		if (!FromNode || !ToNode)
		{
			InOutResult.Errors.Add(FString::Printf(TEXT("link_node_not_found:%s->%s"), *LinkSpec.From, *LinkSpec.To));
			continue;
		}

		UEdGraphPin* FromPin = FBlueprintGraphNodeService::FindPinSmart(FromNode, FromPinName);
		UEdGraphPin* ToPin = FBlueprintGraphNodeService::FindPinSmart(ToNode, ToPinName);
		if (!FromPin)
		{
			InOutResult.Errors.Add(FString::Printf(TEXT("link_from_pin_not_found:node=%s,pin=%s"), *FromNodeId, *FromPinName));
		}
		if (!ToPin)
		{
			InOutResult.Errors.Add(FString::Printf(TEXT("link_to_pin_not_found:node=%s,pin=%s"), *ToNodeId, *ToPinName));
		}
		if (!FromPin || !ToPin)
		{
			continue;
		}

		const bool bLinked = Schema ? Schema->TryCreateConnection(FromPin, ToPin) : false;
		if (!bLinked)
		{
			InOutResult.Warnings.Add(FString::Printf(TEXT("link_failed:%s->%s"), *LinkSpec.From, *LinkSpec.To));
			continue;
		}

		++InOutResult.CreatedLinks;
	}
}
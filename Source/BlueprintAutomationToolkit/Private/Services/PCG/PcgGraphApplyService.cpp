#include "Services/PCG/PcgGraphApplyService.h"

#include "Routes/PCG/PcgApplyRequest.h"
#include "Services/PCG/PcgGraphAssetService.h"
#include "Services/PCG/PcgNodeRegistry.h"

#include "Dom/JsonObject.h"
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGSettings.h"

namespace
{
	static const TCHAR* BAT_NodeIdPrefix = TEXT("BAT_ID:");

	static FString MakeManagedNodeComment(const FString& NodeId)
	{
		return FString::Printf(TEXT("%s%s"), BAT_NodeIdPrefix, *NodeId);
	}

	static bool IsManagedNodeCommentForId(const FString& Comment, const FString& NodeId)
	{
		return Comment.Equals(MakeManagedNodeComment(NodeId), ESearchCase::CaseSensitive);
	}

	static UPCGNode* FindManagedNodeById(UPCGGraph* Graph, const FString& NodeId)
	{
		if (!Graph || NodeId.IsEmpty())
		{
			return nullptr;
		}

		for (UPCGNode* Node : Graph->GetNodes())
		{
			if (Node && IsManagedNodeCommentForId(Node->NodeComment, NodeId))
			{
				return Node;
			}
		}

		return nullptr;
	}

	static bool HasNonEmptySettings(const TSharedPtr<FJsonObject>& SettingsObj)
	{
		return SettingsObj.IsValid() && SettingsObj->Values.Num() > 0;
	}

	static FAutomationResult ApplyNodesAdd(const FPcgApplyOpSpec& Op, FPcgGraphAssetHandle& GraphHandle)
	{
		if (!GraphHandle.Graph)
		{
			return FAutomationResult::Error(TEXT("graph_not_ready"), TEXT("PCG graph is not available for node creation."), 500);
		}

		const FPcgNodeFamilySpec* NodeSpec = FPcgNodeRegistry::FindByExternalType(Op.Type);
		if (!NodeSpec || !NodeSpec->SettingsClass)
		{
			return FAutomationResult::Error(TEXT("unsupported_node_type"), FString::Printf(TEXT("Unsupported PCG node family: %s"), *Op.Type), 400);
		}

		if (HasNonEmptySettings(Op.Settings))
		{
			return FAutomationResult::Error(TEXT("not_implemented"), TEXT("nodes.add settings application is not implemented yet."), 501);
		}

		GraphHandle.Graph->Modify();
		UPCGNode* Node = FindManagedNodeById(GraphHandle.Graph, Op.Id);
		if (!Node)
		{
			UPCGSettings* DefaultSettings = nullptr;
			Node = GraphHandle.Graph->AddNodeOfType(NodeSpec->SettingsClass, DefaultSettings);
			if (!Node)
			{
				return FAutomationResult::Error(TEXT("node_create_failed"), FString::Printf(TEXT("Failed to create PCG node for family: %s"), *Op.Type), 500);
			}
		}
		else
		{
			UPCGSettings* ExistingSettings = Node->GetSettings();
			if (!ExistingSettings || !ExistingSettings->IsA(NodeSpec->SettingsClass))
			{
				return FAutomationResult::Error(TEXT("node_type_mismatch"), TEXT("Existing managed node id resolves to a different PCG node family."), 409);
			}
		}

		Node->Modify();
		Node->NodeComment = MakeManagedNodeComment(Op.Id);
		Node->bCommentBubbleVisible = false;
		Node->bCommentBubblePinned = false;
		if (Op.bHasExplicitX || Op.bHasExplicitY)
		{
			int32 ExistingX = 0;
			int32 ExistingY = 0;
			Node->GetNodePosition(ExistingX, ExistingY);
			Node->SetNodePosition(Op.bHasExplicitX ? Op.X : ExistingX, Op.bHasExplicitY ? Op.Y : ExistingY);
		}

		GraphHandle.Graph->GetOutermost()->MarkPackageDirty();
		return FAutomationResult::Ok(nullptr);
	}
}

FAutomationResult FPcgGraphApplyService::ApplyOps(const FPcgApplyRequest& Request, FPcgGraphAssetHandle& GraphHandle)
{
	for (const FPcgApplyOpSpec& Op : Request.Ops)
	{
		if (Op.Op.Equals(TEXT("parameters.set"), ESearchCase::CaseSensitive))
		{
			continue;
		}

		if (Op.Op.Equals(TEXT("spawners.set_mesh_set"), ESearchCase::CaseSensitive))
		{
			return FAutomationResult::Error(TEXT("not_implemented"), TEXT("spawners.set_mesh_set is not implemented yet."), 501);
		}

		if (Op.Op.Equals(TEXT("nodes.add"), ESearchCase::CaseSensitive))
		{
			const FAutomationResult Result = ApplyNodesAdd(Op, GraphHandle);
			if (!Result.bSuccess)
			{
				return Result;
			}
			continue;
		}

		return FAutomationResult::Error(TEXT("not_implemented"), FString::Printf(TEXT("PCG op '%s' is not implemented yet."), *Op.Op), 501);
	}

	return FAutomationResult::Ok(nullptr);
}
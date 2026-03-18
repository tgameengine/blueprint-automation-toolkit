#include "Services/PCG/PcgGraphApplyService.h"

#include "Routes/PCG/PcgApplyRequest.h"
#include "Services/PCG/PcgGraphAssetService.h"
#include "Services/PCG/PcgNodeRegistry.h"

#include "Dom/JsonObject.h"
#include "Elements/ControlFlow/PCGBranch.h"
#include "Elements/PCGAttributeFilter.h"
#include "Elements/PCGDifferenceElement.h"
#include "Elements/PCGStaticMeshSpawner.h"
#include "Elements/PCGSurfaceSampler.h"
#include "Elements/PCGTransformPoints.h"
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

	static bool TryParsePinAddress(const FString& Address, FString& OutNodeId, FName& OutPinLabel)
	{
		OutNodeId.Reset();
		OutPinLabel = NAME_None;

		int32 DotIndex = INDEX_NONE;
		if (!Address.FindLastChar(TEXT('.'), DotIndex) || DotIndex <= 0 || DotIndex >= Address.Len() - 1)
		{
			return false;
		}

		OutNodeId = Address.Left(DotIndex).TrimStartAndEnd();
		const FString PinLabel = Address.Mid(DotIndex + 1).TrimStartAndEnd();
		if (OutNodeId.IsEmpty() || PinLabel.IsEmpty())
		{
			return false;
		}

		OutPinLabel = FName(*PinLabel);
		return OutPinLabel != NAME_None;
	}

	static bool TryGetBoolValue(const TSharedPtr<FJsonValue>& Value, bool& OutValue)
	{
		if (!Value.IsValid() || Value->Type != EJson::Boolean)
		{
			return false;
		}

		OutValue = Value->AsBool();
		return true;
	}

	static bool TryGetNumberValue(const TSharedPtr<FJsonValue>& Value, double& OutValue)
	{
		if (!Value.IsValid() || Value->Type != EJson::Number)
		{
			return false;
		}

		OutValue = Value->AsNumber();
		return true;
	}

	static bool TryGetStringValue(const TSharedPtr<FJsonValue>& Value, FString& OutValue)
	{
		if (!Value.IsValid() || Value->Type != EJson::String)
		{
			return false;
		}

		OutValue = Value->AsString();
		OutValue.TrimStartAndEndInline();
		return !OutValue.IsEmpty();
	}

	static FAutomationResult MakeUnsupportedSettingResult(const FString& ExternalType, const FString& SettingKey)
	{
		return FAutomationResult::Error(TEXT("unsupported_setting"), FString::Printf(TEXT("Unsupported setting '%s' for PCG node family '%s'"), *SettingKey, *ExternalType), 400);
	}

	static FAutomationResult MakeInvalidSettingValueResult(const FString& ExternalType, const FString& SettingKey)
	{
		return FAutomationResult::Error(TEXT("invalid_request"), FString::Printf(TEXT("Invalid value for setting '%s' on PCG node family '%s'"), *SettingKey, *ExternalType), 400);
	}

	static FAutomationResult ApplySurfaceSamplerSetting(UPCGSurfaceSamplerSettings* Settings, const FString& SettingKey, const TSharedPtr<FJsonValue>& Value)
	{
		if (SettingKey.Equals(TEXT("points_per_squared_meter"), ESearchCase::CaseSensitive))
		{
			double NumberValue = 0.0;
			if (!TryGetNumberValue(Value, NumberValue))
			{
				return MakeInvalidSettingValueResult(TEXT("SurfaceSampler"), SettingKey);
			}
			Settings->PointsPerSquaredMeter = static_cast<float>(NumberValue);
			return FAutomationResult::Ok(nullptr);
		}
		if (SettingKey.Equals(TEXT("looseness"), ESearchCase::CaseSensitive))
		{
			double NumberValue = 0.0;
			if (!TryGetNumberValue(Value, NumberValue))
			{
				return MakeInvalidSettingValueResult(TEXT("SurfaceSampler"), SettingKey);
			}
			Settings->Looseness = static_cast<float>(NumberValue);
			return FAutomationResult::Ok(nullptr);
		}
		if (SettingKey.Equals(TEXT("point_steepness"), ESearchCase::CaseSensitive))
		{
			double NumberValue = 0.0;
			if (!TryGetNumberValue(Value, NumberValue))
			{
				return MakeInvalidSettingValueResult(TEXT("SurfaceSampler"), SettingKey);
			}
			Settings->PointSteepness = static_cast<float>(NumberValue);
			return FAutomationResult::Ok(nullptr);
		}

		bool BoolValue = false;
		if (SettingKey.Equals(TEXT("unbounded"), ESearchCase::CaseSensitive))
		{
			if (!TryGetBoolValue(Value, BoolValue))
			{
				return MakeInvalidSettingValueResult(TEXT("SurfaceSampler"), SettingKey);
			}
			Settings->bUnbounded = BoolValue;
			return FAutomationResult::Ok(nullptr);
		}
		if (SettingKey.Equals(TEXT("apply_density_to_points"), ESearchCase::CaseSensitive))
		{
			if (!TryGetBoolValue(Value, BoolValue))
			{
				return MakeInvalidSettingValueResult(TEXT("SurfaceSampler"), SettingKey);
			}
			Settings->bApplyDensityToPoints = BoolValue;
			return FAutomationResult::Ok(nullptr);
		}
		if (SettingKey.Equals(TEXT("use_legacy_grid_creation_method"), ESearchCase::CaseSensitive))
		{
			if (!TryGetBoolValue(Value, BoolValue))
			{
				return MakeInvalidSettingValueResult(TEXT("SurfaceSampler"), SettingKey);
			}
			Settings->bUseLegacyGridCreationMethod = BoolValue;
			return FAutomationResult::Ok(nullptr);
		}

		return MakeUnsupportedSettingResult(TEXT("SurfaceSampler"), SettingKey);
	}

	static FAutomationResult ApplyBranchSetting(UPCGBranchSettings* Settings, const FString& SettingKey, const TSharedPtr<FJsonValue>& Value)
	{
		if (SettingKey.Equals(TEXT("output_to_b"), ESearchCase::CaseSensitive))
		{
			bool BoolValue = false;
			if (!TryGetBoolValue(Value, BoolValue))
			{
				return MakeInvalidSettingValueResult(TEXT("Branch"), SettingKey);
			}
			Settings->bOutputToB = BoolValue;
			return FAutomationResult::Ok(nullptr);
		}

		return MakeUnsupportedSettingResult(TEXT("Branch"), SettingKey);
	}

	static FAutomationResult ApplyStaticMeshSpawnerSetting(UPCGStaticMeshSpawnerSettings* Settings, const FString& SettingKey, const TSharedPtr<FJsonValue>& Value)
	{
		if (SettingKey.Equals(TEXT("out_attribute_name"), ESearchCase::CaseSensitive))
		{
			FString StringValue;
			if (!TryGetStringValue(Value, StringValue))
			{
				return MakeInvalidSettingValueResult(TEXT("StaticMeshSpawner"), SettingKey);
			}
			Settings->OutAttributeName = FName(*StringValue);
			return FAutomationResult::Ok(nullptr);
		}

		bool BoolValue = false;
		auto ApplyBool = [&](bool& Target) -> FAutomationResult
		{
			if (!TryGetBoolValue(Value, BoolValue))
			{
				return MakeInvalidSettingValueResult(TEXT("StaticMeshSpawner"), SettingKey);
			}
			Target = BoolValue;
			return FAutomationResult::Ok(nullptr);
		};

		if (SettingKey.Equals(TEXT("apply_mesh_bounds_to_points"), ESearchCase::CaseSensitive)) return ApplyBool(Settings->bApplyMeshBoundsToPoints);
		if (SettingKey.Equals(TEXT("synchronous_load"), ESearchCase::CaseSensitive)) return ApplyBool(Settings->bSynchronousLoad);
		if (SettingKey.Equals(TEXT("allow_merge_different_data_in_same_instanced_components"), ESearchCase::CaseSensitive)) return ApplyBool(Settings->bAllowMergeDifferentDataInSameInstancedComponents);
		if (SettingKey.Equals(TEXT("silence_override_attribute_not_found_errors"), ESearchCase::CaseSensitive)) return ApplyBool(Settings->bSilenceOverrideAttributeNotFoundErrors);
		if (SettingKey.Equals(TEXT("warn_on_identical_spawn"), ESearchCase::CaseSensitive)) return ApplyBool(Settings->bWarnOnIdenticalSpawn);

		return MakeUnsupportedSettingResult(TEXT("StaticMeshSpawner"), SettingKey);
	}

	static FAutomationResult ApplyDifferenceSetting(UPCGDifferenceSettings* Settings, const FString& SettingKey, const TSharedPtr<FJsonValue>& Value)
	{
		bool BoolValue = false;
		if (!TryGetBoolValue(Value, BoolValue))
		{
			return MakeInvalidSettingValueResult(TEXT("Difference"), SettingKey);
		}

		if (SettingKey.Equals(TEXT("diff_metadata"), ESearchCase::CaseSensitive))
		{
			Settings->bDiffMetadata = BoolValue;
			return FAutomationResult::Ok(nullptr);
		}
		if (SettingKey.Equals(TEXT("keep_zero_density_points"), ESearchCase::CaseSensitive))
		{
			Settings->bKeepZeroDensityPoints = BoolValue;
			return FAutomationResult::Ok(nullptr);
		}

		return MakeUnsupportedSettingResult(TEXT("Difference"), SettingKey);
	}

	static FAutomationResult ApplyTransformPointsSetting(UPCGTransformPointsSettings* Settings, const FString& SettingKey, const TSharedPtr<FJsonValue>& Value)
	{
		if (SettingKey.Equals(TEXT("attribute_name"), ESearchCase::CaseSensitive))
		{
			FString StringValue;
			if (!TryGetStringValue(Value, StringValue))
			{
				return MakeInvalidSettingValueResult(TEXT("TransformPoints"), SettingKey);
			}
			Settings->AttributeName = FName(*StringValue);
			return FAutomationResult::Ok(nullptr);
		}

		bool BoolValue = false;
		if (!TryGetBoolValue(Value, BoolValue))
		{
			return MakeInvalidSettingValueResult(TEXT("TransformPoints"), SettingKey);
		}

		if (SettingKey.Equals(TEXT("apply_to_attribute"), ESearchCase::CaseSensitive)) { Settings->bApplyToAttribute = BoolValue; return FAutomationResult::Ok(nullptr); }
		if (SettingKey.Equals(TEXT("absolute_offset"), ESearchCase::CaseSensitive)) { Settings->bAbsoluteOffset = BoolValue; return FAutomationResult::Ok(nullptr); }
		if (SettingKey.Equals(TEXT("absolute_rotation"), ESearchCase::CaseSensitive)) { Settings->bAbsoluteRotation = BoolValue; return FAutomationResult::Ok(nullptr); }
		if (SettingKey.Equals(TEXT("absolute_scale"), ESearchCase::CaseSensitive)) { Settings->bAbsoluteScale = BoolValue; return FAutomationResult::Ok(nullptr); }
		if (SettingKey.Equals(TEXT("uniform_scale"), ESearchCase::CaseSensitive)) { Settings->bUniformScale = BoolValue; return FAutomationResult::Ok(nullptr); }
		if (SettingKey.Equals(TEXT("recompute_seed"), ESearchCase::CaseSensitive)) { Settings->bRecomputeSeed = BoolValue; return FAutomationResult::Ok(nullptr); }

		return MakeUnsupportedSettingResult(TEXT("TransformPoints"), SettingKey);
	}

	static FAutomationResult ApplyAttributeFilterSetting(UPCGAttributeFilteringSettings* Settings, const FString& SettingKey, const TSharedPtr<FJsonValue>& Value)
	{
		bool BoolValue = false;
		if (!TryGetBoolValue(Value, BoolValue))
		{
			return MakeInvalidSettingValueResult(TEXT("AttributeFilter"), SettingKey);
		}

		if (SettingKey.Equals(TEXT("use_constant_threshold"), ESearchCase::CaseSensitive)) { Settings->bUseConstantThreshold = BoolValue; return FAutomationResult::Ok(nullptr); }
		if (SettingKey.Equals(TEXT("warn_on_data_missing_attribute"), ESearchCase::CaseSensitive)) { Settings->bWarnOnDataMissingAttribute = BoolValue; return FAutomationResult::Ok(nullptr); }
		if (SettingKey.Equals(TEXT("generate_output_data_even_if_empty"), ESearchCase::CaseSensitive)) { Settings->bGenerateOutputDataEvenIfEmpty = BoolValue; return FAutomationResult::Ok(nullptr); }

		return MakeUnsupportedSettingResult(TEXT("AttributeFilter"), SettingKey);
	}

	static FAutomationResult ApplySettingsToNode(UPCGNode* Node, const TSharedPtr<FJsonObject>& SettingsObj)
	{
		if (!Node)
		{
			return FAutomationResult::Error(TEXT("node_not_found"), TEXT("Target PCG node could not be resolved."), 404);
		}

		UPCGSettings* Settings = Node->GetSettings();
		const FPcgNodeFamilySpec* NodeSpec = FPcgNodeRegistry::FindBySettingsClass(Settings ? Settings->GetClass() : nullptr);
		if (!Settings || !NodeSpec)
		{
			return FAutomationResult::Error(TEXT("unsupported_node_type"), TEXT("Target PCG node family is not supported for settings application."), 400);
		}

		if (!SettingsObj.IsValid())
		{
			return FAutomationResult::Ok(nullptr);
		}

		Settings->Modify();
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : SettingsObj->Values)
		{
			if (!NodeSpec->SupportedSettingKeys.Contains(Pair.Key))
			{
				return MakeUnsupportedSettingResult(NodeSpec->ExternalType, Pair.Key);
			}

			FAutomationResult SettingResult = FAutomationResult::Error(TEXT("unsupported_setting"), TEXT("Unsupported setting."), 400);
			if (UPCGSurfaceSamplerSettings* SurfaceSampler = Cast<UPCGSurfaceSamplerSettings>(Settings))
			{
				SettingResult = ApplySurfaceSamplerSetting(SurfaceSampler, Pair.Key, Pair.Value);
			}
			else if (UPCGBranchSettings* Branch = Cast<UPCGBranchSettings>(Settings))
			{
				SettingResult = ApplyBranchSetting(Branch, Pair.Key, Pair.Value);
			}
			else if (UPCGStaticMeshSpawnerSettings* StaticMeshSpawner = Cast<UPCGStaticMeshSpawnerSettings>(Settings))
			{
				SettingResult = ApplyStaticMeshSpawnerSetting(StaticMeshSpawner, Pair.Key, Pair.Value);
			}
			else if (UPCGDifferenceSettings* Difference = Cast<UPCGDifferenceSettings>(Settings))
			{
				SettingResult = ApplyDifferenceSetting(Difference, Pair.Key, Pair.Value);
			}
			else if (UPCGTransformPointsSettings* TransformPoints = Cast<UPCGTransformPointsSettings>(Settings))
			{
				SettingResult = ApplyTransformPointsSetting(TransformPoints, Pair.Key, Pair.Value);
			}
			else if (UPCGAttributeFilteringSettings* AttributeFilter = Cast<UPCGAttributeFilteringSettings>(Settings))
			{
				SettingResult = ApplyAttributeFilterSetting(AttributeFilter, Pair.Key, Pair.Value);
			}

			if (!SettingResult.bSuccess)
			{
				return SettingResult;
			}
		}

		return FAutomationResult::Ok(nullptr);
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
			// Settings are applied after the node exists and before the graph is saved.
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

		const FAutomationResult SettingsResult = ApplySettingsToNode(Node, Op.Settings);
		if (!SettingsResult.bSuccess)
		{
			return SettingsResult;
		}

		GraphHandle.Graph->GetOutermost()->MarkPackageDirty();
		return FAutomationResult::Ok(nullptr);
	}

	static FAutomationResult ApplyNodesSet(const FPcgApplyOpSpec& Op, FPcgGraphAssetHandle& GraphHandle)
	{
		if (!GraphHandle.Graph)
		{
			return FAutomationResult::Error(TEXT("graph_not_ready"), TEXT("PCG graph is not available for settings updates."), 500);
		}

		UPCGNode* Node = FindManagedNodeById(GraphHandle.Graph, Op.Node);
		if (!Node)
		{
			return FAutomationResult::Error(TEXT("unknown_node_reference"), FString::Printf(TEXT("Unknown managed PCG node id: %s"), *Op.Node), 404);
		}

		const FAutomationResult SettingsResult = ApplySettingsToNode(Node, Op.Settings);
		if (!SettingsResult.bSuccess)
		{
			return SettingsResult;
		}

		GraphHandle.Graph->Modify();
		GraphHandle.Graph->GetOutermost()->MarkPackageDirty();
		return FAutomationResult::Ok(nullptr);
	}

	static FAutomationResult ApplyNodesRemove(const FPcgApplyOpSpec& Op, FPcgGraphAssetHandle& GraphHandle)
	{
		if (!GraphHandle.Graph)
		{
			return FAutomationResult::Error(TEXT("graph_not_ready"), TEXT("PCG graph is not available for node removal."), 500);
		}

		UPCGNode* Node = FindManagedNodeById(GraphHandle.Graph, Op.Node);
		if (!Node)
		{
			return FAutomationResult::Error(TEXT("unknown_node_reference"), FString::Printf(TEXT("Unknown managed PCG node id: %s"), *Op.Node), 404);
		}

		GraphHandle.Graph->Modify();
		Node->Modify();
		GraphHandle.Graph->RemoveNode(Node);
		GraphHandle.Graph->GetOutermost()->MarkPackageDirty();
		return FAutomationResult::Ok(nullptr);
	}

	static FAutomationResult ApplyEdgesConnect(const FPcgApplyOpSpec& Op, FPcgGraphAssetHandle& GraphHandle)
	{
		if (!GraphHandle.Graph)
		{
			return FAutomationResult::Error(TEXT("graph_not_ready"), TEXT("PCG graph is not available for edge creation."), 500);
		}

		FString FromNodeId;
		FName FromPinLabel = NAME_None;
		FString ToNodeId;
		FName ToPinLabel = NAME_None;
		if (!TryParsePinAddress(Op.From, FromNodeId, FromPinLabel) || !TryParsePinAddress(Op.To, ToNodeId, ToPinLabel))
		{
			return FAutomationResult::Error(TEXT("invalid_pin_reference"), TEXT("Edge pin references must use '<nodeId>.<pinLabel>' format."), 400);
		}

		UPCGNode* FromNode = FindManagedNodeById(GraphHandle.Graph, FromNodeId);
		if (!FromNode)
		{
			return FAutomationResult::Error(TEXT("unknown_node_reference"), FString::Printf(TEXT("Unknown managed PCG node id: %s"), *FromNodeId), 404);
		}

		UPCGNode* ToNode = FindManagedNodeById(GraphHandle.Graph, ToNodeId);
		if (!ToNode)
		{
			return FAutomationResult::Error(TEXT("unknown_node_reference"), FString::Printf(TEXT("Unknown managed PCG node id: %s"), *ToNodeId), 404);
		}

		UPCGPin* FromPin = FromNode->GetOutputPin(FromPinLabel);
		UPCGPin* ToPin = ToNode->GetInputPin(ToPinLabel);
		if (!FromPin)
		{
			return FAutomationResult::Error(TEXT("invalid_pin_reference"), FString::Printf(TEXT("Output pin '%s' does not exist on node '%s'"), *FromPinLabel.ToString(), *FromNodeId), 400);
		}
		if (!ToPin)
		{
			return FAutomationResult::Error(TEXT("invalid_pin_reference"), FString::Printf(TEXT("Input pin '%s' does not exist on node '%s'"), *ToPinLabel.ToString(), *ToNodeId), 400);
		}

		if (!FromPin->CanConnect(ToPin))
		{
			return FAutomationResult::Error(TEXT("edge_connect_failed"), TEXT("PCG pins are not compatible for connection."), 400);
		}

		GraphHandle.Graph->Modify();
		FromNode->Modify();
		ToNode->Modify();
		const bool bConnected = FromPin->AddEdgeTo(ToPin);
		if (!bConnected && !FromPin->IsConnected())
		{
			return FAutomationResult::Error(TEXT("edge_connect_failed"), TEXT("Failed to create PCG graph edge."), 500);
		}

		GraphHandle.Graph->GetOutermost()->MarkPackageDirty();
		return FAutomationResult::Ok(nullptr);
	}

	static FAutomationResult ApplyEdgesDisconnect(const FPcgApplyOpSpec& Op, FPcgGraphAssetHandle& GraphHandle)
	{
		if (!GraphHandle.Graph)
		{
			return FAutomationResult::Error(TEXT("graph_not_ready"), TEXT("PCG graph is not available for edge removal."), 500);
		}

		FString FromNodeId;
		FName FromPinLabel = NAME_None;
		FString ToNodeId;
		FName ToPinLabel = NAME_None;
		if (!TryParsePinAddress(Op.From, FromNodeId, FromPinLabel) || !TryParsePinAddress(Op.To, ToNodeId, ToPinLabel))
		{
			return FAutomationResult::Error(TEXT("invalid_pin_reference"), TEXT("Edge pin references must use '<nodeId>.<pinLabel>' format."), 400);
		}

		UPCGNode* FromNode = FindManagedNodeById(GraphHandle.Graph, FromNodeId);
		if (!FromNode)
		{
			return FAutomationResult::Error(TEXT("unknown_node_reference"), FString::Printf(TEXT("Unknown managed PCG node id: %s"), *FromNodeId), 404);
		}

		UPCGNode* ToNode = FindManagedNodeById(GraphHandle.Graph, ToNodeId);
		if (!ToNode)
		{
			return FAutomationResult::Error(TEXT("unknown_node_reference"), FString::Printf(TEXT("Unknown managed PCG node id: %s"), *ToNodeId), 404);
		}

		UPCGPin* FromPin = FromNode->GetOutputPin(FromPinLabel);
		UPCGPin* ToPin = ToNode->GetInputPin(ToPinLabel);
		if (!FromPin)
		{
			return FAutomationResult::Error(TEXT("invalid_pin_reference"), FString::Printf(TEXT("Output pin '%s' does not exist on node '%s'"), *FromPinLabel.ToString(), *FromNodeId), 400);
		}
		if (!ToPin)
		{
			return FAutomationResult::Error(TEXT("invalid_pin_reference"), FString::Printf(TEXT("Input pin '%s' does not exist on node '%s'"), *ToPinLabel.ToString(), *ToNodeId), 400);
		}

		GraphHandle.Graph->Modify();
		FromNode->Modify();
		ToNode->Modify();
		const bool bDisconnected = FromPin->BreakEdgeTo(ToPin);
		if (!bDisconnected && (FromPin->IsConnected() || ToPin->IsConnected()))
		{
			return FAutomationResult::Error(TEXT("edge_disconnect_failed"), TEXT("Failed to remove PCG graph edge."), 500);
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

		if (Op.Op.Equals(TEXT("nodes.set"), ESearchCase::CaseSensitive))
		{
			const FAutomationResult Result = ApplyNodesSet(Op, GraphHandle);
			if (!Result.bSuccess)
			{
				return Result;
			}
			continue;
		}

		if (Op.Op.Equals(TEXT("nodes.remove"), ESearchCase::CaseSensitive))
		{
			const FAutomationResult Result = ApplyNodesRemove(Op, GraphHandle);
			if (!Result.bSuccess)
			{
				return Result;
			}
			continue;
		}

		if (Op.Op.Equals(TEXT("edges.connect"), ESearchCase::CaseSensitive))
		{
			const FAutomationResult Result = ApplyEdgesConnect(Op, GraphHandle);
			if (!Result.bSuccess)
			{
				return Result;
			}
			continue;
		}

		if (Op.Op.Equals(TEXT("edges.disconnect"), ESearchCase::CaseSensitive))
		{
			const FAutomationResult Result = ApplyEdgesDisconnect(Op, GraphHandle);
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
// Copyright 2026 AkaSoft. All Rights Reserved.

#include "Commands/Blueprint/ReadGraphCommand.h"

#include "Core/EditorExecution.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/PackageName.h"
#include "Services/BlueprintGraph/BlueprintGraphNodeService.h"
#include "Services/BlueprintGraphService.h"
#include "Services/Reflection/ReflectionTypes.h"

namespace
{
	static FString NormalizeBlueprintObjectPath(const FString& InPath)
	{
		FString Path = InPath;
		Path.TrimStartAndEndInline();
		if (Path.IsEmpty())
		{
			return Path;
		}

		if (!Path.Contains(TEXT(".")))
		{
			const FString AssetName = FPackageName::GetLongPackageAssetName(Path);
			if (!AssetName.IsEmpty())
			{
				Path = FString::Printf(TEXT("%s.%s"), *Path, *AssetName);
			}
		}

		return Path;
	}

	static void GetAllBlueprintGraphs(UBlueprint* Blueprint, TArray<UEdGraph*>& OutGraphs)
	{
		OutGraphs.Reset();
		if (!Blueprint)
		{
			return;
		}

		auto AddGraphRecursive = [&OutGraphs](UEdGraph* RootGraph)
		{
			if (!RootGraph || OutGraphs.Contains(RootGraph))
			{
				return;
			}

			OutGraphs.Add(RootGraph);
			TArray<UEdGraph*> ChildGraphs;
			RootGraph->GetAllChildrenGraphs(ChildGraphs);
			for (UEdGraph* ChildGraph : ChildGraphs)
			{
				if (ChildGraph && !OutGraphs.Contains(ChildGraph))
				{
					OutGraphs.Add(ChildGraph);
				}
			}
		};

		for (UEdGraph* Graph : Blueprint->UbergraphPages)
		{
			AddGraphRecursive(Graph);
		}
		for (UEdGraph* Graph : Blueprint->FunctionGraphs)
		{
			AddGraphRecursive(Graph);
		}
		for (UEdGraph* Graph : Blueprint->MacroGraphs)
		{
			AddGraphRecursive(Graph);
		}
	}

	static UEdGraph* FindGraphByName(UBlueprint* Blueprint, const FString& GraphName)
	{
		TArray<UEdGraph*> Graphs;
		GetAllBlueprintGraphs(Blueprint, Graphs);
		if (GraphName.IsEmpty())
		{
			return Graphs.Num() > 0 ? Graphs[0] : nullptr;
		}

		for (UEdGraph* Graph : Graphs)
		{
			if (Graph && Graph->GetName().Equals(GraphName, ESearchCase::CaseSensitive))
			{
				return Graph;
			}
		}

		return nullptr;
	}

	static FString GetStableNodeId(UEdGraphNode* Node)
	{
		if (!Node)
		{
			return FString();
		}

		if (Node->NodeGuid.IsValid())
		{
			return Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphensLower);
		}

		return Node->GetName();
	}

	static FString GetGraphKind(UBlueprint* Blueprint, UEdGraph* Graph)
	{
		if (!Blueprint || !Graph)
		{
			return TEXT("unknown");
		}

		if (Blueprint->UbergraphPages.Contains(Graph))
		{
			return TEXT("event_graph");
		}
		if (Blueprint->FunctionGraphs.Contains(Graph))
		{
			return TEXT("function_graph");
		}
		if (Blueprint->MacroGraphs.Contains(Graph))
		{
			return TEXT("macro_graph");
		}

		return TEXT("graph");
	}

	static TSharedRef<FJsonObject> DescribePin(UEdGraphPin* Pin)
	{
		TSharedRef<FJsonObject> PinObj = MakeShared<FJsonObject>();
		PinObj->SetStringField(TEXT("name"), Pin ? Pin->PinName.ToString() : FString());
		PinObj->SetStringField(TEXT("direction"), (Pin && Pin->Direction == EGPD_Output) ? TEXT("output") : TEXT("input"));
		PinObj->SetStringField(TEXT("category"), Pin ? Pin->PinType.PinCategory.ToString() : FString());
		PinObj->SetStringField(TEXT("subCategory"), Pin ? Pin->PinType.PinSubCategory.ToString() : FString());
		PinObj->SetBoolField(TEXT("isArray"), Pin ? Pin->PinType.ContainerType == EPinContainerType::Array : false);
		PinObj->SetBoolField(TEXT("isReference"), Pin ? Pin->PinType.bIsReference : false);
		PinObj->SetNumberField(TEXT("linkedCount"), Pin ? Pin->LinkedTo.Num() : 0);
		PinObj->SetStringField(TEXT("defaultValue"), Pin ? Pin->DefaultValue : FString());
		return PinObj;
	}

	static TSharedRef<FJsonObject> DescribeNode(UEdGraphNode* Node)
	{
		TSharedRef<FJsonObject> NodeObj = MakeShared<FJsonObject>();
		NodeObj->SetStringField(TEXT("id"), GetStableNodeId(Node));
		NodeObj->SetStringField(TEXT("name"), Node ? Node->GetName() : FString());
		NodeObj->SetStringField(TEXT("type"), (Node && Node->GetClass()) ? Node->GetClass()->GetPathName() : FString());
		NodeObj->SetStringField(TEXT("title"), Node ? Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString() : FString());
		NodeObj->SetNumberField(TEXT("x"), Node ? Node->NodePosX : 0);
		NodeObj->SetNumberField(TEXT("y"), Node ? Node->NodePosY : 0);
		NodeObj->SetStringField(TEXT("comment"), Node ? Node->NodeComment : FString());

		TArray<TSharedPtr<FJsonValue>> Pins;
		if (Node)
		{
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin)
				{
					Pins.Add(MakeShared<FJsonValueObject>(DescribePin(Pin)));
				}
			}
		}
		NodeObj->SetArrayField(TEXT("pins"), Pins);
		return NodeObj;
	}

	static TSharedRef<FJsonObject> MakeNodeValidationIssueObject(const FBlueprintGraphNodeValidationIssue& Issue)
	{
		TSharedRef<FJsonObject> IssueObj = MakeShared<FJsonObject>();
		IssueObj->SetStringField(TEXT("nodeId"), Issue.NodeId);
		IssueObj->SetStringField(TEXT("code"), Issue.Code);
		IssueObj->SetStringField(TEXT("message"), Issue.Message);
		IssueObj->SetStringField(TEXT("severity"), Issue.Severity);
		if (!Issue.PropertyPath.IsEmpty())
		{
			IssueObj->SetStringField(TEXT("propertyPath"), Issue.PropertyPath);
		}
		return IssueObj;
	}

	static bool IsPosePin(const UEdGraphPin* Pin)
	{
		if (!Pin)
		{
			return false;
		}

		const FString PinName = Pin->PinName.ToString();
		return PinName.Equals(TEXT("Pose"), ESearchCase::CaseSensitive)
			|| PinName.Equals(TEXT("ComponentPose"), ESearchCase::CaseSensitive)
			|| PinName.Equals(TEXT("LocalPose"), ESearchCase::CaseSensitive)
			|| PinName.Equals(TEXT("BasePose"), ESearchCase::CaseSensitive)
			|| PinName.Equals(TEXT("Result"), ESearchCase::CaseSensitive)
			|| PinName.Equals(TEXT("Source"), ESearchCase::CaseSensitive)
			|| PinName.StartsWith(TEXT("BlendPoses_"), ESearchCase::CaseSensitive);
	}

	static FString InferPoseSpaceForPin(const UEdGraphNode* Node, const UEdGraphPin* Pin)
	{
		if (!Node || !Pin || !IsPosePin(Pin) || !Node->GetClass())
		{
			return TEXT("unknown");
		}

		const FString PinName = Pin->PinName.ToString();
		const FString ClassName = Node->GetClass()->GetName();
		if (PinName.Equals(TEXT("ComponentPose"), ESearchCase::CaseSensitive))
		{
			return TEXT("component");
		}
		if (PinName.Equals(TEXT("LocalPose"), ESearchCase::CaseSensitive))
		{
			return TEXT("local");
		}
		if (ClassName.Equals(TEXT("AnimGraphNode_LocalToComponentSpace"), ESearchCase::CaseSensitive))
		{
			return Pin->Direction == EGPD_Output ? TEXT("component") : TEXT("local");
		}
		if (ClassName.Equals(TEXT("AnimGraphNode_ComponentToLocalSpace"), ESearchCase::CaseSensitive))
		{
			return Pin->Direction == EGPD_Output ? TEXT("local") : TEXT("component");
		}
		if (ClassName.Equals(TEXT("AnimGraphNode_ModifyBone"), ESearchCase::CaseSensitive))
		{
			return TEXT("component");
		}
		if (ClassName.Equals(TEXT("AnimGraphNode_Root"), ESearchCase::CaseSensitive)
			|| ClassName.Equals(TEXT("AnimGraphNode_SequencePlayer"), ESearchCase::CaseSensitive)
			|| ClassName.Equals(TEXT("AnimGraphNode_StateMachine"), ESearchCase::CaseSensitive)
			|| ClassName.Equals(TEXT("AnimGraphNode_SaveCachedPose"), ESearchCase::CaseSensitive)
			|| ClassName.Equals(TEXT("AnimGraphNode_UseCachedPose"), ESearchCase::CaseSensitive)
			|| ClassName.Equals(TEXT("AnimGraphNode_LayeredBoneBlend"), ESearchCase::CaseSensitive)
			|| ClassName.Equals(TEXT("AnimGraphNode_Slot"), ESearchCase::CaseSensitive)
			|| ClassName.Equals(TEXT("AnimGraphNode_ControlRig"), ESearchCase::CaseSensitive))
		{
			return TEXT("local");
		}

		return TEXT("unknown");
	}

	struct FGraphReadLinkInfo
	{
		FString FromNodeId;
		FString FromPin;
		FString ToNodeId;
		FString ToPin;
	};

	struct FDownstreamTraversalState
	{
		FString NodeId;
		int32 Distance = 0;
		FString FirstBlendNodeId;
		FString FirstBlendInputPin;
	};

	static void BuildGraphAnalysis(
		UEdGraph* Graph,
		const TMap<const UEdGraphNode*, FString>& NodeIds,
		const TMap<FString, UEdGraphNode*>& NodesById,
		const TArray<FGraphReadLinkInfo>& Links,
		TSharedRef<FJsonObject>& OutGraphObj,
		TMap<FString, TSharedRef<FJsonObject>>& InOutNodeObjects)
	{
		if (!Graph)
		{
			return;
		}

		TMap<FString, TArray<FGraphReadLinkInfo>> OutgoingByNode;
		TMap<FString, TArray<FGraphReadLinkInfo>> IncomingByNode;
		for (const FGraphReadLinkInfo& Link : Links)
		{
			OutgoingByNode.FindOrAdd(Link.FromNodeId).Add(Link);
			IncomingByNode.FindOrAdd(Link.ToNodeId).Add(Link);
		}

		TArray<FString> RootNodeIds;
		TArray<TSharedPtr<FJsonValue>> BlendSummaries;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node)
			{
				continue;
			}

			const FString NodeId = GetStableNodeId(Node);
			if (Node->GetClass() && Node->GetClass()->GetName().Equals(TEXT("AnimGraphNode_Root"), ESearchCase::CaseSensitive))
			{
				RootNodeIds.Add(NodeId);
			}

			if (!(Node->GetClass() && Node->GetClass()->GetName().Equals(TEXT("AnimGraphNode_LayeredBoneBlend"), ESearchCase::CaseSensitive)))
			{
				continue;
			}

			TSharedRef<FJsonObject> BlendObj = MakeShared<FJsonObject>();
			BlendObj->SetStringField(TEXT("nodeId"), NodeId);
			TArray<TSharedPtr<FJsonValue>> OverlayInputs;
			if (const TArray<FGraphReadLinkInfo>* Incoming = IncomingByNode.Find(NodeId))
			{
				for (const FGraphReadLinkInfo& Link : *Incoming)
				{
					if (Link.ToPin.Equals(TEXT("BasePose"), ESearchCase::CaseSensitive))
					{
						BlendObj->SetStringField(TEXT("basePoseSourceNodeId"), Link.FromNodeId);
					}
					else if (Link.ToPin.StartsWith(TEXT("BlendPoses_"), ESearchCase::CaseSensitive))
					{
						TSharedRef<FJsonObject> OverlayObj = MakeShared<FJsonObject>();
						OverlayObj->SetStringField(TEXT("inputPin"), Link.ToPin);
						OverlayObj->SetStringField(TEXT("sourceNodeId"), Link.FromNodeId);
						OverlayInputs.Add(MakeShared<FJsonValueObject>(OverlayObj));
					}
				}
			}
			BlendObj->SetArrayField(TEXT("overlaySources"), OverlayInputs);
			BlendSummaries.Add(MakeShared<FJsonValueObject>(BlendObj));
		}

		TArray<TSharedPtr<FJsonValue>> RootNodeValues;
		for (const FString& RootNodeId : RootNodeIds)
		{
			RootNodeValues.Add(MakeShared<FJsonValueString>(RootNodeId));
		}

		TSharedRef<FJsonObject> AnalysisObj = MakeShared<FJsonObject>();
		AnalysisObj->SetArrayField(TEXT("rootNodeIds"), RootNodeValues);
		AnalysisObj->SetArrayField(TEXT("blendNodes"), BlendSummaries);
		OutGraphObj->SetObjectField(TEXT("analysis"), AnalysisObj);

		for (TPair<FString, TSharedRef<FJsonObject>>& Pair : InOutNodeObjects)
		{
			const FString& NodeId = Pair.Key;
			UEdGraphNode* const* NodePtr = NodesById.Find(NodeId);
			if (!NodePtr || !*NodePtr)
			{
				continue;
			}

			UEdGraphNode* Node = *NodePtr;
			TSharedRef<FJsonObject> NodeAnalysisObj = MakeShared<FJsonObject>();
			NodeAnalysisObj->SetBoolField(TEXT("reachesRoot"), false);
			NodeAnalysisObj->SetStringField(TEXT("stage"), TEXT("disconnected"));
			NodeAnalysisObj->SetStringField(TEXT("primaryInputPoseSpace"), TEXT("unknown"));
			NodeAnalysisObj->SetStringField(TEXT("primaryOutputPoseSpace"), TEXT("unknown"));

			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (!Pin || !IsPosePin(Pin))
				{
					continue;
				}

				const FString PoseSpace = InferPoseSpaceForPin(Node, Pin);
				if (Pin->Direction == EGPD_Input && NodeAnalysisObj->GetStringField(TEXT("primaryInputPoseSpace")).Equals(TEXT("unknown"), ESearchCase::CaseSensitive))
				{
					NodeAnalysisObj->SetStringField(TEXT("primaryInputPoseSpace"), PoseSpace);
				}
				if (Pin->Direction == EGPD_Output && NodeAnalysisObj->GetStringField(TEXT("primaryOutputPoseSpace")).Equals(TEXT("unknown"), ESearchCase::CaseSensitive))
				{
					NodeAnalysisObj->SetStringField(TEXT("primaryOutputPoseSpace"), PoseSpace);
				}
			}

			TQueue<FDownstreamTraversalState> Queue;
			TSet<FString> Visited;
			Queue.Enqueue(FDownstreamTraversalState{ NodeId, 0, FString(), FString() });
			Visited.Add(NodeId);
			bool bFoundRoot = false;
			FDownstreamTraversalState BestState;

			while (!Queue.IsEmpty() && !bFoundRoot)
			{
				FDownstreamTraversalState State;
				Queue.Dequeue(State);
				if (RootNodeIds.Contains(State.NodeId))
				{
					bFoundRoot = true;
					BestState = State;
					break;
				}

				if (const TArray<FGraphReadLinkInfo>* Outgoing = OutgoingByNode.Find(State.NodeId))
				{
					for (const FGraphReadLinkInfo& Link : *Outgoing)
					{
						if (Visited.Contains(Link.ToNodeId))
						{
							continue;
						}

						FDownstreamTraversalState NextState;
						NextState.NodeId = Link.ToNodeId;
						NextState.Distance = State.Distance + 1;
						NextState.FirstBlendNodeId = State.FirstBlendNodeId;
						NextState.FirstBlendInputPin = State.FirstBlendInputPin;

						UEdGraphNode* const* DownstreamNodePtr = NodesById.Find(Link.ToNodeId);
						if (DownstreamNodePtr && *DownstreamNodePtr && (*DownstreamNodePtr)->GetClass()
							&& (*DownstreamNodePtr)->GetClass()->GetName().Equals(TEXT("AnimGraphNode_LayeredBoneBlend"), ESearchCase::CaseSensitive)
							&& NextState.FirstBlendNodeId.IsEmpty())
						{
							NextState.FirstBlendNodeId = Link.ToNodeId;
							NextState.FirstBlendInputPin = Link.ToPin;
						}

						Visited.Add(Link.ToNodeId);
						Queue.Enqueue(NextState);
					}
				}
			}

			if (bFoundRoot)
			{
				NodeAnalysisObj->SetBoolField(TEXT("reachesRoot"), true);
				NodeAnalysisObj->SetNumberField(TEXT("rootDistance"), BestState.Distance);
				if (BestState.FirstBlendNodeId.IsEmpty())
				{
					NodeAnalysisObj->SetStringField(TEXT("stage"), TEXT("post_blend"));
				}
				else if (BestState.FirstBlendInputPin.Equals(TEXT("BasePose"), ESearchCase::CaseSensitive))
				{
					NodeAnalysisObj->SetStringField(TEXT("stage"), TEXT("pre_blend_base"));
				}
				else if (BestState.FirstBlendInputPin.StartsWith(TEXT("BlendPoses_"), ESearchCase::CaseSensitive))
				{
					NodeAnalysisObj->SetStringField(TEXT("stage"), TEXT("pre_blend_overlay"));
				}
				else
				{
					NodeAnalysisObj->SetStringField(TEXT("stage"), TEXT("pre_blend_other"));
				}

				if (!BestState.FirstBlendNodeId.IsEmpty())
				{
					NodeAnalysisObj->SetStringField(TEXT("nearestBlendNodeId"), BestState.FirstBlendNodeId);
					NodeAnalysisObj->SetStringField(TEXT("nearestBlendInputPin"), BestState.FirstBlendInputPin);
				}
			}

			Pair.Value->SetObjectField(TEXT("analysis"), NodeAnalysisObj);
		}
	}

	static void AddNodeInspectionData(UEdGraphNode* Node, TSharedRef<FJsonObject>& NodeObj, const bool bIncludeNodeProperties, const bool bIncludeNodeValidation, const TArray<FString>& RequestedPropertyPaths)
	{
		if (!Node)
		{
			return;
		}

		if (bIncludeNodeProperties)
		{
			TArray<FString> PropertyPaths = RequestedPropertyPaths;
			if (PropertyPaths.Num() == 0)
			{
				FBlueprintGraphNodeService::GetDefaultInspectionPropertyPaths(Node, PropertyPaths);
			}

			TSharedRef<FJsonObject> PropertiesObj = MakeShared<FJsonObject>();
			TArray<TSharedPtr<FJsonValue>> PropertyWarnings;
			for (const FString& PropertyPath : PropertyPaths)
			{
				FString ExportedValue;
				FString ExportError;
				if (FBlueprintGraphNodeService::TryExportNodePropertyText(Node, PropertyPath, ExportedValue, ExportError))
				{
					PropertiesObj->SetStringField(PropertyPath, ExportedValue);
				}
				else
				{
					TSharedRef<FJsonObject> WarningObj = MakeShared<FJsonObject>();
					WarningObj->SetStringField(TEXT("propertyPath"), PropertyPath);
					WarningObj->SetStringField(TEXT("message"), ExportError);
					PropertyWarnings.Add(MakeShared<FJsonValueObject>(WarningObj));
				}
			}

			NodeObj->SetObjectField(TEXT("properties"), PropertiesObj);
			if (PropertyWarnings.Num() > 0)
			{
				NodeObj->SetArrayField(TEXT("propertyWarnings"), PropertyWarnings);
			}
		}

		if (bIncludeNodeValidation)
		{
			TArray<FBlueprintGraphNodeValidationIssue> Issues;
			FBlueprintGraphNodeService::CollectNodeValidationIssues(Node, GetStableNodeId(Node), Issues);
			TArray<TSharedPtr<FJsonValue>> IssueValues;
			for (const FBlueprintGraphNodeValidationIssue& Issue : Issues)
			{
				IssueValues.Add(MakeShared<FJsonValueObject>(MakeNodeValidationIssueObject(Issue)));
			}
			NodeObj->SetArrayField(TEXT("validation"), IssueValues);
		}
	}

	static void BuildGraphSnapshot(UBlueprint* Blueprint, UEdGraph* Graph, const bool bIncludeNodeProperties, const bool bIncludeNodeValidation, const bool bIncludeGraphAnalysis, const TArray<FString>& RequestedPropertyPaths, TSharedRef<FJsonObject>& OutGraphObj)
	{
		OutGraphObj->SetStringField(TEXT("name"), Graph->GetName());
		OutGraphObj->SetStringField(TEXT("kind"), GetGraphKind(Blueprint, Graph));
		OutGraphObj->SetNumberField(TEXT("nodeCount"), Graph->Nodes.Num());

		TMap<const UEdGraphNode*, FString> NodeIds;
		TMap<FString, UEdGraphNode*> NodesById;
		TMap<FString, TSharedRef<FJsonObject>> NodeObjects;
		TArray<TSharedPtr<FJsonValue>> NodeValues;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node)
			{
				continue;
			}

			const FString NodeId = GetStableNodeId(Node);
			NodeIds.Add(Node, NodeId);
			NodesById.Add(NodeId, Node);
			TSharedRef<FJsonObject> NodeObj = DescribeNode(Node);
			AddNodeInspectionData(Node, NodeObj, bIncludeNodeProperties, bIncludeNodeValidation, RequestedPropertyPaths);
			NodeObjects.Add(NodeId, NodeObj);
			NodeValues.Add(MakeShared<FJsonValueObject>(NodeObj));
		}
		OutGraphObj->SetArrayField(TEXT("nodes"), NodeValues);

		TSet<FString> SeenLinks;
		TArray<FGraphReadLinkInfo> GraphLinks;
		TArray<TSharedPtr<FJsonValue>> LinkValues;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node)
			{
				continue;
			}

			const FString* FromNodeId = NodeIds.Find(Node);
			if (!FromNodeId)
			{
				continue;
			}

			for (UEdGraphPin* Pin : Node->Pins)
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

					const FString* ToNodeId = NodeIds.Find(LinkedPin->GetOwningNode());
					if (!ToNodeId)
					{
						continue;
					}

					const FString LinkKey = FString::Printf(TEXT("%s.%s->%s.%s"), **FromNodeId, *Pin->PinName.ToString(), **ToNodeId, *LinkedPin->PinName.ToString());
					if (SeenLinks.Contains(LinkKey))
					{
						continue;
					}
					SeenLinks.Add(LinkKey);

					TSharedRef<FJsonObject> LinkObj = MakeShared<FJsonObject>();
					LinkObj->SetStringField(TEXT("from"), FString::Printf(TEXT("%s.%s"), **FromNodeId, *Pin->PinName.ToString()));
					LinkObj->SetStringField(TEXT("to"), FString::Printf(TEXT("%s.%s"), **ToNodeId, *LinkedPin->PinName.ToString()));
					GraphLinks.Add({ *FromNodeId, Pin->PinName.ToString(), *ToNodeId, LinkedPin->PinName.ToString() });
					LinkValues.Add(MakeShared<FJsonValueObject>(LinkObj));
				}
			}
		}

		OutGraphObj->SetArrayField(TEXT("links"), LinkValues);
		if (bIncludeGraphAnalysis)
		{
			BuildGraphAnalysis(Graph, NodeIds, NodesById, GraphLinks, OutGraphObj, NodeObjects);
		}
	}
}

FAutomationResult FReadGraphCommand::Execute(FAutomationContext& Context)
{
	if (!Context.Body.IsValid())
	{
		return BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("bad_args"), TEXT("Missing request body."), 400);
	}

	FString BlueprintPath;
	if (!Context.Body->TryGetStringField(TEXT("blueprint"), BlueprintPath) || BlueprintPath.TrimStartAndEnd().IsEmpty())
	{
		return BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("missing_blueprint"), TEXT("Body must include non-empty 'blueprint'."), 400);
	}

	FString GraphName;
	Context.Body->TryGetStringField(TEXT("graph"), GraphName);
	bool bIncludeNodeProperties = false;
	bool bIncludeNodeValidation = false;
	bool bIncludeGraphAnalysis = false;
	Context.Body->TryGetBoolField(TEXT("includeNodeProperties"), bIncludeNodeProperties);
	Context.Body->TryGetBoolField(TEXT("includeNodeValidation"), bIncludeNodeValidation);
	Context.Body->TryGetBoolField(TEXT("includeGraphAnalysis"), bIncludeGraphAnalysis);

	TArray<FString> RequestedPropertyPaths;
	if (const TArray<TSharedPtr<FJsonValue>>* PropertyPaths = nullptr; Context.Body->TryGetArrayField(TEXT("propertyPaths"), PropertyPaths) && PropertyPaths)
	{
		for (const TSharedPtr<FJsonValue>& PropertyPathValue : *PropertyPaths)
		{
			if (PropertyPathValue.IsValid() && PropertyPathValue->Type == EJson::String)
			{
				const FString PropertyPath = PropertyPathValue->AsString().TrimStartAndEnd();
				if (!PropertyPath.IsEmpty())
				{
					RequestedPropertyPaths.AddUnique(PropertyPath);
				}
			}
		}
	}

	TOptional<FAutomationResult> Result;
	const bool bCompleted = BAT::EditorExecution::RunOnGameThreadAndWaitVoid([&Context, BlueprintPath, GraphName, bIncludeNodeProperties, bIncludeNodeValidation, bIncludeGraphAnalysis, RequestedPropertyPaths, &Result]()
	{
		const FString ObjectPath = NormalizeBlueprintObjectPath(BlueprintPath);
		UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *ObjectPath);
		if (!Blueprint)
		{
			Result = BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("blueprint_not_found"), TEXT("Blueprint could not be loaded."), 404);
			return;
		}

		TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("blueprint"), ObjectPath);
		Data->SetStringField(TEXT("compileStatus"), Blueprint->Status == BS_Error ? TEXT("error") : TEXT("available"));

		if (GraphName.TrimStartAndEnd().IsEmpty())
		{
			TArray<UEdGraph*> Graphs;
			GetAllBlueprintGraphs(Blueprint, Graphs);
			TArray<TSharedPtr<FJsonValue>> GraphValues;
			for (UEdGraph* Graph : Graphs)
			{
				if (!Graph)
				{
					continue;
				}

				TSharedRef<FJsonObject> GraphObj = MakeShared<FJsonObject>();
				GraphObj->SetStringField(TEXT("name"), Graph->GetName());
				GraphObj->SetStringField(TEXT("kind"), GetGraphKind(Blueprint, Graph));
				GraphObj->SetNumberField(TEXT("nodeCount"), Graph->Nodes.Num());
				GraphValues.Add(MakeShared<FJsonValueObject>(GraphObj));
			}

			Data->SetArrayField(TEXT("graphs"), GraphValues);
			Result = BAT::Reflection::MakeStructuredSuccess(Context.RequestId, Data);
			return;
		}

		UEdGraph* Graph = FindGraphByName(Blueprint, GraphName);
		if (!Graph)
		{
			Result = BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("graph_not_found"), TEXT("Graph could not be found."), 404);
			return;
		}

		TSharedRef<FJsonObject> GraphObj = MakeShared<FJsonObject>();
		BuildGraphSnapshot(Blueprint, Graph, bIncludeNodeProperties, bIncludeNodeValidation, bIncludeGraphAnalysis, RequestedPropertyPaths, GraphObj);
		Data->SetObjectField(TEXT("graph"), GraphObj);
		Result = BAT::Reflection::MakeStructuredSuccess(Context.RequestId, Data);
	}, 10.0f);

	return (bCompleted && Result.IsSet()) ? Result.GetValue() : BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("game_thread_timeout"), TEXT("Timed out waiting for game thread execution."), 504);
}
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

	static void BuildGraphSnapshot(UBlueprint* Blueprint, UEdGraph* Graph, const bool bIncludeNodeProperties, const bool bIncludeNodeValidation, const TArray<FString>& RequestedPropertyPaths, TSharedRef<FJsonObject>& OutGraphObj)
	{
		OutGraphObj->SetStringField(TEXT("name"), Graph->GetName());
		OutGraphObj->SetStringField(TEXT("kind"), GetGraphKind(Blueprint, Graph));
		OutGraphObj->SetNumberField(TEXT("nodeCount"), Graph->Nodes.Num());

		TMap<const UEdGraphNode*, FString> NodeIds;
		TArray<TSharedPtr<FJsonValue>> NodeValues;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node)
			{
				continue;
			}

			const FString NodeId = GetStableNodeId(Node);
			NodeIds.Add(Node, NodeId);
			TSharedRef<FJsonObject> NodeObj = DescribeNode(Node);
			AddNodeInspectionData(Node, NodeObj, bIncludeNodeProperties, bIncludeNodeValidation, RequestedPropertyPaths);
			NodeValues.Add(MakeShared<FJsonValueObject>(NodeObj));
		}
		OutGraphObj->SetArrayField(TEXT("nodes"), NodeValues);

		TSet<FString> SeenLinks;
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
					LinkValues.Add(MakeShared<FJsonValueObject>(LinkObj));
				}
			}
		}

		OutGraphObj->SetArrayField(TEXT("links"), LinkValues);
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
	Context.Body->TryGetBoolField(TEXT("includeNodeProperties"), bIncludeNodeProperties);
	Context.Body->TryGetBoolField(TEXT("includeNodeValidation"), bIncludeNodeValidation);

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
	const bool bCompleted = BAT::EditorExecution::RunOnGameThreadAndWaitVoid([&Context, BlueprintPath, GraphName, bIncludeNodeProperties, bIncludeNodeValidation, RequestedPropertyPaths, &Result]()
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
		BuildGraphSnapshot(Blueprint, Graph, bIncludeNodeProperties, bIncludeNodeValidation, RequestedPropertyPaths, GraphObj);
		Data->SetObjectField(TEXT("graph"), GraphObj);
		Result = BAT::Reflection::MakeStructuredSuccess(Context.RequestId, Data);
	}, 10.0f);

	return (bCompleted && Result.IsSet()) ? Result.GetValue() : BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("game_thread_timeout"), TEXT("Timed out waiting for game thread execution."), 504);
}
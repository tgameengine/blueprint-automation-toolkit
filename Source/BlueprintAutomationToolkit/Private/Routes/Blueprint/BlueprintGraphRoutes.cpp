#include "BlueprintAutomationToolkitModule.h"

#include "Commands/BlueprintGraphEditCommand.h"
#include "Commands/CommandDispatcher.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "Http/HttpRequestUtils.h"
#include "HttpPath.h"
#include "HttpResultCallback.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Services/BlueprintService.h"

namespace
{
	static constexpr const TCHAR* Route_BlueprintGraphLinks = TEXT("/blueprint/graph/links");
	static constexpr const TCHAR* Route_BlueprintNodeDescribe = TEXT("/blueprint/node/describe");

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

	static bool TryLoadBlueprint(const FString& InPath, UBlueprint*& OutBlueprint, FString& OutObjectPath)
	{
		OutBlueprint = nullptr;
		OutObjectPath = NormalizeBlueprintObjectPath(InPath);
		if (OutObjectPath.IsEmpty())
		{
			return false;
		}

		OutBlueprint = LoadObject<UBlueprint>(nullptr, *OutObjectPath);
		return OutBlueprint != nullptr;
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
		if (!Blueprint)
		{
			return nullptr;
		}

		TArray<UEdGraph*> Graphs;
		GetAllBlueprintGraphs(Blueprint, Graphs);

		if (GraphName.IsEmpty())
		{
			return Graphs.Num() > 0 ? Graphs[0] : nullptr;
		}

		for (UEdGraph* Graph : Graphs)
		{
			if (Graph && Graph->GetName() == GraphName)
			{
				return Graph;
			}
		}

		for (UEdGraph* Graph : Graphs)
		{
			if (Graph && Graph->GetFName().ToString() == GraphName)
			{
				return Graph;
			}
		}

		return nullptr;
	}

	static UEdGraphNode* FindNodeByGuid(UBlueprint* Blueprint, const FGuid& NodeGuid)
	{
		if (!Blueprint)
		{
			return nullptr;
		}

		TArray<UEdGraph*> Graphs;
		GetAllBlueprintGraphs(Blueprint, Graphs);
		for (UEdGraph* Graph : Graphs)
		{
			if (!Graph)
			{
				continue;
			}
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (Node && Node->NodeGuid == NodeGuid)
				{
					return Node;
				}
			}
		}

		return nullptr;
	}

	static TSharedPtr<FJsonObject> DescribeNodeJson(UEdGraphNode* Node)
	{
		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		if (!Node)
		{
			Obj->SetBoolField(TEXT("ok"), false);
			return Obj;
		}

		Obj->SetBoolField(TEXT("ok"), true);
		Obj->SetStringField(TEXT("node_guid"), Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
		Obj->SetStringField(TEXT("class"), Node->GetClass()->GetPathName());
		Obj->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
		Obj->SetNumberField(TEXT("x"), Node->NodePosX);
		Obj->SetNumberField(TEXT("y"), Node->NodePosY);

		TArray<TSharedPtr<FJsonValue>> Pins;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin)
			{
				continue;
			}

			TSharedRef<FJsonObject> PinObj = MakeShared<FJsonObject>();
			PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
			PinObj->SetStringField(TEXT("dir"), (Pin->Direction == EGPD_Input) ? TEXT("input") : TEXT("output"));
			PinObj->SetStringField(TEXT("category"), Pin->PinType.PinCategory.ToString());
			PinObj->SetStringField(TEXT("sub_category"), Pin->PinType.PinSubCategory.ToString());
			PinObj->SetStringField(TEXT("default"), Pin->DefaultValue);
			PinObj->SetBoolField(TEXT("linked"), Pin->LinkedTo.Num() > 0);
			Pins.Add(MakeShared<FJsonValueObject>(PinObj));
		}
		Obj->SetArrayField(TEXT("pins"), Pins);
		return Obj;
	}

	static bool TryParseGuidString(const FString& InGuid, FGuid& OutGuid)
	{
		return FGuid::Parse(InGuid, OutGuid);
	}

	static FString GetStableNodeId(UEdGraphNode* Node)
	{
		if (!Node)
		{
			return FString();
		}

		const FString Comment = Node->NodeComment;
		const FString Prefix = TEXT("[BAT:id=");
		const int32 PrefixPos = Comment.Find(Prefix, ESearchCase::CaseSensitive);
		if (PrefixPos != INDEX_NONE)
		{
			const int32 IdStart = PrefixPos + Prefix.Len();
			const int32 SuffixPos = Comment.Find(TEXT("]"), ESearchCase::CaseSensitive, ESearchDir::FromStart, IdStart);
			if (SuffixPos != INDEX_NONE && SuffixPos > IdStart)
			{
				return Comment.Mid(IdStart, SuffixPos - IdStart);
			}
		}

		return Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens);
	}
}

void FBlueprintAutomationToolkitModule::BindBlueprintGraphRoutes()
{
	BlueprintGraphLinksRoute = Router->BindRoute(
		FHttpPath(Route_BlueprintGraphLinks),
		EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, Route_BlueprintGraphLinks))
			{
				return true;
			}

			const FString RequestId = ResolveOrCreateRequestId(Request);
			const FString* BlueprintPathParam = Request.QueryParams.Find(TEXT("blueprint"));
			const FString* GraphNameParam = Request.QueryParams.Find(TEXT("graph"));
			if (BlueprintPathParam == nullptr || BlueprintPathParam->TrimStartAndEnd().IsEmpty())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, RequestId, TEXT("missing_blueprint"), TEXT("Query must include 'blueprint'")));
				return true;
			}
			if (GraphNameParam == nullptr || GraphNameParam->TrimStartAndEnd().IsEmpty())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, RequestId, TEXT("missing_graph"), TEXT("Query must include 'graph'")));
				return true;
			}

			const FString BlueprintPath = *BlueprintPathParam;
			const FString GraphName = *GraphNameParam;

			TSharedPtr<FJsonValue> DataValue;
			const bool bCompleted = RunOnGameThreadWait([&DataValue, BlueprintPath, GraphName]()
			{
				UBlueprint* Blueprint = nullptr;
				FString ObjectPath;
				if (!TryLoadBlueprint(BlueprintPath, Blueprint, ObjectPath) || !Blueprint)
				{
					return;
				}

				UEdGraph* Graph = FindGraphByName(Blueprint, GraphName);
				if (!Graph)
				{
					return;
				}

				TMap<const UEdGraphNode*, FString> NodeIds;
				TArray<TSharedPtr<FJsonValue>> NodeArr;
				for (UEdGraphNode* Node : Graph->Nodes)
				{
					if (!Node)
					{
						continue;
					}

					const FString NodeId = GetStableNodeId(Node);
					NodeIds.Add(Node, NodeId);

					TSharedRef<FJsonObject> NodeObj = MakeShared<FJsonObject>();
					NodeObj->SetStringField(TEXT("id"), NodeId);
					NodeObj->SetStringField(TEXT("type"), Node->GetClass()->GetName());
					NodeObj->SetNumberField(TEXT("x"), Node->NodePosX);
					NodeObj->SetNumberField(TEXT("y"), Node->NodePosY);
					NodeArr.Add(MakeShared<FJsonValueObject>(NodeObj));
				}

				TSet<FString> SeenLinks;
				TArray<TSharedPtr<FJsonValue>> LinkArr;
				for (UEdGraphNode* Node : Graph->Nodes)
				{
					if (!Node)
					{
						continue;
					}

					const FString* FromNodeIdPtr = NodeIds.Find(Node);
					if (!FromNodeIdPtr)
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
							if (!LinkedPin)
							{
								continue;
							}

							UEdGraphNode* ToNode = LinkedPin->GetOwningNode();
							if (!ToNode)
							{
								continue;
							}

							const FString* ToNodeIdPtr = NodeIds.Find(ToNode);
							if (!ToNodeIdPtr)
							{
								continue;
							}

							const FString LinkKey = FString::Printf(TEXT("%s.%s->%s.%s"), **FromNodeIdPtr, *Pin->PinName.ToString(), **ToNodeIdPtr, *LinkedPin->PinName.ToString());
							if (SeenLinks.Contains(LinkKey))
							{
								continue;
							}
							SeenLinks.Add(LinkKey);

							TSharedRef<FJsonObject> FromObj = MakeShared<FJsonObject>();
							FromObj->SetStringField(TEXT("nodeId"), *FromNodeIdPtr);
							FromObj->SetStringField(TEXT("pin"), Pin->PinName.ToString());

							TSharedRef<FJsonObject> ToObj = MakeShared<FJsonObject>();
							ToObj->SetStringField(TEXT("nodeId"), *ToNodeIdPtr);
							ToObj->SetStringField(TEXT("pin"), LinkedPin->PinName.ToString());

							TSharedRef<FJsonObject> LinkObj = MakeShared<FJsonObject>();
							LinkObj->SetObjectField(TEXT("from"), FromObj);
							LinkObj->SetObjectField(TEXT("to"), ToObj);
							LinkArr.Add(MakeShared<FJsonValueObject>(LinkObj));
						}
					}
				}

				TSharedRef<FJsonObject> DataObj = MakeShared<FJsonObject>();
				DataObj->SetArrayField(TEXT("nodes"), NodeArr);
				DataObj->SetArrayField(TEXT("links"), LinkArr);
				DataValue = MakeShared<FJsonValueObject>(DataObj);
			}, 10.0);

			if (!bCompleted)
			{
				OnComplete(MakeErrorResponse(504, RequestId, TEXT("game_thread_timeout"), TEXT("Timed out waiting for GameThread execution")));
				return true;
			}

			if (!DataValue.IsValid())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, RequestId, TEXT("graph_not_found"), TEXT("Blueprint or graph not found")));
				return true;
			}

			BAT::Http::JsonOk(OnComplete, DataValue, 200, RequestId);
			return true;
		}));

	BlueprintNodeDescribeRoute = Router->BindRoute(
		FHttpPath(Route_BlueprintNodeDescribe),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, Route_BlueprintNodeDescribe))
			{
				return true;
			}

			TSharedPtr<FJsonObject> BodyObj;
			if (!BAT::Http::TryParseJsonBody(Request.Body, BodyObj) || !BodyObj.IsValid())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("bad_json"), TEXT("Invalid JSON body")));
				return true;
			}

			FString BlueprintPath;
			FString NodeGuidStr;
			if (!BodyObj->TryGetStringField(TEXT("blueprint"), BlueprintPath) || BlueprintPath.IsEmpty())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("missing_blueprint"), TEXT("Body must include 'blueprint'")));
				return true;
			}
			if (!BodyObj->TryGetStringField(TEXT("node_guid"), NodeGuidStr) || NodeGuidStr.IsEmpty())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("missing_node"), TEXT("Body must include 'node_guid'")));
				return true;
			}

			const FString RequestId = ResolveOrCreateRequestId(Request);

			FBlueprintService Service([BlueprintPath, NodeGuidStr](FAutomationContext& Context) -> FAutomationResult
			{
				TOptional<FAutomationResult> ThreadResult;
				const bool bCompleted = Context.Module->RunOnGameThreadWait([BlueprintPath, NodeGuidStr, &ThreadResult]()
				{
					UBlueprint* Blueprint = nullptr;
					FString ObjectPath;
					if (!TryLoadBlueprint(BlueprintPath, Blueprint, ObjectPath))
					{
						ThreadResult = FAutomationResult::Error(TEXT("not_found"), TEXT("Blueprint not found"), (int32)EHttpServerResponseCodes::BadRequest);
						return;
					}

					FGuid NodeGuid;
					if (!TryParseGuidString(NodeGuidStr, NodeGuid))
					{
						ThreadResult = FAutomationResult::Error(TEXT("bad_guid"), TEXT("Invalid node_guid format"), (int32)EHttpServerResponseCodes::BadRequest);
						return;
					}

					UEdGraphNode* Node = FindNodeByGuid(Blueprint, NodeGuid);
					if (!Node)
					{
						ThreadResult = FAutomationResult::Error(TEXT("node_not_found"), TEXT("Node not found"), (int32)EHttpServerResponseCodes::BadRequest);
						return;
					}

					TSharedPtr<FJsonObject> Obj = DescribeNodeJson(Node);
					Obj->SetStringField(TEXT("blueprint"), ObjectPath);
					ThreadResult = FAutomationResult::Ok(MakeShared<FJsonValueObject>(Obj.ToSharedRef()));
				}, 10.0);

				if (!bCompleted)
				{
					return FAutomationResult::Error(TEXT("game_thread_timeout"), TEXT("Timed out waiting for GameThread execution"), 504);
				}

				if (!ThreadResult.IsSet())
				{
					return FAutomationResult::Error(TEXT("internal_error"), TEXT("No result produced by node describe operation"), 500);
				}

				return ThreadResult.GetValue();
			});

			FBlueprintGraphEditCommand Command(Service);
			FCommandDispatcher Dispatcher;
			FAutomationContext ExecContext;
			ExecContext.Module = this;
			ExecContext.RequestId = RequestId;
			ExecContext.Body = BodyObj;

			const FAutomationResult Result = Dispatcher.Dispatch(Command, ExecContext);
			if (!Result.bSuccess)
			{
				OnComplete(MakeErrorResponse(Result.StatusCode, RequestId, Result.ErrorCode, Result.ErrorMessage));
				return true;
			}

			BAT::Http::JsonOk(OnComplete, Result.Data, Result.StatusCode, RequestId);
			return true;
		}));

	BindBlueprintGraphRoutesInternal();
}

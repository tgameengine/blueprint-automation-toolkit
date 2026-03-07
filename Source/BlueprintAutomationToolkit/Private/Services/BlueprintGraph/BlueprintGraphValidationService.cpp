#include "Services/BlueprintGraph/BlueprintGraphValidationService.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "Routes/Blueprint/BlueprintGraphApplyRequest.h"
#include "Services/BlueprintGraphService.h"
#include "UObject/SoftObjectPath.h"

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

	static UBlueprint* LoadBlueprintAsset(const FString& BlueprintPath, FString& OutResolvedPath)
	{
		OutResolvedPath = NormalizeBlueprintObjectPath(BlueprintPath);
		if (OutResolvedPath.IsEmpty())
		{
			return nullptr;
		}

		if (UBlueprint* LoadedBlueprint = LoadObject<UBlueprint>(nullptr, *OutResolvedPath))
		{
			return LoadedBlueprint;
		}

		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		const FAssetData AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(FSoftObjectPath(OutResolvedPath));
		if (AssetData.IsValid())
		{
			return Cast<UBlueprint>(AssetData.GetAsset());
		}

		return nullptr;
	}

	static UEdGraph* ResolveTargetGraph(UBlueprint* Blueprint, const FString& GraphName)
	{
		if (!Blueprint)
		{
			return nullptr;
		}

		auto FindGraphRecursive = [&GraphName](const TArray<UEdGraph*>& RootGraphs) -> UEdGraph*
		{
			for (UEdGraph* Graph : RootGraphs)
			{
				if (!Graph)
				{
					continue;
				}

				if (Graph->GetName().Equals(GraphName, ESearchCase::CaseSensitive))
				{
					return Graph;
				}

				TArray<UEdGraph*> ChildGraphs;
				Graph->GetAllChildrenGraphs(ChildGraphs);
				for (UEdGraph* ChildGraph : ChildGraphs)
				{
					if (ChildGraph && ChildGraph->GetName().Equals(GraphName, ESearchCase::CaseSensitive))
					{
						return ChildGraph;
					}
				}
			}

			return nullptr;
		};

		if (GraphName.Equals(TEXT("UserConstructionScript"), ESearchCase::CaseSensitive)
			|| GraphName.Equals(TEXT("ConstructionScript"), ESearchCase::CaseSensitive))
		{
			return FBlueprintEditorUtils::FindUserConstructionScript(Blueprint);
		}

		if (GraphName.Equals(TEXT("EventGraph"), ESearchCase::CaseSensitive))
		{
			if (UEdGraph* EventGraph = FindGraphRecursive(Blueprint->UbergraphPages))
			{
				return EventGraph;
			}
			return Blueprint->UbergraphPages.Num() > 0 ? Blueprint->UbergraphPages[0] : nullptr;
		}

		if (UEdGraph* Found = FindGraphRecursive(Blueprint->UbergraphPages))
		{
			return Found;
		}
		if (UEdGraph* Found = FindGraphRecursive(Blueprint->FunctionGraphs))
		{
			return Found;
		}
		if (UEdGraph* Found = FindGraphRecursive(Blueprint->MacroGraphs))
		{
			return Found;
		}

		return nullptr;
	}
}

bool FBlueprintGraphValidationService::PrepareApplyTarget(const FBlueprintGraphApplyRequest& Request, FBlueprintGraphResolvedTarget& OutTarget, FBlueprintGraphApplyResult& OutResult)
{
	OutTarget = FBlueprintGraphResolvedTarget();
	OutTarget.Blueprint = LoadBlueprintAsset(Request.BlueprintPath, OutTarget.BlueprintObjectPath);
	if (!OutTarget.Blueprint)
	{
		OutResult.Errors.Add(TEXT("blueprint_not_found"));
		OutResult.bOk = false;
		return false;
	}

	OutTarget.Graph = ResolveTargetGraph(OutTarget.Blueprint, Request.GraphName);
	if (!OutTarget.Graph)
	{
		OutResult.Errors.Add(TEXT("graph_not_found"));
		OutResult.bOk = false;
		return false;
	}

	return true;
}

TSharedPtr<FJsonValue> FBlueprintGraphValidationService::MakeApplyResultData(const FString& BlueprintObjectPath, const FString& GraphName, const FBlueprintGraphApplyResult& Result)
{
	auto BuildIssueObject = [](const FString& RawIssue) -> TSharedPtr<FJsonValue>
	{
		FString Code = RawIssue;
		FString Detail;
		if (RawIssue.Split(TEXT(":"), &Code, &Detail, ESearchCase::CaseSensitive))
		{
			Code.TrimStartAndEndInline();
			Detail.TrimStartAndEndInline();
		}

		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("code"), Code);
		Obj->SetStringField(TEXT("message"), RawIssue);

		if (!Detail.IsEmpty())
		{
			if (Code.StartsWith(TEXT("link_")))
			{
				FString From;
				FString To;
				if (Detail.Split(TEXT("->"), &From, &To, ESearchCase::CaseSensitive))
				{
					From.TrimStartAndEndInline();
					To.TrimStartAndEndInline();
					TSharedRef<FJsonObject> Link = MakeShared<FJsonObject>();
					Link->SetStringField(TEXT("from"), From);
					Link->SetStringField(TEXT("to"), To);
					Obj->SetObjectField(TEXT("link"), Link);
				}
			}

			FString NodeId;
			FString PinName;
			if (Detail.Split(TEXT("."), &NodeId, &PinName, ESearchCase::CaseSensitive))
			{
				NodeId.TrimStartAndEndInline();
				PinName.TrimStartAndEndInline();
				if (!NodeId.IsEmpty())
				{
					Obj->SetStringField(TEXT("nodeId"), NodeId);
				}
				if (!PinName.IsEmpty())
				{
					Obj->SetStringField(TEXT("pin"), PinName);
				}
			}
			else
			{
				FString ParsedNode;
				FString ParsedPin;
				if (Detail.Split(TEXT(","), &ParsedNode, &ParsedPin, ESearchCase::CaseSensitive)
					&& ParsedNode.StartsWith(TEXT("node="))
					&& ParsedPin.StartsWith(TEXT("pin=")))
				{
					Obj->SetStringField(TEXT("nodeId"), ParsedNode.RightChop(5).TrimStartAndEnd());
					Obj->SetStringField(TEXT("pin"), ParsedPin.RightChop(4).TrimStartAndEnd());
				}
				else if (!Detail.Contains(TEXT("->")) && !Detail.Contains(TEXT("=")))
				{
					Obj->SetStringField(TEXT("nodeId"), Detail);
				}
			}
		}

		return MakeShared<FJsonValueObject>(Obj);
	};

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("blueprint"), BlueprintObjectPath);
	Data->SetStringField(TEXT("graph"), GraphName);

	TArray<TSharedPtr<FJsonValue>> CreatedNodeValues;
	for (const FString& NodeId : Result.CreatedNodes)
	{
		CreatedNodeValues.Add(MakeShared<FJsonValueString>(NodeId));
	}
	Data->SetArrayField(TEXT("createdNodes"), CreatedNodeValues);

	TArray<TSharedPtr<FJsonValue>> UpdatedNodeValues;
	for (const FString& NodeId : Result.UpdatedNodes)
	{
		UpdatedNodeValues.Add(MakeShared<FJsonValueString>(NodeId));
	}
	Data->SetArrayField(TEXT("updatedNodes"), UpdatedNodeValues);
	Data->SetNumberField(TEXT("createdLinks"), Result.CreatedLinks);

	TArray<TSharedPtr<FJsonValue>> NodeValidationValues;
	for (const FBlueprintGraphNodeValidationIssue& Issue : Result.NodeValidationIssues)
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
		NodeValidationValues.Add(MakeShared<FJsonValueObject>(IssueObj));
	}
	Data->SetArrayField(TEXT("nodeValidation"), NodeValidationValues);

	Data->SetStringField(TEXT("compileStatus"), Result.CompileStatus);
	Data->SetStringField(TEXT("saveStatus"), Result.SaveStatus);

	TSharedRef<FJsonObject> CompileDiagnostics = MakeShared<FJsonObject>();
	CompileDiagnostics->SetStringField(TEXT("compileStatus"), Result.CompileStatus);
	CompileDiagnostics->SetNumberField(TEXT("errorCount"), Result.CompileErrorCount);
	CompileDiagnostics->SetNumberField(TEXT("warningCount"), Result.CompileWarningCount);
	CompileDiagnostics->SetArrayField(TEXT("errors"), Result.CompileErrors);
	CompileDiagnostics->SetArrayField(TEXT("warnings"), Result.CompileWarnings);
	Data->SetObjectField(TEXT("compileDiagnostics"), CompileDiagnostics);

	TArray<TSharedPtr<FJsonValue>> WarningValues;
	for (const FString& Warning : Result.Warnings)
	{
		WarningValues.Add(BuildIssueObject(Warning));
	}
	WarningValues.Append(Result.CompileWarnings);

	TArray<TSharedPtr<FJsonValue>> ErrorValues;
	for (const FString& Error : Result.Errors)
	{
		ErrorValues.Add(BuildIssueObject(Error));
	}
	ErrorValues.Append(Result.CompileErrors);

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), Result.bOk);
	Root->SetArrayField(TEXT("errors"), ErrorValues);
	Root->SetArrayField(TEXT("warnings"), WarningValues);
	Root->SetObjectField(TEXT("data"), Data);

	return MakeShared<FJsonValueObject>(Root);
}
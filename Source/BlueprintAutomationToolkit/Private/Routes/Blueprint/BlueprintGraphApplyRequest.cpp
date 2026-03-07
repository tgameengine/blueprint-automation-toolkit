#include "Routes/Blueprint/BlueprintGraphApplyRequest.h"

#include "Core/ForwardAxis.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
	static bool TryGetRequiredString(const TSharedPtr<FJsonObject>& Obj, const TCHAR* FieldName, FString& OutValue, TArray<FString>& OutErrors)
	{
		OutValue.Reset();
		if (!Obj.IsValid() || !Obj->TryGetStringField(FieldName, OutValue) || OutValue.TrimStartAndEnd().IsEmpty())
		{
			OutErrors.Add(FString::Printf(TEXT("missing_or_empty:%s"), FieldName));
			return false;
		}
		return true;
	}

	static bool TryParsePinAddress(const FString& Address, FString& OutNodeId, FString& OutPinName)
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

namespace BAT::BlueprintGraphApplyRequest
{
	bool Parse(const TSharedPtr<FJsonObject>& BodyObj, FBlueprintGraphApplyRequest& OutRequest, TArray<FString>& OutErrors)
	{
		OutErrors.Reset();
		OutRequest = FBlueprintGraphApplyRequest();

		if (!BodyObj.IsValid())
		{
			OutErrors.Add(TEXT("invalid_payload"));
			return false;
		}

		TryGetRequiredString(BodyObj, TEXT("blueprint"), OutRequest.BlueprintPath, OutErrors);
		TryGetRequiredString(BodyObj, TEXT("graph"), OutRequest.GraphName, OutErrors);

		if (const TSharedPtr<FJsonObject>* OptionsObjPtr = nullptr; BodyObj->TryGetObjectField(TEXT("options"), OptionsObjPtr) && OptionsObjPtr && OptionsObjPtr->IsValid())
		{
			const TSharedPtr<FJsonObject>& OptionsObj = *OptionsObjPtr;
			OptionsObj->TryGetBoolField(TEXT("compile"), OutRequest.Options.bCompile);
			OptionsObj->TryGetBoolField(TEXT("save"), OutRequest.Options.bSave);
			OptionsObj->TryGetBoolField(TEXT("transaction"), OutRequest.Options.bUseTransaction);
			OptionsObj->TryGetBoolField(TEXT("dryRun"), OutRequest.Options.bDryRun);
			OptionsObj->TryGetBoolField(TEXT("createMissingNodes"), OutRequest.Options.bCreateMissingNodes);
		}

		const TArray<TSharedPtr<FJsonValue>>* NodesArray = nullptr;
		if (!BodyObj->TryGetArrayField(TEXT("nodes"), NodesArray) || NodesArray == nullptr)
		{
			OutErrors.Add(TEXT("missing_nodes"));
		}

		const TArray<TSharedPtr<FJsonValue>>* LinksArray = nullptr;
		if (!BodyObj->TryGetArrayField(TEXT("links"), LinksArray) || LinksArray == nullptr)
		{
			OutErrors.Add(TEXT("missing_links"));
		}

		TSet<FString> SeenIds;
		if (NodesArray)
		{
			OutRequest.Nodes.Reserve(NodesArray->Num());
			for (int32 NodeIndex = 0; NodeIndex < NodesArray->Num(); ++NodeIndex)
			{
				const TSharedPtr<FJsonValue>& NodeValue = (*NodesArray)[NodeIndex];
				if (!NodeValue.IsValid() || NodeValue->Type != EJson::Object)
				{
					OutErrors.Add(FString::Printf(TEXT("nodes[%d]_must_be_object"), NodeIndex));
					continue;
				}

				const TSharedPtr<FJsonObject> NodeObj = NodeValue->AsObject();
				FBlueprintGraphApplyNodeSpec NodeSpec;
				TryGetRequiredString(NodeObj, TEXT("id"), NodeSpec.Id, OutErrors);
				NodeObj->TryGetStringField(TEXT("type"), NodeSpec.Type);
				NodeObj->TryGetBoolField(TEXT("updateOnly"), NodeSpec.bUpdateOnly);
				if (!NodeSpec.bUpdateOnly)
				{
					NodeObj->TryGetBoolField(TEXT("update_only"), NodeSpec.bUpdateOnly);
				}
				NodeObj->TryGetStringField(TEXT("forward_axis"), NodeSpec.ForwardAxis);
				NodeObj->TryGetStringField(TEXT("event"), NodeSpec.Event);
				NodeObj->TryGetStringField(TEXT("class"), NodeSpec.ClassPath);
				NodeObj->TryGetStringField(TEXT("function"), NodeSpec.Function);
				NodeObj->TryGetStringField(TEXT("message"), NodeSpec.Message);
				NodeObj->TryGetStringField(TEXT("variable"), NodeSpec.Variable);
				NodeObj->TryGetStringField(TEXT("macro"), NodeSpec.Macro);
				NodeSpec.bHasExplicitX = NodeObj->TryGetNumberField(TEXT("x"), NodeSpec.X);
				NodeSpec.bHasExplicitY = NodeObj->TryGetNumberField(TEXT("y"), NodeSpec.Y);
				NodeObj->TryGetNumberField(TEXT("outputs"), NodeSpec.Outputs);

				if (NodeSpec.Type.TrimStartAndEnd().IsEmpty() && !NodeSpec.bUpdateOnly)
				{
					OutErrors.Add(FString::Printf(TEXT("nodes[%d]_missing_type"), NodeIndex));
				}

				if (!NodeSpec.Id.IsEmpty())
				{
					if (SeenIds.Contains(NodeSpec.Id))
					{
						OutErrors.Add(FString::Printf(TEXT("duplicate_node_id:%s"), *NodeSpec.Id));
					}
					SeenIds.Add(NodeSpec.Id);
				}

				if (NodeSpec.Type.TrimStartAndEnd().IsEmpty())
				{
				}
				else if (NodeSpec.Type.Equals(TEXT("K2Node_Event"), ESearchCase::CaseSensitive))
				{
					if (!NodeSpec.Event.Equals(TEXT("BeginPlay"), ESearchCase::CaseSensitive))
					{
						OutErrors.Add(FString::Printf(TEXT("nodes[%d]_unsupported_event"), NodeIndex));
					}
				}
				else if (NodeSpec.Type.Equals(TEXT("K2Node_SpawnActor"), ESearchCase::CaseSensitive))
				{
					if (NodeSpec.ClassPath.TrimStartAndEnd().IsEmpty())
					{
						OutErrors.Add(FString::Printf(TEXT("nodes[%d]_missing_class"), NodeIndex));
					}
				}
				else if (NodeSpec.Type.Equals(TEXT("K2Node_PrintString"), ESearchCase::CaseSensitive))
				{
					if (NodeSpec.Message.TrimStartAndEnd().IsEmpty())
					{
						OutErrors.Add(FString::Printf(TEXT("nodes[%d]_missing_message"), NodeIndex));
					}
				}
				else if (NodeSpec.Type.Equals(TEXT("K2Node_CallFunction"), ESearchCase::CaseSensitive))
				{
					if (NodeSpec.Function.TrimStartAndEnd().IsEmpty())
					{
						OutErrors.Add(FString::Printf(TEXT("nodes[%d]_missing_function"), NodeIndex));
					}
				}
				else if (NodeSpec.Type.Equals(TEXT("K2Node_AddComponent"), ESearchCase::CaseSensitive))
				{
					if (NodeSpec.ClassPath.TrimStartAndEnd().IsEmpty())
					{
						OutErrors.Add(FString::Printf(TEXT("nodes[%d]_missing_class"), NodeIndex));
					}
				}
				else if (NodeSpec.Type.Equals(TEXT("K2Node_VariableGet"), ESearchCase::CaseSensitive))
				{
					if (NodeSpec.Variable.TrimStartAndEnd().IsEmpty())
					{
						OutErrors.Add(FString::Printf(TEXT("nodes[%d]_missing_variable"), NodeIndex));
					}
				}
				else if (NodeSpec.Type.Equals(TEXT("K2Node_VariableSet"), ESearchCase::CaseSensitive))
				{
					if (NodeSpec.Variable.TrimStartAndEnd().IsEmpty())
					{
						OutErrors.Add(FString::Printf(TEXT("nodes[%d]_missing_variable"), NodeIndex));
					}
				}
				else if (NodeSpec.Type.Equals(TEXT("K2Node_ExecutionSequence"), ESearchCase::CaseSensitive)
					|| NodeSpec.Type.Equals(TEXT("K2Node_Sequence"), ESearchCase::CaseSensitive))
				{
					if (NodeSpec.Outputs < 0)
					{
						OutErrors.Add(FString::Printf(TEXT("nodes[%d]_invalid_outputs"), NodeIndex));
					}
				}
				else if (NodeSpec.Type.Equals(TEXT("K2Node_Delay"), ESearchCase::CaseSensitive)
					|| NodeSpec.Type.Equals(TEXT("K2Node_Knot"), ESearchCase::CaseSensitive)
					|| NodeSpec.Type.Equals(TEXT("K2Node_Reroute"), ESearchCase::CaseSensitive))
				{
				}
				else if (NodeSpec.Type.Equals(TEXT("K2Node_MacroInstance"), ESearchCase::CaseSensitive))
				{
					if (NodeSpec.Macro.TrimStartAndEnd().IsEmpty())
					{
						OutErrors.Add(FString::Printf(TEXT("nodes[%d]_missing_macro"), NodeIndex));
					}
				}
				else if (NodeSpec.Type.StartsWith(TEXT("AnimGraphNode_"), ESearchCase::CaseSensitive))
				{
					if (!NodeSpec.ForwardAxis.TrimStartAndEnd().IsEmpty())
					{
						FString CanonicalForwardAxis;
						FString AxisError;
						if (!BAT::ForwardAxis::TryNormalizeAxis(NodeSpec.ForwardAxis, CanonicalForwardAxis, AxisError))
						{
							OutErrors.Add(FString::Printf(TEXT("nodes[%d]_invalid_forward_axis"), NodeIndex));
						}
						else
						{
							NodeSpec.ForwardAxis = CanonicalForwardAxis;
						}
					}
				}
				else
				{
					OutErrors.Add(FString::Printf(TEXT("nodes[%d]_unsupported_type:%s"), NodeIndex, *NodeSpec.Type));
				}

				if (const TSharedPtr<FJsonObject>* PinsObjPtr = nullptr; NodeObj->TryGetObjectField(TEXT("pins"), PinsObjPtr) && PinsObjPtr && PinsObjPtr->IsValid())
				{
					for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*PinsObjPtr)->Values)
					{
						if (!Pair.Value.IsValid() || Pair.Value->Type != EJson::String)
						{
							OutErrors.Add(FString::Printf(TEXT("nodes[%d]_pins_%s_must_be_string"), NodeIndex, *Pair.Key));
							continue;
						}
						NodeSpec.Pins.Add(Pair.Key, Pair.Value->AsString());
					}
				}

				if (const TSharedPtr<FJsonObject>* PropertiesObjPtr = nullptr; NodeObj->TryGetObjectField(TEXT("properties"), PropertiesObjPtr) && PropertiesObjPtr && PropertiesObjPtr->IsValid())
				{
					for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*PropertiesObjPtr)->Values)
					{
						if (!Pair.Value.IsValid() || Pair.Value->Type != EJson::String)
						{
							OutErrors.Add(FString::Printf(TEXT("nodes[%d]_properties_%s_must_be_string"), NodeIndex, *Pair.Key));
							continue;
						}
						NodeSpec.Properties.Add(Pair.Key, Pair.Value->AsString());
					}
				}

				OutRequest.Nodes.Add(MoveTemp(NodeSpec));
			}
		}

		if (LinksArray)
		{
			OutRequest.Links.Reserve(LinksArray->Num());
			for (int32 LinkIndex = 0; LinkIndex < LinksArray->Num(); ++LinkIndex)
			{
				const TSharedPtr<FJsonValue>& LinkValue = (*LinksArray)[LinkIndex];
				if (!LinkValue.IsValid() || LinkValue->Type != EJson::Object)
				{
					OutErrors.Add(FString::Printf(TEXT("links[%d]_must_be_object"), LinkIndex));
					continue;
				}

				const TSharedPtr<FJsonObject> LinkObj = LinkValue->AsObject();
				FBlueprintGraphApplyLinkSpec LinkSpec;
				TryGetRequiredString(LinkObj, TEXT("from"), LinkSpec.From, OutErrors);
				TryGetRequiredString(LinkObj, TEXT("to"), LinkSpec.To, OutErrors);

				FString FromNodeId;
				FString FromPinName;
				FString ToNodeId;
				FString ToPinName;
				if (!LinkSpec.From.IsEmpty() && !TryParsePinAddress(LinkSpec.From, FromNodeId, FromPinName))
				{
					OutErrors.Add(FString::Printf(TEXT("links[%d]_invalid_from"), LinkIndex));
				}
				if (!LinkSpec.To.IsEmpty() && !TryParsePinAddress(LinkSpec.To, ToNodeId, ToPinName))
				{
					OutErrors.Add(FString::Printf(TEXT("links[%d]_invalid_to"), LinkIndex));
				}

				OutRequest.Links.Add(MoveTemp(LinkSpec));
			}
		}

		return OutErrors.Num() == 0;
	}
}

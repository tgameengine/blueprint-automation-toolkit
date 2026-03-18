#include "Routes/PCG/PcgApplyRequest.h"

#include "Services/PCG/PcgNodeRegistry.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
	static bool IsNonEmptyTrimmed(const FString& Value)
	{
		FString Copy = Value;
		Copy.TrimStartAndEndInline();
		return !Copy.IsEmpty();
	}

	static bool IsProjectObjectPath(const FString& Value)
	{
		return Value.StartsWith(TEXT("/Game/"), ESearchCase::CaseSensitive) && Value.Contains(TEXT("."), ESearchCase::CaseSensitive);
	}

	static bool IsSupportedMode(const FString& Value)
	{
		return Value.Equals(TEXT("reconcile"), ESearchCase::CaseSensitive) || Value.Equals(TEXT("patch"), ESearchCase::CaseSensitive);
	}

	static bool IsSupportedOwnership(const FString& Value)
	{
		return Value.Equals(TEXT("bat"), ESearchCase::CaseSensitive);
	}

	static bool IsSupportedParameterType(const FString& Value)
	{
		return Value.Equals(TEXT("bool"), ESearchCase::CaseSensitive)
			|| Value.Equals(TEXT("int"), ESearchCase::CaseSensitive)
			|| Value.Equals(TEXT("float"), ESearchCase::CaseSensitive)
			|| Value.Equals(TEXT("string"), ESearchCase::CaseSensitive)
			|| Value.Equals(TEXT("name"), ESearchCase::CaseSensitive)
			|| Value.Equals(TEXT("vector"), ESearchCase::CaseSensitive);
	}

	static bool IsSupportedOp(const FString& Value)
	{
		return Value.Equals(TEXT("nodes.add"), ESearchCase::CaseSensitive)
			|| Value.Equals(TEXT("nodes.set"), ESearchCase::CaseSensitive)
			|| Value.Equals(TEXT("nodes.remove"), ESearchCase::CaseSensitive)
			|| Value.Equals(TEXT("edges.connect"), ESearchCase::CaseSensitive)
			|| Value.Equals(TEXT("edges.disconnect"), ESearchCase::CaseSensitive)
			|| Value.Equals(TEXT("spawners.set_mesh_set"), ESearchCase::CaseSensitive)
			|| Value.Equals(TEXT("parameters.set"), ESearchCase::CaseSensitive);
	}

	static bool TryParsePinAddress(const FString& Address)
	{
		int32 DotIndex = INDEX_NONE;
		if (!Address.FindLastChar(TEXT('.'), DotIndex) || DotIndex <= 0 || DotIndex >= Address.Len() - 1)
		{
			return false;
		}

		const FString NodeId = Address.Left(DotIndex).TrimStartAndEnd();
		const FString PinName = Address.Mid(DotIndex + 1).TrimStartAndEnd();
		return !NodeId.IsEmpty() && !PinName.IsEmpty();
	}

	static bool TryParseMeshPathArray(const TSharedPtr<FJsonObject>& Obj, const TCHAR* FieldName, TArray<FString>& OutMeshes, TArray<FString>& OutErrors, const FString& ErrorPrefix)
	{
		OutMeshes.Reset();
		const TArray<TSharedPtr<FJsonValue>>* MeshesArray = nullptr;
		if (!Obj.IsValid() || !Obj->TryGetArrayField(FieldName, MeshesArray) || !MeshesArray)
		{
			OutErrors.Add(FString::Printf(TEXT("%s_missing_%s"), *ErrorPrefix, FieldName));
			return false;
		}

		if (MeshesArray->Num() == 0)
		{
			OutErrors.Add(FString::Printf(TEXT("%s_empty_%s"), *ErrorPrefix, FieldName));
			return false;
		}

		for (int32 MeshIndex = 0; MeshIndex < MeshesArray->Num(); ++MeshIndex)
		{
			const TSharedPtr<FJsonValue>& MeshValue = (*MeshesArray)[MeshIndex];
			if (!MeshValue.IsValid() || MeshValue->Type != EJson::String)
			{
				OutErrors.Add(FString::Printf(TEXT("%s_%s[%d]_must_be_string"), *ErrorPrefix, FieldName, MeshIndex));
				continue;
			}

			const FString MeshPath = MeshValue->AsString().TrimStartAndEnd();
			if (!IsProjectObjectPath(MeshPath))
			{
				OutErrors.Add(FString::Printf(TEXT("%s_invalid_project_mesh:%s"), *ErrorPrefix, *MeshPath));
				continue;
			}

			OutMeshes.Add(MeshPath);
		}

		return OutMeshes.Num() > 0;
	}

	static bool TryParseParameterEntry(const TSharedPtr<FJsonObject>& EntryObj, FPcgApplyParameterEntry& OutEntry, TArray<FString>& OutErrors, const FString& ErrorPrefix)
	{
		OutEntry = FPcgApplyParameterEntry();
		if (!EntryObj.IsValid())
		{
			OutErrors.Add(FString::Printf(TEXT("%s_invalid_parameter_entry"), *ErrorPrefix));
			return false;
		}

		if (!EntryObj->TryGetStringField(TEXT("name"), OutEntry.Name) || !IsNonEmptyTrimmed(OutEntry.Name))
		{
			OutErrors.Add(FString::Printf(TEXT("%s_missing_name"), *ErrorPrefix));
		}

		if (!EntryObj->TryGetStringField(TEXT("type"), OutEntry.Type) || !IsSupportedParameterType(OutEntry.Type))
		{
			OutErrors.Add(FString::Printf(TEXT("%s_invalid_type"), *ErrorPrefix));
		}

		OutEntry.Name = OutEntry.Name.TrimStartAndEnd();
		OutEntry.Type = OutEntry.Type.TrimStartAndEnd();
		EntryObj->TryGetStringField(TEXT("description"), OutEntry.Description);

		if (const TSharedPtr<FJsonValue>* DefaultValuePtr = EntryObj->Values.Find(TEXT("default")); DefaultValuePtr)
		{
			OutEntry.DefaultValue = *DefaultValuePtr;
		}

		return true;
	}

	static bool TryParseParameterArray(const TArray<TSharedPtr<FJsonValue>>* Array, TArray<FPcgApplyParameterEntry>& OutEntries, TArray<FString>& OutErrors, const FString& ErrorPrefix)
	{
		OutEntries.Reset();
		if (!Array)
		{
			return true;
		}

		for (int32 EntryIndex = 0; EntryIndex < Array->Num(); ++EntryIndex)
		{
			const TSharedPtr<FJsonValue>& EntryValue = (*Array)[EntryIndex];
			if (!EntryValue.IsValid() || EntryValue->Type != EJson::Object)
			{
				OutErrors.Add(FString::Printf(TEXT("%s[%d]_must_be_object"), *ErrorPrefix, EntryIndex));
				continue;
			}

			FPcgApplyParameterEntry Entry;
			TryParseParameterEntry(EntryValue->AsObject(), Entry, OutErrors, FString::Printf(TEXT("%s[%d]"), *ErrorPrefix, EntryIndex));
			OutEntries.Add(MoveTemp(Entry));
		}

		return true;
	}

	static bool TryParseMeshSet(const TSharedPtr<FJsonObject>& MeshSetObj, FPcgApplyMeshSetSpec& OutMeshSet, TArray<FString>& OutErrors, const FString& ErrorPrefix)
	{
		OutMeshSet = FPcgApplyMeshSetSpec();
		if (!MeshSetObj.IsValid())
		{
			OutErrors.Add(FString::Printf(TEXT("%s_invalid_mesh_set"), *ErrorPrefix));
			return false;
		}

		if (!MeshSetObj->TryGetStringField(TEXT("mode"), OutMeshSet.Mode) || !IsNonEmptyTrimmed(OutMeshSet.Mode))
		{
			OutErrors.Add(FString::Printf(TEXT("%s_missing_mode"), *ErrorPrefix));
			return false;
		}

		OutMeshSet.Mode = OutMeshSet.Mode.TrimStartAndEnd();
		OutMeshSet.bIsSet = true;

		if (OutMeshSet.Mode.Equals(TEXT("weighted"), ESearchCase::CaseSensitive))
		{
			TryParseMeshPathArray(MeshSetObj, TEXT("meshes"), OutMeshSet.Meshes, OutErrors, ErrorPrefix);
			return true;
		}

		if (OutMeshSet.Mode.Equals(TEXT("weighted_by_category"), ESearchCase::CaseSensitive))
		{
			const TArray<TSharedPtr<FJsonValue>>* CategoriesArray = nullptr;
			if (!MeshSetObj->TryGetArrayField(TEXT("categories"), CategoriesArray) || !CategoriesArray || CategoriesArray->Num() == 0)
			{
				OutErrors.Add(FString::Printf(TEXT("%s_missing_categories"), *ErrorPrefix));
				return false;
			}

			for (int32 CategoryIndex = 0; CategoryIndex < CategoriesArray->Num(); ++CategoryIndex)
			{
				const TSharedPtr<FJsonValue>& CategoryValue = (*CategoriesArray)[CategoryIndex];
				if (!CategoryValue.IsValid() || CategoryValue->Type != EJson::Object)
				{
					OutErrors.Add(FString::Printf(TEXT("%s_categories[%d]_must_be_object"), *ErrorPrefix, CategoryIndex));
					continue;
				}

				FPcgApplyMeshCategorySpec Category;
				const TSharedPtr<FJsonObject> CategoryObj = CategoryValue->AsObject();
				if (!CategoryObj->TryGetStringField(TEXT("name"), Category.Name) || !IsNonEmptyTrimmed(Category.Name))
				{
					OutErrors.Add(FString::Printf(TEXT("%s_categories[%d]_missing_name"), *ErrorPrefix, CategoryIndex));
				}

				Category.Name = Category.Name.TrimStartAndEnd();
				TryParseMeshPathArray(CategoryObj, TEXT("meshes"), Category.Meshes, OutErrors, FString::Printf(TEXT("%s_categories[%d]"), *ErrorPrefix, CategoryIndex));
				OutMeshSet.Categories.Add(MoveTemp(Category));
			}

			return true;
		}

		OutErrors.Add(FString::Printf(TEXT("%s_unsupported_mesh_set_mode:%s"), *ErrorPrefix, *OutMeshSet.Mode));
		return false;
	}
}

namespace BAT::PcgApplyRequest
{
	bool Parse(const TSharedPtr<FJsonObject>& BodyObj, FPcgApplyRequest& OutRequest, TArray<FString>& OutErrors)
	{
		OutErrors.Reset();
		OutRequest = FPcgApplyRequest();

		if (!BodyObj.IsValid())
		{
			OutErrors.Add(TEXT("invalid_payload"));
			return false;
		}

		if (!BodyObj->TryGetStringField(TEXT("graph"), OutRequest.GraphPath) || !IsProjectObjectPath(OutRequest.GraphPath.TrimStartAndEnd()))
		{
			OutErrors.Add(TEXT("invalid_graph_path"));
		}

		OutRequest.GraphPath = OutRequest.GraphPath.TrimStartAndEnd();

		if (const TSharedPtr<FJsonObject>* OptionsObjPtr = nullptr; BodyObj->TryGetObjectField(TEXT("options"), OptionsObjPtr) && OptionsObjPtr && OptionsObjPtr->IsValid())
		{
			const TSharedPtr<FJsonObject>& OptionsObj = *OptionsObjPtr;
			OptionsObj->TryGetStringField(TEXT("mode"), OutRequest.Options.Mode);
			OptionsObj->TryGetStringField(TEXT("ownership"), OutRequest.Options.Ownership);
			OptionsObj->TryGetBoolField(TEXT("create_if_missing"), OutRequest.Options.bCreateIfMissing);
			OptionsObj->TryGetBoolField(TEXT("clear_unmanaged"), OutRequest.Options.bClearUnmanaged);
			OptionsObj->TryGetBoolField(TEXT("transaction"), OutRequest.Options.bUseTransaction);
			OptionsObj->TryGetBoolField(TEXT("save"), OutRequest.Options.bSave);

			if (!IsSupportedMode(OutRequest.Options.Mode))
			{
				OutErrors.Add(TEXT("invalid_options_mode"));
			}
			else if (OutRequest.Options.Mode.Equals(TEXT("patch"), ESearchCase::CaseSensitive))
			{
				OutErrors.Add(TEXT("unsupported_options_mode_patch"));
			}

			if (!IsSupportedOwnership(OutRequest.Options.Ownership))
			{
				OutErrors.Add(TEXT("invalid_options_ownership"));
			}

			if (OutRequest.Options.bClearUnmanaged)
			{
				OutErrors.Add(TEXT("unsupported_options_clear_unmanaged"));
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* TopLevelParameters = nullptr;
		if (BodyObj->TryGetArrayField(TEXT("parameters"), TopLevelParameters) && TopLevelParameters)
		{
			TryParseParameterArray(TopLevelParameters, OutRequest.Parameters, OutErrors, TEXT("parameters"));
		}

		if (OutRequest.Parameters.Num() > 0)
		{
			FPcgApplyOpSpec ParametersOp;
			ParametersOp.Op = TEXT("parameters.set");
			ParametersOp.SourceOpIndex = INDEX_NONE;
			ParametersOp.ParameterEntries = OutRequest.Parameters;
			OutRequest.Ops.Add(MoveTemp(ParametersOp));
		}

		const TArray<TSharedPtr<FJsonValue>>* OpsArray = nullptr;
		if (!BodyObj->TryGetArrayField(TEXT("ops"), OpsArray) || !OpsArray)
		{
			OutErrors.Add(TEXT("missing_ops"));
			return false;
		}

		TSet<FString> SeenNodeIds;
		for (int32 OpIndex = 0; OpIndex < OpsArray->Num(); ++OpIndex)
		{
			const TSharedPtr<FJsonValue>& OpValue = (*OpsArray)[OpIndex];
			if (!OpValue.IsValid() || OpValue->Type != EJson::Object)
			{
				OutErrors.Add(FString::Printf(TEXT("ops[%d]_must_be_object"), OpIndex));
				continue;
			}

			const TSharedPtr<FJsonObject> OpObj = OpValue->AsObject();
			FPcgApplyOpSpec Op;
			Op.SourceOpIndex = OpIndex;
			if (!OpObj->TryGetStringField(TEXT("op"), Op.Op) || !IsSupportedOp(Op.Op.TrimStartAndEnd()))
			{
				OutErrors.Add(FString::Printf(TEXT("ops[%d]_invalid_op"), OpIndex));
				continue;
			}

			Op.Op = Op.Op.TrimStartAndEnd();

			if (Op.Op.Equals(TEXT("nodes.add"), ESearchCase::CaseSensitive))
			{
				OpObj->TryGetStringField(TEXT("id"), Op.Id);
				Op.Id = Op.Id.TrimStartAndEnd();
				if (OpObj->TryGetStringField(TEXT("type"), Op.Type))
				{
					Op.Type = Op.Type.TrimStartAndEnd();
				}
				if (Op.Type.IsEmpty())
				{
					OutErrors.Add(FString::Printf(TEXT("ops[%d]_missing_type"), OpIndex));
				}
				else if (!FPcgNodeRegistry::IsSupportedType(Op.Type))
				{
					OutErrors.Add(FString::Printf(TEXT("unsupported_node_type:%s"), *Op.Type));
				}
				if (!Op.Id.IsEmpty())
				{
					if (SeenNodeIds.Contains(Op.Id))
					{
						OutErrors.Add(FString::Printf(TEXT("duplicate_node_id:%s"), *Op.Id));
					}
					SeenNodeIds.Add(Op.Id);
				}

				double XValue = 0.0;
				double YValue = 0.0;
				if (OpObj->TryGetNumberField(TEXT("x"), XValue))
				{
					Op.bHasExplicitX = true;
					Op.X = static_cast<int32>(XValue);
				}
				if (OpObj->TryGetNumberField(TEXT("y"), YValue))
				{
					Op.bHasExplicitY = true;
					Op.Y = static_cast<int32>(YValue);
				}

				if (const TSharedPtr<FJsonObject>* SettingsObjPtr = nullptr; OpObj->TryGetObjectField(TEXT("settings"), SettingsObjPtr) && SettingsObjPtr && SettingsObjPtr->IsValid())
				{
					Op.Settings = *SettingsObjPtr;
				}

				OutRequest.Ops.Add(Op);

				if (const TSharedPtr<FJsonObject>* MeshSetObjPtr = nullptr; OpObj->TryGetObjectField(TEXT("mesh_set"), MeshSetObjPtr) && MeshSetObjPtr && MeshSetObjPtr->IsValid())
				{
					if (Op.Id.IsEmpty())
					{
						OutErrors.Add(FString::Printf(TEXT("ops[%d]_mesh_set_requires_id"), OpIndex));
					}

					FPcgApplyOpSpec MeshSetOp;
					MeshSetOp.Op = TEXT("spawners.set_mesh_set");
					MeshSetOp.SourceOpIndex = OpIndex;
					MeshSetOp.Node = Op.Id;
					TryParseMeshSet(*MeshSetObjPtr, MeshSetOp.MeshSet, OutErrors, FString::Printf(TEXT("ops[%d]_mesh_set"), OpIndex));
					OutRequest.Ops.Add(MoveTemp(MeshSetOp));
				}

				continue;
			}

			if (Op.Op.Equals(TEXT("nodes.set"), ESearchCase::CaseSensitive) || Op.Op.Equals(TEXT("nodes.remove"), ESearchCase::CaseSensitive) || Op.Op.Equals(TEXT("spawners.set_mesh_set"), ESearchCase::CaseSensitive))
			{
				if (!OpObj->TryGetStringField(TEXT("node"), Op.Node) || !IsNonEmptyTrimmed(Op.Node))
				{
					OutErrors.Add(FString::Printf(TEXT("ops[%d]_missing_node"), OpIndex));
				}
				Op.Node = Op.Node.TrimStartAndEnd();
			}

			if (Op.Op.Equals(TEXT("nodes.set"), ESearchCase::CaseSensitive))
			{
				if (const TSharedPtr<FJsonObject>* SettingsObjPtr = nullptr; OpObj->TryGetObjectField(TEXT("settings"), SettingsObjPtr) && SettingsObjPtr && SettingsObjPtr->IsValid())
				{
					Op.Settings = *SettingsObjPtr;
				}
				else
				{
					OutErrors.Add(FString::Printf(TEXT("ops[%d]_missing_settings"), OpIndex));
				}
			}

			if (Op.Op.Equals(TEXT("edges.connect"), ESearchCase::CaseSensitive) || Op.Op.Equals(TEXT("edges.disconnect"), ESearchCase::CaseSensitive))
			{
				if (!OpObj->TryGetStringField(TEXT("from"), Op.From) || !TryParsePinAddress(Op.From.TrimStartAndEnd()))
				{
					OutErrors.Add(FString::Printf(TEXT("ops[%d]_invalid_from"), OpIndex));
				}
				if (!OpObj->TryGetStringField(TEXT("to"), Op.To) || !TryParsePinAddress(Op.To.TrimStartAndEnd()))
				{
					OutErrors.Add(FString::Printf(TEXT("ops[%d]_invalid_to"), OpIndex));
				}
				Op.From = Op.From.TrimStartAndEnd();
				Op.To = Op.To.TrimStartAndEnd();
			}

			if (Op.Op.Equals(TEXT("spawners.set_mesh_set"), ESearchCase::CaseSensitive))
			{
				if (const TSharedPtr<FJsonObject>* MeshSetObjPtr = nullptr; OpObj->TryGetObjectField(TEXT("mesh_set"), MeshSetObjPtr) && MeshSetObjPtr && MeshSetObjPtr->IsValid())
				{
					TryParseMeshSet(*MeshSetObjPtr, Op.MeshSet, OutErrors, FString::Printf(TEXT("ops[%d]_mesh_set"), OpIndex));
				}
				else
				{
					FPcgApplyMeshSetSpec MeshSet;
					if (!OpObj->TryGetStringField(TEXT("mode"), MeshSet.Mode))
					{
						OutErrors.Add(FString::Printf(TEXT("ops[%d]_missing_mode"), OpIndex));
					}
					else
					{
						TSharedRef<FJsonObject> InlineMeshSet = MakeShared<FJsonObject>();
						InlineMeshSet->SetStringField(TEXT("mode"), MeshSet.Mode);
						if (const TArray<TSharedPtr<FJsonValue>>* MeshesArray = nullptr; OpObj->TryGetArrayField(TEXT("meshes"), MeshesArray) && MeshesArray)
						{
							InlineMeshSet->SetArrayField(TEXT("meshes"), *MeshesArray);
						}
						if (const TArray<TSharedPtr<FJsonValue>>* CategoriesArray = nullptr; OpObj->TryGetArrayField(TEXT("categories"), CategoriesArray) && CategoriesArray)
						{
							InlineMeshSet->SetArrayField(TEXT("categories"), *CategoriesArray);
						}
						TryParseMeshSet(InlineMeshSet, Op.MeshSet, OutErrors, FString::Printf(TEXT("ops[%d]"), OpIndex));
					}
				}
			}

			if (Op.Op.Equals(TEXT("parameters.set"), ESearchCase::CaseSensitive))
			{
				const TArray<TSharedPtr<FJsonValue>>* EntriesArray = nullptr;
				if (!OpObj->TryGetArrayField(TEXT("entries"), EntriesArray) || !EntriesArray)
				{
					OutErrors.Add(FString::Printf(TEXT("ops[%d]_missing_entries"), OpIndex));
				}
				else
				{
					TryParseParameterArray(EntriesArray, Op.ParameterEntries, OutErrors, FString::Printf(TEXT("ops[%d]_entries"), OpIndex));
				}
			}

			OutRequest.Ops.Add(MoveTemp(Op));
		}

		return OutErrors.Num() == 0;
	}
}
#include "Services/BlueprintGraph/BlueprintGraphNodeService.h"

#include "Core/ForwardAxis.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "K2Node_AddComponent.h"
#include "K2Node_CallFunction.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_Knot.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_SpawnActorFromClass.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/PackageName.h"
#include "Routes/Blueprint/BlueprintGraphApplyRequest.h"
#include "Services/BlueprintGraph/BlueprintGraphLayoutService.h"
#include "Services/BlueprintGraphService.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UnrealType.h"

namespace
{
	static constexpr TCHAR UasNodeIdPrefix[] = TEXT("[BAT:id=");
	static constexpr TCHAR UasNodeIdSuffix[] = TEXT("]");

	struct FResolvedEditableProperty

	{
		FProperty* Property = nullptr;
		void* ValuePtr = nullptr;
	};

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

	static void ApplyForwardAxisToVector(FVector& InOutVector, const FQuat& AxisToUnrealQuat)
	{
		if (!AxisToUnrealQuat.Equals(FQuat::Identity))
		{
			InOutVector = AxisToUnrealQuat.RotateVector(InOutVector);
		}
	}

	static void ApplyForwardAxisToQuat(FQuat& InOutQuat, const FQuat& AxisToUnrealQuat)
	{
		if (!AxisToUnrealQuat.Equals(FQuat::Identity))
		{
			const FQuat AxisFromUnrealQuat = AxisToUnrealQuat.Inverse();
			InOutQuat = (AxisToUnrealQuat * InOutQuat * AxisFromUnrealQuat).GetNormalized();
		}
	}

	static void ApplyForwardAxisToTransform(FTransform& InOutTransform, const FQuat& AxisToUnrealQuat)
	{
		if (!AxisToUnrealQuat.Equals(FQuat::Identity))
		{
			FVector Location = InOutTransform.GetLocation();
			FQuat Rotation = InOutTransform.GetRotation();
			ApplyForwardAxisToVector(Location, AxisToUnrealQuat);
			ApplyForwardAxisToQuat(Rotation, AxisToUnrealQuat);
			InOutTransform.SetLocation(Location);
			InOutTransform.SetRotation(Rotation);
		}
	}

	static bool TryResolveGraphNodeClass(const FString& NodeType, UClass*& OutClass)
	{
		OutClass = nullptr;
		if (NodeType.IsEmpty())
		{
			return false;
		}

		auto TryClassPath = [&OutClass](const FString& ClassPath) -> bool
		{
			if (ClassPath.IsEmpty())
			{
				return false;
			}

			OutClass = FindObject<UClass>(nullptr, *ClassPath);
			if (!OutClass)
			{
				OutClass = LoadObject<UClass>(nullptr, *ClassPath);
			}
			if (!OutClass)
			{
				OutClass = LoadClass<UEdGraphNode>(nullptr, *ClassPath);
			}
			return OutClass != nullptr;
		};

		if (NodeType.StartsWith(TEXT("/Script/"), ESearchCase::CaseSensitive) && TryClassPath(NodeType))
		{
			return true;
		}
		if (TryClassPath(FString::Printf(TEXT("/Script/AnimGraph.%s"), *NodeType)))
		{
			return true;
		}
		if (TryClassPath(FString::Printf(TEXT("/Script/ControlRigDeveloper.%s"), *NodeType)))
		{
			return true;
		}

		return false;
	}

	static bool ResolveEditablePropertyPath(UStruct* RootStruct, void* RootPtr, const FString& PropertyPath, FResolvedEditableProperty& OutResolved, FString& OutError)
	{
		OutResolved = FResolvedEditableProperty();
		OutError.Reset();
		if (!RootStruct || !RootPtr)
		{
			OutError = TEXT("invalid_root");
			return false;
		}

		TArray<FString> Segments;
		PropertyPath.ParseIntoArray(Segments, TEXT("."), true);
		if (Segments.Num() == 0)
		{
			OutError = TEXT("empty_property_path");
			return false;
		}

		UStruct* CurrentStruct = RootStruct;
		void* CurrentPtr = RootPtr;
		for (int32 Index = 0; Index < Segments.Num(); ++Index)
		{
			const FName SegmentName(*Segments[Index]);
			FProperty* Property = FindFProperty<FProperty>(CurrentStruct, SegmentName);
			if (!Property)
			{
				OutError = FString::Printf(TEXT("property_not_found:%s"), *Segments[Index]);
				return false;
			}

			void* ValuePtr = Property->ContainerPtrToValuePtr<void>(CurrentPtr);
			if (Index == Segments.Num() - 1)
			{
				OutResolved.Property = Property;
				OutResolved.ValuePtr = ValuePtr;
				return true;
			}

			if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
			{
				CurrentStruct = StructProperty->Struct;
				CurrentPtr = ValuePtr;
				continue;
			}

			OutError = FString::Printf(TEXT("non_struct_intermediate:%s"), *Segments[Index]);
			return false;
		}

		OutError = TEXT("property_not_resolved");
		return false;
	}

	static bool TryApplyAxisAwareNodeProperty(UEdGraphNode* Node, const FResolvedEditableProperty& Resolved, const FString& PropertyPath, const FString& RawValue, const FString& ForwardAxis, FBlueprintGraphApplyResult& InOutResult, const FString& NodeId)
	{
		if (!Node)
		{
			return false;
		}

		FQuat AxisToUnrealQuat = FQuat::Identity;
		FString AxisError;
		if (!BAT::ForwardAxis::TryBuildAxisToUnrealQuat(ForwardAxis, AxisToUnrealQuat, AxisError))
		{
			InOutResult.Errors.Add(FString::Printf(TEXT("node_property_axis_failed:%s:%s:%s"), *NodeId, *PropertyPath, *AxisError));
			return true;
		}
		if (AxisToUnrealQuat.Equals(FQuat::Identity))
		{
			return false;
		}

		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Resolved.Property))
		{
			if (StructProperty->Struct == TBaseStructure<FVector>::Get())
			{
				FVector ParsedValue = FVector::ZeroVector;
				if (StructProperty->ImportText_Direct(*RawValue, &ParsedValue, Node, PPF_None) == nullptr)
				{
					InOutResult.Errors.Add(FString::Printf(TEXT("node_property_import_failed:%s:%s"), *NodeId, *PropertyPath));
					return true;
				}
				ApplyForwardAxisToVector(ParsedValue, AxisToUnrealQuat);
				*static_cast<FVector*>(Resolved.ValuePtr) = ParsedValue;
				return true;
			}
			if (StructProperty->Struct == TBaseStructure<FRotator>::Get())
			{
				FRotator ParsedValue = FRotator::ZeroRotator;
				if (StructProperty->ImportText_Direct(*RawValue, &ParsedValue, Node, PPF_None) == nullptr)
				{
					InOutResult.Errors.Add(FString::Printf(TEXT("node_property_import_failed:%s:%s"), *NodeId, *PropertyPath));
					return true;
				}
				FQuat ParsedQuat = ParsedValue.Quaternion();
				ApplyForwardAxisToQuat(ParsedQuat, AxisToUnrealQuat);
				*static_cast<FRotator*>(Resolved.ValuePtr) = ParsedQuat.Rotator();
				return true;
			}
			if (StructProperty->Struct == TBaseStructure<FQuat>::Get())
			{
				FQuat ParsedValue = FQuat::Identity;
				if (StructProperty->ImportText_Direct(*RawValue, &ParsedValue, Node, PPF_None) == nullptr)
				{
					InOutResult.Errors.Add(FString::Printf(TEXT("node_property_import_failed:%s:%s"), *NodeId, *PropertyPath));
					return true;
				}
				ApplyForwardAxisToQuat(ParsedValue, AxisToUnrealQuat);
				*static_cast<FQuat*>(Resolved.ValuePtr) = ParsedValue;
				return true;
			}
			if (StructProperty->Struct == TBaseStructure<FTransform>::Get())
			{
				FTransform ParsedValue = FTransform::Identity;
				if (StructProperty->ImportText_Direct(*RawValue, &ParsedValue, Node, PPF_None) == nullptr)
				{
					InOutResult.Errors.Add(FString::Printf(TEXT("node_property_import_failed:%s:%s"), *NodeId, *PropertyPath));
					return true;
				}
				ApplyForwardAxisToTransform(ParsedValue, AxisToUnrealQuat);
				*static_cast<FTransform*>(Resolved.ValuePtr) = ParsedValue;
				return true;
			}
		}

		return false;
	}

	static void ApplyNodeProperties(UEdGraphNode* Node, const TMap<FString, FString>& Properties, const FString& ForwardAxis, FBlueprintGraphApplyResult& InOutResult, const FString& NodeId)
	{
		if (!Node)
		{
			return;
		}

		for (const TPair<FString, FString>& PropertyPair : Properties)
		{
			FResolvedEditableProperty Resolved;
			FString ResolveError;
			if (!ResolveEditablePropertyPath(Node->GetClass(), Node, PropertyPair.Key, Resolved, ResolveError))
			{
				InOutResult.Errors.Add(FString::Printf(TEXT("node_property_resolve_failed:%s:%s:%s"), *NodeId, *PropertyPair.Key, *ResolveError));
				continue;
			}

			Resolved.Property->SetPropertyFlags(CPF_Edit);
			if (!ForwardAxis.TrimStartAndEnd().IsEmpty() && TryApplyAxisAwareNodeProperty(Node, Resolved, PropertyPair.Key, PropertyPair.Value, ForwardAxis, InOutResult, NodeId))
			{
				continue;
			}

			if (Resolved.Property->ImportText_Direct(*PropertyPair.Value, Resolved.ValuePtr, Node, PPF_None) == nullptr)
			{
				InOutResult.Errors.Add(FString::Printf(TEXT("node_property_import_failed:%s:%s"), *NodeId, *PropertyPair.Key));
			}
		}
	}

	static bool ExportResolvedPropertyText(UEdGraphNode* Node, const FResolvedEditableProperty& Resolved, FString& OutValue)
	{
		OutValue.Reset();
		if (!Node || !Resolved.Property || !Resolved.ValuePtr)
		{
			return false;
		}

		Resolved.Property->ExportTextItem_Direct(OutValue, Resolved.ValuePtr, nullptr, Node, PPF_None);
		return true;
	}

	static void AddValidationIssue(TArray<FBlueprintGraphNodeValidationIssue>& OutIssues, const FString& NodeId, const FString& Code, const FString& Message, const FString& PropertyPath = FString())
	{
		FBlueprintGraphNodeValidationIssue& Issue = OutIssues.AddDefaulted_GetRef();
		Issue.NodeId = NodeId;
		Issue.Code = Code;
		Issue.Message = Message;
		Issue.PropertyPath = PropertyPath;
	}

	static bool TryExportNodePropertyTextImpl(UEdGraphNode* Node, const FString& PropertyPath, FString& OutValue, FString& OutError)
	{
		FResolvedEditableProperty Resolved;
		if (!ResolveEditablePropertyPath(Node ? Node->GetClass() : nullptr, Node, PropertyPath, Resolved, OutError))
		{
			OutValue.Reset();
			return false;
		}

		if (!ExportResolvedPropertyText(Node, Resolved, OutValue))
		{
			OutError = TEXT("property_export_failed");
			return false;
		}

		OutError.Reset();
		return true;
	}

	static void GetDefaultInspectionPropertyPathsImpl(UEdGraphNode* Node, TArray<FString>& OutPropertyPaths)
	{
		OutPropertyPaths.Reset();
		if (!Node || !Node->GetClass())
		{
			return;
		}

		const FString ClassName = Node->GetClass()->GetName();
		if (ClassName.Equals(TEXT("AnimGraphNode_ModifyBone"), ESearchCase::CaseSensitive))
		{
			OutPropertyPaths = {
				TEXT("Node.BoneToModify.BoneName"),
				TEXT("Node.TranslationMode"),
				TEXT("Node.RotationMode"),
				TEXT("Node.ScaleMode"),
				TEXT("Node.TranslationSpace"),
				TEXT("Node.RotationSpace"),
				TEXT("Node.ScaleSpace")
			};
			return;
		}

		if (ClassName.Equals(TEXT("AnimGraphNode_LayeredBoneBlend"), ESearchCase::CaseSensitive))
		{
			OutPropertyPaths = {
				TEXT("Node.BlendWeights"),
				TEXT("Node.LayerSetup")
			};
			return;
		}

		if (ClassName.Equals(TEXT("AnimGraphNode_SequencePlayer"), ESearchCase::CaseSensitive))
		{
			OutPropertyPaths = {
				TEXT("Node.Sequence")
			};
		}
	}

	static void CollectNodeValidationIssuesImpl(UEdGraphNode* Node, const FString& NodeId, TArray<FBlueprintGraphNodeValidationIssue>& OutIssues)
	{
		if (!Node || !Node->GetClass())
		{
			return;
		}

		if (!Node->GetClass()->GetName().Equals(TEXT("AnimGraphNode_ModifyBone"), ESearchCase::CaseSensitive))
		{
			return;
		}

		FString BoneName;
		FString RotationMode;
		FString TranslationMode;
		FString ScaleMode;
		FString Error;
		TryExportNodePropertyTextImpl(Node, TEXT("Node.BoneToModify.BoneName"), BoneName, Error);
		TryExportNodePropertyTextImpl(Node, TEXT("Node.RotationMode"), RotationMode, Error);
		TryExportNodePropertyTextImpl(Node, TEXT("Node.TranslationMode"), TranslationMode, Error);
		TryExportNodePropertyTextImpl(Node, TEXT("Node.ScaleMode"), ScaleMode, Error);

		const FString TrimmedBoneName = BoneName.TrimStartAndEnd();
		if (TrimmedBoneName.IsEmpty() || TrimmedBoneName.Equals(TEXT("None"), ESearchCase::IgnoreCase))
		{
			AddValidationIssue(OutIssues, NodeId, TEXT("modify_bone_missing_bone"), TEXT("Transform Modify Bone requires BoneToModify.BoneName."), TEXT("Node.BoneToModify.BoneName"));
		}

		const bool bTranslationIgnored = TranslationMode.Contains(TEXT("BMM_Ignore"), ESearchCase::CaseSensitive);
		const bool bRotationIgnored = RotationMode.Contains(TEXT("BMM_Ignore"), ESearchCase::CaseSensitive);
		const bool bScaleIgnored = ScaleMode.Contains(TEXT("BMM_Ignore"), ESearchCase::CaseSensitive);
		if (bTranslationIgnored && bRotationIgnored && bScaleIgnored)
		{
			AddValidationIssue(OutIssues, NodeId, TEXT("modify_bone_no_active_component"), TEXT("Transform Modify Bone must modify translation, rotation, or scale."));
		}
	}

	static void AddUniquePinLookupCandidate(TArray<FString>& Candidates, const FString& Candidate)
	{
		if (!Candidate.IsEmpty())
		{
			Candidates.AddUnique(Candidate);
		}
	}

	static void BuildPinLookupCandidates(const FString& PinName, TArray<FString>& OutCandidates)
	{
		OutCandidates.Reset();
		AddUniquePinLookupCandidate(OutCandidates, PinName);
		if (PinName.Equals(TEXT("Then"), ESearchCase::IgnoreCase))
		{
			AddUniquePinLookupCandidate(OutCandidates, TEXT("then"));
		}
		if (PinName.Equals(TEXT("then"), ESearchCase::IgnoreCase))
		{
			AddUniquePinLookupCandidate(OutCandidates, TEXT("Then"));
		}
		if (PinName.Equals(TEXT("Exec"), ESearchCase::IgnoreCase))
		{
			AddUniquePinLookupCandidate(OutCandidates, TEXT("execute"));
		}
		if (PinName.Equals(TEXT("execute"), ESearchCase::IgnoreCase))
		{
			AddUniquePinLookupCandidate(OutCandidates, TEXT("Exec"));
		}
		if (PinName.Equals(TEXT("ReturnValue"), ESearchCase::IgnoreCase))
		{
			AddUniquePinLookupCandidate(OutCandidates, TEXT("Return"));
		}
		if (PinName.Equals(TEXT("Return"), ESearchCase::IgnoreCase))
		{
			AddUniquePinLookupCandidate(OutCandidates, TEXT("ReturnValue"));
		}
	}

	static UObject* ResolveObjectForPinDefault(const FString& Value, bool bPreferGeneratedClass)
	{
		FString ObjectPath = Value;
		ObjectPath.TrimStartAndEndInline();
		if (!ObjectPath.StartsWith(TEXT("/")))
		{
			return nullptr;
		}

		const FString Normalized = NormalizeBlueprintObjectPath(ObjectPath);
		auto TryLoadGeneratedClass = [&]() -> UClass*
		{
			if (UBlueprint* BlueprintAsset = LoadObject<UBlueprint>(nullptr, *Normalized))
			{
				if (BlueprintAsset->GeneratedClass)
				{
					return BlueprintAsset->GeneratedClass;
				}
			}
			if (UClass* LoadedClass = LoadClass<UObject>(nullptr, *ObjectPath))
			{
				return LoadedClass;
			}
			const FString GeneratedClassPath = ObjectPath.EndsWith(TEXT("_C")) ? ObjectPath : ObjectPath + TEXT("_C");
			return LoadClass<UObject>(nullptr, *GeneratedClassPath);
		};

		if (bPreferGeneratedClass)
		{
			if (UClass* GeneratedClass = TryLoadGeneratedClass())
			{
				return GeneratedClass;
			}
		}

		if (UObject* Loaded = LoadObject<UObject>(nullptr, *ObjectPath))
		{
			if (bPreferGeneratedClass)
			{
				if (UBlueprint* BlueprintAsset = Cast<UBlueprint>(Loaded))
				{
					if (BlueprintAsset->GeneratedClass)
					{
						return BlueprintAsset->GeneratedClass;
					}
				}
			}
			return Loaded;
		}

		if (Normalized != ObjectPath)
		{
			if (UBlueprint* BlueprintAsset = LoadObject<UBlueprint>(nullptr, *Normalized))
			{
				if (bPreferGeneratedClass && BlueprintAsset->GeneratedClass)
				{
					return BlueprintAsset->GeneratedClass;
				}
				return BlueprintAsset;
			}
		}

		if (UClass* LoadedClass = LoadClass<UObject>(nullptr, *ObjectPath))
		{
			return LoadedClass;
		}
		if (!bPreferGeneratedClass)
		{
			const FString GeneratedClassPath = ObjectPath.EndsWith(TEXT("_C")) ? ObjectPath : ObjectPath + TEXT("_C");
			return LoadClass<UObject>(nullptr, *GeneratedClassPath);
		}

		return nullptr;
	}

	static bool TryResolveFunctionByPath(const FString& FunctionPath, UFunction*& OutFunction)
	{
		OutFunction = nullptr;
		FString TrimmedFunctionPath = FunctionPath;
		TrimmedFunctionPath.TrimStartAndEndInline();
		if (TrimmedFunctionPath.IsEmpty())
		{
			return false;
		}

		FString ClassPath;
		FString FunctionName;
		if (!TrimmedFunctionPath.Split(TEXT(":"), &ClassPath, &FunctionName, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
		{
			return false;
		}
		ClassPath.TrimStartAndEndInline();
		FunctionName.TrimStartAndEndInline();
		if (ClassPath.IsEmpty() || FunctionName.IsEmpty())
		{
			return false;
		}

		UClass* OwnerClass = LoadObject<UClass>(nullptr, *ClassPath);
		if (!OwnerClass)
		{
			return false;
		}
		OutFunction = OwnerClass->FindFunctionByName(FName(*FunctionName));
		return OutFunction != nullptr;
	}

	static bool TryResolveClassByPath(const FString& ClassPath, UClass*& OutClass)
	{
		OutClass = nullptr;
		FString TrimmedClassPath = ClassPath;
		TrimmedClassPath.TrimStartAndEndInline();
		if (TrimmedClassPath.IsEmpty())
		{
			return false;
		}

		OutClass = LoadObject<UClass>(nullptr, *TrimmedClassPath);
		if (!OutClass)
		{
			OutClass = FindObject<UClass>(nullptr, *TrimmedClassPath);
		}
		if (!OutClass)
		{
			OutClass = LoadClass<UObject>(nullptr, *TrimmedClassPath);
		}
		return OutClass != nullptr;
	}

	static UEdGraph* FindGraphByNameInBlueprint(UBlueprint* Blueprint, const FString& GraphName)
	{
		if (!Blueprint)
		{
			return nullptr;
		}

		auto FindInArray = [&GraphName](const TArray<UEdGraph*>& Graphs) -> UEdGraph*
		{
			for (UEdGraph* Graph : Graphs)
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

		if (UEdGraph* Found = FindInArray(Blueprint->UbergraphPages))
		{
			return Found;
		}
		if (UEdGraph* Found = FindInArray(Blueprint->FunctionGraphs))
		{
			return Found;
		}
		if (UEdGraph* Found = FindInArray(Blueprint->MacroGraphs))
		{
			return Found;
		}

		return nullptr;
	}

	static bool TryResolveMacroGraph(const FString& MacroPath, UEdGraph*& OutMacroGraph)
	{
		OutMacroGraph = nullptr;
		FString TrimmedMacroPath = MacroPath;
		TrimmedMacroPath.TrimStartAndEndInline();
		if (TrimmedMacroPath.IsEmpty())
		{
			return false;
		}

		FString BlueprintPath;
		FString MacroName;
		if (!TrimmedMacroPath.Split(TEXT(":"), &BlueprintPath, &MacroName, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
		{
			return false;
		}
		BlueprintPath.TrimStartAndEndInline();
		MacroName.TrimStartAndEndInline();
		if (BlueprintPath.IsEmpty() || MacroName.IsEmpty())
		{
			return false;
		}

		const FString ObjectPath = NormalizeBlueprintObjectPath(BlueprintPath);
		UBlueprint* MacroBlueprint = LoadObject<UBlueprint>(nullptr, *ObjectPath);
		if (!MacroBlueprint)
		{
			return false;
		}

		OutMacroGraph = FindGraphByNameInBlueprint(MacroBlueprint, MacroName);
		return OutMacroGraph != nullptr;
	}

	static void SetPinDefault(UEdGraphPin* Pin, const FString& Value, FBlueprintGraphApplyResult& InOutResult, const FString& NodeId, const FString& PinName)
	{
		if (!Pin)
		{
			return;
		}

		const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
		const bool bClassLikePin = Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Class
			|| Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_SoftClass;
		const bool bObjectLikePin = Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object
			|| Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Class
			|| Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_SoftObject
			|| Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_SoftClass;

		if (bObjectLikePin && Value.StartsWith(TEXT("/")))
		{
			if (UObject* DefaultObject = ResolveObjectForPinDefault(Value, bClassLikePin))
			{
				Pin->DefaultObject = DefaultObject;
				Pin->DefaultValue.Reset();
				Pin->DefaultTextValue = FText::GetEmpty();
				if (K2Schema)
				{
					K2Schema->TrySetDefaultObject(*Pin, DefaultObject);
				}
				return;
			}

			InOutResult.Warnings.Add(FString::Printf(TEXT("pin_default_object_not_found:%s.%s"), *NodeId, *PinName));
		}

		if (K2Schema)
		{
			K2Schema->TrySetDefaultValue(*Pin, Value);
		}
		else
		{
			Pin->DefaultValue = Value;
		}
	}

	static void EnsureSequenceOutputPins(UK2Node_ExecutionSequence* SequenceNode, int32 DesiredOutputs)
	{
		if (!SequenceNode)
		{
			return;
		}

		const int32 SafeOutputs = FMath::Max(2, DesiredOutputs);
		int32 ExistingOutputs = 0;
		for (UEdGraphPin* Pin : SequenceNode->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Output && UEdGraphSchema_K2::IsExecPin(*Pin))
			{
				++ExistingOutputs;
			}
		}

		while (ExistingOutputs < SafeOutputs)
		{
			SequenceNode->AddInputPin();
			++ExistingOutputs;
		}
	}

	static UEdGraphNode* CreateNodeFromSpec(UEdGraph* Graph, UBlueprint* Blueprint, const FBlueprintGraphApplyNodeSpec& NodeSpec, FBlueprintGraphApplyResult& InOutResult)
	{
		if (!Graph || !Blueprint)
		{
			return nullptr;
		}

		if (NodeSpec.Type.Equals(TEXT("K2Node_Event"), ESearchCase::CaseSensitive))
		{
			UK2Node_Event* EventNode = NewObject<UK2Node_Event>(Graph);
			EventNode->EventReference.SetExternalMember(FName(TEXT("ReceiveBeginPlay")), AActor::StaticClass());
			EventNode->bOverrideFunction = true;
			Graph->AddNode(EventNode, true, false);
			EventNode->CreateNewGuid();
			EventNode->PostPlacedNewNode();
			EventNode->AllocateDefaultPins();
			EventNode->ReconstructNode();
			return EventNode;
		}

		if (NodeSpec.Type.Equals(TEXT("K2Node_SpawnActor"), ESearchCase::CaseSensitive))
		{
			UK2Node_SpawnActorFromClass* SpawnNode = NewObject<UK2Node_SpawnActorFromClass>(Graph);
			Graph->AddNode(SpawnNode, true, false);
			SpawnNode->CreateNewGuid();
			SpawnNode->PostPlacedNewNode();
			SpawnNode->AllocateDefaultPins();
			SpawnNode->ReconstructNode();

			if (UEdGraphPin* ClassPin = FBlueprintGraphNodeService::FindPinSmart(SpawnNode, TEXT("Class")))
			{
				if (UObject* ClassObject = ResolveObjectForPinDefault(NodeSpec.ClassPath, true))
				{
					if (UClass* SpawnClass = Cast<UClass>(ClassObject))
					{
						ClassPin->DefaultObject = SpawnClass;
						ClassPin->DefaultValue.Reset();
					}
					else
					{
						InOutResult.Errors.Add(FString::Printf(TEXT("node_class_not_class:%s"), *NodeSpec.Id));
					}
				}
				else
				{
					InOutResult.Errors.Add(FString::Printf(TEXT("node_class_not_found:%s"), *NodeSpec.Id));
				}
			}
			else
			{
				InOutResult.Errors.Add(FString::Printf(TEXT("node_missing_class_pin:%s"), *NodeSpec.Id));
			}

			return SpawnNode;
		}

		if (NodeSpec.Type.Equals(TEXT("K2Node_PrintString"), ESearchCase::CaseSensitive)
			|| NodeSpec.Type.Equals(TEXT("K2Node_CallFunction"), ESearchCase::CaseSensitive))
		{
			FString FunctionPath = NodeSpec.Function;
			if (NodeSpec.Type.Equals(TEXT("K2Node_PrintString"), ESearchCase::CaseSensitive) && FunctionPath.TrimStartAndEnd().IsEmpty())
			{
				FunctionPath = TEXT("/Script/Engine.KismetSystemLibrary:PrintString");
			}
			UFunction* TargetFunction = nullptr;
			if (!TryResolveFunctionByPath(FunctionPath, TargetFunction))
			{
				InOutResult.Errors.Add(FString::Printf(TEXT("node_function_not_found:%s"), *NodeSpec.Id));
				return nullptr;
			}

			UK2Node_CallFunction* CallFunctionNode = NewObject<UK2Node_CallFunction>(Graph);
			CallFunctionNode->SetFromFunction(TargetFunction);
			Graph->AddNode(CallFunctionNode, true, false);
			CallFunctionNode->CreateNewGuid();
			CallFunctionNode->PostPlacedNewNode();
			CallFunctionNode->AllocateDefaultPins();
			CallFunctionNode->ReconstructNode();

			if (NodeSpec.Type.Equals(TEXT("K2Node_PrintString"), ESearchCase::CaseSensitive))
			{
				if (UEdGraphPin* InStringPin = FBlueprintGraphNodeService::FindPinSmart(CallFunctionNode, TEXT("InString")))
				{
					SetPinDefault(InStringPin, NodeSpec.Message, InOutResult, NodeSpec.Id, TEXT("InString"));
				}
				else
				{
					InOutResult.Warnings.Add(FString::Printf(TEXT("node_missing_instring_pin:%s"), *NodeSpec.Id));
				}
			}

			return CallFunctionNode;
		}

		if (NodeSpec.Type.Equals(TEXT("K2Node_AddComponent"), ESearchCase::CaseSensitive))
		{
			UClass* ComponentClass = nullptr;
			if (!TryResolveClassByPath(NodeSpec.ClassPath, ComponentClass))
			{
				InOutResult.Errors.Add(FString::Printf(TEXT("node_class_not_found:%s"), *NodeSpec.Id));
				return nullptr;
			}

			UK2Node_AddComponent* AddComponentNode = NewObject<UK2Node_AddComponent>(Graph);
			UFunction* AddComponentFn = FindFieldChecked<UFunction>(AActor::StaticClass(), UK2Node_AddComponent::GetAddComponentFunctionName());
			AddComponentNode->FunctionReference.SetFromField<UFunction>(AddComponentFn, FBlueprintEditorUtils::IsActorBased(Blueprint));
			AddComponentNode->TemplateType = ComponentClass;
			Graph->AddNode(AddComponentNode, true, false);
			AddComponentNode->CreateNewGuid();
			AddComponentNode->PostPlacedNewNode();
			AddComponentNode->AllocateDefaultPins();
			AddComponentNode->ReconstructNode();
			if (UEdGraphPin* ReturnPin = AddComponentNode->GetReturnValuePin())
			{
				ReturnPin->PinType.PinSubCategoryObject = ComponentClass;
			}
			AddComponentNode->MakeNewComponentTemplate();
			return AddComponentNode;
		}

		if (NodeSpec.Type.Equals(TEXT("K2Node_VariableGet"), ESearchCase::CaseSensitive))
		{
			UK2Node_VariableGet* VariableGetNode = NewObject<UK2Node_VariableGet>(Graph);
			VariableGetNode->VariableReference.SetSelfMember(FName(*NodeSpec.Variable));
			Graph->AddNode(VariableGetNode, true, false);
			VariableGetNode->CreateNewGuid();
			VariableGetNode->PostPlacedNewNode();
			VariableGetNode->AllocateDefaultPins();
			VariableGetNode->ReconstructNode();
			return VariableGetNode;
		}

		if (NodeSpec.Type.Equals(TEXT("K2Node_VariableSet"), ESearchCase::CaseSensitive))
		{
			const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
			if (!Schema)
			{
				InOutResult.Errors.Add(FString::Printf(TEXT("schema_not_available:%s"), *NodeSpec.Id));
				return nullptr;
			}

			UStruct* VariableSource = Blueprint->SkeletonGeneratedClass ? static_cast<UStruct*>(Blueprint->SkeletonGeneratedClass) : static_cast<UStruct*>(Blueprint->ParentClass);
			UK2Node_VariableSet* VariableSetNode = Schema->SpawnVariableSetNode(FVector2D((float)NodeSpec.X, (float)NodeSpec.Y), Graph, FName(*NodeSpec.Variable), VariableSource);
			if (!VariableSetNode)
			{
				InOutResult.Errors.Add(FString::Printf(TEXT("node_variable_set_spawn_failed:%s"), *NodeSpec.Id));
			}
			return VariableSetNode;
		}

		if (NodeSpec.Type.Equals(TEXT("K2Node_ExecutionSequence"), ESearchCase::CaseSensitive)
			|| NodeSpec.Type.Equals(TEXT("K2Node_Sequence"), ESearchCase::CaseSensitive))
		{
			UK2Node_ExecutionSequence* SequenceNode = NewObject<UK2Node_ExecutionSequence>(Graph);
			Graph->AddNode(SequenceNode, true, false);
			SequenceNode->CreateNewGuid();
			SequenceNode->PostPlacedNewNode();
			SequenceNode->AllocateDefaultPins();
			EnsureSequenceOutputPins(SequenceNode, NodeSpec.Outputs);
			SequenceNode->ReconstructNode();
			EnsureSequenceOutputPins(SequenceNode, NodeSpec.Outputs);
			return SequenceNode;
		}

		if (NodeSpec.Type.Equals(TEXT("K2Node_Delay"), ESearchCase::CaseSensitive))
		{
			FBlueprintGraphApplyNodeSpec DelaySpec = NodeSpec;
			DelaySpec.Type = TEXT("K2Node_CallFunction");
			if (DelaySpec.Function.TrimStartAndEnd().IsEmpty())
			{
				DelaySpec.Function = TEXT("/Script/Engine.KismetSystemLibrary:Delay");
			}
			return CreateNodeFromSpec(Graph, Blueprint, DelaySpec, InOutResult);
		}

		if (NodeSpec.Type.Equals(TEXT("K2Node_Knot"), ESearchCase::CaseSensitive)
			|| NodeSpec.Type.Equals(TEXT("K2Node_Reroute"), ESearchCase::CaseSensitive))
		{
			UK2Node_Knot* KnotNode = NewObject<UK2Node_Knot>(Graph);
			Graph->AddNode(KnotNode, true, false);
			KnotNode->CreateNewGuid();
			KnotNode->PostPlacedNewNode();
			KnotNode->AllocateDefaultPins();
			KnotNode->ReconstructNode();
			return KnotNode;
		}

		if (NodeSpec.Type.Equals(TEXT("K2Node_MacroInstance"), ESearchCase::CaseSensitive))
		{
			UEdGraph* MacroGraph = nullptr;
			if (!TryResolveMacroGraph(NodeSpec.Macro, MacroGraph))
			{
				InOutResult.Errors.Add(FString::Printf(TEXT("node_macro_not_found:%s"), *NodeSpec.Id));
				return nullptr;
			}

			UK2Node_MacroInstance* MacroNode = NewObject<UK2Node_MacroInstance>(Graph);
			MacroNode->SetMacroGraph(MacroGraph);
			Graph->AddNode(MacroNode, true, false);
			MacroNode->CreateNewGuid();
			MacroNode->PostPlacedNewNode();
			MacroNode->AllocateDefaultPins();
			MacroNode->ReconstructNode();
			return MacroNode;
		}

		if (NodeSpec.Type.StartsWith(TEXT("AnimGraphNode_"), ESearchCase::CaseSensitive)
			|| NodeSpec.Type.StartsWith(TEXT("/Script/AnimGraph."), ESearchCase::CaseSensitive)
			|| NodeSpec.Type.StartsWith(TEXT("/Script/ControlRigDeveloper."), ESearchCase::CaseSensitive))
		{
			UClass* NodeClass = nullptr;
			if (!TryResolveGraphNodeClass(NodeSpec.Type, NodeClass) || !NodeClass || !NodeClass->IsChildOf(UEdGraphNode::StaticClass()))
			{
				InOutResult.Errors.Add(FString::Printf(TEXT("node_class_not_found:%s"), *NodeSpec.Id));
				return nullptr;
			}

			UEdGraphNode* GraphNode = NewObject<UEdGraphNode>(Graph, NodeClass);
			Graph->AddNode(GraphNode, true, false);
			GraphNode->CreateNewGuid();
			GraphNode->PostPlacedNewNode();
			GraphNode->AllocateDefaultPins();
			GraphNode->ReconstructNode();
			return GraphNode;
		}

		InOutResult.Errors.Add(FString::Printf(TEXT("unsupported_node_type:%s"), *NodeSpec.Type));
		return nullptr;
	}
}

bool FBlueprintGraphNodeService::TryExportNodePropertyText(UEdGraphNode* Node, const FString& PropertyPath, FString& OutValue, FString& OutError)
{
	return TryExportNodePropertyTextImpl(Node, PropertyPath, OutValue, OutError);
}

void FBlueprintGraphNodeService::GetDefaultInspectionPropertyPaths(UEdGraphNode* Node, TArray<FString>& OutPropertyPaths)
{
	GetDefaultInspectionPropertyPathsImpl(Node, OutPropertyPaths);
}

void FBlueprintGraphNodeService::CollectNodeValidationIssues(UEdGraphNode* Node, const FString& NodeId, TArray<FBlueprintGraphNodeValidationIssue>& OutIssues)
{
	CollectNodeValidationIssuesImpl(Node, NodeId, OutIssues);
}

UEdGraphNode* FBlueprintGraphNodeService::FindNodeByUasId(UEdGraph* Graph, const FString& NodeId)
{
	if (!Graph || NodeId.IsEmpty())
	{
		return nullptr;
	}

	FGuid ParsedGuid;
	if (FGuid::Parse(NodeId, ParsedGuid))
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->NodeGuid == ParsedGuid)
			{
				return Node;
			}
		}
	}

	const FString Prefix(UasNodeIdPrefix);
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node)
		{
			continue;
		}

		const FString& Comment = Node->NodeComment;
		const int32 PrefixPos = Comment.Find(Prefix, ESearchCase::CaseSensitive);
		if (PrefixPos == INDEX_NONE)
		{
			continue;
		}

		const int32 IdStart = PrefixPos + Prefix.Len();
		const int32 SuffixPos = Comment.Find(UasNodeIdSuffix, ESearchCase::CaseSensitive, ESearchDir::FromStart, IdStart);
		if (SuffixPos == INDEX_NONE || SuffixPos <= IdStart)
		{
			continue;
		}

		const FString ParsedId = Comment.Mid(IdStart, SuffixPos - IdStart);
		if (ParsedId.Equals(NodeId, ESearchCase::CaseSensitive))
		{
			return Node;
		}
	}

	return nullptr;
}

void FBlueprintGraphNodeService::SetNodeUasId(UEdGraphNode* Node, const FString& NodeId)
{
	if (!Node)
	{
		return;
	}

	const FString Prefix(UasNodeIdPrefix);
	const FString Marker = FString::Printf(TEXT("%s%s%s"), *Prefix, *NodeId, UasNodeIdSuffix);
	FString Comment = Node->NodeComment;
	const int32 PrefixPos = Comment.Find(Prefix, ESearchCase::CaseSensitive);
	if (PrefixPos != INDEX_NONE)
	{
		const int32 IdStart = PrefixPos + Prefix.Len();
		const int32 SuffixPos = Comment.Find(UasNodeIdSuffix, ESearchCase::CaseSensitive, ESearchDir::FromStart, IdStart);
		if (SuffixPos != INDEX_NONE)
		{
			Comment.RemoveAt(PrefixPos, (SuffixPos - PrefixPos) + 1);
			Comment.TrimStartAndEndInline();
		}
	}

	Node->NodeComment = Comment.IsEmpty() ? Marker : FString::Printf(TEXT("%s %s"), *Marker, *Comment);
	Node->bCommentBubbleVisible = true;
}

UEdGraphPin* FBlueprintGraphNodeService::FindPinSmart(UEdGraphNode* Node, const FString& PinName)
{
	if (!Node)
	{
		return nullptr;
	}

	TArray<FString> CandidateNames;
	BuildPinLookupCandidates(PinName, CandidateNames);
	for (const FString& CandidateName : CandidateNames)
	{
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->PinName.ToString().Equals(CandidateName, ESearchCase::CaseSensitive))
			{
				return Pin;
			}
		}
	}
	for (const FString& CandidateName : CandidateNames)
	{
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->PinName.ToString().Equals(CandidateName, ESearchCase::IgnoreCase))
			{
				return Pin;
			}
		}
	}
	for (const FString& CandidateName : CandidateNames)
	{
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin)
			{
				const FString Friendly = Pin->PinFriendlyName.ToString();
				if (!Friendly.IsEmpty() && Friendly.Equals(CandidateName, ESearchCase::IgnoreCase))
				{
					return Pin;
				}
			}
		}
	}
	return nullptr;
}

UEdGraphNode* FBlueprintGraphNodeService::ResolveNodeReferenceInGraph(UEdGraph* Graph, const TMap<FString, UEdGraphNode*>& RequestNodeMap, const FString& NodeId)
{
	if (!Graph)
	{
		return nullptr;
	}

	if (NodeId.Equals(TEXT("__entry__"), ESearchCase::CaseSensitive))
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Cast<UK2Node_FunctionEntry>(Node) != nullptr)
			{
				return Node;
			}
		}
		return nullptr;
	}

	if (UEdGraphNode* const* ExistingNode = RequestNodeMap.Find(NodeId))
	{
		return *ExistingNode;
	}

	return FindNodeByUasId(Graph, NodeId);
}

void FBlueprintGraphNodeService::ApplyNodes(UBlueprint* Blueprint, UEdGraph* Graph, const TArray<FBlueprintGraphApplyNodeSpec>& NodeSpecs, bool bWillMutate, bool bCreateMissingNodes, FBlueprintGraphApplyResult& InOutResult, TMap<FString, UEdGraphNode*>& OutNodeById, TSet<FString>& OutCreatedNodeIds)
{
	if (!Blueprint || !Graph)
	{
		return;
	}

	OutNodeById.Reset();
	OutNodeById.Reserve(NodeSpecs.Num());
	OutCreatedNodeIds.Reset();

	for (const FBlueprintGraphApplyNodeSpec& NodeSpec : NodeSpecs)
	{
		UEdGraphNode* Node = FindNodeByUasId(Graph, NodeSpec.Id);
		const bool bExisting = Node != nullptr;
		if (bExisting && !NodeSpec.Type.TrimStartAndEnd().IsEmpty())
		{
			UClass* ExpectedClass = nullptr;
			if (TryResolveGraphNodeClass(NodeSpec.Type, ExpectedClass) && ExpectedClass && !Node->IsA(ExpectedClass))
			{
				InOutResult.Errors.Add(FString::Printf(TEXT("node_type_mismatch:%s"), *NodeSpec.Id));
				continue;
			}
		}

		if (!Node && bWillMutate)
		{
			if (NodeSpec.bUpdateOnly || !bCreateMissingNodes)
			{
				InOutResult.Errors.Add(FString::Printf(TEXT("node_not_found:%s"), *NodeSpec.Id));
				continue;
			}

			Node = CreateNodeFromSpec(Graph, Blueprint, NodeSpec, InOutResult);
			if (!Node)
			{
				InOutResult.Errors.Add(FString::Printf(TEXT("node_create_failed:%s"), *NodeSpec.Id));
				continue;
			}
			InOutResult.CreatedNodes.Add(NodeSpec.Id);
			OutCreatedNodeIds.Add(NodeSpec.Id);
		}
		else if (Node)
		{
			InOutResult.UpdatedNodes.Add(NodeSpec.Id);
		}

		if (!Node)
		{
			if (!bWillMutate)
			{
				if (!(NodeSpec.bUpdateOnly || !bCreateMissingNodes))
				{
					InOutResult.CreatedNodes.Add(NodeSpec.Id);
					OutCreatedNodeIds.Add(NodeSpec.Id);
				}
				else
				{
					InOutResult.Errors.Add(FString::Printf(TEXT("node_not_found:%s"), *NodeSpec.Id));
				}
				OutNodeById.Add(NodeSpec.Id, nullptr);
			}
			continue;
		}

		if (bWillMutate)
		{
			Node->Modify();
			FBlueprintGraphLayoutService::ApplyNodeLayout(Node, NodeSpec);
			SetNodeUasId(Node, NodeSpec.Id);

			if (NodeSpec.Type.Equals(TEXT("K2Node_AddComponent"), ESearchCase::CaseSensitive))
			{
				if (UK2Node_AddComponent* AddComponentNode = Cast<UK2Node_AddComponent>(Node))
				{
					UClass* ComponentClass = nullptr;
					if (TryResolveClassByPath(NodeSpec.ClassPath, ComponentClass))
					{
						AddComponentNode->TemplateType = ComponentClass;
						AddComponentNode->ReconstructNode();
						if (UEdGraphPin* ReturnPin = AddComponentNode->GetReturnValuePin())
						{
							ReturnPin->PinType.PinSubCategoryObject = ComponentClass;
						}
						AddComponentNode->MakeNewComponentTemplate();
					}
				}
			}

			if (NodeSpec.Type.Equals(TEXT("K2Node_VariableSet"), ESearchCase::CaseSensitive))
			{
				if (UK2Node_VariableSet* VariableSetNode = Cast<UK2Node_VariableSet>(Node))
				{
					const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
					if (Schema)
					{
						UStruct* VariableSource = Blueprint->SkeletonGeneratedClass ? static_cast<UStruct*>(Blueprint->SkeletonGeneratedClass) : static_cast<UStruct*>(Blueprint->ParentClass);
						Schema->ConfigureVarNode(VariableSetNode, FName(*NodeSpec.Variable), VariableSource, Blueprint);
						VariableSetNode->ReconstructNode();
					}
				}
			}

			if (NodeSpec.Type.Equals(TEXT("K2Node_ExecutionSequence"), ESearchCase::CaseSensitive)
				|| NodeSpec.Type.Equals(TEXT("K2Node_Sequence"), ESearchCase::CaseSensitive))
			{
				if (UK2Node_ExecutionSequence* SequenceNode = Cast<UK2Node_ExecutionSequence>(Node))
				{
					EnsureSequenceOutputPins(SequenceNode, NodeSpec.Outputs);
				}
			}

			ApplyNodeProperties(Node, NodeSpec.Properties, NodeSpec.ForwardAxis, InOutResult, NodeSpec.Id);
			if (NodeSpec.Properties.Num() > 0)
			{
				Node->ReconstructNode();
			}
		}

		for (const TPair<FString, FString>& PinDefault : NodeSpec.Pins)
		{
			if (UEdGraphPin* Pin = FindPinSmart(Node, PinDefault.Key))
			{
				if (bWillMutate)
				{
					SetPinDefault(Pin, PinDefault.Value, InOutResult, NodeSpec.Id, PinDefault.Key);
				}
			}
			else
			{
				InOutResult.Warnings.Add(FString::Printf(TEXT("pin_not_found:%s.%s"), *NodeSpec.Id, *PinDefault.Key));
			}
		}

		if (bExisting && NodeSpec.Type.Equals(TEXT("K2Node_PrintString"), ESearchCase::CaseSensitive))
		{
			if (UEdGraphPin* InStringPin = FindPinSmart(Node, TEXT("InString")))
			{
				if (bWillMutate)
				{
					SetPinDefault(InStringPin, NodeSpec.Message, InOutResult, NodeSpec.Id, TEXT("InString"));
				}
			}
		}

		CollectNodeValidationIssues(Node, NodeSpec.Id, InOutResult.NodeValidationIssues);

		OutNodeById.Add(NodeSpec.Id, Node);
	}
}
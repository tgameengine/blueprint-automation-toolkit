// Copyright 2026 AkaSoft. All Rights Reserved.

#include "BlueprintAutomationToolkitModule.h"

#include "Commands/BlueprintGraphEditCommand.h"
#include "Commands/CommandDispatcher.h"
#include "Routes/Blueprint/BlueprintGraphApplyRequest.h"
#include "Services/BlueprintGraphService.h"
#include "Services/BlueprintService.h"
#include "Async/Async.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Components/SplineComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraphSchema_K2.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/StaticMesh.h"
#include "FileHelpers.h"
#include "Http/HttpRequestUtils.h"
#include "HttpPath.h"
#include "HttpResultCallback.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Guid.h"
#include "IHttpRouter.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_IfThenElse.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet/KismetMathLibrary.h"
#include "Materials/MaterialInterface.h"
#include "ScopedTransaction.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace
{
	static constexpr const TCHAR* Route_BlueprintGraphApply = TEXT("/blueprint/graph/apply");
	static constexpr const TCHAR* Route_BlueprintGraphLinks = TEXT("/blueprint/graph/links");

	static UEdGraph* FindGraphByName(UBlueprint* Blueprint, const FString& GraphName);
	static FString GetStableNodeId(UEdGraphNode* Node);

	static TUniquePtr<FHttpServerResponse> MakeJsonResponse(EHttpServerResponseCodes Code, const FString& JsonString)
	{
		return BAT::Http::MakeJsonResponseFromString(static_cast<int32>(Code), JsonString);
	}

	static FString ToJsonString(const TSharedRef<FJsonObject>& Object)
	{
		FString Out;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Object, Writer);
		return Out;
	}

	static bool TryParseJsonBody(const TArray<uint8>& Body, TSharedPtr<FJsonObject>& OutObj)
	{
		FString BodyString = FString(Body.Num(), reinterpret_cast<const ANSICHAR*>(Body.GetData()));
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(BodyString);
		return FJsonSerializer::Deserialize(Reader, OutObj) && OutObj.IsValid();
	}

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
		return (OutBlueprint != nullptr);
	}

	static FString NormalizeAssetObjectPath(const FString& InPath)
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

	static FString MakeGeneratedClassObjectPath(const FString& InPath)
	{
		const FString AssetObjectPath = NormalizeAssetObjectPath(InPath);
		if (AssetObjectPath.IsEmpty())
		{
			return AssetObjectPath;
		}

		int32 DotIndex = INDEX_NONE;
		if (!AssetObjectPath.FindLastChar(TEXT('.'), DotIndex) || DotIndex + 1 >= AssetObjectPath.Len())
		{
			return AssetObjectPath;
		}

		const FString ObjectName = AssetObjectPath.Mid(DotIndex + 1);
		if (ObjectName.EndsWith(TEXT("_C"), ESearchCase::CaseSensitive))
		{
			return AssetObjectPath;
		}

		return FString::Printf(TEXT("%s.%s_C"), *AssetObjectPath.Left(DotIndex), *ObjectName);
	}

	static bool TryResolveObjectReference(const FString& InPath, UClass* RequiredClass, UObject*& OutObject)
	{
		OutObject = nullptr;

		auto TryCandidate = [&OutObject, RequiredClass](const FString& Candidate) -> bool
		{
			FString Path = Candidate;
			Path.TrimStartAndEndInline();
			if (Path.IsEmpty())
			{
				return false;
			}

			UObject* Resolved = FindObject<UObject>(nullptr, *Path);
			if (!Resolved)
			{
				Resolved = LoadObject<UObject>(nullptr, *Path);
			}
			if (!Resolved)
			{
				FSoftObjectPath SoftPath(Path);
				Resolved = SoftPath.ResolveObject();
				if (!Resolved)
				{
					Resolved = SoftPath.TryLoad();
				}
			}

			if (!Resolved)
			{
				return false;
			}
			if (RequiredClass && !Resolved->IsA(RequiredClass))
			{
				return false;
			}

			OutObject = Resolved;
			return true;
		};

		const FString TrimmedPath = InPath.TrimStartAndEnd();
		if (TryCandidate(TrimmedPath))
		{
			return true;
		}

		const FString AssetObjectPath = NormalizeAssetObjectPath(TrimmedPath);
		if (!AssetObjectPath.Equals(TrimmedPath, ESearchCase::CaseSensitive) && TryCandidate(AssetObjectPath))
		{
			return true;
		}

		return false;
	}

	static bool TryResolveClassReference(const FString& InPath, UClass*& OutClass)
	{
		OutClass = nullptr;

		auto TryDirectClass = [&OutClass](const FString& Candidate) -> bool
		{
			FString Path = Candidate;
			Path.TrimStartAndEndInline();
			if (Path.IsEmpty())
			{
				return false;
			}

			OutClass = FindObject<UClass>(nullptr, *Path);
			if (!OutClass)
			{
				OutClass = LoadObject<UClass>(nullptr, *Path);
			}
			if (!OutClass)
			{
				OutClass = LoadClass<UObject>(nullptr, *Path);
			}

			return OutClass != nullptr;
		};

		const FString TrimmedPath = InPath.TrimStartAndEnd();
		if (TryDirectClass(TrimmedPath))
		{
			return true;
		}

		const FString AssetObjectPath = NormalizeAssetObjectPath(TrimmedPath);
		if (!AssetObjectPath.Equals(TrimmedPath, ESearchCase::CaseSensitive) && TryDirectClass(AssetObjectPath))
		{
			return true;
		}

		const FString GeneratedClassPath = MakeGeneratedClassObjectPath(TrimmedPath);
		if (!GeneratedClassPath.Equals(TrimmedPath, ESearchCase::CaseSensitive) && TryDirectClass(GeneratedClassPath))
		{
			return true;
		}

		UBlueprint* Blueprint = FindObject<UBlueprint>(nullptr, *AssetObjectPath);
		if (!Blueprint)
		{
			Blueprint = LoadObject<UBlueprint>(nullptr, *AssetObjectPath);
		}
		if (Blueprint && Blueprint->GeneratedClass)
		{
			OutClass = Blueprint->GeneratedClass;
			return true;
		}

		return false;
	}

	static bool TryParseVector3(const TSharedPtr<FJsonValue>& Value, FVector& OutVec)
	{
		if (!Value.IsValid())
		{
			return false;
		}

		if (Value->Type == EJson::Array)
		{
			const TArray<TSharedPtr<FJsonValue>>& Arr = Value->AsArray();
			if (Arr.Num() == 3)
			{
				OutVec.X = (float)Arr[0]->AsNumber();
				OutVec.Y = (float)Arr[1]->AsNumber();
				OutVec.Z = (float)Arr[2]->AsNumber();
				return true;
			}
			return false;
		}

		if (Value->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject> Obj = Value->AsObject();
			if (!Obj.IsValid())
			{
				return false;
			}
			double X = 0.0, Y = 0.0, Z = 0.0;
			if (!(Obj->TryGetNumberField(TEXT("x"), X) || Obj->TryGetNumberField(TEXT("X"), X))) return false;
			if (!(Obj->TryGetNumberField(TEXT("y"), Y) || Obj->TryGetNumberField(TEXT("Y"), Y))) return false;
			if (!(Obj->TryGetNumberField(TEXT("z"), Z) || Obj->TryGetNumberField(TEXT("Z"), Z))) return false;
			OutVec = FVector((float)X, (float)Y, (float)Z);
			return true;
		}

		return false;
	}

	static bool TryParseVector2(const TSharedPtr<FJsonValue>& Value, FVector2D& OutVec)
	{
		if (!Value.IsValid())
		{
			return false;
		}

		if (Value->Type == EJson::Array)
		{
			const TArray<TSharedPtr<FJsonValue>>& Arr = Value->AsArray();
			if (Arr.Num() == 2 && Arr[0].IsValid() && Arr[1].IsValid()
				&& Arr[0]->Type == EJson::Number && Arr[1]->Type == EJson::Number)
			{
				OutVec.X = (float)Arr[0]->AsNumber();
				OutVec.Y = (float)Arr[1]->AsNumber();
				return true;
			}
			return false;
		}

		if (Value->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject> Obj = Value->AsObject();
			if (!Obj.IsValid())
			{
				return false;
			}

			double X = 0.0;
			double Y = 0.0;
			if (!(Obj->TryGetNumberField(TEXT("x"), X) || Obj->TryGetNumberField(TEXT("X"), X)))
			{
				return false;
			}
			if (!(Obj->TryGetNumberField(TEXT("y"), Y) || Obj->TryGetNumberField(TEXT("Y"), Y)))
			{
				return false;
			}

			OutVec = FVector2D((float)X, (float)Y);
			return true;
		}

		return false;
	}

	static bool TryParseRotator(const TSharedPtr<FJsonValue>& Value, FRotator& OutRot)
	{
		if (!Value.IsValid())
		{
			return false;
		}

		if (Value->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject> Obj = Value->AsObject();
			if (!Obj.IsValid())
			{
				return false;
			}
			double Pitch = 0.0, Yaw = 0.0, Roll = 0.0;
			if (!(Obj->TryGetNumberField(TEXT("pitch"), Pitch) || Obj->TryGetNumberField(TEXT("p"), Pitch) || Obj->TryGetNumberField(TEXT("Pitch"), Pitch))) return false;
			if (!(Obj->TryGetNumberField(TEXT("yaw"), Yaw) || Obj->TryGetNumberField(TEXT("y"), Yaw) || Obj->TryGetNumberField(TEXT("Yaw"), Yaw))) return false;
			if (!(Obj->TryGetNumberField(TEXT("roll"), Roll) || Obj->TryGetNumberField(TEXT("r"), Roll) || Obj->TryGetNumberField(TEXT("Roll"), Roll))) return false;
			OutRot = FRotator((float)Pitch, (float)Yaw, (float)Roll);
			return true;
		}

		return false;
	}

	static bool SetObjectPropertyFromJson(UObject* Obj, FProperty* Prop, const TSharedPtr<FJsonValue>& JsonValue, FString& OutError)
	{
		if (!Obj || !Prop || !JsonValue.IsValid())
		{
			OutError = TEXT("Invalid args");
			return false;
		}

		void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Obj);

		if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
		{
			if (JsonValue->Type != EJson::Boolean)
			{
				OutError = TEXT("Expected boolean");
				return false;
			}
			BoolProp->SetPropertyValue(ValuePtr, JsonValue->AsBool());
			return true;
		}

		if (FNumericProperty* NumProp = CastField<FNumericProperty>(Prop))
		{
			if (JsonValue->Type != EJson::Number)
			{
				OutError = TEXT("Expected number");
				return false;
			}
			const double Num = JsonValue->AsNumber();
			if (NumProp->IsInteger())
			{
				NumProp->SetIntPropertyValue(ValuePtr, (int64)Num);
			}
			else
			{
				NumProp->SetFloatingPointPropertyValue(ValuePtr, Num);
			}
			return true;
		}

		if (FStrProperty* StrProp = CastField<FStrProperty>(Prop))
		{
			if (JsonValue->Type != EJson::String)
			{
				OutError = TEXT("Expected string");
				return false;
			}
			StrProp->SetPropertyValue(ValuePtr, JsonValue->AsString());
			return true;
		}

		if (FNameProperty* NameProp = CastField<FNameProperty>(Prop))
		{
			if (JsonValue->Type != EJson::String)
			{
				OutError = TEXT("Expected string (name)");
				return false;
			}
			NameProp->SetPropertyValue(ValuePtr, FName(*JsonValue->AsString()));
			return true;
		}

		if (FClassProperty* ClassProp = CastField<FClassProperty>(Prop))
		{
			if (JsonValue->Type == EJson::Null)
			{
				ClassProp->SetPropertyValue(ValuePtr, nullptr);
				return true;
			}
			if (JsonValue->Type != EJson::String)
			{
				OutError = TEXT("Expected string (class reference)");
				return false;
			}

			UClass* ResolvedClass = nullptr;
			if (!TryResolveClassReference(JsonValue->AsString(), ResolvedClass))
			{
				OutError = TEXT("Class reference could not be resolved");
				return false;
			}
			if (ClassProp->MetaClass && !ResolvedClass->IsChildOf(ClassProp->MetaClass))
			{
				OutError = FString::Printf(TEXT("Expected subclass of %s"), *ClassProp->MetaClass->GetPathName());
				return false;
			}

			ClassProp->SetPropertyValue(ValuePtr, ResolvedClass);
			return true;
		}

		if (FSoftClassProperty* SoftClassProp = CastField<FSoftClassProperty>(Prop))
		{
			if (JsonValue->Type == EJson::Null)
			{
				*static_cast<FSoftObjectPtr*>(ValuePtr) = FSoftObjectPtr();
				return true;
			}
			if (JsonValue->Type != EJson::String)
			{
				OutError = TEXT("Expected string (soft class reference)");
				return false;
			}

			UClass* ResolvedClass = nullptr;
			if (!TryResolveClassReference(JsonValue->AsString(), ResolvedClass))
			{
				OutError = TEXT("Soft class reference could not be resolved");
				return false;
			}
			if (SoftClassProp->MetaClass && !ResolvedClass->IsChildOf(SoftClassProp->MetaClass))
			{
				OutError = FString::Printf(TEXT("Expected subclass of %s"), *SoftClassProp->MetaClass->GetPathName());
				return false;
			}

			*static_cast<FSoftObjectPtr*>(ValuePtr) = FSoftObjectPtr(ResolvedClass);
			return true;
		}

		if (FObjectPropertyBase* ObjectProp = CastField<FObjectPropertyBase>(Prop))
		{
			if (JsonValue->Type == EJson::Null)
			{
				ObjectProp->SetObjectPropertyValue(ValuePtr, nullptr);
				return true;
			}
			if (JsonValue->Type != EJson::String)
			{
				OutError = TEXT("Expected string (object reference)");
				return false;
			}

			UObject* ResolvedObject = nullptr;
			if (!TryResolveObjectReference(JsonValue->AsString(), ObjectProp->PropertyClass, ResolvedObject))
			{
				OutError = TEXT("Object reference could not be resolved");
				return false;
			}

			ObjectProp->SetObjectPropertyValue(ValuePtr, ResolvedObject);
			return true;
		}

		if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
		{
			if (StructProp->Struct == TBaseStructure<FVector>::Get())
			{
				FVector V;
				if (!TryParseVector3(JsonValue, V))
				{
					OutError = TEXT("Expected vector {x,y,z} or [x,y,z]");
					return false;
				}
				*static_cast<FVector*>(ValuePtr) = V;
				return true;
			}
			if (StructProp->Struct == TBaseStructure<FRotator>::Get())
			{
				FRotator R;
				if (!TryParseRotator(JsonValue, R))
				{
					OutError = TEXT("Expected rotator {pitch,yaw,roll}");
					return false;
				}
				*static_cast<FRotator*>(ValuePtr) = R;
				return true;
			}
		}

		OutError = FString::Printf(TEXT("Unsupported property type: %s"), *Prop->GetClass()->GetName());
		return false;
	}

	static bool SaveBlueprintPackage(UBlueprint* Blueprint, FString& OutError)
	{
		if (!Blueprint)
		{
			OutError = TEXT("Blueprint is null");
			return false;
		}
		UPackage* Package = Blueprint->GetOutermost();
		if (!Package)
		{
			OutError = TEXT("Blueprint package is null");
			return false;
		}

		TArray<UPackage*> Packages;
		Packages.Add(Package);
		const bool bOk = UEditorLoadingAndSavingUtils::SavePackages(Packages, /*bOnlyDirty*/ false);
		if (!bOk)
		{
			OutError = TEXT("SavePackages returned false");
			return false;
		}

		return true;
	}

	static bool TryParseGuidString(const FString& InGuid, FGuid& OutGuid)
	{
		return FGuid::Parse(InGuid, OutGuid);
	}

	enum class EBATComponentApplyKind : uint8
	{
		Unsupported,
		ISM,
		HISM,
		Spline,
		SplineMesh,
	};

	enum class EBATBlueprintComponentKind : uint8
	{
		Unsupported,
		StaticMesh,
		ISM,
		HISM,
		SplineMesh,
	};

	static EBATBlueprintComponentKind ResolveBlueprintComponentKind(const FString& ClassPath)
	{
		FString S = ClassPath;
		S.TrimStartAndEndInline();
		if (S.Equals(TEXT("/Script/Engine.StaticMeshComponent"), ESearchCase::IgnoreCase)
			|| S.Equals(TEXT("UStaticMeshComponent"), ESearchCase::IgnoreCase)
			|| S.Equals(TEXT("StaticMeshComponent"), ESearchCase::IgnoreCase))
		{
			return EBATBlueprintComponentKind::StaticMesh;
		}
		if (S.Equals(TEXT("/Script/Engine.InstancedStaticMeshComponent"), ESearchCase::IgnoreCase)
			|| S.Equals(TEXT("UInstancedStaticMeshComponent"), ESearchCase::IgnoreCase)
			|| S.Equals(TEXT("InstancedStaticMeshComponent"), ESearchCase::IgnoreCase))
		{
			return EBATBlueprintComponentKind::ISM;
		}
		if (S.Equals(TEXT("/Script/Engine.HierarchicalInstancedStaticMeshComponent"), ESearchCase::IgnoreCase)
			|| S.Equals(TEXT("UHierarchicalInstancedStaticMeshComponent"), ESearchCase::IgnoreCase)
			|| S.Equals(TEXT("HierarchicalInstancedStaticMeshComponent"), ESearchCase::IgnoreCase))
		{
			return EBATBlueprintComponentKind::HISM;
		}
		if (S.Equals(TEXT("/Script/Engine.SplineMeshComponent"), ESearchCase::IgnoreCase)
			|| S.Equals(TEXT("USplineMeshComponent"), ESearchCase::IgnoreCase)
			|| S.Equals(TEXT("SplineMeshComponent"), ESearchCase::IgnoreCase))
		{
			return EBATBlueprintComponentKind::SplineMesh;
		}
		return EBATBlueprintComponentKind::Unsupported;
	}

	static UClass* ResolveBlueprintComponentClass(EBATBlueprintComponentKind Kind)
	{
		switch (Kind)
		{
		case EBATBlueprintComponentKind::StaticMesh:
			return UStaticMeshComponent::StaticClass();
		case EBATBlueprintComponentKind::ISM:
			return UInstancedStaticMeshComponent::StaticClass();
		case EBATBlueprintComponentKind::HISM:
			return UHierarchicalInstancedStaticMeshComponent::StaticClass();
		case EBATBlueprintComponentKind::SplineMesh:
			return USplineMeshComponent::StaticClass();
		default:
			break;
		}
		return nullptr;
	}

	static bool TryResolveClassPath(const FString& InClassPath, UClass*& OutClass)
	{
		OutClass = nullptr;
		FString ClassPath = InClassPath;
		ClassPath.TrimStartAndEndInline();
		if (ClassPath.IsEmpty())
		{
			return false;
		}

		OutClass = LoadObject<UClass>(nullptr, *ClassPath);
		if (!OutClass)
		{
			OutClass = FindObject<UClass>(nullptr, *ClassPath);
		}
		if (!OutClass)
		{
			OutClass = LoadClass<UObject>(nullptr, *ClassPath);
		}

		return OutClass != nullptr;
	}

	static bool TryBuildBlueprintVariablePinType(const FString& InType, FEdGraphPinType& OutPinType, FString& OutError)
	{
		OutPinType = FEdGraphPinType();
		OutError.Reset();

		FString TypeSpec = InType;
		TypeSpec.TrimStartAndEndInline();
		if (TypeSpec.IsEmpty())
		{
			OutError = TEXT("variable_type_required");
			return false;
		}

		EPinContainerType ContainerType = EPinContainerType::None;
		if (TypeSpec.EndsWith(TEXT("[]"), ESearchCase::CaseSensitive))
		{
			ContainerType = EPinContainerType::Array;
			TypeSpec.LeftChopInline(2, EAllowShrinking::No);
			TypeSpec.TrimStartAndEndInline();
		}

		const FString LowerType = TypeSpec.ToLower();
		OutPinType.ContainerType = ContainerType;

		if (LowerType == TEXT("bool") || LowerType == TEXT("boolean"))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
			return true;
		}
		if (LowerType == TEXT("int") || LowerType == TEXT("int32"))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Int;
			return true;
		}
		if (LowerType == TEXT("int64"))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Int64;
			return true;
		}
		if (LowerType == TEXT("float"))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Real;
			OutPinType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
			return true;
		}
		if (LowerType == TEXT("double") || LowerType == TEXT("real"))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Real;
			OutPinType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
			return true;
		}
		if (LowerType == TEXT("string"))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_String;
			return true;
		}
		if (LowerType == TEXT("name"))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Name;
			return true;
		}
		if (LowerType == TEXT("text"))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Text;
			return true;
		}
		if (LowerType == TEXT("vector"))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			OutPinType.PinSubCategory = TEXT("Vector");
			OutPinType.PinSubCategoryObject = TBaseStructure<FVector>::Get();
			return true;
		}
		if (LowerType == TEXT("rotator"))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			OutPinType.PinSubCategory = TEXT("Rotator");
			OutPinType.PinSubCategoryObject = TBaseStructure<FRotator>::Get();
			return true;
		}
		if (LowerType == TEXT("transform"))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			OutPinType.PinSubCategory = TEXT("Transform");
			OutPinType.PinSubCategoryObject = TBaseStructure<FTransform>::Get();
			return true;
		}

		if (LowerType.StartsWith(TEXT("object:")))
		{
			UClass* ObjectClass = nullptr;
			if (!TryResolveClassPath(TypeSpec.Mid(7), ObjectClass))
			{
				OutError = TEXT("variable_object_class_not_found");
				return false;
			}

			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Object;
			OutPinType.PinSubCategoryObject = ObjectClass;
			return true;
		}

		if (LowerType.StartsWith(TEXT("class:")))
		{
			UClass* MetaClass = nullptr;
			if (!TryResolveClassPath(TypeSpec.Mid(6), MetaClass))
			{
				OutError = TEXT("variable_meta_class_not_found");
				return false;
			}

			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Class;
			OutPinType.PinSubCategoryObject = MetaClass;
			return true;
		}

		OutError = TEXT("variable_type_not_supported");
		return false;
	}

	static bool TryConvertVariableDefaultValue(const FString& InType, const TSharedPtr<FJsonValue>* DefaultValuePtr, FString& OutDefaultValue, FString& OutError)
	{
		OutDefaultValue.Reset();
		OutError.Reset();
		if (DefaultValuePtr == nullptr || !DefaultValuePtr->IsValid())
		{
			return true;
		}

		FString TypeSpec = InType;
		TypeSpec.TrimStartAndEndInline();
		const bool bIsArray = TypeSpec.EndsWith(TEXT("[]"), ESearchCase::CaseSensitive);
		if (bIsArray)
		{
			OutError = TEXT("variable_default_not_supported_for_container_types");
			return false;
		}

		const FString LowerType = TypeSpec.ToLower();
		const TSharedPtr<FJsonValue>& DefaultValue = *DefaultValuePtr;

		if (LowerType == TEXT("bool") || LowerType == TEXT("boolean"))
		{
			if (DefaultValue->Type != EJson::Boolean)
			{
				OutError = TEXT("variable_default_type_mismatch");
				return false;
			}
			OutDefaultValue = DefaultValue->AsBool() ? TEXT("true") : TEXT("false");
			return true;
		}

		if (LowerType == TEXT("int") || LowerType == TEXT("int32") || LowerType == TEXT("int64"))
		{
			if (DefaultValue->Type != EJson::Number)
			{
				OutError = TEXT("variable_default_type_mismatch");
				return false;
			}
			OutDefaultValue = FString::Printf(TEXT("%lld"), (long long)DefaultValue->AsNumber());
			return true;
		}

		if (LowerType == TEXT("float") || LowerType == TEXT("double") || LowerType == TEXT("real"))
		{
			if (DefaultValue->Type != EJson::Number)
			{
				OutError = TEXT("variable_default_type_mismatch");
				return false;
			}
			OutDefaultValue = FString::SanitizeFloat(DefaultValue->AsNumber());
			return true;
		}

		if (LowerType == TEXT("string") || LowerType == TEXT("name") || LowerType == TEXT("text"))
		{
			if (DefaultValue->Type != EJson::String)
			{
				OutError = TEXT("variable_default_type_mismatch");
				return false;
			}
			OutDefaultValue = DefaultValue->AsString();
			return true;
		}

		OutError = TEXT("variable_default_not_supported_for_type");
		return false;
	}

	static USCS_Node* FindSCSNodeByName(UBlueprint* Blueprint, const FString& ComponentName)
	{
		if (!Blueprint || !Blueprint->SimpleConstructionScript)
		{
			return nullptr;
		}
		for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			if (!Node)
			{
				continue;
			}
			if (Node->GetVariableName().ToString().Equals(ComponentName, ESearchCase::IgnoreCase))
			{
				return Node;
			}
		}
		return nullptr;
	}

	static USceneComponent* FindBlueprintComponentTemplateByName(UBlueprint* Blueprint, const FString& ComponentName)
	{
		if (!Blueprint)
		{
			return nullptr;
		}

		if (USCS_Node* Node = FindSCSNodeByName(Blueprint, ComponentName))
		{
			return Cast<USceneComponent>(Node->ComponentTemplate);
		}

		UClass* SearchClass = Blueprint->GeneratedClass;
		if (!SearchClass)
		{
			SearchClass = Blueprint->SkeletonGeneratedClass;
		}
		if (!SearchClass)
		{
			SearchClass = Blueprint->ParentClass;
		}

		AActor* DefaultActor = SearchClass ? Cast<AActor>(SearchClass->GetDefaultObject()) : nullptr;
		if (!DefaultActor)
		{
			return nullptr;
		}

		TInlineComponentArray<USceneComponent*> SceneComponents(DefaultActor);
		for (USceneComponent* Component : SceneComponents)
		{
			if (!Component)
			{
				continue;
			}

			if (Component->GetName().Equals(ComponentName, ESearchCase::IgnoreCase))
			{
				return Component;
			}
		}

		return nullptr;
	}

	static bool TryParseMobilityString(const FString& In, EComponentMobility::Type& OutMobility)
	{
		if (In.Equals(TEXT("Static"), ESearchCase::IgnoreCase))
		{
			OutMobility = EComponentMobility::Static;
			return true;
		}
		if (In.Equals(TEXT("Stationary"), ESearchCase::IgnoreCase))
		{
			OutMobility = EComponentMobility::Stationary;
			return true;
		}
		if (In.Equals(TEXT("Movable"), ESearchCase::IgnoreCase))
		{
			OutMobility = EComponentMobility::Movable;
			return true;
		}
		return false;
	}

	static void ApplyAllowedSceneTransformProperties(USceneComponent* SceneComp, const TSharedPtr<FJsonObject>& PropertiesObj, int32& InOutApplied, int32& InOutRejected)
	{
		if (!SceneComp || !PropertiesObj.IsValid())
		{
			return;
		}

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : PropertiesObj->Values)
		{
			const FString& Name = Pair.Key;
			if (Name.Equals(TEXT("relative_location"), ESearchCase::IgnoreCase))
			{
				FVector V;
				if (TryParseVector3(Pair.Value, V))
				{
					SceneComp->SetRelativeLocation(V);
					++InOutApplied;
				}
				else
				{
					++InOutRejected;
				}
				continue;
			}

			if (Name.Equals(TEXT("relative_rotation"), ESearchCase::IgnoreCase))
			{
				FVector V;
				if (TryParseVector3(Pair.Value, V))
				{
					SceneComp->SetRelativeRotation(FRotator(V.X, V.Y, V.Z));
					++InOutApplied;
				}
				else
				{
					++InOutRejected;
				}
				continue;
			}

			if (Name.Equals(TEXT("relative_scale3d"), ESearchCase::IgnoreCase))
			{
				FVector V;
				if (TryParseVector3(Pair.Value, V))
				{
					SceneComp->SetRelativeScale3D(V);
					++InOutApplied;
				}
				else
				{
					++InOutRejected;
				}
				continue;
			}

			if (Name.Equals(TEXT("mobility"), ESearchCase::IgnoreCase))
			{
				if (Pair.Value.IsValid() && Pair.Value->Type == EJson::String)
				{
					EComponentMobility::Type Mobility;
					if (TryParseMobilityString(Pair.Value->AsString(), Mobility))
					{
						SceneComp->SetMobility(Mobility);
						++InOutApplied;
					}
					else
					{
						++InOutRejected;
					}
				}
				else
				{
					++InOutRejected;
				}
				continue;
			}

			++InOutRejected;
		}
	}

	static void ApplyAllowedMeshAssets(UStaticMeshComponent* MeshComp, const TSharedPtr<FJsonObject>& EntryObj, int32& InOutApplied, int32& InOutRejected, bool& bOutOk)
	{
		bOutOk = true;
		if (!MeshComp || !EntryObj.IsValid())
		{
			return;
		}

		FString StaticMeshPath;
		if (!EntryObj->TryGetStringField(TEXT("static_mesh"), StaticMeshPath))
		{
			const TSharedPtr<FJsonObject>* AssetsObjPtr = nullptr;
			if (EntryObj->TryGetObjectField(TEXT("assets"), AssetsObjPtr) && AssetsObjPtr && AssetsObjPtr->IsValid())
			{
				(*AssetsObjPtr)->TryGetStringField(TEXT("static_mesh"), StaticMeshPath);
			}
		}
		if (!StaticMeshPath.IsEmpty())
		{
			if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *StaticMeshPath))
			{
				MeshComp->SetStaticMesh(Mesh);
				++InOutApplied;
			}
			else
			{
				++InOutRejected;
				bOutOk = false;
			}
		}

		FString MaterialPath;
		if (!EntryObj->TryGetStringField(TEXT("material0"), MaterialPath))
		{
			const TSharedPtr<FJsonObject>* AssetsObjPtr = nullptr;
			if (EntryObj->TryGetObjectField(TEXT("assets"), AssetsObjPtr) && AssetsObjPtr && AssetsObjPtr->IsValid())
			{
				(*AssetsObjPtr)->TryGetStringField(TEXT("material0"), MaterialPath);
			}
		}
		if (!MaterialPath.IsEmpty())
		{
			if (UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, *MaterialPath))
			{
				MeshComp->SetMaterial(0, Mat);
				++InOutApplied;
			}
			else
			{
				++InOutRejected;
				bOutOk = false;
			}
		}
	}

	static EBATComponentApplyKind ResolveComponentApplyKind(const FString& ClassPath)
	{
		FString S = ClassPath;
		S.TrimStartAndEndInline();
		if (S.Equals(TEXT("/Script/Engine.InstancedStaticMeshComponent"), ESearchCase::IgnoreCase)
			|| S.Equals(TEXT("UInstancedStaticMeshComponent"), ESearchCase::IgnoreCase)
			|| S.Equals(TEXT("InstancedStaticMeshComponent"), ESearchCase::IgnoreCase))
		{
			return EBATComponentApplyKind::ISM;
		}
		if (S.Equals(TEXT("/Script/Engine.HierarchicalInstancedStaticMeshComponent"), ESearchCase::IgnoreCase)
			|| S.Equals(TEXT("UHierarchicalInstancedStaticMeshComponent"), ESearchCase::IgnoreCase)
			|| S.Equals(TEXT("HierarchicalInstancedStaticMeshComponent"), ESearchCase::IgnoreCase))
		{
			return EBATComponentApplyKind::HISM;
		}
		if (S.Equals(TEXT("/Script/Engine.SplineComponent"), ESearchCase::IgnoreCase)
			|| S.Equals(TEXT("USplineComponent"), ESearchCase::IgnoreCase)
			|| S.Equals(TEXT("SplineComponent"), ESearchCase::IgnoreCase))
		{
			return EBATComponentApplyKind::Spline;
		}
		if (S.Equals(TEXT("/Script/Engine.SplineMeshComponent"), ESearchCase::IgnoreCase)
			|| S.Equals(TEXT("USplineMeshComponent"), ESearchCase::IgnoreCase)
			|| S.Equals(TEXT("SplineMeshComponent"), ESearchCase::IgnoreCase))
		{
			return EBATComponentApplyKind::SplineMesh;
		}
		return EBATComponentApplyKind::Unsupported;
	}

	static ESplinePointType::Type ResolveSplinePointType(const FString& PointTypeString)
	{
		if (PointTypeString.Equals(TEXT("Linear"), ESearchCase::IgnoreCase))
		{
			return ESplinePointType::Linear;
		}
		if (PointTypeString.Equals(TEXT("Constant"), ESearchCase::IgnoreCase))
		{
			return ESplinePointType::Constant;
		}
		if (PointTypeString.Equals(TEXT("CurveClamped"), ESearchCase::IgnoreCase))
		{
			return ESplinePointType::CurveClamped;
		}
		if (PointTypeString.Equals(TEXT("CurveCustomTangent"), ESearchCase::IgnoreCase))
		{
			return ESplinePointType::CurveCustomTangent;
		}

		return ESplinePointType::Curve;
	}

	static USceneComponent* FindSceneTemplateByName(UBlueprint* Blueprint, const FString& ComponentName, UClass* RequiredClass)
	{
		if (!Blueprint || !Blueprint->SimpleConstructionScript)
		{
			return nullptr;
		}

		for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			if (!Node || !Node->ComponentTemplate)
			{
				continue;
			}
			if (!Node->GetVariableName().ToString().Equals(ComponentName, ESearchCase::IgnoreCase))
			{
				continue;
			}
			if (RequiredClass && !Node->ComponentTemplate->IsA(RequiredClass))
			{
				continue;
			}

			return Cast<USceneComponent>(Node->ComponentTemplate);
		}

		return nullptr;
	}

	static USceneComponent* FindOrCreateSceneTemplate(UBlueprint* Blueprint, const FString& ComponentName, UClass* CompClass)
	{
		if (!Blueprint || !Blueprint->SimpleConstructionScript || !CompClass)
		{
			return nullptr;
		}

		if (USceneComponent* Existing = FindSceneTemplateByName(Blueprint, ComponentName, CompClass))
		{
			return Existing;
		}

		USCS_Node* NewNode = Blueprint->SimpleConstructionScript->CreateNode(CompClass, FName(*ComponentName));
		if (!NewNode)
		{
			return nullptr;
		}

		Blueprint->SimpleConstructionScript->AddNode(NewNode);
		return Cast<USceneComponent>(NewNode->ComponentTemplate);
	}

	static bool TryParseTransformFromJsonValue(const TSharedPtr<FJsonValue>& Value, FTransform& OutTransform)
	{
		if (!Value.IsValid() || Value->Type != EJson::Object)
		{
			return false;
		}

		const TSharedPtr<FJsonObject> Obj = Value->AsObject();
		if (!Obj.IsValid())
		{
			return false;
		}

		FVector Loc = FVector::ZeroVector;
		FVector RotVec = FVector::ZeroVector;
		FVector Scale = FVector(1.0f, 1.0f, 1.0f);

		if (const TSharedPtr<FJsonValue>* V = Obj->Values.Find(TEXT("location")))
		{
			TryParseVector3(*V, Loc);
		}
		if (const TSharedPtr<FJsonValue>* V = Obj->Values.Find(TEXT("rotation")))
		{
			TryParseVector3(*V, RotVec);
		}
		if (const TSharedPtr<FJsonValue>* V = Obj->Values.Find(TEXT("scale")))
		{
			TryParseVector3(*V, Scale);
		}

		OutTransform = FTransform(FRotator(RotVec.X, RotVec.Y, RotVec.Z), Loc, Scale);
		return true;
	}

	static UInstancedStaticMeshComponent* FindOrCreateInstancedTemplate(UBlueprint* Blueprint, const FString& ComponentName, EBATComponentApplyKind Kind)
	{
		UClass* CompClass = (Kind == EBATComponentApplyKind::HISM)
			? UHierarchicalInstancedStaticMeshComponent::StaticClass()
			: UInstancedStaticMeshComponent::StaticClass();

		return Cast<UInstancedStaticMeshComponent>(FindOrCreateSceneTemplate(Blueprint, ComponentName, CompClass));
	}

	static USplineComponent* FindOrCreateSplineTemplate(UBlueprint* Blueprint, const FString& ComponentName)
	{
		return Cast<USplineComponent>(FindOrCreateSceneTemplate(Blueprint, ComponentName, USplineComponent::StaticClass()));
	}

	struct FBATSplineMeshPatch
	{
		bool bSetCurve = false;
		FVector StartPosition = FVector::ZeroVector;
		FVector StartTangent = FVector::ZeroVector;
		FVector EndPosition = FVector::ZeroVector;
		FVector EndTangent = FVector::ZeroVector;
		TOptional<FVector2D> StartScale;
		TOptional<FVector2D> EndScale;
		TOptional<float> StartRollRadians;
		TOptional<float> EndRollRadians;
		TOptional<FVector2D> StartOffset;
		TOptional<FVector2D> EndOffset;
		TOptional<ESplineMeshAxis::Type> ForwardAxis;
		TOptional<FVector> SplineUpDirection;
		TOptional<bool> bSmoothInterpRollScale;
		TOptional<bool> bAllowSplineEditingPerInstance;
		TOptional<ECollisionEnabled::Type> CollisionEnabled;
		TOptional<bool> bGenerateOverlapEvents;
		TOptional<bool> bCastShadow;
	};

	static bool TryParseSplineMeshAxis(const FString& AxisString, ESplineMeshAxis::Type& OutAxis)
	{
		FString Axis = AxisString;
		Axis.TrimStartAndEndInline();
		if (Axis.Equals(TEXT("X"), ESearchCase::IgnoreCase)
			|| Axis.Equals(TEXT("ESplineMeshAxis::X"), ESearchCase::IgnoreCase))
		{
			OutAxis = ESplineMeshAxis::X;
			return true;
		}
		if (Axis.Equals(TEXT("Y"), ESearchCase::IgnoreCase)
			|| Axis.Equals(TEXT("ESplineMeshAxis::Y"), ESearchCase::IgnoreCase))
		{
			OutAxis = ESplineMeshAxis::Y;
			return true;
		}
		if (Axis.Equals(TEXT("Z"), ESearchCase::IgnoreCase)
			|| Axis.Equals(TEXT("ESplineMeshAxis::Z"), ESearchCase::IgnoreCase))
		{
			OutAxis = ESplineMeshAxis::Z;
			return true;
		}
		return false;
	}

	static bool TryParsePortableCollisionEnabled(const FString& CollisionString, ECollisionEnabled::Type& OutCollision)
	{
		FString Value = CollisionString;
		Value.TrimStartAndEndInline();
		if (Value.Equals(TEXT("NoCollision"), ESearchCase::IgnoreCase))
		{
			OutCollision = ECollisionEnabled::NoCollision;
			return true;
		}
		if (Value.Equals(TEXT("QueryOnly"), ESearchCase::IgnoreCase))
		{
			OutCollision = ECollisionEnabled::QueryOnly;
			return true;
		}
		if (Value.Equals(TEXT("PhysicsOnly"), ESearchCase::IgnoreCase))
		{
			OutCollision = ECollisionEnabled::PhysicsOnly;
			return true;
		}
		if (Value.Equals(TEXT("QueryAndPhysics"), ESearchCase::IgnoreCase))
		{
			OutCollision = ECollisionEnabled::QueryAndPhysics;
			return true;
		}
		return false;
	}

	static bool HasSplineMeshCurveField(const TSharedPtr<FJsonObject>& EntryObj)
	{
		return EntryObj.IsValid()
			&& (EntryObj->HasField(TEXT("start_position"))
				|| EntryObj->HasField(TEXT("start_tangent"))
				|| EntryObj->HasField(TEXT("end_position"))
				|| EntryObj->HasField(TEXT("end_tangent")));
	}

	static bool IsSplineMeshPatchField(const FString& Key)
	{
		return Key.Equals(TEXT("start_position"), ESearchCase::IgnoreCase)
			|| Key.Equals(TEXT("start_tangent"), ESearchCase::IgnoreCase)
			|| Key.Equals(TEXT("end_position"), ESearchCase::IgnoreCase)
			|| Key.Equals(TEXT("end_tangent"), ESearchCase::IgnoreCase)
			|| Key.Equals(TEXT("from_spline"), ESearchCase::IgnoreCase)
			|| Key.Equals(TEXT("start_scale"), ESearchCase::IgnoreCase)
			|| Key.Equals(TEXT("end_scale"), ESearchCase::IgnoreCase)
			|| Key.Equals(TEXT("start_roll"), ESearchCase::IgnoreCase)
			|| Key.Equals(TEXT("start_roll_degrees"), ESearchCase::IgnoreCase)
			|| Key.Equals(TEXT("end_roll"), ESearchCase::IgnoreCase)
			|| Key.Equals(TEXT("end_roll_degrees"), ESearchCase::IgnoreCase)
			|| Key.Equals(TEXT("start_offset"), ESearchCase::IgnoreCase)
			|| Key.Equals(TEXT("end_offset"), ESearchCase::IgnoreCase)
			|| Key.Equals(TEXT("forward_axis"), ESearchCase::IgnoreCase)
			|| Key.Equals(TEXT("spline_up_dir"), ESearchCase::IgnoreCase)
			|| Key.Equals(TEXT("smooth_interp_roll_scale"), ESearchCase::IgnoreCase)
			|| Key.Equals(TEXT("allow_spline_editing_per_instance"), ESearchCase::IgnoreCase)
			|| Key.Equals(TEXT("collision_enabled"), ESearchCase::IgnoreCase)
			|| Key.Equals(TEXT("generate_overlap_events"), ESearchCase::IgnoreCase)
			|| Key.Equals(TEXT("cast_shadow"), ESearchCase::IgnoreCase);
	}

	static bool TryBuildSplineMeshPatch(
		UBlueprint* Blueprint,
		const USplineMeshComponent* Component,
		const TSharedPtr<FJsonObject>& EntryObj,
		bool bRequireCurve,
		FBATSplineMeshPatch& OutPatch,
		FString& OutError)
	{
		OutPatch = FBATSplineMeshPatch();
		OutError.Reset();
		if (!EntryObj.IsValid())
		{
			OutError = TEXT("spline mesh entry must be an object");
			return false;
		}

		const USplineMeshComponent* Defaults = Component ? Component : GetDefault<USplineMeshComponent>();
		OutPatch.StartPosition = Defaults->GetStartPosition();
		OutPatch.StartTangent = Defaults->GetStartTangent();
		OutPatch.EndPosition = Defaults->GetEndPosition();
		OutPatch.EndTangent = Defaults->GetEndTangent();

		const bool bHasDirectCurve = HasSplineMeshCurveField(EntryObj);
		const bool bHasFromSplineField = EntryObj->HasField(TEXT("from_spline"));
		const TSharedPtr<FJsonObject>* FromSplineObjPtr = nullptr;
		const bool bHasFromSpline = EntryObj->TryGetObjectField(TEXT("from_spline"), FromSplineObjPtr)
			&& FromSplineObjPtr && FromSplineObjPtr->IsValid();
		if (bHasFromSplineField && !bHasFromSpline)
		{
			OutError = TEXT("from_spline must be an object");
			return false;
		}
		if (bHasFromSpline && bHasDirectCurve)
		{
			OutError = TEXT("from_spline cannot be combined with direct spline curve fields");
			return false;
		}

		if (bHasFromSpline)
		{
			if (!Blueprint)
			{
				OutError = TEXT("from_spline requires a Blueprint context");
				return false;
			}

			const TSharedPtr<FJsonObject> FromSplineObj = *FromSplineObjPtr;
			FString SplineComponentName;
			if (!FromSplineObj->TryGetStringField(TEXT("component"), SplineComponentName)
				|| SplineComponentName.TrimStartAndEnd().IsEmpty())
			{
				OutError = TEXT("from_spline requires 'component' name");
				return false;
			}

			USplineComponent* SourceSpline = Cast<USplineComponent>(
				FindSceneTemplateByName(Blueprint, SplineComponentName, USplineComponent::StaticClass()));
			if (!SourceSpline)
			{
				OutError = TEXT("from_spline component not found or not a SplineComponent");
				return false;
			}

			const float SplineLength = SourceSpline->GetSplineLength();
			double StartDistance = 0.0;
			double EndDistance = (double)SplineLength;
			double TangentScale = 1.0;
			if (FromSplineObj->HasField(TEXT("start_distance"))
				&& !FromSplineObj->TryGetNumberField(TEXT("start_distance"), StartDistance))
			{
				OutError = TEXT("from_spline.start_distance must be a number");
				return false;
			}
			if (FromSplineObj->HasField(TEXT("end_distance"))
				&& !FromSplineObj->TryGetNumberField(TEXT("end_distance"), EndDistance))
			{
				OutError = TEXT("from_spline.end_distance must be a number");
				return false;
			}
			if (FromSplineObj->HasField(TEXT("tangent_scale"))
				&& !FromSplineObj->TryGetNumberField(TEXT("tangent_scale"), TangentScale))
			{
				OutError = TEXT("from_spline.tangent_scale must be a number");
				return false;
			}
			if (TangentScale < 0.0)
			{
				OutError = TEXT("from_spline.tangent_scale must be >= 0");
				return false;
			}

			const float Start = FMath::Clamp((float)StartDistance, 0.0f, SplineLength);
			const float End = FMath::Clamp((float)EndDistance, 0.0f, SplineLength);
			if (End <= Start + KINDA_SMALL_NUMBER)
			{
				OutError = TEXT("from_spline.end_distance must be greater than start_distance");
				return false;
			}

			OutPatch.StartPosition = SourceSpline->GetLocationAtDistanceAlongSpline(Start, ESplineCoordinateSpace::Local);
			OutPatch.StartTangent = SourceSpline->GetTangentAtDistanceAlongSpline(Start, ESplineCoordinateSpace::Local) * (float)TangentScale;
			OutPatch.EndPosition = SourceSpline->GetLocationAtDistanceAlongSpline(End, ESplineCoordinateSpace::Local);
			OutPatch.EndTangent = SourceSpline->GetTangentAtDistanceAlongSpline(End, ESplineCoordinateSpace::Local) * (float)TangentScale;
			OutPatch.bSetCurve = true;
		}
		else if (bHasDirectCurve)
		{
			bool bHasStartPosition = false;
			bool bHasStartTangent = false;
			bool bHasEndPosition = false;
			bool bHasEndTangent = false;

			auto ParseCurveVector = [&EntryObj, &OutError](const TCHAR* Field, FVector& OutValue, bool& bOutPresent) -> bool
			{
				const TSharedPtr<FJsonValue>* Value = EntryObj->Values.Find(Field);
				if (!Value)
				{
					return true;
				}
				bOutPresent = true;
				if (!TryParseVector3(*Value, OutValue))
				{
					OutError = FString::Printf(TEXT("%s must be a three-component vector"), Field);
					return false;
				}
				return true;
			};

			if (!ParseCurveVector(TEXT("start_position"), OutPatch.StartPosition, bHasStartPosition)
				|| !ParseCurveVector(TEXT("start_tangent"), OutPatch.StartTangent, bHasStartTangent)
				|| !ParseCurveVector(TEXT("end_position"), OutPatch.EndPosition, bHasEndPosition)
				|| !ParseCurveVector(TEXT("end_tangent"), OutPatch.EndTangent, bHasEndTangent))
			{
				return false;
			}
			if (bRequireCurve && (!bHasStartPosition || !bHasEndPosition))
			{
				OutError = TEXT("spline mesh entry requires start_position and end_position");
				return false;
			}

			const FVector DefaultTangent = OutPatch.EndPosition - OutPatch.StartPosition;
			if (!bHasStartTangent && bHasStartPosition && bHasEndPosition)
			{
				OutPatch.StartTangent = DefaultTangent;
			}
			if (!bHasEndTangent && bHasStartPosition && bHasEndPosition)
			{
				OutPatch.EndTangent = DefaultTangent;
			}
			OutPatch.bSetCurve = true;
		}
		else if (bRequireCurve)
		{
			OutError = TEXT("spline mesh entry requires direct curve fields or from_spline");
			return false;
		}

		auto ParseVector2Field = [&EntryObj, &OutError](const TCHAR* Field, TOptional<FVector2D>& OutValue) -> bool
		{
			const TSharedPtr<FJsonValue>* Value = EntryObj->Values.Find(Field);
			if (!Value)
			{
				return true;
			}
			FVector2D Parsed;
			if (!TryParseVector2(*Value, Parsed))
			{
				OutError = FString::Printf(TEXT("%s must be a two-component vector"), Field);
				return false;
			}
			OutValue = Parsed;
			return true;
		};
		if (!ParseVector2Field(TEXT("start_scale"), OutPatch.StartScale)
			|| !ParseVector2Field(TEXT("end_scale"), OutPatch.EndScale)
			|| !ParseVector2Field(TEXT("start_offset"), OutPatch.StartOffset)
			|| !ParseVector2Field(TEXT("end_offset"), OutPatch.EndOffset))
		{
			return false;
		}

		auto ParseRoll = [&EntryObj, &OutError](const TCHAR* RadiansField, const TCHAR* DegreesField, TOptional<float>& OutValue) -> bool
		{
			const bool bHasRadians = EntryObj->HasField(RadiansField);
			const bool bHasDegrees = EntryObj->HasField(DegreesField);
			if (bHasRadians && bHasDegrees)
			{
				OutError = FString::Printf(TEXT("%s and %s are mutually exclusive"), RadiansField, DegreesField);
				return false;
			}

			double Parsed = 0.0;
			if (bHasRadians)
			{
				if (!EntryObj->TryGetNumberField(RadiansField, Parsed))
				{
					OutError = FString::Printf(TEXT("%s must be a number"), RadiansField);
					return false;
				}
				OutValue = (float)Parsed;
			}
			else if (bHasDegrees)
			{
				if (!EntryObj->TryGetNumberField(DegreesField, Parsed))
				{
					OutError = FString::Printf(TEXT("%s must be a number"), DegreesField);
					return false;
				}
				OutValue = FMath::DegreesToRadians((float)Parsed);
			}
			return true;
		};
		if (!ParseRoll(TEXT("start_roll"), TEXT("start_roll_degrees"), OutPatch.StartRollRadians)
			|| !ParseRoll(TEXT("end_roll"), TEXT("end_roll_degrees"), OutPatch.EndRollRadians))
		{
			return false;
		}

		if (EntryObj->HasField(TEXT("forward_axis")))
		{
			FString AxisString;
			ESplineMeshAxis::Type Axis = ESplineMeshAxis::X;
			if (!EntryObj->TryGetStringField(TEXT("forward_axis"), AxisString)
				|| !TryParseSplineMeshAxis(AxisString, Axis))
			{
				OutError = TEXT("forward_axis must be X, Y, or Z");
				return false;
			}
			OutPatch.ForwardAxis = Axis;
		}

		if (const TSharedPtr<FJsonValue>* UpValue = EntryObj->Values.Find(TEXT("spline_up_dir")))
		{
			FVector UpDirection;
			if (!TryParseVector3(*UpValue, UpDirection) || UpDirection.IsNearlyZero())
			{
				OutError = TEXT("spline_up_dir must be a non-zero three-component vector");
				return false;
			}
			OutPatch.SplineUpDirection = UpDirection.GetSafeNormal();
		}

		auto ParseBoolField = [&EntryObj, &OutError](const TCHAR* Field, TOptional<bool>& OutValue) -> bool
		{
			if (!EntryObj->HasField(Field))
			{
				return true;
			}
			bool bParsed = false;
			if (!EntryObj->TryGetBoolField(Field, bParsed))
			{
				OutError = FString::Printf(TEXT("%s must be a boolean"), Field);
				return false;
			}
			OutValue = bParsed;
			return true;
		};
		if (!ParseBoolField(TEXT("smooth_interp_roll_scale"), OutPatch.bSmoothInterpRollScale)
			|| !ParseBoolField(TEXT("allow_spline_editing_per_instance"), OutPatch.bAllowSplineEditingPerInstance)
			|| !ParseBoolField(TEXT("generate_overlap_events"), OutPatch.bGenerateOverlapEvents)
			|| !ParseBoolField(TEXT("cast_shadow"), OutPatch.bCastShadow))
		{
			return false;
		}

		if (EntryObj->HasField(TEXT("collision_enabled")))
		{
			FString CollisionString;
			ECollisionEnabled::Type Collision = ECollisionEnabled::NoCollision;
			if (!EntryObj->TryGetStringField(TEXT("collision_enabled"), CollisionString)
				|| !TryParsePortableCollisionEnabled(CollisionString, Collision))
			{
				OutError = TEXT("collision_enabled must be NoCollision, QueryOnly, PhysicsOnly, or QueryAndPhysics");
				return false;
			}
			OutPatch.CollisionEnabled = Collision;
		}

		return true;
	}

	static void ApplySplineMeshPatch(USplineMeshComponent* Component, const FBATSplineMeshPatch& Patch)
	{
		if (!Component)
		{
			return;
		}

		if (Patch.bSetCurve)
		{
			Component->SetStartAndEnd(
				Patch.StartPosition,
				Patch.StartTangent,
				Patch.EndPosition,
				Patch.EndTangent,
				false);
		}
		if (Patch.StartScale.IsSet())
		{
			Component->SetStartScale(Patch.StartScale.GetValue(), false);
		}
		if (Patch.EndScale.IsSet())
		{
			Component->SetEndScale(Patch.EndScale.GetValue(), false);
		}
		if (Patch.StartRollRadians.IsSet())
		{
			Component->SetStartRoll(Patch.StartRollRadians.GetValue(), false);
		}
		if (Patch.EndRollRadians.IsSet())
		{
			Component->SetEndRoll(Patch.EndRollRadians.GetValue(), false);
		}
		if (Patch.StartOffset.IsSet())
		{
			Component->SetStartOffset(Patch.StartOffset.GetValue(), false);
		}
		if (Patch.EndOffset.IsSet())
		{
			Component->SetEndOffset(Patch.EndOffset.GetValue(), false);
		}
		if (Patch.ForwardAxis.IsSet())
		{
			Component->SetForwardAxis(Patch.ForwardAxis.GetValue(), false);
		}
		if (Patch.SplineUpDirection.IsSet())
		{
			Component->SetSplineUpDir(Patch.SplineUpDirection.GetValue(), false);
		}
		if (Patch.bSmoothInterpRollScale.IsSet())
		{
			Component->bSmoothInterpRollScale = Patch.bSmoothInterpRollScale.GetValue();
		}
		if (Patch.bAllowSplineEditingPerInstance.IsSet())
		{
			Component->bAllowSplineEditingPerInstance = Patch.bAllowSplineEditingPerInstance.GetValue();
		}
		if (Patch.CollisionEnabled.IsSet())
		{
			Component->SetCollisionEnabled(Patch.CollisionEnabled.GetValue());
		}
		if (Patch.bGenerateOverlapEvents.IsSet())
		{
			Component->SetGenerateOverlapEvents(Patch.bGenerateOverlapEvents.GetValue());
		}
		if (Patch.bCastShadow.IsSet())
		{
			Component->SetCastShadow(Patch.bCastShadow.GetValue());
		}

		Component->UpdateMesh();
	}

	static bool TryParseSplinePoints(const TSharedPtr<FJsonObject>& EntryObj, TArray<FVector>& OutPoints)
	{
		OutPoints.Reset();
		if (!EntryObj.IsValid())
		{
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* PointsArr = nullptr;
		if (!EntryObj->TryGetArrayField(TEXT("points"), PointsArr) || !PointsArr)
		{
			return false;
		}

		for (const TSharedPtr<FJsonValue>& PointVal : *PointsArr)
		{
			FVector Point;
			if (TryParseVector3(PointVal, Point))
			{
				OutPoints.Add(Point);
			}
		}

		return OutPoints.Num() > 0;
	}

	static bool TryAppendInstancesFromSpline(
		UBlueprint* Blueprint,
		const TSharedPtr<FJsonObject>& EntryObj,
		TArray<FTransform>& InOutInstances,
		FString& OutError)
	{
		OutError.Empty();
		if (!Blueprint || !EntryObj.IsValid())
		{
			return true;
		}

		const TSharedPtr<FJsonObject>* FromSplineObjPtr = nullptr;
		if (!EntryObj->TryGetObjectField(TEXT("from_spline"), FromSplineObjPtr) || !FromSplineObjPtr || !FromSplineObjPtr->IsValid())
		{
			return true;
		}

		const TSharedPtr<FJsonObject> FromSplineObj = *FromSplineObjPtr;
		FString SplineComponentName;
		if (!FromSplineObj->TryGetStringField(TEXT("component"), SplineComponentName) || SplineComponentName.IsEmpty())
		{
			OutError = TEXT("from_spline requires 'component' name");
			return false;
		}

		USplineComponent* SourceSpline = Cast<USplineComponent>(FindSceneTemplateByName(Blueprint, SplineComponentName, USplineComponent::StaticClass()));
		if (!SourceSpline)
		{
			OutError = TEXT("from_spline component not found or not a SplineComponent");
			return false;
		}

		double StepNum = 300.0;
		FromSplineObj->TryGetNumberField(TEXT("step"), StepNum);
		const float Step = (float)StepNum;
		if (Step <= KINDA_SMALL_NUMBER)
		{
			OutError = TEXT("from_spline.step must be > 0");
			return false;
		}

		const float SplineLen = SourceSpline->GetSplineLength();
		double StartNum = 0.0;
		double EndNum = SplineLen;
		FromSplineObj->TryGetNumberField(TEXT("start_distance"), StartNum);
		FromSplineObj->TryGetNumberField(TEXT("end_distance"), EndNum);
		const float StartDist = FMath::Clamp((float)StartNum, 0.0f, SplineLen);
		const float EndDist = FMath::Clamp((float)EndNum, 0.0f, SplineLen);
		if (EndDist < StartDist)
		{
			OutError = TEXT("from_spline.end_distance must be >= start_distance");
			return false;
		}

		bool bAlignToTangent = true;
		FromSplineObj->TryGetBoolField(TEXT("align_to_tangent"), bAlignToTangent);

		FVector Offset = FVector::ZeroVector;
		if (const TSharedPtr<FJsonValue>* V = FromSplineObj->Values.Find(TEXT("offset")))
		{
			TryParseVector3(*V, Offset);
		}

		FVector RotationVec = FVector::ZeroVector;
		if (const TSharedPtr<FJsonValue>* V = FromSplineObj->Values.Find(TEXT("rotation")))
		{
			TryParseVector3(*V, RotationVec);
		}

		FVector Scale = FVector(1.0f, 1.0f, 1.0f);
		if (const TSharedPtr<FJsonValue>* V = FromSplineObj->Values.Find(TEXT("scale")))
		{
			TryParseVector3(*V, Scale);
		}

		for (float Dist = StartDist; Dist <= EndDist + KINDA_SMALL_NUMBER; Dist += Step)
		{
			const FVector SplineLoc = SourceSpline->GetLocationAtDistanceAlongSpline(Dist, ESplineCoordinateSpace::Local);
			const FRotator SplineRot = bAlignToTangent
				? SourceSpline->GetRotationAtDistanceAlongSpline(Dist, ESplineCoordinateSpace::Local)
				: FRotator::ZeroRotator;

			const FTransform BaseTransform(SplineRot, SplineLoc, FVector(1.0f, 1.0f, 1.0f));
			const FTransform LocalAdjust(FRotator(RotationVec.X, RotationVec.Y, RotationVec.Z), Offset, Scale);
			InOutInstances.Add(LocalAdjust * BaseTransform);
		}

		if (InOutInstances.Num() == 0)
		{
			OutError = TEXT("from_spline produced zero instances");
			return false;
		}

		return true;
	}

	static bool ParseInstancesArray(const TSharedPtr<FJsonObject>& EntryObj, TArray<FTransform>& OutInstances, FString& OutError)
	{
		OutInstances.Reset();
		OutError.Empty();
		if (!EntryObj.IsValid())
		{
			OutError = TEXT("entry must be an object");
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* InstancesArr = nullptr;
		if (!EntryObj->TryGetArrayField(TEXT("instances"), InstancesArr) || !InstancesArr)
		{
			OutError = TEXT("entry requires 'instances' array");
			return false;
		}

		for (const TSharedPtr<FJsonValue>& InstanceVal : *InstancesArr)
		{
			FTransform Xf;
			if (!TryParseTransformFromJsonValue(InstanceVal, Xf))
			{
				OutError = TEXT("invalid instance transform object");
				return false;
			}
			OutInstances.Add(Xf);
		}

		if (OutInstances.Num() == 0)
		{
			OutError = TEXT("instances array cannot be empty");
			return false;
		}

		return true;
	}

	static bool ApplyAllowlistedComponentEntry(
		UBlueprint* Blueprint,
		USCS_Node* Node,
		EBATBlueprintComponentKind Kind,
		const TSharedPtr<FJsonObject>& EntryObj,
		bool bReplaceInstances,
		int32& InOutApplied,
		int32& InOutRejected,
		FString& OutError)
	{
		OutError.Empty();
		if (!Blueprint || !Node || !EntryObj.IsValid() || !Node->ComponentTemplate)
		{
			OutError = TEXT("invalid component entry");
			return false;
		}

		USceneComponent* SceneComp = Cast<USceneComponent>(Node->ComponentTemplate);
		UStaticMeshComponent* MeshComp = Cast<UStaticMeshComponent>(Node->ComponentTemplate);
		if (!SceneComp || !MeshComp)
		{
			OutError = TEXT("component template is not a scene/mesh component");
			return false;
		}

		Node->Modify();
		SceneComp->Modify();
		SceneComp->PreEditChange(nullptr);

		bool bAssetsOk = true;
		ApplyAllowedMeshAssets(MeshComp, EntryObj, InOutApplied, InOutRejected, bAssetsOk);
		if (!bAssetsOk)
		{
			SceneComp->PostEditChange();
			OutError = TEXT("failed to load allowlisted asset path");
			return false;
		}

		const TSharedPtr<FJsonObject>* PropertiesObjPtr = nullptr;
		if (EntryObj->TryGetObjectField(TEXT("properties"), PropertiesObjPtr) && PropertiesObjPtr && PropertiesObjPtr->IsValid())
		{
			ApplyAllowedSceneTransformProperties(SceneComp, *PropertiesObjPtr, InOutApplied, InOutRejected);
		}

		if (Kind == EBATBlueprintComponentKind::ISM || Kind == EBATBlueprintComponentKind::HISM)
		{
			UInstancedStaticMeshComponent* ISMComp = Cast<UInstancedStaticMeshComponent>(Node->ComponentTemplate);
			if (!ISMComp)
			{
				SceneComp->PostEditChange();
				OutError = TEXT("component template is not InstancedStaticMeshComponent");
				return false;
			}

			const bool bHasInstances = EntryObj->HasTypedField<EJson::Array>(TEXT("instances"));
			if (bReplaceInstances || bHasInstances)
			{
				TArray<FTransform> ParsedInstances;
				FString ParseError;
				if (!ParseInstancesArray(EntryObj, ParsedInstances, ParseError))
				{
					SceneComp->PostEditChange();
					OutError = ParseError;
					return false;
				}

				if (bReplaceInstances)
				{
					ISMComp->ClearInstances();
				}

				for (const FTransform& Xf : ParsedInstances)
				{
					ISMComp->AddInstance(Xf);
					++InOutApplied;
				}
			}
		}
		else if (EntryObj->HasTypedField<EJson::Array>(TEXT("instances")))
		{
			SceneComp->PostEditChange();
			OutError = TEXT("instances are only valid for InstancedStaticMeshComponent/HISM");
			return false;
		}

		SceneComp->PostEditChange();
		return true;
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

	static UEdGraphPin* FindPinByName(UEdGraphNode* Node, const FString& PinName)
	{
		if (!Node)
		{
			return nullptr;
		}

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->PinName.ToString() == PinName)
			{
				return Pin;
			}
		}
		return Node->FindPin(*PinName);
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
		if (!Node->NodeComment.IsEmpty())
		{
			Obj->SetStringField(TEXT("comment"), Node->NodeComment);
		}

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

	static bool ShouldIncludeSchemaSection(const TSet<FString>& RequestedSections, const TCHAR* SectionName)
	{
		if (RequestedSections.Num() == 0)
		{
			return true;
		}
		return RequestedSections.Contains(FString(SectionName));
	}

	static FString DescribePinTypeAsTypeString(const FEdGraphPinType& PinType)
	{
		FString BaseType;
		if (PinType.PinCategory == UEdGraphSchema_K2::PC_Boolean)
		{
			BaseType = TEXT("bool");
		}
		else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Int)
		{
			BaseType = TEXT("int");
		}
		else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Int64)
		{
			BaseType = TEXT("int64");
		}
		else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Real)
		{
			BaseType = (PinType.PinSubCategory == UEdGraphSchema_K2::PC_Double) ? TEXT("double") : TEXT("float");
		}
		else if (PinType.PinCategory == UEdGraphSchema_K2::PC_String)
		{
			BaseType = TEXT("string");
		}
		else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Name)
		{
			BaseType = TEXT("name");
		}
		else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Text)
		{
			BaseType = TEXT("text");
		}
		else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
		{
			if (PinType.PinSubCategoryObject == TBaseStructure<FVector>::Get())
			{
				BaseType = TEXT("vector");
			}
			else if (PinType.PinSubCategoryObject == TBaseStructure<FRotator>::Get())
			{
				BaseType = TEXT("rotator");
			}
			else if (PinType.PinSubCategoryObject == TBaseStructure<FTransform>::Get())
			{
				BaseType = TEXT("transform");
			}
			else
			{
				BaseType = PinType.PinSubCategoryObject.IsValid() ? PinType.PinSubCategoryObject->GetPathName() : PinType.PinSubCategory.ToString();
			}
		}
		else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Object)
		{
			BaseType = FString::Printf(TEXT("object:%s"), PinType.PinSubCategoryObject.IsValid() ? *PinType.PinSubCategoryObject->GetPathName() : TEXT("/Script/CoreUObject.Object"));
		}
		else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Class)
		{
			BaseType = FString::Printf(TEXT("class:%s"), PinType.PinSubCategoryObject.IsValid() ? *PinType.PinSubCategoryObject->GetPathName() : TEXT("/Script/CoreUObject.Object"));
		}
		else
		{
			BaseType = PinType.PinCategory.ToString();
		}

		if (PinType.ContainerType == EPinContainerType::Array)
		{
			BaseType += TEXT("[]");
		}

		return BaseType;
	}

	static FString DescribePropertyAsTypeString(FProperty* Property)
	{
		if (!Property)
		{
			return TEXT("unknown");
		}

		FEdGraphPinType PinType;
		const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
		if (K2Schema && K2Schema->ConvertPropertyToPinType(Property, PinType))
		{
			return DescribePinTypeAsTypeString(PinType);
		}

		return Property->GetClass()->GetName();
	}

	static FString GetBlueprintGraphKind(UBlueprint* Blueprint, UEdGraph* Graph)
	{
		if (!Blueprint || !Graph)
		{
			return TEXT("unknown");
		}

		if (Blueprint->UbergraphPages.Contains(Graph))
		{
			return TEXT("ubergraph");
		}
		if (Blueprint->FunctionGraphs.Contains(Graph))
		{
			return TEXT("function");
		}
		if (Blueprint->MacroGraphs.Contains(Graph))
		{
			return TEXT("macro");
		}
		return TEXT("graph");
	}

	static void AddEditablePropertyName(TArray<TSharedPtr<FJsonValue>>& OutFields, const TCHAR* Name)
	{
		OutFields.Add(MakeShared<FJsonValueString>(FString(Name)));
	}

	static void AppendEditablePropertiesForComponent(UActorComponent* ComponentTemplate, TArray<TSharedPtr<FJsonValue>>& OutFields)
	{
		OutFields.Reset();
		if (!ComponentTemplate)
		{
			return;
		}

		if (ComponentTemplate->IsA<UStaticMeshComponent>())
		{
			AddEditablePropertyName(OutFields, TEXT("static_mesh"));
			AddEditablePropertyName(OutFields, TEXT("material0"));
			AddEditablePropertyName(OutFields, TEXT("mobility"));
			AddEditablePropertyName(OutFields, TEXT("relative_location"));
			AddEditablePropertyName(OutFields, TEXT("relative_rotation"));
			AddEditablePropertyName(OutFields, TEXT("relative_scale3d"));
		}
		if (ComponentTemplate->IsA<UInstancedStaticMeshComponent>())
		{
			AddEditablePropertyName(OutFields, TEXT("num_custom_data_floats"));
		}
		if (ComponentTemplate->IsA<USplineComponent>())
		{
			AddEditablePropertyName(OutFields, TEXT("points"));
			AddEditablePropertyName(OutFields, TEXT("point_type"));
			AddEditablePropertyName(OutFields, TEXT("closed_loop"));
		}
		if (ComponentTemplate->IsA<USplineMeshComponent>())
		{
			AddEditablePropertyName(OutFields, TEXT("start_position"));
			AddEditablePropertyName(OutFields, TEXT("start_tangent"));
			AddEditablePropertyName(OutFields, TEXT("end_position"));
			AddEditablePropertyName(OutFields, TEXT("end_tangent"));
			AddEditablePropertyName(OutFields, TEXT("from_spline"));
			AddEditablePropertyName(OutFields, TEXT("start_scale"));
			AddEditablePropertyName(OutFields, TEXT("end_scale"));
			AddEditablePropertyName(OutFields, TEXT("start_roll"));
			AddEditablePropertyName(OutFields, TEXT("start_roll_degrees"));
			AddEditablePropertyName(OutFields, TEXT("end_roll"));
			AddEditablePropertyName(OutFields, TEXT("end_roll_degrees"));
			AddEditablePropertyName(OutFields, TEXT("start_offset"));
			AddEditablePropertyName(OutFields, TEXT("end_offset"));
			AddEditablePropertyName(OutFields, TEXT("forward_axis"));
			AddEditablePropertyName(OutFields, TEXT("spline_up_dir"));
			AddEditablePropertyName(OutFields, TEXT("smooth_interp_roll_scale"));
			AddEditablePropertyName(OutFields, TEXT("allow_spline_editing_per_instance"));
			AddEditablePropertyName(OutFields, TEXT("collision_enabled"));
			AddEditablePropertyName(OutFields, TEXT("generate_overlap_events"));
			AddEditablePropertyName(OutFields, TEXT("cast_shadow"));
		}
	}

	static TSharedPtr<FJsonValue> MakeBlueprintVariableJson(const FBPVariableDescription& Variable)
	{
		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), Variable.VarName.ToString());
		Obj->SetStringField(TEXT("type"), DescribePinTypeAsTypeString(Variable.VarType));
		Obj->SetStringField(TEXT("default_value"), Variable.DefaultValue);
		Obj->SetBoolField(TEXT("instance_editable"), (Variable.PropertyFlags & CPF_DisableEditOnInstance) == 0);
		Obj->SetBoolField(TEXT("blueprint_read_only"), (Variable.PropertyFlags & CPF_BlueprintReadOnly) != 0);
		Obj->SetBoolField(TEXT("blueprint_visible"), (Variable.PropertyFlags & CPF_BlueprintVisible) != 0);
		return MakeShared<FJsonValueObject>(Obj);
	}

	static TArray<TSharedPtr<FJsonValue>> MakeSupportedNodeTypesJson()
	{
		TArray<TSharedPtr<FJsonValue>> Types;

		auto AddType = [&Types](const TCHAR* TypeName, const TArray<FString>& Required, const TArray<FString>& Optional)
		{
			TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("type"), TypeName);

			TArray<TSharedPtr<FJsonValue>> RequiredValues;
			for (const FString& Value : Required)
			{
				RequiredValues.Add(MakeShared<FJsonValueString>(Value));
			}
			Obj->SetArrayField(TEXT("required_fields"), RequiredValues);

			TArray<TSharedPtr<FJsonValue>> OptionalValues;
			for (const FString& Value : Optional)
			{
				OptionalValues.Add(MakeShared<FJsonValueString>(Value));
			}
			Obj->SetArrayField(TEXT("optional_fields"), OptionalValues);
			Types.Add(MakeShared<FJsonValueObject>(Obj));
		};

		AddType(TEXT("K2Node_Event"), { TEXT("id"), TEXT("type"), TEXT("event") }, { TEXT("x"), TEXT("y") });
		AddType(TEXT("K2Node_ComponentBoundEvent"), { TEXT("id"), TEXT("type"), TEXT("component"), TEXT("event") }, { TEXT("x"), TEXT("y") });
		AddType(TEXT("K2Node_SpawnActor"), { TEXT("id"), TEXT("type"), TEXT("class") }, { TEXT("x"), TEXT("y"), TEXT("pins") });
		AddType(TEXT("K2Node_CallFunction"), { TEXT("id"), TEXT("type"), TEXT("function") }, { TEXT("x"), TEXT("y"), TEXT("pins") });
		AddType(TEXT("K2Node_PrintString"), { TEXT("id"), TEXT("type"), TEXT("message") }, { TEXT("x"), TEXT("y") });
		AddType(TEXT("K2Node_Delay"), { TEXT("id"), TEXT("type") }, { TEXT("x"), TEXT("y"), TEXT("pins") });
		AddType(TEXT("K2Node_AddComponent"), { TEXT("id"), TEXT("type"), TEXT("class") }, { TEXT("x"), TEXT("y"), TEXT("pins") });
		AddType(TEXT("K2Node_VariableGet"), { TEXT("id"), TEXT("type"), TEXT("variable") }, { TEXT("x"), TEXT("y") });
		AddType(TEXT("K2Node_VariableSet"), { TEXT("id"), TEXT("type"), TEXT("variable") }, { TEXT("x"), TEXT("y"), TEXT("pins") });
		AddType(TEXT("K2Node_ExecutionSequence"), { TEXT("id"), TEXT("type") }, { TEXT("x"), TEXT("y"), TEXT("outputs") });
		AddType(TEXT("K2Node_Knot"), { TEXT("id"), TEXT("type") }, { TEXT("x"), TEXT("y") });
		AddType(TEXT("K2Node_MacroInstance"), { TEXT("id"), TEXT("type"), TEXT("macro") }, { TEXT("x"), TEXT("y"), TEXT("pins") });

		return Types;
	}

	static void BuildGraphSnapshotJson(UBlueprint* Blueprint, UEdGraph* Graph, TSharedRef<FJsonObject>& OutGraphObj)
	{
		TArray<TSharedPtr<FJsonValue>> NodeArr;
		TArray<TSharedPtr<FJsonValue>> LinkArr;
		TMap<const UEdGraphNode*, FString> NodeIds;
		TSet<FString> SeenLinks;

		if (!Blueprint || !Graph)
		{
			OutGraphObj->SetArrayField(TEXT("nodes"), NodeArr);
			OutGraphObj->SetArrayField(TEXT("links"), LinkArr);
			return;
		}

		OutGraphObj->SetStringField(TEXT("graph"), Graph->GetName());
		OutGraphObj->SetStringField(TEXT("kind"), GetBlueprintGraphKind(Blueprint, Graph));

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node)
			{
				continue;
			}

			const FString NodeId = GetStableNodeId(Node);
			NodeIds.Add(Node, NodeId);

			TSharedPtr<FJsonObject> NodeObj = DescribeNodeJson(Node);
			NodeObj->SetStringField(TEXT("id"), NodeId);
			NodeArr.Add(MakeShared<FJsonValueObject>(NodeObj.ToSharedRef()));
		}

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

					TSharedRef<FJsonObject> FromObj = MakeShared<FJsonObject>();
					FromObj->SetStringField(TEXT("nodeId"), *FromNodeId);
					FromObj->SetStringField(TEXT("pin"), Pin->PinName.ToString());

					TSharedRef<FJsonObject> ToObj = MakeShared<FJsonObject>();
					ToObj->SetStringField(TEXT("nodeId"), *ToNodeId);
					ToObj->SetStringField(TEXT("pin"), LinkedPin->PinName.ToString());

					TSharedRef<FJsonObject> LinkObj = MakeShared<FJsonObject>();
					LinkObj->SetObjectField(TEXT("from"), FromObj);
					LinkObj->SetObjectField(TEXT("to"), ToObj);
					LinkArr.Add(MakeShared<FJsonValueObject>(LinkObj));
				}
			}
		}

		OutGraphObj->SetArrayField(TEXT("nodes"), NodeArr);
		OutGraphObj->SetArrayField(TEXT("links"), LinkArr);
	}

	static void CollectCallableFunctionsForClass(
		UClass* SourceClass,
		const FString& Search,
		bool bBlueprintCallableOnly,
		bool bExcludeLatent,
		bool bExcludeUnsafe,
		int32 MaxFunctions,
		TSet<FString>& InOutSeenPaths,
		TArray<TSharedPtr<FJsonValue>>& OutFunctions)
	{
		if (!SourceClass || MaxFunctions <= 0)
		{
			return;
		}

		for (TFieldIterator<UFunction> It(SourceClass, EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			if (OutFunctions.Num() >= MaxFunctions)
			{
				return;
			}

			UFunction* Function = *It;
			if (!Function)
			{
				continue;
			}

			if (bBlueprintCallableOnly && !Function->HasAnyFunctionFlags(FUNC_BlueprintCallable))
			{
				continue;
			}
			if (bExcludeLatent && Function->HasMetaData(TEXT("Latent")))
			{
				continue;
			}
			if (bExcludeUnsafe && Function->HasAnyFunctionFlags(FUNC_Exec | FUNC_Net | FUNC_NetClient | FUNC_NetServer | FUNC_NetMulticast))
			{
				continue;
			}

			UClass* OwnerClass = Function->GetOwnerClass();
			if (!OwnerClass)
			{
				continue;
			}

			const FString FunctionPath = FString::Printf(TEXT("%s:%s"), *OwnerClass->GetPathName(), *Function->GetName());
			if (InOutSeenPaths.Contains(FunctionPath))
			{
				continue;
			}

			const FString SearchHaystack = FString::Printf(TEXT("%s %s %s"), *Function->GetName(), *Function->GetDisplayNameText().ToString(), *FunctionPath);
			if (!Search.IsEmpty() && !SearchHaystack.Contains(Search, ESearchCase::IgnoreCase))
			{
				continue;
			}

			InOutSeenPaths.Add(FunctionPath);

			TArray<TSharedPtr<FJsonValue>> Inputs;
			TArray<TSharedPtr<FJsonValue>> Outputs;
			for (TFieldIterator<FProperty> ParamIt(Function); ParamIt && (ParamIt->PropertyFlags & CPF_Parm); ++ParamIt)
			{
				FProperty* Param = *ParamIt;
				if (!Param)
				{
					continue;
				}

				TSharedRef<FJsonObject> ParamObj = MakeShared<FJsonObject>();
				ParamObj->SetStringField(TEXT("name"), Param->GetName());
				ParamObj->SetStringField(TEXT("type"), DescribePropertyAsTypeString(Param));
				ParamObj->SetBoolField(TEXT("is_return"), Param->HasAnyPropertyFlags(CPF_ReturnParm));
				ParamObj->SetBoolField(TEXT("is_out"), Param->HasAnyPropertyFlags(CPF_OutParm));
				ParamObj->SetBoolField(TEXT("is_reference"), Param->HasAnyPropertyFlags(CPF_ReferenceParm));

				if (Param->HasAnyPropertyFlags(CPF_ReturnParm) || Param->HasAnyPropertyFlags(CPF_OutParm))
				{
					Outputs.Add(MakeShared<FJsonValueObject>(ParamObj));
				}
				else
				{
					Inputs.Add(MakeShared<FJsonValueObject>(ParamObj));
				}
			}

			TSharedRef<FJsonObject> FunctionObj = MakeShared<FJsonObject>();
			FunctionObj->SetStringField(TEXT("path"), FunctionPath);
			FunctionObj->SetStringField(TEXT("name"), Function->GetName());
			FunctionObj->SetStringField(TEXT("display_name"), Function->GetDisplayNameText().ToString());
			FunctionObj->SetStringField(TEXT("owner_class"), OwnerClass->GetPathName());
			FunctionObj->SetBoolField(TEXT("blueprint_callable"), Function->HasAnyFunctionFlags(FUNC_BlueprintCallable));
			FunctionObj->SetBoolField(TEXT("static"), Function->HasAnyFunctionFlags(FUNC_Static));
			FunctionObj->SetBoolField(TEXT("pure"), Function->HasAnyFunctionFlags(FUNC_BlueprintPure));
			FunctionObj->SetBoolField(TEXT("latent"), Function->HasMetaData(TEXT("Latent")));
			FunctionObj->SetArrayField(TEXT("inputs"), Inputs);
			FunctionObj->SetArrayField(TEXT("outputs"), Outputs);
			OutFunctions.Add(MakeShared<FJsonValueObject>(FunctionObj));
		}
	}

	static void AddBlueprintOpError(TArray<TSharedPtr<FJsonValue>>& OutErrors, int32 OpIndex, const FString& Error)
	{
		TSharedRef<FJsonObject> Err = MakeShared<FJsonObject>();
		Err->SetNumberField(TEXT("op_index"), OpIndex);
		Err->SetStringField(TEXT("error"), Error);
		OutErrors.Add(MakeShared<FJsonValueObject>(Err));
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

bool FBlueprintAutomationToolkitModule::ExecuteBlueprintPatch(const TSharedPtr<FJsonObject>& BodyObj, bool bApply, int32& InOutTotalInstances, TSharedRef<FJsonObject>& OutResult, TArray<TSharedPtr<FJsonValue>>& OutErrors) const
{
	OutResult->SetStringField(TEXT("op"), TEXT("blueprint.apply"));

	if (!BodyObj.IsValid())
	{
		AddBlueprintOpError(OutErrors, -1, TEXT("invalid_payload"));
		OutResult->SetBoolField(TEXT("ok"), false);
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* OpsArray = nullptr;
	if (!BodyObj->TryGetArrayField(TEXT("ops"), OpsArray) || !OpsArray)
	{
		AddBlueprintOpError(OutErrors, -1, TEXT("missing_ops"));
		OutResult->SetBoolField(TEXT("ok"), false);
		return false;
	}

	FString BlueprintPath;
	BodyObj->TryGetStringField(TEXT("blueprint"), BlueprintPath);

	UBlueprint* Blueprint = nullptr;
	FString ObjectPath;
	if (!BlueprintPath.IsEmpty())
	{
		if (!TryLoadBlueprint(BlueprintPath, Blueprint, ObjectPath))
		{
			AddBlueprintOpError(OutErrors, -1, TEXT("blueprint_not_found"));
			OutResult->SetBoolField(TEXT("ok"), false);
			return false;
		}
	}

	bool bStructuralChange = false;
	bool bCompileRequested = false;
	int32 CreatedBlueprints = 0;
	int32 VariablesAdded = 0;
	int32 VariablesListedCount = 0;
	int32 FunctionsAdded = 0;
	int32 FunctionsListedCount = 0;
	int32 ComponentsAdded = 0;
	int32 ComponentsRemoved = 0;
	int32 ComponentsUpdated = 0;
	int32 InstancesAdded = 0;
	TArray<TSharedPtr<FJsonValue>> VariablesListed;
	TArray<TSharedPtr<FJsonValue>> FunctionsListed;
	TArray<TSharedPtr<FJsonValue>> ComponentsListed;

	const TArray<TSharedPtr<FJsonValue>>& Ops = *OpsArray;
	if (Ops.Num() > MaxOpsPerPlan)
	{
		AddBlueprintOpError(OutErrors, -1, FString::Printf(TEXT("max_ops_per_plan_exceeded:%d"), MaxOpsPerPlan));
		OutResult->SetBoolField(TEXT("ok"), false);
		return false;
	}

	for (int32 OpIndex = 0; OpIndex < Ops.Num(); ++OpIndex)
	{
		if (!Ops[OpIndex].IsValid() || Ops[OpIndex]->Type != EJson::Object)
		{
			AddBlueprintOpError(OutErrors, OpIndex, TEXT("op_entry_must_be_object"));
			continue;
		}

		const TSharedPtr<FJsonObject> OpObj = Ops[OpIndex]->AsObject();
		FString OpName;
		if (!OpObj.IsValid() || !OpObj->TryGetStringField(TEXT("op"), OpName) || OpName.IsEmpty())
		{
			AddBlueprintOpError(OutErrors, OpIndex, TEXT("missing_op"));
			continue;
		}

		if (OpName.Equals(TEXT("create"), ESearchCase::CaseSensitive))
		{
			FString PackagePath;
			FString Name;
			FString ParentClassPath;
			OpObj->TryGetStringField(TEXT("path"), PackagePath);
			OpObj->TryGetStringField(TEXT("name"), Name);
			OpObj->TryGetStringField(TEXT("parent"), ParentClassPath);

			if (PackagePath.IsEmpty() || Name.IsEmpty())
			{
				AddBlueprintOpError(OutErrors, OpIndex, TEXT("create_requires_path_and_name"));
				continue;
			}

			FString CleanPath = PackagePath;
			CleanPath.TrimStartAndEndInline();
			CleanPath.RemoveFromEnd(TEXT("/"));
			if (!FPackageName::IsValidLongPackageName(CleanPath))
			{
				AddBlueprintOpError(OutErrors, OpIndex, TEXT("bad_path"));
				continue;
			}

			UClass* ParentClass = AActor::StaticClass();
			if (!ParentClassPath.IsEmpty())
			{
				ParentClass = LoadObject<UClass>(nullptr, *ParentClassPath);
				if (!ParentClass)
				{
					ParentClass = FindObject<UClass>(nullptr, *ParentClassPath);
				}
				if (!ParentClass)
				{
					AddBlueprintOpError(OutErrors, OpIndex, TEXT("bad_parent"));
					continue;
				}
			}

			if (bApply)
			{
				const FString PackageName = FString::Printf(TEXT("%s/%s"), *CleanPath, *Name);
				const FString ExistingObjectPath = FString::Printf(TEXT("%s.%s"), *PackageName, *Name);
				if (UBlueprint* ExistingBlueprint = LoadObject<UBlueprint>(nullptr, *ExistingObjectPath))
				{
					Blueprint = ExistingBlueprint;
					ObjectPath = ExistingObjectPath;
					continue;
				}

				UPackage* Package = CreatePackage(*PackageName);
				if (!Package)
				{
					AddBlueprintOpError(OutErrors, OpIndex, TEXT("package_failed"));
					continue;
				}
				Package->Modify();

				UBlueprint* NewBlueprint = FKismetEditorUtilities::CreateBlueprint(
					ParentClass,
					Package,
					FName(*Name),
					BPTYPE_Normal,
					UBlueprint::StaticClass(),
					UBlueprintGeneratedClass::StaticClass(),
					NAME_None);
				if (!NewBlueprint)
				{
					AddBlueprintOpError(OutErrors, OpIndex, TEXT("create_failed"));
					continue;
				}

				NewBlueprint->Modify();
				FAssetRegistryModule::AssetCreated(NewBlueprint);
				Package->MarkPackageDirty();

				Blueprint = NewBlueprint;
				ObjectPath = ExistingObjectPath;
				++CreatedBlueprints;
				bStructuralChange = true;
			}

			continue;
		}

		if (!Blueprint)
		{
			AddBlueprintOpError(OutErrors, OpIndex, TEXT("blueprint_required"));
			continue;
		}

		if (OpName.Equals(TEXT("variables.add"), ESearchCase::CaseSensitive))
		{
			FString Name;
			FString Type;
			OpObj->TryGetStringField(TEXT("name"), Name);
			OpObj->TryGetStringField(TEXT("type"), Type);
			if (Name.IsEmpty() || Type.IsEmpty())
			{
				AddBlueprintOpError(OutErrors, OpIndex, TEXT("variables.add_requires_name_and_type"));
				continue;
			}

			const FName VariableName(*Name);
			if (FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, VariableName) != INDEX_NONE)
			{
				AddBlueprintOpError(OutErrors, OpIndex, TEXT("variable_already_exists"));
				continue;
			}

			FEdGraphPinType PinType;
			FString TypeError;
			if (!TryBuildBlueprintVariablePinType(Type, PinType, TypeError))
			{
				AddBlueprintOpError(OutErrors, OpIndex, TypeError);
				continue;
			}

			const TSharedPtr<FJsonValue>* DefaultValuePtr = OpObj->Values.Find(TEXT("default"));
			FString DefaultValue;
			FString DefaultError;
			if (!TryConvertVariableDefaultValue(Type, DefaultValuePtr, DefaultValue, DefaultError))
			{
				AddBlueprintOpError(OutErrors, OpIndex, DefaultError);
				continue;
			}

			const bool bHasInstanceEditable = OpObj->HasTypedField<EJson::Boolean>(TEXT("instance_editable"));
			const bool bInstanceEditable = bHasInstanceEditable ? OpObj->GetBoolField(TEXT("instance_editable")) : false;
			const bool bHasBlueprintReadOnly = OpObj->HasTypedField<EJson::Boolean>(TEXT("blueprint_read_only"));
			const bool bBlueprintReadOnly = bHasBlueprintReadOnly ? OpObj->GetBoolField(TEXT("blueprint_read_only")) : false;

			if (bApply)
			{
				Blueprint->Modify();
				if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, VariableName, PinType, DefaultValue))
				{
					AddBlueprintOpError(OutErrors, OpIndex, TEXT("variable_add_failed"));
					continue;
				}
				if (bHasInstanceEditable)
				{
					FBlueprintEditorUtils::SetBlueprintOnlyEditableFlag(Blueprint, VariableName, !bInstanceEditable);
				}
				if (bHasBlueprintReadOnly)
				{
					FBlueprintEditorUtils::SetBlueprintPropertyReadOnlyFlag(Blueprint, VariableName, bBlueprintReadOnly);
				}
			}

			++VariablesAdded;
			bStructuralChange = true;
			continue;
		}

		if (OpName.Equals(TEXT("variables.list"), ESearchCase::CaseSensitive))
		{
			FString NamePrefix;
			FString TypeFilter;
			OpObj->TryGetStringField(TEXT("name_prefix"), NamePrefix);
			OpObj->TryGetStringField(TEXT("type"), TypeFilter);

			for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
			{
				const FString VariableName = Variable.VarName.ToString();
				const FString VariableType = DescribePinTypeAsTypeString(Variable.VarType);
				if (!NamePrefix.IsEmpty() && !VariableName.StartsWith(NamePrefix, ESearchCase::IgnoreCase))
				{
					continue;
				}
				if (!TypeFilter.IsEmpty() && !VariableType.Equals(TypeFilter, ESearchCase::IgnoreCase))
				{
					continue;
				}

				VariablesListed.Add(MakeBlueprintVariableJson(Variable));
				++VariablesListedCount;
			}

			continue;
		}

		if (OpName.Equals(TEXT("functions.add"), ESearchCase::CaseSensitive))
		{
			FString Name;
			OpObj->TryGetStringField(TEXT("name"), Name);
			if (Name.IsEmpty())
			{
				AddBlueprintOpError(OutErrors, OpIndex, TEXT("functions.add_requires_name"));
				continue;
			}

			if (FindGraphByName(Blueprint, Name) != nullptr)
			{
				AddBlueprintOpError(OutErrors, OpIndex, TEXT("function_graph_already_exists"));
				continue;
			}

			if (bApply)
			{
				Blueprint->Modify();
				UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
					Blueprint,
					FName(*Name),
					UEdGraph::StaticClass(),
					UEdGraphSchema_K2::StaticClass());
				if (!NewGraph)
				{
					AddBlueprintOpError(OutErrors, OpIndex, TEXT("function_graph_create_failed"));
					continue;
				}

				FBlueprintEditorUtils::AddFunctionGraph<UFunction>(Blueprint, NewGraph, true, static_cast<UFunction*>(nullptr));
			}

			++FunctionsAdded;
			bStructuralChange = true;
			continue;
		}

		if (OpName.Equals(TEXT("functions.list"), ESearchCase::CaseSensitive))
		{
			FString Search;
			FString ClassPath;
			bool bBlueprintCallableOnly = true;
			bool bExcludeLatent = true;
			bool bExcludeUnsafe = true;
			int32 Limit = 100;

			OpObj->TryGetStringField(TEXT("search"), Search);
			OpObj->TryGetStringField(TEXT("class"), ClassPath);
			OpObj->TryGetBoolField(TEXT("blueprint_callable_only"), bBlueprintCallableOnly);
			OpObj->TryGetBoolField(TEXT("exclude_latent"), bExcludeLatent);
			OpObj->TryGetBoolField(TEXT("exclude_unsafe"), bExcludeUnsafe);
			OpObj->TryGetNumberField(TEXT("limit"), Limit);
			Limit = FMath::Clamp(Limit, 1, 500);

			TArray<UClass*> SourceClasses;
			if (!ClassPath.IsEmpty())
			{
				UClass* SourceClass = nullptr;
				if (!TryResolveClassPath(ClassPath, SourceClass))
				{
					AddBlueprintOpError(OutErrors, OpIndex, TEXT("functions.list_class_not_found"));
					continue;
				}
				SourceClasses.Add(SourceClass);
			}
			else
			{
				if (Blueprint->GeneratedClass)
				{
					SourceClasses.Add(Blueprint->GeneratedClass);
				}
				if (Blueprint->ParentClass && !SourceClasses.Contains(Blueprint->ParentClass))
				{
					SourceClasses.Add(Blueprint->ParentClass);
				}
			}

			TSet<FString> SeenPaths;
			for (UClass* SourceClass : SourceClasses)
			{
				CollectCallableFunctionsForClass(SourceClass, Search, bBlueprintCallableOnly, bExcludeLatent, bExcludeUnsafe, Limit, SeenPaths, FunctionsListed);
				if (FunctionsListed.Num() >= Limit)
				{
					break;
				}
			}

			FunctionsListedCount = FunctionsListed.Num();
			continue;
		}

		if (OpName.Equals(TEXT("components.add"), ESearchCase::CaseSensitive))
		{
			FString Name;
			FString ClassPath;
			OpObj->TryGetStringField(TEXT("name"), Name);
			OpObj->TryGetStringField(TEXT("class"), ClassPath);
			if (Name.IsEmpty() || ClassPath.IsEmpty())
			{
				AddBlueprintOpError(OutErrors, OpIndex, TEXT("components.add_requires_name_and_class"));
				continue;
			}

			const EBATBlueprintComponentKind Kind = ResolveBlueprintComponentKind(ClassPath);
			if (Kind == EBATBlueprintComponentKind::Unsupported)
			{
				AddBlueprintOpError(OutErrors, OpIndex, TEXT("component_class_not_allowlisted"));
				continue;
			}

			if (!Blueprint->SimpleConstructionScript)
			{
				AddBlueprintOpError(OutErrors, OpIndex, TEXT("no_simple_construction_script"));
				continue;
			}

			if (FindSCSNodeByName(Blueprint, Name))
			{
				AddBlueprintOpError(OutErrors, OpIndex, TEXT("component_already_exists"));
				continue;
			}

			if (bApply)
			{
				UClass* ComponentClass = ResolveBlueprintComponentClass(Kind);
				if (!ComponentClass)
				{
					AddBlueprintOpError(OutErrors, OpIndex, TEXT("component_class_resolution_failed"));
					continue;
				}

				Blueprint->Modify();
				USCS_Node* NewNode = Blueprint->SimpleConstructionScript->CreateNode(ComponentClass, FName(*Name));
				if (!NewNode)
				{
					AddBlueprintOpError(OutErrors, OpIndex, TEXT("component_create_failed"));
					continue;
				}
				NewNode->Modify();
				Blueprint->SimpleConstructionScript->AddNode(NewNode);
			}

			++ComponentsAdded;
			bStructuralChange = true;
			continue;
		}

		if (OpName.Equals(TEXT("components.remove"), ESearchCase::CaseSensitive))
		{
			FString Name;
			OpObj->TryGetStringField(TEXT("name"), Name);
			if (Name.IsEmpty())
			{
				AddBlueprintOpError(OutErrors, OpIndex, TEXT("components.remove_requires_name"));
				continue;
			}

			const bool bMissingOk = OpObj->HasTypedField<EJson::Boolean>(TEXT("missing_ok")) ? OpObj->GetBoolField(TEXT("missing_ok")) : false;

			if (!Blueprint->SimpleConstructionScript)
			{
				AddBlueprintOpError(OutErrors, OpIndex, TEXT("no_simple_construction_script"));
				continue;
			}

			USCS_Node* ExistingNode = FindSCSNodeByName(Blueprint, Name);
			if (!ExistingNode)
			{
				if (!bMissingOk)
				{
					AddBlueprintOpError(OutErrors, OpIndex, TEXT("component_not_found"));
				}
				continue;
			}

			if (bApply)
			{
				Blueprint->Modify();
				Blueprint->SimpleConstructionScript->Modify();
				ExistingNode->Modify();
				Blueprint->SimpleConstructionScript->RemoveNode(ExistingNode);
			}

			++ComponentsRemoved;
			bStructuralChange = true;
			continue;
		}

		if (OpName.Equals(TEXT("components.set"), ESearchCase::CaseSensitive))
		{
			FString Name;
			OpObj->TryGetStringField(TEXT("name"), Name);
			if (Name.IsEmpty())
			{
				AddBlueprintOpError(OutErrors, OpIndex, TEXT("components.set_requires_name"));
				continue;
			}

			USCS_Node* Node = FindSCSNodeByName(Blueprint, Name);
			if (!Node || !Node->ComponentTemplate)
			{
				AddBlueprintOpError(OutErrors, OpIndex, TEXT("component_not_found"));
				continue;
			}

			USceneComponent* SceneComp = Cast<USceneComponent>(Node->ComponentTemplate);
			UStaticMeshComponent* MeshComp = Cast<UStaticMeshComponent>(Node->ComponentTemplate);
			UInstancedStaticMeshComponent* ISMComp = Cast<UInstancedStaticMeshComponent>(Node->ComponentTemplate);
			USplineMeshComponent* SplineMeshComp = Cast<USplineMeshComponent>(Node->ComponentTemplate);
			if (!SceneComp)
			{
				AddBlueprintOpError(OutErrors, OpIndex, TEXT("component_type_not_supported"));
				continue;
			}

			const bool bIsISM = ISMComp != nullptr;
			const bool bIsSplineMesh = SplineMeshComp != nullptr;
			const bool bHasMeshProperties = MeshComp != nullptr;
			const bool bHasAttachmentProperties = OpObj->HasField(TEXT("attach_parent")) || OpObj->HasField(TEXT("attach_socket_name"));
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : OpObj->Values)
			{
				const FString& Key = Pair.Key;
				if (Key.Equals(TEXT("op"), ESearchCase::CaseSensitive) || Key.Equals(TEXT("name"), ESearchCase::CaseSensitive))
				{
					continue;
				}
				const bool bAllowed =
					((Key.Equals(TEXT("static_mesh"), ESearchCase::IgnoreCase)
					|| Key.Equals(TEXT("material0"), ESearchCase::IgnoreCase)) && bHasMeshProperties)
					|| Key.Equals(TEXT("mobility"), ESearchCase::IgnoreCase)
					|| Key.Equals(TEXT("relative_location"), ESearchCase::IgnoreCase)
					|| Key.Equals(TEXT("relative_rotation"), ESearchCase::IgnoreCase)
					|| Key.Equals(TEXT("relative_scale3d"), ESearchCase::IgnoreCase)
					|| Key.Equals(TEXT("attach_parent"), ESearchCase::IgnoreCase)
					|| Key.Equals(TEXT("attach_socket_name"), ESearchCase::IgnoreCase)
					|| (bIsISM && Key.Equals(TEXT("num_custom_data_floats"), ESearchCase::IgnoreCase))
					|| (bIsSplineMesh && IsSplineMeshPatchField(Key));
				if (!bAllowed)
				{
					AddBlueprintOpError(OutErrors, OpIndex, FString::Printf(TEXT("property_not_allowed:%s"), *Key));
				}
			}

			USceneComponent* AttachParentComp = nullptr;
			USCS_Node* AttachParentNode = nullptr;
			FName AttachSocketName = Node->AttachToName;
			if (bHasAttachmentProperties)
			{
				AttachParentComp = Node->GetParentComponentTemplate(Blueprint);

				FString AttachParentName;
				if (OpObj->TryGetStringField(TEXT("attach_parent"), AttachParentName))
				{
					AttachParentName.TrimStartAndEndInline();
					if (AttachParentName.IsEmpty() || AttachParentName.Equals(TEXT("None"), ESearchCase::IgnoreCase))
					{
						AddBlueprintOpError(OutErrors, OpIndex, TEXT("invalid_attach_parent"));
						continue;
					}

					AttachParentNode = FindSCSNodeByName(Blueprint, AttachParentName);
					AttachParentComp = AttachParentNode
						? Cast<USceneComponent>(AttachParentNode->ComponentTemplate)
						: FindBlueprintComponentTemplateByName(Blueprint, AttachParentName);
					if (!AttachParentComp)
					{
						AddBlueprintOpError(OutErrors, OpIndex, TEXT("attach_parent_not_found"));
						continue;
					}
				}

				FString AttachSocketString;
				if (OpObj->TryGetStringField(TEXT("attach_socket_name"), AttachSocketString))
				{
					AttachSocketName = AttachSocketString.TrimStartAndEnd().IsEmpty() ? NAME_None : FName(*AttachSocketString.TrimStartAndEnd());
				}

				if (!AttachParentComp)
				{
					AddBlueprintOpError(OutErrors, OpIndex, TEXT("attach_parent_required"));
					continue;
				}
			}

			FBATSplineMeshPatch SplineMeshPatch;
			if (bIsSplineMesh)
			{
				FString SplineMeshError;
				if (!TryBuildSplineMeshPatch(Blueprint, SplineMeshComp, OpObj, false, SplineMeshPatch, SplineMeshError))
				{
					AddBlueprintOpError(OutErrors, OpIndex, SplineMeshError);
					continue;
				}
			}

			if (bApply)
			{
				Blueprint->Modify();
				if (Blueprint->SimpleConstructionScript)
				{
					Blueprint->SimpleConstructionScript->Modify();
				}
				Node->Modify();
				if (AttachParentNode)
				{
					AttachParentNode->Modify();
				}
				SceneComp->Modify();
				SceneComp->PreEditChange(nullptr);

				FString StaticMeshPath;
				if (MeshComp && OpObj->TryGetStringField(TEXT("static_mesh"), StaticMeshPath) && !StaticMeshPath.IsEmpty())
				{
					if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *StaticMeshPath))
					{
						MeshComp->SetStaticMesh(Mesh);
					}
					else
					{
						AddBlueprintOpError(OutErrors, OpIndex, TEXT("failed_to_load_static_mesh"));
					}
				}

				FString MaterialPath;
				if (MeshComp && OpObj->TryGetStringField(TEXT("material0"), MaterialPath) && !MaterialPath.IsEmpty())
				{
					if (UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, *MaterialPath))
					{
						MeshComp->SetMaterial(0, Mat);
					}
					else
					{
						AddBlueprintOpError(OutErrors, OpIndex, TEXT("failed_to_load_material0"));
					}
				}

				FString MobilityStr;
				if (OpObj->TryGetStringField(TEXT("mobility"), MobilityStr))
				{
					EComponentMobility::Type Mobility;
					if (TryParseMobilityString(MobilityStr, Mobility))
					{
						SceneComp->SetMobility(Mobility);
					}
					else
					{
						AddBlueprintOpError(OutErrors, OpIndex, TEXT("invalid_mobility"));
					}
				}

				if (const TSharedPtr<FJsonValue>* V = OpObj->Values.Find(TEXT("relative_location")))
				{
					FVector Parsed;
					if (TryParseVector3(*V, Parsed))
					{
						SceneComp->SetRelativeLocation(Parsed);
					}
					else
					{
						AddBlueprintOpError(OutErrors, OpIndex, TEXT("invalid_relative_location"));
					}
				}
				if (const TSharedPtr<FJsonValue>* V = OpObj->Values.Find(TEXT("relative_rotation")))
				{
					FVector Parsed;
					if (TryParseVector3(*V, Parsed))
					{
						SceneComp->SetRelativeRotation(FRotator(Parsed.X, Parsed.Y, Parsed.Z));
					}
					else
					{
						AddBlueprintOpError(OutErrors, OpIndex, TEXT("invalid_relative_rotation"));
					}
				}
				if (const TSharedPtr<FJsonValue>* V = OpObj->Values.Find(TEXT("relative_scale3d")))
				{
					FVector Parsed;
					if (TryParseVector3(*V, Parsed))
					{
						SceneComp->SetRelativeScale3D(Parsed);
					}
					else
					{
						AddBlueprintOpError(OutErrors, OpIndex, TEXT("invalid_relative_scale3d"));
					}
				}

				if (bHasAttachmentProperties)
				{
					if (AttachParentNode)
					{
						Node->SetParent(AttachParentNode);
					}
					else
					{
						Node->SetParent(AttachParentComp);
					}
					Node->AttachToName = AttachSocketName;
					SceneComp->SetupAttachment(AttachParentComp, AttachSocketName);
					bStructuralChange = true;
				}

				if (bIsISM)
				{
					double NumCustomDataFloats = 0.0;
					if (OpObj->TryGetNumberField(TEXT("num_custom_data_floats"), NumCustomDataFloats))
					{
						if (NumCustomDataFloats < 0.0)
						{
							AddBlueprintOpError(OutErrors, OpIndex, TEXT("invalid_num_custom_data_floats"));
						}
						else
						{
							ISMComp->NumCustomDataFloats = (int32)NumCustomDataFloats;
						}
					}
				}

				if (bIsSplineMesh)
				{
					ApplySplineMeshPatch(SplineMeshComp, SplineMeshPatch);
				}

				SceneComp->PostEditChange();
			}

			++ComponentsUpdated;
			continue;
		}

		if (OpName.Equals(TEXT("components.instances.add"), ESearchCase::CaseSensitive))
		{
			FString Name;
			OpObj->TryGetStringField(TEXT("name"), Name);
			if (Name.IsEmpty())
			{
				AddBlueprintOpError(OutErrors, OpIndex, TEXT("components.instances.add_requires_name"));
				continue;
			}

			USCS_Node* Node = FindSCSNodeByName(Blueprint, Name);
			UInstancedStaticMeshComponent* ISMComp = Node ? Cast<UInstancedStaticMeshComponent>(Node->ComponentTemplate) : nullptr;
			if (!ISMComp)
			{
				AddBlueprintOpError(OutErrors, OpIndex, TEXT("component_not_instanced_mesh"));
				continue;
			}

			TArray<FTransform> ParsedInstances;
			FString ParseError;
			if (!ParseInstancesArray(OpObj, ParsedInstances, ParseError))
			{
				AddBlueprintOpError(OutErrors, OpIndex, ParseError);
				continue;
			}

			if (ParsedInstances.Num() > MaxInstancesPerOp)
			{
				AddBlueprintOpError(OutErrors, OpIndex, FString::Printf(TEXT("max_instances_per_op_exceeded:%d"), MaxInstancesPerOp));
				continue;
			}

			if ((InOutTotalInstances + ParsedInstances.Num()) > MaxTotalInstancesPerPlan)
			{
				AddBlueprintOpError(OutErrors, OpIndex, FString::Printf(TEXT("max_total_instances_per_plan_exceeded:%d"), MaxTotalInstancesPerPlan));
				continue;
			}

			if (bApply)
			{
				ISMComp->Modify();
				for (const FTransform& Xf : ParsedInstances)
				{
					ISMComp->AddInstance(Xf);
				}
			}

			InOutTotalInstances += ParsedInstances.Num();
			InstancesAdded += ParsedInstances.Num();
			continue;
		}

		if (OpName.Equals(TEXT("components.list"), ESearchCase::CaseSensitive))
		{
			if (!Blueprint->SimpleConstructionScript)
			{
				AddBlueprintOpError(OutErrors, OpIndex, TEXT("no_simple_construction_script"));
				continue;
			}

			FString NamePrefix;
			FString ClassFilter;
			OpObj->TryGetStringField(TEXT("name_prefix"), NamePrefix);
			OpObj->TryGetStringField(TEXT("class"), ClassFilter);

			for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
			{
				if (!Node || !Node->ComponentTemplate)
				{
					continue;
				}

				const FString CompName = Node->GetVariableName().ToString();
				const FString ClassPath = Node->ComponentTemplate->GetClass()->GetPathName();

				if (!NamePrefix.IsEmpty() && !CompName.StartsWith(NamePrefix, ESearchCase::IgnoreCase))
				{
					continue;
				}
				if (!ClassFilter.IsEmpty() && !ClassPath.Equals(ClassFilter, ESearchCase::IgnoreCase)
					&& !Node->ComponentTemplate->GetClass()->GetName().Equals(ClassFilter, ESearchCase::IgnoreCase))
				{
					continue;
				}

				TSharedRef<FJsonObject> CompObj = MakeShared<FJsonObject>();
				CompObj->SetStringField(TEXT("name"), CompName);
				CompObj->SetStringField(TEXT("class"), ClassPath);
				ComponentsListed.Add(MakeShared<FJsonValueObject>(CompObj));
			}

			continue;
		}

		if (OpName.Equals(TEXT("graph.apply"), ESearchCase::CaseSensitive))
		{
			TSharedRef<FJsonObject> GraphApplyObj = MakeShared<FJsonObject>();
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : OpObj->Values)
			{
				if (Pair.Key.Equals(TEXT("op"), ESearchCase::CaseSensitive))
				{
					continue;
				}
				GraphApplyObj->SetField(Pair.Key, Pair.Value);
			}

			GraphApplyObj->SetStringField(TEXT("blueprint"), ObjectPath.IsEmpty() ? BlueprintPath : ObjectPath);

			FBlueprintGraphApplyRequest GraphRequest;
			TArray<FString> ParseErrors;
			if (!BAT::BlueprintGraphApplyRequest::Parse(GraphApplyObj, GraphRequest, ParseErrors))
			{
				for (const FString& ParseError : ParseErrors)
				{
					AddBlueprintOpError(OutErrors, OpIndex, FString::Printf(TEXT("graph_apply_parse:%s"), *ParseError));
				}
				continue;
			}

			if (!bApply)
			{
				GraphRequest.Options.bDryRun = true;
			}

			const FAutomationResult GraphResult = FBlueprintGraphService::ApplyGraphPatch(GraphRequest);
			if (!GraphResult.bSuccess || !GraphResult.Data.IsValid() || GraphResult.Data->Type != EJson::Object)
			{
				AddBlueprintOpError(OutErrors, OpIndex, TEXT("graph_apply_failed"));
				continue;
			}

			const TSharedPtr<FJsonObject> GraphRoot = GraphResult.Data->AsObject();
			bool bGraphOk = false;
			if (GraphRoot.IsValid())
			{
				GraphRoot->TryGetBoolField(TEXT("ok"), bGraphOk);
			}
			if (!bGraphOk)
			{
				const TArray<TSharedPtr<FJsonValue>>* GraphErrors = nullptr;
				if (GraphRoot.IsValid() && GraphRoot->TryGetArrayField(TEXT("errors"), GraphErrors) && GraphErrors)
				{
					for (const TSharedPtr<FJsonValue>& ErrValue : *GraphErrors)
					{
						if (!ErrValue.IsValid() || ErrValue->Type != EJson::Object)
						{
							AddBlueprintOpError(OutErrors, OpIndex, TEXT("graph_apply_error"));
							continue;
						}

						const TSharedPtr<FJsonObject> ErrObj = ErrValue->AsObject();
						const FString Message = ErrObj.IsValid() && ErrObj->HasField(TEXT("message"))
							? ErrObj->GetStringField(TEXT("message"))
							: TEXT("graph_apply_error");
						AddBlueprintOpError(OutErrors, OpIndex, FString::Printf(TEXT("graph_apply:%s"), *Message));
					}
				}
				else
				{
					AddBlueprintOpError(OutErrors, OpIndex, TEXT("graph_apply_failed"));
				}
				continue;
			}

			if (bApply)
			{
				bStructuralChange = true;
			}
			continue;
		}

		if (OpName.Equals(TEXT("compile"), ESearchCase::CaseSensitive))
		{
			bCompileRequested = true;
			continue;
		}

		AddBlueprintOpError(OutErrors, OpIndex, TEXT("unsupported_blueprint_op"));
	}

	if (bApply && Blueprint)
	{
		Blueprint->Modify();
		if (UEdGraph* ConstructionGraph = FBlueprintEditorUtils::FindUserConstructionScript(Blueprint))
		{
			ConstructionGraph->Modify();
		}
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		if (bStructuralChange)
		{
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		}
		if (bCompileRequested)
		{
			FKismetEditorUtilities::CompileBlueprint(Blueprint);
		}
		if (Blueprint->GetOutermost())
		{
			Blueprint->GetOutermost()->MarkPackageDirty();
		}
	}

	OutResult->SetBoolField(TEXT("ok"), OutErrors.Num() == 0);
	OutResult->SetStringField(TEXT("blueprint"), ObjectPath);
	OutResult->SetNumberField(TEXT("created"), CreatedBlueprints);
	OutResult->SetNumberField(TEXT("variables_added"), VariablesAdded);
	OutResult->SetArrayField(TEXT("variables_list"), VariablesListed);
	OutResult->SetNumberField(TEXT("variables_listed"), VariablesListedCount);
	OutResult->SetNumberField(TEXT("functions_added"), FunctionsAdded);
	OutResult->SetArrayField(TEXT("functions_list"), FunctionsListed);
	OutResult->SetNumberField(TEXT("functions_listed"), FunctionsListedCount);
	OutResult->SetNumberField(TEXT("components_added"), ComponentsAdded);
	OutResult->SetNumberField(TEXT("components_removed"), ComponentsRemoved);
	OutResult->SetNumberField(TEXT("components_updated"), ComponentsUpdated);
	OutResult->SetNumberField(TEXT("instances_added"), InstancesAdded);
	OutResult->SetArrayField(TEXT("components_list"), ComponentsListed);
	OutResult->SetNumberField(TEXT("components_listed"), ComponentsListed.Num());
	OutResult->SetBoolField(TEXT("compiled"), bCompileRequested);
	return OutErrors.Num() == 0;
}

void FBlueprintAutomationToolkitModule::BindBlueprintRoutes()
{
BindBlueprintAssetsRoutes();
BindBlueprintComponentsRoutes();
BindBlueprintGraphRoutes();
}

void FBlueprintAutomationToolkitModule::BindBlueprintAssetsRoutesInternal()
{
BlueprintCreateRoute = Router->BindRoute(
		FHttpPath(TEXT("/blueprint/create")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, TEXT("/blueprint/create")))
			{
				return true;
			}

			TSharedPtr<FJsonObject> BodyObj;
			if (!TryParseJsonBody(Request.Body, BodyObj))
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("bad_json"), TEXT("Invalid JSON body")));
				return true;
			}

			FString PackagePath;
			FString Name;
			FString ParentClassPath;
			BodyObj->TryGetStringField(TEXT("path"), PackagePath);
			BodyObj->TryGetStringField(TEXT("name"), Name);
			BodyObj->TryGetStringField(TEXT("parent"), ParentClassPath);
			bool bCompile = false;
			BodyObj->TryGetBoolField(TEXT("compile"), bCompile);

			if (PackagePath.IsEmpty() || Name.IsEmpty())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("missing_fields"), TEXT("Body must include 'path' and 'name'")));
				return true;
			}

			TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
			Payload->SetStringField(TEXT("path"), PackagePath);
			Payload->SetStringField(TEXT("name"), Name);
			if (!ParentClassPath.IsEmpty())
			{
				Payload->SetStringField(TEXT("parent"), ParentClassPath);
			}
			Payload->SetBoolField(TEXT("compile"), bCompile);

			const FString RequestId = ResolveOrCreateRequestId(Request);
			const FString IdempotencyKey = ReadHeaderValueCaseInsensitive(Request.Headers, TEXT("Idempotency-Key"));
			const FString ScopedKey = IdempotencyKey.IsEmpty() ? FString() : FString::Printf(TEXT("/blueprint/create:%s"), *IdempotencyKey);
			if (!ScopedKey.IsEmpty())
			{
				FScopeLock Lock(&JobMutex);
				if (const FString* ExistingJobId = IdempotencyToJob.Find(ScopedKey))
				{
					TSharedRef<FJsonObject> Cached = MakeShared<FJsonObject>();
					Cached->SetStringField(TEXT("jobId"), *ExistingJobId);
					Cached->SetStringField(TEXT("requestId"), RequestId);
					Cached->SetBoolField(TEXT("idempotent_replay"), true);
					OnComplete(BAT::Http::MakeJsonResponseFromString(static_cast<int32>(EHttpServerResponseCodes::Accepted), ToJsonString(Cached), RequestId));
					return true;
				}
			}

			const FString JobId = SubmitJob(TEXT("blueprint.create"), RequestId, Payload);
			if (!ScopedKey.IsEmpty())
			{
				FScopeLock Lock(&JobMutex);
				IdempotencyToJob.Add(ScopedKey, JobId);
			}

			TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("jobId"), JobId);
			Obj->SetStringField(TEXT("requestId"), RequestId);
			OnComplete(BAT::Http::MakeJsonResponseFromString(static_cast<int32>(EHttpServerResponseCodes::Accepted), ToJsonString(Obj), RequestId));

			return true;
		}));

	BlueprintApplyRoute = Router->BindRoute(
		FHttpPath(TEXT("/blueprint/apply")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, TEXT("/blueprint/apply")))
			{
				return true;
			}

			TSharedPtr<FJsonObject> BodyObj;
			if (!TryParseJsonBody(Request.Body, BodyObj) || !BodyObj.IsValid())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("bad_json"), TEXT("Invalid JSON body")));
				return true;
			}

			const FString RequestId = ResolveOrCreateRequestId(Request);

			FBlueprintService Service([this, BodyObj](FAutomationContext& Context) -> FAutomationResult
			{
				TOptional<FAutomationResult> ThreadResult;
				const bool bCompleted = RunOnGameThreadWait([this, BodyObj, &ThreadResult]()
				{
					int32 TotalInstances = 0;
					TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
					TArray<TSharedPtr<FJsonValue>> Errors;
					ExecuteBlueprintPatch(BodyObj, true, TotalInstances, Result, Errors);
					Result->SetArrayField(TEXT("errors"), Errors);
					ThreadResult = FAutomationResult::Ok(MakeShared<FJsonValueObject>(Result));
				}, 10.0);

				if (!bCompleted)
				{
					return FAutomationResult::Error(TEXT("game_thread_timeout"), TEXT("Timed out waiting for GameThread execution"), 504);
				}

				if (!ThreadResult.IsSet())
				{
					return FAutomationResult::Error(TEXT("internal_error"), TEXT("No result produced by blueprint apply operation"), 500);
				}

				return ThreadResult.GetValue();
			});

			FBlueprintGraphEditCommand Command(Service);
			FCommandDispatcher Dispatcher;
			FAutomationContext Context;
			Context.RequestId = RequestId;
			Context.Body = BodyObj;

			const FAutomationResult Result = Dispatcher.Dispatch(Command, Context);
			if (!Result.bSuccess)
			{
				OnComplete(MakeErrorResponse(Result.StatusCode, RequestId, Result.ErrorCode, Result.ErrorMessage));
				return true;
			}

			BAT::Http::JsonOk(OnComplete, Result.Data, Result.StatusCode, RequestId);

			return true;
		}));

	BlueprintSetDefaultsRoute = Router->BindRoute(
		FHttpPath(TEXT("/blueprint/set-defaults")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, TEXT("/blueprint/set-defaults")))
			{
				return true;
			}

			TSharedPtr<FJsonObject> BodyObj;
			if (!TryParseJsonBody(Request.Body, BodyObj))
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("bad_json"), TEXT("Invalid JSON body")));
				return true;
			}

			FString BlueprintPath;
			if (!BodyObj->TryGetStringField(TEXT("blueprint"), BlueprintPath) || BlueprintPath.IsEmpty())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("missing_blueprint"), TEXT("Body must include 'blueprint'")));
				return true;
			}

			const TSharedPtr<FJsonObject>* DefaultsObjPtr = nullptr;
			if (!BodyObj->TryGetObjectField(TEXT("defaults"), DefaultsObjPtr) || DefaultsObjPtr == nullptr || !DefaultsObjPtr->IsValid())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("missing_defaults"), TEXT("Body must include 'defaults' object")));
				return true;
			}
			const TSharedPtr<FJsonObject> DefaultsObj = *DefaultsObjPtr;
			const TArray<TSharedPtr<FJsonValue>>* ComponentsApplyArr = nullptr;
			BodyObj->TryGetArrayField(TEXT("components_apply"), ComponentsApplyArr);
			const TArray<TSharedPtr<FJsonValue>> ComponentsApply = ComponentsApplyArr ? *ComponentsApplyArr : TArray<TSharedPtr<FJsonValue>>();
			bool bCompile = false;
			BodyObj->TryGetBoolField(TEXT("compile"), bCompile);

			const FString RequestId = ResolveOrCreateRequestId(Request);
			TSharedRef<TAtomic<bool>> bResponded = MakeShared<TAtomic<bool>>(false);
			FHttpResultCallback Complete = [OnComplete, bResponded](TUniquePtr<FHttpServerResponse> Response) mutable
			{
				if (!bResponded->Exchange(true))
				{
					OnComplete(MoveTemp(Response));
				}
			};

			const bool bCompleted = RunOnGameThreadWait([this, BlueprintPath, DefaultsObj, ComponentsApply, bCompile, Complete, RequestId]()
			{
				UBlueprint* Blueprint = nullptr;
				FString ObjectPath;
				if (!TryLoadBlueprint(BlueprintPath, Blueprint, ObjectPath))
				{
					Complete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("not_found"), TEXT("Blueprint not found")));
					return;
				}

				if (!Blueprint->GeneratedClass)
				{
					FKismetEditorUtilities::CompileBlueprint(Blueprint);
				}
				UClass* GenClass = Blueprint->GeneratedClass;
				if (!GenClass)
				{
					Complete(MakeErrorResponse(EHttpServerResponseCodes::ServerError, TEXT("compile_failed"), TEXT("Blueprint has no GeneratedClass")));
					return;
				}

				UObject* CDO = GenClass->GetDefaultObject();
				if (!CDO)
				{
					Complete(MakeErrorResponse(EHttpServerResponseCodes::ServerError, TEXT("no_cdo"), TEXT("Failed to get CDO")));
					return;
				}

				const FScopedTransaction Tx(FText::FromString(TEXT("BAT: Set Blueprint Defaults")));
				Blueprint->Modify();
				CDO->Modify();

				TArray<TSharedPtr<FJsonValue>> SetProps;
				TArray<TSharedPtr<FJsonValue>> Errors;
				TArray<TSharedPtr<FJsonValue>> ComponentsApplied;
				TArray<TSharedPtr<FJsonValue>> ComponentErrors;

				for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : DefaultsObj->Values)
				{
					const FString& PropName = Pair.Key;
					FProperty* Prop = CDO->GetClass()->FindPropertyByName(FName(*PropName));
					if (!Prop)
					{
						TSharedRef<FJsonObject> Err = MakeShared<FJsonObject>();
						Err->SetStringField(TEXT("property"), PropName);
						Err->SetStringField(TEXT("error"), TEXT("Property not found"));
						Errors.Add(MakeShared<FJsonValueObject>(Err));
						continue;
					}

					FString ErrMsg;
					if (!SetObjectPropertyFromJson(CDO, Prop, Pair.Value, ErrMsg))
					{
						TSharedRef<FJsonObject> Err = MakeShared<FJsonObject>();
						Err->SetStringField(TEXT("property"), PropName);
						Err->SetStringField(TEXT("error"), ErrMsg);
						Errors.Add(MakeShared<FJsonValueObject>(Err));
						continue;
					}

					SetProps.Add(MakeShared<FJsonValueString>(PropName));
				}

				for (const TSharedPtr<FJsonValue>& EntryValue : ComponentsApply)
				{
					if (!EntryValue.IsValid() || EntryValue->Type != EJson::Object)
					{
						TSharedRef<FJsonObject> Err = MakeShared<FJsonObject>();
						Err->SetStringField(TEXT("error"), TEXT("components_apply entry must be an object"));
						ComponentErrors.Add(MakeShared<FJsonValueObject>(Err));
						continue;
					}

					const TSharedPtr<FJsonObject> EntryObj = EntryValue->AsObject();
					FString ClassPath;
					FString CompName;
					EntryObj->TryGetStringField(TEXT("class"), ClassPath);
					EntryObj->TryGetStringField(TEXT("name"), CompName);
					if (ClassPath.IsEmpty() || CompName.IsEmpty())
					{
						TSharedRef<FJsonObject> Err = MakeShared<FJsonObject>();
						Err->SetStringField(TEXT("error"), TEXT("components_apply entry requires 'class' and 'name'"));
						ComponentErrors.Add(MakeShared<FJsonValueObject>(Err));
						continue;
					}

					const EBATComponentApplyKind Kind = ResolveComponentApplyKind(ClassPath);
					if (Kind == EBATComponentApplyKind::Unsupported)
					{
						TSharedRef<FJsonObject> Err = MakeShared<FJsonObject>();
						Err->SetStringField(TEXT("name"), CompName);
						Err->SetStringField(TEXT("error"), TEXT("component class not allowlisted"));
						ComponentErrors.Add(MakeShared<FJsonValueObject>(Err));
						continue;
					}

					if (Kind == EBATComponentApplyKind::Spline)
					{
						USplineComponent* SplineTemplate = FindOrCreateSplineTemplate(Blueprint, CompName);
						if (!SplineTemplate)
						{
							TSharedRef<FJsonObject> Err = MakeShared<FJsonObject>();
							Err->SetStringField(TEXT("name"), CompName);
							Err->SetStringField(TEXT("error"), TEXT("failed to create/find spline component template"));
							ComponentErrors.Add(MakeShared<FJsonValueObject>(Err));
							continue;
						}

						SplineTemplate->Modify();
						SplineTemplate->PreEditChange(nullptr);
						SplineTemplate->ClearSplinePoints(false);

						TArray<FVector> Points;
						if (!TryParseSplinePoints(EntryObj, Points))
						{
							TSharedRef<FJsonObject> Err = MakeShared<FJsonObject>();
							Err->SetStringField(TEXT("name"), CompName);
							Err->SetStringField(TEXT("error"), TEXT("spline entry requires non-empty 'points' array of vectors"));
							ComponentErrors.Add(MakeShared<FJsonValueObject>(Err));
							continue;
						}

						FString PointTypeString;
						EntryObj->TryGetStringField(TEXT("point_type"), PointTypeString);
						const ESplinePointType::Type PointType = ResolveSplinePointType(PointTypeString);

						for (const FVector& LocalPoint : Points)
						{
							SplineTemplate->AddSplinePoint(LocalPoint, ESplineCoordinateSpace::Local, false);
						}
						for (int32 PointIdx = 0; PointIdx < Points.Num(); ++PointIdx)
						{
							SplineTemplate->SetSplinePointType(PointIdx, PointType, false);
						}

						bool bClosedLoop = false;
						EntryObj->TryGetBoolField(TEXT("closed_loop"), bClosedLoop);
						SplineTemplate->SetClosedLoop(bClosedLoop, false);
						SplineTemplate->UpdateSpline();
						SplineTemplate->PostEditChange();

						ComponentsApplied.Add(MakeShared<FJsonValueString>(CompName));
						continue;
					}

					if (Kind == EBATComponentApplyKind::SplineMesh)
					{
						USplineMeshComponent* ExistingTemplate = Cast<USplineMeshComponent>(
							FindSceneTemplateByName(Blueprint, CompName, USplineMeshComponent::StaticClass()));
						FBATSplineMeshPatch SplineMeshPatch;
						FString SplineMeshError;
						if (!TryBuildSplineMeshPatch(Blueprint, ExistingTemplate, EntryObj, true, SplineMeshPatch, SplineMeshError))
						{
							TSharedRef<FJsonObject> Err = MakeShared<FJsonObject>();
							Err->SetStringField(TEXT("name"), CompName);
							Err->SetStringField(TEXT("error"), SplineMeshError);
							ComponentErrors.Add(MakeShared<FJsonValueObject>(Err));
							continue;
						}

						USplineMeshComponent* SplineMeshTemplate = ExistingTemplate
							? ExistingTemplate
							: Cast<USplineMeshComponent>(FindOrCreateSceneTemplate(Blueprint, CompName, USplineMeshComponent::StaticClass()));
						if (!SplineMeshTemplate)
						{
							TSharedRef<FJsonObject> Err = MakeShared<FJsonObject>();
							Err->SetStringField(TEXT("name"), CompName);
							Err->SetStringField(TEXT("error"), TEXT("failed to create/find spline mesh component template"));
							ComponentErrors.Add(MakeShared<FJsonValueObject>(Err));
							continue;
						}

						SplineMeshTemplate->Modify();
						SplineMeshTemplate->PreEditChange(nullptr);
						int32 AppliedProperties = 0;
						int32 RejectedProperties = 0;
						bool bAssetsOk = true;
						ApplyAllowedMeshAssets(
							SplineMeshTemplate,
							EntryObj,
							AppliedProperties,
							RejectedProperties,
							bAssetsOk);
						if (!bAssetsOk)
						{
							SplineMeshTemplate->PostEditChange();
							TSharedRef<FJsonObject> Err = MakeShared<FJsonObject>();
							Err->SetStringField(TEXT("name"), CompName);
							Err->SetStringField(TEXT("error"), TEXT("failed to load spline mesh asset or material"));
							ComponentErrors.Add(MakeShared<FJsonValueObject>(Err));
							continue;
						}

						const TSharedPtr<FJsonObject>* PropertiesObjPtr = nullptr;
						if (EntryObj->TryGetObjectField(TEXT("properties"), PropertiesObjPtr)
							&& PropertiesObjPtr && PropertiesObjPtr->IsValid())
						{
							ApplyAllowedSceneTransformProperties(
								SplineMeshTemplate,
								*PropertiesObjPtr,
								AppliedProperties,
								RejectedProperties);
						}
						if (RejectedProperties > 0)
						{
							SplineMeshTemplate->PostEditChange();
							TSharedRef<FJsonObject> Err = MakeShared<FJsonObject>();
							Err->SetStringField(TEXT("name"), CompName);
							Err->SetStringField(TEXT("error"), TEXT("one or more spline mesh properties were rejected"));
							ComponentErrors.Add(MakeShared<FJsonValueObject>(Err));
							continue;
						}

						ApplySplineMeshPatch(SplineMeshTemplate, SplineMeshPatch);
						SplineMeshTemplate->PostEditChange();
						ComponentsApplied.Add(MakeShared<FJsonValueString>(CompName));
						continue;
					}

					UInstancedStaticMeshComponent* CompTemplate = FindOrCreateInstancedTemplate(Blueprint, CompName, Kind);
					if (!CompTemplate)
					{
						TSharedRef<FJsonObject> Err = MakeShared<FJsonObject>();
						Err->SetStringField(TEXT("name"), CompName);
						Err->SetStringField(TEXT("error"), TEXT("failed to create/find component template"));
						ComponentErrors.Add(MakeShared<FJsonValueObject>(Err));
						continue;
					}

					CompTemplate->Modify();
					CompTemplate->PreEditChange(nullptr);
					CompTemplate->ClearInstances();

					FString StaticMeshPath;
					if (EntryObj->TryGetStringField(TEXT("static_mesh"), StaticMeshPath) && !StaticMeshPath.IsEmpty())
					{
						if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *StaticMeshPath))
						{
							CompTemplate->SetStaticMesh(Mesh);
						}
						else
						{
							TSharedRef<FJsonObject> Err = MakeShared<FJsonObject>();
							Err->SetStringField(TEXT("name"), CompName);
							Err->SetStringField(TEXT("error"), TEXT("failed to load static_mesh"));
							ComponentErrors.Add(MakeShared<FJsonValueObject>(Err));
							continue;
						}
					}

					FString MaterialPath;
					if (EntryObj->TryGetStringField(TEXT("material0"), MaterialPath) && !MaterialPath.IsEmpty())
					{
						if (UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, *MaterialPath))
						{
							CompTemplate->SetMaterial(0, Mat);
						}
						else
						{
							TSharedRef<FJsonObject> Err = MakeShared<FJsonObject>();
							Err->SetStringField(TEXT("name"), CompName);
							Err->SetStringField(TEXT("error"), TEXT("failed to load material0"));
							ComponentErrors.Add(MakeShared<FJsonValueObject>(Err));
							continue;
						}
					}

					const TArray<TSharedPtr<FJsonValue>>* InstancesArr = nullptr;
					TArray<FTransform> ParsedInstances;
					if (EntryObj->TryGetArrayField(TEXT("instances"), InstancesArr) && InstancesArr)
					{
						for (const TSharedPtr<FJsonValue>& InstanceVal : *InstancesArr)
						{
							FTransform Xf;
							if (TryParseTransformFromJsonValue(InstanceVal, Xf))
							{
								ParsedInstances.Add(Xf);
							}
							else
							{
								TSharedRef<FJsonObject> Err = MakeShared<FJsonObject>();
								Err->SetStringField(TEXT("name"), CompName);
								Err->SetStringField(TEXT("error"), TEXT("invalid instance transform object"));
								ComponentErrors.Add(MakeShared<FJsonValueObject>(Err));
							}
						}
					}

					FString FromSplineError;
					if (!TryAppendInstancesFromSpline(Blueprint, EntryObj, ParsedInstances, FromSplineError))
					{
						TSharedRef<FJsonObject> Err = MakeShared<FJsonObject>();
						Err->SetStringField(TEXT("name"), CompName);
						Err->SetStringField(TEXT("error"), FromSplineError);
						ComponentErrors.Add(MakeShared<FJsonValueObject>(Err));
						continue;
					}

					for (const FTransform& Xf : ParsedInstances)
					{
						CompTemplate->AddInstance(Xf);
					}

					CompTemplate->PostEditChange();

					ComponentsApplied.Add(MakeShared<FJsonValueString>(CompName));
				}

				FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
				if (ComponentsApplied.Num() > 0)
				{
					FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
				}
				if (bCompile)
				{
					FKismetEditorUtilities::CompileBlueprint(Blueprint);
				}
				Blueprint->GetOutermost()->MarkPackageDirty();

				TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
				Obj->SetBoolField(TEXT("ok"), Errors.Num() == 0 && ComponentErrors.Num() == 0);
				Obj->SetStringField(TEXT("blueprint"), ObjectPath);
				Obj->SetArrayField(TEXT("set"), SetProps);
				Obj->SetArrayField(TEXT("errors"), Errors);
				Obj->SetArrayField(TEXT("components_applied"), ComponentsApplied);
				Obj->SetArrayField(TEXT("component_errors"), ComponentErrors);
				Complete(BAT::Http::MakeJsonResponseFromString(static_cast<int32>(EHttpServerResponseCodes::Ok), ToJsonString(Obj), RequestId));
			}, 10.0);

			if (!bCompleted && !bResponded->Exchange(true))
			{
				OnComplete(MakeErrorResponse(504, RequestId, TEXT("game_thread_timeout"), TEXT("Timed out waiting for GameThread execution")));
			}

			return true;
		}));
}

void FBlueprintAutomationToolkitModule::BindBlueprintGraphRoutesInternal()
{
	BlueprintSchemaRoute = Router->BindRoute(
		FHttpPath(TEXT("/blueprint/schema")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, TEXT("/blueprint/schema")))
			{
				return true;
			}

			TSharedPtr<FJsonObject> BodyObj;
			if (!TryParseJsonBody(Request.Body, BodyObj) || !BodyObj.IsValid())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("bad_json"), TEXT("Invalid JSON body")));
				return true;
			}

			FString BlueprintPath;
			FString GraphName;
			if (!BodyObj->TryGetStringField(TEXT("blueprint"), BlueprintPath) || BlueprintPath.TrimStartAndEnd().IsEmpty())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("missing_blueprint"), TEXT("Body must include 'blueprint'")));
				return true;
			}
			BodyObj->TryGetStringField(TEXT("graph"), GraphName);

			TSet<FString> RequestedSections;
			if (const TArray<TSharedPtr<FJsonValue>>* IncludeArray = nullptr; BodyObj->TryGetArrayField(TEXT("include"), IncludeArray) && IncludeArray)
			{
				for (const TSharedPtr<FJsonValue>& SectionValue : *IncludeArray)
				{
					if (SectionValue.IsValid() && SectionValue->Type == EJson::String)
					{
						FString SectionName = SectionValue->AsString();
						SectionName.TrimStartAndEndInline();
						if (!SectionName.IsEmpty())
						{
							RequestedSections.Add(SectionName);
						}
					}
				}
			}

			FString FunctionSearch;
			FString FunctionClassPath;
			bool bBlueprintCallableOnly = true;
			bool bExcludeLatent = true;
			bool bExcludeUnsafe = true;
			int32 MaxFunctions = 100;
			if (const TSharedPtr<FJsonObject>* FunctionFilterPtr = nullptr; BodyObj->TryGetObjectField(TEXT("function_filter"), FunctionFilterPtr) && FunctionFilterPtr && FunctionFilterPtr->IsValid())
			{
				const TSharedPtr<FJsonObject> FunctionFilter = *FunctionFilterPtr;
				FunctionFilter->TryGetStringField(TEXT("search"), FunctionSearch);
				FunctionFilter->TryGetStringField(TEXT("class"), FunctionClassPath);
				FunctionFilter->TryGetBoolField(TEXT("blueprint_callable_only"), bBlueprintCallableOnly);
				FunctionFilter->TryGetBoolField(TEXT("exclude_latent"), bExcludeLatent);
				FunctionFilter->TryGetBoolField(TEXT("exclude_unsafe"), bExcludeUnsafe);
				FunctionFilter->TryGetNumberField(TEXT("limit"), MaxFunctions);
				MaxFunctions = FMath::Clamp(MaxFunctions, 1, 500);
			}

			const FString RequestId = ResolveOrCreateRequestId(Request);
			TSharedPtr<FJsonObject> Root;
			int32 StatusCode = 200;
			FString ErrorCode;
			FString ErrorMessage;

			const bool bCompleted = RunOnGameThreadWait([&]()
			{
				UBlueprint* Blueprint = nullptr;
				FString ObjectPath;
				if (!TryLoadBlueprint(BlueprintPath, Blueprint, ObjectPath))
				{
					StatusCode = (int32)EHttpServerResponseCodes::BadRequest;
					ErrorCode = TEXT("not_found");
					ErrorMessage = TEXT("Blueprint not found");
					return;
				}

				TArray<TSharedPtr<FJsonValue>> Errors;
				TArray<TSharedPtr<FJsonValue>> Warnings;
				TSharedRef<FJsonObject> DataObj = MakeShared<FJsonObject>();
				DataObj->SetStringField(TEXT("blueprint"), ObjectPath);

				if (ShouldIncludeSchemaSection(RequestedSections, TEXT("graphs")))
				{
					TArray<UEdGraph*> Graphs;
					GetAllBlueprintGraphs(Blueprint, Graphs);
					TArray<TSharedPtr<FJsonValue>> GraphArr;
					for (UEdGraph* Graph : Graphs)
					{
						if (!Graph)
						{
							continue;
						}

						TSharedRef<FJsonObject> GraphObj = MakeShared<FJsonObject>();
						GraphObj->SetStringField(TEXT("name"), Graph->GetName());
						GraphObj->SetStringField(TEXT("kind"), GetBlueprintGraphKind(Blueprint, Graph));
						GraphObj->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());
						GraphArr.Add(MakeShared<FJsonValueObject>(GraphObj));
					}
					DataObj->SetArrayField(TEXT("graphs"), GraphArr);
				}

				if (ShouldIncludeSchemaSection(RequestedSections, TEXT("components")))
				{
					TArray<TSharedPtr<FJsonValue>> Components;
					if (Blueprint->SimpleConstructionScript)
					{
						for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
						{
							if (!Node || !Node->ComponentTemplate)
							{
								continue;
							}

							TArray<TSharedPtr<FJsonValue>> EditableProperties;
							AppendEditablePropertiesForComponent(Node->ComponentTemplate, EditableProperties);

							TSharedRef<FJsonObject> ComponentObj = MakeShared<FJsonObject>();
							ComponentObj->SetStringField(TEXT("name"), Node->GetVariableName().ToString());
							ComponentObj->SetStringField(TEXT("class"), Node->ComponentTemplate->GetClass()->GetPathName());
							ComponentObj->SetArrayField(TEXT("editable_properties"), EditableProperties);
							Components.Add(MakeShared<FJsonValueObject>(ComponentObj));
						}
					}
					DataObj->SetArrayField(TEXT("components"), Components);
				}

				if (ShouldIncludeSchemaSection(RequestedSections, TEXT("variables")))
				{
					TArray<TSharedPtr<FJsonValue>> Variables;
					for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
					{
						Variables.Add(MakeBlueprintVariableJson(Variable));
					}
					DataObj->SetArrayField(TEXT("variables"), Variables);
				}

				if (ShouldIncludeSchemaSection(RequestedSections, TEXT("supported_node_types")))
				{
					DataObj->SetArrayField(TEXT("supported_node_types"), MakeSupportedNodeTypesJson());
				}

				if (ShouldIncludeSchemaSection(RequestedSections, TEXT("graph_snapshot")))
				{
					UEdGraph* Graph = FindGraphByName(Blueprint, GraphName);
					if (!Graph)
					{
						StatusCode = (int32)EHttpServerResponseCodes::BadRequest;
						ErrorCode = TEXT("graph_not_found");
						ErrorMessage = TEXT("Graph not found");
						return;
					}

					TSharedRef<FJsonObject> GraphObj = MakeShared<FJsonObject>();
					BuildGraphSnapshotJson(Blueprint, Graph, GraphObj);
					DataObj->SetObjectField(TEXT("graph_snapshot"), GraphObj);
				}

				if (ShouldIncludeSchemaSection(RequestedSections, TEXT("functions")))
				{
					TArray<TSharedPtr<FJsonValue>> Functions;
					TSet<FString> SeenPaths;
					TArray<UClass*> SourceClasses;

					if (!FunctionClassPath.IsEmpty())
					{
						UClass* SourceClass = nullptr;
						if (!TryResolveClassPath(FunctionClassPath, SourceClass))
						{
							StatusCode = (int32)EHttpServerResponseCodes::BadRequest;
							ErrorCode = TEXT("function_class_not_found");
							ErrorMessage = TEXT("Function filter class not found");
							return;
						}
						SourceClasses.Add(SourceClass);
					}
					else
					{
						if (Blueprint->GeneratedClass)
						{
							SourceClasses.Add(Blueprint->GeneratedClass);
						}
						if (Blueprint->ParentClass && !SourceClasses.Contains(Blueprint->ParentClass))
						{
							SourceClasses.Add(Blueprint->ParentClass);
						}

						TSharedRef<FJsonObject> WarnObj = MakeShared<FJsonObject>();
						WarnObj->SetStringField(TEXT("code"), TEXT("functions_limited_scope"));
						WarnObj->SetStringField(TEXT("message"), TEXT("Functions are limited to the Blueprint generated class and parent class unless function_filter.class is provided"));
						Warnings.Add(MakeShared<FJsonValueObject>(WarnObj));
					}

					for (UClass* SourceClass : SourceClasses)
					{
						CollectCallableFunctionsForClass(SourceClass, FunctionSearch, bBlueprintCallableOnly, bExcludeLatent, bExcludeUnsafe, MaxFunctions, SeenPaths, Functions);
						if (Functions.Num() >= MaxFunctions)
						{
							break;
						}
					}

					DataObj->SetArrayField(TEXT("functions"), Functions);
				}

				Root = MakeShared<FJsonObject>();
				Root->SetBoolField(TEXT("ok"), true);
				Root->SetArrayField(TEXT("errors"), Errors);
				Root->SetArrayField(TEXT("warnings"), Warnings);
				Root->SetObjectField(TEXT("data"), DataObj);
			}, 10.0);

			if (!bCompleted)
			{
				OnComplete(MakeErrorResponse(504, RequestId, TEXT("game_thread_timeout"), TEXT("Timed out waiting for GameThread execution")));
				return true;
			}

			if (StatusCode != 200)
			{
				OnComplete(MakeErrorResponse(StatusCode, RequestId, ErrorCode, ErrorMessage));
				return true;
			}

			if (!Root.IsValid())
			{
				OnComplete(MakeErrorResponse(500, RequestId, TEXT("internal_error"), TEXT("No schema result produced")));
				return true;
			}

			OnComplete(BAT::Http::MakeJsonResponse(200, Root.ToSharedRef(), RequestId));
			return true;
		}));

BlueprintGraphsRoute = Router->BindRoute(
		FHttpPath(TEXT("/blueprint/graphs")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, TEXT("/blueprint/graphs")))
			{
				return true;
			}

			TSharedPtr<FJsonObject> BodyObj;
			if (!TryParseJsonBody(Request.Body, BodyObj))
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("bad_json"), TEXT("Invalid JSON body")));
				return true;
			}

			FString BlueprintPath;
			if (!BodyObj->TryGetStringField(TEXT("blueprint"), BlueprintPath) || BlueprintPath.IsEmpty())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("missing_blueprint"), TEXT("Body must include 'blueprint'")));
				return true;
			}

			const FString RequestId = ResolveOrCreateRequestId(Request);

			FBlueprintService Service([this, BlueprintPath](FAutomationContext& Context) -> FAutomationResult
			{
				TOptional<FAutomationResult> ThreadResult;
				const bool bCompleted = RunOnGameThreadWait([this, BlueprintPath, &ThreadResult]()
				{
					UBlueprint* Blueprint = nullptr;
					FString ObjectPath;
					if (!TryLoadBlueprint(BlueprintPath, Blueprint, ObjectPath))
					{
						ThreadResult = FAutomationResult::Error(TEXT("not_found"), TEXT("Blueprint not found"), (int32)EHttpServerResponseCodes::BadRequest);
						return;
					}

					TArray<UEdGraph*> Graphs;
					GetAllBlueprintGraphs(Blueprint, Graphs);
					TArray<TSharedPtr<FJsonValue>> GraphArr;
					for (UEdGraph* Graph : Graphs)
					{
						if (!Graph)
						{
							continue;
						}
						TSharedRef<FJsonObject> G = MakeShared<FJsonObject>();
						G->SetStringField(TEXT("name"), Graph->GetName());
						G->SetNumberField(TEXT("nodes"), Graph->Nodes.Num());
						GraphArr.Add(MakeShared<FJsonValueObject>(G));
					}

					TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
					Obj->SetBoolField(TEXT("ok"), true);
					Obj->SetStringField(TEXT("blueprint"), ObjectPath);
					Obj->SetArrayField(TEXT("graphs"), GraphArr);
					ThreadResult = FAutomationResult::Ok(MakeShared<FJsonValueObject>(Obj));
				}, 10.0);

				if (!bCompleted)
				{
					return FAutomationResult::Error(TEXT("game_thread_timeout"), TEXT("Timed out waiting for GameThread execution"), 504);
				}

				if (!ThreadResult.IsSet())
				{
					return FAutomationResult::Error(TEXT("internal_error"), TEXT("No result produced by blueprint graphs operation"), 500);
				}

				return ThreadResult.GetValue();
			});

			FBlueprintGraphEditCommand Command(Service);
			FCommandDispatcher Dispatcher;
			FAutomationContext ExecContext;
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

	BlueprintGraphNodesRoute = Router->BindRoute(
		FHttpPath(TEXT("/blueprint/graph/nodes")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, TEXT("/blueprint/graph/nodes")))
			{
				return true;
			}

			TSharedPtr<FJsonObject> BodyObj;
			if (!TryParseJsonBody(Request.Body, BodyObj))
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("bad_json"), TEXT("Invalid JSON body")));
				return true;
			}

			FString BlueprintPath;
			FString GraphName;
			if (!BodyObj->TryGetStringField(TEXT("blueprint"), BlueprintPath) || BlueprintPath.IsEmpty())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("missing_blueprint"), TEXT("Body must include 'blueprint'")));
				return true;
			}
			if (!BodyObj->TryGetStringField(TEXT("graph"), GraphName) || GraphName.IsEmpty())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("missing_graph"), TEXT("Body must include 'graph'")));
				return true;
			}

			const FString RequestId = ResolveOrCreateRequestId(Request);

			FBlueprintService Service([this, BlueprintPath, GraphName](FAutomationContext& Context) -> FAutomationResult
			{
				TOptional<FAutomationResult> ThreadResult;
				const bool bCompleted = RunOnGameThreadWait([this, BlueprintPath, GraphName, &ThreadResult]()
				{
					UBlueprint* Blueprint = nullptr;
					FString ObjectPath;
					if (!TryLoadBlueprint(BlueprintPath, Blueprint, ObjectPath))
					{
						ThreadResult = FAutomationResult::Error(TEXT("not_found"), TEXT("Blueprint not found"), (int32)EHttpServerResponseCodes::BadRequest);
						return;
					}

					UEdGraph* Graph = FindGraphByName(Blueprint, GraphName);
					if (!Graph)
					{
						ThreadResult = FAutomationResult::Error(TEXT("graph_not_found"), TEXT("Graph not found"), (int32)EHttpServerResponseCodes::BadRequest);
						return;
					}

					TArray<TSharedPtr<FJsonValue>> NodeArr;
					NodeArr.Reserve(Graph->Nodes.Num());
					for (UEdGraphNode* Node : Graph->Nodes)
					{
						if (!Node)
						{
							continue;
						}
						NodeArr.Add(MakeShared<FJsonValueObject>(DescribeNodeJson(Node)));
					}

					TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
					Obj->SetBoolField(TEXT("ok"), true);
					Obj->SetStringField(TEXT("blueprint"), ObjectPath);
					Obj->SetStringField(TEXT("graph"), GraphName);
					Obj->SetArrayField(TEXT("nodes"), NodeArr);
					ThreadResult = FAutomationResult::Ok(MakeShared<FJsonValueObject>(Obj));
				}, 10.0);

				if (!bCompleted)
				{
					return FAutomationResult::Error(TEXT("game_thread_timeout"), TEXT("Timed out waiting for GameThread execution"), 504);
				}

				if (!ThreadResult.IsSet())
				{
					return FAutomationResult::Error(TEXT("internal_error"), TEXT("No result produced by graph nodes operation"), 500);
				}

				return ThreadResult.GetValue();
			});

			FBlueprintGraphEditCommand Command(Service);
			FCommandDispatcher Dispatcher;
			FAutomationContext ExecContext;
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

	BlueprintGraphApplyRoute = Router->BindRoute(
		FHttpPath(Route_BlueprintGraphApply),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, Route_BlueprintGraphApply))
			{
				return true;
			}

			TSharedPtr<FJsonObject> BodyObj;
			if (!BAT::Http::TryParseJsonBody(Request.Body, BodyObj) || !BodyObj.IsValid())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("bad_json"), TEXT("Invalid JSON body")));
				return true;
			}

			const FString RequestId = ResolveOrCreateRequestId(Request);
			const FAutomationResult Result = ExecuteAutomationCommand(Route_BlueprintGraphApply, RequestId, BodyObj, true);
			const TSharedPtr<FJsonObject> Root = Result.Data.IsValid() && Result.Data->Type == EJson::Object ? Result.Data->AsObject() : nullptr;

			bool bApplySucceeded = Result.bSuccess;
			if (Root.IsValid())
			{
				Root->TryGetBoolField(TEXT("ok"), bApplySucceeded);
			}

			if (!bApplySucceeded || !Root.IsValid())
			{
				OnComplete(MakeCanonicalResponseFromAutomationResult(Result, RequestId));
				return true;
			}

			const TSharedPtr<FJsonObject>* RawDataPtr = nullptr;
			if (!Root->TryGetObjectField(TEXT("data"), RawDataPtr) || !RawDataPtr || !RawDataPtr->IsValid())
			{
				OnComplete(MakeCanonicalErrorResponse(500, RequestId, TEXT("internal_error"), TEXT("Blueprint graph apply returned no data payload.")));
				return true;
			}

			const TSharedPtr<FJsonObject> RawData = *RawDataPtr;
			const TArray<TSharedPtr<FJsonValue>>* RawWarnings = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* RawErrors = nullptr;
			Root->TryGetArrayField(TEXT("warnings"), RawWarnings);
			Root->TryGetArrayField(TEXT("errors"), RawErrors);

			const TSharedPtr<FJsonObject>* OptionsPtr = nullptr;
			bool bCompileRequested = false;
			bool bSaveRequested = false;
			bool bDryRun = false;
			if (BodyObj->TryGetObjectField(TEXT("options"), OptionsPtr) && OptionsPtr && OptionsPtr->IsValid())
			{
				(*OptionsPtr)->TryGetBoolField(TEXT("compile"), bCompileRequested);
				(*OptionsPtr)->TryGetBoolField(TEXT("save"), bSaveRequested);
				(*OptionsPtr)->TryGetBoolField(TEXT("dryRun"), bDryRun);
			}

			TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
			FString BlueprintPath;
			FString GraphName;
			RawData->TryGetStringField(TEXT("blueprint"), BlueprintPath);
			RawData->TryGetStringField(TEXT("graph"), GraphName);
			Data->SetStringField(TEXT("blueprint"), BlueprintPath);
			Data->SetStringField(TEXT("target"), BlueprintPath);
			Data->SetStringField(TEXT("graph"), GraphName);

			const TArray<TSharedPtr<FJsonValue>>* CreatedNodes = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* UpdatedNodes = nullptr;
			RawData->TryGetArrayField(TEXT("createdNodes"), CreatedNodes);
			RawData->TryGetArrayField(TEXT("updatedNodes"), UpdatedNodes);
			Data->SetArrayField(TEXT("nodesCreated"), CreatedNodes ? *CreatedNodes : TArray<TSharedPtr<FJsonValue>>());
			Data->SetArrayField(TEXT("nodesUpdated"), UpdatedNodes ? *UpdatedNodes : TArray<TSharedPtr<FJsonValue>>());

			double LinksCreated = 0.0;
			RawData->TryGetNumberField(TEXT("createdLinks"), LinksCreated);
			Data->SetNumberField(TEXT("linksCreated"), LinksCreated);
			Data->SetNumberField(TEXT("linksRemoved"), 0.0);
			Data->SetBoolField(TEXT("compiled"), bCompileRequested && !bDryRun);
			Data->SetBoolField(TEXT("saved"), bSaveRequested && !bDryRun);
			Data->SetStringField(TEXT("compileStatus"), bCompileRequested ? (bDryRun ? TEXT("skipped_dry_run") : TEXT("compiled")) : TEXT("not_requested"));
			Data->SetStringField(TEXT("saveStatus"), bSaveRequested ? (bDryRun ? TEXT("skipped_dry_run") : TEXT("saved")) : TEXT("not_requested"));
			Data->SetArrayField(TEXT("warnings"), RawWarnings ? *RawWarnings : TArray<TSharedPtr<FJsonValue>>());
			Data->SetArrayField(TEXT("errors"), RawErrors ? *RawErrors : TArray<TSharedPtr<FJsonValue>>());

			OnComplete(MakeCanonicalSuccessResponse(200, RequestId, Data, RawWarnings ? *RawWarnings : TArray<TSharedPtr<FJsonValue>>()));
			return true;
		}));

	BlueprintNodeAddCallFunctionRoute = Router->BindRoute(
		FHttpPath(TEXT("/blueprint/node/add-callfunction")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, TEXT("/blueprint/node/add-callfunction")))
			{
				return true;
			}

			TSharedPtr<FJsonObject> BodyObj;
			if (!TryParseJsonBody(Request.Body, BodyObj))
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("bad_json"), TEXT("Invalid JSON body")));
				return true;
			}

			FString BlueprintPath;
			FString GraphName;
			FString FunctionPath;
			int32 X = 0;
			int32 Y = 0;
			if (!BodyObj->TryGetStringField(TEXT("blueprint"), BlueprintPath) || BlueprintPath.IsEmpty())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("missing_blueprint"), TEXT("Body must include 'blueprint'")));
				return true;
			}
			BodyObj->TryGetStringField(TEXT("graph"), GraphName);
			if (!BodyObj->TryGetStringField(TEXT("function"), FunctionPath) || FunctionPath.IsEmpty())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("missing_function"), TEXT("Body must include 'function'")));
				return true;
			}
			BodyObj->TryGetNumberField(TEXT("x"), X);
			BodyObj->TryGetNumberField(TEXT("y"), Y);

			const FString RequestId = ResolveOrCreateRequestId(Request);

			FBlueprintService Service([this, BlueprintPath, GraphName, FunctionPath, X, Y](FAutomationContext& Context) -> FAutomationResult
			{
				TOptional<FAutomationResult> ThreadResult;
				const bool bCompleted = RunOnGameThreadWait([this, BlueprintPath, GraphName, FunctionPath, X, Y, &ThreadResult]()
				{
					UBlueprint* Blueprint = nullptr;
					FString ObjectPath;
					if (!TryLoadBlueprint(BlueprintPath, Blueprint, ObjectPath))
					{
						ThreadResult = FAutomationResult::Error(TEXT("not_found"), TEXT("Blueprint not found"), (int32)EHttpServerResponseCodes::BadRequest);
						return;
					}

					UEdGraph* Graph = FindGraphByName(Blueprint, GraphName);
					if (!Graph)
					{
						ThreadResult = FAutomationResult::Error(TEXT("graph_not_found"), TEXT("Graph not found"), (int32)EHttpServerResponseCodes::BadRequest);
						return;
					}

					UFunction* Func = FindObject<UFunction>(nullptr, *FunctionPath);
					if (!Func)
					{
						Func = LoadObject<UFunction>(nullptr, *FunctionPath);
					}
					if (!Func)
					{
						ThreadResult = FAutomationResult::Error(TEXT("function_not_found"), TEXT("Function not found"), (int32)EHttpServerResponseCodes::BadRequest);
						return;
					}

					const FScopedTransaction Tx(FText::FromString(TEXT("BAT: Add Call Function Node")));
					Blueprint->Modify();
					Graph->Modify();

					UK2Node_CallFunction* Node = NewObject<UK2Node_CallFunction>(Graph);
					Node->Modify();
					Node->SetFromFunction(Func);
					Node->NodePosX = X;
					Node->NodePosY = Y;
					Graph->AddNode(Node, true, false);
					Node->CreateNewGuid();
					Node->PostPlacedNewNode();
					Node->AllocateDefaultPins();
					Node->ReconstructNode();

					FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
					ThreadResult = FAutomationResult::Ok(MakeShared<FJsonValueObject>(DescribeNodeJson(Node).ToSharedRef()));
				}, 10.0);

				if (!bCompleted)
				{
					return FAutomationResult::Error(TEXT("game_thread_timeout"), TEXT("Timed out waiting for GameThread execution"), 504);
				}

				if (!ThreadResult.IsSet())
				{
					return FAutomationResult::Error(TEXT("internal_error"), TEXT("No result produced by add-callfunction operation"), 500);
				}

				return ThreadResult.GetValue();
			});

			FBlueprintGraphEditCommand Command(Service);
			FCommandDispatcher Dispatcher;
			FAutomationContext ExecContext;
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

	BlueprintNodeAddCustomEventRoute = Router->BindRoute(
		FHttpPath(TEXT("/blueprint/node/add-custom-event")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, TEXT("/blueprint/node/add-custom-event")))
			{
				return true;
			}

			TSharedPtr<FJsonObject> BodyObj;
			if (!TryParseJsonBody(Request.Body, BodyObj))
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("bad_json"), TEXT("Invalid JSON body")));
				return true;
			}

			FString BlueprintPath;
			FString GraphName;
			FString EventName;
			int32 X = 0;
			int32 Y = 0;
			if (!BodyObj->TryGetStringField(TEXT("blueprint"), BlueprintPath) || BlueprintPath.IsEmpty())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("missing_blueprint"), TEXT("Body must include 'blueprint'")));
				return true;
			}
			BodyObj->TryGetStringField(TEXT("graph"), GraphName);
			if (!BodyObj->TryGetStringField(TEXT("name"), EventName) || EventName.IsEmpty())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("missing_name"), TEXT("Body must include 'name'")));
				return true;
			}
			BodyObj->TryGetNumberField(TEXT("x"), X);
			BodyObj->TryGetNumberField(TEXT("y"), Y);

			const FString RequestId = ResolveOrCreateRequestId(Request);

			FBlueprintService Service([this, BlueprintPath, GraphName, EventName, X, Y](FAutomationContext& Context) -> FAutomationResult
			{
				TOptional<FAutomationResult> ThreadResult;
				const bool bCompleted = RunOnGameThreadWait([this, BlueprintPath, GraphName, EventName, X, Y, &ThreadResult]()
				{
					UBlueprint* Blueprint = nullptr;
					FString ObjectPath;
					if (!TryLoadBlueprint(BlueprintPath, Blueprint, ObjectPath))
					{
						ThreadResult = FAutomationResult::Error(TEXT("not_found"), TEXT("Blueprint not found"), (int32)EHttpServerResponseCodes::BadRequest);
						return;
					}

					UEdGraph* Graph = FindGraphByName(Blueprint, GraphName);
					if (!Graph)
					{
						ThreadResult = FAutomationResult::Error(TEXT("graph_not_found"), TEXT("Graph not found"), (int32)EHttpServerResponseCodes::BadRequest);
						return;
					}

					const FScopedTransaction Tx(FText::FromString(TEXT("BAT: Add Custom Event Node")));
					Blueprint->Modify();
					Graph->Modify();

					UK2Node_CustomEvent* Node = NewObject<UK2Node_CustomEvent>(Graph);
					Node->Modify();
					Node->CustomFunctionName = FName(*EventName);
					Node->NodePosX = X;
					Node->NodePosY = Y;
					Graph->AddNode(Node, true, false);
					Node->CreateNewGuid();
					Node->PostPlacedNewNode();
					Node->AllocateDefaultPins();
					Node->ReconstructNode();

					FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
					ThreadResult = FAutomationResult::Ok(MakeShared<FJsonValueObject>(DescribeNodeJson(Node).ToSharedRef()));
				}, 10.0);

				if (!bCompleted)
				{
					return FAutomationResult::Error(TEXT("game_thread_timeout"), TEXT("Timed out waiting for GameThread execution"), 504);
				}

				if (!ThreadResult.IsSet())
				{
					return FAutomationResult::Error(TEXT("internal_error"), TEXT("No result produced by add-custom-event operation"), 500);
				}

				return ThreadResult.GetValue();
			});

			FBlueprintGraphEditCommand Command(Service);
			FCommandDispatcher Dispatcher;
			FAutomationContext ExecContext;
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

	BlueprintNodeAddBranchRoute = Router->BindRoute(
		FHttpPath(TEXT("/blueprint/node/add-branch")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, TEXT("/blueprint/node/add-branch")))
			{
				return true;
			}

			TSharedPtr<FJsonObject> BodyObj;
			if (!TryParseJsonBody(Request.Body, BodyObj))
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("bad_json"), TEXT("Invalid JSON body")));
				return true;
			}

			FString BlueprintPath;
			FString GraphName;
			int32 X = 0;
			int32 Y = 0;
			if (!BodyObj->TryGetStringField(TEXT("blueprint"), BlueprintPath) || BlueprintPath.IsEmpty())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("missing_blueprint"), TEXT("Body must include 'blueprint'")));
				return true;
			}
			BodyObj->TryGetStringField(TEXT("graph"), GraphName);
			BodyObj->TryGetNumberField(TEXT("x"), X);
			BodyObj->TryGetNumberField(TEXT("y"), Y);

			const FString RequestId = ResolveOrCreateRequestId(Request);

			FBlueprintService Service([this, BlueprintPath, GraphName, X, Y](FAutomationContext& Context) -> FAutomationResult
			{
				TOptional<FAutomationResult> ThreadResult;
				const bool bCompleted = RunOnGameThreadWait([this, BlueprintPath, GraphName, X, Y, &ThreadResult]()
				{
					UBlueprint* Blueprint = nullptr;
					FString ObjectPath;
					if (!TryLoadBlueprint(BlueprintPath, Blueprint, ObjectPath))
					{
						ThreadResult = FAutomationResult::Error(TEXT("not_found"), TEXT("Blueprint not found"), (int32)EHttpServerResponseCodes::BadRequest);
						return;
					}

					UEdGraph* Graph = FindGraphByName(Blueprint, GraphName);
					if (!Graph)
					{
						ThreadResult = FAutomationResult::Error(TEXT("graph_not_found"), TEXT("Graph not found"), (int32)EHttpServerResponseCodes::BadRequest);
						return;
					}

					const FScopedTransaction Tx(FText::FromString(TEXT("BAT: Add Branch Node")));
					Blueprint->Modify();
					Graph->Modify();

					UK2Node_IfThenElse* Node = NewObject<UK2Node_IfThenElse>(Graph);
					Node->Modify();
					Node->NodePosX = X;
					Node->NodePosY = Y;
					Graph->AddNode(Node, true, false);
					Node->CreateNewGuid();
					Node->PostPlacedNewNode();
					Node->AllocateDefaultPins();
					Node->ReconstructNode();

					FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
					ThreadResult = FAutomationResult::Ok(MakeShared<FJsonValueObject>(DescribeNodeJson(Node).ToSharedRef()));
				}, 10.0);

				if (!bCompleted)
				{
					return FAutomationResult::Error(TEXT("game_thread_timeout"), TEXT("Timed out waiting for GameThread execution"), 504);
				}

				if (!ThreadResult.IsSet())
				{
					return FAutomationResult::Error(TEXT("internal_error"), TEXT("No result produced by add-branch operation"), 500);
				}

				return ThreadResult.GetValue();
			});

			FBlueprintGraphEditCommand Command(Service);
			FCommandDispatcher Dispatcher;
			FAutomationContext ExecContext;
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

	BlueprintPinConnectRoute = Router->BindRoute(
		FHttpPath(TEXT("/blueprint/pin/connect")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, TEXT("/blueprint/pin/connect")))
			{
				return true;
			}

			TSharedPtr<FJsonObject> BodyObj;
			if (!TryParseJsonBody(Request.Body, BodyObj))
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("bad_json"), TEXT("Invalid JSON body")));
				return true;
			}

			FString BlueprintPath;
			if (!BodyObj->TryGetStringField(TEXT("blueprint"), BlueprintPath) || BlueprintPath.IsEmpty())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("missing_blueprint"), TEXT("Body must include 'blueprint'")));
				return true;
			}

			const TSharedPtr<FJsonObject>* FromObjPtr = nullptr;
			const TSharedPtr<FJsonObject>* ToObjPtr = nullptr;
			if (!BodyObj->TryGetObjectField(TEXT("from"), FromObjPtr) || !BodyObj->TryGetObjectField(TEXT("to"), ToObjPtr) || FromObjPtr == nullptr || ToObjPtr == nullptr)
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("missing_pins"), TEXT("Body must include 'from' and 'to' objects")));
				return true;
			}
			const TSharedPtr<FJsonObject> FromObj = *FromObjPtr;
			const TSharedPtr<FJsonObject> ToObj = *ToObjPtr;

			FString FromGuidStr;
			FString FromPinName;
			FString ToGuidStr;
			FString ToPinName;
			FromObj->TryGetStringField(TEXT("node_guid"), FromGuidStr);
			FromObj->TryGetStringField(TEXT("pin"), FromPinName);
			ToObj->TryGetStringField(TEXT("node_guid"), ToGuidStr);
			ToObj->TryGetStringField(TEXT("pin"), ToPinName);
			if (FromGuidStr.IsEmpty() || FromPinName.IsEmpty() || ToGuidStr.IsEmpty() || ToPinName.IsEmpty())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("bad_pins"), TEXT("from/to must include node_guid and pin")));
				return true;
			}

			const FString RequestId = ResolveOrCreateRequestId(Request);

			FBlueprintService Service([this, BlueprintPath, FromGuidStr, FromPinName, ToGuidStr, ToPinName](FAutomationContext& Context) -> FAutomationResult
			{
				TOptional<FAutomationResult> ThreadResult;
				const bool bCompleted = RunOnGameThreadWait([this, BlueprintPath, FromGuidStr, FromPinName, ToGuidStr, ToPinName, &ThreadResult]()
				{
					UBlueprint* Blueprint = nullptr;
					FString ObjectPath;
					if (!TryLoadBlueprint(BlueprintPath, Blueprint, ObjectPath))
					{
						ThreadResult = FAutomationResult::Error(TEXT("not_found"), TEXT("Blueprint not found"), (int32)EHttpServerResponseCodes::BadRequest);
						return;
					}

					FGuid FromGuid;
					FGuid ToGuid;
					if (!TryParseGuidString(FromGuidStr, FromGuid) || !TryParseGuidString(ToGuidStr, ToGuid))
					{
						ThreadResult = FAutomationResult::Error(TEXT("bad_guid"), TEXT("Invalid node_guid format"), (int32)EHttpServerResponseCodes::BadRequest);
						return;
					}

					UEdGraphNode* FromNode = FindNodeByGuid(Blueprint, FromGuid);
					UEdGraphNode* ToNode = FindNodeByGuid(Blueprint, ToGuid);
					if (!FromNode || !ToNode)
					{
						ThreadResult = FAutomationResult::Error(TEXT("node_not_found"), TEXT("Node not found"), (int32)EHttpServerResponseCodes::BadRequest);
						return;
					}

					UEdGraphPin* FromPin = FindPinByName(FromNode, FromPinName);
					UEdGraphPin* ToPin = FindPinByName(ToNode, ToPinName);
					if (!FromPin || !ToPin)
					{
						ThreadResult = FAutomationResult::Error(TEXT("pin_not_found"), TEXT("Pin not found"), (int32)EHttpServerResponseCodes::BadRequest);
						return;
					}

					const FScopedTransaction Tx(FText::FromString(TEXT("BAT: Connect Blueprint Pins")));
					Blueprint->Modify();
					FromNode->Modify();
					ToNode->Modify();

					UEdGraph* Graph = FromNode->GetGraph();
					if (Graph)
					{
						Graph->Modify();
					}
					const UEdGraphSchema* Schema = Graph ? Graph->GetSchema() : nullptr;
					bool bOk = false;
					if (Schema)
					{
						bOk = Schema->TryCreateConnection(FromPin, ToPin);
					}
					else
					{
						FromPin->MakeLinkTo(ToPin);
						bOk = true;
					}

					FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
					TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
					Obj->SetBoolField(TEXT("ok"), bOk);
					Obj->SetStringField(TEXT("blueprint"), ObjectPath);
					ThreadResult = FAutomationResult::Ok(MakeShared<FJsonValueObject>(Obj));
				}, 10.0);

				if (!bCompleted)
				{
					return FAutomationResult::Error(TEXT("game_thread_timeout"), TEXT("Timed out waiting for GameThread execution"), 504);
				}

				if (!ThreadResult.IsSet())
				{
					return FAutomationResult::Error(TEXT("internal_error"), TEXT("No result produced by blueprint pin connect operation"), 500);
				}

				return ThreadResult.GetValue();
			});

			FBlueprintGraphEditCommand Command(Service);
			FCommandDispatcher Dispatcher;
			FAutomationContext Context;
			Context.RequestId = RequestId;
			Context.Body = BodyObj;

			const FAutomationResult Result = Dispatcher.Dispatch(Command, Context);
			if (!Result.bSuccess)
			{
				OnComplete(MakeErrorResponse(Result.StatusCode, RequestId, Result.ErrorCode, Result.ErrorMessage));
				return true;
			}

			BAT::Http::JsonOk(OnComplete, Result.Data, Result.StatusCode, RequestId);

			return true;
		}));

	BlueprintPinSetDefaultRoute = Router->BindRoute(
		FHttpPath(TEXT("/blueprint/pin/set-default")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, TEXT("/blueprint/pin/set-default")))
			{
				return true;
			}

			TSharedPtr<FJsonObject> BodyObj;
			if (!TryParseJsonBody(Request.Body, BodyObj))
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("bad_json"), TEXT("Invalid JSON body")));
				return true;
			}

			FString BlueprintPath;
			FString NodeGuidStr;
			FString PinName;
			FString DefaultValue;
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
			if (!BodyObj->TryGetStringField(TEXT("pin"), PinName) || PinName.IsEmpty())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("missing_pin"), TEXT("Body must include 'pin'")));
				return true;
			}
			BodyObj->TryGetStringField(TEXT("value"), DefaultValue);

			const FString RequestId = ResolveOrCreateRequestId(Request);

			FBlueprintService Service([this, BlueprintPath, NodeGuidStr, PinName, DefaultValue](FAutomationContext& Context) -> FAutomationResult
			{
				TOptional<FAutomationResult> ThreadResult;
				const bool bCompleted = RunOnGameThreadWait([this, BlueprintPath, NodeGuidStr, PinName, DefaultValue, &ThreadResult]()
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

					UEdGraphPin* Pin = FindPinByName(Node, PinName);
					if (!Pin)
					{
						ThreadResult = FAutomationResult::Error(TEXT("pin_not_found"), TEXT("Pin not found"), (int32)EHttpServerResponseCodes::BadRequest);
						return;
					}

					const FScopedTransaction Tx(FText::FromString(TEXT("BAT: Set Blueprint Pin Default")));
					Blueprint->Modify();
					Node->Modify();
					if (UEdGraph* Graph = Node->GetGraph())
					{
						Graph->Modify();
					}

					const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
					const bool bClassLikePin = Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Class
						|| Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_SoftClass;
					const bool bObjectLikePin = Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object
						|| Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Class
						|| Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_SoftObject
						|| Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_SoftClass;

					UObject* ResolvedDefaultObject = nullptr;
					if (bObjectLikePin && DefaultValue.StartsWith(TEXT("/")))
					{
						if (bClassLikePin)
						{
							UClass* ResolvedClass = nullptr;
							if (TryResolveClassReference(DefaultValue, ResolvedClass))
							{
								UClass* RequiredMetaClass = Cast<UClass>(Pin->PinType.PinSubCategoryObject.Get());
								if (!RequiredMetaClass || ResolvedClass->IsChildOf(RequiredMetaClass))
								{
									ResolvedDefaultObject = ResolvedClass;
								}
							}
						}
						else
						{
							UClass* RequiredClass = Cast<UClass>(Pin->PinType.PinSubCategoryObject.Get());
							TryResolveObjectReference(DefaultValue, RequiredClass, ResolvedDefaultObject);
						}

						if (!ResolvedDefaultObject)
						{
							ThreadResult = FAutomationResult::Error(
								TEXT("pin_default_object_not_found"),
								FString::Printf(TEXT("Could not resolve a compatible object for pin '%s': %s"), *PinName, *DefaultValue),
								(int32)EHttpServerResponseCodes::BadRequest);
							return;
						}

						Pin->DefaultObject = ResolvedDefaultObject;
						Pin->DefaultValue.Reset();
						Pin->DefaultTextValue = FText::GetEmpty();
						if (K2Schema)
						{
							K2Schema->TrySetDefaultObject(*Pin, ResolvedDefaultObject);
						}
					}
					else if (K2Schema)
					{
						K2Schema->TrySetDefaultValue(*Pin, DefaultValue);
					}
					else
					{
						Pin->DefaultValue = DefaultValue;
					}
					FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

					TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
					Obj->SetBoolField(TEXT("ok"), true);
					Obj->SetStringField(TEXT("blueprint"), ObjectPath);
					Obj->SetStringField(TEXT("default_kind"), ResolvedDefaultObject ? TEXT("object") : TEXT("value"));
					if (ResolvedDefaultObject)
					{
						Obj->SetStringField(TEXT("resolved_object"), ResolvedDefaultObject->GetPathName());
					}
					ThreadResult = FAutomationResult::Ok(MakeShared<FJsonValueObject>(Obj));
				}, 10.0);

				if (!bCompleted)
				{
					return FAutomationResult::Error(TEXT("game_thread_timeout"), TEXT("Timed out waiting for GameThread execution"), 504);
				}

				if (!ThreadResult.IsSet())
				{
					return FAutomationResult::Error(TEXT("internal_error"), TEXT("No result produced by pin-set-default operation"), 500);
				}

				return ThreadResult.GetValue();
			});

			FBlueprintGraphEditCommand Command(Service);
			FCommandDispatcher Dispatcher;
			FAutomationContext ExecContext;
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

	BlueprintNodeDeleteRoute = Router->BindRoute(
		FHttpPath(TEXT("/blueprint/node/delete")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, TEXT("/blueprint/node/delete")))
			{
				return true;
			}

			TSharedPtr<FJsonObject> BodyObj;
			if (!TryParseJsonBody(Request.Body, BodyObj))
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

			FBlueprintService Service([this, BlueprintPath, NodeGuidStr](FAutomationContext& Context) -> FAutomationResult
			{
				TOptional<FAutomationResult> ThreadResult;
				const bool bCompleted = RunOnGameThreadWait([this, BlueprintPath, NodeGuidStr, &ThreadResult]()
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

					const FScopedTransaction Tx(FText::FromString(TEXT("BAT: Delete Blueprint Node")));
					Blueprint->Modify();
					Node->Modify();

					UEdGraph* Graph = Node->GetGraph();
					if (Graph)
					{
						Graph->Modify();
						Graph->RemoveNode(Node);
						FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
					}

					TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
					Obj->SetBoolField(TEXT("ok"), true);
					Obj->SetStringField(TEXT("blueprint"), ObjectPath);
					ThreadResult = FAutomationResult::Ok(MakeShared<FJsonValueObject>(Obj));
				}, 10.0);

				if (!bCompleted)
				{
					return FAutomationResult::Error(TEXT("game_thread_timeout"), TEXT("Timed out waiting for GameThread execution"), 504);
				}

				if (!ThreadResult.IsSet())
				{
					return FAutomationResult::Error(TEXT("internal_error"), TEXT("No result produced by blueprint node delete operation"), 500);
				}

				return ThreadResult.GetValue();
			});

			FBlueprintGraphEditCommand Command(Service);
			FCommandDispatcher Dispatcher;
			FAutomationContext Context;
			Context.RequestId = RequestId;
			Context.Body = BodyObj;

			const FAutomationResult Result = Dispatcher.Dispatch(Command, Context);
			if (!Result.bSuccess)
			{
				OnComplete(MakeErrorResponse(Result.StatusCode, RequestId, Result.ErrorCode, Result.ErrorMessage));
				return true;
			}

			BAT::Http::JsonOk(OnComplete, Result.Data, Result.StatusCode, RequestId);

			return true;
		}));

}

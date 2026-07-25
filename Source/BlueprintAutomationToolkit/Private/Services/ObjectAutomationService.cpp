// Copyright 2026 AkaSoft. All Rights Reserved.

#include "Services/ObjectAutomationService.h"

#include "BlueprintAutomationToolkitModule.h"
#include "Core/EditorExecution.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Misc/DefaultValueHelper.h"
#include "Misc/PackageName.h"
#include "Runtime/Launch/Resources/Version.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/StructOnScope.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

namespace
{
	struct FResolvedPropertyPath
	{
		FProperty* Property = nullptr;
		void* ValuePtr = nullptr;
	};

	static UObject* ResolveObjectFromPath(const FString& InPath)
	{
		FString Path = InPath;
		Path.TrimStartAndEndInline();
		if (Path.IsEmpty())
		{
			return nullptr;
		}

		if (UObject* Found = FindObject<UObject>(nullptr, *Path))
		{
			return Found;
		}
		if (UObject* Found = StaticFindObject(UObject::StaticClass(), nullptr, *Path))
		{
			return Found;
		}

		FSoftObjectPath SoftPath(Path);
		if (UObject* Resolved = SoftPath.ResolveObject())
		{
			return Resolved;
		}
		return SoftPath.TryLoad();
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

	static bool TryResolveReferencedObject(const FString& InPath, UClass* RequiredClass, UObject*& OutObject)
	{
		OutObject = nullptr;

		auto TryCandidate = [&OutObject, RequiredClass](const FString& Candidate) -> bool
		{
			UObject* Resolved = ResolveObjectFromPath(Candidate);
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

	static bool TryResolveReferencedClass(const FString& InPath, UClass*& OutClass)
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

	static bool TryParseVec3(const TSharedPtr<FJsonValue>& JsonValue, FVector& OutVec)
	{
		if (!JsonValue.IsValid() || JsonValue->Type != EJson::Array)
		{
			return false;
		}
		const TArray<TSharedPtr<FJsonValue>>& Arr = JsonValue->AsArray();
		if (Arr.Num() != 3)
		{
			return false;
		}
		OutVec.X = (float)Arr[0]->AsNumber();
		OutVec.Y = (float)Arr[1]->AsNumber();
		OutVec.Z = (float)Arr[2]->AsNumber();
		return true;
	}

	static bool TryParseRotator(const TSharedPtr<FJsonValue>& JsonValue, FRotator& OutRotator)
	{
		if (!JsonValue.IsValid() || JsonValue->Type != EJson::Array)
		{
			return false;
		}
		const TArray<TSharedPtr<FJsonValue>>& Arr = JsonValue->AsArray();
		if (Arr.Num() != 3)
		{
			return false;
		}
		OutRotator.Pitch = (float)Arr[0]->AsNumber();
		OutRotator.Yaw = (float)Arr[1]->AsNumber();
		OutRotator.Roll = (float)Arr[2]->AsNumber();
		return true;
	}

	static TSharedPtr<FJsonValue> Vec3ToJson(const FVector& Value)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		Out.Add(MakeShared<FJsonValueNumber>(Value.X));
		Out.Add(MakeShared<FJsonValueNumber>(Value.Y));
		Out.Add(MakeShared<FJsonValueNumber>(Value.Z));
		return MakeShared<FJsonValueArray>(Out);
	}

	static TSharedPtr<FJsonValue> RotatorToJson(const FRotator& Value)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		Out.Add(MakeShared<FJsonValueNumber>(Value.Pitch));
		Out.Add(MakeShared<FJsonValueNumber>(Value.Yaw));
		Out.Add(MakeShared<FJsonValueNumber>(Value.Roll));
		return MakeShared<FJsonValueArray>(Out);
	}

	static bool IsPropertyPathAllowed(const TSet<FName>& AllowedSet, const FString& PropertyPath)
	{
		if (AllowedSet.Num() == 0)
		{
			return true;
		}

		FString RootSegment = PropertyPath;
		int32 DotIndex = INDEX_NONE;
		if (PropertyPath.FindChar(TEXT('.'), DotIndex))
		{
			RootSegment = PropertyPath.Left(DotIndex);
		}
		RootSegment.TrimStartAndEndInline();
		return AllowedSet.Contains(FName(*RootSegment));
	}

	static bool ResolvePropertyPath(UObject* Root, const FString& PropertyPath, FResolvedPropertyPath& OutResolved, FString& OutErrorCode)
	{
		OutResolved = FResolvedPropertyPath();
		OutErrorCode = TEXT("property_not_found");
		if (!Root)
		{
			OutErrorCode = TEXT("uobject_not_found");
			return false;
		}

		TArray<FString> Segments;
		PropertyPath.ParseIntoArray(Segments, TEXT("."), true);
		if (Segments.Num() <= 0)
		{
			OutErrorCode = TEXT("bad_args");
			return false;
		}

		void* ContainerPtr = Root;
		UStruct* ContainerStruct = Root->GetClass();
		for (int32 Index = 0; Index < Segments.Num(); ++Index)
		{
			const FString Segment = Segments[Index].TrimStartAndEnd();
			if (Segment.IsEmpty())
			{
				OutErrorCode = TEXT("bad_args");
				return false;
			}

			FProperty* Property = ContainerStruct ? ContainerStruct->FindPropertyByName(FName(*Segment)) : nullptr;
			if (!Property)
			{
				OutErrorCode = TEXT("property_not_found");
				return false;
			}

			if (Index == Segments.Num() - 1)
			{
				OutResolved.Property = Property;
				OutResolved.ValuePtr = Property->ContainerPtrToValuePtr<void>(ContainerPtr);
				return OutResolved.ValuePtr != nullptr;
			}

			if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
			{
				ContainerPtr = StructProperty->ContainerPtrToValuePtr<void>(ContainerPtr);
				ContainerStruct = StructProperty->Struct;
				continue;
			}

			if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
			{
				UObject* NextObject = ObjectProperty->GetObjectPropertyValue_InContainer(ContainerPtr);
				if (!NextObject)
				{
					OutErrorCode = TEXT("not_found");
					return false;
				}
				ContainerPtr = NextObject;
				ContainerStruct = NextObject->GetClass();
				continue;
			}

			OutErrorCode = TEXT("type_unsupported");
			return false;
		}

		return false;
	}

	static TSharedPtr<FJsonValue> SerializePropertyValue(FProperty* Property, const void* ValuePtr)
	{
		if (!Property || !ValuePtr)
		{
			return MakeShared<FJsonValueNull>();
		}

		if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
		{
			return MakeShared<FJsonValueBoolean>(BoolProperty->GetPropertyValue(ValuePtr));
		}
		if (const FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
		{
			if (NumericProperty->IsInteger())
			{
				return MakeShared<FJsonValueNumber>((double)NumericProperty->GetSignedIntPropertyValue(ValuePtr));
			}
			return MakeShared<FJsonValueNumber>(NumericProperty->GetFloatingPointPropertyValue(ValuePtr));
		}
		if (const FStrProperty* StringProperty = CastField<FStrProperty>(Property))
		{
			return MakeShared<FJsonValueString>(StringProperty->GetPropertyValue(ValuePtr));
		}
		if (const FNameProperty* NameProperty = CastField<FNameProperty>(Property))
		{
			return MakeShared<FJsonValueString>(NameProperty->GetPropertyValue(ValuePtr).ToString());
		}
		if (const FClassProperty* ClassProperty = CastField<FClassProperty>(Property))
		{
			if (UObject* ClassObject = ClassProperty->GetObjectPropertyValue(ValuePtr))
			{
				if (const UClass* ClassValue = Cast<UClass>(ClassObject))
				{
					return MakeShared<FJsonValueString>(ClassValue->GetPathName());
				}
			}
			return MakeShared<FJsonValueNull>();
		}
		if (const FSoftClassProperty* SoftClassProperty = CastField<FSoftClassProperty>(Property))
		{
			const FSoftObjectPtr* SoftPtr = static_cast<const FSoftObjectPtr*>(ValuePtr);
			const FString Path = SoftPtr ? SoftPtr->ToSoftObjectPath().ToString() : FString();
			return Path.IsEmpty()
				? TSharedPtr<FJsonValue>(MakeShared<FJsonValueNull>())
				: TSharedPtr<FJsonValue>(MakeShared<FJsonValueString>(Path));
		}
		if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
		{
			if (UObject* ObjectValue = ObjectProperty->GetObjectPropertyValue(ValuePtr))
			{
				return MakeShared<FJsonValueString>(ObjectValue->GetPathName());
			}
			return MakeShared<FJsonValueNull>();
		}
		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			if (StructProperty->Struct == TBaseStructure<FVector>::Get())
			{
				return Vec3ToJson(*reinterpret_cast<const FVector*>(ValuePtr));
			}
			if (StructProperty->Struct == TBaseStructure<FRotator>::Get())
			{
				return RotatorToJson(*reinterpret_cast<const FRotator*>(ValuePtr));
			}
			if (StructProperty->Struct == TBaseStructure<FTransform>::Get())
			{
				const FTransform& Transform = *reinterpret_cast<const FTransform*>(ValuePtr);
				TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
				Obj->SetField(TEXT("location"), Vec3ToJson(Transform.GetLocation()));
				Obj->SetField(TEXT("rotation"), RotatorToJson(Transform.Rotator()));
				Obj->SetField(TEXT("scale"), Vec3ToJson(Transform.GetScale3D()));
				return MakeShared<FJsonValueObject>(Obj);
			}
		}

		return MakeShared<FJsonValueNull>();
	}

	static bool DeserializePropertyValue(FProperty* Property, void* ValuePtr, const TSharedPtr<FJsonValue>& JsonValue, FString& OutCode, FString& OutMessage)
	{
		OutCode = TEXT("type_unsupported");
		OutMessage = TEXT("Unsupported property type");
		if (!Property || !ValuePtr || !JsonValue.IsValid())
		{
			OutCode = TEXT("bad_args");
			OutMessage = TEXT("Invalid property assignment payload");
			return false;
		}

		if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
		{
			if (JsonValue->Type != EJson::Boolean)
			{
				OutCode = TEXT("bad_args");
				OutMessage = TEXT("Boolean property requires a JSON boolean");
				return false;
			}
			BoolProperty->SetPropertyValue(ValuePtr, JsonValue->AsBool());
			return true;
		}

		if (FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
		{
			double NumberValue = 0.0;
			if (JsonValue->Type == EJson::Number)
			{
				NumberValue = JsonValue->AsNumber();
			}
			else if (JsonValue->Type == EJson::String && FDefaultValueHelper::ParseDouble(JsonValue->AsString(), NumberValue))
			{
			}
			else
			{
				OutCode = TEXT("bad_args");
				OutMessage = TEXT("Numeric property requires a number");
				return false;
			}

			if (NumericProperty->IsInteger())
			{
				NumericProperty->SetIntPropertyValue(ValuePtr, (int64)NumberValue);
			}
			else
			{
				NumericProperty->SetFloatingPointPropertyValue(ValuePtr, NumberValue);
			}
			return true;
		}

		if (FStrProperty* StringProperty = CastField<FStrProperty>(Property))
		{
			if (JsonValue->Type != EJson::String)
			{
				OutCode = TEXT("bad_args");
				OutMessage = TEXT("String property requires a string");
				return false;
			}
			StringProperty->SetPropertyValue(ValuePtr, JsonValue->AsString());
			return true;
		}

		if (FNameProperty* NameProperty = CastField<FNameProperty>(Property))
		{
			if (JsonValue->Type != EJson::String)
			{
				OutCode = TEXT("bad_args");
				OutMessage = TEXT("Name property requires a string");
				return false;
			}
			NameProperty->SetPropertyValue(ValuePtr, FName(*JsonValue->AsString()));
			return true;
		}

		if (FClassProperty* ClassProperty = CastField<FClassProperty>(Property))
		{
			if (JsonValue->Type == EJson::Null)
			{
				ClassProperty->SetPropertyValue(ValuePtr, nullptr);
				return true;
			}
			if (JsonValue->Type != EJson::String)
			{
				OutCode = TEXT("bad_args");
				OutMessage = TEXT("Class property requires a string reference");
				return false;
			}

			UClass* ResolvedClass = nullptr;
			if (!TryResolveReferencedClass(JsonValue->AsString(), ResolvedClass))
			{
				OutCode = TEXT("not_found");
				OutMessage = TEXT("Class reference could not be resolved");
				return false;
			}
			if (ClassProperty->MetaClass && !ResolvedClass->IsChildOf(ClassProperty->MetaClass))
			{
				OutCode = TEXT("bad_args");
				OutMessage = FString::Printf(TEXT("Class must derive from %s"), *ClassProperty->MetaClass->GetPathName());
				return false;
			}

			ClassProperty->SetPropertyValue(ValuePtr, ResolvedClass);
			return true;
		}

		if (FSoftClassProperty* SoftClassProperty = CastField<FSoftClassProperty>(Property))
		{
			if (JsonValue->Type == EJson::Null)
			{
				*static_cast<FSoftObjectPtr*>(ValuePtr) = FSoftObjectPtr();
				return true;
			}
			if (JsonValue->Type != EJson::String)
			{
				OutCode = TEXT("bad_args");
				OutMessage = TEXT("Soft class property requires a string reference");
				return false;
			}

			UClass* ResolvedClass = nullptr;
			if (!TryResolveReferencedClass(JsonValue->AsString(), ResolvedClass))
			{
				OutCode = TEXT("not_found");
				OutMessage = TEXT("Soft class reference could not be resolved");
				return false;
			}
			if (SoftClassProperty->MetaClass && !ResolvedClass->IsChildOf(SoftClassProperty->MetaClass))
			{
				OutCode = TEXT("bad_args");
				OutMessage = FString::Printf(TEXT("Class must derive from %s"), *SoftClassProperty->MetaClass->GetPathName());
				return false;
			}

			*static_cast<FSoftObjectPtr*>(ValuePtr) = FSoftObjectPtr(ResolvedClass);
			return true;
		}

		if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
		{
			if (JsonValue->Type == EJson::Null)
			{
				ObjectProperty->SetObjectPropertyValue(ValuePtr, nullptr);
				return true;
			}
			if (JsonValue->Type != EJson::String)
			{
				OutCode = TEXT("bad_args");
				OutMessage = TEXT("Object property requires a string reference");
				return false;
			}

			UObject* ResolvedObject = nullptr;
			if (!TryResolveReferencedObject(JsonValue->AsString(), ObjectProperty->PropertyClass, ResolvedObject))
			{
				OutCode = TEXT("not_found");
				OutMessage = TEXT("Object reference could not be resolved");
				return false;
			}

			ObjectProperty->SetObjectPropertyValue(ValuePtr, ResolvedObject);
			return true;
		}

		if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			if (StructProperty->Struct == TBaseStructure<FVector>::Get())
			{
				FVector Vec = FVector::ZeroVector;
				if (!TryParseVec3(JsonValue, Vec))
				{
					OutCode = TEXT("bad_args");
					OutMessage = TEXT("FVector requires [x,y,z]");
					return false;
				}
				*reinterpret_cast<FVector*>(ValuePtr) = Vec;
				return true;
			}
			if (StructProperty->Struct == TBaseStructure<FRotator>::Get())
			{
				FRotator Rotator = FRotator::ZeroRotator;
				if (!TryParseRotator(JsonValue, Rotator))
				{
					OutCode = TEXT("bad_args");
					OutMessage = TEXT("FRotator requires [pitch,yaw,roll]");
					return false;
				}
				*reinterpret_cast<FRotator*>(ValuePtr) = Rotator;
				return true;
			}
			if (StructProperty->Struct == TBaseStructure<FTransform>::Get())
			{
				if (JsonValue->Type != EJson::Object)
				{
					OutCode = TEXT("bad_args");
					OutMessage = TEXT("FTransform requires object with location/rotation/scale");
					return false;
				}

				const TSharedPtr<FJsonObject> Obj = JsonValue->AsObject();
				if (!Obj.IsValid())
				{
					OutCode = TEXT("bad_args");
					OutMessage = TEXT("Invalid transform payload");
					return false;
				}

				FTransform& Transform = *reinterpret_cast<FTransform*>(ValuePtr);
				const TArray<TSharedPtr<FJsonValue>>* LocationArray = nullptr;
				const TArray<TSharedPtr<FJsonValue>>* RotationArray = nullptr;
				const TArray<TSharedPtr<FJsonValue>>* ScaleArray = nullptr;

				if (Obj->TryGetArrayField(TEXT("location"), LocationArray) && LocationArray)
				{
					FVector Location;
					if (!TryParseVec3(MakeShared<FJsonValueArray>(*LocationArray), Location))
					{
						OutCode = TEXT("bad_args");
						OutMessage = TEXT("transform.location requires [x,y,z]");
						return false;
					}
					Transform.SetLocation(Location);
				}
				if (Obj->TryGetArrayField(TEXT("rotation"), RotationArray) && RotationArray)
				{
					FRotator Rotation;
					if (!TryParseRotator(MakeShared<FJsonValueArray>(*RotationArray), Rotation))
					{
						OutCode = TEXT("bad_args");
						OutMessage = TEXT("transform.rotation requires [pitch,yaw,roll]");
						return false;
					}
					Transform.SetRotation(Rotation.Quaternion());
				}
				if (Obj->TryGetArrayField(TEXT("scale"), ScaleArray) && ScaleArray)
				{
					FVector Scale;
					if (!TryParseVec3(MakeShared<FJsonValueArray>(*ScaleArray), Scale))
					{
						OutCode = TEXT("bad_args");
						OutMessage = TEXT("transform.scale requires [x,y,z]");
						return false;
					}
					Transform.SetScale3D(Scale);
				}
				return true;
			}
		}

		return false;
	}

	static UClass* ResolveActorClassFromPath(const FString& InClassPath)
	{
		FString ClassPath = InClassPath;
		ClassPath.TrimStartAndEndInline();
		if (ClassPath.IsEmpty())
		{
			return nullptr;
		}

		UClass* FoundClass = FindObject<UClass>(nullptr, *ClassPath);
		if (!FoundClass)
		{
			FoundClass = LoadObject<UClass>(nullptr, *ClassPath);
		}
		if (!FoundClass)
		{
			for (TObjectIterator<UClass> It; It; ++It)
			{
				UClass* Candidate = *It;
				if (!Candidate)
				{
					continue;
				}
				if (Candidate->GetName().Equals(ClassPath, ESearchCase::IgnoreCase)
					|| Candidate->GetPathName().Equals(ClassPath, ESearchCase::IgnoreCase)
					|| Candidate->GetClassPathName().ToString().Equals(ClassPath, ESearchCase::IgnoreCase))
				{
					FoundClass = Candidate;
					break;
				}
			}
		}
		return FoundClass;
	}
}

FAutomationResult FObjectAutomationService::SetProperty(FBlueprintAutomationToolkitModule& Module, const FString& RequestId, const TSharedPtr<FJsonObject>& BodyObj) const
{
	if (!BodyObj.IsValid())
	{
		return FAutomationResult::Error(TEXT("bad_args"), TEXT("Invalid JSON body"), 400);
	}

	FString Path;
	BodyObj->TryGetStringField(TEXT("path"), Path);
	const TSharedPtr<FJsonObject>* ValuesObj = nullptr;
	BodyObj->TryGetObjectField(TEXT("values"), ValuesObj);
	const TSharedPtr<FJsonObject> Values = (ValuesObj && ValuesObj->IsValid()) ? *ValuesObj : nullptr;

	if (Path.TrimStartAndEnd().IsEmpty() || !Values.IsValid())
	{
		return FAutomationResult::Error(TEXT("bad_args"), TEXT("Body must include non-empty 'path' and object field 'values'"), 400);
	}

	TOptional<FAutomationResult> Result;
	const bool bCompleted = BAT::EditorExecution::RunOnGameThreadAndWaitVoid([&Module, RequestId, Path, Values, &Result]()
	{
		UObject* TargetObject = ResolveObjectFromPath(Path);
		if (!TargetObject)
		{
			Result = FAutomationResult::Error(TEXT("uobject_not_found"), TEXT("Object path could not be resolved"), 404);
			return;
		}

		TargetObject->Modify();
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Values->Values)
		{
			const FString PropertyPath = Pair.Key;
			if (!IsPropertyPathAllowed(Module.GetAllowedUObjectProperties(), PropertyPath))
			{
				Result = FAutomationResult::Error(TEXT("call_denied"), FString::Printf(TEXT("Property not allowlisted: %s"), *PropertyPath), 403);
				return;
			}

			FResolvedPropertyPath Resolved;
			FString ErrorCode;
			if (!ResolvePropertyPath(TargetObject, PropertyPath, Resolved, ErrorCode))
			{
				Result = FAutomationResult::Error(ErrorCode, FString::Printf(TEXT("Failed to resolve property '%s'"), *PropertyPath), 400);
				return;
			}

			FString DecodeCode;
			FString DecodeMessage;
			if (!DeserializePropertyValue(Resolved.Property, Resolved.ValuePtr, Pair.Value, DecodeCode, DecodeMessage))
			{
				Result = FAutomationResult::Error(DecodeCode, FString::Printf(TEXT("%s (%s)"), *DecodeMessage, *PropertyPath), 400);
				return;
			}
		}

		TargetObject->MarkPackageDirty();

		TSharedRef<FJsonObject> ResponseObj = MakeShared<FJsonObject>();
		ResponseObj->SetStringField(TEXT("request_id"), RequestId);
		ResponseObj->SetStringField(TEXT("path"), Path);
		Result = FAutomationResult::Ok(MakeShared<FJsonValueObject>(ResponseObj));
	}, 10.0f);

	if (!bCompleted)
	{
		return FAutomationResult::Error(TEXT("game_thread_timeout"), TEXT("Timed out waiting for GameThread execution"), 504);
	}
	return Result.IsSet() ? Result.GetValue() : FAutomationResult::Error(TEXT("internal_error"), TEXT("No result produced by object set operation"), 500);
}

FAutomationResult FObjectAutomationService::CallFunction(FBlueprintAutomationToolkitModule& Module, const FString& RequestId, const TSharedPtr<FJsonObject>& BodyObj) const
{
	if (!BodyObj.IsValid())
	{
		return FAutomationResult::Error(TEXT("bad_args"), TEXT("Invalid JSON body"), 400);
	}

	FString Path;
	FString FunctionName;
	BodyObj->TryGetStringField(TEXT("path"), Path);
	BodyObj->TryGetStringField(TEXT("function"), FunctionName);
	const TSharedPtr<FJsonObject>* ArgsField = nullptr;
	BodyObj->TryGetObjectField(TEXT("args"), ArgsField);
	const TSharedPtr<FJsonObject> ArgsObj = (ArgsField && ArgsField->IsValid()) ? *ArgsField : MakeShared<FJsonObject>();

	if (Path.TrimStartAndEnd().IsEmpty() || FunctionName.TrimStartAndEnd().IsEmpty())
	{
		return FAutomationResult::Error(TEXT("bad_args"), TEXT("Body must include non-empty 'path' and 'function'"), 400);
	}

	TOptional<FAutomationResult> Result;
	const bool bCompleted = BAT::EditorExecution::RunOnGameThreadAndWaitVoid([&Module, RequestId, Path, FunctionName, ArgsObj, &Result]()
	{
		UObject* TargetObject = ResolveObjectFromPath(Path);
		if (!TargetObject)
		{
			Result = FAutomationResult::Error(TEXT("uobject_not_found"), TEXT("Object path could not be resolved"), 404);
			return;
		}

		const FName FunctionFName(*FunctionName);
		if (!Module.GetAllowedUObjectFunctions().Contains(FunctionFName))
		{
			Result = FAutomationResult::Error(Module.IsSafeModeEnabled() ? TEXT("safe_mode_denied") : TEXT("call_denied"), TEXT("Function is not allowlisted"), 403);
			return;
		}

		UFunction* Function = TargetObject->FindFunction(FunctionFName);
		if (!Function)
		{
			Result = FAutomationResult::Error(TEXT("function_not_found"), TEXT("Function not found on target object"), 404);
			return;
		}

		const bool bBlueprintCallable = Function->HasAnyFunctionFlags(FUNC_BlueprintCallable);
		const bool bDeniedFlags = Function->HasAnyFunctionFlags(FUNC_Exec | FUNC_Net | FUNC_NetClient | FUNC_NetServer | FUNC_NetMulticast);
		const bool bLatent = Function->HasMetaData(TEXT("Latent"));
		if (!bBlueprintCallable || bDeniedFlags || bLatent)
		{
			Result = FAutomationResult::Error(TEXT("call_denied"), TEXT("Function flags are not allowed"), 403);
			return;
		}

		FStructOnScope Params(Function);
		FMemory::Memzero(Params.GetStructMemory(), Function->GetStructureSize());

		for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			FProperty* ParamProperty = *It;
			if (!ParamProperty)
			{
				continue;
			}

			const bool bIsReturn = ParamProperty->HasAnyPropertyFlags(CPF_ReturnParm);
			const bool bIsOutOnly = ParamProperty->HasAnyPropertyFlags(CPF_OutParm) && !ParamProperty->HasAnyPropertyFlags(CPF_ConstParm | CPF_ReferenceParm);
			if (bIsReturn || bIsOutOnly)
			{
				continue;
			}

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8)
			const TSharedPtr<FJsonValue>* JsonField = ArgsObj->Values.Find(UE::FSharedString(*ParamProperty->GetName()));
#else
			const TSharedPtr<FJsonValue>* JsonField = ArgsObj->Values.Find(ParamProperty->GetName());
#endif
			if (!JsonField)
			{
				Result = FAutomationResult::Error(TEXT("bad_args"), FString::Printf(TEXT("Missing argument '%s'"), *ParamProperty->GetName()), 400);
				return;
			}

			void* ParamPtr = ParamProperty->ContainerPtrToValuePtr<void>(Params.GetStructMemory());
			FString DecodeCode;
			FString DecodeMessage;
			if (!DeserializePropertyValue(ParamProperty, ParamPtr, *JsonField, DecodeCode, DecodeMessage))
			{
				Result = FAutomationResult::Error(DecodeCode, FString::Printf(TEXT("Invalid argument '%s': %s"), *ParamProperty->GetName(), *DecodeMessage), 400);
				return;
			}
		}

		TargetObject->ProcessEvent(Function, Params.GetStructMemory());

		TSharedRef<FJsonObject> OutObj = MakeShared<FJsonObject>();
		TSharedPtr<FJsonValue> ReturnValue = MakeShared<FJsonValueNull>();
		for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			FProperty* ParamProperty = *It;
			if (!ParamProperty)
			{
				continue;
			}

			void* ParamPtr = ParamProperty->ContainerPtrToValuePtr<void>(Params.GetStructMemory());
			if (ParamProperty->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				ReturnValue = SerializePropertyValue(ParamProperty, ParamPtr);
				continue;
			}
			if (ParamProperty->HasAnyPropertyFlags(CPF_OutParm))
			{
				OutObj->SetField(ParamProperty->GetName(), SerializePropertyValue(ParamProperty, ParamPtr));
			}
		}

		TSharedRef<FJsonObject> ResponseObj = MakeShared<FJsonObject>();
		ResponseObj->SetStringField(TEXT("request_id"), RequestId);
		ResponseObj->SetField(TEXT("return"), ReturnValue);
		ResponseObj->SetObjectField(TEXT("out"), OutObj);
		Result = FAutomationResult::Ok(MakeShared<FJsonValueObject>(ResponseObj));
	}, 10.0f);

	if (!bCompleted)
	{
		return FAutomationResult::Error(TEXT("game_thread_timeout"), TEXT("Timed out waiting for GameThread execution"), 504);
	}
	return Result.IsSet() ? Result.GetValue() : FAutomationResult::Error(TEXT("internal_error"), TEXT("No result produced by object call operation"), 500);
}

FAutomationResult FObjectAutomationService::SpawnActor(FBlueprintAutomationToolkitModule& Module, const FString& RequestId, const TSharedPtr<FJsonObject>& BodyObj) const
{
	if (!BodyObj.IsValid())
	{
		return FAutomationResult::Error(TEXT("bad_args"), TEXT("Invalid JSON body"), 400);
	}

	FString ClassPath;
	FString Name;
	BodyObj->TryGetStringField(TEXT("class"), ClassPath);
	BodyObj->TryGetStringField(TEXT("name"), Name);
	const TArray<TSharedPtr<FJsonValue>>* LocationField = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* RotationField = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* ScaleField = nullptr;
	BodyObj->TryGetArrayField(TEXT("location"), LocationField);
	BodyObj->TryGetArrayField(TEXT("rotation"), RotationField);
	BodyObj->TryGetArrayField(TEXT("scale"), ScaleField);
	const TSharedPtr<FJsonObject>* PropertiesField = nullptr;
	BodyObj->TryGetObjectField(TEXT("properties"), PropertiesField);
	const TSharedPtr<FJsonObject> PropertiesObj = (PropertiesField && PropertiesField->IsValid()) ? *PropertiesField : nullptr;

	if (ClassPath.TrimStartAndEnd().IsEmpty())
	{
		return FAutomationResult::Error(TEXT("bad_args"), TEXT("Body must include non-empty 'class'"), 400);
	}

	const TArray<TSharedPtr<FJsonValue>> LocationCopy = LocationField ? *LocationField : TArray<TSharedPtr<FJsonValue>>();
	const TArray<TSharedPtr<FJsonValue>> RotationCopy = RotationField ? *RotationField : TArray<TSharedPtr<FJsonValue>>();
	const TArray<TSharedPtr<FJsonValue>> ScaleCopy = ScaleField ? *ScaleField : TArray<TSharedPtr<FJsonValue>>();

	TOptional<FAutomationResult> Result;
	const bool bCompleted = BAT::EditorExecution::RunOnGameThreadAndWaitVoid([&Module, RequestId, ClassPath, Name, LocationCopy, RotationCopy, ScaleCopy, PropertiesObj, &Result]()
	{
		UWorld* EditorWorld = Module.GetEditorWorld();
		if (!EditorWorld)
		{
			Result = FAutomationResult::Error(TEXT("not_found"), TEXT("Editor world not available"), 404);
			return;
		}

		UClass* ActorClass = ResolveActorClassFromPath(ClassPath);
		if (!ActorClass || !ActorClass->IsChildOf(AActor::StaticClass()))
		{
			Result = FAutomationResult::Error(TEXT("type_unsupported"), TEXT("'class' must resolve to an Actor class"), 400);
			return;
		}

		FVector SpawnLocation = FVector::ZeroVector;
		FRotator SpawnRotation = FRotator::ZeroRotator;
		FVector SpawnScale = FVector::OneVector;
		if (LocationCopy.Num() > 0 && !TryParseVec3(MakeShared<FJsonValueArray>(LocationCopy), SpawnLocation))
		{
			Result = FAutomationResult::Error(TEXT("bad_args"), TEXT("'location' must be [x,y,z]"), 400);
			return;
		}
		if (RotationCopy.Num() > 0 && !TryParseRotator(MakeShared<FJsonValueArray>(RotationCopy), SpawnRotation))
		{
			Result = FAutomationResult::Error(TEXT("bad_args"), TEXT("'rotation' must be [pitch,yaw,roll]"), 400);
			return;
		}
		if (ScaleCopy.Num() > 0 && !TryParseVec3(MakeShared<FJsonValueArray>(ScaleCopy), SpawnScale))
		{
			Result = FAutomationResult::Error(TEXT("bad_args"), TEXT("'scale' must be [x,y,z]"), 400);
			return;
		}

		FActorSpawnParameters SpawnParams;
		if (!Name.TrimStartAndEnd().IsEmpty())
		{
			SpawnParams.Name = FName(*Name);
		}
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		AActor* SpawnedActor = EditorWorld->SpawnActor<AActor>(ActorClass, SpawnLocation, SpawnRotation, SpawnParams);
		if (!SpawnedActor)
		{
			Result = FAutomationResult::Error(TEXT("spawn_failed"), TEXT("Failed to spawn actor"), 500);
			return;
		}

		SpawnedActor->SetActorScale3D(SpawnScale);
		if (PropertiesObj.IsValid())
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : PropertiesObj->Values)
			{
				const FString& PropertyPath = Pair.Key;
				if (!IsPropertyPathAllowed(Module.GetAllowedUObjectProperties(), PropertyPath))
				{
					Result = FAutomationResult::Error(TEXT("call_denied"), FString::Printf(TEXT("Property not allowlisted: %s"), *PropertyPath), 403);
					return;
				}

				FResolvedPropertyPath Resolved;
				FString ErrorCode;
				if (!ResolvePropertyPath(SpawnedActor, PropertyPath, Resolved, ErrorCode))
				{
					Result = FAutomationResult::Error(ErrorCode, FString::Printf(TEXT("Failed to resolve property '%s'"), *PropertyPath), 400);
					return;
				}

				FString DecodeCode;
				FString DecodeMessage;
				if (!DeserializePropertyValue(Resolved.Property, Resolved.ValuePtr, Pair.Value, DecodeCode, DecodeMessage))
				{
					Result = FAutomationResult::Error(DecodeCode, FString::Printf(TEXT("%s (%s)"), *DecodeMessage, *PropertyPath), 400);
					return;
				}
			}
		}

		TSharedRef<FJsonObject> ResponseObj = MakeShared<FJsonObject>();
		ResponseObj->SetStringField(TEXT("request_id"), RequestId);
		ResponseObj->SetStringField(TEXT("path"), SpawnedActor->GetPathName());
#if WITH_EDITOR
		ResponseObj->SetStringField(TEXT("label"), SpawnedActor->GetActorLabel());
#endif
		ResponseObj->SetStringField(TEXT("class"), SpawnedActor->GetClass()->GetPathName());
		Result = FAutomationResult::Ok(MakeShared<FJsonValueObject>(ResponseObj));
	}, 10.0f);

	if (!bCompleted)
	{
		return FAutomationResult::Error(TEXT("game_thread_timeout"), TEXT("Timed out waiting for GameThread execution"), 504);
	}
	return Result.IsSet() ? Result.GetValue() : FAutomationResult::Error(TEXT("internal_error"), TEXT("No result produced by actor spawn operation"), 500);
}

// Copyright 2026 AkaSoft. All Rights Reserved.

#include "Services/Reflection/ReflectionSerializationService.h"

#include "Services/Reflection/ReflectionObjectResolver.h"

#include "Containers/ScriptArray.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "GameFramework/Actor.h"
#include "Misc/DefaultValueHelper.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/EnumProperty.h"
#include "UObject/UnrealType.h"

namespace
{
	static TSharedPtr<FJsonValue> MakeNumber(double Value)
	{
		return MakeShared<FJsonValueNumber>(Value);
	}

	static TSharedPtr<FJsonValue> MakeString(const FString& Value)
	{
		return MakeShared<FJsonValueString>(Value);
	}

	static TSharedRef<FJsonObject> MakeVectorObject(const FVector& Value)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetNumberField(TEXT("x"), Value.X);
		Object->SetNumberField(TEXT("y"), Value.Y);
		Object->SetNumberField(TEXT("z"), Value.Z);
		return Object;
	}

	static TSharedRef<FJsonObject> MakeRotatorObject(const FRotator& Value)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetNumberField(TEXT("pitch"), Value.Pitch);
		Object->SetNumberField(TEXT("yaw"), Value.Yaw);
		Object->SetNumberField(TEXT("roll"), Value.Roll);
		return Object;
	}

	static TSharedRef<FJsonObject> MakeLinearColorObject(const FLinearColor& Value)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetNumberField(TEXT("r"), Value.R);
		Object->SetNumberField(TEXT("g"), Value.G);
		Object->SetNumberField(TEXT("b"), Value.B);
		Object->SetNumberField(TEXT("a"), Value.A);
		return Object;
	}

	static bool TryReadVector(const TSharedPtr<FJsonValue>& JsonValue, FVector& OutValue)
	{
		if (!JsonValue.IsValid())
		{
			return false;
		}

		if (JsonValue->Type == EJson::Array)
		{
			const TArray<TSharedPtr<FJsonValue>>& ArrayValue = JsonValue->AsArray();
			if (ArrayValue.Num() != 3)
			{
				return false;
			}
			OutValue.X = static_cast<float>(ArrayValue[0]->AsNumber());
			OutValue.Y = static_cast<float>(ArrayValue[1]->AsNumber());
			OutValue.Z = static_cast<float>(ArrayValue[2]->AsNumber());
			return true;
		}

		if (JsonValue->Type != EJson::Object)
		{
			return false;
		}

		const TSharedPtr<FJsonObject> ObjectValue = JsonValue->AsObject();
		return ObjectValue.IsValid()
			&& ObjectValue->TryGetNumberField(TEXT("x"), OutValue.X)
			&& ObjectValue->TryGetNumberField(TEXT("y"), OutValue.Y)
			&& ObjectValue->TryGetNumberField(TEXT("z"), OutValue.Z);
	}

	static bool TryReadRotator(const TSharedPtr<FJsonValue>& JsonValue, FRotator& OutValue)
	{
		if (!JsonValue.IsValid())
		{
			return false;
		}

		if (JsonValue->Type == EJson::Array)
		{
			const TArray<TSharedPtr<FJsonValue>>& ArrayValue = JsonValue->AsArray();
			if (ArrayValue.Num() != 3)
			{
				return false;
			}
			OutValue.Pitch = static_cast<float>(ArrayValue[0]->AsNumber());
			OutValue.Yaw = static_cast<float>(ArrayValue[1]->AsNumber());
			OutValue.Roll = static_cast<float>(ArrayValue[2]->AsNumber());
			return true;
		}

		if (JsonValue->Type != EJson::Object)
		{
			return false;
		}

		const TSharedPtr<FJsonObject> ObjectValue = JsonValue->AsObject();
		return ObjectValue.IsValid()
			&& ObjectValue->TryGetNumberField(TEXT("pitch"), OutValue.Pitch)
			&& ObjectValue->TryGetNumberField(TEXT("yaw"), OutValue.Yaw)
			&& ObjectValue->TryGetNumberField(TEXT("roll"), OutValue.Roll);
	}

	static bool TryReadLinearColor(const TSharedPtr<FJsonValue>& JsonValue, FLinearColor& OutValue)
	{
		if (!JsonValue.IsValid() || JsonValue->Type != EJson::Object)
		{
			return false;
		}

		const TSharedPtr<FJsonObject> ObjectValue = JsonValue->AsObject();
		return ObjectValue.IsValid()
			&& ObjectValue->TryGetNumberField(TEXT("r"), OutValue.R)
			&& ObjectValue->TryGetNumberField(TEXT("g"), OutValue.G)
			&& ObjectValue->TryGetNumberField(TEXT("b"), OutValue.B)
			&& ObjectValue->TryGetNumberField(TEXT("a"), OutValue.A);
	}

	static FString GetReferencePath(const TSharedPtr<FJsonValue>& JsonValue)
	{
		if (!JsonValue.IsValid())
		{
			return FString();
		}

		if (JsonValue->Type == EJson::String)
		{
			return JsonValue->AsString();
		}

		if (JsonValue->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject> ObjectValue = JsonValue->AsObject();
			FString Path;
			if (ObjectValue.IsValid() && (ObjectValue->TryGetStringField(TEXT("objectPath"), Path) || ObjectValue->TryGetStringField(TEXT("path"), Path)))
			{
				return Path;
			}
		}

		return FString();
	}

	static UEnum* ResolveEnum(const FProperty* Property)
	{
		if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			return EnumProperty->GetEnum();
		}

		return nullptr;
	}

	static bool IsSupportedArrayInnerType(const FProperty* Property)
	{
		return Property
			&& (CastField<FBoolProperty>(Property)
				|| CastField<FIntProperty>(Property)
				|| CastField<FFloatProperty>(Property)
				|| CastField<FStrProperty>(Property)
				|| CastField<FNameProperty>(Property)
				|| CastField<FEnumProperty>(Property)
				|| CastField<FObjectProperty>(Property));
	}

	static UObject* LoadObjectReference(const FObjectProperty* ObjectProperty, const FString& ReferencePath)
	{
		if (!ObjectProperty)
		{
			return nullptr;
		}

		const FString TrimmedPath = ReferencePath.TrimStartAndEnd();
		if (TrimmedPath.IsEmpty())
		{
			return nullptr;
		}

		return StaticLoadObject(ObjectProperty->PropertyClass, nullptr, *TrimmedPath);
	}
}

bool FReflectionSerializationService::IsSupportedPropertyType(const FProperty* Property, bool bForWrite) const
{
	if (!Property)
	{
		return false;
	}

	if (CastField<FBoolProperty>(Property)
		|| CastField<FIntProperty>(Property)
		|| CastField<FFloatProperty>(Property)
		|| CastField<FStrProperty>(Property)
		|| CastField<FNameProperty>(Property)
		|| CastField<FObjectProperty>(Property)
		|| CastField<FEnumProperty>(Property))
	{
		return true;
	}

	if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
	{
		return IsSupportedArrayInnerType(ArrayProperty->Inner);
	}

	if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
	{
		return StructProperty->Struct == TBaseStructure<FVector>::Get()
			|| StructProperty->Struct == TBaseStructure<FRotator>::Get()
			|| StructProperty->Struct == TBaseStructure<FTransform>::Get()
			|| StructProperty->Struct == TBaseStructure<FLinearColor>::Get();
	}

	return false;
}

TSharedRef<FJsonObject> FReflectionSerializationService::SerializeObjectReference(const UObject* Object) const
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	if (!Object)
	{
		Result->SetStringField(TEXT("objectPath"), TEXT(""));
		Result->SetStringField(TEXT("className"), TEXT(""));
		Result->SetStringField(TEXT("classPath"), TEXT(""));
		return Result;
	}

	Result->SetStringField(TEXT("objectPath"), Object->GetPathName());
	Result->SetStringField(TEXT("className"), Object->GetClass()->GetName());
	Result->SetStringField(TEXT("classPath"), Object->GetClass()->GetPathName());

	if (const AActor* Actor = Cast<AActor>(Object))
	{
#if WITH_EDITOR
		Result->SetStringField(TEXT("displayName"), Actor->GetActorLabel());
#else
		Result->SetStringField(TEXT("displayName"), Actor->GetName());
#endif
	}
	else
	{
		Result->SetStringField(TEXT("displayName"), Object->GetName());
	}

	return Result;
}

TSharedPtr<FJsonObject> FReflectionSerializationService::SerializeReflectedValue(const FProperty* Property, const void* ValuePtr) const
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("type"), Property ? Property->GetCPPType() : TEXT(""));
	Result->SetBoolField(TEXT("supported"), IsSupportedPropertyType(Property, false));
	Result->SetField(TEXT("value"), SerializeValue(Property, ValuePtr));
	return Result;
}

TSharedRef<FJsonObject> FReflectionSerializationService::DescribeProperty(const FProperty* Property) const
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("name"), Property ? Property->GetName() : TEXT(""));
	Result->SetStringField(TEXT("displayName"), Property ? Property->GetMetaData(TEXT("DisplayName")) : TEXT(""));
	Result->SetStringField(TEXT("cppType"), Property ? Property->GetCPPType() : TEXT(""));
	Result->SetStringField(TEXT("category"), Property ? Property->GetMetaData(TEXT("Category")) : TEXT(""));
	Result->SetBoolField(TEXT("editable"), Property && Property->HasAnyPropertyFlags(CPF_Edit) && !Property->HasAnyPropertyFlags(CPF_EditConst));
	Result->SetBoolField(TEXT("blueprintReadOnly"), Property && Property->HasAnyPropertyFlags(CPF_BlueprintReadOnly));
	Result->SetBoolField(TEXT("isArray"), Property && CastField<FArrayProperty>(Property) != nullptr);
	Result->SetBoolField(TEXT("isMap"), Property && CastField<FMapProperty>(Property) != nullptr);
	Result->SetBoolField(TEXT("isSet"), Property && CastField<FSetProperty>(Property) != nullptr);
	Result->SetBoolField(TEXT("isObjectReference"), Property && CastField<FObjectProperty>(Property) != nullptr);
	Result->SetBoolField(TEXT("isEnum"), Property && ResolveEnum(Property) != nullptr);
	Result->SetBoolField(TEXT("isStruct"), Property && CastField<FStructProperty>(Property) != nullptr);
	Result->SetBoolField(TEXT("supported"), IsSupportedPropertyType(Property, false));
	return Result;
}

TSharedRef<FJsonObject> FReflectionSerializationService::DescribeFunction(const UFunction* Function, bool bCallableInSafeMode) const
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("name"), Function ? Function->GetName() : TEXT(""));
	Result->SetStringField(TEXT("displayName"), Function ? Function->GetMetaData(TEXT("DisplayName")) : TEXT(""));
	Result->SetStringField(TEXT("category"), Function ? Function->GetMetaData(TEXT("Category")) : TEXT(""));
	Result->SetBoolField(TEXT("blueprintCallable"), Function && Function->HasAnyFunctionFlags(FUNC_BlueprintCallable));
	Result->SetBoolField(TEXT("callInEditor"), Function && Function->HasMetaData(TEXT("CallInEditor")));
	Result->SetBoolField(TEXT("const"), Function && Function->HasAnyFunctionFlags(FUNC_Const));
	Result->SetBoolField(TEXT("static"), Function && Function->HasAnyFunctionFlags(FUNC_Static));
	Result->SetBoolField(TEXT("safeModeCallable"), bCallableInSafeMode);

	TArray<TSharedPtr<FJsonValue>> Parameters;
	FString ReturnType;
	if (Function)
	{
		for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			const FProperty* Parameter = *It;
			if (!Parameter)
			{
				continue;
			}

			if (Parameter->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				ReturnType = Parameter->GetCPPType();
				continue;
			}

			TSharedRef<FJsonObject> ParameterObject = MakeShared<FJsonObject>();
			ParameterObject->SetStringField(TEXT("name"), Parameter->GetName());
			ParameterObject->SetStringField(TEXT("cppType"), Parameter->GetCPPType());
			ParameterObject->SetBoolField(TEXT("isOut"), Parameter->HasAnyPropertyFlags(CPF_OutParm));
			ParameterObject->SetBoolField(TEXT("isReference"), Parameter->HasAnyPropertyFlags(CPF_ReferenceParm));
			ParameterObject->SetBoolField(TEXT("isConst"), Parameter->HasAnyPropertyFlags(CPF_ConstParm));
			ParameterObject->SetBoolField(TEXT("supported"), IsSupportedPropertyType(Parameter, true));
			Parameters.Add(MakeShared<FJsonValueObject>(ParameterObject));
		}
	}

	Result->SetArrayField(TEXT("parameters"), Parameters);
	Result->SetStringField(TEXT("returnType"), ReturnType);
	return Result;
}

TSharedPtr<FJsonValue> FReflectionSerializationService::SerializeValue(const FProperty* Property, const void* ValuePtr) const
{
	if (!Property || !ValuePtr)
	{
		return MakeShared<FJsonValueNull>();
	}

	if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
	{
		return MakeShared<FJsonValueBoolean>(BoolProperty->GetPropertyValue(ValuePtr));
	}

	if (const FIntProperty* IntProperty = CastField<FIntProperty>(Property))
	{
		return MakeNumber(static_cast<double>(IntProperty->GetPropertyValue(ValuePtr)));
	}

	if (const FFloatProperty* FloatProperty = CastField<FFloatProperty>(Property))
	{
		return MakeNumber(FloatProperty->GetPropertyValue(ValuePtr));
	}

	if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
	{
		const UEnum* Enum = EnumProperty->GetEnum();
		const int64 EnumValue = EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(ValuePtr);

		TSharedRef<FJsonObject> EnumObject = MakeShared<FJsonObject>();
		EnumObject->SetStringField(TEXT("name"), Enum ? Enum->GetNameStringByValue(EnumValue) : TEXT(""));
		EnumObject->SetNumberField(TEXT("value"), static_cast<double>(EnumValue));
		return MakeShared<FJsonValueObject>(EnumObject);
	}

	if (const FStrProperty* StringProperty = CastField<FStrProperty>(Property))
	{
		return MakeString(StringProperty->GetPropertyValue(ValuePtr));
	}

	if (const FNameProperty* NameProperty = CastField<FNameProperty>(Property))
	{
		return MakeString(NameProperty->GetPropertyValue(ValuePtr).ToString());
	}

	if (const FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property))
	{
		return MakeShared<FJsonValueObject>(SerializeObjectReference(ObjectProperty->GetObjectPropertyValue(ValuePtr)));
	}

	if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
	{
		FScriptArrayHelper ArrayHelper(ArrayProperty, ValuePtr);
		TArray<TSharedPtr<FJsonValue>> Items;
		Items.Reserve(ArrayHelper.Num());
		for (int32 Index = 0; Index < ArrayHelper.Num(); ++Index)
		{
			Items.Add(SerializeValue(ArrayProperty->Inner, ArrayHelper.GetRawPtr(Index)));
		}
		return MakeShared<FJsonValueArray>(Items);
	}

	if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
	{
		if (StructProperty->Struct == TBaseStructure<FVector>::Get())
		{
			return MakeShared<FJsonValueObject>(MakeVectorObject(*reinterpret_cast<const FVector*>(ValuePtr)));
		}
		if (StructProperty->Struct == TBaseStructure<FRotator>::Get())
		{
			return MakeShared<FJsonValueObject>(MakeRotatorObject(*reinterpret_cast<const FRotator*>(ValuePtr)));
		}
		if (StructProperty->Struct == TBaseStructure<FLinearColor>::Get())
		{
			return MakeShared<FJsonValueObject>(MakeLinearColorObject(*reinterpret_cast<const FLinearColor*>(ValuePtr)));
		}
		if (StructProperty->Struct == TBaseStructure<FTransform>::Get())
		{
			const FTransform& Transform = *reinterpret_cast<const FTransform*>(ValuePtr);
			TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetObjectField(TEXT("location"), MakeVectorObject(Transform.GetLocation()));
			Object->SetObjectField(TEXT("rotation"), MakeRotatorObject(Transform.Rotator()));
			Object->SetObjectField(TEXT("scale"), MakeVectorObject(Transform.GetScale3D()));
			return MakeShared<FJsonValueObject>(Object);
		}
	}

	return MakeShared<FJsonValueNull>();
}

bool FReflectionSerializationService::DeserializeValue(FProperty* Property, void* ValuePtr, const TSharedPtr<FJsonValue>& JsonValue, const FReflectionObjectResolver& Resolver, FString& OutCode, FString& OutMessage) const
{
	(void)Resolver;

	OutCode = TEXT("InvalidType");
	OutMessage = TEXT("Unsupported reflected type.");
	if (!Property || !ValuePtr || !JsonValue.IsValid())
	{
		OutCode = TEXT("InvalidArguments");
		OutMessage = TEXT("Invalid reflected value payload.");
		return false;
	}

	if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
	{
		if (JsonValue->Type != EJson::Boolean)
		{
			OutCode = TEXT("InvalidType");
			OutMessage = TEXT("Boolean property requires a JSON boolean.");
			return false;
		}
		BoolProperty->SetPropertyValue(ValuePtr, JsonValue->AsBool());
		return true;
	}

	if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
	{
		UEnum* Enum = EnumProperty->GetEnum();
		int64 EnumValue = INDEX_NONE;
		if (JsonValue->Type == EJson::String)
		{
			const FString EnumString = JsonValue->AsString();
			EnumValue = Enum ? Enum->GetValueByNameString(EnumString, EGetByNameFlags::None) : INDEX_NONE;
			if (EnumValue == INDEX_NONE)
			{
				EnumValue = Enum ? Enum->GetValueByName(FName(*EnumString)) : INDEX_NONE;
			}
		}
		else if (JsonValue->Type == EJson::Number)
		{
			EnumValue = static_cast<int64>(JsonValue->AsNumber());
		}

		if (EnumValue == INDEX_NONE)
		{
			OutCode = TEXT("InvalidType");
			OutMessage = TEXT("Enum value must be a valid enum name or numeric value.");
			return false;
		}

		EnumProperty->GetUnderlyingProperty()->SetIntPropertyValue(ValuePtr, EnumValue);
		return true;
	}

	if (FIntProperty* IntProperty = CastField<FIntProperty>(Property))
	{
		int32 IntValue = 0;
		if (JsonValue->Type == EJson::Number)
		{
			IntValue = static_cast<int32>(JsonValue->AsNumber());
		}
		else if (JsonValue->Type == EJson::String && FDefaultValueHelper::ParseInt(JsonValue->AsString(), IntValue))
		{
		}
		else
		{
			OutCode = TEXT("InvalidType");
			OutMessage = TEXT("Integer property requires an integer value.");
			return false;
		}

		IntProperty->SetPropertyValue(ValuePtr, IntValue);
		return true;
	}

	if (FFloatProperty* FloatProperty = CastField<FFloatProperty>(Property))
	{
		float FloatValue = 0.0f;
		if (JsonValue->Type == EJson::Number)
		{
			FloatValue = static_cast<float>(JsonValue->AsNumber());
		}
		else if (JsonValue->Type == EJson::String && FDefaultValueHelper::ParseFloat(JsonValue->AsString(), FloatValue))
		{
		}
		else
		{
			OutCode = TEXT("InvalidType");
			OutMessage = TEXT("Float property requires a numeric value.");
			return false;
		}

		FloatProperty->SetPropertyValue(ValuePtr, FloatValue);
		return true;
	}

	if (FStrProperty* StringProperty = CastField<FStrProperty>(Property))
	{
		if (JsonValue->Type != EJson::String)
		{
			OutCode = TEXT("InvalidType");
			OutMessage = TEXT("String property requires a JSON string.");
			return false;
		}
		StringProperty->SetPropertyValue(ValuePtr, JsonValue->AsString());
		return true;
	}

	if (FNameProperty* NameProperty = CastField<FNameProperty>(Property))
	{
		if (JsonValue->Type != EJson::String)
		{
			OutCode = TEXT("InvalidType");
			OutMessage = TEXT("Name property requires a JSON string.");
			return false;
		}
		NameProperty->SetPropertyValue(ValuePtr, FName(*JsonValue->AsString()));
		return true;
	}

	if (FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property))
	{
		const FString Path = GetReferencePath(JsonValue);
		if (Path.IsEmpty())
		{
			OutCode = TEXT("InvalidArguments");
			OutMessage = TEXT("Object property requires an object path string or object with objectPath.");
			return false;
		}

		UObject* ResolvedObject = LoadObjectReference(ObjectProperty, Path);
		if (!ResolvedObject)
		{
			OutCode = TEXT("ObjectNotFound");
			OutMessage = TEXT("Referenced object could not be resolved.");
			return false;
		}

		if (!ResolvedObject->IsA(ObjectProperty->PropertyClass))
		{
			OutCode = TEXT("InvalidType");
			OutMessage = FString::Printf(TEXT("Referenced object must be of type '%s'."), *ObjectProperty->PropertyClass->GetPathName());
			return false;
		}

		ObjectProperty->SetObjectPropertyValue(ValuePtr, ResolvedObject);
		return true;
	}

	if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
	{
		if (JsonValue->Type != EJson::Array)
		{
			OutCode = TEXT("InvalidType");
			OutMessage = TEXT("Array property requires a JSON array.");
			return false;
		}

		if (!IsSupportedArrayInnerType(ArrayProperty->Inner))
		{
			OutCode = TEXT("InvalidType");
			OutMessage = TEXT("Only arrays of bool, int32, float, FString, FName, enums, or UObject references are supported.");
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>& ArrayValue = JsonValue->AsArray();
		FScriptArrayHelper ArrayHelper(ArrayProperty, ValuePtr);
		ArrayHelper.EmptyAndAddValues(ArrayValue.Num());
		for (int32 Index = 0; Index < ArrayValue.Num(); ++Index)
		{
			void* ElementPtr = ArrayHelper.GetRawPtr(Index);
			ArrayProperty->Inner->InitializeValue(ElementPtr);
			if (!DeserializeValue(ArrayProperty->Inner, ElementPtr, ArrayValue[Index], Resolver, OutCode, OutMessage))
			{
				OutMessage = FString::Printf(TEXT("Array index %d: %s"), Index, *OutMessage);
				return false;
			}
		}
		return true;
	}

	if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
	{
		if (StructProperty->Struct == TBaseStructure<FVector>::Get())
		{
			FVector VectorValue = FVector::ZeroVector;
			if (!TryReadVector(JsonValue, VectorValue))
			{
				OutCode = TEXT("InvalidType");
				OutMessage = TEXT("FVector requires {x,y,z} or [x,y,z].");
				return false;
			}
			*reinterpret_cast<FVector*>(ValuePtr) = VectorValue;
			return true;
		}

		if (StructProperty->Struct == TBaseStructure<FRotator>::Get())
		{
			FRotator RotatorValue = FRotator::ZeroRotator;
			if (!TryReadRotator(JsonValue, RotatorValue))
			{
				OutCode = TEXT("InvalidType");
				OutMessage = TEXT("FRotator requires {pitch,yaw,roll} or [pitch,yaw,roll].");
				return false;
			}
			*reinterpret_cast<FRotator*>(ValuePtr) = RotatorValue;
			return true;
		}

		if (StructProperty->Struct == TBaseStructure<FLinearColor>::Get())
		{
			FLinearColor ColorValue = FLinearColor::White;
			if (!TryReadLinearColor(JsonValue, ColorValue))
			{
				OutCode = TEXT("InvalidType");
				OutMessage = TEXT("FLinearColor requires {r,g,b,a}.");
				return false;
			}
			*reinterpret_cast<FLinearColor*>(ValuePtr) = ColorValue;
			return true;
		}

		if (StructProperty->Struct == TBaseStructure<FTransform>::Get())
		{
			if (JsonValue->Type != EJson::Object)
			{
				OutCode = TEXT("InvalidType");
				OutMessage = TEXT("FTransform requires an object with location, rotation, and scale.");
				return false;
			}

			const TSharedPtr<FJsonObject> ObjectValue = JsonValue->AsObject();
			if (!ObjectValue.IsValid())
			{
				OutCode = TEXT("InvalidArguments");
				OutMessage = TEXT("Invalid FTransform payload.");
				return false;
			}

			FTransform TransformValue = *reinterpret_cast<FTransform*>(ValuePtr);
			const TSharedPtr<FJsonValue>* LocationValue = ObjectValue->Values.Find(TEXT("location"));
			const TSharedPtr<FJsonValue>* RotationValue = ObjectValue->Values.Find(TEXT("rotation"));
			const TSharedPtr<FJsonValue>* ScaleValue = ObjectValue->Values.Find(TEXT("scale"));

			if (LocationValue)
			{
				FVector Location = FVector::ZeroVector;
				if (!TryReadVector(*LocationValue, Location))
				{
					OutCode = TEXT("InvalidType");
					OutMessage = TEXT("FTransform.location requires {x,y,z} or [x,y,z].");
					return false;
				}
				TransformValue.SetLocation(Location);
			}

			if (RotationValue)
			{
				FRotator Rotation = FRotator::ZeroRotator;
				if (!TryReadRotator(*RotationValue, Rotation))
				{
					OutCode = TEXT("InvalidType");
					OutMessage = TEXT("FTransform.rotation requires {pitch,yaw,roll} or [pitch,yaw,roll].");
					return false;
				}
				TransformValue.SetRotation(Rotation.Quaternion());
			}

			if (ScaleValue)
			{
				FVector Scale = FVector::OneVector;
				if (!TryReadVector(*ScaleValue, Scale))
				{
					OutCode = TEXT("InvalidType");
					OutMessage = TEXT("FTransform.scale requires {x,y,z} or [x,y,z].");
					return false;
				}
				TransformValue.SetScale3D(Scale);
			}

			*reinterpret_cast<FTransform*>(ValuePtr) = TransformValue;
			return true;
		}
	}

	return false;
}
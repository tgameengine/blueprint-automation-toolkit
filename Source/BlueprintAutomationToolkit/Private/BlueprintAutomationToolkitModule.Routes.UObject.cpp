#include "BlueprintAutomationToolkitModule.h"

#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "HttpPath.h"
#include "HttpResultCallback.h"
#include "Http/HttpRequestUtils.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"
#include "Misc/DefaultValueHelper.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/StructOnScope.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogBlueprintAutomationToolkitUObject, Log, All);

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
			if (Path.IsEmpty())
			{
				return MakeShared<FJsonValueNull>();
			}
			return MakeShared<FJsonValueString>(Path);
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

	static TSharedPtr<FJsonValue> MakeActorSummary(AActor* Actor)
	{
		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("path"), Actor ? Actor->GetPathName() : TEXT(""));
		Obj->SetStringField(TEXT("class"), (Actor && Actor->GetClass()) ? Actor->GetClass()->GetPathName() : TEXT(""));
#if WITH_EDITOR
		Obj->SetStringField(TEXT("label"), Actor ? Actor->GetActorLabel() : TEXT(""));
#else
		Obj->SetStringField(TEXT("label"), Actor ? Actor->GetName() : TEXT(""));
#endif
		return MakeShared<FJsonValueObject>(Obj);
	}
}

void FBlueprintAutomationToolkitModule::BindUObjectRoutes()
{
	UObjectGetRoute = Router->BindRoute(
		FHttpPath(TEXT("/uobject/get")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, TEXT("/uobject/get")))
			{
				return true;
			}

			TSharedPtr<FJsonObject> BodyObj;
			if (!BAT::Http::TryParseJsonBody(Request.Body, BodyObj) || !BodyObj.IsValid())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("bad_args"), TEXT("Invalid JSON body")));
				return true;
			}

			FString Path;
			BodyObj->TryGetStringField(TEXT("path"), Path);
			const TArray<TSharedPtr<FJsonValue>>* PropertyArray = nullptr;
			BodyObj->TryGetArrayField(TEXT("properties"), PropertyArray);
			const FString RequestId = ResolveOrCreateRequestId(Request);

			AsyncTask(ENamedThreads::GameThread, [this, Path, PropertyArrayCopy = PropertyArray ? *PropertyArray : TArray<TSharedPtr<FJsonValue>>(), RequestId, OnComplete]()
			{
				UObject* TargetObject = ResolveObjectFromPath(Path);
				if (!TargetObject)
				{
					OnComplete(MakeErrorResponse(EHttpServerResponseCodes::NotFound, RequestId, TEXT("uobject_not_found"), TEXT("Object path could not be resolved")));
					return;
				}

				TSharedRef<FJsonObject> ValuesObj = MakeShared<FJsonObject>();
				for (const TSharedPtr<FJsonValue>& PropertyValue : PropertyArrayCopy)
				{
					if (!PropertyValue.IsValid() || PropertyValue->Type != EJson::String)
					{
						OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, RequestId, TEXT("bad_args"), TEXT("'properties' must contain strings")));
						return;
					}

					const FString PropertyPath = PropertyValue->AsString().TrimStartAndEnd();
					if (!IsPropertyPathAllowed(AllowedUObjectProperties, PropertyPath))
					{
						OnComplete(MakeErrorResponse(EHttpServerResponseCodes::Denied, RequestId, TEXT("call_denied"), FString::Printf(TEXT("Property not allowlisted: %s"), *PropertyPath)));
						return;
					}

					FResolvedPropertyPath Resolved;
					FString ErrorCode;
					if (!ResolvePropertyPath(TargetObject, PropertyPath, Resolved, ErrorCode))
					{
						OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, RequestId, ErrorCode, FString::Printf(TEXT("Failed to resolve property '%s'"), *PropertyPath)));
						return;
					}

					TSharedPtr<FJsonValue> Serialized = SerializePropertyValue(Resolved.Property, Resolved.ValuePtr);
					if (!Serialized.IsValid())
					{
						OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, RequestId, TEXT("type_unsupported"), FString::Printf(TEXT("Unsupported property type '%s'"), *PropertyPath)));
						return;
					}
					ValuesObj->SetField(PropertyPath, Serialized);
				}

				TSharedRef<FJsonObject> ResponseObj = MakeShared<FJsonObject>();
				ResponseObj->SetBoolField(TEXT("ok"), true);
				ResponseObj->SetStringField(TEXT("request_id"), RequestId);
				ResponseObj->SetObjectField(TEXT("values"), ValuesObj);
				OnComplete(BAT::Http::MakeJsonResponse(200, ResponseObj, RequestId));
			});
			return true;
		}));

	UObjectSetRoute = Router->BindRoute(
		FHttpPath(TEXT("/uobject/set")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, TEXT("/uobject/set")))
			{
				return true;
			}

			TSharedPtr<FJsonObject> BodyObj;
			if (!BAT::Http::TryParseJsonBody(Request.Body, BodyObj) || !BodyObj.IsValid())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("bad_args"), TEXT("Invalid JSON body")));
				return true;
			}

			return DispatchAutomationCommandRoute(TEXT("/uobject/set"), Request, OnComplete, BodyObj);
		}));

	UObjectCallRoute = Router->BindRoute(
		FHttpPath(TEXT("/uobject/call")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, TEXT("/uobject/call")))
			{
				return true;
			}

			TSharedPtr<FJsonObject> BodyObj;
			if (!BAT::Http::TryParseJsonBody(Request.Body, BodyObj) || !BodyObj.IsValid())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("bad_args"), TEXT("Invalid JSON body")));
				return true;
			}

			return DispatchAutomationCommandRoute(TEXT("/uobject/call"), Request, OnComplete, BodyObj);
		}));

	ActorSpawnRoute = Router->BindRoute(
		FHttpPath(TEXT("/actor/spawn")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, TEXT("/actor/spawn")))
			{
				return true;
			}

			TSharedPtr<FJsonObject> BodyObj;
			if (!BAT::Http::TryParseJsonBody(Request.Body, BodyObj) || !BodyObj.IsValid())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("bad_args"), TEXT("Invalid JSON body")));
				return true;
			}

			return DispatchAutomationCommandRoute(TEXT("/actor/spawn"), Request, OnComplete, BodyObj);
		}));

	ActorFindRoute = Router->BindRoute(
		FHttpPath(TEXT("/actor/find")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, TEXT("/actor/find")))
			{
				return true;
			}

			TSharedPtr<FJsonObject> BodyObj;
			if (!BAT::Http::TryParseJsonBody(Request.Body, BodyObj) || !BodyObj.IsValid())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("bad_args"), TEXT("Invalid JSON body")));
				return true;
			}

			FString By;
			FString Value;
			double LimitRaw = 50.0;
			BodyObj->TryGetStringField(TEXT("by"), By);
			BodyObj->TryGetStringField(TEXT("value"), Value);
			BodyObj->TryGetNumberField(TEXT("limit"), LimitRaw);
			const int32 Limit = FMath::Clamp((int32)LimitRaw, 1, 500);
			const FString RequestId = ResolveOrCreateRequestId(Request);

			AsyncTask(ENamedThreads::GameThread, [this, By, Value, Limit, RequestId, OnComplete]()
			{
				UWorld* EditorWorld = GetEditorWorld();
				if (!EditorWorld)
				{
					OnComplete(MakeErrorResponse(EHttpServerResponseCodes::NotFound, RequestId, TEXT("not_found"), TEXT("Editor world not available")));
					return;
				}

				const FString Mode = By.TrimStartAndEnd().ToLower();
				if (!(Mode == TEXT("tag") || Mode == TEXT("name") || Mode == TEXT("class")))
				{
					OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, RequestId, TEXT("bad_args"), TEXT("'by' must be one of: tag, name, class")));
					return;
				}

				TArray<TSharedPtr<FJsonValue>> Actors;
				for (TActorIterator<AActor> It(EditorWorld); It && Actors.Num() < Limit; ++It)
				{
					AActor* Actor = *It;
					if (!Actor)
					{
						continue;
					}

					bool bMatches = false;
					if (Mode == TEXT("tag"))
					{
						bMatches = Actor->ActorHasTag(FName(*Value));
					}
					else if (Mode == TEXT("name"))
					{
						bMatches = Actor->GetName().Equals(Value, ESearchCase::IgnoreCase);
#if WITH_EDITOR
						bMatches = bMatches || Actor->GetActorLabel().Equals(Value, ESearchCase::IgnoreCase);
#endif
					}
					else if (Mode == TEXT("class"))
					{
						const UClass* Class = Actor->GetClass();
						const FString ClassName = Class ? Class->GetName() : FString();
						const FString ClassPath = Class ? Class->GetPathName() : FString();
						const FString ClassScriptPath = Class ? Class->GetClassPathName().ToString() : FString();
						bMatches = ClassName.Equals(Value, ESearchCase::IgnoreCase)
							|| ClassPath.Equals(Value, ESearchCase::IgnoreCase)
							|| ClassScriptPath.Equals(Value, ESearchCase::IgnoreCase);
					}

					if (bMatches)
					{
						Actors.Add(MakeActorSummary(Actor));
					}
				}

				TSharedRef<FJsonObject> ResponseObj = MakeShared<FJsonObject>();
				ResponseObj->SetBoolField(TEXT("ok"), true);
				ResponseObj->SetStringField(TEXT("request_id"), RequestId);
				ResponseObj->SetArrayField(TEXT("actors"), Actors);
				OnComplete(BAT::Http::MakeJsonResponse(200, ResponseObj, RequestId));
			});

			return true;
		}));
}

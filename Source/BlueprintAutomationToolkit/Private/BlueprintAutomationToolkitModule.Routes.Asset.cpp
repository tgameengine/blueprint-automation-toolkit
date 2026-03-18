#include "BlueprintAutomationToolkitModule.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "AssetToolsModule.h"
#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Engine/World.h"
#include "Factories/AnimSequenceFactory.h"
#include "GameFramework/Actor.h"
#include "HttpPath.h"
#include "HttpResultCallback.h"
#include "Http/HttpRequestUtils.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"
#include "Modules/ModuleManager.h"
#include "Misc/PackageName.h"
#include "ObjectTools.h"
#include "PCGComponent.h"
#include "PCGGraph.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"

namespace
{
	struct FBATDuplicateRequestEntry
	{
		FString Src;
		FString Dst;
	};

	struct FBATAnimSequenceTrackRequest
	{
		FName BoneName;
		TArray<FVector> PosKeys;
		TArray<FQuat> RotKeys;
		TArray<FVector> ScaleKeys;
	};

	static FString NormalizeObjectPath(const FString& InPath)
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

	static void ApplyForwardAxisToVectorKeys(TArray<FVector>& Keys, const FQuat& AxisToUnrealQuat)
	{
		if (!AxisToUnrealQuat.Equals(FQuat::Identity))
		{
			for (FVector& Key : Keys)
			{
				Key = AxisToUnrealQuat.RotateVector(Key);
			}
		}
	}

	static void ApplyForwardAxisToQuatKeys(TArray<FQuat>& Keys, const FQuat& AxisToUnrealQuat)
	{
		if (!AxisToUnrealQuat.Equals(FQuat::Identity))
		{
			const FQuat AxisFromUnrealQuat = AxisToUnrealQuat.Inverse();
			for (FQuat& Key : Keys)
			{
				Key = (AxisToUnrealQuat * Key * AxisFromUnrealQuat).GetNormalized();
			}
		}
	}

	static bool SaveObjectPackage(UObject* ObjectToSave)
	{
		if (!ObjectToSave)
		{
			return false;
		}

		UPackage* Package = ObjectToSave->GetOutermost();
		if (!Package)
		{
			return false;
		}

		const FString PackageName = Package->GetName();
		if (!FPackageName::IsValidLongPackageName(PackageName))
		{
			return false;
		}

		const FString Filename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		Package->MarkPackageDirty();
		return UPackage::SavePackage(Package, ObjectToSave, *Filename, SaveArgs);
	}

	static bool TryParseVector(const TSharedPtr<FJsonValue>& JsonValue, FVector& OutVector)
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
		OutVector = FVector((float)Arr[0]->AsNumber(), (float)Arr[1]->AsNumber(), (float)Arr[2]->AsNumber());
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
		OutRotator = FRotator((float)Arr[0]->AsNumber(), (float)Arr[1]->AsNumber(), (float)Arr[2]->AsNumber());
		return true;
	}

	static bool TryParseQuat(const TSharedPtr<FJsonValue>& JsonValue, FQuat& OutQuat)
	{
		if (!JsonValue.IsValid() || JsonValue->Type != EJson::Array)
		{
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>& Arr = JsonValue->AsArray();
		if (Arr.Num() == 3)
		{
			OutQuat = FRotator((float)Arr[0]->AsNumber(), (float)Arr[1]->AsNumber(), (float)Arr[2]->AsNumber()).Quaternion();
			return true;
		}
		if (Arr.Num() == 4)
		{
			OutQuat = FQuat((float)Arr[0]->AsNumber(), (float)Arr[1]->AsNumber(), (float)Arr[2]->AsNumber(), (float)Arr[3]->AsNumber()).GetNormalized();
			return true;
		}

		return false;
	}

	static bool TryParseVectorKeyArray(const TSharedPtr<FJsonObject>& TrackObj, const TCHAR* FieldName, TArray<FVector>& OutKeys, FString& OutError)
	{
		OutKeys.Reset();
		const TArray<TSharedPtr<FJsonValue>>* KeyArray = nullptr;
		if (!TrackObj.IsValid() || !TrackObj->TryGetArrayField(FieldName, KeyArray) || !KeyArray)
		{
			return true;
		}

		for (const TSharedPtr<FJsonValue>& KeyValue : *KeyArray)
		{
			FVector ParsedValue = FVector::ZeroVector;
			if (!TryParseVector(KeyValue, ParsedValue))
			{
				OutError = FString::Printf(TEXT("track field '%s' must be an array of [x,y,z] vectors"), FieldName);
				return false;
			}
			OutKeys.Add(ParsedValue);
		}

		return true;
	}

	static bool TryParseQuatKeyArray(const TSharedPtr<FJsonObject>& TrackObj, const TCHAR* FieldName, TArray<FQuat>& OutKeys, FString& OutError)
	{
		OutKeys.Reset();
		const TArray<TSharedPtr<FJsonValue>>* KeyArray = nullptr;
		if (!TrackObj.IsValid() || !TrackObj->TryGetArrayField(FieldName, KeyArray) || !KeyArray)
		{
			return true;
		}

		for (const TSharedPtr<FJsonValue>& KeyValue : *KeyArray)
		{
			FQuat ParsedQuat = FQuat::Identity;
			if (!TryParseQuat(KeyValue, ParsedQuat))
			{
				OutError = FString::Printf(TEXT("track field '%s' must contain [pitch,yaw,roll] or [x,y,z,w] entries"), FieldName);
				return false;
			}
			OutKeys.Add(ParsedQuat);
		}

		return true;
	}

	static int32 GetTrackKeyCount(const TSharedPtr<FJsonObject>& TrackObj)
	{
		if (!TrackObj.IsValid())
		{
			return 0;
		}

		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		int32 Count = 0;
		if (TrackObj->TryGetArrayField(TEXT("position_keys"), Values) && Values)
		{
			Count = FMath::Max(Count, Values->Num());
		}
		if (TrackObj->TryGetArrayField(TEXT("rotation_keys"), Values) && Values)
		{
			Count = FMath::Max(Count, Values->Num());
		}
		if (TrackObj->TryGetArrayField(TEXT("scale_keys"), Values) && Values)
		{
			Count = FMath::Max(Count, Values->Num());
		}
		return Count;
	}

	static bool TryParseAnimSequenceTrack(const TSharedPtr<FJsonObject>& TrackObj, const int32 NumFrames, const FQuat& AxisToUnrealQuat, FBATAnimSequenceTrackRequest& OutTrack, FString& OutError)
	{
		OutError.Reset();
		OutTrack = FBATAnimSequenceTrackRequest();
		if (!TrackObj.IsValid())
		{
			OutError = TEXT("track entry must be an object");
			return false;
		}

		FString BoneNameString;
		if (!TrackObj->TryGetStringField(TEXT("bone_name"), BoneNameString) || BoneNameString.TrimStartAndEnd().IsEmpty())
		{
			OutError = TEXT("each track requires non-empty 'bone_name'");
			return false;
		}
		OutTrack.BoneName = FName(*BoneNameString);

		if (!TryParseVectorKeyArray(TrackObj, TEXT("position_keys"), OutTrack.PosKeys, OutError)
			|| !TryParseQuatKeyArray(TrackObj, TEXT("rotation_keys"), OutTrack.RotKeys, OutError)
			|| !TryParseVectorKeyArray(TrackObj, TEXT("scale_keys"), OutTrack.ScaleKeys, OutError))
		{
			return false;
		}

		if (OutTrack.PosKeys.Num() == 0)
		{
			OutTrack.PosKeys.Init(FVector::ZeroVector, NumFrames);
		}
		else if (OutTrack.PosKeys.Num() != NumFrames)
		{
			OutError = FString::Printf(TEXT("track field 'position_keys' must contain exactly %d keys"), NumFrames);
			return false;
		}

		if (OutTrack.RotKeys.Num() == 0)
		{
			OutTrack.RotKeys.Init(FQuat::Identity, NumFrames);
		}
		else if (OutTrack.RotKeys.Num() != NumFrames)
		{
			OutError = FString::Printf(TEXT("track field 'rotation_keys' must contain exactly %d keys"), NumFrames);
			return false;
		}

		if (OutTrack.ScaleKeys.Num() == 0)
		{
			OutTrack.ScaleKeys.Init(FVector(1.0f, 1.0f, 1.0f), NumFrames);
		}
		else if (OutTrack.ScaleKeys.Num() != NumFrames)
		{
			OutError = FString::Printf(TEXT("track field 'scale_keys' must contain exactly %d keys"), NumFrames);
			return false;
		}

		ApplyForwardAxisToVectorKeys(OutTrack.PosKeys, AxisToUnrealQuat);
		ApplyForwardAxisToQuatKeys(OutTrack.RotKeys, AxisToUnrealQuat);

		return true;
	}

	static void ApplySkeletalMeshSocketFields(
		USkeletalMeshSocket* Socket,
		const FName SocketName,
		const FName BoneName,
		const FVector& RelativeLocation,
		const FRotator& RelativeRotation,
		const FVector& RelativeScale,
		const bool bForceAlwaysAnimated)
	{
		if (!Socket)
		{
			return;
		}

		Socket->SocketName = SocketName;
		Socket->BoneName = BoneName;
		Socket->RelativeLocation = RelativeLocation;
		Socket->RelativeRotation = RelativeRotation;
		Socket->RelativeScale = RelativeScale;
		Socket->bForceAlwaysAnimated = bForceAlwaysAnimated;
	}

	static bool TrySetNumericProp(UObject* Obj, const FName PropName, double Value)
	{
		if (!Obj)
		{
			return false;
		}
		FProperty* Property = Obj->GetClass()->FindPropertyByName(PropName);
		FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property);
		if (!NumericProperty)
		{
			return false;
		}

		void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Obj);
		if (NumericProperty->IsInteger())
		{
			NumericProperty->SetIntPropertyValue(ValuePtr, (int64)Value);
		}
		else
		{
			NumericProperty->SetFloatingPointPropertyValue(ValuePtr, Value);
		}
		return true;
	}

	static bool TryCollectDuplicateRequests(const TSharedPtr<FJsonObject>& BodyObj, TArray<FBATDuplicateRequestEntry>& OutEntries)
	{
		OutEntries.Reset();
		if (!BodyObj.IsValid())
		{
			return false;
		}

		FString Src;
		FString Dst;
		if (BodyObj->TryGetStringField(TEXT("src"), Src) && BodyObj->TryGetStringField(TEXT("dst"), Dst))
		{
			Src.TrimStartAndEndInline();
			Dst.TrimStartAndEndInline();
			if (!Src.IsEmpty() && !Dst.IsEmpty())
			{
				FBATDuplicateRequestEntry& Entry = OutEntries.AddDefaulted_GetRef();
				Entry.Src = MoveTemp(Src);
				Entry.Dst = MoveTemp(Dst);
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* DuplicatesField = nullptr;
		if (BodyObj->TryGetArrayField(TEXT("duplicates"), DuplicatesField) && DuplicatesField)
		{
			for (const TSharedPtr<FJsonValue>& EntryValue : *DuplicatesField)
			{
				if (!EntryValue.IsValid() || EntryValue->Type != EJson::Object)
				{
					continue;
				}

				const TSharedPtr<FJsonObject> EntryObj = EntryValue->AsObject();
				FString EntrySrc;
				FString EntryDst;
				if (!EntryObj.IsValid()
					|| !EntryObj->TryGetStringField(TEXT("src"), EntrySrc)
					|| !EntryObj->TryGetStringField(TEXT("dst"), EntryDst))
				{
					continue;
				}

				EntrySrc.TrimStartAndEndInline();
				EntryDst.TrimStartAndEndInline();
				if (EntrySrc.IsEmpty() || EntryDst.IsEmpty())
				{
					continue;
				}

				FBATDuplicateRequestEntry& Entry = OutEntries.AddDefaulted_GetRef();
				Entry.Src = MoveTemp(EntrySrc);
				Entry.Dst = MoveTemp(EntryDst);
			}
		}

		return OutEntries.Num() > 0;
	}

}

void FBlueprintAutomationToolkitModule::BindAssetRoutes()
{
	BindAssetDuplicateRoute();

	BindAssetSaveRoute();

	BindAssetCreateRoute();
}

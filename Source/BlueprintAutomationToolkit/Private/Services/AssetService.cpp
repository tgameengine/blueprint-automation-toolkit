#include "Services/AssetService.h"

#include "BlueprintAutomationToolkitModule.h"
#include "Domain/Requests/AssetSaveRequest.h"

#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Factories/AnimSequenceFactory.h"
#include "Modules/ModuleManager.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"

namespace
{
	struct FBATAnimSequenceTrackRequest
	{
		FName BoneName;
		TArray<FVector> PosKeys;
		TArray<FQuat> RotKeys;
		TArray<FVector> ScaleKeys;
	};

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

	static bool TryParseForwardAxisVector(const FString& InAxis, FVector& OutForwardVector, FString& OutError)
	{
		FString Axis = InAxis;
		Axis.TrimStartAndEndInline();
		Axis.ToUpperInline();

		if (Axis.IsEmpty() || Axis.Equals(TEXT("X"), ESearchCase::CaseSensitive))
		{
			OutForwardVector = FVector::ForwardVector;
			return true;
		}
		if (Axis.Equals(TEXT("Y"), ESearchCase::CaseSensitive))
		{
			OutForwardVector = FVector::RightVector;
			return true;
		}
		if (Axis.Equals(TEXT("Z"), ESearchCase::CaseSensitive))
		{
			OutForwardVector = FVector::UpVector;
			return true;
		}

		OutError = TEXT("'forward_axis' must be one of 'X', 'Y', or 'Z'");
		return false;
	}

	static bool TryBuildForwardAxisToUnrealQuat(const FString& InAxis, FQuat& OutQuat, FString& OutError)
	{
		OutQuat = FQuat::Identity;
		OutError.Reset();

		FVector SourceForwardVector = FVector::ForwardVector;
		if (!TryParseForwardAxisVector(InAxis, SourceForwardVector, OutError))
		{
			return false;
		}

		if (SourceForwardVector.Equals(FVector::ForwardVector))
		{
			return true;
		}

		OutQuat = FQuat::FindBetweenNormals(SourceForwardVector, FVector::ForwardVector);
		return true;
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
}

FAutomationResult FAssetService::CreateAsset(FBlueprintAutomationToolkitModule& Module, const FBATAssetCreateRequest& Request) const
{
	if (Request.ClassPath.IsEmpty())
	{
		return FAutomationResult::Error(TEXT("bad_args"), TEXT("Invalid asset class"), 400);
	}

	UClass* AssetClass = FindObject<UClass>(nullptr, *Request.ClassPath);
	if (!AssetClass)
	{
		AssetClass = LoadObject<UClass>(nullptr, *Request.ClassPath);
	}
	if (!AssetClass || AssetClass->HasAnyClassFlags(CLASS_Abstract))
	{
		return FAutomationResult::Error(TEXT("bad_args"), TEXT("Invalid asset class"), 400);
	}

	const TSharedPtr<FJsonObject>& BodyObj = Request.Body;
	if (!BodyObj.IsValid())
	{
		return FAutomationResult::Error(TEXT("bad_args"), TEXT("Invalid JSON body"), 400);
	}

	if (AssetClass->IsChildOf(USkeletalMeshSocket::StaticClass()))
	{
		const FString OuterObjectPath = NormalizeAssetObjectPath(Request.OuterPath);
		if (OuterObjectPath.IsEmpty())
		{
			return FAutomationResult::Error(TEXT("bad_args"), TEXT("SkeletalMeshSocket creation requires 'outer' SkeletalMesh object path"), 400);
		}

		USkeletalMesh* SkeletalMesh = LoadObject<USkeletalMesh>(nullptr, *OuterObjectPath);
		if (!SkeletalMesh)
		{
			return FAutomationResult::Error(TEXT("bad_args"), TEXT("'outer' must resolve to a SkeletalMesh asset"), 400);
		}

		FString SocketNameString;
		FString BoneNameString;
		BodyObj->TryGetStringField(TEXT("socket_name"), SocketNameString);
		BodyObj->TryGetStringField(TEXT("bone_name"), BoneNameString);
		SocketNameString.TrimStartAndEndInline();
		BoneNameString.TrimStartAndEndInline();
		if (SocketNameString.IsEmpty() || BoneNameString.IsEmpty())
		{
			return FAutomationResult::Error(TEXT("bad_args"), TEXT("SkeletalMeshSocket creation requires 'socket_name' and 'bone_name'"), 400);
		}

		const FName SocketName(*SocketNameString);
		const FName BoneName(*BoneNameString);
		if (SkeletalMesh->GetRefSkeleton().FindBoneIndex(BoneName) == INDEX_NONE)
		{
			return FAutomationResult::Error(TEXT("bad_args"), TEXT("'bone_name' does not exist on the SkeletalMesh"), 400);
		}

		FVector RelativeLocation = FVector::ZeroVector;
		FRotator RelativeRotation = FRotator::ZeroRotator;
		FVector RelativeScale = FVector(1.0f, 1.0f, 1.0f);
		bool bForceAlwaysAnimated = false;
		const TArray<TSharedPtr<FJsonValue>>* VectorField = nullptr;
		if (BodyObj->TryGetArrayField(TEXT("relative_location"), VectorField))
		{
			TryParseVector(MakeShared<FJsonValueArray>(*VectorField), RelativeLocation);
		}
		if (BodyObj->TryGetArrayField(TEXT("relative_rotation"), VectorField))
		{
			TryParseRotator(MakeShared<FJsonValueArray>(*VectorField), RelativeRotation);
		}
		if (BodyObj->TryGetArrayField(TEXT("relative_scale"), VectorField))
		{
			TryParseVector(MakeShared<FJsonValueArray>(*VectorField), RelativeScale);
		}
		BodyObj->TryGetBoolField(TEXT("force_always_animated"), bForceAlwaysAnimated);

		const bool bAddToSkeleton = false;
		USkeletalMeshSocket* ExistingSocket = SkeletalMesh->FindSocket(SocketName);
		bool bCreated = false;
		bool bUpdated = false;
		if (ExistingSocket)
		{
			if (ExistingSocket->GetOuter() != SkeletalMesh)
			{
				return FAutomationResult::Error(TEXT("socket_conflict"), TEXT("A socket with that name already exists on the shared skeleton; choose a different mesh socket name"), 409);
			}

			ExistingSocket->Modify();
			ApplySkeletalMeshSocketFields(ExistingSocket, SocketName, BoneName, RelativeLocation, RelativeRotation, RelativeScale, bForceAlwaysAnimated);
			ExistingSocket->PostEditChange();
			bUpdated = true;
		}
		else
		{
			USkeletalMeshSocket* NewSocket = NewObject<USkeletalMeshSocket>(SkeletalMesh, USkeletalMeshSocket::StaticClass(), NAME_None, RF_Transactional);
			ApplySkeletalMeshSocketFields(NewSocket, SocketName, BoneName, RelativeLocation, RelativeRotation, RelativeScale, bForceAlwaysAnimated);
			SkeletalMesh->AddSocket(NewSocket, bAddToSkeleton);
			if (NewSocket->GetOuter() != SkeletalMesh || !SkeletalMesh->FindSocket(SocketName) || SkeletalMesh->FindSocket(SocketName)->GetOuter() != SkeletalMesh)
			{
				return FAutomationResult::Error(TEXT("create_failed"), TEXT("Failed to create SkeletalMesh socket"), 500);
			}
			bCreated = true;
		}

		SkeletalMesh->Modify();
		SkeletalMesh->MarkPackageDirty();
		SkeletalMesh->PostEditChange();

		TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("outer"), OuterObjectPath);
		Data->SetStringField(TEXT("socket_name"), SocketName.ToString());
		Data->SetStringField(TEXT("bone_name"), BoneName.ToString());
		Data->SetBoolField(TEXT("created"), bCreated);
		Data->SetBoolField(TEXT("updated"), bUpdated);
		return FAutomationResult::Ok(MakeShared<FJsonValueObject>(Data));
	}

	if (AssetClass->IsChildOf(UAnimSequence::StaticClass()))
	{
		if (!FPackageName::IsValidLongPackageName(Request.AssetPath))
		{
			return FAutomationResult::Error(TEXT("bad_args"), TEXT("'path' must be a long package path"), 400);
		}

		FString SkeletonPath;
		if (!BodyObj->TryGetStringField(TEXT("skeleton"), SkeletonPath) || SkeletonPath.TrimStartAndEnd().IsEmpty())
		{
			return FAutomationResult::Error(TEXT("bad_args"), TEXT("AnimSequence creation requires 'skeleton'"), 400);
		}

		USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *NormalizeAssetObjectPath(SkeletonPath));
		if (!Skeleton)
		{
			return FAutomationResult::Error(TEXT("bad_args"), TEXT("'skeleton' must resolve to a Skeleton asset"), 400);
		}

		USkeletalMesh* PreviewMesh = nullptr;
		FString PreviewMeshPath;
		if (BodyObj->TryGetStringField(TEXT("preview_mesh"), PreviewMeshPath) && !PreviewMeshPath.TrimStartAndEnd().IsEmpty())
		{
			PreviewMesh = LoadObject<USkeletalMesh>(nullptr, *NormalizeAssetObjectPath(PreviewMeshPath));
			if (!PreviewMesh)
			{
				return FAutomationResult::Error(TEXT("bad_args"), TEXT("'preview_mesh' must resolve to a SkeletalMesh asset"), 400);
			}
		}

		double FrameRateValue = 30.0;
		BodyObj->TryGetNumberField(TEXT("frame_rate"), FrameRateValue);
		const int32 FrameRate = FMath::Max(1, FMath::RoundToInt(FrameRateValue));

		FString ForwardAxis;
		BodyObj->TryGetStringField(TEXT("forward_axis"), ForwardAxis);
		FQuat AxisToUnrealQuat = FQuat::Identity;
		FString AxisError;
		if (!TryBuildForwardAxisToUnrealQuat(ForwardAxis, AxisToUnrealQuat, AxisError))
		{
			return FAutomationResult::Error(TEXT("bad_args"), AxisError, 400);
		}

		int32 NumFrames = 0;
		if (BodyObj->HasTypedField<EJson::Number>(TEXT("number_of_frames")))
		{
			double NumFramesValue = 0.0;
			BodyObj->TryGetNumberField(TEXT("number_of_frames"), NumFramesValue);
			NumFrames = FMath::RoundToInt(NumFramesValue);
		}

		const TArray<TSharedPtr<FJsonValue>>* TrackValues = nullptr;
		BodyObj->TryGetArrayField(TEXT("tracks"), TrackValues);
		if ((!TrackValues || TrackValues->Num() == 0) && NumFrames <= 0)
		{
			NumFrames = 1;
		}

		if (TrackValues && TrackValues->Num() > 0 && NumFrames <= 0)
		{
			for (const TSharedPtr<FJsonValue>& TrackValue : *TrackValues)
			{
				if (!TrackValue.IsValid() || TrackValue->Type != EJson::Object)
				{
					return FAutomationResult::Error(TEXT("bad_args"), TEXT("'tracks' entries must be objects"), 400);
				}
				NumFrames = FMath::Max(NumFrames, GetTrackKeyCount(TrackValue->AsObject()));
			}
			NumFrames = FMath::Max(NumFrames, 1);
		}

		if (NumFrames <= 0)
		{
			return FAutomationResult::Error(TEXT("bad_args"), TEXT("'number_of_frames' must be >= 1"), 400);
		}

		TArray<FBATAnimSequenceTrackRequest> ParsedTracks;
		if (TrackValues)
		{
			for (const TSharedPtr<FJsonValue>& TrackValue : *TrackValues)
			{
				if (!TrackValue.IsValid() || TrackValue->Type != EJson::Object)
				{
					return FAutomationResult::Error(TEXT("bad_args"), TEXT("'tracks' entries must be objects"), 400);
				}

				FBATAnimSequenceTrackRequest TrackRequest;
				FString TrackError;
				if (!TryParseAnimSequenceTrack(TrackValue->AsObject(), NumFrames, AxisToUnrealQuat, TrackRequest, TrackError))
				{
					return FAutomationResult::Error(TEXT("bad_args"), TrackError, 400);
				}

				if (Skeleton->GetReferenceSkeleton().FindBoneIndex(TrackRequest.BoneName) == INDEX_NONE)
				{
					return FAutomationResult::Error(TEXT("bad_args"), FString::Printf(TEXT("track bone does not exist on skeleton: %s"), *TrackRequest.BoneName.ToString()), 400);
				}

				ParsedTracks.Add(MoveTemp(TrackRequest));
			}
		}

		const FString AssetName = FPackageName::GetLongPackageAssetName(Request.AssetPath);
		UPackage* Package = CreatePackage(*Request.AssetPath);
		if (!Package)
		{
			return FAutomationResult::Error(TEXT("create_failed"), TEXT("Failed to create package"), 500);
		}

		UAnimSequenceFactory* Factory = NewObject<UAnimSequenceFactory>();
		Factory->TargetSkeleton = Skeleton;
		Factory->PreviewSkeletalMesh = PreviewMesh;

		UAnimSequence* AnimSequence = Cast<UAnimSequence>(Factory->FactoryCreateNew(UAnimSequence::StaticClass(), Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional, nullptr, GWarn));
		if (!AnimSequence)
		{
			return FAutomationResult::Error(TEXT("create_failed"), TEXT("Failed to create AnimSequence asset"), 500);
		}

		AnimSequence->Modify();
		AnimSequence->SetSkeleton(Skeleton);
		if (PreviewMesh)
		{
			AnimSequence->SetPreviewMesh(PreviewMesh);
		}

		IAnimationDataController& Controller = AnimSequence->GetController();
		{
			IAnimationDataController::FScopedBracket ScopedBracket(Controller, FText::FromString(TEXT("BAT Create AnimSequence")), true);
			Controller.InitializeModel();
			Controller.SetFrameRate(FFrameRate(FrameRate, 1), false);
			Controller.SetNumberOfFrames(FFrameNumber(NumFrames), false);

			for (const FBATAnimSequenceTrackRequest& TrackRequest : ParsedTracks)
			{
				Controller.AddBoneCurve(TrackRequest.BoneName, false);
				Controller.SetBoneTrackKeys(TrackRequest.BoneName, TrackRequest.PosKeys, TrackRequest.RotKeys, TrackRequest.ScaleKeys, false);
			}

			Controller.NotifyPopulated();
		}

		FAssetRegistryModule::AssetCreated(AnimSequence);
		Package->MarkPackageDirty();
		if (Request.bSave && !SaveObjectPackage(AnimSequence))
		{
			return FAutomationResult::Error(TEXT("save_failed"), TEXT("Failed to save AnimSequence asset"), 500);
		}

		TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("path"), Request.AssetPath);
		Data->SetStringField(TEXT("skeleton"), NormalizeAssetObjectPath(SkeletonPath));
		if (PreviewMesh)
		{
			Data->SetStringField(TEXT("preview_mesh"), NormalizeAssetObjectPath(PreviewMeshPath));
		}
		Data->SetNumberField(TEXT("frame_rate"), FrameRate);
		Data->SetNumberField(TEXT("number_of_frames"), NumFrames);
		Data->SetNumberField(TEXT("tracks"), ParsedTracks.Num());
		if (!ForwardAxis.TrimStartAndEnd().IsEmpty())
		{
			ForwardAxis.TrimStartAndEndInline();
			ForwardAxis.ToUpperInline();
			Data->SetStringField(TEXT("forward_axis"), ForwardAxis);
		}
		Data->SetBoolField(TEXT("saved"), Request.bSave);
		return FAutomationResult::Ok(MakeShared<FJsonValueObject>(Data));
	}

	if (!FPackageName::IsValidLongPackageName(Request.AssetPath))
	{
		return FAutomationResult::Error(TEXT("bad_args"), TEXT("'path' must be a long package path"), 400);
	}

	const FString AssetName = FPackageName::GetLongPackageAssetName(Request.AssetPath);
	UPackage* Package = CreatePackage(*Request.AssetPath);
	if (!Package)
	{
		return FAutomationResult::Error(TEXT("create_failed"), TEXT("Failed to create package"), 500);
	}

	UObject* NewAsset = NewObject<UObject>(Package, AssetClass, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
	if (!NewAsset)
	{
		return FAutomationResult::Error(TEXT("create_failed"), TEXT("Failed to create asset"), 500);
	}

	FAssetRegistryModule::AssetCreated(NewAsset);
	Package->MarkPackageDirty();
	if (Request.bSave && !SaveObjectPackage(NewAsset))
	{
		return FAutomationResult::Error(TEXT("save_failed"), TEXT("Failed to save asset"), 500);
	}

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("path"), Request.AssetPath);
	Data->SetStringField(TEXT("class"), AssetClass->GetPathName());
	Data->SetBoolField(TEXT("saved"), Request.bSave);
	return FAutomationResult::Ok(MakeShared<FJsonValueObject>(Data));
}

FAutomationResult FAssetService::DuplicateAssets(FBlueprintAutomationToolkitModule& Module, const FBATAssetDuplicateRequest& Request) const
{
	if (Request.Entries.Num() == 0)
	{
		return FAutomationResult::Error(TEXT("bad_args"), TEXT("Body must include 'src'/'dst' or non-empty 'duplicates' array."), 400);
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	int32 CreatedCount = 0;
	int32 ExistingCount = 0;
	int32 SavedCount = 0;
	TArray<TSharedPtr<FJsonValue>> ResultEntries;
	ResultEntries.Reserve(Request.Entries.Num());

	for (const FBATAssetDuplicateEntry& Entry : Request.Entries)
	{
		const FString SourceObjectPath = NormalizeAssetObjectPath(Entry.SourcePath);
		UObject* SourceObject = LoadObject<UObject>(nullptr, *SourceObjectPath);
		if (!SourceObject)
		{
			return FAutomationResult::Error(TEXT("asset_not_found"), FString::Printf(TEXT("Source asset could not be loaded: %s"), *Entry.SourcePath), 404);
		}

		FString DestinationPackagePath = Entry.DestinationPath;
		DestinationPackagePath.TrimStartAndEndInline();
		if (!FPackageName::IsValidLongPackageName(DestinationPackagePath))
		{
			return FAutomationResult::Error(TEXT("bad_args"), FString::Printf(TEXT("Invalid destination package path: %s"), *Entry.DestinationPath), 400);
		}

		const FString DestinationObjectPath = NormalizeAssetObjectPath(DestinationPackagePath);
		UObject* ResultObject = LoadObject<UObject>(nullptr, *DestinationObjectPath);
		bool bCreated = false;
		if (!ResultObject)
		{
			const FString AssetName = FPackageName::GetLongPackageAssetName(DestinationPackagePath);
			const FString AssetPath = FPackageName::GetLongPackagePath(DestinationPackagePath);
			ResultObject = AssetToolsModule.Get().DuplicateAsset(AssetName, AssetPath, SourceObject);
			if (!ResultObject)
			{
				return FAutomationResult::Error(TEXT("duplicate_failed"), FString::Printf(TEXT("Asset duplication failed: %s -> %s"), *Entry.SourcePath, *Entry.DestinationPath), 500);
			}
			bCreated = true;
			++CreatedCount;
		}
		else
		{
			++ExistingCount;
		}

		if (Request.bSave)
		{
			if (!SaveObjectPackage(ResultObject))
			{
				return FAutomationResult::Error(TEXT("save_failed"), FString::Printf(TEXT("Failed to save duplicated asset: %s"), *DestinationObjectPath), 500);
			}
			++SavedCount;
		}

		TSharedRef<FJsonObject> EntryObject = MakeShared<FJsonObject>();
		EntryObject->SetStringField(TEXT("src"), Entry.SourcePath);
		EntryObject->SetStringField(TEXT("dst"), DestinationPackagePath);
		EntryObject->SetBoolField(TEXT("created"), bCreated);
		EntryObject->SetBoolField(TEXT("saved"), Request.bSave);
		ResultEntries.Add(MakeShared<FJsonValueObject>(EntryObject));
	}

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("createdCount"), CreatedCount);
	Data->SetNumberField(TEXT("existingCount"), ExistingCount);
	Data->SetNumberField(TEXT("savedCount"), SavedCount);
	if (Request.Entries.Num() == 1)
	{
		Data->SetStringField(TEXT("target"), Request.Entries[0].DestinationPath);
	}
	Data->SetArrayField(TEXT("duplicates"), ResultEntries);
	return FAutomationResult::Ok(MakeShared<FJsonValueObject>(Data));
}

FAutomationResult FAssetService::SaveAssets(FBlueprintAutomationToolkitModule& Module, const FBATAssetSaveRequest& Request) const
{
	if (Request.Paths.Num() == 0)
	{
		return FAutomationResult::Error(TEXT("bad_args"), TEXT("Body must include 'path', 'target', or non-empty 'paths' array."), 400);
	}

	int32 SavedCount = 0;
	TArray<TSharedPtr<FJsonValue>> SavedAssets;
	SavedAssets.Reserve(Request.Paths.Num());

	for (const FString& RequestedPath : Request.Paths)
	{
		const FString ObjectPath = NormalizeAssetObjectPath(RequestedPath);
		if (ObjectPath.IsEmpty())
		{
			return FAutomationResult::Error(TEXT("bad_args"), TEXT("Asset paths must be non-empty strings."), 400);
		}

		UObject* Asset = LoadObject<UObject>(nullptr, *ObjectPath);
		if (!Asset)
		{
			return FAutomationResult::Error(TEXT("asset_not_found"), FString::Printf(TEXT("Asset not found: %s"), *ObjectPath), 404);
		}

		if (!SaveObjectPackage(Asset))
		{
			return FAutomationResult::Error(TEXT("save_failed"), FString::Printf(TEXT("Failed to save asset: %s"), *ObjectPath), 500);
		}

		++SavedCount;
		SavedAssets.Add(MakeShared<FJsonValueString>(ObjectPath));
	}

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("savedCount"), SavedCount);
	Data->SetArrayField(TEXT("savedAssets"), SavedAssets);
	if (SavedAssets.Num() == 1)
	{
		Data->SetStringField(TEXT("target"), SavedAssets[0]->AsString());
	}
	Data->SetArrayField(TEXT("errors"), TArray<TSharedPtr<FJsonValue>>());
	return FAutomationResult::Ok(MakeShared<FJsonValueObject>(Data));
}
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

	static bool TryCollectAssetPaths(const TSharedPtr<FJsonObject>& BodyObj, TArray<FString>& OutPaths)
	{
		OutPaths.Reset();
		if (!BodyObj.IsValid())
		{
			return false;
		}

		FString SinglePath;
		if (BodyObj->TryGetStringField(TEXT("path"), SinglePath))
		{
			SinglePath.TrimStartAndEndInline();
			if (!SinglePath.IsEmpty())
			{
				OutPaths.Add(MoveTemp(SinglePath));
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* PathsField = nullptr;
		if (BodyObj->TryGetArrayField(TEXT("paths"), PathsField) && PathsField)
		{
			for (const TSharedPtr<FJsonValue>& PathValue : *PathsField)
			{
				if (!PathValue.IsValid() || PathValue->Type != EJson::String)
				{
					continue;
				}

				FString Path = PathValue->AsString();
				Path.TrimStartAndEndInline();
				if (!Path.IsEmpty())
				{
					OutPaths.Add(MoveTemp(Path));
				}
			}
		}

		return OutPaths.Num() > 0;
	}
}

void FBlueprintAutomationToolkitModule::BindAssetRoutes()
{
	AssetDuplicateRoute = Router->BindRoute(
		FHttpPath(TEXT("/asset/duplicate")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, TEXT("/asset/duplicate")))
			{
				return true;
			}

			TSharedPtr<FJsonObject> BodyObj;
			if (!BAT::Http::TryParseJsonBody(Request.Body, BodyObj) || !BodyObj.IsValid())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("bad_args"), TEXT("Invalid JSON body")));
				return true;
			}

			FString Src;
			FString Dst;
			TArray<FBATDuplicateRequestEntry> DuplicateRequests;
			const bool bSave = BodyObj->HasTypedField<EJson::Boolean>(TEXT("save")) ? BodyObj->GetBoolField(TEXT("save")) : false;
			if (!TryCollectDuplicateRequests(BodyObj, DuplicateRequests))
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("bad_args"), TEXT("Body must include 'src'/'dst' or non-empty 'duplicates' array")));
				return true;
			}
			const FString RequestId = ResolveOrCreateRequestId(Request);

			AsyncTask(ENamedThreads::GameThread, [this, DuplicateRequests, bSave, RequestId, OnComplete]()
			{
				FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
				int32 CreatedCount = 0;
				int32 ExistingCount = 0;
				int32 SavedCount = 0;
				TArray<TSharedPtr<FJsonValue>> ResultEntries;
				ResultEntries.Reserve(DuplicateRequests.Num());

				for (const FBATDuplicateRequestEntry& DuplicateRequest : DuplicateRequests)
				{
					UObject* SourceObject = LoadObject<UObject>(nullptr, *NormalizeObjectPath(DuplicateRequest.Src));
					if (!SourceObject)
					{
						OnComplete(MakeErrorResponse(EHttpServerResponseCodes::NotFound, RequestId, TEXT("asset_not_found"), FString::Printf(TEXT("Source asset could not be loaded: %s"), *DuplicateRequest.Src)));
						return;
					}

					FString DstPackagePath = DuplicateRequest.Dst;
					DstPackagePath.TrimStartAndEndInline();
					if (!FPackageName::IsValidLongPackageName(DstPackagePath))
					{
						OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, RequestId, TEXT("bad_args"), FString::Printf(TEXT("Invalid destination package path: %s"), *DuplicateRequest.Dst)));
						return;
					}

					const FString DstObjectPath = NormalizeObjectPath(DstPackagePath);
					UObject* ResultObject = LoadObject<UObject>(nullptr, *DstObjectPath);
					bool bCreated = false;
					if (!ResultObject)
					{
						const FString AssetName = FPackageName::GetLongPackageAssetName(DstPackagePath);
						const FString AssetPath = FPackageName::GetLongPackagePath(DstPackagePath);
						ResultObject = AssetToolsModule.Get().DuplicateAsset(AssetName, AssetPath, SourceObject);
						if (!ResultObject)
						{
							OnComplete(MakeErrorResponse(EHttpServerResponseCodes::ServerError, RequestId, TEXT("duplicate_failed"), FString::Printf(TEXT("Asset duplication failed: %s -> %s"), *DuplicateRequest.Src, *DuplicateRequest.Dst)));
							return;
						}
						bCreated = true;
						++CreatedCount;
					}
					else
					{
						++ExistingCount;
					}

					if (bSave)
					{
						if (!SaveObjectPackage(ResultObject))
						{
							OnComplete(MakeErrorResponse(EHttpServerResponseCodes::ServerError, RequestId, TEXT("save_failed"), FString::Printf(TEXT("Failed to save duplicated asset: %s"), *DstObjectPath)));
							return;
						}
						++SavedCount;
					}

					TSharedRef<FJsonObject> EntryObj = MakeShared<FJsonObject>();
					EntryObj->SetStringField(TEXT("src"), DuplicateRequest.Src);
					EntryObj->SetStringField(TEXT("dst"), DstPackagePath);
					EntryObj->SetBoolField(TEXT("created"), bCreated);
					EntryObj->SetBoolField(TEXT("saved"), bSave);
					ResultEntries.Add(MakeShared<FJsonValueObject>(EntryObj));
				}

				TSharedRef<FJsonObject> ResponseObj = MakeShared<FJsonObject>();
				ResponseObj->SetBoolField(TEXT("ok"), true);
				ResponseObj->SetStringField(TEXT("request_id"), RequestId);
				ResponseObj->SetNumberField(TEXT("created"), CreatedCount);
				ResponseObj->SetNumberField(TEXT("existing"), ExistingCount);
				ResponseObj->SetNumberField(TEXT("saved"), SavedCount);
				if (DuplicateRequests.Num() == 1)
				{
					ResponseObj->SetStringField(TEXT("path"), DuplicateRequests[0].Dst);
				}
				ResponseObj->SetArrayField(TEXT("duplicates"), ResultEntries);
				OnComplete(BAT::Http::MakeJsonResponse(200, ResponseObj, RequestId));
			});
			return true;
		}));

	AssetDeleteRoute = Router->BindRoute(
		FHttpPath(TEXT("/asset/delete")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, TEXT("/asset/delete")))
			{
				return true;
			}

			TSharedPtr<FJsonObject> BodyObj;
			if (!BAT::Http::TryParseJsonBody(Request.Body, BodyObj) || !BodyObj.IsValid())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("bad_args"), TEXT("Invalid JSON body")));
				return true;
			}

			TArray<FString> RequestedPaths;
			if (!TryCollectAssetPaths(BodyObj, RequestedPaths))
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("bad_args"), TEXT("Body must include 'path' or non-empty 'paths' array")));
				return true;
			}

			const bool bForceDelete = BodyObj->HasTypedField<EJson::Boolean>(TEXT("force")) ? BodyObj->GetBoolField(TEXT("force")) : false;

			const FString RequestId = ResolveOrCreateRequestId(Request);

			AsyncTask(ENamedThreads::GameThread, [this, RequestedPaths, bForceDelete, RequestId, OnComplete]()
			{
				TArray<FAssetData> AssetsToDelete;
				AssetsToDelete.Reserve(RequestedPaths.Num());
				TArray<UObject*> ForceDeleteObjects;
				ForceDeleteObjects.Reserve(RequestedPaths.Num());
				TArray<TSharedPtr<FJsonValue>> DeletedEntries;
				DeletedEntries.Reserve(RequestedPaths.Num());

				for (const FString& RequestedPath : RequestedPaths)
				{
					const FString ObjectPath = NormalizeObjectPath(RequestedPath);
					UObject* Asset = LoadObject<UObject>(nullptr, *ObjectPath);
					if (!Asset)
					{
						OnComplete(MakeErrorResponse(EHttpServerResponseCodes::NotFound, RequestId, TEXT("asset_not_found"), FString::Printf(TEXT("Asset not found: %s"), *ObjectPath)));
						return;
					}

					const FAssetData AssetData(Asset);
					if (!AssetData.IsValid())
					{
						OnComplete(MakeErrorResponse(EHttpServerResponseCodes::ServerError, RequestId, TEXT("delete_failed"), FString::Printf(TEXT("Failed to resolve asset metadata: %s"), *ObjectPath)));
						return;
					}

					AssetsToDelete.Add(AssetData);
					ForceDeleteObjects.Add(Asset);

					TSharedRef<FJsonObject> EntryObj = MakeShared<FJsonObject>();
					EntryObj->SetStringField(TEXT("path"), RequestedPath);
					EntryObj->SetStringField(TEXT("object_path"), ObjectPath);
					DeletedEntries.Add(MakeShared<FJsonValueObject>(EntryObj));
				}

				const int32 DeletedCount = bForceDelete
					? ObjectTools::ForceDeleteObjects(ForceDeleteObjects, false)
					: ObjectTools::DeleteAssets(AssetsToDelete, false);
				if (DeletedCount != AssetsToDelete.Num())
				{
					const FString FailureMessage = bForceDelete
						? FString::Printf(TEXT("Force deleted %d of %d requested assets"), DeletedCount, AssetsToDelete.Num())
						: FString::Printf(TEXT("Deleted %d of %d requested assets"), DeletedCount, AssetsToDelete.Num());
					OnComplete(MakeErrorResponse(EHttpServerResponseCodes::Conflict, RequestId, TEXT("delete_failed"), FailureMessage));
					return;
				}

				TSharedRef<FJsonObject> ResponseObj = MakeShared<FJsonObject>();
				ResponseObj->SetBoolField(TEXT("ok"), true);
				ResponseObj->SetStringField(TEXT("request_id"), RequestId);
				ResponseObj->SetNumberField(TEXT("deleted"), DeletedCount);
				ResponseObj->SetBoolField(TEXT("force"), bForceDelete);
				if (RequestedPaths.Num() == 1)
				{
					ResponseObj->SetStringField(TEXT("path"), RequestedPaths[0]);
				}
				ResponseObj->SetArrayField(TEXT("paths"), DeletedEntries);
				OnComplete(BAT::Http::MakeJsonResponse(200, ResponseObj, RequestId));
			});
			return true;
		}));

	AssetSaveRoute = Router->BindRoute(
		FHttpPath(TEXT("/asset/save")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, TEXT("/asset/save")))
			{
				return true;
			}

			TSharedPtr<FJsonObject> BodyObj;
			if (!BAT::Http::TryParseJsonBody(Request.Body, BodyObj) || !BodyObj.IsValid())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("bad_args"), TEXT("Invalid JSON body")));
				return true;
			}

			const TArray<TSharedPtr<FJsonValue>>* PathsField = nullptr;
			BodyObj->TryGetArrayField(TEXT("paths"), PathsField);
			const TArray<TSharedPtr<FJsonValue>> Paths = PathsField ? *PathsField : TArray<TSharedPtr<FJsonValue>>();
			const FString RequestId = ResolveOrCreateRequestId(Request);

			AsyncTask(ENamedThreads::GameThread, [this, Paths, RequestId, OnComplete]()
			{
				int32 SavedCount = 0;
				for (const TSharedPtr<FJsonValue>& PathValue : Paths)
				{
					if (!PathValue.IsValid() || PathValue->Type != EJson::String)
					{
						OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, RequestId, TEXT("bad_args"), TEXT("'paths' must contain strings")));
						return;
					}

					const FString ObjectPath = NormalizeObjectPath(PathValue->AsString());
					UObject* Asset = LoadObject<UObject>(nullptr, *ObjectPath);
					if (!Asset)
					{
						OnComplete(MakeErrorResponse(EHttpServerResponseCodes::NotFound, RequestId, TEXT("asset_not_found"), FString::Printf(TEXT("Asset not found: %s"), *ObjectPath)));
						return;
					}

					if (!SaveObjectPackage(Asset))
					{
						OnComplete(MakeErrorResponse(EHttpServerResponseCodes::ServerError, RequestId, TEXT("save_failed"), FString::Printf(TEXT("Failed to save asset: %s"), *ObjectPath)));
						return;
					}
					++SavedCount;
				}

				TSharedRef<FJsonObject> ResponseObj = MakeShared<FJsonObject>();
				ResponseObj->SetBoolField(TEXT("ok"), true);
				ResponseObj->SetStringField(TEXT("request_id"), RequestId);
				ResponseObj->SetNumberField(TEXT("saved"), SavedCount);
				OnComplete(BAT::Http::MakeJsonResponse(200, ResponseObj, RequestId));
			});
			return true;
		}));

	AssetCreateRoute = Router->BindRoute(
		FHttpPath(TEXT("/asset/create")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, TEXT("/asset/create")))
			{
				return true;
			}

			TSharedPtr<FJsonObject> BodyObj;
			if (!BAT::Http::TryParseJsonBody(Request.Body, BodyObj) || !BodyObj.IsValid())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("bad_args"), TEXT("Invalid JSON body")));
				return true;
			}

			FString ClassPath;
			FString AssetPath;
			FString OuterPath;
			BodyObj->TryGetStringField(TEXT("class"), ClassPath);
			BodyObj->TryGetStringField(TEXT("path"), AssetPath);
			BodyObj->TryGetStringField(TEXT("outer"), OuterPath);
			const FString RequestId = ResolveOrCreateRequestId(Request);

			AsyncTask(ENamedThreads::GameThread, [this, BodyObj, ClassPath, AssetPath, OuterPath, RequestId, OnComplete]()
			{
				UClass* AssetClass = FindObject<UClass>(nullptr, *ClassPath);
				if (!AssetClass)
				{
					AssetClass = LoadObject<UClass>(nullptr, *ClassPath);
				}
				if (!AssetClass || AssetClass->HasAnyClassFlags(CLASS_Abstract))
				{
					OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, RequestId, TEXT("bad_args"), TEXT("Invalid asset class")));
					return;
				}

				if (AssetClass->IsChildOf(USkeletalMeshSocket::StaticClass()))
				{
					const FString OuterObjectPath = NormalizeObjectPath(OuterPath);
					if (OuterObjectPath.IsEmpty())
					{
						OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, RequestId, TEXT("bad_args"), TEXT("SkeletalMeshSocket creation requires 'outer' SkeletalMesh object path")));
						return;
					}

					USkeletalMesh* SkeletalMesh = LoadObject<USkeletalMesh>(nullptr, *OuterObjectPath);
					if (!SkeletalMesh)
					{
						OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, RequestId, TEXT("bad_args"), TEXT("'outer' must resolve to a SkeletalMesh asset")));
						return;
					}

					FString SocketNameString;
					FString BoneNameString;
					BodyObj->TryGetStringField(TEXT("socket_name"), SocketNameString);
					BodyObj->TryGetStringField(TEXT("bone_name"), BoneNameString);
					SocketNameString.TrimStartAndEndInline();
					BoneNameString.TrimStartAndEndInline();
					if (SocketNameString.IsEmpty() || BoneNameString.IsEmpty())
					{
						OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, RequestId, TEXT("bad_args"), TEXT("SkeletalMeshSocket creation requires 'socket_name' and 'bone_name'")));
						return;
					}

					const FName SocketName(*SocketNameString);
					const FName BoneName(*BoneNameString);
					if (SkeletalMesh->GetRefSkeleton().FindBoneIndex(BoneName) == INDEX_NONE)
					{
						OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, RequestId, TEXT("bad_args"), TEXT("'bone_name' does not exist on the SkeletalMesh")));
						return;
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
							OnComplete(MakeErrorResponse(EHttpServerResponseCodes::Conflict, RequestId, TEXT("socket_conflict"), TEXT("A socket with that name already exists on the shared skeleton; choose a different mesh socket name")));
							return;
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
							OnComplete(MakeErrorResponse(EHttpServerResponseCodes::ServerError, RequestId, TEXT("create_failed"), TEXT("Failed to create SkeletalMesh socket")));
							return;
						}
						bCreated = true;
					}

					SkeletalMesh->Modify();
					SkeletalMesh->MarkPackageDirty();
					SkeletalMesh->PostEditChange();

					TSharedRef<FJsonObject> ResponseObj = MakeShared<FJsonObject>();
					ResponseObj->SetBoolField(TEXT("ok"), true);
					ResponseObj->SetStringField(TEXT("request_id"), RequestId);
					ResponseObj->SetStringField(TEXT("outer"), OuterObjectPath);
					ResponseObj->SetStringField(TEXT("socket_name"), SocketName.ToString());
					ResponseObj->SetStringField(TEXT("bone_name"), BoneName.ToString());
					ResponseObj->SetBoolField(TEXT("created"), bCreated);
					ResponseObj->SetBoolField(TEXT("updated"), bUpdated);
					OnComplete(BAT::Http::MakeJsonResponse(200, ResponseObj, RequestId));
					return;
				}

				if (AssetClass->IsChildOf(UAnimSequence::StaticClass()))
				{
					if (!FPackageName::IsValidLongPackageName(AssetPath))
					{
						OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, RequestId, TEXT("bad_args"), TEXT("'path' must be a long package path")));
						return;
					}

					FString SkeletonPath;
					if (!BodyObj->TryGetStringField(TEXT("skeleton"), SkeletonPath) || SkeletonPath.TrimStartAndEnd().IsEmpty())
					{
						OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, RequestId, TEXT("bad_args"), TEXT("AnimSequence creation requires 'skeleton'")));
						return;
					}

					USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *NormalizeObjectPath(SkeletonPath));
					if (!Skeleton)
					{
						OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, RequestId, TEXT("bad_args"), TEXT("'skeleton' must resolve to a Skeleton asset")));
						return;
					}

					USkeletalMesh* PreviewMesh = nullptr;
					FString PreviewMeshPath;
					if (BodyObj->TryGetStringField(TEXT("preview_mesh"), PreviewMeshPath) && !PreviewMeshPath.TrimStartAndEnd().IsEmpty())
					{
						PreviewMesh = LoadObject<USkeletalMesh>(nullptr, *NormalizeObjectPath(PreviewMeshPath));
						if (!PreviewMesh)
						{
							OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, RequestId, TEXT("bad_args"), TEXT("'preview_mesh' must resolve to a SkeletalMesh asset")));
							return;
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
						OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, RequestId, TEXT("bad_args"), AxisError));
						return;
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
								OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, RequestId, TEXT("bad_args"), TEXT("'tracks' entries must be objects")));
								return;
							}
							NumFrames = FMath::Max(NumFrames, GetTrackKeyCount(TrackValue->AsObject()));
						}
						NumFrames = FMath::Max(NumFrames, 1);
					}

					if (NumFrames <= 0)
					{
						OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, RequestId, TEXT("bad_args"), TEXT("'number_of_frames' must be >= 1")));
						return;
					}

					TArray<FBATAnimSequenceTrackRequest> ParsedTracks;
					if (TrackValues)
					{
						for (const TSharedPtr<FJsonValue>& TrackValue : *TrackValues)
						{
							if (!TrackValue.IsValid() || TrackValue->Type != EJson::Object)
							{
								OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, RequestId, TEXT("bad_args"), TEXT("'tracks' entries must be objects")));
								return;
							}

							FBATAnimSequenceTrackRequest TrackRequest;
							FString TrackError;
							if (!TryParseAnimSequenceTrack(TrackValue->AsObject(), NumFrames, AxisToUnrealQuat, TrackRequest, TrackError))
							{
								OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, RequestId, TEXT("bad_args"), TrackError));
								return;
							}

							if (Skeleton->GetReferenceSkeleton().FindBoneIndex(TrackRequest.BoneName) == INDEX_NONE)
							{
								OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, RequestId, TEXT("bad_args"), FString::Printf(TEXT("track bone does not exist on skeleton: %s"), *TrackRequest.BoneName.ToString())));
								return;
							}

							ParsedTracks.Add(MoveTemp(TrackRequest));
						}
					}

					const FString AssetName = FPackageName::GetLongPackageAssetName(AssetPath);
					UPackage* Package = CreatePackage(*AssetPath);
					if (!Package)
					{
						OnComplete(MakeErrorResponse(EHttpServerResponseCodes::ServerError, RequestId, TEXT("create_failed"), TEXT("Failed to create package")));
						return;
					}

					UAnimSequenceFactory* Factory = NewObject<UAnimSequenceFactory>();
					Factory->TargetSkeleton = Skeleton;
					Factory->PreviewSkeletalMesh = PreviewMesh;

					UAnimSequence* AnimSequence = Cast<UAnimSequence>(Factory->FactoryCreateNew(UAnimSequence::StaticClass(), Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional, nullptr, GWarn));
					if (!AnimSequence)
					{
						OnComplete(MakeErrorResponse(EHttpServerResponseCodes::ServerError, RequestId, TEXT("create_failed"), TEXT("Failed to create AnimSequence asset")));
						return;
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

					bool bSave = false;
					BodyObj->TryGetBoolField(TEXT("save"), bSave);
					if (bSave && !SaveObjectPackage(AnimSequence))
					{
						OnComplete(MakeErrorResponse(EHttpServerResponseCodes::ServerError, RequestId, TEXT("save_failed"), TEXT("Failed to save AnimSequence asset")));
						return;
					}

					TSharedRef<FJsonObject> ResponseObj = MakeShared<FJsonObject>();
					ResponseObj->SetBoolField(TEXT("ok"), true);
					ResponseObj->SetStringField(TEXT("request_id"), RequestId);
					ResponseObj->SetStringField(TEXT("path"), AssetPath);
					ResponseObj->SetStringField(TEXT("skeleton"), NormalizeObjectPath(SkeletonPath));
					if (PreviewMesh)
					{
						ResponseObj->SetStringField(TEXT("preview_mesh"), NormalizeObjectPath(PreviewMeshPath));
					}
					ResponseObj->SetNumberField(TEXT("frame_rate"), FrameRate);
					ResponseObj->SetNumberField(TEXT("number_of_frames"), NumFrames);
					ResponseObj->SetNumberField(TEXT("tracks"), ParsedTracks.Num());
					if (!ForwardAxis.TrimStartAndEnd().IsEmpty())
					{
						ForwardAxis.TrimStartAndEndInline();
						ForwardAxis.ToUpperInline();
						ResponseObj->SetStringField(TEXT("forward_axis"), ForwardAxis);
					}
					ResponseObj->SetBoolField(TEXT("saved"), bSave);
					OnComplete(BAT::Http::MakeJsonResponse(200, ResponseObj, RequestId));
					return;
				}

				if (!FPackageName::IsValidLongPackageName(AssetPath))
				{
					OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, RequestId, TEXT("bad_args"), TEXT("'path' must be a long package path")));
					return;
				}

				const FString AssetName = FPackageName::GetLongPackageAssetName(AssetPath);
				UPackage* Package = CreatePackage(*AssetPath);
				if (!Package)
				{
					OnComplete(MakeErrorResponse(EHttpServerResponseCodes::ServerError, RequestId, TEXT("create_failed"), TEXT("Failed to create package")));
					return;
				}

				UObject* NewAsset = NewObject<UObject>(Package, AssetClass, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
				if (!NewAsset)
				{
					OnComplete(MakeErrorResponse(EHttpServerResponseCodes::ServerError, RequestId, TEXT("create_failed"), TEXT("Failed to create asset")));
					return;
				}

				FAssetRegistryModule::AssetCreated(NewAsset);
				Package->MarkPackageDirty();

				TSharedRef<FJsonObject> ResponseObj = MakeShared<FJsonObject>();
				ResponseObj->SetBoolField(TEXT("ok"), true);
				ResponseObj->SetStringField(TEXT("request_id"), RequestId);
				ResponseObj->SetStringField(TEXT("path"), AssetPath);
				OnComplete(BAT::Http::MakeJsonResponse(200, ResponseObj, RequestId));
			});
			return true;
		}));

	PcgSpawnSpheresRoute = Router->BindRoute(
		FHttpPath(TEXT("/pcg/spawn_spheres")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, TEXT("/pcg/spawn_spheres")))
			{
				return true;
			}

			TSharedPtr<FJsonObject> BodyObj;
			if (!BAT::Http::TryParseJsonBody(Request.Body, BodyObj) || !BodyObj.IsValid())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("bad_args"), TEXT("Invalid JSON body")));
				return true;
			}

			FString DstGraph;
			BodyObj->TryGetStringField(TEXT("dst_graph"), DstGraph);
			double Count = 100.0;
			double Seed = 1337.0;
			BodyObj->TryGetNumberField(TEXT("count"), Count);
			BodyObj->TryGetNumberField(TEXT("seed"), Seed);
			FVector Location = FVector::ZeroVector;
			FVector Extent = FVector(500.0f, 500.0f, 200.0f);
			const TArray<TSharedPtr<FJsonValue>>* LocationField = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* ExtentField = nullptr;
			BodyObj->TryGetArrayField(TEXT("location"), LocationField);
			BodyObj->TryGetArrayField(TEXT("extent"), ExtentField);
			if (LocationField)
			{
				TryParseVector(MakeShared<FJsonValueArray>(*LocationField), Location);
			}
			if (ExtentField)
			{
				TryParseVector(MakeShared<FJsonValueArray>(*ExtentField), Extent);
			}

			const FString RequestId = ResolveOrCreateRequestId(Request);
			AsyncTask(ENamedThreads::GameThread, [this, DstGraph, Location, Extent, Count, Seed, RequestId, OnComplete]()
			{
				const FString TemplatePath = TEXT("/BlueprintAutomationToolkit/Templates/PCG/PCG_SphereSpawner.PCG_SphereSpawner");
				UObject* TemplateAsset = LoadObject<UObject>(nullptr, *TemplatePath);
				if (!TemplateAsset)
				{
					OnComplete(MakeErrorResponse(EHttpServerResponseCodes::NotFound, RequestId, TEXT("asset_not_found"), TEXT("PCG template graph asset not found")));
					return;
				}

				const FString DstPackagePath = DstGraph.TrimStartAndEnd();
				if (!FPackageName::IsValidLongPackageName(DstPackagePath))
				{
					OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, RequestId, TEXT("bad_args"), TEXT("'dst_graph' must be a long package path")));
					return;
				}

				const FString DstAssetName = FPackageName::GetLongPackageAssetName(DstPackagePath);
				const FString DstAssetPath = FPackageName::GetLongPackagePath(DstPackagePath);
				FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
				UObject* Duplicated = AssetToolsModule.Get().DuplicateAsset(DstAssetName, DstAssetPath, TemplateAsset);
				if (!Duplicated)
				{
					OnComplete(MakeErrorResponse(EHttpServerResponseCodes::ServerError, RequestId, TEXT("duplicate_failed"), TEXT("Failed to duplicate PCG template graph")));
					return;
				}

				UWorld* EditorWorld = GetEditorWorld();
				if (!EditorWorld)
				{
					OnComplete(MakeErrorResponse(EHttpServerResponseCodes::ServerError, RequestId, TEXT("not_found"), TEXT("Editor world not available")));
					return;
				}

				UClass* HostClass = LoadObject<UClass>(nullptr, TEXT("/BlueprintAutomationToolkit/Templates/PCG/BP_PCGHost.BP_PCGHost_C"));
				if (!HostClass)
				{
					HostClass = AActor::StaticClass();
				}

				AActor* HostActor = EditorWorld->SpawnActor<AActor>(HostClass, Location, FRotator::ZeroRotator);
				if (!HostActor)
				{
					OnComplete(MakeErrorResponse(EHttpServerResponseCodes::ServerError, RequestId, TEXT("not_found"), TEXT("Failed to spawn PCG host actor")));
					return;
				}
				HostActor->SetActorScale3D(FVector(FMath::Max(Extent.X, 1.0f) / 100.0f, FMath::Max(Extent.Y, 1.0f) / 100.0f, FMath::Max(Extent.Z, 1.0f) / 100.0f));

				if (UPCGComponent* PcgComponent = HostActor->FindComponentByClass<UPCGComponent>())
				{
					if (UPCGGraphInterface* GraphInterface = Cast<UPCGGraphInterface>(Duplicated))
					{
						PcgComponent->SetGraph(GraphInterface);
					}
					TrySetNumericProp(PcgComponent, TEXT("Seed"), Seed);
					TrySetNumericProp(PcgComponent, TEXT("GenerationGridSize"), Count);
					PcgComponent->MarkPackageDirty();
				}

				if (!SaveObjectPackage(Duplicated))
				{
					OnComplete(MakeErrorResponse(EHttpServerResponseCodes::ServerError, RequestId, TEXT("save_failed"), TEXT("Failed to save duplicated graph")));
					return;
				}

				TSharedRef<FJsonObject> ResponseObj = MakeShared<FJsonObject>();
				ResponseObj->SetBoolField(TEXT("ok"), true);
				ResponseObj->SetStringField(TEXT("request_id"), RequestId);
				ResponseObj->SetStringField(TEXT("graph"), DstPackagePath);
				ResponseObj->SetStringField(TEXT("host"), HostActor->GetPathName());
				OnComplete(BAT::Http::MakeJsonResponse(200, ResponseObj, RequestId));
			});
			return true;
		}));
}

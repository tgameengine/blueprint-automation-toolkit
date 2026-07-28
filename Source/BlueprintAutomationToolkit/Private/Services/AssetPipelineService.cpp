// Copyright 2026 AkaSoft. All Rights Reserved.

#include "Services/AssetPipelineService.h"

#include "BlueprintAutomationToolkitModule.h"
#include "BlueprintAutomationToolkitSettings.h"

#include "Animation/AnimSequence.h"
#include "Animation/SkeletalMeshActor.h"
#include "Animation/Skeleton.h"
#include "AssetImportTask.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "EditorFramework/AssetImportData.h"
#include "EditorReimportHandler.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Texture.h"
#include "Factories/FbxAssetImportData.h"
#include "Factories/FbxImportUI.h"
#include "Factories/FbxSkeletalMeshImportData.h"
#include "Factories/FbxStaticMeshImportData.h"
#include "Factories/Factory.h"
#include "Factories/PhysicsAssetFactory.h"
#include "HAL/FileManager.h"
#include "IAssetTools.h"
#include "IAssetViewport.h"
#include "ImageUtils.h"
#include "LevelEditor.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/EngineVersionComparison.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Modules/ModuleManager.h"
#include "ObjectTools.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "ScopedTransaction.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Sound/SoundWave.h"
#include "UObject/MetaData.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"
#include "UnrealClient.h"

namespace
{
	static const TCHAR* SourceFileMetadataKey = TEXT("BAT.SourceFile");
	static const TCHAR* SourceFingerprintMetadataKey = TEXT("BAT.SourceFingerprint");
	static const TCHAR* PipelineVersionMetadataKey = TEXT("BAT.AssetPipelineVersion");
	static const TCHAR* PipelineVersion = TEXT("1.0");

	static FString GetAssetMetadataValue(UObject* Asset, const TCHAR* Key)
	{
		if (!Asset || !Asset->GetOutermost())
		{
			return FString();
		}
#if UE_VERSION_OLDER_THAN(5, 6, 0)
		if (UMetaData* Metadata = Asset->GetOutermost()->GetMetaData())
		{
			return Metadata->GetValue(Asset, Key);
		}
		return FString();
#else
		return Asset->GetOutermost()->GetMetaData().GetValue(Asset, Key);
#endif
	}

	static void SetAssetMetadataValue(UObject* Asset, const TCHAR* Key, const FString& Value)
	{
		if (!Asset || !Asset->GetOutermost())
		{
			return;
		}
#if UE_VERSION_OLDER_THAN(5, 6, 0)
		if (UMetaData* Metadata = Asset->GetOutermost()->GetMetaData())
		{
			Metadata->SetValue(Asset, Key, *Value);
		}
#else
		Asset->GetOutermost()->GetMetaData().SetValue(Asset, Key, *Value);
#endif
	}

	struct FBATImportEntry
	{
		FString SourceFile;
		FString DestinationName;
		FString ExpectedType;
		FString FactoryClass;
		FString OptionsClass;
		TSharedPtr<FJsonObject> CommonOptions;
		TSharedPtr<FJsonObject> FactoryProperties;
		TSharedPtr<FJsonObject> OptionsProperties;
	};

	static TSharedPtr<FJsonValue> MakeObjectValue(const TSharedPtr<FJsonObject>& Object)
	{
		return MakeShared<FJsonValueObject>(Object);
	}

	static TSharedRef<FJsonObject> CloneJsonObject(const TSharedPtr<FJsonObject>& Source)
	{
		TSharedRef<FJsonObject> Clone = MakeShared<FJsonObject>();
		if (Source.IsValid())
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Source->Values)
			{
				Clone->SetField(Pair.Key, Pair.Value);
			}
		}
		return Clone;
	}

	static FString NormalizeObjectPath(const FString& InPath)
	{
		FString Path = InPath.TrimStartAndEnd();
		if (!Path.Contains(TEXT(".")) && FPackageName::IsValidLongPackageName(Path))
		{
			const FString AssetName = FPackageName::GetLongPackageAssetName(Path);
			Path = FString::Printf(TEXT("%s.%s"), *Path, *AssetName);
		}
		return Path;
	}

	static bool IsGameDestinationPath(const FString& InPath)
	{
		const FString Path = InPath.TrimStartAndEnd();
		return Path.Equals(TEXT("/Game"), ESearchCase::CaseSensitive)
			|| (Path.StartsWith(TEXT("/Game/"), ESearchCase::CaseSensitive)
				&& FPackageName::IsValidLongPackageName(Path));
	}

	static FString NormalizeDiskPath(const FString& InPath)
	{
		FString Path = InPath;
		FPaths::NormalizeFilename(Path);
		FPaths::CollapseRelativeDirectories(Path);
		Path = FPaths::ConvertRelativePathToFull(Path);
		FPaths::NormalizeFilename(Path);
		return Path;
	}

	static bool IsPathInsideRoot(const FString& Path, const FString& Root)
	{
		FString NormalizedPath = NormalizeDiskPath(Path);
		FString NormalizedRoot = NormalizeDiskPath(Root);
		NormalizedPath.ToLowerInline();
		NormalizedRoot.ToLowerInline();
		NormalizedRoot.RemoveFromEnd(TEXT("/"));
		return NormalizedPath.Equals(NormalizedRoot, ESearchCase::CaseSensitive)
			|| NormalizedPath.StartsWith(NormalizedRoot + TEXT("/"), ESearchCase::CaseSensitive);
	}

	static FString ResolveExistingDiskPath(const FString& Path)
	{
		const FString OnDisk = IFileManager::Get().GetFilenameOnDisk(*Path);
		return NormalizeDiskPath(OnDisk.IsEmpty() ? Path : OnDisk);
	}

	static TArray<FString> GetAllowedImportRoots()
	{
		const UBlueprintAutomationToolkitSettings* Settings = GetDefault<UBlueprintAutomationToolkitSettings>();
		TArray<FString> Roots;
		if (Settings)
		{
			for (const FString& ConfiguredRoot : Settings->AssetImportAllowedRoots)
			{
				FString Root = ConfiguredRoot.TrimStartAndEnd();
				if (Root.IsEmpty())
				{
					continue;
				}
				if (FPaths::IsRelative(Root))
				{
					Root = FPaths::Combine(FPaths::ProjectDir(), Root);
				}
				Root = NormalizeDiskPath(Root);
				Roots.AddUnique(IFileManager::Get().DirectoryExists(*Root) ? ResolveExistingDiskPath(Root) : Root);
			}
		}
		if (Roots.Num() == 0)
		{
			Roots.Add(ResolveExistingDiskPath(FPaths::ProjectDir()));
		}
		return Roots;
	}

	static TSet<FString> GetAllowedExtensions()
	{
		const UBlueprintAutomationToolkitSettings* Settings = GetDefault<UBlueprintAutomationToolkitSettings>();
		TSet<FString> Result;
		if (Settings)
		{
			for (FString Extension : Settings->AssetImportAllowedExtensions)
			{
				Extension.TrimStartAndEndInline();
				Extension.RemoveFromStart(TEXT("."));
				Extension.ToLowerInline();
				if (!Extension.IsEmpty())
				{
					Result.Add(Extension);
				}
			}
		}
		return Result;
	}

	static bool ValidateImportSidecarPath(const FString& SourceFile, const FString& Uri, FString& OutError)
	{
		const FString TrimmedUri = Uri.TrimStartAndEnd();
		if (TrimmedUri.IsEmpty() || TrimmedUri.StartsWith(TEXT("data:"), ESearchCase::IgnoreCase))
		{
			return true;
		}
		if (TrimmedUri.Contains(TEXT("://"))
			|| TrimmedUri.StartsWith(TEXT("//"))
			|| TrimmedUri.Contains(TEXT("%"))
			|| TrimmedUri.Contains(TEXT("?"))
			|| TrimmedUri.Contains(TEXT("#")))
		{
			OutError = FString::Printf(TEXT("Remote, escaped, or ambiguous sidecar URI is not permitted: %s"), *TrimmedUri);
			return false;
		}

		FString Sidecar = TrimmedUri;
		if (FPaths::IsRelative(Sidecar))
		{
			Sidecar = FPaths::Combine(FPaths::GetPath(SourceFile), Sidecar);
		}
		Sidecar = NormalizeDiskPath(Sidecar);
		if (!IFileManager::Get().FileExists(*Sidecar))
		{
			OutError = FString::Printf(TEXT("Referenced sidecar does not exist: %s"), *TrimmedUri);
			return false;
		}
		Sidecar = ResolveExistingDiskPath(Sidecar);
		bool bInsideAllowedRoot = false;
		for (const FString& Root : GetAllowedImportRoots())
		{
			if (IsPathInsideRoot(Sidecar, Root))
			{
				bInsideAllowedRoot = true;
				break;
			}
		}
		if (!bInsideAllowedRoot)
		{
			OutError = FString::Printf(TEXT("Referenced sidecar is outside Asset Import Allowed Roots: %s"), *TrimmedUri);
			return false;
		}

		const UBlueprintAutomationToolkitSettings* Settings = GetDefault<UBlueprintAutomationToolkitSettings>();
		const int64 MaxBytes = static_cast<int64>(Settings ? Settings->AssetImportMaxFileSizeMb : 2048) * 1024ll * 1024ll;
		const int64 FileSize = IFileManager::Get().FileSize(*Sidecar);
		if (FileSize < 0 || FileSize > MaxBytes)
		{
			OutError = FString::Printf(TEXT("Referenced sidecar exceeds the configured file-size limit: %s"), *TrimmedUri);
			return false;
		}
		return true;
	}

	static bool ValidateStructuredImportReferences(const FString& SourceFile, FString& OutError)
	{
		if (!FPaths::GetExtension(SourceFile, false).Equals(TEXT("gltf"), ESearchCase::IgnoreCase))
		{
			return true;
		}

		FString JsonText;
		if (!FFileHelper::LoadFileToString(JsonText, *SourceFile))
		{
			OutError = TEXT("Could not inspect glTF source references.");
			return false;
		}
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			OutError = TEXT("glTF source is not valid JSON.");
			return false;
		}

		for (const TCHAR* CollectionName : {TEXT("buffers"), TEXT("images")})
		{
			const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
			if (!Root->TryGetArrayField(CollectionName, Values) || !Values)
			{
				continue;
			}
			for (const TSharedPtr<FJsonValue>& Value : *Values)
			{
				if (!Value.IsValid() || Value->Type != EJson::Object)
				{
					continue;
				}
				FString Uri;
				if (Value->AsObject()->TryGetStringField(TEXT("uri"), Uri)
					&& !ValidateImportSidecarPath(SourceFile, Uri, OutError))
				{
					return false;
				}
			}
		}
		return true;
	}

	static bool ResolveAndValidateSourceFile(const FString& InSource, FString& OutSource, FString& OutError)
	{
		OutError.Reset();
		FString Source = InSource.TrimStartAndEnd();
		if (Source.IsEmpty())
		{
			OutError = TEXT("Source file must not be empty.");
			return false;
		}
		if (FPaths::IsRelative(Source))
		{
			Source = FPaths::Combine(FPaths::ProjectDir(), Source);
		}
		Source = NormalizeDiskPath(Source);

		if (!IFileManager::Get().FileExists(*Source))
		{
			OutError = TEXT("Source file does not exist.");
			return false;
		}
		Source = ResolveExistingDiskPath(Source);
		bool bInsideAllowedRoot = false;
		for (const FString& Root : GetAllowedImportRoots())
		{
			if (IsPathInsideRoot(Source, Root))
			{
				bInsideAllowedRoot = true;
				break;
			}
		}
		if (!bInsideAllowedRoot)
		{
			OutError = TEXT("Source file is outside Asset Import Allowed Roots after resolving filesystem links.");
			return false;
		}

		FString Extension = FPaths::GetExtension(Source, false).ToLower();
		const TSet<FString> AllowedExtensions = GetAllowedExtensions();
		if (!AllowedExtensions.Contains(Extension))
		{
			OutError = FString::Printf(TEXT("File extension '.%s' is not allowed."), *Extension);
			return false;
		}

		const UBlueprintAutomationToolkitSettings* Settings = GetDefault<UBlueprintAutomationToolkitSettings>();
		const int64 MaxBytes = static_cast<int64>(Settings ? Settings->AssetImportMaxFileSizeMb : 2048) * 1024ll * 1024ll;
		const int64 FileSize = IFileManager::Get().FileSize(*Source);
		if (FileSize < 0 || FileSize > MaxBytes)
		{
			OutError = FString::Printf(TEXT("Source file exceeds the configured %d MB limit."), Settings ? Settings->AssetImportMaxFileSizeMb : 2048);
			return false;
		}
		if (!ValidateStructuredImportReferences(Source, OutError))
		{
			return false;
		}

		OutSource = Source;
		return true;
	}

	static FString HashFileMd5(const FString& Filename)
	{
		const FMD5Hash Hash = FMD5Hash::HashFile(*Filename);
		if (!Hash.IsValid())
		{
			return FString();
		}

		FString Result;
		const uint8* Bytes = Hash.GetBytes();
		for (int32 Index = 0; Index < Hash.GetSize(); ++Index)
		{
			Result += FString::Printf(TEXT("%02x"), Bytes[Index]);
		}
		return Result;
	}

	static bool TryGetBool(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, const bool DefaultValue)
	{
		bool Value = DefaultValue;
		if (Object.IsValid())
		{
			Object->TryGetBoolField(Field, Value);
		}
		return Value;
	}

	static int32 TryGetInt(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, const int32 DefaultValue)
	{
		double Value = static_cast<double>(DefaultValue);
		if (Object.IsValid())
		{
			Object->TryGetNumberField(Field, Value);
		}
		return FMath::RoundToInt(Value);
	}

	static TSharedPtr<FJsonObject> GetObjectField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		const TSharedPtr<FJsonObject>* Value = nullptr;
		if (Object.IsValid() && Object->TryGetObjectField(Field, Value) && Value)
		{
			return *Value;
		}
		return nullptr;
	}

	static bool ParseImportEntries(const TSharedPtr<FJsonObject>& Request, TArray<FBATImportEntry>& OutEntries, FString& OutError)
	{
		OutEntries.Reset();
		OutError.Reset();
		if (!Request.IsValid())
		{
			OutError = TEXT("Invalid JSON body.");
			return false;
		}

		TSharedPtr<FJsonObject> SharedOptions = GetObjectField(Request, TEXT("options"));
		TSharedPtr<FJsonObject> SharedFactoryProperties = GetObjectField(Request, TEXT("factory_properties"));
		TSharedPtr<FJsonObject> SharedOptionsProperties = GetObjectField(Request, TEXT("options_properties"));

		auto FillCommonFields = [&](FBATImportEntry& Entry, const TSharedPtr<FJsonObject>& Object)
		{
			Object->TryGetStringField(TEXT("destination_name"), Entry.DestinationName);
			Object->TryGetStringField(TEXT("expected_type"), Entry.ExpectedType);
			Object->TryGetStringField(TEXT("factory_class"), Entry.FactoryClass);
			Object->TryGetStringField(TEXT("options_class"), Entry.OptionsClass);
			Entry.CommonOptions = GetObjectField(Object, TEXT("options"));
			Entry.FactoryProperties = GetObjectField(Object, TEXT("factory_properties"));
			Entry.OptionsProperties = GetObjectField(Object, TEXT("options_properties"));
			if (!Entry.CommonOptions.IsValid()) Entry.CommonOptions = SharedOptions;
			if (!Entry.FactoryProperties.IsValid()) Entry.FactoryProperties = SharedFactoryProperties;
			if (!Entry.OptionsProperties.IsValid()) Entry.OptionsProperties = SharedOptionsProperties;
		};

		FString SingleSource;
		if (Request->TryGetStringField(TEXT("source"), SingleSource) || Request->TryGetStringField(TEXT("file"), SingleSource))
		{
			FBATImportEntry& Entry = OutEntries.AddDefaulted_GetRef();
			Entry.SourceFile = SingleSource;
			FillCommonFields(Entry, Request);
		}

		const TArray<TSharedPtr<FJsonValue>>* Sources = nullptr;
		if (Request->TryGetArrayField(TEXT("sources"), Sources) && Sources)
		{
			for (const TSharedPtr<FJsonValue>& Value : *Sources)
			{
				FBATImportEntry Entry;
				if (Value.IsValid() && Value->Type == EJson::String)
				{
					Entry.SourceFile = Value->AsString();
					FillCommonFields(Entry, Request);
				}
				else if (Value.IsValid() && Value->Type == EJson::Object)
				{
					const TSharedPtr<FJsonObject> Object = Value->AsObject();
					if (!Object.IsValid()
						|| (!Object->TryGetStringField(TEXT("source"), Entry.SourceFile)
							&& !Object->TryGetStringField(TEXT("file"), Entry.SourceFile)))
					{
						OutError = TEXT("Each 'sources' object requires 'source' or 'file'.");
						return false;
					}
					FillCommonFields(Entry, Object);
				}
				else
				{
					OutError = TEXT("'sources' entries must be strings or objects.");
					return false;
				}
				OutEntries.Add(MoveTemp(Entry));
			}
		}

		if (OutEntries.Num() == 0)
		{
			OutError = TEXT("Body requires 'source', 'file', or a non-empty 'sources' array.");
			return false;
		}
		if (OutEntries.Num() > 100)
		{
			OutError = TEXT("One request may import at most 100 source files.");
			return false;
		}
		return true;
	}

	static bool TrySetSimpleProperty(UObject* Target, const FString& PropertyName, const TSharedPtr<FJsonValue>& Value, FString& OutError)
	{
		OutError.Reset();
		if (!Target || PropertyName.IsEmpty() || !Value.IsValid())
		{
			OutError = TEXT("Invalid property assignment.");
			return false;
		}

		FProperty* Property = FindFProperty<FProperty>(Target->GetClass(), *PropertyName);
		if (!Property || !Property->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible))
		{
			OutError = FString::Printf(TEXT("Property is not editor-visible: %s"), *PropertyName);
			return false;
		}
		void* Address = Property->ContainerPtrToValuePtr<void>(Target);

		if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
		{
			if (Value->Type != EJson::Boolean)
			{
				OutError = TEXT("Expected boolean.");
				return false;
			}
			BoolProperty->SetPropertyValue(Address, Value->AsBool());
			return true;
		}
		if (FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
		{
			if (Value->Type != EJson::Number)
			{
				OutError = TEXT("Expected number.");
				return false;
			}
			if (NumericProperty->IsInteger())
			{
				NumericProperty->SetIntPropertyValue(Address, static_cast<int64>(Value->AsNumber()));
			}
			else
			{
				NumericProperty->SetFloatingPointPropertyValue(Address, Value->AsNumber());
			}
			return true;
		}
		if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			int64 EnumValue = INDEX_NONE;
			if (Value->Type == EJson::String)
			{
				EnumValue = EnumProperty->GetEnum()->GetValueByNameString(Value->AsString());
			}
			else if (Value->Type == EJson::Number)
			{
				EnumValue = static_cast<int64>(Value->AsNumber());
			}
			if (EnumValue == INDEX_NONE)
			{
				OutError = TEXT("Invalid enum value.");
				return false;
			}
			EnumProperty->GetUnderlyingProperty()->SetIntPropertyValue(Address, EnumValue);
			return true;
		}
		if (FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
		{
			int64 EnumValue = INDEX_NONE;
			if (ByteProperty->Enum && Value->Type == EJson::String)
			{
				EnumValue = ByteProperty->Enum->GetValueByNameString(Value->AsString());
			}
			else if (Value->Type == EJson::Number)
			{
				EnumValue = static_cast<int64>(Value->AsNumber());
			}
			if (EnumValue == INDEX_NONE)
			{
				OutError = TEXT("Invalid byte/enum value.");
				return false;
			}
			ByteProperty->SetPropertyValue(Address, static_cast<uint8>(EnumValue));
			return true;
		}
		if (FStrProperty* StringProperty = CastField<FStrProperty>(Property))
		{
			if (Value->Type != EJson::String)
			{
				OutError = TEXT("Expected string.");
				return false;
			}
			StringProperty->SetPropertyValue(Address, Value->AsString());
			return true;
		}
		if (FNameProperty* NameProperty = CastField<FNameProperty>(Property))
		{
			if (Value->Type != EJson::String)
			{
				OutError = TEXT("Expected string.");
				return false;
			}
			NameProperty->SetPropertyValue(Address, FName(*Value->AsString()));
			return true;
		}
		if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
		{
			if (Value->Type != EJson::String)
			{
				OutError = TEXT("Expected asset object path string.");
				return false;
			}
			UObject* ObjectValue = LoadObject<UObject>(nullptr, *NormalizeObjectPath(Value->AsString()));
			if (!ObjectValue || !ObjectValue->IsA(ObjectProperty->PropertyClass))
			{
				OutError = TEXT("Asset path does not resolve to the required property class.");
				return false;
			}
			ObjectProperty->SetObjectPropertyValue(Address, ObjectValue);
			return true;
		}

		OutError = FString::Printf(TEXT("Unsupported property type: %s"), *Property->GetClass()->GetName());
		return false;
	}

	static bool ApplyPropertyMap(UObject* Target, const TSharedPtr<FJsonObject>& Properties, FString& OutError)
	{
		if (!Properties.IsValid())
		{
			return true;
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Properties->Values)
		{
			FString PropertyError;
			if (!TrySetSimpleProperty(Target, Pair.Key, Pair.Value, PropertyError))
			{
				OutError = FString::Printf(TEXT("%s: %s"), *Pair.Key, *PropertyError);
				return false;
			}
		}
		return true;
	}

	static bool IsImportAdapterClassAllowed(const UClass* Class, const UClass* RequiredBase)
	{
		if (!Class || Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			return false;
		}
		if (RequiredBase == UFactory::StaticClass() && Class->IsChildOf(UFactory::StaticClass()))
		{
			return true;
		}
		const FString Name = Class->GetName();
		const FString Path = Class->GetPathName();
		return Name.Contains(TEXT("Import"), ESearchCase::IgnoreCase)
			|| Name.Contains(TEXT("Interchange"), ESearchCase::IgnoreCase)
			|| Name.Contains(TEXT("Options"), ESearchCase::IgnoreCase)
			|| Path.Contains(TEXT("Import"), ESearchCase::IgnoreCase)
			|| Path.Contains(TEXT("Interchange"), ESearchCase::IgnoreCase);
	}

	static UObject* CreateClassInstance(const FString& ClassPath, UClass* RequiredBase, FString& OutError)
	{
		OutError.Reset();
		UClass* Class = LoadObject<UClass>(nullptr, *ClassPath);
		if (!Class)
		{
			Class = FindObject<UClass>(nullptr, *ClassPath);
		}
		if (!Class || (RequiredBase && !Class->IsChildOf(RequiredBase)) || !IsImportAdapterClassAllowed(Class, RequiredBase))
		{
			OutError = FString::Printf(TEXT("Import adapter class is invalid or not allowed: %s"), *ClassPath);
			return nullptr;
		}
		return NewObject<UObject>(GetTransientPackage(), Class);
	}

	static UObject* BuildFbxOptions(const TSharedPtr<FJsonObject>& Options, FString& OutError)
	{
		UFbxImportUI* Fbx = NewObject<UFbxImportUI>(GetTransientPackage());
		if (!Fbx)
		{
			OutError = TEXT("Failed to allocate FBX import options.");
			return nullptr;
		}
		Fbx->bAutomatedImportShouldDetectType = true;
		if (!Options.IsValid())
		{
			return Fbx;
		}

		FString ImportType;
		Options->TryGetStringField(TEXT("import_type"), ImportType);
		ImportType.ToLowerInline();
		if (!ImportType.IsEmpty() && ImportType != TEXT("auto"))
		{
			Fbx->bAutomatedImportShouldDetectType = false;
			if (ImportType == TEXT("static_mesh") || ImportType == TEXT("static"))
			{
				Fbx->MeshTypeToImport = FBXIT_StaticMesh;
				Fbx->bImportAsSkeletal = false;
			}
			else if (ImportType == TEXT("skeletal_mesh") || ImportType == TEXT("skeletal"))
			{
				Fbx->MeshTypeToImport = FBXIT_SkeletalMesh;
				Fbx->bImportAsSkeletal = true;
			}
			else if (ImportType == TEXT("animation"))
			{
				Fbx->MeshTypeToImport = FBXIT_Animation;
				Fbx->bImportAsSkeletal = true;
				Fbx->bImportMesh = false;
			}
			else
			{
				OutError = TEXT("options.import_type must be auto, static_mesh, skeletal_mesh, or animation.");
				return nullptr;
			}
		}

		Fbx->bImportMesh = TryGetBool(Options, TEXT("import_mesh"), Fbx->bImportMesh);
		Fbx->bImportAnimations = TryGetBool(Options, TEXT("import_animations"), Fbx->bImportAnimations);
		Fbx->bImportMaterials = TryGetBool(Options, TEXT("import_materials"), Fbx->bImportMaterials);
		Fbx->bImportTextures = TryGetBool(Options, TEXT("import_textures"), Fbx->bImportTextures);
		Fbx->bCreatePhysicsAsset = TryGetBool(Options, TEXT("create_physics_asset"), Fbx->bCreatePhysicsAsset);

		FString SkeletonPath;
		if (Options->TryGetStringField(TEXT("skeleton"), SkeletonPath) && !SkeletonPath.TrimStartAndEnd().IsEmpty())
		{
			Fbx->Skeleton = LoadObject<USkeleton>(nullptr, *NormalizeObjectPath(SkeletonPath));
			if (!Fbx->Skeleton)
			{
				OutError = TEXT("options.skeleton does not resolve to a Skeleton asset.");
				return nullptr;
			}
		}

		if (Fbx->StaticMeshImportData)
		{
			Fbx->StaticMeshImportData->bCombineMeshes = TryGetBool(Options, TEXT("combine_meshes"), Fbx->StaticMeshImportData->bCombineMeshes);
			Fbx->StaticMeshImportData->bConvertScene = TryGetBool(Options, TEXT("convert_scene"), Fbx->StaticMeshImportData->bConvertScene);
			Fbx->StaticMeshImportData->bForceFrontXAxis = TryGetBool(Options, TEXT("force_front_x_axis"), Fbx->StaticMeshImportData->bForceFrontXAxis);
			Fbx->StaticMeshImportData->bConvertSceneUnit = TryGetBool(Options, TEXT("convert_scene_unit"), Fbx->StaticMeshImportData->bConvertSceneUnit);
		}
		if (Fbx->SkeletalMeshImportData)
		{
			Fbx->SkeletalMeshImportData->bImportMorphTargets = TryGetBool(Options, TEXT("import_morph_targets"), Fbx->SkeletalMeshImportData->bImportMorphTargets);
			Fbx->SkeletalMeshImportData->bConvertScene = TryGetBool(Options, TEXT("convert_scene"), Fbx->SkeletalMeshImportData->bConvertScene);
			Fbx->SkeletalMeshImportData->bForceFrontXAxis = TryGetBool(Options, TEXT("force_front_x_axis"), Fbx->SkeletalMeshImportData->bForceFrontXAxis);
			Fbx->SkeletalMeshImportData->bConvertSceneUnit = TryGetBool(Options, TEXT("convert_scene_unit"), Fbx->SkeletalMeshImportData->bConvertSceneUnit);
		}
		return Fbx;
	}

	static bool AssetMatchesExpectedType(UObject* Object, const FString& ExpectedType)
	{
		if (!Object || ExpectedType.TrimStartAndEnd().IsEmpty())
		{
			return true;
		}
		const FString Expected = ExpectedType.TrimStartAndEnd();
		return Object->GetClass()->GetName().Equals(Expected, ESearchCase::IgnoreCase)
			|| Object->GetClass()->GetPathName().Equals(Expected, ESearchCase::IgnoreCase)
			|| (Expected.Equals(TEXT("StaticMesh"), ESearchCase::IgnoreCase) && Object->IsA<UStaticMesh>())
			|| (Expected.Equals(TEXT("SkeletalMesh"), ESearchCase::IgnoreCase) && Object->IsA<USkeletalMesh>())
			|| (Expected.Equals(TEXT("AnimSequence"), ESearchCase::IgnoreCase) && Object->IsA<UAnimSequence>())
			|| (Expected.Equals(TEXT("Texture"), ESearchCase::IgnoreCase) && Object->IsA<UTexture>())
			|| (Expected.Equals(TEXT("Material"), ESearchCase::IgnoreCase) && Object->IsA<UMaterialInterface>())
			|| (Expected.Equals(TEXT("Skeleton"), ESearchCase::IgnoreCase) && Object->IsA<USkeleton>())
			|| (Expected.Equals(TEXT("PhysicsAsset"), ESearchCase::IgnoreCase) && Object->IsA<UPhysicsAsset>())
			|| (Expected.Equals(TEXT("SoundWave"), ESearchCase::IgnoreCase) && Object->IsA<USoundWave>());
	}

	static int32 AssetPipelinePriority(const UObject* Object)
	{
		if (Object && Object->IsA<USkeletalMesh>()) return 0;
		if (Object && Object->IsA<UStaticMesh>()) return 1;
		if (Object && Object->IsA<UAnimSequence>()) return 2;
		if (Object && Object->IsA<USkeleton>()) return 3;
		if (Object && Object->IsA<UPhysicsAsset>()) return 4;
		if (Object && Object->IsA<UMaterialInterface>()) return 5;
		if (Object && Object->IsA<UTexture>()) return 6;
		if (Object && Object->IsA<USoundWave>()) return 7;
		return 8;
	}

	static TArray<UObject*> OrderAssetPipelineObjects(const TArray<UObject*>& Objects)
	{
		TArray<UObject*> Ordered;
		for (int32 Priority = 0; Priority <= 8; ++Priority)
		{
			for (UObject* Object : Objects)
			{
				if (Object && AssetPipelinePriority(Object) == Priority)
				{
					Ordered.AddUnique(Object);
				}
			}
		}
		return Ordered;
	}

	static UObject* ChoosePrimaryAsset(const TArray<UObject*>& Objects, const FString& ExpectedType)
	{
		if (!ExpectedType.TrimStartAndEnd().IsEmpty())
		{
			for (UObject* Object : Objects)
			{
				if (AssetMatchesExpectedType(Object, ExpectedType))
				{
					return Object;
				}
			}
			return nullptr;
		}
		const TArray<UObject*> Ordered = OrderAssetPipelineObjects(Objects);
		return Ordered.Num() > 0 ? Ordered[0] : nullptr;
	}

	static bool IsNaniteEnabled(const UStaticMesh* StaticMesh)
	{
		if (!StaticMesh)
		{
			return false;
		}
#if UE_VERSION_OLDER_THAN(5, 7, 0)
		return StaticMesh->NaniteSettings.bEnabled;
#else
		return StaticMesh->GetNaniteSettings().bEnabled;
#endif
	}

	static void SetNaniteEnabled(UStaticMesh* StaticMesh, const bool bEnabled)
	{
		if (!StaticMesh)
		{
			return;
		}
#if UE_VERSION_OLDER_THAN(5, 7, 0)
		StaticMesh->NaniteSettings.bEnabled = bEnabled;
#else
		FMeshNaniteSettings Settings = StaticMesh->GetNaniteSettings();
		Settings.bEnabled = bEnabled;
		StaticMesh->SetNaniteSettings(Settings);
#endif
	}

	static TSet<FString> GetAssetObjectPathsUnder(const FString& DestinationPath)
	{
		TSet<FString> Result;
		FAssetRegistryModule& Module = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		TArray<FAssetData> Assets;
		Module.Get().GetAssetsByPath(FName(*DestinationPath), Assets, true);
		for (const FAssetData& Asset : Assets)
		{
			Result.Add(Asset.GetObjectPathString());
		}
		return Result;
	}

	static TArray<UObject*> FindUnchangedImportedAssets(const FString& DestinationPath, const FString& SourceFile, const FString& Fingerprint)
	{
		TArray<UObject*> Result;
		FAssetRegistryModule& Module = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		TArray<FAssetData> Assets;
		Module.Get().GetAssetsByPath(FName(*DestinationPath), Assets, true);
		for (const FAssetData& AssetData : Assets)
		{
			UObject* Asset = AssetData.GetAsset();
			if (!Asset || !Asset->GetOutermost())
			{
				continue;
			}
			const FString PreviousSource = GetAssetMetadataValue(Asset, SourceFileMetadataKey);
			const FString PreviousFingerprint = GetAssetMetadataValue(Asset, SourceFingerprintMetadataKey);
			if (PreviousSource.Equals(SourceFile, ESearchCase::IgnoreCase)
				&& PreviousFingerprint.Equals(Fingerprint, ESearchCase::CaseSensitive))
			{
				Result.AddUnique(Asset);
			}
		}
		return OrderAssetPipelineObjects(Result);
	}

	static bool SaveObjectPackages(const TArray<UObject*>& Objects)
	{
		TSet<UPackage*> UniquePackages;
		for (UObject* Object : Objects)
		{
			if (Object && Object->GetOutermost())
			{
				UniquePackages.Add(Object->GetOutermost());
			}
		}
		bool bAllSaved = true;
		for (UPackage* Package : UniquePackages)
		{
			if (!Package || !FPackageName::IsValidLongPackageName(Package->GetName()))
			{
				bAllSaved = false;
				continue;
			}
			UObject* Asset = Package->FindAssetInPackage();
			const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			SaveArgs.SaveFlags = SAVE_NoError;
			Package->MarkPackageDirty();
			bAllSaved &= UPackage::SavePackage(Package, Asset, *Filename, SaveArgs);
		}
		return bAllSaved;
	}

	static bool ExtractAssetPaths(const TSharedPtr<FJsonObject>& Request, TArray<FString>& OutPaths)
	{
		OutPaths.Reset();
		if (!Request.IsValid())
		{
			return false;
		}
		FString Single;
		if ((Request->TryGetStringField(TEXT("path"), Single)
			|| Request->TryGetStringField(TEXT("target"), Single))
			&& !Single.TrimStartAndEnd().IsEmpty())
		{
			OutPaths.AddUnique(NormalizeObjectPath(Single));
		}
		const TArray<TSharedPtr<FJsonValue>>* Paths = nullptr;
		if (Request->TryGetArrayField(TEXT("paths"), Paths) && Paths)
		{
			for (const TSharedPtr<FJsonValue>& Value : *Paths)
			{
				if (Value.IsValid() && Value->Type == EJson::String && !Value->AsString().TrimStartAndEnd().IsEmpty())
				{
					OutPaths.AddUnique(NormalizeObjectPath(Value->AsString()));
				}
			}
		}
		return OutPaths.Num() > 0;
	}

	static TArray<TSharedPtr<FJsonValue>> StringArrayToJson(const TArray<FString>& Values)
	{
		TArray<TSharedPtr<FJsonValue>> Result;
		for (const FString& Value : Values)
		{
			Result.Add(MakeShared<FJsonValueString>(Value));
		}
		return Result;
	}

	static TArray<TSharedPtr<FJsonValue>> NameArrayToJson(const TArray<FName>& Values)
	{
		TArray<TSharedPtr<FJsonValue>> Result;
		for (const FName& Value : Values)
		{
			Result.Add(MakeShared<FJsonValueString>(Value.ToString()));
		}
		return Result;
	}

	static void AddBounds(TSharedRef<FJsonObject> Object, const FBoxSphereBounds& Bounds)
	{
		TSharedRef<FJsonObject> BoundsObject = MakeShared<FJsonObject>();
		BoundsObject->SetArrayField(TEXT("origin"), {
			MakeShared<FJsonValueNumber>(Bounds.Origin.X),
			MakeShared<FJsonValueNumber>(Bounds.Origin.Y),
			MakeShared<FJsonValueNumber>(Bounds.Origin.Z)
		});
		BoundsObject->SetArrayField(TEXT("extent"), {
			MakeShared<FJsonValueNumber>(Bounds.BoxExtent.X),
			MakeShared<FJsonValueNumber>(Bounds.BoxExtent.Y),
			MakeShared<FJsonValueNumber>(Bounds.BoxExtent.Z)
		});
		BoundsObject->SetNumberField(TEXT("sphereRadius"), Bounds.SphereRadius);
		Object->SetObjectField(TEXT("bounds"), BoundsObject);
	}

	static void AddMaterialSlots(TSharedRef<FJsonObject> Object, UObject* Asset)
	{
		TArray<TSharedPtr<FJsonValue>> Slots;
		if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(Asset))
		{
			for (int32 Index = 0; Index < StaticMesh->GetStaticMaterials().Num(); ++Index)
			{
				const FStaticMaterial& Material = StaticMesh->GetStaticMaterials()[Index];
				TSharedRef<FJsonObject> Slot = MakeShared<FJsonObject>();
				Slot->SetNumberField(TEXT("index"), Index);
				Slot->SetStringField(TEXT("name"), Material.MaterialSlotName.ToString());
				Slot->SetStringField(TEXT("importedName"), Material.ImportedMaterialSlotName.ToString());
				Slot->SetStringField(TEXT("material"), Material.MaterialInterface ? Material.MaterialInterface->GetPathName() : FString());
				Slots.Add(MakeObjectValue(Slot));
			}
		}
		else if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Asset))
		{
			for (int32 Index = 0; Index < SkeletalMesh->GetMaterials().Num(); ++Index)
			{
				const FSkeletalMaterial& Material = SkeletalMesh->GetMaterials()[Index];
				TSharedRef<FJsonObject> Slot = MakeShared<FJsonObject>();
				Slot->SetNumberField(TEXT("index"), Index);
				Slot->SetStringField(TEXT("name"), Material.MaterialSlotName.ToString());
				Slot->SetStringField(TEXT("importedName"), Material.ImportedMaterialSlotName.ToString());
				Slot->SetStringField(TEXT("material"), Material.MaterialInterface ? Material.MaterialInterface->GetPathName() : FString());
				Slots.Add(MakeObjectValue(Slot));
			}
		}
		Object->SetArrayField(TEXT("materialSlots"), Slots);
	}

	static TSharedRef<FJsonObject> DescribeAsset(UObject* Asset, const bool bIncludeDependencies, const bool bIncludeReferencers)
	{
		TSharedRef<FJsonObject> Description = MakeShared<FJsonObject>();
		Description->SetStringField(TEXT("objectPath"), Asset->GetPathName());
		Description->SetStringField(TEXT("package"), Asset->GetOutermost()->GetName());
		Description->SetStringField(TEXT("name"), Asset->GetName());
		Description->SetStringField(TEXT("class"), Asset->GetClass()->GetName());
		Description->SetStringField(TEXT("classPath"), Asset->GetClass()->GetPathName());
		Description->SetBoolField(TEXT("dirty"), Asset->GetOutermost()->IsDirty());

		TSharedRef<FJsonObject> Pipeline = MakeShared<FJsonObject>();
		Pipeline->SetStringField(TEXT("sourceFile"), GetAssetMetadataValue(Asset, SourceFileMetadataKey));
		Pipeline->SetStringField(TEXT("sourceFingerprint"), GetAssetMetadataValue(Asset, SourceFingerprintMetadataKey));
		Pipeline->SetStringField(TEXT("pipelineVersion"), GetAssetMetadataValue(Asset, PipelineVersionMetadataKey));
		Description->SetObjectField(TEXT("pipeline"), Pipeline);

		if (FObjectPropertyBase* ImportDataProperty = FindFProperty<FObjectPropertyBase>(Asset->GetClass(), TEXT("AssetImportData")))
		{
			if (UAssetImportData* ImportData = Cast<UAssetImportData>(ImportDataProperty->GetObjectPropertyValue_InContainer(Asset)))
			{
				Description->SetArrayField(TEXT("sourceFiles"), StringArrayToJson(ImportData->ExtractFilenames()));
			}
		}

		if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(Asset))
		{
			Description->SetStringField(TEXT("assetType"), TEXT("static_mesh"));
			Description->SetNumberField(TEXT("lodCount"), StaticMesh->GetNumLODs());
			Description->SetNumberField(TEXT("vertexCountLod0"), StaticMesh->GetNumLODs() > 0 ? StaticMesh->GetNumVertices(0) : 0);
			Description->SetNumberField(TEXT("sectionCountLod0"), StaticMesh->GetNumLODs() > 0 ? StaticMesh->GetNumSections(0) : 0);
			Description->SetBoolField(TEXT("naniteEnabled"), IsNaniteEnabled(StaticMesh));
			Description->SetNumberField(TEXT("simpleCollisionShapeCount"), StaticMesh->GetBodySetup() ? StaticMesh->GetBodySetup()->AggGeom.GetElementCount() : 0);
			AddBounds(Description, StaticMesh->GetBounds());
			AddMaterialSlots(Description, StaticMesh);
		}
		else if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Asset))
		{
			Description->SetStringField(TEXT("assetType"), TEXT("skeletal_mesh"));
			Description->SetNumberField(TEXT("lodCount"), SkeletalMesh->GetLODNum());
			Description->SetNumberField(TEXT("boneCount"), SkeletalMesh->GetRefSkeleton().GetRawBoneNum());
			Description->SetNumberField(TEXT("morphTargetCount"), SkeletalMesh->GetMorphTargets().Num());
			Description->SetStringField(TEXT("skeleton"), SkeletalMesh->GetSkeleton() ? SkeletalMesh->GetSkeleton()->GetPathName() : FString());
			Description->SetStringField(TEXT("physicsAsset"), SkeletalMesh->GetPhysicsAsset() ? SkeletalMesh->GetPhysicsAsset()->GetPathName() : FString());
			AddBounds(Description, SkeletalMesh->GetBounds());
			AddMaterialSlots(Description, SkeletalMesh);
		}
		else if (UAnimSequence* Animation = Cast<UAnimSequence>(Asset))
		{
			Description->SetStringField(TEXT("assetType"), TEXT("animation"));
			Description->SetStringField(TEXT("skeleton"), Animation->GetSkeleton() ? Animation->GetSkeleton()->GetPathName() : FString());
			Description->SetNumberField(TEXT("durationSeconds"), Animation->GetPlayLength());
			Description->SetNumberField(TEXT("sampledKeyCount"), Animation->GetNumberOfSampledKeys());
		}
		else if (UTexture* Texture = Cast<UTexture>(Asset))
		{
			Description->SetStringField(TEXT("assetType"), TEXT("texture"));
			Description->SetNumberField(TEXT("width"), Texture->GetSurfaceWidth());
			Description->SetNumberField(TEXT("height"), Texture->GetSurfaceHeight());
			Description->SetBoolField(TEXT("sRGB"), Texture->SRGB);
			Description->SetNumberField(TEXT("compressionSettings"), static_cast<int32>(Texture->CompressionSettings));
		}
		else if (USoundWave* SoundWave = Cast<USoundWave>(Asset))
		{
			Description->SetStringField(TEXT("assetType"), TEXT("audio"));
			Description->SetNumberField(TEXT("durationSeconds"), SoundWave->GetDuration());
			Description->SetNumberField(TEXT("channelCount"), SoundWave->NumChannels);
		}
		else if (Asset->IsA<UMaterialInterface>())
		{
			Description->SetStringField(TEXT("assetType"), TEXT("material"));
		}
		else if (Asset->IsA<UPhysicsAsset>())
		{
			Description->SetStringField(TEXT("assetType"), TEXT("physics_asset"));
		}
		else if (Asset->IsA<USkeleton>())
		{
			Description->SetStringField(TEXT("assetType"), TEXT("skeleton"));
		}
		else
		{
			Description->SetStringField(TEXT("assetType"), TEXT("other"));
		}

		if (bIncludeDependencies || bIncludeReferencers)
		{
			FAssetRegistryModule& RegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
			if (bIncludeDependencies)
			{
				TArray<FName> Dependencies;
				RegistryModule.Get().GetDependencies(FName(*Asset->GetOutermost()->GetName()), Dependencies);
				Description->SetArrayField(TEXT("dependencies"), NameArrayToJson(Dependencies));
			}
			if (bIncludeReferencers)
			{
				TArray<FName> Referencers;
				RegistryModule.Get().GetReferencers(FName(*Asset->GetOutermost()->GetName()), Referencers);
				Description->SetArrayField(TEXT("referencers"), NameArrayToJson(Referencers));
			}
		}
		return Description;
	}

	static TSharedRef<FJsonObject> MakeIssue(
		const FString& Severity,
		const FString& Code,
		const FString& Message,
		const UObject* Asset,
		const FString& SuggestedAction = FString())
	{
		TSharedRef<FJsonObject> Issue = MakeShared<FJsonObject>();
		Issue->SetStringField(TEXT("severity"), Severity);
		Issue->SetStringField(TEXT("code"), Code);
		Issue->SetStringField(TEXT("message"), Message);
		Issue->SetStringField(TEXT("asset"), Asset ? Asset->GetPathName() : FString());
		Issue->SetBoolField(TEXT("recoverable"), !SuggestedAction.IsEmpty());
		if (!SuggestedAction.IsEmpty())
		{
			Issue->SetStringField(TEXT("suggestedAction"), SuggestedAction);
		}
		return Issue;
	}

	static int32 CountMissingMaterials(UObject* Asset)
	{
		int32 Missing = 0;
		if (const UStaticMesh* StaticMesh = Cast<UStaticMesh>(Asset))
		{
			for (const FStaticMaterial& Slot : StaticMesh->GetStaticMaterials())
			{
				Missing += Slot.MaterialInterface == nullptr ? 1 : 0;
			}
		}
		else if (const USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Asset))
		{
			for (const FSkeletalMaterial& Slot : SkeletalMesh->GetMaterials())
			{
				Missing += Slot.MaterialInterface == nullptr ? 1 : 0;
			}
		}
		return Missing;
	}

	static void ValidateAssetObject(
		UObject* Asset,
		const TSharedPtr<FJsonObject>& Rules,
		const FString& Profile,
		TArray<TSharedPtr<FJsonValue>>& Issues,
		int32& ErrorCount,
		int32& WarningCount)
	{
		auto AddIssue = [&](const FString& Severity, const FString& Code, const FString& Message, const FString& SuggestedAction = FString())
		{
			Issues.Add(MakeObjectValue(MakeIssue(Severity, Code, Message, Asset, SuggestedAction)));
			if (Severity == TEXT("error")) ++ErrorCount;
			else if (Severity == TEXT("warning")) ++WarningCount;
		};

		const bool bStrict = Profile.Equals(TEXT("strict"), ESearchCase::IgnoreCase)
			|| Profile.Equals(TEXT("fab"), ESearchCase::IgnoreCase)
			|| Profile.Equals(TEXT("production"), ESearchCase::IgnoreCase);
		const bool bRequireMaterials = TryGetBool(Rules, TEXT("require_materials"), true);
		const bool bRequireSource = TryGetBool(Rules, TEXT("require_source_metadata"), bStrict);

		if (bRequireSource)
		{
			if (GetAssetMetadataValue(Asset, SourceFingerprintMetadataKey).IsEmpty())
			{
				AddIssue(TEXT("error"), TEXT("source_fingerprint_missing"), TEXT("Asset has no BAT source fingerprint."), TEXT("Reimport through /asset/import."));
			}
		}

		if (bRequireMaterials && CountMissingMaterials(Asset) > 0)
		{
			AddIssue(TEXT("error"), TEXT("material_slot_unassigned"), TEXT("One or more material slots are unassigned."), TEXT("Use /asset/configure material_assignments."));
		}

		if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(Asset))
		{
			if (StaticMesh->GetNumLODs() <= 0 || StaticMesh->GetNumVertices(0) <= 0)
			{
				AddIssue(TEXT("error"), TEXT("static_mesh_empty"), TEXT("Static Mesh has no renderable LOD0 geometry."), TEXT("Reimport the source mesh."));
			}
			const bool bRequireCollision = TryGetBool(Rules, TEXT("require_collision"), bStrict);
			if (bRequireCollision && (!StaticMesh->GetBodySetup() || StaticMesh->GetBodySetup()->AggGeom.GetElementCount() <= 0))
			{
				AddIssue(TEXT("error"), TEXT("collision_missing"), TEXT("Static Mesh has no simple collision."), TEXT("Use /asset/configure with create_box_collision=true."));
			}
			if (TryGetBool(Rules, TEXT("require_nanite"), false) && !IsNaniteEnabled(StaticMesh))
			{
				AddIssue(TEXT("error"), TEXT("nanite_disabled"), TEXT("Nanite is required by the selected validation rules."), TEXT("Enable Nanite through /asset/configure."));
			}
		}
		else if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Asset))
		{
			const int32 MinBones = FMath::Max(1, TryGetInt(Rules, TEXT("minimum_bones"), 1));
			if (SkeletalMesh->GetRefSkeleton().GetRawBoneNum() < MinBones)
			{
				AddIssue(TEXT("error"), TEXT("skeleton_bone_count_low"), FString::Printf(TEXT("Skeletal Mesh has fewer than %d bones."), MinBones), TEXT("Reimport as a rigged Skeletal Mesh."));
			}
			if (!SkeletalMesh->GetSkeleton())
			{
				AddIssue(TEXT("error"), TEXT("skeleton_missing"), TEXT("Skeletal Mesh has no Skeleton asset."), TEXT("Reimport with skeleton creation enabled."));
			}
			const bool bRequirePhysics = TryGetBool(Rules, TEXT("require_physics_asset"), bStrict);
			if (bRequirePhysics && !SkeletalMesh->GetPhysicsAsset())
			{
				AddIssue(TEXT("error"), TEXT("physics_asset_missing"), TEXT("Skeletal Mesh has no Physics Asset."), TEXT("Use /asset/configure with create_physics_asset=true."));
			}
			else if (bRequirePhysics && SkeletalMesh->GetPhysicsAsset()->SkeletalBodySetups.Num() == 0)
			{
				AddIssue(TEXT("error"), TEXT("physics_asset_empty"), TEXT("The assigned Physics Asset contains no skeletal bodies."), TEXT("Regenerate or author bodies in the Physics Asset."));
			}
		}
		else if (UAnimSequence* Animation = Cast<UAnimSequence>(Asset))
		{
			if (!Animation->GetSkeleton())
			{
				AddIssue(TEXT("error"), TEXT("animation_skeleton_missing"), TEXT("Animation has no Skeleton."));
			}
			if (Animation->GetPlayLength() <= 0.0f || Animation->GetNumberOfSampledKeys() <= 0)
			{
				AddIssue(TEXT("error"), TEXT("animation_empty"), TEXT("Animation has no playable samples."), TEXT("Reimport animation tracks."));
			}
		}
		else if (UTexture* Texture = Cast<UTexture>(Asset))
		{
			if (Texture->GetSurfaceWidth() <= 0 || Texture->GetSurfaceHeight() <= 0)
			{
				AddIssue(TEXT("error"), TEXT("texture_empty"), TEXT("Texture has invalid dimensions."), TEXT("Reimport the source image."));
			}
			if (Texture->GetName().Contains(TEXT("Normal"), ESearchCase::IgnoreCase))
			{
				const FString Severity = bStrict ? TEXT("error") : TEXT("warning");
				if (Texture->SRGB)
				{
					AddIssue(Severity, TEXT("normal_map_srgb_enabled"), TEXT("Normal-map texture has sRGB enabled."), TEXT("Use /asset/configure mode=safe_auto or texture.srgb=false."));
				}
				if (Texture->CompressionSettings != TC_Normalmap)
				{
					AddIssue(Severity, TEXT("normal_map_compression_invalid"), TEXT("Normal-map texture is not using normal-map compression."), TEXT("Use /asset/configure mode=safe_auto."));
				}
			}
		}
		else if (USoundWave* SoundWave = Cast<USoundWave>(Asset))
		{
			if (SoundWave->GetDuration() <= 0.0f || SoundWave->NumChannels <= 0)
			{
				AddIssue(TEXT("error"), TEXT("audio_empty"), TEXT("Sound Wave has no playable duration or channels."), TEXT("Reimport the source audio."));
			}
		}
	}

	static int32 FindMaterialSlot(UObject* Asset, const FString& SlotName)
	{
		if (SlotName.IsNumeric())
		{
			return FCString::Atoi(*SlotName);
		}
		if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(Asset))
		{
			for (int32 Index = 0; Index < StaticMesh->GetStaticMaterials().Num(); ++Index)
			{
				const FStaticMaterial& Slot = StaticMesh->GetStaticMaterials()[Index];
				if (Slot.MaterialSlotName.ToString().Equals(SlotName, ESearchCase::IgnoreCase)
					|| Slot.ImportedMaterialSlotName.ToString().Equals(SlotName, ESearchCase::IgnoreCase))
				{
					return Index;
				}
			}
		}
		else if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Asset))
		{
			for (int32 Index = 0; Index < SkeletalMesh->GetMaterials().Num(); ++Index)
			{
				const FSkeletalMaterial& Slot = SkeletalMesh->GetMaterials()[Index];
				if (Slot.MaterialSlotName.ToString().Equals(SlotName, ESearchCase::IgnoreCase)
					|| Slot.ImportedMaterialSlotName.ToString().Equals(SlotName, ESearchCase::IgnoreCase))
				{
					return Index;
				}
			}
		}
		return INDEX_NONE;
	}

	static bool ApplyMaterialAssignments(UObject* Asset, const TSharedPtr<FJsonObject>& Assignments, int32& OutApplied, FString& OutError)
	{
		if (!Assignments.IsValid())
		{
			return true;
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Assignments->Values)
		{
			if (!Pair.Value.IsValid() || Pair.Value->Type != EJson::String)
			{
				OutError = TEXT("Material assignment values must be object path strings.");
				return false;
			}
			UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *NormalizeObjectPath(Pair.Value->AsString()));
			const int32 SlotIndex = FindMaterialSlot(Asset, Pair.Key);
			if (!Material || SlotIndex < 0)
			{
				OutError = FString::Printf(TEXT("Unable to resolve material or slot: %s"), *Pair.Key);
				return false;
			}
			if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(Asset))
			{
				if (!StaticMesh->GetStaticMaterials().IsValidIndex(SlotIndex))
				{
					OutError = TEXT("Static Mesh material slot index is out of range.");
					return false;
				}
				StaticMesh->GetStaticMaterials()[SlotIndex].MaterialInterface = Material;
			}
			else if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Asset))
			{
				if (!SkeletalMesh->GetMaterials().IsValidIndex(SlotIndex))
				{
					OutError = TEXT("Skeletal Mesh material slot index is out of range.");
					return false;
				}
				SkeletalMesh->GetMaterials()[SlotIndex].MaterialInterface = Material;
			}
			else
			{
				OutError = TEXT("Material assignments require a Static Mesh or Skeletal Mesh.");
				return false;
			}
			++OutApplied;
		}
		return true;
	}

	static bool TryParseVector(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, FVector& OutValue)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Object.IsValid() || !Object->TryGetArrayField(Field, Values) || !Values)
		{
			return true;
		}
		if (Values->Num() != 3)
		{
			return false;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Values)
		{
			if (!Value.IsValid() || Value->Type != EJson::Number)
			{
				return false;
			}
		}
		OutValue = FVector(
			static_cast<float>((*Values)[0]->AsNumber()),
			static_cast<float>((*Values)[1]->AsNumber()),
			static_cast<float>((*Values)[2]->AsNumber()));
		return true;
	}

	static bool ResolveSafeCaptureDirectory(
		const TSharedPtr<FJsonObject>& Request,
		FString& OutDirectory,
		FString& OutError,
		const bool bCreateDirectory = true)
	{
		FString RelativeFolder = TEXT("BlueprintAutomationToolkit/Captures");
		if (Request.IsValid())
		{
			Request->TryGetStringField(TEXT("output_folder"), RelativeFolder);
		}
		RelativeFolder.TrimStartAndEndInline();
		if (RelativeFolder.IsEmpty())
		{
			RelativeFolder = TEXT("BlueprintAutomationToolkit/Captures");
		}
		if (!FPaths::IsRelative(RelativeFolder))
		{
			OutError = TEXT("output_folder must be relative to the project's Saved directory.");
			return false;
		}
		FPaths::NormalizeDirectoryName(RelativeFolder);
		if (!FPaths::CollapseRelativeDirectories(RelativeFolder) || RelativeFolder.StartsWith(TEXT("..")))
		{
			OutError = TEXT("output_folder may not escape the project's Saved directory.");
			return false;
		}
		OutDirectory = NormalizeDiskPath(FPaths::Combine(FPaths::ProjectSavedDir(), RelativeFolder));
		const FString SavedRoot = ResolveExistingDiskPath(FPaths::ProjectSavedDir());
		if (!IsPathInsideRoot(OutDirectory, FPaths::ProjectSavedDir()))
		{
			OutError = TEXT("Resolved capture directory is outside the project's Saved directory.");
			return false;
		}
		FString ExistingProbe = OutDirectory;
		while (!IFileManager::Get().DirectoryExists(*ExistingProbe))
		{
			const FString Parent = FPaths::GetPath(ExistingProbe);
			if (Parent.IsEmpty() || Parent == ExistingProbe)
			{
				break;
			}
			ExistingProbe = Parent;
		}
		if (!IFileManager::Get().DirectoryExists(*ExistingProbe)
			|| !IsPathInsideRoot(ResolveExistingDiskPath(ExistingProbe), SavedRoot))
		{
			OutError = TEXT("Capture directory resolves outside the project's Saved directory through a filesystem link.");
			return false;
		}
		if (bCreateDirectory)
		{
			if (!IFileManager::Get().MakeDirectory(*OutDirectory, true)
				|| !IsPathInsideRoot(ResolveExistingDiskPath(OutDirectory), SavedRoot))
			{
				OutError = TEXT("Capture directory could not be created safely beneath the project's Saved directory.");
				return false;
			}
		}
		return true;
	}

	static bool CaptureViewportPng(const FString& Filename, FString& OutError)
	{
		FViewport* Viewport = GEditor ? GEditor->GetActiveViewport() : nullptr;
		if (!Viewport && FModuleManager::Get().IsModuleLoaded(TEXT("LevelEditor")))
		{
			FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
			const TSharedPtr<IAssetViewport> AssetViewport = LevelEditorModule.GetFirstActiveViewport();
			Viewport = AssetViewport.IsValid() ? AssetViewport->GetActiveViewport() : nullptr;
		}
		if (!Viewport)
		{
			OutError = TEXT("No active editor viewport is available for capture.");
			return false;
		}

		Viewport->Draw(false);
		const FIntPoint Size = Viewport->GetSizeXY();
		TArray<FColor> Pixels;
		if (Size.X <= 0 || Size.Y <= 0 || !Viewport->ReadPixels(Pixels) || Pixels.Num() != Size.X * Size.Y)
		{
			OutError = TEXT("Failed to read pixels from the active editor viewport.");
			return false;
		}

		TArray64<uint8> Compressed;
		FImageUtils::PNGCompressImageArray(Size.X, Size.Y, Pixels, Compressed);
		if (Compressed.Num() == 0 || !FFileHelper::SaveArrayToFile(Compressed, *Filename))
		{
			OutError = TEXT("Failed to encode or save viewport PNG.");
			return false;
		}
		return true;
	}

	static TArray<FString> ExtractStringArrayField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		TArray<FString> Result;
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (Object.IsValid() && Object->TryGetArrayField(Field, Values) && Values)
		{
			for (const TSharedPtr<FJsonValue>& Value : *Values)
			{
				if (Value.IsValid() && Value->Type == EJson::String)
				{
					Result.Add(Value->AsString());
				}
			}
		}
		return Result;
	}

	static TSharedPtr<FJsonObject> ResultObject(const FAutomationResult& Result)
	{
		return Result.Data.IsValid() && Result.Data->Type == EJson::Object ? Result.Data->AsObject() : nullptr;
	}

}

FAutomationResult FAssetPipelineService::RunOnGameThread(
	FBlueprintAutomationToolkitModule& Module,
	TFunction<FAutomationResult()> Operation,
	const double TimeoutSeconds) const
{
	if (IsInGameThread())
	{
		return Operation();
	}
	TOptional<FAutomationResult> Result;
	const bool bRan = Module.RunOnGameThreadWait([&Result, &Operation]()
	{
		Result = Operation();
	}, TimeoutSeconds);
	if (!bRan)
	{
		return FAutomationResult::Error(TEXT("game_thread_timeout"), TEXT("Timed out waiting for Unreal's Game Thread."), 504);
	}
	return Result.IsSet()
		? Result.GetValue()
		: FAutomationResult::Error(TEXT("internal_error"), TEXT("Asset pipeline produced no result."), 500);
}

FAutomationResult FAssetPipelineService::DescribeImportFormats(FBlueprintAutomationToolkitModule& Module) const
{
	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetArrayField(TEXT("allowedRoots"), StringArrayToJson(GetAllowedImportRoots()));

	TArray<FString> ExtensionValues = GetAllowedExtensions().Array();
	ExtensionValues.Sort();
	Data->SetArrayField(TEXT("allowedExtensions"), StringArrayToJson(ExtensionValues));
	Data->SetNumberField(TEXT("maximumFileSizeMb"), GetDefault<UBlueprintAutomationToolkitSettings>()->AssetImportMaxFileSizeMb);
	Data->SetNumberField(TEXT("maximumBatchSizeMb"), GetDefault<UBlueprintAutomationToolkitSettings>()->AssetImportMaxBatchSizeMb);
	Data->SetArrayField(TEXT("commonModelFormats"), StringArrayToJson({
		TEXT("fbx"), TEXT("obj"), TEXT("gltf"), TEXT("glb"), TEXT("usd"), TEXT("usda"), TEXT("usdc"), TEXT("abc")
	}));
	Data->SetArrayField(TEXT("assetKinds"), StringArrayToJson({
		TEXT("static_mesh"), TEXT("skeletal_mesh"), TEXT("skeleton"), TEXT("animation"),
		TEXT("material"), TEXT("texture"), TEXT("audio"), TEXT("physics_asset")
	}));
	Data->SetArrayField(TEXT("fbxImportTypes"), StringArrayToJson({
		TEXT("auto"), TEXT("static_mesh"), TEXT("skeletal_mesh"), TEXT("animation")
	}));
	Data->SetBoolField(TEXT("customFactoryAdapters"), true);
	Data->SetBoolField(TEXT("dryRun"), true);
	Data->SetBoolField(TEXT("fingerprintSkip"), true);
	Data->SetStringField(TEXT("pipelineVersion"), PipelineVersion);
	return FAutomationResult::Ok(MakeObjectValue(Data));
}

FAutomationResult FAssetPipelineService::ImportAssets(FBlueprintAutomationToolkitModule& Module, const TSharedPtr<FJsonObject>& Request) const
{
	return RunOnGameThread(Module, [this, &Module, Request]()
	{
		return ImportAssetsOnGameThread(Module, Request);
	}, 300.0);
}

FAutomationResult FAssetPipelineService::InspectAssets(FBlueprintAutomationToolkitModule& Module, const TSharedPtr<FJsonObject>& Request) const
{
	return RunOnGameThread(Module, [this, &Module, Request]()
	{
		return InspectAssetsOnGameThread(Module, Request);
	}, 120.0);
}

FAutomationResult FAssetPipelineService::ConfigureAssets(FBlueprintAutomationToolkitModule& Module, const TSharedPtr<FJsonObject>& Request) const
{
	return RunOnGameThread(Module, [this, &Module, Request]()
	{
		return ConfigureAssetsOnGameThread(Module, Request);
	}, 300.0);
}

FAutomationResult FAssetPipelineService::ValidateAssets(FBlueprintAutomationToolkitModule& Module, const TSharedPtr<FJsonObject>& Request) const
{
	return RunOnGameThread(Module, [this, &Module, Request]()
	{
		return ValidateAssetsOnGameThread(Module, Request);
	}, 120.0);
}

FAutomationResult FAssetPipelineService::CreateShowcaseAndCapture(FBlueprintAutomationToolkitModule& Module, const TSharedPtr<FJsonObject>& Request) const
{
	return RunOnGameThread(Module, [this, &Module, Request]()
	{
		return CreateShowcaseAndCaptureOnGameThread(Module, Request);
	}, 300.0);
}

FAutomationResult FAssetPipelineService::ExecutePipeline(
	FBlueprintAutomationToolkitModule& Module,
	const TSharedPtr<FJsonObject>& Request,
	const FString& JobId) const
{
	return RunOnGameThread(Module, [this, &Module, Request, JobId]()
	{
		return ExecutePipelineOnGameThread(Module, Request, JobId);
	}, 900.0);
}

FAutomationResult FAssetPipelineService::ImportAssetsOnGameThread(
	FBlueprintAutomationToolkitModule& Module,
	const TSharedPtr<FJsonObject>& Request) const
{
	check(IsInGameThread());
	TArray<FBATImportEntry> Entries;
	FString ParseError;
	if (!ParseImportEntries(Request, Entries, ParseError))
	{
		return FAutomationResult::Error(TEXT("bad_args"), ParseError, 400);
	}

	FString DestinationPath;
	if (!Request->TryGetStringField(TEXT("destination"), DestinationPath)
		&& !Request->TryGetStringField(TEXT("destination_path"), DestinationPath))
	{
		return FAutomationResult::Error(TEXT("bad_args"), TEXT("'destination' is required."), 400);
	}
	DestinationPath.TrimStartAndEndInline();
	DestinationPath.RemoveFromEnd(TEXT("/"));
	if (!IsGameDestinationPath(DestinationPath))
	{
		return FAutomationResult::Error(TEXT("bad_destination"), TEXT("Destination must be a valid /Game package path."), 400);
	}

	const bool bDryRun = TryGetBool(Request, TEXT("dry_run"), false);
	const bool bReplaceExisting = TryGetBool(Request, TEXT("replace_existing"), false);
	const bool bReplaceSettings = TryGetBool(Request, TEXT("replace_existing_settings"), false);
	const bool bSave = TryGetBool(Request, TEXT("save"), true);
	const bool bSkipUnchanged = TryGetBool(Request, TEXT("skip_unchanged"), true);

	TArray<TSharedPtr<FJsonValue>> EntryResults;
	TArray<UObject*> ObjectsToSave;
	TArray<FString> ImportedPaths;
	TArray<FString> PrimaryPaths;
	TArray<FString> CreatedPaths;
	int32 ImportedCount = 0;
	int32 SkippedCount = 0;
	int64 BatchSourceBytes = 0;
	auto FailImport = [&](const FString& Code, const FString& Message, const int32 StatusCode)
	{
		int32 RollbackCount = 0;
		if (!bDryRun && CreatedPaths.Num() > 0)
		{
			TArray<UObject*> ObjectsToDelete;
			for (const FString& CreatedPath : CreatedPaths)
			{
				if (UObject* Object = LoadObject<UObject>(nullptr, *CreatedPath))
				{
					ObjectsToDelete.AddUnique(Object);
				}
			}
			if (ObjectsToDelete.Num() > 0)
			{
				RollbackCount = ObjectsToDelete.Num();
				ObjectTools::DeleteObjectsUnchecked(ObjectsToDelete);
			}
		}
		const FString RollbackNote = RollbackCount > 0
			? FString::Printf(TEXT(" Rolled back %d newly created asset(s); pre-existing or replaced assets were not deleted."), RollbackCount)
			: FString();
		return FAutomationResult::Error(Code, Message + RollbackNote, StatusCode);
	};

	for (const FBATImportEntry& Entry : Entries)
	{
		FString SourceFile;
		FString SourceError;
		if (!ResolveAndValidateSourceFile(Entry.SourceFile, SourceFile, SourceError))
		{
			return FailImport(TEXT("source_denied"), FString::Printf(TEXT("%s (%s)"), *SourceError, *Entry.SourceFile), 403);
		}
		BatchSourceBytes += FMath::Max<int64>(0, IFileManager::Get().FileSize(*SourceFile));
		const UBlueprintAutomationToolkitSettings* PipelineSettings = GetDefault<UBlueprintAutomationToolkitSettings>();
		const int64 MaxBatchBytes = static_cast<int64>(PipelineSettings ? PipelineSettings->AssetImportMaxBatchSizeMb : 4096) * 1024ll * 1024ll;
		if (BatchSourceBytes > MaxBatchBytes)
		{
			return FailImport(
				TEXT("batch_size_exceeded"),
				FString::Printf(TEXT("Combined primary source size exceeds the configured %d MB batch limit."), PipelineSettings ? PipelineSettings->AssetImportMaxBatchSizeMb : 4096),
				413);
		}

		const FString Fingerprint = HashFileMd5(SourceFile);
		if (Fingerprint.IsEmpty())
		{
			return FailImport(TEXT("source_hash_failed"), FString::Printf(TEXT("Could not fingerprint source: %s"), *SourceFile), 500);
		}

		TSharedRef<FJsonObject> EntryResult = MakeShared<FJsonObject>();
		EntryResult->SetStringField(TEXT("source"), SourceFile);
		EntryResult->SetStringField(TEXT("fingerprint"), Fingerprint);
		EntryResult->SetStringField(TEXT("destination"), DestinationPath);
		EntryResult->SetBoolField(TEXT("dryRun"), bDryRun);

		if (bSkipUnchanged)
		{
			const TArray<UObject*> ExistingAssets = FindUnchangedImportedAssets(DestinationPath, SourceFile, Fingerprint);
			if (UObject* PrimaryAsset = ChoosePrimaryAsset(ExistingAssets, Entry.ExpectedType))
			{
				TArray<FString> ExistingPaths;
				for (UObject* Existing : ExistingAssets)
				{
					if (Existing)
					{
						ExistingPaths.Add(Existing->GetPathName());
						ImportedPaths.AddUnique(Existing->GetPathName());
					}
				}
				EntryResult->SetStringField(TEXT("status"), TEXT("unchanged"));
				EntryResult->SetStringField(TEXT("primaryObjectPath"), PrimaryAsset->GetPathName());
				EntryResult->SetArrayField(TEXT("objectPaths"), StringArrayToJson(ExistingPaths));
				PrimaryPaths.AddUnique(PrimaryAsset->GetPathName());
				++SkippedCount;
				EntryResults.Add(MakeObjectValue(EntryResult));
				continue;
			}
		}

		if (bDryRun)
		{
			EntryResult->SetStringField(TEXT("status"), TEXT("ready"));
			EntryResult->SetStringField(TEXT("extension"), FPaths::GetExtension(SourceFile, false).ToLower());
			EntryResults.Add(MakeObjectValue(EntryResult));
			continue;
		}

		const TSet<FString> BeforePaths = GetAssetObjectPathsUnder(DestinationPath);
		UAssetImportTask* Task = NewObject<UAssetImportTask>(GetTransientPackage());
		Task->Filename = SourceFile;
		Task->DestinationPath = DestinationPath;
		Task->DestinationName = Entry.DestinationName.TrimStartAndEnd();
		Task->bAutomated = true;
		Task->bSave = false;
		Task->bAsync = false;
		Task->bReplaceExisting = bReplaceExisting;
		Task->bReplaceExistingSettings = bReplaceSettings;

		if (!Entry.FactoryClass.TrimStartAndEnd().IsEmpty())
		{
			FString AdapterError;
			Task->Factory = Cast<UFactory>(CreateClassInstance(Entry.FactoryClass, UFactory::StaticClass(), AdapterError));
			if (!Task->Factory)
			{
				return FailImport(TEXT("factory_denied"), AdapterError, 400);
			}
			if (!ApplyPropertyMap(Task->Factory, Entry.FactoryProperties, AdapterError))
			{
				return FailImport(TEXT("factory_property_invalid"), AdapterError, 400);
			}
		}

		FString AdapterError;
		if (!Entry.OptionsClass.TrimStartAndEnd().IsEmpty())
		{
			Task->Options = CreateClassInstance(Entry.OptionsClass, nullptr, AdapterError);
			if (!Task->Options)
			{
				return FailImport(TEXT("options_denied"), AdapterError, 400);
			}
		}
		else if (FPaths::GetExtension(SourceFile, false).Equals(TEXT("fbx"), ESearchCase::IgnoreCase)
			|| FPaths::GetExtension(SourceFile, false).Equals(TEXT("obj"), ESearchCase::IgnoreCase))
		{
			Task->Options = BuildFbxOptions(Entry.CommonOptions, AdapterError);
			if (!Task->Options)
			{
				return FailImport(TEXT("import_options_invalid"), AdapterError, 400);
			}
		}
		if (Task->Options && !ApplyPropertyMap(Task->Options, Entry.OptionsProperties, AdapterError))
		{
			return FailImport(TEXT("options_property_invalid"), AdapterError, 400);
		}

		TArray<UAssetImportTask*> Tasks;
		Tasks.Add(Task);
		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
		AssetToolsModule.Get().ImportAssetTasks(Tasks);

		TArray<UObject*> ImportedObjects;
		for (UObject* Object : Task->GetObjects())
		{
			if (Object)
			{
				ImportedObjects.AddUnique(Object);
			}
		}
		for (const FString& ObjectPath : Task->ImportedObjectPaths)
		{
			if (UObject* Object = LoadObject<UObject>(nullptr, *NormalizeObjectPath(ObjectPath)))
			{
				ImportedObjects.AddUnique(Object);
			}
		}
		if (ImportedObjects.Num() == 0)
		{
			return FailImport(TEXT("import_failed"), FString::Printf(TEXT("Unreal imported no objects from %s. Ensure an importer for this format is enabled."), *SourceFile), 422);
		}
		ImportedObjects = OrderAssetPipelineObjects(ImportedObjects);
		TArray<FString> EntryCreatedPaths;
		for (UObject* Object : ImportedObjects)
		{
			if (Object && !BeforePaths.Contains(Object->GetPathName()))
			{
				CreatedPaths.AddUnique(Object->GetPathName());
				EntryCreatedPaths.AddUnique(Object->GetPathName());
			}
		}
		UObject* PrimaryAsset = ChoosePrimaryAsset(ImportedObjects, Entry.ExpectedType);
		if (!PrimaryAsset)
		{
			return FailImport(
				TEXT("unexpected_asset_type"),
				FString::Printf(TEXT("Import produced no %s asset. Companion assets are allowed, but at least one expected type must be present."), *Entry.ExpectedType),
				422);
		}

		TArray<FString> EntryPaths;
		for (UObject* Object : ImportedObjects)
		{
			Object->Modify();
			SetAssetMetadataValue(Object, SourceFileMetadataKey, SourceFile);
			SetAssetMetadataValue(Object, SourceFingerprintMetadataKey, Fingerprint);
			SetAssetMetadataValue(Object, PipelineVersionMetadataKey, PipelineVersion);
			Object->MarkPackageDirty();
			ObjectsToSave.AddUnique(Object);
			EntryPaths.Add(Object->GetPathName());
			ImportedPaths.AddUnique(Object->GetPathName());
		}

		EntryResult->SetStringField(TEXT("status"), TEXT("imported"));
		EntryResult->SetStringField(TEXT("primaryObjectPath"), PrimaryAsset->GetPathName());
		EntryResult->SetArrayField(TEXT("objectPaths"), StringArrayToJson(EntryPaths));
		EntryResult->SetArrayField(TEXT("createdObjectPaths"), StringArrayToJson(EntryCreatedPaths));
		PrimaryPaths.AddUnique(PrimaryAsset->GetPathName());
		EntryResults.Add(MakeObjectValue(EntryResult));
		++ImportedCount;
	}

	if (!bDryRun && bSave && !SaveObjectPackages(ObjectsToSave))
	{
		return FailImport(TEXT("save_failed"), TEXT("One or more imported packages could not be saved."), 500);
	}

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("destination"), DestinationPath);
	Data->SetBoolField(TEXT("dryRun"), bDryRun);
	Data->SetNumberField(TEXT("importedCount"), ImportedCount);
	Data->SetNumberField(TEXT("skippedUnchangedCount"), SkippedCount);
	Data->SetArrayField(TEXT("entries"), EntryResults);
	Data->SetArrayField(TEXT("importedObjectPaths"), StringArrayToJson(ImportedPaths));
	Data->SetArrayField(TEXT("primaryObjectPaths"), StringArrayToJson(PrimaryPaths));
	Data->SetArrayField(TEXT("createdObjectPaths"), StringArrayToJson(CreatedPaths));
	Data->SetBoolField(TEXT("saved"), !bDryRun && bSave);
	Data->SetStringField(TEXT("pipelineVersion"), PipelineVersion);
	return FAutomationResult::Ok(MakeObjectValue(Data), (bDryRun || ImportedCount == 0) ? 200 : 201);
}

FAutomationResult FAssetPipelineService::InspectAssetsOnGameThread(
	FBlueprintAutomationToolkitModule& Module,
	const TSharedPtr<FJsonObject>& Request) const
{
	check(IsInGameThread());
	TArray<FString> Paths;
	if (!ExtractAssetPaths(Request, Paths))
	{
		return FAutomationResult::Error(TEXT("bad_args"), TEXT("Body requires 'path', 'target', or non-empty 'paths'."), 400);
	}
	const bool bDependencies = TryGetBool(Request, TEXT("include_dependencies"), true);
	const bool bReferencers = TryGetBool(Request, TEXT("include_referencers"), false);

	TArray<TSharedPtr<FJsonValue>> Assets;
	TMap<FString, int32> TypeCounts;
	for (const FString& Path : Paths)
	{
		UObject* Asset = LoadObject<UObject>(nullptr, *Path);
		if (!Asset)
		{
			return FAutomationResult::Error(TEXT("asset_not_found"), FString::Printf(TEXT("Asset not found: %s"), *Path), 404);
		}
		TSharedRef<FJsonObject> Description = DescribeAsset(Asset, bDependencies, bReferencers);
		FString Type;
		Description->TryGetStringField(TEXT("assetType"), Type);
		++TypeCounts.FindOrAdd(Type);
		Assets.Add(MakeObjectValue(Description));
	}

	TSharedRef<FJsonObject> Counts = MakeShared<FJsonObject>();
	for (const TPair<FString, int32>& Pair : TypeCounts)
	{
		Counts->SetNumberField(Pair.Key, Pair.Value);
	}
	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("assetCount"), Assets.Num());
	Data->SetObjectField(TEXT("typeCounts"), Counts);
	Data->SetArrayField(TEXT("assets"), Assets);
	return FAutomationResult::Ok(MakeObjectValue(Data));
}

FAutomationResult FAssetPipelineService::ConfigureAssetsOnGameThread(
	FBlueprintAutomationToolkitModule& Module,
	const TSharedPtr<FJsonObject>& Request) const
{
	check(IsInGameThread());
	TArray<FString> Paths;
	if (!ExtractAssetPaths(Request, Paths))
	{
		return FAutomationResult::Error(TEXT("bad_args"), TEXT("Body requires 'path', 'target', or non-empty 'paths'."), 400);
	}
	if (TryGetBool(Request, TEXT("dry_run"), false))
	{
		TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("dryRun"), true);
		Data->SetArrayField(TEXT("eligiblePaths"), StringArrayToJson(Paths));
		return FAutomationResult::Ok(MakeObjectValue(Data));
	}

	const bool bSave = TryGetBool(Request, TEXT("save"), true);
	const bool bReimport = TryGetBool(Request, TEXT("reimport"), false);
	FString Mode;
	Request->TryGetStringField(TEXT("mode"), Mode);
	const bool bSafeAuto = Mode.Equals(TEXT("safe_auto"), ESearchCase::IgnoreCase);
	TSharedPtr<FJsonObject> MaterialAssignments = GetObjectField(Request, TEXT("material_assignments"));
	TSharedPtr<FJsonObject> TextureOptions = GetObjectField(Request, TEXT("texture"));
	TSharedPtr<FJsonObject> StaticOptions = GetObjectField(Request, TEXT("static_mesh"));
	TSharedPtr<FJsonObject> SkeletalOptions = GetObjectField(Request, TEXT("skeletal_mesh"));

	TArray<UObject*> ModifiedObjects;
	TArray<TSharedPtr<FJsonValue>> Results;
	const FScopedTransaction Transaction(FText::FromString(TEXT("BAT: Configure Assets")));

	for (const FString& Path : Paths)
	{
		UObject* Asset = LoadObject<UObject>(nullptr, *Path);
		if (!Asset)
		{
			return FAutomationResult::Error(TEXT("asset_not_found"), FString::Printf(TEXT("Asset not found: %s"), *Path), 404);
		}
		Asset->Modify();
		int32 Applied = 0;
		TArray<FString> Actions;
		FString ConfigureError;

		if (bReimport)
		{
			if (!FReimportManager::Instance()->CanReimport(Asset)
				|| !FReimportManager::Instance()->Reimport(Asset, false, false, FString(), nullptr, INDEX_NONE, false, true))
			{
				return FAutomationResult::Error(TEXT("reimport_failed"), FString::Printf(TEXT("Automated reimport failed: %s"), *Path), 422);
			}
			Actions.Add(TEXT("reimported"));
			++Applied;
		}

		if (!ApplyMaterialAssignments(Asset, MaterialAssignments, Applied, ConfigureError))
		{
			return FAutomationResult::Error(TEXT("material_assignment_failed"), ConfigureError, 400);
		}
		if (MaterialAssignments.IsValid())
		{
			Actions.Add(TEXT("materials_assigned"));
		}

		if (UTexture* Texture = Cast<UTexture>(Asset))
		{
			if (TextureOptions.IsValid())
			{
				bool bSRGB = Texture->SRGB;
				if (TextureOptions->TryGetBoolField(TEXT("srgb"), bSRGB))
				{
					Texture->SRGB = bSRGB;
					++Applied;
				}
				double Compression = static_cast<double>(Texture->CompressionSettings);
				if (TextureOptions->TryGetNumberField(TEXT("compression_settings"), Compression))
				{
					Texture->CompressionSettings = static_cast<TextureCompressionSettings>(FMath::RoundToInt(Compression));
					++Applied;
				}
				Actions.Add(TEXT("texture_configured"));
			}
			else if (bSafeAuto && Texture->GetName().Contains(TEXT("Normal"), ESearchCase::IgnoreCase))
			{
				Texture->SRGB = false;
				Texture->CompressionSettings = TC_Normalmap;
				Actions.Add(TEXT("normal_map_defaults_applied"));
				Applied += 2;
			}
			Texture->PostEditChange();
		}
		else if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(Asset))
		{
			bool bNeedsBuild = false;
			if (StaticOptions.IsValid())
			{
				for (int32 LodIndex = 0; LodIndex < StaticMesh->GetNumSourceModels(); ++LodIndex)
				{
					FMeshBuildSettings& BuildSettings = StaticMesh->GetSourceModel(LodIndex).BuildSettings;
					bool Value = false;
					if (StaticOptions->TryGetBoolField(TEXT("recompute_normals"), Value))
					{
						BuildSettings.bRecomputeNormals = Value;
						++Applied;
						bNeedsBuild = true;
					}
					if (StaticOptions->TryGetBoolField(TEXT("recompute_tangents"), Value))
					{
						BuildSettings.bRecomputeTangents = Value;
						++Applied;
						bNeedsBuild = true;
					}
					if (StaticOptions->TryGetBoolField(TEXT("generate_lightmap_uvs"), Value))
					{
						BuildSettings.bGenerateLightmapUVs = Value;
						++Applied;
						bNeedsBuild = true;
					}
				}
				bool bNanite = IsNaniteEnabled(StaticMesh);
				if (StaticOptions->TryGetBoolField(TEXT("nanite"), bNanite))
				{
					SetNaniteEnabled(StaticMesh, bNanite);
					++Applied;
					bNeedsBuild = true;
				}
			}
			const bool bCreateCollision = TryGetBool(StaticOptions, TEXT("create_box_collision"), bSafeAuto);
			if (bCreateCollision && (!StaticMesh->GetBodySetup() || StaticMesh->GetBodySetup()->AggGeom.GetElementCount() == 0))
			{
				StaticMesh->CreateBodySetup();
				UBodySetup* BodySetup = StaticMesh->GetBodySetup();
				BodySetup->Modify();
				const FBoxSphereBounds Bounds = StaticMesh->GetBounds();
				FKBoxElem Box;
				Box.Center = Bounds.Origin;
				Box.X = FMath::Max(1.0f, Bounds.BoxExtent.X * 2.0f);
				Box.Y = FMath::Max(1.0f, Bounds.BoxExtent.Y * 2.0f);
				Box.Z = FMath::Max(1.0f, Bounds.BoxExtent.Z * 2.0f);
				BodySetup->AggGeom.BoxElems.Add(Box);
				BodySetup->CreatePhysicsMeshes();
				Actions.Add(TEXT("box_collision_created"));
				++Applied;
			}
			if (bNeedsBuild || TryGetBool(StaticOptions, TEXT("build"), false))
			{
				StaticMesh->Build(true);
				Actions.Add(TEXT("static_mesh_rebuilt"));
			}
			StaticMesh->PostEditChange();
		}
		else if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Asset))
		{
			FString SkeletonPath;
			if (SkeletalOptions.IsValid() && SkeletalOptions->TryGetStringField(TEXT("skeleton"), SkeletonPath))
			{
				USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *NormalizeObjectPath(SkeletonPath));
				if (!Skeleton)
				{
					return FAutomationResult::Error(TEXT("skeleton_not_found"), TEXT("skeletal_mesh.skeleton does not resolve to a Skeleton."), 404);
				}
				SkeletalMesh->SetSkeleton(Skeleton);
				Actions.Add(TEXT("skeleton_assigned"));
				++Applied;
			}

			const bool bCreatePhysics = TryGetBool(SkeletalOptions, TEXT("create_physics_asset"), bSafeAuto);
			if (bCreatePhysics && !SkeletalMesh->GetPhysicsAsset())
			{
				const FString PackagePath = FPackageName::GetLongPackagePath(SkeletalMesh->GetOutermost()->GetName());
				const FString PhysicsName = FString::Printf(TEXT("PHYS_%s"), *SkeletalMesh->GetName());
				const FString PhysicsPackageName = PackagePath / PhysicsName;
				UPackage* PhysicsPackage = CreatePackage(*PhysicsPackageName);
				UObject* Created = UPhysicsAssetFactory::CreatePhysicsAssetFromMesh(FName(*PhysicsName), PhysicsPackage, SkeletalMesh, true);
				UPhysicsAsset* PhysicsAsset = Cast<UPhysicsAsset>(Created);
				if (!PhysicsAsset)
				{
					return FAutomationResult::Error(TEXT("physics_asset_create_failed"), FString::Printf(TEXT("Failed to create Physics Asset for %s."), *Path), 500);
				}
				FAssetRegistryModule::AssetCreated(PhysicsAsset);
				PhysicsAsset->MarkPackageDirty();
				ModifiedObjects.AddUnique(PhysicsAsset);
				Actions.Add(TEXT("physics_asset_created"));
				++Applied;
			}
			SkeletalMesh->PostEditChange();
		}

		Asset->MarkPackageDirty();
		ModifiedObjects.AddUnique(Asset);
		TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("path"), Asset->GetPathName());
		Item->SetNumberField(TEXT("appliedChangeCount"), Applied);
		Item->SetArrayField(TEXT("actions"), StringArrayToJson(Actions));
		Results.Add(MakeObjectValue(Item));
	}

	if (bSave && !SaveObjectPackages(ModifiedObjects))
	{
		return FAutomationResult::Error(TEXT("save_failed"), TEXT("One or more configured packages could not be saved."), 500);
	}

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetArrayField(TEXT("results"), Results);
	Data->SetNumberField(TEXT("assetCount"), Results.Num());
	Data->SetBoolField(TEXT("saved"), bSave);
	return FAutomationResult::Ok(MakeObjectValue(Data));
}

FAutomationResult FAssetPipelineService::ValidateAssetsOnGameThread(
	FBlueprintAutomationToolkitModule& Module,
	const TSharedPtr<FJsonObject>& Request) const
{
	check(IsInGameThread());
	TArray<FString> Paths;
	if (!ExtractAssetPaths(Request, Paths))
	{
		return FAutomationResult::Error(TEXT("bad_args"), TEXT("Body requires 'path', 'target', or non-empty 'paths'."), 400);
	}
	FString Profile = TEXT("default");
	Request->TryGetStringField(TEXT("profile"), Profile);
	Profile.TrimStartAndEndInline();
	Profile.ToLowerInline();
	if (Profile != TEXT("default")
		&& Profile != TEXT("production")
		&& Profile != TEXT("strict")
		&& Profile != TEXT("fab"))
	{
		return FAutomationResult::Error(TEXT("invalid_validation_profile"), TEXT("profile must be default, production, strict, or fab."), 400);
	}
	TSharedPtr<FJsonObject> Rules = GetObjectField(Request, TEXT("rules"));

	TArray<TSharedPtr<FJsonValue>> Assets;
	int32 TotalErrors = 0;
	int32 TotalWarnings = 0;
	for (const FString& Path : Paths)
	{
		UObject* Asset = LoadObject<UObject>(nullptr, *Path);
		if (!Asset)
		{
			return FAutomationResult::Error(TEXT("asset_not_found"), FString::Printf(TEXT("Asset not found: %s"), *Path), 404);
		}
		TArray<TSharedPtr<FJsonValue>> Issues;
		int32 ErrorCount = 0;
		int32 WarningCount = 0;
		ValidateAssetObject(Asset, Rules, Profile, Issues, ErrorCount, WarningCount);
		TotalErrors += ErrorCount;
		TotalWarnings += WarningCount;

		TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("path"), Asset->GetPathName());
		Item->SetStringField(TEXT("class"), Asset->GetClass()->GetName());
		Item->SetBoolField(TEXT("valid"), ErrorCount == 0);
		Item->SetNumberField(TEXT("errorCount"), ErrorCount);
		Item->SetNumberField(TEXT("warningCount"), WarningCount);
		Item->SetNumberField(TEXT("score"), FMath::Clamp(100 - ErrorCount * 20 - WarningCount * 5, 0, 100));
		Item->SetArrayField(TEXT("issues"), Issues);
		Assets.Add(MakeObjectValue(Item));
	}

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("ok"), TotalErrors == 0);
	Data->SetStringField(TEXT("profile"), Profile);
	Data->SetBoolField(TEXT("valid"), TotalErrors == 0);
	Data->SetNumberField(TEXT("assetCount"), Assets.Num());
	Data->SetNumberField(TEXT("errorCount"), TotalErrors);
	Data->SetNumberField(TEXT("warningCount"), TotalWarnings);
	Data->SetNumberField(TEXT("score"), FMath::Clamp(100 - TotalErrors * 20 - TotalWarnings * 5, 0, 100));
	Data->SetArrayField(TEXT("assets"), Assets);
	FAutomationResult Result = FAutomationResult::Ok(MakeObjectValue(Data), TotalErrors == 0 ? 200 : 422);
	if (TotalErrors > 0)
	{
		Result.ErrorCode = TEXT("asset_validation_failed");
		Result.ErrorMessage = TEXT("One or more assets failed the requested validation profile.");
	}
	return Result;
}

FAutomationResult FAssetPipelineService::CreateShowcaseAndCaptureOnGameThread(
	FBlueprintAutomationToolkitModule& Module,
	const TSharedPtr<FJsonObject>& Request) const
{
	check(IsInGameThread());
	FString AssetPath;
	if (!Request.IsValid()
		|| (!Request->TryGetStringField(TEXT("asset"), AssetPath)
			&& !Request->TryGetStringField(TEXT("path"), AssetPath)))
	{
		return FAutomationResult::Error(TEXT("bad_args"), TEXT("Body requires 'asset'."), 400);
	}
	UObject* Asset = LoadObject<UObject>(nullptr, *NormalizeObjectPath(AssetPath));
	if (!Asset)
	{
		return FAutomationResult::Error(TEXT("asset_not_found"), TEXT("Showcase asset could not be loaded."), 404);
	}

	FVector Location = FVector::ZeroVector;
	FVector RotationVector = FVector::ZeroVector;
	FVector Scale = FVector::OneVector;
	if (!TryParseVector(Request, TEXT("location"), Location)
		|| !TryParseVector(Request, TEXT("rotation"), RotationVector)
		|| !TryParseVector(Request, TEXT("scale"), Scale))
	{
		return FAutomationResult::Error(TEXT("bad_args"), TEXT("location, rotation, and scale must be [x,y,z]."), 400);
	}

	FString ActorLabel = FString::Printf(TEXT("BAT_Showcase_%s"), *Asset->GetName());
	Request->TryGetStringField(TEXT("actor_label"), ActorLabel);
	UStaticMesh* RequestedStaticMesh = Cast<UStaticMesh>(Asset);
	USkeletalMesh* RequestedSkeletalMesh = Cast<USkeletalMesh>(Asset);
	if (!RequestedStaticMesh && !RequestedSkeletalMesh)
	{
		return FAutomationResult::Error(TEXT("unsupported_showcase_asset"), TEXT("Showcase currently accepts Static Mesh or Skeletal Mesh assets."), 400);
	}

	UAnimSequence* Animation = nullptr;
	FString AnimationPath;
	if (Request->TryGetStringField(TEXT("animation"), AnimationPath) && !AnimationPath.TrimStartAndEnd().IsEmpty())
	{
		if (!RequestedSkeletalMesh)
		{
			return FAutomationResult::Error(TEXT("animation_requires_skeletal_mesh"), TEXT("animation may only be used with a Skeletal Mesh showcase."), 400);
		}
		Animation = LoadObject<UAnimSequence>(nullptr, *NormalizeObjectPath(AnimationPath));
		if (!Animation)
		{
			return FAutomationResult::Error(TEXT("animation_not_found"), TEXT("animation does not resolve to an AnimSequence."), 404);
		}
		if (!Animation->GetSkeleton()
			|| !RequestedSkeletalMesh->GetSkeleton()
			|| Animation->GetSkeleton() != RequestedSkeletalMesh->GetSkeleton())
		{
			return FAutomationResult::Error(TEXT("animation_skeleton_mismatch"), TEXT("animation is not compatible with the showcase Skeletal Mesh Skeleton."), 422);
		}
	}

	const bool bCapture = TryGetBool(Request, TEXT("capture"), true);
	const bool bDryRun = TryGetBool(Request, TEXT("dry_run"), false);
	if (bDryRun)
	{
		FString OutputDirectory;
		if (bCapture)
		{
			FString CaptureError;
			if (!ResolveSafeCaptureDirectory(Request, OutputDirectory, CaptureError, false))
			{
				return FAutomationResult::Error(TEXT("capture_path_denied"), CaptureError, 403);
			}
		}
		const UBlueprintAutomationToolkitSettings* Settings = GetDefault<UBlueprintAutomationToolkitSettings>();
		const int32 MaxFrames = Settings ? Settings->AssetPipelineMaxCaptureFrames : 300;
		const int32 FrameCount = FMath::Clamp(TryGetInt(Request, TEXT("frame_count"), Animation ? 60 : 1), 1, MaxFrames);
		TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("dryRun"), true);
		Data->SetStringField(TEXT("asset"), Asset->GetPathName());
		Data->SetStringField(TEXT("assetType"), RequestedSkeletalMesh ? TEXT("skeletal_mesh") : TEXT("static_mesh"));
		Data->SetStringField(TEXT("animation"), Animation ? Animation->GetPathName() : FString());
		Data->SetStringField(TEXT("actorLabel"), ActorLabel);
		Data->SetBoolField(TEXT("wouldSpawnActor"), true);
		Data->SetBoolField(TEXT("wouldCapture"), bCapture);
		Data->SetNumberField(TEXT("frameCount"), bCapture ? FrameCount : 0);
		Data->SetStringField(TEXT("outputRoot"), OutputDirectory);
		return FAutomationResult::Ok(MakeObjectValue(Data));
	}

	UWorld* World = Module.GetEditorWorld();
	if (!World)
	{
		return FAutomationResult::Error(TEXT("editor_world_unavailable"), TEXT("Editor world is not available."), 503);
	}
	AActor* Actor = nullptr;
	USkeletalMeshComponent* SkeletalComponent = nullptr;
	const FScopedTransaction Transaction(FText::FromString(TEXT("BAT: Create Asset Showcase")));

	if (RequestedStaticMesh)
	{
		AStaticMeshActor* StaticActor = World->SpawnActor<AStaticMeshActor>(Location, FRotator(RotationVector.X, RotationVector.Y, RotationVector.Z));
		if (StaticActor)
		{
			StaticActor->GetStaticMeshComponent()->SetStaticMesh(RequestedStaticMesh);
			Actor = StaticActor;
		}
	}
	else if (RequestedSkeletalMesh)
	{
		ASkeletalMeshActor* SkeletalActor = World->SpawnActor<ASkeletalMeshActor>(Location, FRotator(RotationVector.X, RotationVector.Y, RotationVector.Z));
		if (SkeletalActor)
		{
			SkeletalComponent = SkeletalActor->GetSkeletalMeshComponent();
			SkeletalComponent->SetSkeletalMeshAsset(RequestedSkeletalMesh);
			Actor = SkeletalActor;
		}
	}
	if (!Actor)
	{
		return FAutomationResult::Error(TEXT("spawn_failed"), TEXT("Failed to spawn showcase actor."), 500);
	}

	Actor->Modify();
	Actor->SetActorLabel(ActorLabel);
	Actor->Tags.AddUnique(TEXT("BAT_Showcase"));
	Actor->SetActorScale3D(Scale);
	auto DestroyFailedShowcase = [&]()
	{
		if (Actor && World)
		{
			World->DestroyActor(Actor);
			Actor = nullptr;
		}
	};

	if (SkeletalComponent && Animation)
	{
		SkeletalComponent->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		SkeletalComponent->SetAnimation(Animation);
		SkeletalComponent->SetPosition(0.0f, false);
		SkeletalComponent->RefreshBoneTransforms();
	}

	if (GEditor)
	{
		GEditor->SelectNone(false, true);
		GEditor->SelectActor(Actor, true, true);
		GEditor->MoveViewportCamerasToActor(*Actor, false);
		GEditor->RedrawLevelEditingViewports(true);
	}

	TArray<FString> FramePaths;
	FString ManifestPath;
	FString CaptureId = FDateTime::UtcNow().ToString(TEXT("%Y%m%dT%H%M%SZ")) + TEXT("-") + FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8);
	if (bCapture)
	{
		FString OutputDirectory;
		FString CaptureError;
		if (!ResolveSafeCaptureDirectory(Request, OutputDirectory, CaptureError))
		{
			DestroyFailedShowcase();
			return FAutomationResult::Error(TEXT("capture_path_denied"), CaptureError, 403);
		}
		OutputDirectory = FPaths::Combine(OutputDirectory, CaptureId);
		IFileManager::Get().MakeDirectory(*OutputDirectory, true);

		const UBlueprintAutomationToolkitSettings* Settings = GetDefault<UBlueprintAutomationToolkitSettings>();
		const int32 MaxFrames = Settings ? Settings->AssetPipelineMaxCaptureFrames : 300;
		const int32 RequestedFrames = TryGetInt(Request, TEXT("frame_count"), Animation ? 60 : 1);
		const int32 FrameCount = FMath::Clamp(RequestedFrames, 1, MaxFrames);
		const int32 Fps = FMath::Clamp(TryGetInt(Request, TEXT("fps"), 30), 1, 120);
		const float Duration = Animation ? Animation->GetPlayLength() : 0.0f;

		for (int32 FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex)
		{
			if (Animation && SkeletalComponent)
			{
				const float Alpha = FrameCount > 1 ? static_cast<float>(FrameIndex) / static_cast<float>(FrameCount - 1) : 0.0f;
				SkeletalComponent->SetPosition(Duration * Alpha, false);
				SkeletalComponent->RefreshBoneTransforms();
				SkeletalComponent->MarkRenderTransformDirty();
				SkeletalComponent->MarkRenderDynamicDataDirty();
			}
			if (GEditor)
			{
				GEditor->RedrawLevelEditingViewports(true);
			}
			const FString FramePath = FPaths::Combine(OutputDirectory, FString::Printf(TEXT("frame_%04d.png"), FrameIndex));
			if (!CaptureViewportPng(FramePath, CaptureError))
			{
				DestroyFailedShowcase();
				return FAutomationResult::Error(TEXT("capture_failed"), CaptureError, 503);
			}
			FramePaths.Add(FramePath);
		}

		TSharedRef<FJsonObject> Manifest = MakeShared<FJsonObject>();
		Manifest->SetStringField(TEXT("captureId"), CaptureId);
		Manifest->SetStringField(TEXT("asset"), Asset->GetPathName());
		Manifest->SetStringField(TEXT("animation"), Animation ? Animation->GetPathName() : FString());
		Manifest->SetNumberField(TEXT("fps"), Fps);
		Manifest->SetNumberField(TEXT("frameCount"), FramePaths.Num());
		Manifest->SetNumberField(TEXT("durationSeconds"), Animation ? Animation->GetPlayLength() : 0.0);
		Manifest->SetStringField(TEXT("format"), FramePaths.Num() > 1 ? TEXT("png_sequence") : TEXT("png"));
		Manifest->SetArrayField(TEXT("frames"), StringArrayToJson(FramePaths));
		ManifestPath = FPaths::Combine(OutputDirectory, TEXT("capture-manifest.json"));
		FString ManifestText;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ManifestText);
		FJsonSerializer::Serialize(Manifest, Writer);
		if (!FFileHelper::SaveStringToFile(ManifestText, *ManifestPath))
		{
			DestroyFailedShowcase();
			return FAutomationResult::Error(TEXT("capture_manifest_failed"), TEXT("Failed to save the capture manifest."), 500);
		}
	}

	TSharedRef<FJsonObject> Evidence = MakeShared<FJsonObject>();
	Evidence->SetBoolField(TEXT("captured"), bCapture);
	Evidence->SetStringField(TEXT("captureId"), CaptureId);
	Evidence->SetStringField(TEXT("screenshot"), FramePaths.Num() > 0 ? FramePaths[0] : FString());
	Evidence->SetArrayField(TEXT("frames"), StringArrayToJson(FramePaths));
	Evidence->SetStringField(TEXT("manifest"), ManifestPath);
	Evidence->SetStringField(TEXT("format"), FramePaths.Num() > 1 ? TEXT("png_sequence") : TEXT("png"));
	Evidence->SetBoolField(TEXT("requiresVideoEncoding"), FramePaths.Num() > 1);

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset"), Asset->GetPathName());
	Data->SetStringField(TEXT("actor"), Actor->GetPathName());
	Data->SetStringField(TEXT("actorLabel"), ActorLabel);
	Data->SetStringField(TEXT("animation"), Animation ? Animation->GetPathName() : FString());
	Data->SetObjectField(TEXT("evidence"), Evidence);
	return FAutomationResult::Ok(MakeObjectValue(Data), 201);
}

FAutomationResult FAssetPipelineService::ExecutePipelineOnGameThread(
	FBlueprintAutomationToolkitModule& Module,
	const TSharedPtr<FJsonObject>& Request,
	const FString& JobId) const
{
	check(IsInGameThread());
	const TArray<TSharedPtr<FJsonValue>>* Steps = nullptr;
	if (!Request.IsValid() || !Request->TryGetArrayField(TEXT("steps"), Steps) || !Steps || Steps->Num() == 0)
	{
		return FAutomationResult::Error(TEXT("bad_args"), TEXT("Pipeline requires a non-empty 'steps' array."), 400);
	}
	if (Steps->Num() > 50)
	{
		return FAutomationResult::Error(TEXT("pipeline_too_large"), TEXT("Pipeline may contain at most 50 steps."), 400);
	}

	const bool bDryRun = TryGetBool(Request, TEXT("dry_run"), false);
	const bool bRollback = TryGetBool(Request, TEXT("rollback_on_failure"), true);
	const bool bContinueOnError = TryGetBool(Request, TEXT("continue_on_error"), false);
	TArray<FString> ContextPaths;
	TArray<FString> CreatedPaths;
	TArray<TSharedPtr<FJsonValue>> StepResults;
	int32 FailureCount = 0;
	bool bSawImportStep = false;

	for (int32 StepIndex = 0; StepIndex < Steps->Num(); ++StepIndex)
	{
		if (!(*Steps)[StepIndex].IsValid() || (*Steps)[StepIndex]->Type != EJson::Object)
		{
			return FAutomationResult::Error(TEXT("bad_args"), FString::Printf(TEXT("Pipeline step %d must be an object."), StepIndex), 400);
		}
		if (!JobId.IsEmpty())
		{
			FBlueprintAutomationToolkitModule::FJobRecord Job;
			if (Module.TryGetJob(JobId, Job) && (Job.bCancelRequested || Job.State == FBlueprintAutomationToolkitModule::EJobState::Canceled))
			{
				return FAutomationResult::Error(TEXT("job_canceled"), TEXT("Pipeline job was canceled."), 409);
			}
			Module.UpdateJobState(JobId, FBlueprintAutomationToolkitModule::EJobState::Running, 0.05 + (0.85 * StepIndex / Steps->Num()));
		}

		const TSharedPtr<FJsonObject> Step = (*Steps)[StepIndex]->AsObject();
		FString Operation;
		Step->TryGetStringField(TEXT("op"), Operation);
		Operation.ToLowerInline();
		const TSharedPtr<FJsonObject> StepPayload = GetObjectField(Step, TEXT("payload"));
		TSharedRef<FJsonObject> EffectivePayload = CloneJsonObject(StepPayload.IsValid() ? StepPayload : Step);
		EffectivePayload->RemoveField(TEXT("op"));
		EffectivePayload->RemoveField(TEXT("payload"));
		if (bDryRun)
		{
			EffectivePayload->SetBoolField(TEXT("dry_run"), true);
		}

		FString ExplicitPath;
		const bool bUsesImportedContext = EffectivePayload->TryGetStringField(TEXT("path"), ExplicitPath)
			&& ExplicitPath.Equals(TEXT("$imported"), ESearchCase::CaseSensitive);
		if (bUsesImportedContext)
		{
			EffectivePayload->RemoveField(TEXT("path"));
		}
		if ((bUsesImportedContext || (!EffectivePayload->HasField(TEXT("path")) && !EffectivePayload->HasField(TEXT("paths"))))
			&& ContextPaths.Num() > 0 && Operation != TEXT("import"))
		{
			EffectivePayload->SetArrayField(TEXT("paths"), StringArrayToJson(ContextPaths));
		}
		const bool bHasExplicitTarget = EffectivePayload->HasField(TEXT("path"))
			|| EffectivePayload->HasField(TEXT("paths"))
			|| EffectivePayload->HasField(TEXT("asset"))
			|| EffectivePayload->HasField(TEXT("target"));
		const bool bSupportsImportedContext = Operation == TEXT("inspect")
			|| Operation == TEXT("configure")
			|| Operation == TEXT("repair")
			|| Operation == TEXT("validate")
			|| Operation == TEXT("showcase")
			|| Operation == TEXT("capture");
		if (bDryRun
			&& Operation != TEXT("import")
			&& bSupportsImportedContext
			&& bSawImportStep
			&& ContextPaths.Num() == 0
			&& (bUsesImportedContext || !bHasExplicitTarget))
		{
			TSharedRef<FJsonObject> SkippedData = MakeShared<FJsonObject>();
			SkippedData->SetBoolField(TEXT("dryRun"), true);
			SkippedData->SetBoolField(TEXT("skipped"), true);
			SkippedData->SetStringField(TEXT("reason"), TEXT("No imported asset context exists during a dry-run import."));
			TSharedRef<FJsonObject> StepRecord = MakeShared<FJsonObject>();
			StepRecord->SetNumberField(TEXT("index"), StepIndex);
			StepRecord->SetStringField(TEXT("op"), Operation);
			StepRecord->SetBoolField(TEXT("ok"), true);
			StepRecord->SetNumberField(TEXT("status"), 200);
			StepRecord->SetObjectField(TEXT("data"), SkippedData);
			StepResults.Add(MakeObjectValue(StepRecord));
			if (!JobId.IsEmpty())
			{
				Module.AppendJobLog(JobId, FString::Printf(TEXT("asset_pipeline_step:%d:%s:skipped_dry_run"), StepIndex, *Operation));
			}
			continue;
		}

		FAutomationResult StepResult;
		if (Operation == TEXT("import"))
		{
			bSawImportStep = true;
			StepResult = ImportAssetsOnGameThread(Module, EffectivePayload);
		}
		else if (Operation == TEXT("inspect"))
		{
			StepResult = InspectAssetsOnGameThread(Module, EffectivePayload);
		}
		else if (Operation == TEXT("configure") || Operation == TEXT("repair"))
		{
			StepResult = ConfigureAssetsOnGameThread(Module, EffectivePayload);
		}
		else if (Operation == TEXT("validate"))
		{
			StepResult = ValidateAssetsOnGameThread(Module, EffectivePayload);
		}
		else if (Operation == TEXT("showcase") || Operation == TEXT("capture"))
		{
			if (!EffectivePayload->HasField(TEXT("asset")) && ContextPaths.Num() > 0)
			{
				FString ShowcasePath;
				FString AnimationPath;
				for (const FString& CandidatePath : ContextPaths)
				{
					if (UObject* Candidate = LoadObject<UObject>(nullptr, *NormalizeObjectPath(CandidatePath)))
					{
						if (ShowcasePath.IsEmpty() && (Candidate->IsA<USkeletalMesh>() || Candidate->IsA<UStaticMesh>()))
						{
							ShowcasePath = CandidatePath;
						}
						if (AnimationPath.IsEmpty() && Candidate->IsA<UAnimSequence>())
						{
							AnimationPath = CandidatePath;
						}
					}
				}
				EffectivePayload->SetStringField(TEXT("asset"), ShowcasePath.IsEmpty() ? ContextPaths[0] : ShowcasePath);
				if (!EffectivePayload->HasField(TEXT("animation")) && !AnimationPath.IsEmpty())
				{
					EffectivePayload->SetStringField(TEXT("animation"), AnimationPath);
				}
			}
			StepResult = CreateShowcaseAndCaptureOnGameThread(Module, EffectivePayload);
		}
		else
		{
			StepResult = FAutomationResult::Error(TEXT("unsupported_pipeline_op"), FString::Printf(TEXT("Unsupported pipeline operation: %s"), *Operation), 400);
		}

		TSharedRef<FJsonObject> StepRecord = MakeShared<FJsonObject>();
		StepRecord->SetNumberField(TEXT("index"), StepIndex);
		StepRecord->SetStringField(TEXT("op"), Operation);
		const TSharedPtr<FJsonObject> StepData = ResultObject(StepResult);
		bool bLogicalSuccess = StepResult.bSuccess;
		FString LogicalErrorCode = StepResult.ErrorCode;
		FString LogicalErrorMessage = StepResult.ErrorMessage;
		if (bLogicalSuccess && Operation == TEXT("validate") && StepData.IsValid())
		{
			bool bValid = true;
			if (StepData->TryGetBoolField(TEXT("valid"), bValid) && !bValid)
			{
				bLogicalSuccess = false;
				LogicalErrorCode = TEXT("asset_validation_failed");
				LogicalErrorMessage = TEXT("One or more imported assets failed the requested validation profile.");
			}
		}
		StepRecord->SetBoolField(TEXT("ok"), bLogicalSuccess);
		StepRecord->SetNumberField(TEXT("status"), StepResult.StatusCode);
		if (StepData.IsValid())
		{
			StepRecord->SetObjectField(TEXT("data"), StepData);
		}
		if (bLogicalSuccess)
		{
			if (StepData.IsValid())
			{
				const TArray<FString> Imported = ExtractStringArrayField(StepData, TEXT("importedObjectPaths"));
				if (Imported.Num() > 0)
				{
					ContextPaths = Imported;
				}
				for (const FString& Created : ExtractStringArrayField(StepData, TEXT("createdObjectPaths")))
				{
					CreatedPaths.AddUnique(Created);
				}
			}
		}
		else
		{
			++FailureCount;
			StepRecord->SetStringField(TEXT("errorCode"), LogicalErrorCode);
			StepRecord->SetStringField(TEXT("message"), LogicalErrorMessage);
		}
		StepResults.Add(MakeObjectValue(StepRecord));

		if (!JobId.IsEmpty())
		{
			Module.AppendJobLog(JobId, FString::Printf(TEXT("asset_pipeline_step:%d:%s:%s"), StepIndex, *Operation, bLogicalSuccess ? TEXT("ok") : TEXT("failed")));
		}
		if (!bLogicalSuccess && !bContinueOnError)
		{
			break;
		}
	}

	TArray<FString> RolledBackPaths;
	if (FailureCount > 0 && bRollback && !bDryRun && CreatedPaths.Num() > 0)
	{
		TArray<UObject*> ObjectsToDelete;
		for (const FString& CreatedPath : CreatedPaths)
		{
			if (UObject* Object = LoadObject<UObject>(nullptr, *CreatedPath))
			{
				ObjectsToDelete.Add(Object);
				RolledBackPaths.Add(CreatedPath);
			}
		}
		if (ObjectsToDelete.Num() > 0)
		{
			ObjectTools::DeleteObjectsUnchecked(ObjectsToDelete);
		}
	}

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("ok"), FailureCount == 0);
	Data->SetBoolField(TEXT("dryRun"), bDryRun);
	Data->SetNumberField(TEXT("stepCount"), StepResults.Num());
	Data->SetNumberField(TEXT("failureCount"), FailureCount);
	Data->SetArrayField(TEXT("steps"), StepResults);
	Data->SetArrayField(TEXT("contextPaths"), StringArrayToJson(ContextPaths));
	Data->SetArrayField(TEXT("createdObjectPaths"), StringArrayToJson(CreatedPaths));
	Data->SetArrayField(TEXT("rolledBackObjectPaths"), StringArrayToJson(RolledBackPaths));
	Data->SetStringField(TEXT("rollbackScope"), TEXT("Newly created imported assets only; replaced assets and saved configuration changes are not restored."));
	FAutomationResult Result = FAutomationResult::Ok(MakeObjectValue(Data), FailureCount == 0 ? 200 : 422);
	if (FailureCount > 0)
	{
		Result.ErrorCode = TEXT("asset_pipeline_failed");
		Result.ErrorMessage = TEXT("One or more asset pipeline steps failed. Inspect data.steps for details.");
	}
	return Result;
}

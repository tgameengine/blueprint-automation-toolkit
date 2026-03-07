#include "Services/AssetService.h"

#include "BlueprintAutomationToolkitModule.h"
#include "Domain/Requests/AssetSaveRequest.h"

#include "AssetToolsModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Modules/ModuleManager.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"

namespace
{
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
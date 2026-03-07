#pragma once

#include "CoreMinimal.h"

struct FBATAssetSaveRequest
{
	TArray<FString> Paths;
};

struct FBATAssetDuplicateEntry
{
	FString SourcePath;
	FString DestinationPath;
};

struct FBATAssetDuplicateRequest
{
	TArray<FBATAssetDuplicateEntry> Entries;
	bool bSave = false;
};
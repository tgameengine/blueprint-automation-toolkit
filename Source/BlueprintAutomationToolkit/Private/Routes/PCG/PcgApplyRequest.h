#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class FJsonValue;

struct FPcgApplyOptions
{
	FString Mode = TEXT("reconcile");
	FString Ownership = TEXT("bat");
	bool bCreateIfMissing = false;
	bool bClearUnmanaged = false;
	bool bUseTransaction = true;
	bool bSave = false;
};

struct FPcgApplyParameterEntry
{
	FString Name;
	FString Type;
	TSharedPtr<FJsonValue> DefaultValue;
	FString Description;
};

struct FPcgApplyMeshCategorySpec
{
	FString Name;
	TArray<FString> Meshes;
};

struct FPcgApplyMeshSetSpec
{
	bool bIsSet = false;
	FString Mode;
	TArray<FString> Meshes;
	TArray<FPcgApplyMeshCategorySpec> Categories;
};

struct FPcgApplyOpSpec
{
	FString Op;
	int32 SourceOpIndex = INDEX_NONE;
	FString Id;
	FString Node;
	FString Type;
	FString From;
	FString To;
	bool bHasExplicitX = false;
	bool bHasExplicitY = false;
	int32 X = 0;
	int32 Y = 0;
	TSharedPtr<FJsonObject> Settings;
	TArray<FPcgApplyParameterEntry> ParameterEntries;
	FPcgApplyMeshSetSpec MeshSet;
};

struct FPcgApplyRequest
{
	FString GraphPath;
	FPcgApplyOptions Options;
	TArray<FPcgApplyParameterEntry> Parameters;
	TArray<FPcgApplyOpSpec> Ops;
};

namespace BAT::PcgApplyRequest
{
	bool Parse(const TSharedPtr<FJsonObject>& BodyObj, FPcgApplyRequest& OutRequest, TArray<FString>& OutErrors);
}
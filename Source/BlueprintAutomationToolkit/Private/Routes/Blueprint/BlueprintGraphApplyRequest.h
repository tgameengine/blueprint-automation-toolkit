#pragma once

#include "CoreMinimal.h"

class FJsonObject;

struct FBlueprintGraphApplyOptions
{
	bool bCompile = false;
	bool bSave = false;
	bool bUseTransaction = true;
	bool bDryRun = false;
};

struct FBlueprintGraphApplyNodeSpec
{
	FString Id;
	FString Type;
	FString ForwardAxis;
	FString Event;
	FString ClassPath;
	FString Function;
	FString Message;
	FString Variable;
	FString Macro;
	int32 X = 0;
	int32 Y = 0;
	int32 Outputs = 0;
	TMap<FString, FString> Pins;
	TMap<FString, FString> Properties;
};

struct FBlueprintGraphApplyLinkSpec
{
	FString From;
	FString To;
};

struct FBlueprintGraphApplyRequest
{
	FString BlueprintPath;
	FString GraphName;
	FBlueprintGraphApplyOptions Options;
	TArray<FBlueprintGraphApplyNodeSpec> Nodes;
	TArray<FBlueprintGraphApplyLinkSpec> Links;
};

namespace BAT::BlueprintGraphApplyRequest
{
	bool Parse(const TSharedPtr<FJsonObject>& BodyObj, FBlueprintGraphApplyRequest& OutRequest, TArray<FString>& OutErrors);
}

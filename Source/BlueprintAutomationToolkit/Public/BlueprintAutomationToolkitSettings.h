// Copyright 2026 AkaSoft. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "BlueprintAutomationToolkitSettings.generated.h"

UCLASS(Config=EditorPerProjectUserSettings, DefaultConfig, meta=(DisplayName="Blueprint Automation Toolkit"))
class UBlueprintAutomationToolkitSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UBlueprintAutomationToolkitSettings();

	virtual FName GetCategoryName() const override;

	UPROPERTY(Config, EditAnywhere, Category="Server", meta=(DisplayName="Enable Server"))
	bool bEnableServer;

	UPROPERTY(Config, EditAnywhere, Category="Server", meta=(ClampMin="1", ClampMax="65535"))
	int32 Port;

	UPROPERTY(Config, EditAnywhere, Category="Security", meta=(DisplayName="Require Auth Token"))
	bool bRequireAuthToken;

	UPROPERTY(Config, EditAnywhere, Category="Security", meta=(DisplayName="Safe Mode"))
	bool bSafeMode;

	UPROPERTY(Config, EditAnywhere, Category="Security|Reflection", meta=(DisplayName="Reflection Allowed Classes"))
	TArray<FString> ReflectionAllowedClasses;

	UPROPERTY(Config, EditAnywhere, Category="Security|Reflection", meta=(DisplayName="Reflection Denied Classes"))
	TArray<FString> ReflectionDeniedClasses;

	UPROPERTY(Config, EditAnywhere, Category="Security|Reflection", meta=(DisplayName="Reflection Allowed Functions"))
	TArray<FString> ReflectionAllowedFunctions;

	UPROPERTY(Config, EditAnywhere, Category="Security|Reflection", meta=(DisplayName="Reflection Denied Functions"))
	TArray<FString> ReflectionDeniedFunctions;

	UPROPERTY(Config, EditAnywhere, Category="Security|Reflection", meta=(DisplayName="Reflection Allowed Properties"))
	TArray<FString> ReflectionAllowedProperties;

	UPROPERTY(Config, EditAnywhere, Category="Security|Reflection", meta=(DisplayName="Reflection Denied Properties"))
	TArray<FString> ReflectionDeniedProperties;

	/**
	 * Directories that may be used as sources by the asset import pipeline.
	 * Relative entries are resolved from the project directory. When empty, only
	 * the project directory is allowed.
	 */
	UPROPERTY(Config, EditAnywhere, Category="Security|Asset Pipeline", meta=(DisplayName="Asset Import Allowed Roots"))
	TArray<FString> AssetImportAllowedRoots;

	/** File extensions accepted by the asset import pipeline (without leading dots). */
	UPROPERTY(Config, EditAnywhere, Category="Security|Asset Pipeline", meta=(DisplayName="Asset Import Allowed Extensions"))
	TArray<FString> AssetImportAllowedExtensions;

	/** Maximum size of one source file accepted by the import pipeline. */
	UPROPERTY(Config, EditAnywhere, Category="Security|Asset Pipeline", meta=(ClampMin="1", ClampMax="16384", DisplayName="Maximum Import File Size (MB)"))
	int32 AssetImportMaxFileSizeMb;

	/** Maximum combined size of primary source files accepted by one batch request. */
	UPROPERTY(Config, EditAnywhere, Category="Security|Asset Pipeline", meta=(ClampMin="1", ClampMax="65536", DisplayName="Maximum Import Batch Size (MB)"))
	int32 AssetImportMaxBatchSizeMb;

	/** Maximum number of still frames produced by one evidence capture request. */
	UPROPERTY(Config, EditAnywhere, Category="Security|Asset Pipeline", meta=(ClampMin="1", ClampMax="1000", DisplayName="Maximum Capture Frames"))
	int32 AssetPipelineMaxCaptureFrames;
};

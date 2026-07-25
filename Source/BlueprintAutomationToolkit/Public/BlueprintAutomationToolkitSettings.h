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
};
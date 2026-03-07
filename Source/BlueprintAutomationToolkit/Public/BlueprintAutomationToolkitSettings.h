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
};
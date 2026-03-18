#pragma once

#include "CoreMinimal.h"

class UPCGSettings;

struct FPcgNodeFamilySpec
{
	FString ExternalType;
	TSubclassOf<UPCGSettings> SettingsClass;
	TSet<FString> SupportedSettingKeys;
	bool bSupportsMeshSet = false;
};

class FPcgNodeRegistry
{
public:
	static const FPcgNodeFamilySpec* FindByExternalType(const FString& ExternalType);
	static const FPcgNodeFamilySpec* FindBySettingsClass(const UClass* SettingsClass);
	static bool IsSupportedType(const FString& ExternalType);
	static bool IsSettingKeySupported(const FString& ExternalType, const FString& SettingKey);
	static void GetSupportedFamilies(TArray<FPcgNodeFamilySpec>& OutFamilies);
};
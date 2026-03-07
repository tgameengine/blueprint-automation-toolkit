#pragma once

#include "Automation/AutomationCommandRegistry.h"

#include "Modules/ModuleManager.h"

class BLUEPRINTAUTOMATIONTOOLKIT_API IBlueprintAutomationToolkitModule : public IModuleInterface
{
public:
	static inline IBlueprintAutomationToolkitModule& Get()
	{
		return FModuleManager::LoadModuleChecked<IBlueprintAutomationToolkitModule>(TEXT("BlueprintAutomationToolkit"));
	}

	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded(TEXT("BlueprintAutomationToolkit"));
	}

	virtual bool RegisterAutomationCommand(FBATAutomationCommandRegistration Registration, FString* OutError = nullptr) = 0;
	virtual bool UnregisterAutomationCommand(const FString& Endpoint, FString* OutError = nullptr) = 0;
	virtual bool HasAutomationCommand(const FString& Endpoint) const = 0;
	virtual void GetAutomationCommandInfos(TArray<FBATAutomationCommandInfo>& OutCommands) const = 0;
};
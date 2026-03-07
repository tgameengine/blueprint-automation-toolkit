#pragma once

#include "Automation/AutomationCommandRegistry.h"

class FCommandDispatcher
{
public:
	using FCommandFactory = FBATAutomationCommandFactory;
	using EPermissionTier = EBATAutomationPermissionTier;

	struct FRegistration
	{
		FCommandFactory Factory;
		EPermissionTier PermissionTier = EPermissionTier::Read;
		EBATAutomationPermission RequiredPermissions = EBATAutomationPermission::None;
		bool bBindRoute = false;
		bool bBlockDuringPie = false;
		bool bBuiltIn = false;
	};

	bool Register(FBATAutomationCommandRegistration Registration, bool bBuiltIn, FString* OutError = nullptr);
	bool Unregister(const FString& Endpoint, bool bAllowBuiltIn, FString* OutError = nullptr);
	bool HasCommand(const FString& Endpoint) const;
	bool TryGetRegistration(const FString& Endpoint, FRegistration& OutRegistration) const;
	void GetRegisteredCommands(TArray<FBATAutomationCommandInfo>& OutCommands) const;
	FAutomationResult Dispatch(const FString& Endpoint, FAutomationContext& Context) const;
	FAutomationResult Dispatch(FAutomationCommand& Command, FAutomationContext& Context) const;

private:
	TMap<FString, FRegistration> Registrations;
};

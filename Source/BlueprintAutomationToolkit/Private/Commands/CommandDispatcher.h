#pragma once

#include "Commands/AutomationCommand.h"

class FCommandDispatcher
{
public:
	using FCommandFactory = TFunction<TUniquePtr<FAutomationCommand>()>;
	enum class EPermissionTier : uint8
	{
		Read,
		Edit,
		Admin,
	};

	struct FRegistration
	{
		FCommandFactory Factory;
		EPermissionTier PermissionTier = EPermissionTier::Read;
	};

	void Register(const FString& Endpoint, FCommandFactory Factory, EPermissionTier PermissionTier = EPermissionTier::Read);
	void Unregister(const FString& Endpoint);
	bool HasCommand(const FString& Endpoint) const;
	bool TryGetRegistration(const FString& Endpoint, FRegistration& OutRegistration) const;
	FAutomationResult Dispatch(const FString& Endpoint, FAutomationContext& Context) const;
	FAutomationResult Dispatch(FAutomationCommand& Command, FAutomationContext& Context) const;

private:
	TMap<FString, FRegistration> Registrations;
};

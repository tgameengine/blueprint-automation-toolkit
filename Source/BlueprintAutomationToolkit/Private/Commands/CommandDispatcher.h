#pragma once

#include "Commands/AutomationCommand.h"

class FCommandDispatcher
{
public:
	using FCommandFactory = TFunction<TUniquePtr<FAutomationCommand>()>;

	void Register(const FString& Endpoint, FCommandFactory Factory);
	bool HasCommand(const FString& Endpoint) const;
	FAutomationResult Dispatch(const FString& Endpoint, FAutomationContext& Context) const;
	FAutomationResult Dispatch(FAutomationCommand& Command, FAutomationContext& Context) const;

private:
	TMap<FString, FCommandFactory> Factories;
};

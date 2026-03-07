#include "Commands/CommandDispatcher.h"

void FCommandDispatcher::Register(const FString& Endpoint, FCommandFactory Factory, EPermissionTier PermissionTier)
{
	if (!Endpoint.IsEmpty() && Factory)
	{
		FRegistration Registration;
		Registration.Factory = MoveTemp(Factory);
		Registration.PermissionTier = PermissionTier;
		Registrations.Add(Endpoint, MoveTemp(Registration));
	}
}

void FCommandDispatcher::Unregister(const FString& Endpoint)
{
	Registrations.Remove(Endpoint);
}

bool FCommandDispatcher::HasCommand(const FString& Endpoint) const
{
	return Registrations.Contains(Endpoint);
}

bool FCommandDispatcher::TryGetRegistration(const FString& Endpoint, FRegistration& OutRegistration) const
{
	if (const FRegistration* Registration = Registrations.Find(Endpoint))
	{
		OutRegistration = *Registration;
		return true;
	}

	return false;
}

FAutomationResult FCommandDispatcher::Dispatch(const FString& Endpoint, FAutomationContext& Context) const
{
	const FRegistration* Registration = Registrations.Find(Endpoint);
	if (!Registration || !Registration->Factory)
	{
		return FAutomationResult::Error(TEXT("command_not_registered"), FString::Printf(TEXT("No command registered for endpoint '%s'"), *Endpoint), 404);
	}

	TUniquePtr<FAutomationCommand> Command = Registration->Factory();
	if (!Command.IsValid())
	{
		return FAutomationResult::Error(TEXT("command_not_available"), FString::Printf(TEXT("Command factory returned no command for '%s'"), *Endpoint), 500);
	}

	return Dispatch(*Command, Context);
}

FAutomationResult FCommandDispatcher::Dispatch(FAutomationCommand& Command, FAutomationContext& Context) const
{
	return Command.Execute(Context);
}

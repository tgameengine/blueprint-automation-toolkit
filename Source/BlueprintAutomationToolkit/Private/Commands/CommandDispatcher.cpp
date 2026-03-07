#include "Commands/CommandDispatcher.h"

void FCommandDispatcher::Register(const FString& Endpoint, FCommandFactory Factory)
{
	if (!Endpoint.IsEmpty() && Factory)
	{
		Factories.Add(Endpoint, MoveTemp(Factory));
	}
}

bool FCommandDispatcher::HasCommand(const FString& Endpoint) const
{
	return Factories.Contains(Endpoint);
}

FAutomationResult FCommandDispatcher::Dispatch(const FString& Endpoint, FAutomationContext& Context) const
{
	const FCommandFactory* Factory = Factories.Find(Endpoint);
	if (!Factory || !(*Factory))
	{
		return FAutomationResult::Error(TEXT("command_not_registered"), FString::Printf(TEXT("No command registered for endpoint '%s'"), *Endpoint), 404);
	}

	TUniquePtr<FAutomationCommand> Command = (*Factory)();
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

#include "Commands/CommandDispatcher.h"

bool FCommandDispatcher::Register(FBATAutomationCommandRegistration Registration, bool bBuiltIn, FString* OutError)
{
	if (OutError)
	{
		OutError->Reset();
	}

	Registration.Endpoint.TrimStartAndEndInline();
	if (Registration.Endpoint.IsEmpty())
	{
		if (OutError)
		{
			*OutError = TEXT("Endpoint is required.");
		}
		return false;
	}

	if (!Registration.Factory)
	{
		if (OutError)
		{
			*OutError = TEXT("Command factory is required.");
		}
		return false;
	}

	if (!Registration.Endpoint.StartsWith(TEXT("/"), ESearchCase::CaseSensitive))
	{
		if (OutError)
		{
			*OutError = TEXT("Endpoint must start with '/'.");
		}
		return false;
	}

	if (const FRegistration* Existing = Registrations.Find(Registration.Endpoint))
	{
		if (Existing->bBuiltIn)
		{
			if (OutError)
			{
				*OutError = FString::Printf(TEXT("Endpoint '%s' is reserved by the built-in automation surface."), *Registration.Endpoint);
			}
			return false;
		}

		if (!Registration.bAllowReplace)
		{
			if (OutError)
			{
				*OutError = FString::Printf(TEXT("Endpoint '%s' is already registered."), *Registration.Endpoint);
			}
			return false;
		}
	}

	FRegistration Stored;
	Stored.Factory = MoveTemp(Registration.Factory);
	Stored.PermissionTier = Registration.PermissionTier;
	Stored.RequiredPermissions = Registration.RequiredPermissions;
	Stored.bBindRoute = Registration.bBindRoute;
	Stored.bBlockDuringPie = Registration.bBlockDuringPie;
	Stored.bBuiltIn = bBuiltIn;
	Registrations.Add(Registration.Endpoint, MoveTemp(Stored));
	return true;
}

bool FCommandDispatcher::Unregister(const FString& Endpoint, bool bAllowBuiltIn, FString* OutError)
{
	if (OutError)
	{
		OutError->Reset();
	}

	const FRegistration* Existing = Registrations.Find(Endpoint);
	if (!Existing)
	{
		if (OutError)
		{
			*OutError = FString::Printf(TEXT("Endpoint '%s' is not registered."), *Endpoint);
		}
		return false;
	}

	if (Existing->bBuiltIn && !bAllowBuiltIn)
	{
		if (OutError)
		{
			*OutError = FString::Printf(TEXT("Endpoint '%s' is built in and cannot be unregistered."), *Endpoint);
		}
		return false;
	}

	Registrations.Remove(Endpoint);
	return true;
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

void FCommandDispatcher::GetRegisteredCommands(TArray<FBATAutomationCommandInfo>& OutCommands) const
{
	OutCommands.Reset();
	OutCommands.Reserve(Registrations.Num());

	for (const TPair<FString, FRegistration>& Pair : Registrations)
	{
		FBATAutomationCommandInfo Info;
		Info.Endpoint = Pair.Key;
		Info.PermissionTier = Pair.Value.PermissionTier;
		Info.RequiredPermissions = Pair.Value.RequiredPermissions;
		Info.bBindRoute = Pair.Value.bBindRoute;
		Info.bBlockDuringPie = Pair.Value.bBlockDuringPie;
		Info.bBuiltIn = Pair.Value.bBuiltIn;
		OutCommands.Add(MoveTemp(Info));
	}

	OutCommands.Sort([](const FBATAutomationCommandInfo& Left, const FBATAutomationCommandInfo& Right)
	{
		return Left.Endpoint < Right.Endpoint;
	});
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

#pragma once

#include "Commands/AutomationCommand.h"

class FPIEControlService
{
public:
	using FSubmitJobFn = TFunction<FString(const FString& Kind, const TSharedRef<class FJsonObject>& Payload)>;

	FPIEControlService(FString InRequestId, FSubmitJobFn InSubmitJobFn);

	FAutomationResult QueueStart() const;
	FAutomationResult QueueStop() const;

private:
	FAutomationResult QueueJob(const FString& Kind) const;

	FString RequestId;
	FSubmitJobFn SubmitJobFn;
};

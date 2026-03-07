#include "Services/PIEControlService.h"

#include "Dom/JsonObject.h"

FPIEControlService::FPIEControlService(FString InRequestId, FSubmitJobFn InSubmitJobFn)
	: RequestId(MoveTemp(InRequestId))
	, SubmitJobFn(MoveTemp(InSubmitJobFn))
{
}

FAutomationResult FPIEControlService::QueueStart() const
{
	return QueueJob(TEXT("pie.start"));
}

FAutomationResult FPIEControlService::QueueStop() const
{
	return QueueJob(TEXT("pie.stop"));
}

FAutomationResult FPIEControlService::QueueJob(const FString& Kind) const
{
	if (!SubmitJobFn)
	{
		return FAutomationResult::Error(TEXT("service_unavailable"), TEXT("PIE control service is not configured"), 500);
	}

	TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	const FString JobId = SubmitJobFn(Kind, Payload);

	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("jobId"), JobId);
	Obj->SetStringField(TEXT("requestId"), RequestId);
	return FAutomationResult::Ok(MakeShared<FJsonValueObject>(Obj), 202);
}

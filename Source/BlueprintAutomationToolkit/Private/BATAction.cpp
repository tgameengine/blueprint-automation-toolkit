#include "BATAction.h"

void UBATAction::Execute_Implementation(const FString& ArgsJson, FString& OutResultJson, bool& bOutOk, FString& OutError) const
{
	bOutOk = false;
	OutResultJson = TEXT("{}");
	OutError = TEXT("Action asset does not implement Execute");
}

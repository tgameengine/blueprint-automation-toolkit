#pragma once

#include "CoreMinimal.h"
#include "Templates/SharedPointer.h"

struct FHttpServerRequest;
class FJsonObject;

namespace BAT::Transport
{
	TSharedPtr<FJsonObject> BuildObjectQueryBody(const FHttpServerRequest& Request);
	TSharedPtr<FJsonObject> BuildBlueprintGraphReadQueryBody(const FHttpServerRequest& Request);
	bool TryParseJsonObjectBody(const FHttpServerRequest& Request, TSharedPtr<FJsonObject>& OutBody);
}
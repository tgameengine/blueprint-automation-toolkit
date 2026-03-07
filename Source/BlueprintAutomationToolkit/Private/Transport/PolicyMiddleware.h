#pragma once

#include "CoreMinimal.h"
#include "HttpResultCallback.h"

class FBlueprintAutomationToolkitModule;
struct FHttpServerRequest;

namespace BAT::Transport
{
	bool ValidateAndHandleRequest(FBlueprintAutomationToolkitModule& Module, const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete, const TCHAR* Endpoint);
}
#include "BlueprintAutomationToolkitModule.h"

#include "CoreGlobals.h"
#include "HttpServerModule.h"
#include "IHttpRouter.h"

bool FBlueprintAutomationToolkitModule::StartServer(bool bInteractiveStart)
{
	if (IsRunningCommandlet())
	{
		UE_LOG(LogBlueprintAutomationToolkit, Log, TEXT("Server start skipped while running commandlet."));
		return false;
	}

	if (bServerRunning)
	{
		return true;
	}

	if (bInteractiveStart && !EnsureServerPermissionGranted())
	{
		UE_LOG(LogBlueprintAutomationToolkit, Warning, TEXT("Server start cancelled by user."));
		return false;
	}

	Router = FHttpServerModule::Get().GetHttpRouter((uint32)Port, true);
	if (!Router.IsValid())
	{
		UE_LOG(LogBlueprintAutomationToolkit, Error, TEXT("Failed to acquire HTTP router on port %d."), Port);
		return false;
	}

	BindRoutes();
	FHttpServerModule::Get().StartAllListeners();
	bServerRunning = true;

	if (!bWarnedPotentialLanReachability)
	{
		bWarnedPotentialLanReachability = true;
		UE_LOG(LogBlueprintAutomationToolkit, Warning, TEXT("Server may be reachable on LAN, but non-local requests are rejected."));
	}

	UE_LOG(LogBlueprintAutomationToolkit, Log, TEXT("Server started (port=%d, exec=%s, safe_mode=%s, python=%s, token=%s)."), Port, bEnableExecRoute ? TEXT("on") : TEXT("off"), bSafeModeEnabled ? TEXT("on") : TEXT("off"), bAllowPythonExec ? TEXT("on") : TEXT("off"), bAuthTokenFromEnv ? TEXT("from_env") : (RuntimeAuthToken.IsEmpty() ? TEXT("unset") : TEXT("set")));
	return true;
}

void FBlueprintAutomationToolkitModule::StopServer()
{
	if (!bServerRunning)
	{
		return;
	}

	UnbindRoutes();
	Router.Reset();
	bServerRunning = false;
	UE_LOG(LogBlueprintAutomationToolkit, Log, TEXT("Server stopped."));
}

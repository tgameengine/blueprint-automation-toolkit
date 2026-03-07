#pragma once

#include "Modules/ModuleInterface.h"
#include "HttpResultCallback.h"
#include "HttpRouteHandle.h"
#include "HttpServerResponse.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Templates/SharedPointer.h"
#include "Containers/Ticker.h"
#include "HAL/CriticalSection.h"
#include "Math/Vector2D.h"
#include "Misc/DateTime.h"
#include "Templates/Function.h"
#include "Templates/Atomic.h"
#include "Templates/UniquePtr.h"
#include "Templates/UnrealTemplate.h"
#include "Misc/Optional.h"
#include "Async/Future.h"
#include "Containers/Set.h"

class UWorld;
class AActor;
class FJsonObject;
struct FAutomationResult;

class IHttpRouter;
class SDockTab;
class FCommandDispatcher;
class FTokenAuthMiddleware;

DECLARE_LOG_CATEGORY_EXTERN(LogBlueprintAutomationToolkit, Log, All);

class FBlueprintAutomationToolkitModule final : public IModuleInterface
{
public:
	~FBlueprintAutomationToolkitModule();

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	const TSet<FName>& GetAllowedUObjectFunctions() const { return AllowedUObjectFunctions; }
	const TSet<FName>& GetAllowedUObjectProperties() const { return AllowedUObjectProperties; }
	const TSet<FString>& GetAllowedReflectionClasses() const { return AllowedReflectionClasses; }
	const TSet<FString>& GetDeniedReflectionClasses() const { return DeniedReflectionClasses; }
	const TSet<FName>& GetAllowedReflectionFunctions() const { return AllowedReflectionFunctions; }
	const TSet<FName>& GetDeniedReflectionFunctions() const { return DeniedReflectionFunctions; }
	const TSet<FName>& GetAllowedReflectionProperties() const { return AllowedReflectionProperties; }
	const TSet<FName>& GetDeniedReflectionProperties() const { return DeniedReflectionProperties; }
	bool IsSafeModeEnabled() const { return bSafeModeEnabled; }
	bool IsExecRouteEnabled() const { return bEnableExecRoute; }
	bool IsPythonExecAllowed() const { return bAllowPythonExec; }
	bool IsAuthTokenRequired() const { return bRequireAuthToken; }
	bool DispatchAutomationCommandRoute(const FString& Endpoint, const struct FHttpServerRequest& Request, const FHttpResultCallback& OnComplete, const TSharedPtr<class FJsonObject>& BodyObj, bool bReturnRawObject = false);
	class UWorld* GetEditorWorld() const;
	class UWorld* GetPIEWorld(int32 PieIndex = 0) const;
	class UWorld* ResolveWorld(const FString& Mode, int32 PieIndex, bool& bOutIsPie, int32& OutResolvedPieIndex, FString& OutError) const;
	bool IsCommandAllowedBySandbox(const FString& Command, FString& OutReason) const;
	bool TryExecBatCommandDirect(class UWorld* World, const FString& FullCommand, class FStringOutputDevice& Out, bool& bOutOk);

#if WITH_DEV_AUTOMATION_TESTS
	enum class EAutomationTestPermission : uint32
	{
		None = 0,
		Editor = 1u << 0,
		Blueprint = 1u << 1,
		Pie = 1u << 2,
		Exec = 1u << 3,
		Python = 1u << 4,
		Filesystem = 1u << 5,
	};

	enum class EAutomationTestJobState : uint8
	{
		Queued,
		Running,
		Succeeded,
		Failed,
		Canceled,
	};

	struct FAutomationTestJobSnapshot
	{
		EAutomationTestJobState State = EAutomationTestJobState::Queued;
		bool bCancelRequested = false;
		TArray<FString> Logs;
	};

	uint32 Test_GetRouteRequiredPermissions(const FString& Endpoint) const;
	uint32 Test_GetRequestRequiredPermissions(const FString& Endpoint, const TSharedPtr<FJsonObject>& BodyObj) const;
	bool Test_IsEditorAssetMutationBlockedDuringPie(const FString& Endpoint) const;
	bool Test_BuildAuthFailureResponse(const FString& Code, FString& OutJson, FString& OutWwwAuthenticate) const;
	void Test_AddOrUpdateJob(const FString& JobId, const FString& RequestId, const FString& Kind, EAutomationTestJobState State, bool bCancelRequested, const TSharedPtr<FJsonObject>& Payload);
	bool Test_GetJobSnapshot(const FString& JobId, FAutomationTestJobSnapshot& OutSnapshot) const;
	void Test_CompleteJobSuccess(const FString& JobId, const TSharedPtr<FJsonObject>& Result);
	void Test_CompleteJobFailure(const FString& JobId, const FString& Code, const FString& Message, const TSharedPtr<FJsonObject>& Details = nullptr);
	void Test_ExecuteJob(const FString& JobId);
	void Test_SetReflectionSafeMode(bool bEnabled);
	void Test_SetReflectionClassAllowList(const TArray<FString>& Values);
	void Test_SetReflectionClassDenyList(const TArray<FString>& Values);
	void Test_SetReflectionFunctionAllowList(const TArray<FString>& Values);
	void Test_SetReflectionFunctionDenyList(const TArray<FString>& Values);
	void Test_SetReflectionPropertyAllowList(const TArray<FString>& Values);
	void Test_SetReflectionPropertyDenyList(const TArray<FString>& Values);
#endif

private:
	friend class FTokenAuthMiddleware;

	void BindRoutes();
	void BindEditorRoutes();
	void BindExecRoute();
	void BindPieControlRoutes();
	void BindPlayerWanderRoute();
	void BindPlayerTeleportRoute();
	void BindActorShootRoute();
	void BindActorInfoRoutes();
	void BindBlueprintRoutes();
	void BindUObjectRoutes();
	void BindReflectionRoutes();
	void BindAssetRoutes();
	void BindActionsRoutes();
	void BindBlueprintAssetsRoutes();
	void BindBlueprintGraphRoutes();
	void BindBlueprintComponentsRoutes();
	void BindBlueprintCompileRoutes();
	void BindBlueprintAssetsRoutesInternal();
	void BindBlueprintGraphRoutesInternal();
	void RegisterAutomationCommands();
	void UnbindRoutes();
	bool EnsureServerPermissionGranted();
	void ApplyDefaultSandboxPolicy();
	bool StartServer(bool bInteractiveStart);
	void StopServer();
	bool RotateAuthToken(bool bRestartIfRunning);
	void PersistSettings(bool bPersistAuthToken) const;
	FString GenerateStrongAuthToken() const;
	TSharedRef<SDockTab> SpawnControlPanelTab(const class FSpawnTabArgs& Args);
	void RegisterControlPanelTab();
	void UnregisterControlPanelTab();
	bool ConfirmUnsafeOption(const FText& Message, const TCHAR* ConfigKey, bool& bConfirmationState);

	bool IsRequestAllowed(const struct FHttpServerRequest& Request, FString* OutDenyReason = nullptr, FString* OutClientKey = nullptr) const;
	bool ValidateAndHandleRequest(const struct FHttpServerRequest& Request, const FHttpResultCallback& OnComplete, const TCHAR* Endpoint);
	FAutomationResult ExecuteAutomationCommand(const FString& Endpoint, const FString& RequestId, const TSharedPtr<FJsonObject>& BodyObj, bool bReturnRawObject = false) const;
	void RunOnGameThread(TFunction<void()> Fn) const;
	bool RunOnGameThreadWait(TFunction<void()> Fn, double TimeoutSeconds = 10.0) const;

	template <typename T>
	TOptional<T> RunOnGameThreadWait(TFunction<T()> Fn, double TimeoutSeconds = 10.0) const
	{
		if (IsInGameThread())
		{
			return TOptional<T>(Fn());
		}

		TPromise<T> Promise;
		TFuture<T> Future = Promise.GetFuture();
		RunOnGameThread([LocalFn = MoveTemp(Fn), LocalPromise = MoveTemp(Promise)]() mutable
		{
			LocalPromise.SetValue(LocalFn());
		});

		const bool bReady = Future.WaitFor(FTimespan::FromSeconds(TimeoutSeconds));
		if (!bReady)
		{
			return TOptional<T>();
		}

		return TOptional<T>(Future.Get());
	}

	enum class EBATPermission : uint32
	{
		None = 0,
		Editor = 1u << 0,
		Blueprint = 1u << 1,
		Pie = 1u << 2,
		Exec = 1u << 3,
		Python = 1u << 4,
		Filesystem = 1u << 5,
	};

	struct FRequestContext
	{
		FString RequestId;
		FString Endpoint;
		FString Subject;
		FString IdempotencyKey;
		FDateTime StartedUtc;
		int32 StatusCode = 200;
		bool bFromIdempotencyCache = false;
	};

	enum class EJobState : uint8
	{
		Queued,
		Running,
		Succeeded,
		Failed,
		Canceled,
	};

	struct FJobRecord
	{
		FString JobId;
		FString Kind;
		FString RequestId;
		EJobState State = EJobState::Queued;
		double Progress = 0.0;
		TSharedPtr<FJsonObject> Result;
		TSharedPtr<FJsonObject> Error;
		TArray<FString> Logs;
		bool bCancelRequested = false;
		FDateTime CreatedUtc;
		FDateTime UpdatedUtc;
	};

	struct FStructuredLogEntry
	{
		FDateTime TimestampUtc;
		FString RequestId;
		FString Route;
		FString Subject;
		double DurationMs = 0.0;
		int32 Status = 0;
		FString ErrorCode;
	};

	struct FTokenRecord
	{
		FString Name;
		FString Token;
		uint32 PermissionsMask = 0;
		bool bHasExpiry = false;
		FDateTime ExpiresUtc;
		FString Secret;
	};

	uint32 GetRequestRequiredPermissions(const FString& Endpoint, const TSharedPtr<FJsonObject>& BodyObj) const;
	uint32 GetRouteRequiredPermissions(const FString& Endpoint) const;
	bool IsEditorAssetMutationBlockedDuringPie(const FString& Endpoint) const;
	bool IsPieSessionRunning() const;
	uint32 GetEffectiveGlobalPermissionsMask() const;
	bool IsPermissionAllowed(uint32 RequiredMask, const FString& SubjectToken, FString& OutReason) const;
	bool TryResolveToken(const FString& RawToken, FTokenRecord& OutToken) const;
	bool ValidateRequestSchema(const FString& Endpoint, const struct FHttpServerRequest& Request, const TSharedPtr<FJsonObject>& BodyObj, FString& OutError) const;
	FString ResolveOrCreateRequestId(const struct FHttpServerRequest& Request) const;
	TUniquePtr<struct FHttpServerResponse> MakeAuthFailureResponse(const FString& RequestId, const FString& Code) const;
	void FillAuthFailure(const FString& Code, FString& OutMessage, TSharedPtr<FJsonObject>& OutDetails, FString& OutWwwAuthenticate) const;
	FString ReadHeaderValueCaseInsensitive(const TMap<FString, TArray<FString>>& Headers, const TCHAR* Name) const;
	TUniquePtr<struct FHttpServerResponse> MakeErrorResponse(int32 HttpCode, const FString& RequestId, const FString& Code, const FString& Message, const TSharedPtr<FJsonObject>& Details = nullptr) const;
	TUniquePtr<struct FHttpServerResponse> MakeErrorResponse(enum EHttpServerResponseCodes HttpCode, const FString& RequestId, const FString& Code, const FString& Message, const TSharedPtr<FJsonObject>& Details = nullptr) const;
	TUniquePtr<struct FHttpServerResponse> MakeErrorResponse(int32 HttpCode, const FString& Code, const FString& Message, const TSharedPtr<FJsonObject>& Details = nullptr) const;
	TUniquePtr<struct FHttpServerResponse> MakeErrorResponse(enum EHttpServerResponseCodes HttpCode, const FString& Code, const FString& Message, const TSharedPtr<FJsonObject>& Details = nullptr) const;
	TUniquePtr<struct FHttpServerResponse> MakeCanonicalSuccessResponse(int32 HttpCode, const FString& RequestId, const TSharedPtr<FJsonObject>& Data, const TArray<TSharedPtr<class FJsonValue>>& Warnings = {}) const;
	TUniquePtr<struct FHttpServerResponse> MakeCanonicalErrorResponse(int32 HttpCode, const FString& RequestId, const FString& Code, const FString& Message, const TSharedPtr<FJsonObject>& Details = nullptr, const TArray<TSharedPtr<class FJsonValue>>& Warnings = {}, const FString& SuggestedAction = FString(), const TOptional<bool>& RetryableOverride = TOptional<bool>()) const;
	TUniquePtr<struct FHttpServerResponse> MakeCanonicalResponseFromAutomationResult(const FAutomationResult& Result, const FString& RequestId) const;
	TSharedPtr<FJsonObject> NormalizeCanonicalObjectRequest(const TSharedPtr<FJsonObject>& BodyObj) const;
	TSharedPtr<FJsonObject> BuildCapabilitiesSummary() const;
	TSharedPtr<FJsonObject> BuildEngineDiscoverPayload() const;
	void AppendStructuredLog(const FStructuredLogEntry& Entry);
	void FinalizeRequestLog(const FRequestContext& Context, int32 StatusCode, const FString& ErrorCode);

	FString SubmitJob(const FString& Kind, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload);
	bool TryGetJob(const FString& JobId, FJobRecord& OutJob) const;
	bool CancelJob(const FString& JobId);
	void UpdateJobState(const FString& JobId, EJobState NewState, double Progress = -1.0);
	void AppendJobLog(const FString& JobId, const FString& Line);
	void CompleteJobSuccess(const FString& JobId, const TSharedPtr<FJsonObject>& Result);
	void CompleteJobFailure(const FString& JobId, const FString& Code, const FString& Message, const TSharedPtr<FJsonObject>& Details = nullptr);
	void ExecuteJob(const FString& JobId);
	TSharedPtr<FJsonObject> ExecuteJobByKind(const FString& JobId, const FString& Kind, const TSharedPtr<FJsonObject>& Payload, FString& OutCode, FString& OutMessage);
	TArray<FStructuredLogEntry> GetRecentLogs(int32 MaxCount) const;

	AActor* ResolveActor(UWorld* World, const FString& NameOrLabelOrPath, const FString& Tag = TEXT("")) const;

	void RecordRequestStat(const FString& Endpoint, bool bAllowed, const FString& Reason);
	bool ConsumeRateLimitToken(const FString& ClientKey);
	FString BuildRequestStatsText() const;
	FString GetTokenStatusText() const;
	void NotifySettingChanged();
	bool ExecuteEditorLayoutApply(const TArray<TSharedPtr<class FJsonValue>>& Actors, bool bApply, TSharedRef<class FJsonObject>& OutResult, TArray<TSharedPtr<class FJsonValue>>& OutErrors) const;
	bool ExecuteBlueprintPatch(const TSharedPtr<class FJsonObject>& BodyObj, bool bApply, int32& InOutTotalInstances, TSharedRef<class FJsonObject>& OutResult, TArray<TSharedPtr<class FJsonValue>>& OutErrors) const;
	void RecordPlanExecution(int32 OpCount, bool bSuccess);

	struct FRateLimitState
	{
		double Tokens = 0.0;
		double LastUpdatedSeconds = 0.0;
	};

	struct FRequestStatEntry
	{
		FString Endpoint;
		FString Timestamp;
		bool bAllowed = false;
		FString Reason;
	};

	struct FPlanExecutionEntry
	{
		FDateTime Timestamp;
		int32 OpCount = 0;
		bool bSuccess = false;
	};

	int32 Port = 9876;
	FString AuthToken;
	FString RuntimeAuthToken;
	bool bAuthTokenFromEnv = false;
	bool bProjectConfigTokenAvailable = false;
	bool bSaveTokenInProjectSettings = true;
	bool bServerEnabled = false;
	bool bPermissionPromptAnswered = false;
	bool bRequireAuthToken = true;
	bool bAllowPythonExec = false;
	bool bEnableExecRoute = false;
	bool bSafeModeEnabled = true;
	bool bAllowFilesystemInSafeMode = true;
	bool bWarnedPotentialLanReachability = false;
	bool bUnsafeModeConfirmationAccepted = false;
	bool bExecRouteConfirmationAccepted = false;
	bool bPythonConfirmationAccepted = false;
	int32 MaxRequestBodyBytes = 65536;
	int32 RateLimitPerSecond = 10;
	int32 RateLimitBurst = 20;
	int32 MaxOpsPerPlan = 64;
	int32 MaxActorsPerLayout = 200;
	int32 MaxInstancesPerOp = 2000;
	int32 MaxTotalInstancesPerPlan = 5000;
	TArray<FString> CommandSandboxAllowPrefixes;
	TArray<FString> CommandSandboxBlockSubstrings;
	TArray<FRequestStatEntry> RequestStats;
	TArray<FPlanExecutionEntry> PlanExecutionLog;
	TMap<FString, FRateLimitState> RateLimitStates;
	mutable FCriticalSection SecurityStateMutex;
	FCommandDispatcher* CommandDispatcher = nullptr;
	FTokenAuthMiddleware* TokenAuthMiddleware = nullptr;

	TSharedPtr<IHttpRouter> Router;
	bool bServerRunning = false;
	FHttpRouteHandle HealthRoute;
	FHttpRouteHandle LegacyHealthRoute;
	FHttpRouteHandle EditorMapRoute;
	FHttpRouteHandle EditorQuitRoute;
	FHttpRouteHandle EditorLayoutApplyRoute;
	FHttpRouteHandle CapabilitiesRoute;
	FHttpRouteHandle EngineDiscoverRoute;
	FHttpRouteHandle PlanValidateRoute;
	FHttpRouteHandle PlanApplyRoute;
	FHttpRouteHandle OpenApiRoute;
	FHttpRouteHandle JobsSubmitRoute;
	FHttpRouteHandle JobGetRoute;
	FHttpRouteHandle JobCancelRoute;
	FHttpRouteHandle LogsTailRoute;
	FHttpRouteHandle ExecRoute;
	FHttpRouteHandle ExecAliasRoute;
	FHttpRouteHandle PieStartRoute;
	FHttpRouteHandle PieStopRoute;
	FHttpRouteHandle LegacyPieStartRoute;
	FHttpRouteHandle LegacyPieStopRoute;
	FHttpRouteHandle PlayerWanderRoute;
	FHttpRouteHandle PlayerTeleportToActorRoute;
	FHttpRouteHandle ActorShootRoute;
	FHttpRouteHandle ActorIntrospectRoute;
	FHttpRouteHandle ActorPropertiesRoute;
	FHttpRouteHandle BlueprintCreateRoute;
	FHttpRouteHandle BlueprintApplyRoute;
	FHttpRouteHandle BlueprintSetDefaultsRoute;
	FHttpRouteHandle BlueprintComponentsAddRoute;
	FHttpRouteHandle BlueprintComponentsSetRoute;
	FHttpRouteHandle BlueprintComponentsInstancesAddRoute;
	FHttpRouteHandle BlueprintComponentsRemoveRoute;
	FHttpRouteHandle BlueprintComponentsReplaceRoute;
	FHttpRouteHandle BlueprintCompileRoute;
	FHttpRouteHandle BlueprintSaveRoute;
	FHttpRouteHandle BlueprintCompileSaveRoute;
	FHttpRouteHandle BlueprintSchemaRoute;
	FHttpRouteHandle BlueprintGraphsRoute;
	FHttpRouteHandle BlueprintGraphNodesRoute;
	FHttpRouteHandle BlueprintGraphLinksRoute;
	FHttpRouteHandle BlueprintGraphApplyRoute;
	FHttpRouteHandle BlueprintNodeAddCallFunctionRoute;
	FHttpRouteHandle BlueprintNodeAddCustomEventRoute;
	FHttpRouteHandle BlueprintNodeAddBranchRoute;
	FHttpRouteHandle BlueprintPinConnectRoute;
	FHttpRouteHandle BlueprintPinSetDefaultRoute;
	FHttpRouteHandle BlueprintNodeDeleteRoute;
	FHttpRouteHandle BlueprintNodeDescribeRoute;
	FHttpRouteHandle UObjectGetRoute;
	FHttpRouteHandle UObjectSetRoute;
	FHttpRouteHandle UObjectCallRoute;
	FHttpRouteHandle ObjectResolveRoute;
	FHttpRouteHandle ObjectDescribeRoute;
	FHttpRouteHandle ObjectGetRoute;
	FHttpRouteHandle ObjectSetPropertyRoute;
	FHttpRouteHandle ObjectCallFunctionRoute;
	FHttpRouteHandle ObjectListPropertiesRoute;
	FHttpRouteHandle ObjectListFunctionsRoute;
	FHttpRouteHandle ActorSpawnRoute;
	FHttpRouteHandle ActorFindRoute;
	FHttpRouteHandle AssetDuplicateRoute;
	FHttpRouteHandle AssetSaveRoute;
	FHttpRouteHandle AssetDeleteRoute;
	FHttpRouteHandle AssetCreateRoute;
	FHttpRouteHandle PcgSpawnSpheresRoute;
	FHttpRouteHandle ActionsListRoute;
	FHttpRouteHandle ActionsRunRoute;

	struct FWanderState
	{
		FTSTicker::FDelegateHandle Handle;
		float RemainingSeconds = 0.0f;
		float Strength = 1.0f;
		float NextChangeSeconds = 0.0f;
		FVector2D Dir = FVector2D(1.0f, 0.0f);
		float YawRate = 0.0f;
	};
	TUniquePtr<FWanderState> Wander;

	bool bPermissionEditor = true;
	bool bPermissionBlueprint = false;
	bool bPermissionPie = false;
	bool bPermissionExec = false;
	bool bPermissionPython = false;
	bool bPermissionFilesystem = false;
	bool bEnableHmacAuth = false;
	int32 MaxClockSkewSeconds = 120;
	int32 LogRingSize = 500;
	TArray<FStructuredLogEntry> StructuredLogs;
	TMap<FString, FJobRecord> Jobs;
	TMap<FString, TSharedPtr<FJsonObject>> IdempotencyCache;
	TMap<FString, FString> IdempotencyToJob;
	TArray<FTokenRecord> ScopedTokens;
	TSet<FName> AllowedUObjectFunctions;
	TSet<FName> AllowedUObjectProperties;
	TSet<FString> AllowedReflectionClasses;
	TSet<FString> DeniedReflectionClasses;
	TSet<FName> AllowedReflectionFunctions;
	TSet<FName> DeniedReflectionFunctions;
	TSet<FName> AllowedReflectionProperties;
	TSet<FName> DeniedReflectionProperties;
	TArray<FString> AllowedActionAssetPrefixes;
	mutable FCriticalSection JobMutex;
	mutable FCriticalSection LogMutex;

};

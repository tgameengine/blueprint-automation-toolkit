#if WITH_DEV_AUTOMATION_TESTS

#include "BlueprintAutomationToolkitModule.h"

#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATOpenApiSpecExistsTest, "BlueprintAutomationToolkit.OpenApi.SpecExists", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATOpenApiHasJobsAndLogsTest, "BlueprintAutomationToolkit.OpenApi.HasJobsAndLogsPaths", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATOpenApiHasBlueprintPlanPathsTest, "BlueprintAutomationToolkit.OpenApi.HasBlueprintPlanPaths", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATOpenApiHasBlueprintSchemaPathTest, "BlueprintAutomationToolkit.OpenApi.HasBlueprintSchemaPath", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATExecPythonRequiresPythonPermissionTest, "BlueprintAutomationToolkit.Security.ExecPythonRequiresPythonPermission", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATPermissionMapCoversPieAliasesTest, "BlueprintAutomationToolkit.Security.PermissionMapCoversPieAliases", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATPermissionMapCoversBlueprintCompileSaveTest, "BlueprintAutomationToolkit.Security.PermissionMapCoversBlueprintCompileSave", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATPieEditBlockRouteClassificationTest, "BlueprintAutomationToolkit.Security.PieEditBlockRouteClassification", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATAuthMissingResponseHasTokenHintTest, "BlueprintAutomationToolkit.Security.AuthMissingResponseHasTokenHint", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATCanceledJobRemainsCanceledTest, "BlueprintAutomationToolkit.Jobs.CanceledJobRemainsCanceled", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBATOpenApiSpecExistsTest::RunTest(const FString& Parameters)
{
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("BlueprintAutomationToolkit"));
	if (!Plugin.IsValid())
	{
		Plugin = IPluginManager::Get().FindPlugin(TEXT("BlueprintAutomationToolkit"));
	}

	if (!TestTrue(TEXT("Plugin must be discoverable"), Plugin.IsValid()))
	{
		return false;
	}

	const FString OpenApiPath = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Docs"), TEXT("openapi.yaml"));
	return TestTrue(TEXT("Docs/openapi.yaml must exist"), FPaths::FileExists(OpenApiPath));
}

bool FBATOpenApiHasJobsAndLogsTest::RunTest(const FString& Parameters)
{
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("BlueprintAutomationToolkit"));
	if (!Plugin.IsValid())
	{
		Plugin = IPluginManager::Get().FindPlugin(TEXT("BlueprintAutomationToolkit"));
	}

	if (!Plugin.IsValid())
	{
		AddError(TEXT("Plugin not found"));
		return false;
	}

	const FString OpenApiPath = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Docs"), TEXT("openapi.yaml"));
	FString Spec;
	if (!FFileHelper::LoadFileToString(Spec, *OpenApiPath))
	{
		AddError(TEXT("Failed to read openapi.yaml"));
		return false;
	}

	TestTrue(TEXT("OpenAPI contains /jobs/submit"), Spec.Contains(TEXT("/jobs/submit:"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("OpenAPI contains /jobs/{jobId}"), Spec.Contains(TEXT("/jobs/{jobId}:"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("OpenAPI contains /logs/tail"), Spec.Contains(TEXT("/logs/tail:"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("OpenAPI contains /openapi"), Spec.Contains(TEXT("/openapi:"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("OpenAPI contains /ai/exec"), Spec.Contains(TEXT("/ai/exec:"), ESearchCase::CaseSensitive));
	return true;
}

bool FBATOpenApiHasBlueprintPlanPathsTest::RunTest(const FString& Parameters)
{
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("BlueprintAutomationToolkit"));
	if (!Plugin.IsValid())
	{
		Plugin = IPluginManager::Get().FindPlugin(TEXT("BlueprintAutomationToolkit"));
	}

	if (!Plugin.IsValid())
	{
		AddError(TEXT("Plugin not found"));
		return false;
	}

	const FString OpenApiPath = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Docs"), TEXT("openapi.yaml"));
	FString Spec;
	if (!FFileHelper::LoadFileToString(Spec, *OpenApiPath))
	{
		AddError(TEXT("Failed to read openapi.yaml"));
		return false;
	}

	TestTrue(TEXT("OpenAPI contains /blueprint/apply"), Spec.Contains(TEXT("/blueprint/apply:"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("OpenAPI contains /blueprint/graph/nodes"), Spec.Contains(TEXT("/blueprint/graph/nodes:"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("OpenAPI contains /object/set-property"), Spec.Contains(TEXT("/object/set-property:"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("OpenAPI contains /object/call-function"), Spec.Contains(TEXT("/object/call-function:"), ESearchCase::CaseSensitive));
	return true;
}

bool FBATOpenApiHasBlueprintSchemaPathTest::RunTest(const FString& Parameters)
{
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("BlueprintAutomationToolkit"));
	if (!Plugin.IsValid())
	{
		Plugin = IPluginManager::Get().FindPlugin(TEXT("BlueprintAutomationToolkit"));
	}

	if (!Plugin.IsValid())
	{
		AddError(TEXT("Plugin not found"));
		return false;
	}

	const FString OpenApiPath = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Docs"), TEXT("openapi.yaml"));
	FString Spec;
	if (!FFileHelper::LoadFileToString(Spec, *OpenApiPath))
	{
		AddError(TEXT("Failed to read openapi.yaml"));
		return false;
	}

	TestTrue(TEXT("OpenAPI contains /blueprint/schema"), Spec.Contains(TEXT("/blueprint/schema:"), ESearchCase::CaseSensitive));
	return true;
}

bool FBATExecPythonRequiresPythonPermissionTest::RunTest(const FString& Parameters)
{
	FBlueprintAutomationToolkitModule Module;
	const uint32 ExecMask = static_cast<uint32>(FBlueprintAutomationToolkitModule::EAutomationTestPermission::Exec);
	const uint32 PythonMask = static_cast<uint32>(FBlueprintAutomationToolkitModule::EAutomationTestPermission::Python);

	TSharedRef<FJsonObject> BodyObj = MakeShared<FJsonObject>();
	BodyObj->SetStringField(TEXT("python"), TEXT("print('hello')"));

	TestEqual(TEXT("/ai/exec without python only requires Exec permission"), Module.Test_GetRequestRequiredPermissions(TEXT("/ai/exec"), nullptr), ExecMask);
	TestEqual(TEXT("/ai/exec with python requires Exec and Python permissions"), Module.Test_GetRequestRequiredPermissions(TEXT("/ai/exec"), BodyObj), ExecMask | PythonMask);
	return true;
}

bool FBATPermissionMapCoversPieAliasesTest::RunTest(const FString& Parameters)
{
	FBlueprintAutomationToolkitModule Module;
	const uint32 PieMask = static_cast<uint32>(FBlueprintAutomationToolkitModule::EAutomationTestPermission::Pie);

	TestEqual(TEXT("/pie/start requires PIE permission"), Module.Test_GetRouteRequiredPermissions(TEXT("/pie/start")), PieMask);
	TestEqual(TEXT("/pie/stop requires PIE permission"), Module.Test_GetRouteRequiredPermissions(TEXT("/pie/stop")), PieMask);
	return true;
}

bool FBATPermissionMapCoversBlueprintCompileSaveTest::RunTest(const FString& Parameters)
{
	FBlueprintAutomationToolkitModule Module;
	const uint32 ExpectedMask =
		static_cast<uint32>(FBlueprintAutomationToolkitModule::EAutomationTestPermission::Blueprint) |
		static_cast<uint32>(FBlueprintAutomationToolkitModule::EAutomationTestPermission::Filesystem);

	TestEqual(TEXT("/blueprint/compile_save requires Blueprint and Filesystem permissions"), Module.Test_GetRouteRequiredPermissions(TEXT("/blueprint/compile_save")), ExpectedMask);
	return true;
}

bool FBATPieEditBlockRouteClassificationTest::RunTest(const FString& Parameters)
{
	FBlueprintAutomationToolkitModule Module;

	TestTrue(TEXT("/asset/duplicate is blocked during PIE"), Module.Test_IsEditorAssetMutationBlockedDuringPie(TEXT("/asset/duplicate")));
	TestTrue(TEXT("/uobject/set is blocked during PIE"), Module.Test_IsEditorAssetMutationBlockedDuringPie(TEXT("/uobject/set")));
	TestTrue(TEXT("/blueprint/set-defaults is blocked during PIE"), Module.Test_IsEditorAssetMutationBlockedDuringPie(TEXT("/blueprint/set-defaults")));
	TestTrue(TEXT("/blueprint/pin/connect is blocked during PIE"), Module.Test_IsEditorAssetMutationBlockedDuringPie(TEXT("/blueprint/pin/connect")));
	TestTrue(TEXT("/blueprint/compile_save is blocked during PIE"), Module.Test_IsEditorAssetMutationBlockedDuringPie(TEXT("/blueprint/compile_save")));

	TestFalse(TEXT("/blueprint/schema remains allowed during PIE"), Module.Test_IsEditorAssetMutationBlockedDuringPie(TEXT("/blueprint/schema")));
	TestFalse(TEXT("/blueprint/graph/nodes remains allowed during PIE"), Module.Test_IsEditorAssetMutationBlockedDuringPie(TEXT("/blueprint/graph/nodes")));
	TestFalse(TEXT("/blueprint/node/describe remains allowed during PIE"), Module.Test_IsEditorAssetMutationBlockedDuringPie(TEXT("/blueprint/node/describe")));
	TestFalse(TEXT("/uobject/get remains allowed during PIE"), Module.Test_IsEditorAssetMutationBlockedDuringPie(TEXT("/uobject/get")));
	TestFalse(TEXT("/pie/start remains allowed during PIE"), Module.Test_IsEditorAssetMutationBlockedDuringPie(TEXT("/pie/start")));
	return true;
}

bool FBATAuthMissingResponseHasTokenHintTest::RunTest(const FString& Parameters)
{
	FBlueprintAutomationToolkitModule Module;
	FString Json;
	FString WwwAuthenticate;
	if (!TestTrue(TEXT("Auth missing response should be buildable"), Module.Test_BuildAuthFailureResponse(TEXT("auth_missing"), Json, WwwAuthenticate)))
	{
		return false;
	}

	TestTrue(TEXT("Auth missing response includes Bearer format hint"), Json.Contains(TEXT("Bearer <token>"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("Auth missing response includes Copy Token guidance"), Json.Contains(TEXT("Copy the token from the Blueprint Automation Toolkit panel"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("Auth missing response avoids settings-file guidance"), !Json.Contains(TEXT("EditorPerProjectUserSettings.ini"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("Auth missing response includes WWW-Authenticate Bearer challenge"), WwwAuthenticate.Contains(TEXT("Bearer realm=\"BlueprintAutomationToolkit\""), ESearchCase::CaseSensitive));
	TestTrue(TEXT("Auth missing response identifies invalid_request"), WwwAuthenticate.Contains(TEXT("error=\"invalid_request\""), ESearchCase::CaseSensitive));
	return true;
}

bool FBATCanceledJobRemainsCanceledTest::RunTest(const FString& Parameters)
{
	FBlueprintAutomationToolkitModule Module;
	const FString JobId = TEXT("job-canceled-test");
	Module.Test_AddOrUpdateJob(
		JobId,
		TEXT("request-canceled-test"),
		TEXT("unknown.kind"),
		FBlueprintAutomationToolkitModule::EAutomationTestJobState::Canceled,
		true,
		MakeShared<FJsonObject>());

	Module.Test_CompleteJobSuccess(JobId, MakeShared<FJsonObject>());
	FBlueprintAutomationToolkitModule::FAutomationTestJobSnapshot Snapshot;
	if (!TestTrue(TEXT("Canceled job remains queryable after CompleteJobSuccess"), Module.Test_GetJobSnapshot(JobId, Snapshot)))
	{
		return false;
	}
	TestEqual(TEXT("Canceled job stays canceled after CompleteJobSuccess"), Snapshot.State, FBlueprintAutomationToolkitModule::EAutomationTestJobState::Canceled);

	Module.Test_CompleteJobFailure(JobId, TEXT("ignored"), TEXT("ignored"));
	if (!TestTrue(TEXT("Canceled job remains queryable after CompleteJobFailure"), Module.Test_GetJobSnapshot(JobId, Snapshot)))
	{
		return false;
	}
	TestEqual(TEXT("Canceled job stays canceled after CompleteJobFailure"), Snapshot.State, FBlueprintAutomationToolkitModule::EAutomationTestJobState::Canceled);

	Module.Test_ExecuteJob(JobId);
	if (!TestTrue(TEXT("Canceled job remains queryable after ExecuteJob"), Module.Test_GetJobSnapshot(JobId, Snapshot)))
	{
		return false;
	}
	TestEqual(TEXT("Canceled job stays canceled after ExecuteJob"), Snapshot.State, FBlueprintAutomationToolkitModule::EAutomationTestJobState::Canceled);
	TestTrue(TEXT("Canceled job logs cancellation"), Snapshot.Logs.Contains(TEXT("job_canceled")));
	return true;
}

#endif

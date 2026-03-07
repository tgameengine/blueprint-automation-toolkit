#if WITH_DEV_AUTOMATION_TESTS

#include "BlueprintAutomationToolkitModule.h"

#include "Commands/AutomationCommand.h"
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Services/Reflection/ReflectionFunctionService.h"
#include "Services/Reflection/ReflectionObjectResolver.h"
#include "Services/Reflection/ReflectionPropertyService.h"
#include "Tests/BATReflectionTestTypes.h"

#include "Engine/EngineTypes.h"
#include "UObject/UObjectGlobals.h"

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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATReflectionResolveObjectByPathTest, "BlueprintAutomationToolkit.Reflection.ResolveObjectByPath", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATReflectionListPropertiesTest, "BlueprintAutomationToolkit.Reflection.ListProperties", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATReflectionSetScalarPropertyTest, "BlueprintAutomationToolkit.Reflection.SetScalarProperty", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATReflectionSetStructPropertyTest, "BlueprintAutomationToolkit.Reflection.SetStructProperty", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATReflectionListFunctionsTest, "BlueprintAutomationToolkit.Reflection.ListFunctions", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATReflectionCallSafeFunctionTest, "BlueprintAutomationToolkit.Reflection.CallSafeFunction", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATReflectionSafeModeBlockingTest, "BlueprintAutomationToolkit.Reflection.SafeModeBlocking", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATReflectionInvalidTargetsTest, "BlueprintAutomationToolkit.Reflection.InvalidTargets", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	static UBATReflectionTestObject* CreateReflectionTestObject()
	{
		UBATReflectionTestObject* Object = NewObject<UBATReflectionTestObject>(GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UBATReflectionTestObject::StaticClass(), TEXT("BATReflectionTestObject")), RF_Transient);
		Object->AddToRoot();
		return Object;
	}

	static void DestroyReflectionTestObject(UBATReflectionTestObject* Object)
	{
		if (Object)
		{
			Object->RemoveFromRoot();
		}
	}

	static TSharedRef<FJsonObject> MakeObjectRequest(const UObject* Object)
	{
		TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetStringField(TEXT("objectPath"), Object ? Object->GetPathName() : TEXT(""));
		return Body;
	}

	static TSharedPtr<FJsonObject> GetStructuredRoot(const FAutomationResult& Result)
	{
		return (Result.Data.IsValid() && Result.Data->Type == EJson::Object) ? Result.Data->AsObject() : nullptr;
	}

	static TSharedPtr<FJsonObject> GetStructuredData(const FAutomationResult& Result)
	{
		const TSharedPtr<FJsonObject> Root = GetStructuredRoot(Result);
		if (!Root.IsValid())
		{
			return nullptr;
		}

		const TSharedPtr<FJsonObject>* Data = nullptr;
		return Root->TryGetObjectField(TEXT("data"), Data) && Data ? *Data : nullptr;
	}
}

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
	TestTrue(TEXT("OpenAPI contains /object/resolve"), Spec.Contains(TEXT("/object/resolve:"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("OpenAPI contains /object/get"), Spec.Contains(TEXT("/object/get:"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("OpenAPI contains /object/list-properties"), Spec.Contains(TEXT("/object/list-properties:"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("OpenAPI contains /object/list-functions"), Spec.Contains(TEXT("/object/list-functions:"), ESearchCase::CaseSensitive));
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

bool FBATReflectionResolveObjectByPathTest::RunTest(const FString& Parameters)
{
	FBlueprintAutomationToolkitModule Module;
	TStrongObjectPtr<UBATReflectionTestObject> Object(CreateReflectionTestObject());
	FReflectionObjectResolver Resolver;
	BAT::Reflection::FResolvedObject ResolvedObject;
	FAutomationResult Failure;
	const bool bResolved = Resolver.Resolve(Module, MakeObjectRequest(Object.Get()), TEXT("resolve-test"), ResolvedObject, Failure);
	DestroyReflectionTestObject(Object.Get());

	if (!TestTrue(TEXT("Reflection resolver resolves object by path"), bResolved))
	{
		return false;
	}

	return TestEqual(TEXT("Resolved path matches object path"), ResolvedObject.ResolvedObjectPath, Object->GetPathName());
}

bool FBATReflectionListPropertiesTest::RunTest(const FString& Parameters)
{
	FBlueprintAutomationToolkitModule Module;
	Module.Test_SetReflectionSafeMode(true);
	Module.Test_SetReflectionClassAllowList({ TEXT("BATReflectionTestObject") });
	TStrongObjectPtr<UBATReflectionTestObject> Object(CreateReflectionTestObject());
	const FReflectionPropertyService Service;
	const FAutomationResult Result = Service.ListProperties(Module, TEXT("list-properties-test"), MakeObjectRequest(Object.Get()));
	DestroyReflectionTestObject(Object.Get());

	if (!TestTrue(TEXT("ListProperties succeeds"), Result.bSuccess))
	{
		return false;
	}

	const TSharedPtr<FJsonObject> Data = GetStructuredData(Result);
	const TArray<TSharedPtr<FJsonValue>>* Properties = nullptr;
	if (!TestTrue(TEXT("ListProperties returns data"), Data.IsValid() && Data->TryGetArrayField(TEXT("properties"), Properties) && Properties))
	{
		return false;
	}

	bool bFoundMaxHealth = false;
	for (const TSharedPtr<FJsonValue>& Entry : *Properties)
	{
		const TSharedPtr<FJsonObject> PropertyObject = Entry.IsValid() ? Entry->AsObject() : nullptr;
		FString Name;
		if (PropertyObject.IsValid() && PropertyObject->TryGetStringField(TEXT("name"), Name) && Name == TEXT("MaxHealth"))
		{
			bFoundMaxHealth = true;
			break;
		}
	}

	return TestTrue(TEXT("ListProperties includes MaxHealth"), bFoundMaxHealth);
}

bool FBATReflectionSetScalarPropertyTest::RunTest(const FString& Parameters)
{
	FBlueprintAutomationToolkitModule Module;
	Module.Test_SetReflectionSafeMode(true);
	Module.Test_SetReflectionClassAllowList({ TEXT("BATReflectionTestObject") });
	TStrongObjectPtr<UBATReflectionTestObject> Object(CreateReflectionTestObject());
	TSharedRef<FJsonObject> Body = MakeObjectRequest(Object.Get());
	Body->SetStringField(TEXT("property"), TEXT("MaxHealth"));
	Body->SetNumberField(TEXT("value"), 250);

	const FReflectionPropertyService Service;
	const FAutomationResult Result = Service.SetProperty(Module, TEXT("set-scalar-test"), Body);
	const bool bValueChanged = Object->MaxHealth == 250;
	DestroyReflectionTestObject(Object.Get());

	return TestTrue(TEXT("SetProperty updates scalar property"), Result.bSuccess && bValueChanged);
}

bool FBATReflectionSetStructPropertyTest::RunTest(const FString& Parameters)
{
	FBlueprintAutomationToolkitModule Module;
	Module.Test_SetReflectionSafeMode(true);
	Module.Test_SetReflectionClassAllowList({ TEXT("BATReflectionTestObject") });
	TStrongObjectPtr<UBATReflectionTestObject> Object(CreateReflectionTestObject());
	TSharedRef<FJsonObject> Body = MakeObjectRequest(Object.Get());
	Body->SetStringField(TEXT("property"), TEXT("SpawnOffset"));
	TSharedRef<FJsonObject> Value = MakeShared<FJsonObject>();
	Value->SetNumberField(TEXT("x"), 100.0);
	Value->SetNumberField(TEXT("y"), 200.0);
	Value->SetNumberField(TEXT("z"), 300.0);
	Body->SetObjectField(TEXT("value"), Value);

	const FReflectionPropertyService Service = FReflectionPropertyService();
	const FAutomationResult Result = Service.SetProperty(Module, TEXT("set-struct-test"), Body);
	const bool bValueChanged = Object->SpawnOffset.Equals(FVector(100.0f, 200.0f, 300.0f));
	DestroyReflectionTestObject(Object.Get());

	return TestTrue(TEXT("SetProperty updates FVector struct property"), Result.bSuccess && bValueChanged);
}

bool FBATReflectionListFunctionsTest::RunTest(const FString& Parameters)
{
	FBlueprintAutomationToolkitModule Module;
	Module.Test_SetReflectionSafeMode(true);
	Module.Test_SetReflectionClassAllowList({ TEXT("BATReflectionTestObject") });
	TStrongObjectPtr<UBATReflectionTestObject> Object(CreateReflectionTestObject());
	const FReflectionFunctionService Service;
	const FAutomationResult Result = Service.ListFunctions(Module, TEXT("list-functions-test"), MakeObjectRequest(Object.Get()));
	DestroyReflectionTestObject(Object.Get());

	if (!TestTrue(TEXT("ListFunctions succeeds"), Result.bSuccess))
	{
		return false;
	}

	const TSharedPtr<FJsonObject> Data = GetStructuredData(Result);
	const TArray<TSharedPtr<FJsonValue>>* Functions = nullptr;
	if (!TestTrue(TEXT("ListFunctions returns function array"), Data.IsValid() && Data->TryGetArrayField(TEXT("functions"), Functions) && Functions))
	{
		return false;
	}

	bool bFoundAddToHealth = false;
	for (const TSharedPtr<FJsonValue>& Entry : *Functions)
	{
		const TSharedPtr<FJsonObject> FunctionObject = Entry.IsValid() ? Entry->AsObject() : nullptr;
		FString Name;
		if (FunctionObject.IsValid() && FunctionObject->TryGetStringField(TEXT("name"), Name) && Name == TEXT("AddToHealth"))
		{
			bFoundAddToHealth = true;
			break;
		}
	}

	return TestTrue(TEXT("ListFunctions includes AddToHealth"), bFoundAddToHealth);
}

bool FBATReflectionCallSafeFunctionTest::RunTest(const FString& Parameters)
{
	FBlueprintAutomationToolkitModule Module;
	Module.Test_SetReflectionSafeMode(true);
	Module.Test_SetReflectionClassAllowList({ TEXT("BATReflectionTestObject") });
	TStrongObjectPtr<UBATReflectionTestObject> Object(CreateReflectionTestObject());
	TSharedRef<FJsonObject> Body = MakeObjectRequest(Object.Get());
	Body->SetStringField(TEXT("function"), TEXT("AddToHealth"));
	TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
	Arguments->SetNumberField(TEXT("Delta"), 25);
	Body->SetObjectField(TEXT("arguments"), Arguments);

	const FReflectionFunctionService Service;
	const FAutomationResult Result = Service.CallFunction(Module, TEXT("call-function-test"), Body);
	DestroyReflectionTestObject(Object.Get());

	if (!TestTrue(TEXT("CallFunction succeeds"), Result.bSuccess))
	{
		return false;
	}

	const TSharedPtr<FJsonObject> Data = GetStructuredData(Result);
	const TSharedPtr<FJsonValue>* ReturnValue = Data.IsValid() ? Data->Values.Find(TEXT("returnValue")) : nullptr;
	return TestTrue(TEXT("CallFunction returns updated health"), ReturnValue && (*ReturnValue)->Type == EJson::Number && static_cast<int32>((*ReturnValue)->AsNumber()) == 125 && Object->MaxHealth == 125);
}

bool FBATReflectionSafeModeBlockingTest::RunTest(const FString& Parameters)
{
	FBlueprintAutomationToolkitModule Module;
	Module.Test_SetReflectionSafeMode(true);
	Module.Test_SetReflectionClassAllowList({ TEXT("Actor") });
	TStrongObjectPtr<UBATReflectionTestObject> Object(CreateReflectionTestObject());
	TSharedRef<FJsonObject> Body = MakeObjectRequest(Object.Get());
	Body->SetStringField(TEXT("function"), TEXT("AddToHealth"));
	TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
	Arguments->SetNumberField(TEXT("Delta"), 25);
	Body->SetObjectField(TEXT("arguments"), Arguments);

	const FReflectionFunctionService Service;
	const FAutomationResult Result = Service.CallFunction(Module, TEXT("safe-mode-block-test"), Body);
	DestroyReflectionTestObject(Object.Get());

	return TestTrue(TEXT("Safe mode blocks function call for class outside allowlist"), !Result.bSuccess && Result.ErrorCode == TEXT("safe_mode_denied"));
}

bool FBATReflectionInvalidTargetsTest::RunTest(const FString& Parameters)
{
	FBlueprintAutomationToolkitModule Module;
	Module.Test_SetReflectionSafeMode(true);
	Module.Test_SetReflectionClassAllowList({ TEXT("BATReflectionTestObject") });

	const FReflectionPropertyService PropertyService;
	TSharedRef<FJsonObject> MissingObjectBody = MakeShared<FJsonObject>();
	MissingObjectBody->SetStringField(TEXT("objectPath"), TEXT("/Engine/Transient.DoesNotExist"));
	const FAutomationResult MissingObjectResult = PropertyService.GetObject(Module, TEXT("missing-object-test"), MissingObjectBody);

	TStrongObjectPtr<UBATReflectionTestObject> Object(CreateReflectionTestObject());
	TSharedRef<FJsonObject> BadPropertyBody = MakeObjectRequest(Object.Get());
	BadPropertyBody->SetStringField(TEXT("property"), TEXT("NotAProperty"));
	BadPropertyBody->SetNumberField(TEXT("value"), 1);
	const FAutomationResult BadPropertyResult = PropertyService.SetProperty(Module, TEXT("bad-property-test"), BadPropertyBody);

	const FReflectionFunctionService FunctionService;
	TSharedRef<FJsonObject> BadFunctionBody = MakeObjectRequest(Object.Get());
	BadFunctionBody->SetStringField(TEXT("function"), TEXT("DoesNotExist"));
	BadFunctionBody->SetObjectField(TEXT("arguments"), MakeShared<FJsonObject>());
	const FAutomationResult BadFunctionResult = FunctionService.CallFunction(Module, TEXT("bad-function-test"), BadFunctionBody);
	DestroyReflectionTestObject(Object.Get());

	const bool bMissingObject = !MissingObjectResult.bSuccess && MissingObjectResult.ErrorCode == TEXT("not_found");
	const bool bMissingProperty = !BadPropertyResult.bSuccess && BadPropertyResult.ErrorCode == TEXT("PropertyNotFound");
	const bool bMissingFunction = !BadFunctionResult.bSuccess && BadFunctionResult.ErrorCode == TEXT("FunctionNotFound");
	return TestTrue(TEXT("Reflection invalid target errors are reported"), bMissingObject && bMissingProperty && bMissingFunction);
}

#endif

#if WITH_DEV_AUTOMATION_TESTS

#include "BlueprintAutomationToolkitModule.h"

#include "Commands/Reflection/CallFunctionCommand.h"
#include "Commands/Reflection/SetPropertyCommand.h"
#include "Commands/PCG/ApplyPcgPlanCommand.h"
#include "Commands/AutomationCommand.h"
#include "Core/ForwardAxis.h"
#include "Domain/Requests/AssetSaveRequest.h"
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Routes/Blueprint/BlueprintGraphApplyRequest.h"
#include "Routes/PCG/PcgApplyRequest.h"
#include "Services/PCG/PcgNodeRegistry.h"
#include "Services/BlueprintGraph/BlueprintGraphLayoutService.h"
#include "Services/BlueprintGraph/BlueprintGraphNodeService.h"
#include "Services/BlueprintGraphService.h"
#include "Services/BlueprintGraph/BlueprintGraphValidationService.h"
#include "Http/HttpRequestUtils.h"
#include "Services/AssetService.h"
#include "Transport/RequestParsing.h"
#include "Services/Reflection/ReflectionFunctionService.h"
#include "Services/Reflection/ReflectionObjectResolver.h"
#include "Services/Reflection/ReflectionPropertyService.h"
#include "Tests/BATReflectionTestTypes.h"

#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include "Engine/EngineTypes.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "HAL/FileManager.h"
#include "UObject/UObjectGlobals.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATOpenApiSpecExistsTest, "BlueprintAutomationToolkit.OpenApi.SpecExists", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATOpenApiHasJobsAndLogsTest, "BlueprintAutomationToolkit.OpenApi.HasJobsAndLogsPaths", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATOpenApiHasEngineDiscoverPathTest, "BlueprintAutomationToolkit.OpenApi.HasEngineDiscoverPath", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATOpenApiHasBlueprintPlanPathsTest, "BlueprintAutomationToolkit.OpenApi.HasBlueprintPlanPaths", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATOpenApiHasBlueprintSchemaPathTest, "BlueprintAutomationToolkit.OpenApi.HasBlueprintSchemaPath", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATOpenApiHasPcgApplyPathTest, "BlueprintAutomationToolkit.OpenApi.HasPcgApplyPath", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATExecPythonRequiresPythonPermissionTest, "BlueprintAutomationToolkit.Security.ExecPythonRequiresPythonPermission", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATPermissionMapCoversPieAliasesTest, "BlueprintAutomationToolkit.Security.PermissionMapCoversPieAliases", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATPermissionMapCoversPcgApplyTest, "BlueprintAutomationToolkit.Security.PermissionMapCoversPcgApply", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATPermissionMapCoversBlueprintCompileSaveTest, "BlueprintAutomationToolkit.Security.PermissionMapCoversBlueprintCompileSave", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATResponseExportAddsFilesystemPermissionTest, "BlueprintAutomationToolkit.Security.ResponseExportAddsFilesystemPermission", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATExtensionCommandRegistrationMetadataTest, "BlueprintAutomationToolkit.Extension.CommandRegistrationMetadata", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATPendingResponseExportWritesViaSharedJsonBuilderTest, "BlueprintAutomationToolkit.Security.PendingResponseExportWritesViaSharedJsonBuilder", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATPieEditBlockRouteClassificationTest, "BlueprintAutomationToolkit.Security.PieEditBlockRouteClassification", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATAuthMissingResponseHasTokenHintTest, "BlueprintAutomationToolkit.Security.AuthMissingResponseHasTokenHint", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATJsonOkEnvelopeIncludesCanonicalFieldsTest, "BlueprintAutomationToolkit.Transport.JsonOkEnvelopeIncludesCanonicalFields", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATAuthFailureUsesCanonicalErrorArrayTest, "BlueprintAutomationToolkit.Transport.AuthFailureUsesCanonicalErrorArray", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATObjectQueryParsingBuildsPropertiesArrayTest, "BlueprintAutomationToolkit.Transport.ObjectQueryParsingBuildsPropertiesArray", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATGraphReadQueryParsingBuildsBodyTest, "BlueprintAutomationToolkit.Transport.GraphReadQueryParsingBuildsBody", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATGraphReadQueryParsingSupportsInspectionOptionsTest, "BlueprintAutomationToolkit.Transport.GraphReadQueryParsingSupportsInspectionOptions", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATGraphReadQueryParsingSupportsGraphAnalysisTest, "BlueprintAutomationToolkit.Transport.GraphReadQueryParsingSupportsGraphAnalysis", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATAssetDuplicateServiceRejectsEmptyRequestTest, "BlueprintAutomationToolkit.Assets.DuplicateServiceRejectsEmptyRequest", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATAssetCreateServiceRejectsInvalidClassTest, "BlueprintAutomationToolkit.Assets.CreateServiceRejectsInvalidClass", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATAssetSaveServiceRejectsEmptyRequestTest, "BlueprintAutomationToolkit.Assets.SaveServiceRejectsEmptyRequest", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATForwardAxisAliasesNormalizeTest, "BlueprintAutomationToolkit.Geometry.ForwardAxisAliasesNormalize", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATBlueprintGraphApplyRequestAcceptsSignedForwardAxisTest, "BlueprintAutomationToolkit.Blueprint.GraphApplyRequestAcceptsSignedForwardAxis", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATBlueprintGraphApplyRequestAcceptsActorOverlapEventTest, "BlueprintAutomationToolkit.Blueprint.GraphApplyRequestAcceptsActorOverlapEvent", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATBlueprintGraphApplyRequestAcceptsComponentBoundEventTest, "BlueprintAutomationToolkit.Blueprint.GraphApplyRequestAcceptsComponentBoundEvent", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATBlueprintGraphApplyRequestSupportsUpdateOnlyNodeTest, "BlueprintAutomationToolkit.Blueprint.GraphApplyRequestSupportsUpdateOnlyNode", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATBlueprintGraphApplyRequestSupportsAutoArrangeExistingNodesTest, "BlueprintAutomationToolkit.Blueprint.GraphApplyRequestSupportsAutoArrangeExistingNodes", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATBlueprintGraphApplyRequestSupportsAutoArrangeConnectedNodesTest, "BlueprintAutomationToolkit.Blueprint.GraphApplyRequestSupportsAutoArrangeConnectedNodes", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATBlueprintGraphApplyRequestSupportsPreserveFeederLanesTest, "BlueprintAutomationToolkit.Blueprint.GraphApplyRequestSupportsPreserveFeederLanes", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATPcgApplyRequestNormalizesConvenienceFieldsTest, "BlueprintAutomationToolkit.PCG.ApplyRequestNormalizesConvenienceFields", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATPcgApplyRequestRejectsDuplicateNodeIdsTest, "BlueprintAutomationToolkit.PCG.ApplyRequestRejectsDuplicateNodeIds", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATPcgNodeRegistryResolvesKnownFamiliesTest, "BlueprintAutomationToolkit.PCG.NodeRegistryResolvesKnownFamilies", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATPcgApplyRequestRejectsUnsupportedNodeFamilyTest, "BlueprintAutomationToolkit.PCG.ApplyRequestRejectsUnsupportedNodeFamily", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATPcgApplyCommandRejectsMissingGraphTest, "BlueprintAutomationToolkit.PCG.ApplyCommandRejectsMissingGraph", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATPcgApplyCommandCreatesAndSavesGraphTest, "BlueprintAutomationToolkit.PCG.ApplyCommandCreatesAndSavesGraph", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATBlueprintGraphLayoutPreservesImplicitNodePositionTest, "BlueprintAutomationToolkit.Blueprint.GraphLayoutPreservesImplicitNodePosition", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATBlueprintGraphLayoutAutoArrangesCreatedNodeTest, "BlueprintAutomationToolkit.Blueprint.GraphLayoutAutoArrangesCreatedNode", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATBlueprintGraphLayoutCentersFanInNodeTest, "BlueprintAutomationToolkit.Blueprint.GraphLayoutCentersFanInNode", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATBlueprintGraphLayoutPreservesFeederLanesTest, "BlueprintAutomationToolkit.Blueprint.GraphLayoutPreservesFeederLanes", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATBlueprintGraphApplyResultIncludesNodeValidationTest, "BlueprintAutomationToolkit.Blueprint.GraphApplyResultIncludesNodeValidation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATCanceledJobRemainsCanceledTest, "BlueprintAutomationToolkit.Jobs.CanceledJobRemainsCanceled", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATReflectionResolveObjectByPathTest, "BlueprintAutomationToolkit.Reflection.ResolveObjectByPath", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATReflectionListPropertiesTest, "BlueprintAutomationToolkit.Reflection.ListProperties", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATReflectionSetScalarPropertyTest, "BlueprintAutomationToolkit.Reflection.SetScalarProperty", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATReflectionSetStructPropertyTest, "BlueprintAutomationToolkit.Reflection.SetStructProperty", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATReflectionListFunctionsTest, "BlueprintAutomationToolkit.Reflection.ListFunctions", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATReflectionCallSafeFunctionTest, "BlueprintAutomationToolkit.Reflection.CallSafeFunction", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATReflectionSetPropertyCommandTest, "BlueprintAutomationToolkit.Reflection.SetPropertyCommand", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATReflectionCallFunctionCommandTest, "BlueprintAutomationToolkit.Reflection.CallFunctionCommand", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATReflectionSafeModeBlockingTest, "BlueprintAutomationToolkit.Reflection.SafeModeBlocking", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATControlPanelSafeModeTogglePersistsTest, "BlueprintAutomationToolkit.Reflection.ControlPanelSafeModeTogglePersists", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATReflectionInvalidTargetsTest, "BlueprintAutomationToolkit.Reflection.InvalidTargets", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATResponseExportWritesFileTest, "BlueprintAutomationToolkit.Reflection.ResponseExportWritesFile", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	static FString MakeUniquePcgTestGraphObjectPath()
	{
		const FString Suffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
		return FString::Printf(TEXT("/Game/__BATTests__/PCG/BAT_TestGraph_%s.BAT_TestGraph_%s"), *Suffix, *Suffix);
	}

	static FString GetPackagePathFromObjectPath(const FString& ObjectPath)
	{
		return FPackageName::ObjectPathToPackageName(ObjectPath);
	}

	static FString GetPackageFilenameFromObjectPath(const FString& ObjectPath)
	{
		return FPackageName::LongPackageNameToFilename(GetPackagePathFromObjectPath(ObjectPath), FPackageName::GetAssetPackageExtension());
	}

	static void CleanupPcgTestGraphAsset(const FString& ObjectPath)
	{
		const FString PackagePath = GetPackagePathFromObjectPath(ObjectPath);
		if (UPackage* Package = FindPackage(nullptr, *PackagePath))
		{
			ResetLoaders(Package);
		}

		const FString Filename = GetPackageFilenameFromObjectPath(ObjectPath);
		IFileManager::Get().Delete(*Filename, false, true, true);
	}

	static FAutomationContext MakePcgApplyContext(const TSharedPtr<FJsonObject>& BodyObj)
	{
		FAutomationContext Context;
		Context.RequestId = TEXT("bat-test-request");
		Context.Endpoint = TEXT("/pcg/apply");
		Context.Body = BodyObj;
		Context.bReturnRawObject = true;
		return Context;
	}

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

	static bool ParseJsonObject(const FString& Json, TSharedPtr<FJsonObject>& OutObject)
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		return FJsonSerializer::Deserialize(Reader, OutObject) && OutObject.IsValid();
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

	struct FTestNoopAutomationCommand final : FAutomationCommand
	{
		virtual FAutomationResult Execute(FAutomationContext& Context) override
		{
			TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
			Payload->SetStringField(TEXT("endpoint"), Context.Endpoint);
			return FAutomationResult::Ok(MakeShared<FJsonValueObject>(Payload));
		}
	};
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

bool FBATOpenApiHasEngineDiscoverPathTest::RunTest(const FString& Parameters)
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

	return TestTrue(TEXT("OpenAPI contains /engine/discover"), Spec.Contains(TEXT("/engine/discover:"), ESearchCase::CaseSensitive));
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
	TestTrue(TEXT("OpenAPI contains /blueprint/graph/apply"), Spec.Contains(TEXT("/blueprint/graph/apply:"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("OpenAPI contains /blueprint/graph/read"), Spec.Contains(TEXT("/blueprint/graph/read:"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("OpenAPI contains /blueprint/compile_save"), Spec.Contains(TEXT("/blueprint/compile_save:"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("OpenAPI documents compileDiagnostics"), Spec.Contains(TEXT("compileDiagnostics"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("OpenAPI documents nodeValidation"), Spec.Contains(TEXT("nodeValidation"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("OpenAPI documents includeNodeProperties"), Spec.Contains(TEXT("includeNodeProperties"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("OpenAPI documents includeGraphAnalysis"), Spec.Contains(TEXT("includeGraphAnalysis"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("OpenAPI documents blendNodes"), Spec.Contains(TEXT("blendNodes"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("OpenAPI documents nearestBlendNodeId"), Spec.Contains(TEXT("nearestBlendNodeId"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("OpenAPI documents primaryOutputPoseSpace"), Spec.Contains(TEXT("primaryOutputPoseSpace"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("OpenAPI documents updateOnly"), Spec.Contains(TEXT("updateOnly"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("OpenAPI documents auto-aligned node placement"), Spec.Contains(TEXT("auto-aligns the node from linked neighbors"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("OpenAPI documents autoArrangeExistingNodes"), Spec.Contains(TEXT("autoArrangeExistingNodes"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("OpenAPI documents autoArrangeConnectedNodes"), Spec.Contains(TEXT("autoArrangeConnectedNodes"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("OpenAPI contains /actor/destroy"), Spec.Contains(TEXT("/actor/destroy:"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("OpenAPI contains /editor/select"), Spec.Contains(TEXT("/editor/select:"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("OpenAPI contains /editor/focus"), Spec.Contains(TEXT("/editor/focus:"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("OpenAPI contains /object/resolve"), Spec.Contains(TEXT("/object/resolve:"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("OpenAPI contains /object/describe"), Spec.Contains(TEXT("/object/describe:"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("OpenAPI contains /object/get_property"), Spec.Contains(TEXT("/object/get_property:"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("OpenAPI contains /object/set_property"), Spec.Contains(TEXT("/object/set_property:"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("OpenAPI contains /object/call_function"), Spec.Contains(TEXT("/object/call_function:"), ESearchCase::CaseSensitive));
	TestFalse(TEXT("OpenAPI removes /ai/health"), Spec.Contains(TEXT("/ai/health:"), ESearchCase::CaseSensitive));
	TestFalse(TEXT("OpenAPI removes /object/get"), Spec.Contains(TEXT("/object/get:"), ESearchCase::CaseSensitive));
	TestFalse(TEXT("OpenAPI removes /object/set-property"), Spec.Contains(TEXT("/object/set-property:"), ESearchCase::CaseSensitive));
	TestFalse(TEXT("OpenAPI removes /object/call-function"), Spec.Contains(TEXT("/object/call-function:"), ESearchCase::CaseSensitive));
	TestFalse(TEXT("OpenAPI removes /ai/pie/start"), Spec.Contains(TEXT("/ai/pie/start:"), ESearchCase::CaseSensitive));
	TestFalse(TEXT("OpenAPI removes /ai/pie/stop"), Spec.Contains(TEXT("/ai/pie/stop:"), ESearchCase::CaseSensitive));
	TestFalse(TEXT("OpenAPI removes /blueprint/compile"), Spec.Contains(TEXT("/blueprint/compile:"), ESearchCase::CaseSensitive));
	TestFalse(TEXT("OpenAPI removes /uobject/get"), Spec.Contains(TEXT("/uobject/get:"), ESearchCase::CaseSensitive));
	TestFalse(TEXT("OpenAPI removes /uobject/set"), Spec.Contains(TEXT("/uobject/set:"), ESearchCase::CaseSensitive));
	TestFalse(TEXT("OpenAPI removes /uobject/call"), Spec.Contains(TEXT("/uobject/call:"), ESearchCase::CaseSensitive));
	TestFalse(TEXT("OpenAPI removes /object/list-properties"), Spec.Contains(TEXT("/object/list-properties:"), ESearchCase::CaseSensitive));
	TestFalse(TEXT("OpenAPI removes /object/list-functions"), Spec.Contains(TEXT("/object/list-functions:"), ESearchCase::CaseSensitive));
	TestFalse(TEXT("OpenAPI removes /blueprint/save"), Spec.Contains(TEXT("/blueprint/save:"), ESearchCase::CaseSensitive));
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

bool FBATOpenApiHasPcgApplyPathTest::RunTest(const FString& Parameters)
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

	TestTrue(TEXT("OpenAPI contains /pcg/apply"), Spec.Contains(TEXT("/pcg/apply:"), ESearchCase::CaseSensitive));
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

bool FBATPermissionMapCoversPcgApplyTest::RunTest(const FString& Parameters)
{
	FBlueprintAutomationToolkitModule Module;
	const uint32 ExpectedMask =
		static_cast<uint32>(FBlueprintAutomationToolkitModule::EAutomationTestPermission::Editor) |
		static_cast<uint32>(FBlueprintAutomationToolkitModule::EAutomationTestPermission::Filesystem);

	return TestEqual(TEXT("/pcg/apply requires Editor and Filesystem permissions"), Module.Test_GetRouteRequiredPermissions(TEXT("/pcg/apply")), ExpectedMask);
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

bool FBATResponseExportAddsFilesystemPermissionTest::RunTest(const FString& Parameters)
{
	FBlueprintAutomationToolkitModule Module;
	const uint32 ExpectedMask =
		static_cast<uint32>(FBlueprintAutomationToolkitModule::EAutomationTestPermission::Editor) |
		static_cast<uint32>(FBlueprintAutomationToolkitModule::EAutomationTestPermission::Filesystem);

	TSharedRef<FJsonObject> BodyObj = MakeShared<FJsonObject>();
	BodyObj->SetStringField(TEXT("objectPath"), TEXT("/Engine/Transient.Dummy"));
	BodyObj->SetStringField(TEXT("responseOutputPath"), TEXT("tests/permission-check"));

	TestEqual(TEXT("/object/get_property with response export requires Editor and Filesystem permissions"), Module.Test_GetRequestRequiredPermissions(TEXT("/object/get_property"), BodyObj), ExpectedMask);
	return true;
}

bool FBATExtensionCommandRegistrationMetadataTest::RunTest(const FString& Parameters)
{
	FBlueprintAutomationToolkitModule Module;
	FString Error;
	const FString Endpoint = TEXT("/extension/test/noop");
	const uint32 ExpectedMask =
		static_cast<uint32>(FBlueprintAutomationToolkitModule::EAutomationTestPermission::Editor) |
		static_cast<uint32>(FBlueprintAutomationToolkitModule::EAutomationTestPermission::Filesystem);

	FBATAutomationCommandRegistration Registration;
	Registration.Endpoint = Endpoint;
	Registration.Factory = []() -> TUniquePtr<FAutomationCommand>
	{
		return MakeUnique<FTestNoopAutomationCommand>();
	};
	Registration.PermissionTier = EBATAutomationPermissionTier::Edit;
	Registration.RequiredPermissions = EBATAutomationPermission::Editor | EBATAutomationPermission::Filesystem;
	Registration.bBindRoute = true;
	Registration.bBlockDuringPie = true;

	if (!TestTrue(TEXT("Extension command registration succeeds"), Module.RegisterAutomationCommand(MoveTemp(Registration), &Error)))
	{
		AddError(Error);
		return false;
	}

	TestTrue(TEXT("Extension command is discoverable via command lookup"), Module.HasAutomationCommand(Endpoint));
	TestEqual(TEXT("Extension command permissions flow through route permission lookup"), Module.Test_GetRouteRequiredPermissions(Endpoint), ExpectedMask);
	TestTrue(TEXT("Extension command PIE blocking flows from registration metadata"), Module.Test_IsEditorAssetMutationBlockedDuringPie(Endpoint));

	TArray<FBATAutomationCommandInfo> Infos;
	Module.GetAutomationCommandInfos(Infos);
	const FBATAutomationCommandInfo* Match = Infos.FindByPredicate([&Endpoint](const FBATAutomationCommandInfo& Info)
	{
		return Info.Endpoint == Endpoint;
	});

	if (!TestNotNull(TEXT("Extension command appears in registration info"), Match))
	{
		return false;
	}

	TestTrue(TEXT("Extension command keeps POST route binding enabled"), Match->bBindRoute);
	TestTrue(TEXT("Extension command metadata marks PIE blocking"), Match->bBlockDuringPie);
	TestFalse(TEXT("Extension command is not marked built-in"), Match->bBuiltIn);

	if (!TestTrue(TEXT("Extension command can be unregistered"), Module.UnregisterAutomationCommand(Endpoint, &Error)))
	{
		AddError(Error);
		return false;
	}

	TestFalse(TEXT("Extension command is removed after unregister"), Module.HasAutomationCommand(Endpoint));
	return true;
}

bool FBATPendingResponseExportWritesViaSharedJsonBuilderTest::RunTest(const FString& Parameters)
{
	const FString RequestId = TEXT("pending-response-export-test");
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("responseOutputPath"), TEXT("tests/pending-json-builder"));

	FString ResolveError;
	FString OutputPath;
	if (!TestTrue(TEXT("Safe responseOutputPath resolves"), BAT::Http::TryResolveResponseOutputPath(Body, RequestId, OutputPath, ResolveError)))
	{
		AddError(ResolveError);
		return false;
	}

	FString RegisterError;
	if (!TestTrue(TEXT("Pending response export registers successfully"), BAT::Http::RegisterPendingResponseExport(Body, RequestId, RegisterError)))
	{
		AddError(RegisterError);
		return false;
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), true);
	TUniquePtr<FHttpServerResponse> Response = BAT::Http::MakeJsonResponse(200, Root, RequestId);
	if (!TestNotNull(TEXT("Shared JSON builder returns a response"), Response.Get()))
	{
		return false;
	}

	FString Contents;
	if (!TestTrue(TEXT("Pending export file exists after shared JSON builder response"), FFileHelper::LoadFileToString(Contents, *OutputPath)))
	{
		return false;
	}

	TSharedPtr<FJsonObject> ExportedObj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Contents);
	if (!TestTrue(TEXT("Pending export file contains valid JSON"), FJsonSerializer::Deserialize(Reader, ExportedObj) && ExportedObj.IsValid()))
	{
		return false;
	}

	bool bOk = false;
	TestTrue(TEXT("Pending export file contains ok field"), ExportedObj->TryGetBoolField(TEXT("ok"), bOk));
	TestTrue(TEXT("Pending export file contains ok=true"), bOk);
	IFileManager::Get().Delete(*OutputPath, false, true, true);
	return true;
}

bool FBATPieEditBlockRouteClassificationTest::RunTest(const FString& Parameters)
{
	FBlueprintAutomationToolkitModule Module;

	TestTrue(TEXT("/asset/duplicate is blocked during PIE"), Module.Test_IsEditorAssetMutationBlockedDuringPie(TEXT("/asset/duplicate")));
	TestTrue(TEXT("/object/set_property is blocked during PIE"), Module.Test_IsEditorAssetMutationBlockedDuringPie(TEXT("/object/set_property")));
	TestTrue(TEXT("/actor/destroy is blocked during PIE"), Module.Test_IsEditorAssetMutationBlockedDuringPie(TEXT("/actor/destroy")));
	TestTrue(TEXT("/blueprint/set-defaults is blocked during PIE"), Module.Test_IsEditorAssetMutationBlockedDuringPie(TEXT("/blueprint/set-defaults")));
	TestTrue(TEXT("/blueprint/pin/connect is blocked during PIE"), Module.Test_IsEditorAssetMutationBlockedDuringPie(TEXT("/blueprint/pin/connect")));
	TestTrue(TEXT("/blueprint/compile_save is blocked during PIE"), Module.Test_IsEditorAssetMutationBlockedDuringPie(TEXT("/blueprint/compile_save")));

	TestFalse(TEXT("/blueprint/schema remains allowed during PIE"), Module.Test_IsEditorAssetMutationBlockedDuringPie(TEXT("/blueprint/schema")));
	TestFalse(TEXT("/blueprint/graph/read remains allowed during PIE"), Module.Test_IsEditorAssetMutationBlockedDuringPie(TEXT("/blueprint/graph/read")));
	TestFalse(TEXT("/blueprint/node/describe remains allowed during PIE"), Module.Test_IsEditorAssetMutationBlockedDuringPie(TEXT("/blueprint/node/describe")));
	TestFalse(TEXT("/object/describe remains allowed during PIE"), Module.Test_IsEditorAssetMutationBlockedDuringPie(TEXT("/object/describe")));
	TestFalse(TEXT("/object/get_property remains allowed during PIE"), Module.Test_IsEditorAssetMutationBlockedDuringPie(TEXT("/object/get_property")));
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

bool FBATJsonOkEnvelopeIncludesCanonicalFieldsTest::RunTest(const FString& Parameters)
{
	const FString RequestId = TEXT("json-ok-envelope-test");
	TUniquePtr<FHttpServerResponse> Response = BAT::Http::MakeJsonOk(MakeShared<FJsonValueString>(TEXT("ok")), 200, RequestId);
	if (!TestNotNull(TEXT("MakeJsonOk should return a response"), Response.Get()))
	{
		return false;
	}

	const FString Body = FString(UTF8_TO_TCHAR(reinterpret_cast<const char*>(Response->Body.GetData())));
	TSharedPtr<FJsonObject> Root;
	if (!TestTrue(TEXT("MakeJsonOk response body parses as JSON"), ParseJsonObject(Body, Root)))
	{
		return false;
	}

	bool bOk = false;
	TestTrue(TEXT("MakeJsonOk response contains ok field"), Root->TryGetBoolField(TEXT("ok"), bOk));
	TestTrue(TEXT("MakeJsonOk response sets ok=true"), bOk);

	FString ParsedRequestId;
	TestTrue(TEXT("MakeJsonOk response contains requestId field"), Root->TryGetStringField(TEXT("requestId"), ParsedRequestId));
	TestEqual(TEXT("MakeJsonOk response preserves requestId"), ParsedRequestId, RequestId);

	const TArray<TSharedPtr<FJsonValue>>* Errors = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Warnings = nullptr;
	TestTrue(TEXT("MakeJsonOk response contains errors array"), Root->TryGetArrayField(TEXT("errors"), Errors) && Errors);
	TestTrue(TEXT("MakeJsonOk response contains warnings array"), Root->TryGetArrayField(TEXT("warnings"), Warnings) && Warnings);
	TestTrue(TEXT("MakeJsonOk response contains data field"), Root->HasField(TEXT("data")));
	return true;
}

bool FBATAuthFailureUsesCanonicalErrorArrayTest::RunTest(const FString& Parameters)
{
	FBlueprintAutomationToolkitModule Module;
	FString Json;
	FString WwwAuthenticate;
	if (!TestTrue(TEXT("Auth failure response should be buildable"), Module.Test_BuildAuthFailureResponse(TEXT("auth_missing"), Json, WwwAuthenticate)))
	{
		return false;
	}

	TSharedPtr<FJsonObject> Root;
	if (!TestTrue(TEXT("Auth failure response body parses as JSON"), ParseJsonObject(Json, Root)))
	{
		return false;
	}

	bool bOk = true;
	TestTrue(TEXT("Auth failure response contains ok field"), Root->TryGetBoolField(TEXT("ok"), bOk));
	TestFalse(TEXT("Auth failure response sets ok=false"), bOk);
	TestFalse(TEXT("Auth failure response does not use legacy top-level error object"), Root->HasField(TEXT("error")));

	const TArray<TSharedPtr<FJsonValue>>* Errors = nullptr;
	if (!TestTrue(TEXT("Auth failure response contains errors array"), Root->TryGetArrayField(TEXT("errors"), Errors) && Errors && Errors->Num() > 0))
	{
		return false;
	}

	const TSharedPtr<FJsonObject> FirstError = (*Errors)[0].IsValid() ? (*Errors)[0]->AsObject() : nullptr;
	if (!TestNotNull(TEXT("Auth failure response first error is structured"), FirstError.Get()))
	{
		return false;
	}

	FString Code;
	FString Message;
	bool bRecoverable = false;
	TestTrue(TEXT("Auth failure response error includes code"), FirstError->TryGetStringField(TEXT("code"), Code));
	TestTrue(TEXT("Auth failure response error includes message"), FirstError->TryGetStringField(TEXT("message"), Message));
	TestTrue(TEXT("Auth failure response error includes recoverable flag"), FirstError->TryGetBoolField(TEXT("recoverable"), bRecoverable));
	TestEqual(TEXT("Auth failure response normalizes auth_missing code"), Code, FString(TEXT("auth_missing")));
	TestTrue(TEXT("Auth failure response marks missing auth as recoverable"), bRecoverable);
	return true;
}

bool FBATObjectQueryParsingBuildsPropertiesArrayTest::RunTest(const FString& Parameters)
{
	FHttpServerRequest Request;
	Request.QueryParams.Add(TEXT("target"), TEXT("/Game/Test/BP_Test.BP_Test"));
	Request.QueryParams.Add(TEXT("properties"), TEXT("Health, MaxHealth ,TeamId"));
	Request.QueryParams.Add(TEXT("verbose"), TEXT("true"));
	Request.QueryParams.Add(TEXT("pie_index"), TEXT("2"));

	const TSharedPtr<FJsonObject> Body = BAT::Transport::BuildObjectQueryBody(Request);
	if (!TestNotNull(TEXT("Object query parsing should build a body object"), Body.Get()))
	{
		return false;
	}

	FString Target;
	bool bVerbose = false;
	double PieIndex = -1.0;
	const TArray<TSharedPtr<FJsonValue>>* Properties = nullptr;

	TestTrue(TEXT("Object query parsing copies target"), Body->TryGetStringField(TEXT("target"), Target));
	TestEqual(TEXT("Object query parsing preserves target value"), Target, FString(TEXT("/Game/Test/BP_Test.BP_Test")));
	TestTrue(TEXT("Object query parsing copies verbose flag"), Body->TryGetBoolField(TEXT("verbose"), bVerbose));
	TestTrue(TEXT("Object query parsing sets verbose=true"), bVerbose);
	TestTrue(TEXT("Object query parsing copies pie_index"), Body->TryGetNumberField(TEXT("pie_index"), PieIndex));
	TestEqual(TEXT("Object query parsing preserves pie_index"), PieIndex, 2.0);

	if (!TestTrue(TEXT("Object query parsing builds properties array"), Body->TryGetArrayField(TEXT("properties"), Properties) && Properties))
	{
		return false;
	}

	TestEqual(TEXT("Object query parsing splits properties into three entries"), Properties->Num(), 3);
	TestEqual(TEXT("Object query parsing trims property whitespace"), (*Properties)[1]->AsString(), FString(TEXT("MaxHealth")));
	return true;
}

bool FBATGraphReadQueryParsingBuildsBodyTest::RunTest(const FString& Parameters)
{
	FHttpServerRequest Request;
	Request.QueryParams.Add(TEXT("blueprint"), TEXT("/Game/BP_Spawner"));
	Request.QueryParams.Add(TEXT("graph"), TEXT("EventGraph"));

	const TSharedPtr<FJsonObject> Body = BAT::Transport::BuildBlueprintGraphReadQueryBody(Request);
	if (!TestNotNull(TEXT("Graph read query parsing should build a body object"), Body.Get()))
	{
		return false;
	}

	FString Blueprint;
	FString Graph;
	TestTrue(TEXT("Graph read query parsing copies blueprint"), Body->TryGetStringField(TEXT("blueprint"), Blueprint));
	TestTrue(TEXT("Graph read query parsing copies graph"), Body->TryGetStringField(TEXT("graph"), Graph));
	TestEqual(TEXT("Graph read query parsing preserves blueprint value"), Blueprint, FString(TEXT("/Game/BP_Spawner")));
	TestEqual(TEXT("Graph read query parsing preserves graph value"), Graph, FString(TEXT("EventGraph")));
	return true;
}

bool FBATGraphReadQueryParsingSupportsInspectionOptionsTest::RunTest(const FString& Parameters)
{
	FHttpServerRequest Request;
	Request.QueryParams.Add(TEXT("blueprint"), TEXT("/Game/BP_Spawner"));
	Request.QueryParams.Add(TEXT("graph"), TEXT("AnimGraph"));
	Request.QueryParams.Add(TEXT("includeNodeProperties"), TEXT("true"));
	Request.QueryParams.Add(TEXT("includeNodeValidation"), TEXT("1"));
	Request.QueryParams.Add(TEXT("propertyPaths"), TEXT("Node.Sequence, Node.LayerSetup , Node.RotationMode"));

	const TSharedPtr<FJsonObject> Body = BAT::Transport::BuildBlueprintGraphReadQueryBody(Request);
	if (!TestNotNull(TEXT("Graph read inspection query parsing should build a body object"), Body.Get()))
	{
		return false;
	}

	bool bIncludeNodeProperties = false;
	bool bIncludeNodeValidation = false;
	const TArray<TSharedPtr<FJsonValue>>* PropertyPaths = nullptr;
	TestTrue(TEXT("Graph read inspection query parsing copies includeNodeProperties"), Body->TryGetBoolField(TEXT("includeNodeProperties"), bIncludeNodeProperties));
	TestTrue(TEXT("Graph read inspection query parsing copies includeNodeValidation"), Body->TryGetBoolField(TEXT("includeNodeValidation"), bIncludeNodeValidation));
	TestTrue(TEXT("Graph read inspection query parsing sets includeNodeProperties=true"), bIncludeNodeProperties);
	TestTrue(TEXT("Graph read inspection query parsing sets includeNodeValidation=true"), bIncludeNodeValidation);
	if (!TestTrue(TEXT("Graph read inspection query parsing builds propertyPaths array"), Body->TryGetArrayField(TEXT("propertyPaths"), PropertyPaths) && PropertyPaths))
	{
		return false;
	}

	TestEqual(TEXT("Graph read inspection query parsing splits propertyPaths into three entries"), PropertyPaths->Num(), 3);
	TestEqual(TEXT("Graph read inspection query parsing trims property path whitespace"), (*PropertyPaths)[1]->AsString(), FString(TEXT("Node.LayerSetup")));
	return true;
}

bool FBATGraphReadQueryParsingSupportsGraphAnalysisTest::RunTest(const FString& Parameters)
{
	FHttpServerRequest Request;
	Request.QueryParams.Add(TEXT("blueprint"), TEXT("/Game/BP_Spawner"));
	Request.QueryParams.Add(TEXT("graph"), TEXT("AnimGraph"));
	Request.QueryParams.Add(TEXT("includeGraphAnalysis"), TEXT("true"));

	const TSharedPtr<FJsonObject> Body = BAT::Transport::BuildBlueprintGraphReadQueryBody(Request);
	if (!TestNotNull(TEXT("Graph read analysis query parsing should build a body object"), Body.Get()))
	{
		return false;
	}

	bool bIncludeGraphAnalysis = false;
	TestTrue(TEXT("Graph read analysis query parsing copies includeGraphAnalysis"), Body->TryGetBoolField(TEXT("includeGraphAnalysis"), bIncludeGraphAnalysis));
	return TestTrue(TEXT("Graph read analysis query parsing sets includeGraphAnalysis=true"), bIncludeGraphAnalysis);
}

bool FBATAssetDuplicateServiceRejectsEmptyRequestTest::RunTest(const FString& Parameters)
{
	FBlueprintAutomationToolkitModule Module;
	FAssetService Service;
	FBATAssetDuplicateRequest Request;

	const FAutomationResult Result = Service.DuplicateAssets(Module, Request);
	TestFalse(TEXT("Asset duplicate service should reject an empty request"), Result.bSuccess);
	TestEqual(TEXT("Asset duplicate service should report bad_args for an empty request"), Result.ErrorCode, FString(TEXT("bad_args")));
	TestEqual(TEXT("Asset duplicate service should use HTTP 400 for an empty request"), Result.StatusCode, 400);
	return true;
}

bool FBATAssetCreateServiceRejectsInvalidClassTest::RunTest(const FString& Parameters)
{
	FBlueprintAutomationToolkitModule Module;
	FAssetService Service;
	FBATAssetCreateRequest Request;
	Request.ClassPath = TEXT("/Script/Engine.DoesNotExist");
	Request.AssetPath = TEXT("/Game/BAT_Invalid");
	Request.Body = MakeShared<FJsonObject>();

	const FAutomationResult Result = Service.CreateAsset(Module, Request);
	TestFalse(TEXT("Asset create service should reject an invalid class"), Result.bSuccess);
	TestEqual(TEXT("Asset create service should report bad_args for an invalid class"), Result.ErrorCode, FString(TEXT("bad_args")));
	TestEqual(TEXT("Asset create service should use HTTP 400 for an invalid class"), Result.StatusCode, 400);
	return true;
}

bool FBATAssetSaveServiceRejectsEmptyRequestTest::RunTest(const FString& Parameters)
{
	FBlueprintAutomationToolkitModule Module;
	FAssetService Service;
	FBATAssetSaveRequest Request;

	const FAutomationResult Result = Service.SaveAssets(Module, Request);
	TestFalse(TEXT("Asset save service should reject an empty request"), Result.bSuccess);
	TestEqual(TEXT("Asset save service should report bad_args for an empty request"), Result.ErrorCode, FString(TEXT("bad_args")));
	TestEqual(TEXT("Asset save service should use HTTP 400 for an empty request"), Result.StatusCode, 400);
	return true;
}

bool FBATForwardAxisAliasesNormalizeTest::RunTest(const FString& Parameters)
{
	FString CanonicalAxis;
	FString Error;

	TestTrue(TEXT("Forward-axis helper should accept 'left'"), BAT::ForwardAxis::TryNormalizeAxis(TEXT("left"), CanonicalAxis, Error));
	TestEqual(TEXT("'left' should normalize to -Y"), CanonicalAxis, FString(TEXT("-Y")));

	TestTrue(TEXT("Forward-axis helper should accept '+Z'"), BAT::ForwardAxis::TryNormalizeAxis(TEXT(" +Z "), CanonicalAxis, Error));
	TestEqual(TEXT("'+Z' should normalize to Z"), CanonicalAxis, FString(TEXT("Z")));

	FQuat AxisToUnrealQuat = FQuat::Identity;
	TestTrue(TEXT("Forward-axis helper should build a quaternion for -Y"), BAT::ForwardAxis::TryBuildAxisToUnrealQuat(TEXT("-Y"), AxisToUnrealQuat, Error));
	TestTrue(TEXT("-Y should rotate to Unreal forward"), AxisToUnrealQuat.RotateVector(-FVector::RightVector).Equals(FVector::ForwardVector, KINDA_SMALL_NUMBER));
	return true;
}

bool FBATBlueprintGraphApplyRequestAcceptsSignedForwardAxisTest::RunTest(const FString& Parameters)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("blueprint"), TEXT("/Game/Test/BP_Test"));
	Body->SetStringField(TEXT("graph"), TEXT("AnimGraph"));

	TSharedRef<FJsonObject> Node = MakeShared<FJsonObject>();
	Node->SetStringField(TEXT("id"), TEXT("pose_node"));
	Node->SetStringField(TEXT("type"), TEXT("AnimGraphNode_ModifyBone"));
	Node->SetStringField(TEXT("forward_axis"), TEXT("left"));

	TArray<TSharedPtr<FJsonValue>> Nodes;
	Nodes.Add(MakeShared<FJsonValueObject>(Node));
	Body->SetArrayField(TEXT("nodes"), Nodes);

	Body->SetArrayField(TEXT("links"), TArray<TSharedPtr<FJsonValue>>());

	FBlueprintGraphApplyRequest Request;
	TArray<FString> ParseErrors;
	const bool bParsed = BAT::BlueprintGraphApplyRequest::Parse(Body, Request, ParseErrors);
	if (!TestTrue(TEXT("Graph apply request should accept signed and alias forward axes"), bParsed))
	{
		for (const FString& ParseError : ParseErrors)
		{
			AddError(ParseError);
		}
		return false;
	}

	if (!TestEqual(TEXT("Graph apply request should preserve one node"), Request.Nodes.Num(), 1))
	{
		return false;
	}

	return TestEqual(TEXT("Graph apply request should canonicalize alias axes"), Request.Nodes[0].ForwardAxis, FString(TEXT("-Y")));
}

bool FBATBlueprintGraphApplyRequestAcceptsActorOverlapEventTest::RunTest(const FString& Parameters)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("blueprint"), TEXT("/Game/Test/BP_Test"));
	Body->SetStringField(TEXT("graph"), TEXT("EventGraph"));

	TSharedRef<FJsonObject> Node = MakeShared<FJsonObject>();
	Node->SetStringField(TEXT("id"), TEXT("overlap_event"));
	Node->SetStringField(TEXT("type"), TEXT("K2Node_Event"));
	Node->SetStringField(TEXT("event"), TEXT("ActorBeginOverlap"));

	TArray<TSharedPtr<FJsonValue>> Nodes;
	Nodes.Add(MakeShared<FJsonValueObject>(Node));
	Body->SetArrayField(TEXT("nodes"), Nodes);
	Body->SetArrayField(TEXT("links"), TArray<TSharedPtr<FJsonValue>>());

	FBlueprintGraphApplyRequest Request;
	TArray<FString> ParseErrors;
	const bool bParsed = BAT::BlueprintGraphApplyRequest::Parse(Body, Request, ParseErrors);
	if (!TestTrue(TEXT("Graph apply request should accept actor overlap events"), bParsed))
	{
		for (const FString& ParseError : ParseErrors)
		{
			AddError(ParseError);
		}
		return false;
	}

	if (!TestEqual(TEXT("Graph apply request should preserve one actor event node"), Request.Nodes.Num(), 1))
	{
		return false;
	}

	return TestEqual(TEXT("Graph apply request should preserve actor event name"), Request.Nodes[0].Event, FString(TEXT("ActorBeginOverlap")));
}

bool FBATBlueprintGraphApplyRequestAcceptsComponentBoundEventTest::RunTest(const FString& Parameters)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("blueprint"), TEXT("/Game/Test/BP_Test"));
	Body->SetStringField(TEXT("graph"), TEXT("EventGraph"));

	TSharedRef<FJsonObject> Node = MakeShared<FJsonObject>();
	Node->SetStringField(TEXT("id"), TEXT("mesh_overlap"));
	Node->SetStringField(TEXT("type"), TEXT("K2Node_ComponentBoundEvent"));
	Node->SetStringField(TEXT("component"), TEXT("PickupMesh"));
	Node->SetStringField(TEXT("event"), TEXT("OnComponentBeginOverlap"));

	TArray<TSharedPtr<FJsonValue>> Nodes;
	Nodes.Add(MakeShared<FJsonValueObject>(Node));
	Body->SetArrayField(TEXT("nodes"), Nodes);
	Body->SetArrayField(TEXT("links"), TArray<TSharedPtr<FJsonValue>>());

	FBlueprintGraphApplyRequest Request;
	TArray<FString> ParseErrors;
	const bool bParsed = BAT::BlueprintGraphApplyRequest::Parse(Body, Request, ParseErrors);
	if (!TestTrue(TEXT("Graph apply request should accept component bound events"), bParsed))
	{
		for (const FString& ParseError : ParseErrors)
		{
			AddError(ParseError);
		}
		return false;
	}

	if (!TestEqual(TEXT("Graph apply request should preserve one component event node"), Request.Nodes.Num(), 1))
	{
		return false;
	}

	TestEqual(TEXT("Graph apply request should preserve component name"), Request.Nodes[0].Component, FString(TEXT("PickupMesh")));
	return TestEqual(TEXT("Graph apply request should preserve component event name"), Request.Nodes[0].Event, FString(TEXT("OnComponentBeginOverlap")));
}

bool FBATBlueprintGraphApplyRequestSupportsUpdateOnlyNodeTest::RunTest(const FString& Parameters)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("blueprint"), TEXT("/Game/Test/BP_Test"));
	Body->SetStringField(TEXT("graph"), TEXT("AnimGraph"));

	TSharedRef<FJsonObject> Options = MakeShared<FJsonObject>();
	Options->SetBoolField(TEXT("createMissingNodes"), false);
	Body->SetObjectField(TEXT("options"), Options);

	TSharedRef<FJsonObject> Node = MakeShared<FJsonObject>();
	Node->SetStringField(TEXT("id"), TEXT("bat_pose_node"));
	Node->SetBoolField(TEXT("updateOnly"), true);
	TSharedRef<FJsonObject> Pins = MakeShared<FJsonObject>();
	Pins->SetStringField(TEXT("Rotation"), TEXT("0,0,0"));
	Node->SetObjectField(TEXT("pins"), Pins);

	TArray<TSharedPtr<FJsonValue>> Nodes;
	Nodes.Add(MakeShared<FJsonValueObject>(Node));
	Body->SetArrayField(TEXT("nodes"), Nodes);
	Body->SetArrayField(TEXT("links"), TArray<TSharedPtr<FJsonValue>>());

	FBlueprintGraphApplyRequest Request;
	TArray<FString> ParseErrors;
	const bool bParsed = BAT::BlueprintGraphApplyRequest::Parse(Body, Request, ParseErrors);
	if (!TestTrue(TEXT("Graph apply request should allow update-only nodes without type"), bParsed))
	{
		for (const FString& ParseError : ParseErrors)
		{
			AddError(ParseError);
		}
		return false;
	}

	if (!TestFalse(TEXT("Graph apply request should preserve createMissingNodes=false"), Request.Options.bCreateMissingNodes))
	{
		return false;
	}

	if (!TestEqual(TEXT("Graph apply request should preserve one update-only node"), Request.Nodes.Num(), 1))
	{
		return false;
	}

	TestTrue(TEXT("Graph apply request should preserve updateOnly=true"), Request.Nodes[0].bUpdateOnly);
	return TestTrue(TEXT("Graph apply request should preserve Rotation pin override"), Request.Nodes[0].Pins.Contains(TEXT("Rotation")));
}

bool FBATBlueprintGraphApplyRequestSupportsAutoArrangeExistingNodesTest::RunTest(const FString& Parameters)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("blueprint"), TEXT("/Game/Test/BP_Test"));
	Body->SetStringField(TEXT("graph"), TEXT("AnimGraph"));

	TSharedRef<FJsonObject> Options = MakeShared<FJsonObject>();
	Options->SetBoolField(TEXT("autoArrangeExistingNodes"), true);
	Body->SetObjectField(TEXT("options"), Options);

	TSharedRef<FJsonObject> Node = MakeShared<FJsonObject>();
	Node->SetStringField(TEXT("id"), TEXT("bat_pose_node"));
	Node->SetBoolField(TEXT("updateOnly"), true);

	TArray<TSharedPtr<FJsonValue>> Nodes;
	Nodes.Add(MakeShared<FJsonValueObject>(Node));
	Body->SetArrayField(TEXT("nodes"), Nodes);
	Body->SetArrayField(TEXT("links"), TArray<TSharedPtr<FJsonValue>>());

	FBlueprintGraphApplyRequest Request;
	TArray<FString> ParseErrors;
	const bool bParsed = BAT::BlueprintGraphApplyRequest::Parse(Body, Request, ParseErrors);
	if (!TestTrue(TEXT("Graph apply request should accept autoArrangeExistingNodes option"), bParsed))
	{
		for (const FString& ParseError : ParseErrors)
		{
			AddError(ParseError);
		}
		return false;
	}

	return TestTrue(TEXT("Graph apply request should preserve autoArrangeExistingNodes=true"), Request.Options.bAutoArrangeExistingNodes);
}

bool FBATBlueprintGraphApplyRequestSupportsAutoArrangeConnectedNodesTest::RunTest(const FString& Parameters)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("blueprint"), TEXT("/Game/Test/BP_Test"));
	Body->SetStringField(TEXT("graph"), TEXT("AnimGraph"));

	TSharedRef<FJsonObject> Options = MakeShared<FJsonObject>();
	Options->SetBoolField(TEXT("autoArrangeConnectedNodes"), true);
	Body->SetObjectField(TEXT("options"), Options);

	TSharedRef<FJsonObject> Node = MakeShared<FJsonObject>();
	Node->SetStringField(TEXT("id"), TEXT("bat_pose_node"));
	Node->SetBoolField(TEXT("updateOnly"), true);

	TArray<TSharedPtr<FJsonValue>> Nodes;
	Nodes.Add(MakeShared<FJsonValueObject>(Node));
	Body->SetArrayField(TEXT("nodes"), Nodes);
	Body->SetArrayField(TEXT("links"), TArray<TSharedPtr<FJsonValue>>());

	FBlueprintGraphApplyRequest Request;
	TArray<FString> ParseErrors;
	const bool bParsed = BAT::BlueprintGraphApplyRequest::Parse(Body, Request, ParseErrors);
	if (!TestTrue(TEXT("Graph apply request should accept autoArrangeConnectedNodes option"), bParsed))
	{
		for (const FString& ParseError : ParseErrors)
		{
			AddError(ParseError);
		}
		return false;
	}

	return TestTrue(TEXT("Graph apply request should preserve autoArrangeConnectedNodes=true"), Request.Options.bAutoArrangeConnectedNodes);
}

bool FBATBlueprintGraphApplyRequestSupportsPreserveFeederLanesTest::RunTest(const FString& Parameters)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("blueprint"), TEXT("/Game/Test/BP_Test"));
	Body->SetStringField(TEXT("graph"), TEXT("AnimGraph"));

	TSharedRef<FJsonObject> Options = MakeShared<FJsonObject>();
	Options->SetBoolField(TEXT("preserveFeederLanes"), true);
	Body->SetObjectField(TEXT("options"), Options);

	TSharedRef<FJsonObject> Node = MakeShared<FJsonObject>();
	Node->SetStringField(TEXT("id"), TEXT("bat_pose_node"));
	Node->SetBoolField(TEXT("updateOnly"), true);

	TArray<TSharedPtr<FJsonValue>> Nodes;
	Nodes.Add(MakeShared<FJsonValueObject>(Node));
	Body->SetArrayField(TEXT("nodes"), Nodes);
	Body->SetArrayField(TEXT("links"), TArray<TSharedPtr<FJsonValue>>());

	FBlueprintGraphApplyRequest Request;
	TArray<FString> ParseErrors;
	const bool bParsed = BAT::BlueprintGraphApplyRequest::Parse(Body, Request, ParseErrors);
	if (!TestTrue(TEXT("Graph apply request should accept preserveFeederLanes option"), bParsed))
	{
		for (const FString& ParseError : ParseErrors)
		{
			AddError(ParseError);
		}
		return false;
	}

	return TestTrue(TEXT("Graph apply request should preserve preserveFeederLanes=true"), Request.Options.bPreserveFeederLanes);
}

bool FBATPcgApplyRequestNormalizesConvenienceFieldsTest::RunTest(const FString& Parameters)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("graph"), TEXT("/Game/PCG/Graphs/BAT_CityGraph.BAT_CityGraph"));

	TSharedRef<FJsonObject> Parameter = MakeShared<FJsonObject>();
	Parameter->SetStringField(TEXT("name"), TEXT("StreetWidth"));
	Parameter->SetStringField(TEXT("type"), TEXT("float"));
	Parameter->SetNumberField(TEXT("default"), 1800.0);
	Body->SetArrayField(TEXT("parameters"), { MakeShared<FJsonValueObject>(Parameter) });

	TSharedRef<FJsonObject> Node = MakeShared<FJsonObject>();
	Node->SetStringField(TEXT("op"), TEXT("nodes.add"));
	Node->SetStringField(TEXT("id"), TEXT("structure_spawner"));
	Node->SetStringField(TEXT("type"), TEXT("StaticMeshSpawner"));

	TSharedRef<FJsonObject> MeshSet = MakeShared<FJsonObject>();
	MeshSet->SetStringField(TEXT("mode"), TEXT("weighted"));
	MeshSet->SetArrayField(TEXT("meshes"), {
		MakeShared<FJsonValueString>(TEXT("/Game/StaticMeshes/SM_CommonHouse2.SM_CommonHouse2")),
		MakeShared<FJsonValueString>(TEXT("/Game/StaticMeshes/SM_Hotel2.SM_Hotel2"))
	});
	Node->SetObjectField(TEXT("mesh_set"), MeshSet);

	Body->SetArrayField(TEXT("ops"), { MakeShared<FJsonValueObject>(Node) });

	FPcgApplyRequest Request;
	TArray<FString> ParseErrors;
	const bool bParsed = BAT::PcgApplyRequest::Parse(Body, Request, ParseErrors);
	if (!TestTrue(TEXT("PCG apply request should parse valid convenience fields"), bParsed))
	{
		for (const FString& ParseError : ParseErrors)
		{
			AddError(ParseError);
		}
		return false;
	}

	if (!TestEqual(TEXT("PCG apply request should preserve one parameter entry"), Request.Parameters.Num(), 1))
	{
		return false;
	}

	if (!TestEqual(TEXT("PCG apply request should normalize top-level parameters and inline mesh_set into canonical ops"), Request.Ops.Num(), 3))
	{
		return false;
	}

	TestEqual(TEXT("First canonical op should be parameters.set"), Request.Ops[0].Op, FString(TEXT("parameters.set")));
	TestEqual(TEXT("Second canonical op should be nodes.add"), Request.Ops[1].Op, FString(TEXT("nodes.add")));
	TestEqual(TEXT("Third canonical op should be spawners.set_mesh_set"), Request.Ops[2].Op, FString(TEXT("spawners.set_mesh_set")));
	return TestEqual(TEXT("Canonical mesh-set node should reference the authored node id"), Request.Ops[2].Node, FString(TEXT("structure_spawner")));
}

bool FBATPcgApplyRequestRejectsDuplicateNodeIdsTest::RunTest(const FString& Parameters)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("graph"), TEXT("/Game/PCG/Graphs/BAT_CityGraph.BAT_CityGraph"));

	TSharedRef<FJsonObject> NodeA = MakeShared<FJsonObject>();
	NodeA->SetStringField(TEXT("op"), TEXT("nodes.add"));
	NodeA->SetStringField(TEXT("id"), TEXT("duplicate_node"));
	NodeA->SetStringField(TEXT("type"), TEXT("StaticMeshSpawner"));

	TSharedRef<FJsonObject> NodeB = MakeShared<FJsonObject>();
	NodeB->SetStringField(TEXT("op"), TEXT("nodes.add"));
	NodeB->SetStringField(TEXT("id"), TEXT("duplicate_node"));
	NodeB->SetStringField(TEXT("type"), TEXT("StaticMeshSpawner"));

	Body->SetArrayField(TEXT("ops"), {
		MakeShared<FJsonValueObject>(NodeA),
		MakeShared<FJsonValueObject>(NodeB)
	});

	FPcgApplyRequest Request;
	TArray<FString> ParseErrors;
	const bool bParsed = BAT::PcgApplyRequest::Parse(Body, Request, ParseErrors);
	if (!TestFalse(TEXT("PCG apply request should reject duplicate node ids"), bParsed))
	{
		return false;
	}

	for (const FString& ParseError : ParseErrors)
	{
		if (ParseError.Contains(TEXT("duplicate_node_id"), ESearchCase::CaseSensitive))
		{
			return true;
		}
	}

	AddError(TEXT("Expected duplicate_node_id parse error"));
	return false;
}

bool FBATPcgNodeRegistryResolvesKnownFamiliesTest::RunTest(const FString& Parameters)
{
	const FPcgNodeFamilySpec* StaticMeshSpawner = FPcgNodeRegistry::FindByExternalType(TEXT("StaticMeshSpawner"));
	const FPcgNodeFamilySpec* SurfaceSampler = FPcgNodeRegistry::FindByExternalType(TEXT("SurfaceSampler"));
	const FPcgNodeFamilySpec* Difference = FPcgNodeRegistry::FindByExternalType(TEXT("Difference"));

	if (!TestNotNull(TEXT("Registry should resolve StaticMeshSpawner"), StaticMeshSpawner))
	{
		return false;
	}

	if (!TestNotNull(TEXT("Registry should resolve SurfaceSampler"), SurfaceSampler))
	{
		return false;
	}

	if (!TestNotNull(TEXT("Registry should resolve Difference"), Difference))
	{
		return false;
	}

	if (!TestTrue(TEXT("StaticMeshSpawner should support mesh-set assignment"), StaticMeshSpawner->bSupportsMeshSet))
	{
		return false;
	}

	if (!TestTrue(TEXT("StaticMeshSpawner should allow density setting"), StaticMeshSpawner->SupportedSettingKeys.Contains(TEXT("density"))))
	{
		return false;
	}

	if (!TestTrue(TEXT("SurfaceSampler should allow density setting"), SurfaceSampler->SupportedSettingKeys.Contains(TEXT("density"))))
	{
		return false;
	}

	return TestFalse(TEXT("Unknown family should not resolve"), FPcgNodeRegistry::FindByExternalType(TEXT("NotARealNodeFamily")) != nullptr);
}

bool FBATPcgApplyRequestRejectsUnsupportedNodeFamilyTest::RunTest(const FString& Parameters)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("graph"), TEXT("/Game/PCG/Graphs/BAT_CityGraph.BAT_CityGraph"));

	TSharedRef<FJsonObject> Node = MakeShared<FJsonObject>();
	Node->SetStringField(TEXT("op"), TEXT("nodes.add"));
	Node->SetStringField(TEXT("id"), TEXT("bad_node"));
	Node->SetStringField(TEXT("type"), TEXT("NotARealNodeFamily"));

	Body->SetArrayField(TEXT("ops"), { MakeShared<FJsonValueObject>(Node) });

	FPcgApplyRequest Request;
	TArray<FString> ParseErrors;
	const bool bParsed = BAT::PcgApplyRequest::Parse(Body, Request, ParseErrors);
	if (!TestFalse(TEXT("PCG apply request should reject unsupported node families"), bParsed))
	{
		return false;
	}

	for (const FString& ParseError : ParseErrors)
	{
		if (ParseError.Contains(TEXT("unsupported_node_type"), ESearchCase::CaseSensitive))
		{
			return true;
		}
	}

	AddError(TEXT("Expected unsupported_node_type parse error"));
	return false;
}

bool FBATPcgApplyCommandRejectsMissingGraphTest::RunTest(const FString& Parameters)
{
	const FString GraphPath = MakeUniquePcgTestGraphObjectPath();
	CleanupPcgTestGraphAsset(GraphPath);

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("graph"), GraphPath);
	Body->SetArrayField(TEXT("ops"), TArray<TSharedPtr<FJsonValue>>());

	FApplyPcgPlanCommand Command;
	FAutomationContext Context = MakePcgApplyContext(Body);
	const FAutomationResult Result = Command.Execute(Context);

	TestFalse(TEXT("PCG apply should fail when graph is missing and create_if_missing is false"), Result.bSuccess);
	return TestEqual(TEXT("Missing graph should return graph_not_found"), Result.ErrorCode, FString(TEXT("graph_not_found")));
}

bool FBATPcgApplyCommandCreatesAndSavesGraphTest::RunTest(const FString& Parameters)
{
	const FString GraphPath = MakeUniquePcgTestGraphObjectPath();
	CleanupPcgTestGraphAsset(GraphPath);

	TSharedRef<FJsonObject> Options = MakeShared<FJsonObject>();
	Options->SetBoolField(TEXT("create_if_missing"), true);
	Options->SetBoolField(TEXT("save"), true);

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("graph"), GraphPath);
	Body->SetObjectField(TEXT("options"), Options);
	Body->SetArrayField(TEXT("ops"), TArray<TSharedPtr<FJsonValue>>());

	FApplyPcgPlanCommand Command;
	FAutomationContext Context = MakePcgApplyContext(Body);
	const FAutomationResult Result = Command.Execute(Context);

	if (!TestTrue(TEXT("PCG apply should create and save a graph when requested"), Result.bSuccess))
	{
		CleanupPcgTestGraphAsset(GraphPath);
		return false;
	}

	const FString Filename = GetPackageFilenameFromObjectPath(GraphPath);
	const bool bFileExists = IFileManager::Get().FileExists(*Filename);
	const bool bObjectLoads = LoadObject<UObject>(nullptr, *GraphPath) != nullptr;

	CleanupPcgTestGraphAsset(GraphPath);

	if (!TestTrue(TEXT("Saved PCG graph package file should exist"), bFileExists))
	{
		return false;
	}

	return TestTrue(TEXT("Saved PCG graph asset should load by object path"), bObjectLoads);
}

bool FBATBlueprintGraphLayoutPreservesImplicitNodePositionTest::RunTest(const FString& Parameters)
{
	UEdGraphNode* Node = NewObject<UEdGraphNode>(GetTransientPackage(), NAME_None, RF_Transient);
	Node->NodePosX = 448;
	Node->NodePosY = 224;

	FBlueprintGraphApplyNodeSpec NodeSpec;
	NodeSpec.X = 0;
	NodeSpec.Y = 0;
	NodeSpec.bHasExplicitX = false;
	NodeSpec.bHasExplicitY = false;

	FBlueprintGraphLayoutService::ApplyNodeLayout(Node, NodeSpec);

	TestEqual(TEXT("Implicit x should preserve existing node x position"), Node->NodePosX, 448);
	return TestEqual(TEXT("Implicit y should preserve existing node y position"), Node->NodePosY, 224);
}

bool FBATBlueprintGraphLayoutAutoArrangesCreatedNodeTest::RunTest(const FString& Parameters)
{
	UEdGraph* Graph = NewObject<UEdGraph>(GetTransientPackage(), NAME_None, RF_Transient);
	if (!TestNotNull(TEXT("Transient graph should be created"), Graph))
	{
		return false;
	}

	UEdGraphNode* TargetNode = NewObject<UEdGraphNode>(Graph, NAME_None, RF_Transient);
	Graph->AddNode(TargetNode, false, false);
	TargetNode->NodePosX = 960;
	TargetNode->NodePosY = 384;
	FBlueprintGraphNodeService::SetNodeUasId(TargetNode, TEXT("target_node"));
	UEdGraphPin* TargetInputPin = TargetNode->CreatePin(EGPD_Input, TEXT("struct"), FName(TEXT("ComponentPose")));

	UEdGraphNode* CreatedNode = NewObject<UEdGraphNode>(Graph, NAME_None, RF_Transient);
	Graph->AddNode(CreatedNode, false, false);
	CreatedNode->NodePosX = 0;
	CreatedNode->NodePosY = 0;
	FBlueprintGraphNodeService::SetNodeUasId(CreatedNode, TEXT("created_node"));
	UEdGraphPin* CreatedOutputPin = CreatedNode->CreatePin(EGPD_Output, TEXT("struct"), FName(TEXT("Pose")));

	if (!TestNotNull(TEXT("Created node output pin should exist"), CreatedOutputPin) || !TestNotNull(TEXT("Target node input pin should exist"), TargetInputPin))
	{
		return false;
	}
	CreatedOutputPin->MakeLinkTo(TargetInputPin);

	FBlueprintGraphApplyNodeSpec CreatedNodeSpec;
	CreatedNodeSpec.Id = TEXT("created_node");

	TMap<FString, UEdGraphNode*> NodeById;
	NodeById.Add(TEXT("created_node"), CreatedNode);
	NodeById.Add(TEXT("target_node"), TargetNode);

	TSet<FString> CreatedNodeIds;
	CreatedNodeIds.Add(TEXT("created_node"));

	FBlueprintGraphLayoutService::AutoArrangeNodes(Graph, NodeById, CreatedNodeIds);

	TestEqual(TEXT("Auto-arranged node should be placed one lane before its target"), CreatedNode->NodePosX, 640);
	return TestEqual(TEXT("Auto-arranged node should align vertically with its target"), CreatedNode->NodePosY, 384);
}

bool FBATBlueprintGraphLayoutCentersFanInNodeTest::RunTest(const FString& Parameters)
{
	UEdGraph* Graph = NewObject<UEdGraph>(GetTransientPackage(), NAME_None, RF_Transient);
	if (!TestNotNull(TEXT("Transient graph should be created for fan-in test"), Graph))
	{
		return false;
	}

	UEdGraphNode* SourceTop = NewObject<UEdGraphNode>(Graph, NAME_None, RF_Transient);
	Graph->AddNode(SourceTop, false, false);
	SourceTop->NodePosX = 0;
	SourceTop->NodePosY = 0;
	FBlueprintGraphNodeService::SetNodeUasId(SourceTop, TEXT("source_top"));
	UEdGraphPin* SourceTopOut = SourceTop->CreatePin(EGPD_Output, TEXT("struct"), FName(TEXT("Pose")));

	UEdGraphNode* SourceBottom = NewObject<UEdGraphNode>(Graph, NAME_None, RF_Transient);
	Graph->AddNode(SourceBottom, false, false);
	SourceBottom->NodePosX = 0;
	SourceBottom->NodePosY = 512;
	FBlueprintGraphNodeService::SetNodeUasId(SourceBottom, TEXT("source_bottom"));
	UEdGraphPin* SourceBottomOut = SourceBottom->CreatePin(EGPD_Output, TEXT("struct"), FName(TEXT("Pose")));

	UEdGraphNode* BlendNode = NewObject<UEdGraphNode>(Graph, NAME_None, RF_Transient);
	Graph->AddNode(BlendNode, false, false);
	BlendNode->NodePosX = 0;
	BlendNode->NodePosY = 0;
	FBlueprintGraphNodeService::SetNodeUasId(BlendNode, TEXT("blend_node"));
	UEdGraphPin* BlendInA = BlendNode->CreatePin(EGPD_Input, TEXT("struct"), FName(TEXT("BasePose")));
	UEdGraphPin* BlendInB = BlendNode->CreatePin(EGPD_Input, TEXT("struct"), FName(TEXT("BlendPoses_0")));

	if (!TestNotNull(TEXT("Top source output pin should exist"), SourceTopOut)
		|| !TestNotNull(TEXT("Bottom source output pin should exist"), SourceBottomOut)
		|| !TestNotNull(TEXT("Blend base pin should exist"), BlendInA)
		|| !TestNotNull(TEXT("Blend overlay pin should exist"), BlendInB))
	{
		return false;
	}

	SourceTopOut->MakeLinkTo(BlendInA);
	SourceBottomOut->MakeLinkTo(BlendInB);

	TMap<FString, UEdGraphNode*> NodeById;
	NodeById.Add(TEXT("source_top"), SourceTop);
	NodeById.Add(TEXT("source_bottom"), SourceBottom);
	NodeById.Add(TEXT("blend_node"), BlendNode);

	TSet<FString> NodeIdsToArrange;
	NodeIdsToArrange.Add(TEXT("blend_node"));

	FBlueprintGraphLayoutService::AutoArrangeNodes(Graph, NodeById, NodeIdsToArrange);

	TestEqual(TEXT("Fan-in node should be placed one column after its sources"), BlendNode->NodePosX, 320);
	return TestEqual(TEXT("Fan-in node should be vertically centered between its sources"), BlendNode->NodePosY, 256);
}

bool FBATBlueprintGraphLayoutPreservesFeederLanesTest::RunTest(const FString& Parameters)
{
	UEdGraph* Graph = NewObject<UEdGraph>(GetTransientPackage(), NAME_None, RF_Transient);
	if (!TestNotNull(TEXT("Transient graph should be created for feeder lane test"), Graph))
	{
		return false;
	}

	UEdGraphNode* SourceTop = NewObject<UEdGraphNode>(Graph, NAME_None, RF_Transient);
	Graph->AddNode(SourceTop, false, false);
	SourceTop->NodePosX = 0;
	SourceTop->NodePosY = 0;
	FBlueprintGraphNodeService::SetNodeUasId(SourceTop, TEXT("source_top"));
	UEdGraphPin* SourceTopOut = SourceTop->CreatePin(EGPD_Output, TEXT("struct"), FName(TEXT("Pose")));

	UEdGraphNode* SourceBottom = NewObject<UEdGraphNode>(Graph, NAME_None, RF_Transient);
	Graph->AddNode(SourceBottom, false, false);
	SourceBottom->NodePosX = 0;
	SourceBottom->NodePosY = 512;
	FBlueprintGraphNodeService::SetNodeUasId(SourceBottom, TEXT("source_bottom"));
	UEdGraphPin* SourceBottomOut = SourceBottom->CreatePin(EGPD_Output, TEXT("struct"), FName(TEXT("Pose")));

	UEdGraphNode* TopLaneNode = NewObject<UEdGraphNode>(Graph, NAME_None, RF_Transient);
	Graph->AddNode(TopLaneNode, false, false);
	TopLaneNode->NodePosX = 0;
	TopLaneNode->NodePosY = 0;
	FBlueprintGraphNodeService::SetNodeUasId(TopLaneNode, TEXT("top_lane"));
	UEdGraphPin* TopLaneIn = TopLaneNode->CreatePin(EGPD_Input, TEXT("struct"), FName(TEXT("BasePose")));

	UEdGraphNode* BlendNode = NewObject<UEdGraphNode>(Graph, NAME_None, RF_Transient);
	Graph->AddNode(BlendNode, false, false);
	BlendNode->NodePosX = 0;
	BlendNode->NodePosY = 0;
	FBlueprintGraphNodeService::SetNodeUasId(BlendNode, TEXT("blend_node"));
	UEdGraphPin* BlendInA = BlendNode->CreatePin(EGPD_Input, TEXT("struct"), FName(TEXT("BasePose")));
	UEdGraphPin* BlendInB = BlendNode->CreatePin(EGPD_Input, TEXT("struct"), FName(TEXT("BlendPoses_0")));

	UEdGraphNode* BottomLaneNode = NewObject<UEdGraphNode>(Graph, NAME_None, RF_Transient);
	Graph->AddNode(BottomLaneNode, false, false);
	BottomLaneNode->NodePosX = 0;
	BottomLaneNode->NodePosY = 0;
	FBlueprintGraphNodeService::SetNodeUasId(BottomLaneNode, TEXT("bottom_lane"));
	UEdGraphPin* BottomLaneIn = BottomLaneNode->CreatePin(EGPD_Input, TEXT("struct"), FName(TEXT("BasePose")));

	if (!TestNotNull(TEXT("Top source output pin should exist for feeder lane test"), SourceTopOut)
		|| !TestNotNull(TEXT("Bottom source output pin should exist for feeder lane test"), SourceBottomOut)
		|| !TestNotNull(TEXT("Top lane input pin should exist"), TopLaneIn)
		|| !TestNotNull(TEXT("Blend base pin should exist for feeder lane test"), BlendInA)
		|| !TestNotNull(TEXT("Blend overlay pin should exist for feeder lane test"), BlendInB)
		|| !TestNotNull(TEXT("Bottom lane input pin should exist"), BottomLaneIn))
	{
		return false;
	}

	SourceTopOut->MakeLinkTo(TopLaneIn);
	SourceTopOut->MakeLinkTo(BlendInA);
	SourceBottomOut->MakeLinkTo(BlendInB);
	SourceBottomOut->MakeLinkTo(BottomLaneIn);

	TMap<FString, UEdGraphNode*> NodeById;
	NodeById.Add(TEXT("source_top"), SourceTop);
	NodeById.Add(TEXT("source_bottom"), SourceBottom);
	NodeById.Add(TEXT("top_lane"), TopLaneNode);
	NodeById.Add(TEXT("blend_node"), BlendNode);
	NodeById.Add(TEXT("bottom_lane"), BottomLaneNode);

	TSet<FString> NodeIdsToArrange;
	NodeIdsToArrange.Add(TEXT("top_lane"));
	NodeIdsToArrange.Add(TEXT("blend_node"));
	NodeIdsToArrange.Add(TEXT("bottom_lane"));

	FBlueprintGraphLayoutService::AutoArrangeNodes(Graph, NodeById, NodeIdsToArrange, true);

	TestEqual(TEXT("Top feeder lane should stay aligned to its source"), TopLaneNode->NodePosY, 0);
	TestEqual(TEXT("Fan-in node should stay centered between feeder lanes"), BlendNode->NodePosY, 256);
	return TestEqual(TEXT("Bottom feeder lane should stay aligned to its source"), BottomLaneNode->NodePosY, 512);
}

bool FBATBlueprintGraphApplyResultIncludesNodeValidationTest::RunTest(const FString& Parameters)
{
	FBlueprintGraphApplyResult Result;
	Result.bOk = true;
	Result.CompileStatus = TEXT("compiled");
	Result.SaveStatus = TEXT("saved");
	FBlueprintGraphNodeValidationIssue& Issue = Result.NodeValidationIssues.AddDefaulted_GetRef();
	Issue.NodeId = TEXT("bat_pose_node");
	Issue.Code = TEXT("modify_bone_missing_bone");
	Issue.Message = TEXT("Transform Modify Bone requires BoneToModify.BoneName.");
	Issue.PropertyPath = TEXT("Node.BoneToModify.BoneName");

	const TSharedPtr<FJsonValue> RootValue = FBlueprintGraphValidationService::MakeApplyResultData(TEXT("/Game/Test/BP_Test.BP_Test"), TEXT("AnimGraph"), Result);
	const TSharedPtr<FJsonObject> Root = RootValue.IsValid() ? RootValue->AsObject() : nullptr;
	if (!TestNotNull(TEXT("Apply result data should be an object"), Root.Get()))
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* Data = nullptr;
	if (!TestTrue(TEXT("Apply result data should include data object"), Root->TryGetObjectField(TEXT("data"), Data) && Data && Data->IsValid()))
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* NodeValidation = nullptr;
	if (!TestTrue(TEXT("Apply result data should include nodeValidation array"), (*Data)->TryGetArrayField(TEXT("nodeValidation"), NodeValidation) && NodeValidation))
	{
		return false;
	}

	if (!TestEqual(TEXT("Apply result data should include one node validation issue"), NodeValidation->Num(), 1))
	{
		return false;
	}

	const TSharedPtr<FJsonObject> IssueObj = (*NodeValidation)[0].IsValid() ? (*NodeValidation)[0]->AsObject() : nullptr;
	if (!TestNotNull(TEXT("Node validation issue should be structured"), IssueObj.Get()))
	{
		return false;
	}

	FString NodeId;
	FString Code;
	TestTrue(TEXT("Node validation issue includes nodeId"), IssueObj->TryGetStringField(TEXT("nodeId"), NodeId));
	TestTrue(TEXT("Node validation issue includes code"), IssueObj->TryGetStringField(TEXT("code"), Code));
	TestEqual(TEXT("Node validation issue preserves nodeId"), NodeId, FString(TEXT("bat_pose_node")));
	return TestEqual(TEXT("Node validation issue preserves code"), Code, FString(TEXT("modify_bone_missing_bone")));
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

bool FBATReflectionSetPropertyCommandTest::RunTest(const FString& Parameters)
{
	FBlueprintAutomationToolkitModule Module;
	Module.Test_SetReflectionSafeMode(true);
	Module.Test_SetReflectionClassAllowList({ TEXT("BATReflectionTestObject") });
	TStrongObjectPtr<UBATReflectionTestObject> Object(CreateReflectionTestObject());

	FAutomationContext Context;
	Context.RequestId = TEXT("set-property-command-test");
	Context.Endpoint = TEXT("/object/set_property");
	Context.Module = &Module;
	Context.Body = MakeObjectRequest(Object.Get());
	Context.Body->SetStringField(TEXT("property"), TEXT("bCanAttack"));
	Context.Body->SetBoolField(TEXT("value"), false);

	FSetPropertyCommand Command;
	const FAutomationResult Result = Command.Execute(Context);
	DestroyReflectionTestObject(Object.Get());

	if (!TestTrue(TEXT("SetProperty command succeeds"), Result.bSuccess))
	{
		return false;
	}

	const TSharedPtr<FJsonObject> Data = GetStructuredData(Result);
	const TSharedPtr<FJsonObject>* UpdatedProperties = nullptr;
	return TestTrue(TEXT("SetProperty command updates object and returns updatedProperties"), !Object->bCanAttack && Data.IsValid() && Data->TryGetObjectField(TEXT("updatedProperties"), UpdatedProperties) && UpdatedProperties && (*UpdatedProperties)->HasField(TEXT("bCanAttack")));
}

bool FBATReflectionCallFunctionCommandTest::RunTest(const FString& Parameters)
{
	FBlueprintAutomationToolkitModule Module;
	Module.Test_SetReflectionSafeMode(true);
	Module.Test_SetReflectionClassAllowList({ TEXT("BATReflectionTestObject") });
	TStrongObjectPtr<UBATReflectionTestObject> Object(CreateReflectionTestObject());

	FAutomationContext Context;
	Context.RequestId = TEXT("call-function-command-test");
	Context.Endpoint = TEXT("/object/call_function");
	Context.Module = &Module;
	Context.Body = MakeObjectRequest(Object.Get());
	Context.Body->SetStringField(TEXT("function"), TEXT("SetSpawnOffset"));
	TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> Offset = MakeShared<FJsonObject>();
	Offset->SetNumberField(TEXT("x"), 1.0);
	Offset->SetNumberField(TEXT("y"), 2.0);
	Offset->SetNumberField(TEXT("z"), 3.0);
	Arguments->SetObjectField(TEXT("NewOffset"), Offset);
	Context.Body->SetObjectField(TEXT("arguments"), Arguments);

	FCallFunctionCommand Command;
	const FAutomationResult Result = Command.Execute(Context);
	DestroyReflectionTestObject(Object.Get());

	if (!TestTrue(TEXT("CallFunction command succeeds"), Result.bSuccess))
	{
		return false;
	}

	const TSharedPtr<FJsonObject> Data = GetStructuredData(Result);
	const TSharedPtr<FJsonObject>* OutParameters = nullptr;
	return TestTrue(TEXT("CallFunction command updates object and returns outParameters object"), Object->SpawnOffset.Equals(FVector(1.0f, 2.0f, 3.0f)) && Data.IsValid() && Data->TryGetObjectField(TEXT("outParameters"), OutParameters) && OutParameters);
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

bool FBATControlPanelSafeModeTogglePersistsTest::RunTest(const FString& Parameters)
{
	FBlueprintAutomationToolkitModule Module;
	Module.Test_SetReflectionSafeMode(false);
	Module.Test_NotifySettingChanged();

	return TestTrue(TEXT("NotifySettingChanged should preserve the live Safe Mode toggle state"), !Module.IsSafeModeEnabled());
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

bool FBATResponseExportWritesFileTest::RunTest(const FString& Parameters)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("responseOutputPath"), TEXT("tests/response-export-check"));

	TUniquePtr<FHttpServerResponse> Response = BAT::Http::MakeJsonOk(MakeShared<FJsonValueString>(TEXT("ok")), 200, TEXT("response-export-test"));
	FString OutputPath;
	FString OutputError;
	if (!TestTrue(TEXT("Response export succeeds for safe relative path"), BAT::Http::TryWriteResponseToDisk(Body, TEXT("response-export-test"), *Response, OutputPath, OutputError)))
	{
		AddError(OutputError);
		return false;
	}

	FString Contents;
	const bool bLoaded = FFileHelper::LoadFileToString(Contents, *OutputPath);
	if (!TestTrue(TEXT("Exported response file exists"), bLoaded))
	{
		return false;
	}

	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Contents);
	if (!TestTrue(TEXT("Exported response parses as JSON"), FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid()))
	{
		return false;
	}

	bool bOk = false;
	FString DataValue;
	TestTrue(TEXT("Exported response includes ok=true"), Root->TryGetBoolField(TEXT("ok"), bOk) && bOk);
	TestTrue(TEXT("Exported response includes string payload"), Root->TryGetStringField(TEXT("data"), DataValue) && DataValue == TEXT("ok"));

	IFileManager::Get().Delete(*OutputPath, false, true, true);
	return true;
}

#endif

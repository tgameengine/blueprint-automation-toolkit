// Copyright 2026 AkaSoft. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "BlueprintAutomationToolkitModule.h"

#include "Commands/Reflection/CallFunctionCommand.h"
#include "Commands/Reflection/SetPropertyCommand.h"
#include "Commands/Material/SetMaterialTextureSamplesCommand.h"
#include "Commands/AutomationCommand.h"
#include "Core/ForwardAxis.h"
#include "Domain/Requests/AssetSaveRequest.h"
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Routes/Blueprint/BlueprintGraphApplyRequest.h"
#include "Services/BlueprintGraph/BlueprintGraphLayoutService.h"
#include "Services/BlueprintGraph/BlueprintGraphNodeService.h"
#include "Services/BlueprintGraphService.h"
#include "Services/BlueprintGraph/BlueprintGraphValidationService.h"
#include "Http/HttpRequestUtils.h"
#include "Services/AssetService.h"
#include "Services/AssetPipelineService.h"
#include "Transport/RequestParsing.h"
#include "Services/Reflection/ReflectionFunctionService.h"
#include "Services/Reflection/ReflectionObjectResolver.h"
#include "Services/Reflection/ReflectionPropertyService.h"
#include "Tests/BATReflectionTestTypes.h"

#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include "Engine/EngineTypes.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "HAL/FileManager.h"
#include "ObjectTools.h"
#include "UObject/UObjectGlobals.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATOpenApiSpecExistsTest, "BlueprintAutomationToolkit.OpenApi.SpecExists", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATOpenApiHasJobsAndLogsTest, "BlueprintAutomationToolkit.OpenApi.HasJobsAndLogsPaths", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATOpenApiHasEngineDiscoverPathTest, "BlueprintAutomationToolkit.OpenApi.HasEngineDiscoverPath", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATOpenApiHasBlueprintPlanPathsTest, "BlueprintAutomationToolkit.OpenApi.HasBlueprintPlanPaths", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATOpenApiHasBlueprintSchemaPathTest, "BlueprintAutomationToolkit.OpenApi.HasBlueprintSchemaPath", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATExecPythonRequiresPythonPermissionTest, "BlueprintAutomationToolkit.Security.ExecPythonRequiresPythonPermission", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATPermissionMapCoversPieAliasesTest, "BlueprintAutomationToolkit.Security.PermissionMapCoversPieAliases", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATAssetPipelineOpenApiPathsTest, "BlueprintAutomationToolkit.AssetPipeline.OpenApiPaths", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATAssetPipelinePermissionMapTest, "BlueprintAutomationToolkit.AssetPipeline.PermissionMap", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATAssetPipelineRejectsInvalidDestinationTest, "BlueprintAutomationToolkit.AssetPipeline.RejectsInvalidDestination", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATAssetPipelineRejectsMissingStepsTest, "BlueprintAutomationToolkit.AssetPipeline.RejectsMissingSteps", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATAssetPipelineDescribesFormatsTest, "BlueprintAutomationToolkit.AssetPipeline.DescribesFormats", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATAssetPipelineTextureRoundTripTest, "BlueprintAutomationToolkit.AssetPipeline.TextureRoundTrip", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATAssetPipelineRejectsEscapingGltfSidecarTest, "BlueprintAutomationToolkit.AssetPipeline.RejectsEscapingGltfSidecar", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATAssetPipelineAnimatedModelRoundTripTest, "BlueprintAutomationToolkit.AssetPipeline.AnimatedModelRoundTrip", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATAssetPipelineValidationStopsPipelineTest, "BlueprintAutomationToolkit.AssetPipeline.ValidationStopsPipeline", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATAssetPipelineDryRunSkipsDependentMutationsTest, "BlueprintAutomationToolkit.AssetPipeline.DryRunSkipsDependentMutations", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATAssetPipelineBatchFailureRollsBackNewAssetsTest, "BlueprintAutomationToolkit.AssetPipeline.BatchFailureRollsBackNewAssets", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATMaterialTextureSamplesCommandRejectsMissingMaterialTest, "BlueprintAutomationToolkit.Materials.TextureSamplesRejectMissingMaterial", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATForwardAxisAliasesNormalizeTest, "BlueprintAutomationToolkit.Geometry.ForwardAxisAliasesNormalize", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATBlueprintGraphApplyRequestAcceptsSignedForwardAxisTest, "BlueprintAutomationToolkit.Blueprint.GraphApplyRequestAcceptsSignedForwardAxis", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATBlueprintGraphApplyRequestAcceptsActorOverlapEventTest, "BlueprintAutomationToolkit.Blueprint.GraphApplyRequestAcceptsActorOverlapEvent", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATBlueprintGraphApplyRequestAcceptsComponentBoundEventTest, "BlueprintAutomationToolkit.Blueprint.GraphApplyRequestAcceptsComponentBoundEvent", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATBlueprintGraphApplyRequestSupportsUpdateOnlyNodeTest, "BlueprintAutomationToolkit.Blueprint.GraphApplyRequestSupportsUpdateOnlyNode", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATBlueprintGraphApplyRequestSupportsAutoArrangeExistingNodesTest, "BlueprintAutomationToolkit.Blueprint.GraphApplyRequestSupportsAutoArrangeExistingNodes", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATBlueprintGraphApplyRequestSupportsAutoArrangeConnectedNodesTest, "BlueprintAutomationToolkit.Blueprint.GraphApplyRequestSupportsAutoArrangeConnectedNodes", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBATBlueprintGraphApplyRequestSupportsPreserveFeederLanesTest, "BlueprintAutomationToolkit.Blueprint.GraphApplyRequestSupportsPreserveFeederLanes", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
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
	TestTrue(TEXT("OpenAPI contains /material/texture_samples/set"), Spec.Contains(TEXT("/material/texture_samples/set:"), ESearchCase::CaseSensitive));
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
	TestTrue(TEXT("/material/texture_samples/set is blocked during PIE"), Module.Test_IsEditorAssetMutationBlockedDuringPie(TEXT("/material/texture_samples/set")));

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

bool FBATMaterialTextureSamplesCommandRejectsMissingMaterialTest::RunTest(const FString& Parameters)
{
	FAutomationContext Context;
	Context.RequestId = TEXT("material-texture-samples-missing-material");
	Context.Endpoint = TEXT("/material/texture_samples/set");
	Context.Body = MakeShared<FJsonObject>();

	FSetMaterialTextureSamplesCommand Command;
	const FAutomationResult Result = Command.Execute(Context);
	TestFalse(TEXT("Material texture command rejects a missing material path"), Result.bSuccess);
	TestEqual(TEXT("Material texture command reports missing_material"), Result.ErrorCode, FString(TEXT("missing_material")));
	TestEqual(TEXT("Material texture command uses HTTP 400 for a missing material path"), Result.StatusCode, 400);
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

	const bool bMissingObject = !MissingObjectResult.bSuccess && MissingObjectResult.ErrorCode == TEXT("ObjectNotFound");
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

bool FBATAssetPipelineOpenApiPathsTest::RunTest(const FString& Parameters)
{
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("BlueprintAutomationToolkit"));
	if (!TestTrue(TEXT("Blueprint Automation Toolkit plugin is discoverable"), Plugin.IsValid()))
	{
		return false;
	}

	FString Spec;
	const FString OpenApiPath = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Docs"), TEXT("openapi.yaml"));
	if (!TestTrue(TEXT("Asset pipeline OpenAPI document is readable"), FFileHelper::LoadFileToString(Spec, *OpenApiPath)))
	{
		return false;
	}

	TestTrue(TEXT("OpenAPI contains /asset/import/formats"), Spec.Contains(TEXT("/asset/import/formats:"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("OpenAPI contains /asset/import"), Spec.Contains(TEXT("/asset/import:"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("OpenAPI contains /asset/inspect"), Spec.Contains(TEXT("/asset/inspect:"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("OpenAPI contains /asset/configure"), Spec.Contains(TEXT("/asset/configure:"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("OpenAPI contains /asset/validate"), Spec.Contains(TEXT("/asset/validate:"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("OpenAPI contains /asset/pipeline/execute"), Spec.Contains(TEXT("/asset/pipeline/execute:"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("OpenAPI contains /asset/showcase/capture"), Spec.Contains(TEXT("/asset/showcase/capture:"), ESearchCase::CaseSensitive));
	return true;
}

bool FBATAssetPipelinePermissionMapTest::RunTest(const FString& Parameters)
{
	FBlueprintAutomationToolkitModule Module;
	const uint32 EditorMask = static_cast<uint32>(FBlueprintAutomationToolkitModule::EAutomationTestPermission::Editor);
	const uint32 FilesystemMask = static_cast<uint32>(FBlueprintAutomationToolkitModule::EAutomationTestPermission::Filesystem);

	TestEqual(TEXT("Import requires editor and filesystem"), Module.Test_GetRouteRequiredPermissions(TEXT("/asset/import")), EditorMask | FilesystemMask);
	TestEqual(TEXT("Configure requires editor and filesystem"), Module.Test_GetRouteRequiredPermissions(TEXT("/asset/configure")), EditorMask | FilesystemMask);
	TestEqual(TEXT("Pipeline requires editor and filesystem"), Module.Test_GetRouteRequiredPermissions(TEXT("/asset/pipeline/execute")), EditorMask | FilesystemMask);
	TestEqual(TEXT("Showcase capture requires editor and filesystem"), Module.Test_GetRouteRequiredPermissions(TEXT("/asset/showcase/capture")), EditorMask | FilesystemMask);
	TestEqual(TEXT("Format discovery is read-only editor access"), Module.Test_GetRouteRequiredPermissions(TEXT("/asset/import/formats")), EditorMask);
	TestEqual(TEXT("Inspect is read-only editor access"), Module.Test_GetRouteRequiredPermissions(TEXT("/asset/inspect")), EditorMask);
	TestEqual(TEXT("Validate is read-only editor access"), Module.Test_GetRouteRequiredPermissions(TEXT("/asset/validate")), EditorMask);

	TSharedRef<FJsonObject> JobBody = MakeShared<FJsonObject>();
	JobBody->SetStringField(TEXT("kind"), TEXT("asset.pipeline"));
	JobBody->SetObjectField(TEXT("payload"), MakeShared<FJsonObject>());
	TestEqual(TEXT("Asset pipeline jobs add filesystem permission"), Module.Test_GetRequestRequiredPermissions(TEXT("/jobs/submit"), JobBody), EditorMask | FilesystemMask);

	TestTrue(TEXT("Import is blocked during PIE"), Module.Test_IsEditorAssetMutationBlockedDuringPie(TEXT("/asset/import")));
	TestTrue(TEXT("Configure is blocked during PIE"), Module.Test_IsEditorAssetMutationBlockedDuringPie(TEXT("/asset/configure")));
	TestTrue(TEXT("Pipeline is blocked during PIE"), Module.Test_IsEditorAssetMutationBlockedDuringPie(TEXT("/asset/pipeline/execute")));
	TestTrue(TEXT("Showcase capture is blocked during PIE"), Module.Test_IsEditorAssetMutationBlockedDuringPie(TEXT("/asset/showcase/capture")));
	TestFalse(TEXT("Inspect remains available during PIE"), Module.Test_IsEditorAssetMutationBlockedDuringPie(TEXT("/asset/inspect")));
	TestFalse(TEXT("Validate remains available during PIE"), Module.Test_IsEditorAssetMutationBlockedDuringPie(TEXT("/asset/validate")));
	return true;
}

bool FBATAssetPipelineRejectsInvalidDestinationTest::RunTest(const FString& Parameters)
{
	FBlueprintAutomationToolkitModule Module;
	const FAssetPipelineService Service;
	TSharedRef<FJsonObject> Request = MakeShared<FJsonObject>();
	Request->SetStringField(TEXT("source"), TEXT("SourceArt/placeholder.fbx"));
	Request->SetStringField(TEXT("destination"), TEXT("/Engine/BATDenied"));

	const FAutomationResult Result = Service.ImportAssets(Module, Request);
	TestFalse(TEXT("Invalid destination is rejected"), Result.bSuccess);
	TestEqual(TEXT("Invalid destination returns bad_destination"), Result.ErrorCode, FString(TEXT("bad_destination")));
	return true;
}

bool FBATAssetPipelineRejectsMissingStepsTest::RunTest(const FString& Parameters)
{
	FBlueprintAutomationToolkitModule Module;
	const FAssetPipelineService Service;
	const FAutomationResult Result = Service.ExecutePipeline(Module, MakeShared<FJsonObject>());
	TestFalse(TEXT("Pipeline without steps is rejected"), Result.bSuccess);
	TestEqual(TEXT("Missing steps returns bad_args"), Result.ErrorCode, FString(TEXT("bad_args")));
	return true;
}

bool FBATAssetPipelineDescribesFormatsTest::RunTest(const FString& Parameters)
{
	FBlueprintAutomationToolkitModule Module;
	const FAssetPipelineService Service;
	const FAutomationResult Result = Service.DescribeImportFormats(Module);
	if (!TestTrue(TEXT("Format discovery succeeds"), Result.bSuccess))
	{
		return false;
	}
	const TSharedPtr<FJsonObject> Root = GetStructuredRoot(Result);
	if (!TestTrue(TEXT("Format discovery returns an object"), Root.IsValid()))
	{
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>* Extensions = nullptr;
	TestTrue(TEXT("Format discovery includes allowed extensions"), Root->TryGetArrayField(TEXT("allowedExtensions"), Extensions) && Extensions && Extensions->Num() > 0);
	bool bDryRun = false;
	TestTrue(TEXT("Format discovery advertises dry-run support"), Root->TryGetBoolField(TEXT("dryRun"), bDryRun) && bDryRun);
	return true;
}

bool FBATAssetPipelineTextureRoundTripTest::RunTest(const FString& Parameters)
{
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("BlueprintAutomationToolkit"));
	if (!TestTrue(TEXT("Plugin exists for the import round trip"), Plugin.IsValid()))
	{
		return false;
	}

	const FString Source = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources"), TEXT("Icon128.png"));
	if (!TestTrue(TEXT("Round-trip source texture exists"), IFileManager::Get().FileExists(*Source)))
	{
		return false;
	}

	FBlueprintAutomationToolkitModule Module;
	const FAssetPipelineService Service;
	TSharedRef<FJsonObject> ImportRequest = MakeShared<FJsonObject>();
	ImportRequest->SetStringField(TEXT("source"), Source);
	ImportRequest->SetStringField(TEXT("destination"), TEXT("/Game/__BAT_Automation/AssetPipeline"));
	ImportRequest->SetStringField(TEXT("destination_name"), TEXT("T_BATAssetPipelineRoundTrip"));
	ImportRequest->SetBoolField(TEXT("replace_existing"), true);
	ImportRequest->SetBoolField(TEXT("skip_unchanged"), false);
	ImportRequest->SetBoolField(TEXT("save"), false);

	const FAutomationResult ImportResult = Service.ImportAssets(Module, ImportRequest);
	if (!TestTrue(TEXT("Texture import succeeds"), ImportResult.bSuccess))
	{
		AddError(ImportResult.ErrorMessage);
		return false;
	}
	const TSharedPtr<FJsonObject> ImportRoot = GetStructuredRoot(ImportResult);
	const TArray<TSharedPtr<FJsonValue>>* ImportedPaths = nullptr;
	if (!TestTrue(TEXT("Import returns an object path"), ImportRoot.IsValid()
		&& ImportRoot->TryGetArrayField(TEXT("importedObjectPaths"), ImportedPaths)
		&& ImportedPaths
		&& ImportedPaths->Num() > 0))
	{
		return false;
	}

	const FString ImportedPath = (*ImportedPaths)[0]->AsString();
	TSharedRef<FJsonObject> InspectRequest = MakeShared<FJsonObject>();
	InspectRequest->SetStringField(TEXT("path"), ImportedPath);
	TestTrue(TEXT("Imported texture can be inspected"), Service.InspectAssets(Module, InspectRequest).bSuccess);

	TSharedRef<FJsonObject> ValidateRequest = MakeShared<FJsonObject>();
	ValidateRequest->SetStringField(TEXT("path"), ImportedPath);
	TestTrue(TEXT("Imported texture can be validated"), Service.ValidateAssets(Module, ValidateRequest).bSuccess);

	TArray<UObject*> ImportedObjects;
	for (const TSharedPtr<FJsonValue>& PathValue : *ImportedPaths)
	{
		if (UObject* Object = LoadObject<UObject>(nullptr, *PathValue->AsString()))
		{
			ImportedObjects.Add(Object);
		}
	}
	if (ImportedObjects.Num() > 0)
	{
		ObjectTools::DeleteObjectsUnchecked(ImportedObjects);
	}
	return true;
}

bool FBATAssetPipelineRejectsEscapingGltfSidecarTest::RunTest(const FString& Parameters)
{
	const FString TestDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("BATTests"), TEXT("AssetPipeline"));
	const FString Source = FPaths::Combine(TestDir, TEXT("EscapingSidecar.gltf"));
	const FString OutsideSidecar = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("BATOutsideAssetPipelineTest.bin")));
	IFileManager::Get().MakeDirectory(*TestDir, true);
	if (!TestTrue(TEXT("Out-of-root sidecar fixture can be written"), FFileHelper::SaveStringToFile(TEXT("BAT!"), *OutsideSidecar)))
	{
		return false;
	}
	const FString Gltf = TEXT("{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"uri\":\"../../../../BATOutsideAssetPipelineTest.bin\",\"byteLength\":4}]}");
	if (!TestTrue(TEXT("Security test glTF can be written"), FFileHelper::SaveStringToFile(Gltf, *Source)))
	{
		IFileManager::Get().Delete(*OutsideSidecar, false, true, true);
		return false;
	}

	FBlueprintAutomationToolkitModule Module;
	const FAssetPipelineService Service;
	TSharedRef<FJsonObject> Request = MakeShared<FJsonObject>();
	Request->SetStringField(TEXT("source"), Source);
	Request->SetStringField(TEXT("destination"), TEXT("/Game/__BAT_Automation/AssetPipeline"));
	Request->SetBoolField(TEXT("dry_run"), true);
	const FAutomationResult Result = Service.ImportAssets(Module, Request);

	TestFalse(TEXT("An out-of-root glTF sidecar is rejected"), Result.bSuccess);
	TestEqual(TEXT("Escaping glTF sidecar uses source_denied"), Result.ErrorCode, FString(TEXT("source_denied")));
	TestTrue(TEXT("Failure explains the allowed-root boundary"), Result.ErrorMessage.Contains(TEXT("outside Asset Import Allowed Roots")));
	IFileManager::Get().Delete(*Source, false, true, true);
	IFileManager::Get().Delete(*OutsideSidecar, false, true, true);
	return true;
}

bool FBATAssetPipelineAnimatedModelRoundTripTest::RunTest(const FString& Parameters)
{
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("BlueprintAutomationToolkit"));
	if (!TestTrue(TEXT("Plugin exists for the animated-model round trip"), Plugin.IsValid()))
	{
		return false;
	}
	const FString Source = FPaths::Combine(
		Plugin->GetBaseDir(),
		TEXT("Examples"),
		TEXT("BATExampleProject"),
		TEXT("SourceArt"),
		TEXT("AnimatedOctopus"),
		TEXT("SK_Octopus.gltf"));
	if (!IFileManager::Get().FileExists(*Source))
	{
		AddWarning(TEXT("Animated octopus SourceArt is not included in this packaged test installation; model round-trip skipped."));
		return true;
	}

	FBlueprintAutomationToolkitModule Module;
	const FAssetPipelineService Service;
	TSharedRef<FJsonObject> Request = MakeShared<FJsonObject>();
	Request->SetStringField(TEXT("source"), Source);
	Request->SetStringField(TEXT("destination"), TEXT("/Game/__BAT_Automation/AnimatedModelRoundTrip"));
	Request->SetStringField(TEXT("expected_type"), TEXT("SkeletalMesh"));
	Request->SetBoolField(TEXT("replace_existing"), true);
	Request->SetBoolField(TEXT("skip_unchanged"), false);
	Request->SetBoolField(TEXT("save"), false);

	const FAutomationResult Result = Service.ImportAssets(Module, Request);
	if (!TestTrue(TEXT("Animated glTF model import succeeds"), Result.bSuccess))
	{
		AddError(Result.ErrorMessage);
		return false;
	}
	const TSharedPtr<FJsonObject> Root = GetStructuredRoot(Result);
	const TArray<TSharedPtr<FJsonValue>>* ImportedPaths = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* PrimaryPaths = nullptr;
	if (!TestTrue(TEXT("Animated model import reports all and primary object paths"), Root.IsValid()
		&& Root->TryGetArrayField(TEXT("importedObjectPaths"), ImportedPaths) && ImportedPaths && ImportedPaths->Num() > 1
		&& Root->TryGetArrayField(TEXT("primaryObjectPaths"), PrimaryPaths) && PrimaryPaths && PrimaryPaths->Num() == 1))
	{
		return false;
	}

	UObject* Primary = LoadObject<UObject>(nullptr, *(*PrimaryPaths)[0]->AsString());
	TestTrue(TEXT("Animated model primary asset is a Skeletal Mesh"), Primary && Primary->IsA<USkeletalMesh>());

	TArray<UObject*> ImportedObjects;
	for (const TSharedPtr<FJsonValue>& PathValue : *ImportedPaths)
	{
		if (UObject* Object = LoadObject<UObject>(nullptr, *PathValue->AsString()))
		{
			ImportedObjects.AddUnique(Object);
		}
	}
	if (ImportedObjects.Num() > 0)
	{
		ObjectTools::DeleteObjectsUnchecked(ImportedObjects);
	}
	return true;
}

bool FBATAssetPipelineValidationStopsPipelineTest::RunTest(const FString& Parameters)
{
	UStaticMesh* EmptyMesh = NewObject<UStaticMesh>(GetTransientPackage(), TEXT("BATEmptyValidationMesh"));
	if (!TestNotNull(TEXT("Transient empty Static Mesh can be created"), EmptyMesh))
	{
		return false;
	}

	TSharedRef<FJsonObject> ValidatePayload = MakeShared<FJsonObject>();
	ValidatePayload->SetStringField(TEXT("path"), EmptyMesh->GetPathName());
	TSharedRef<FJsonObject> Rules = MakeShared<FJsonObject>();
	Rules->SetBoolField(TEXT("require_materials"), false);
	ValidatePayload->SetObjectField(TEXT("rules"), Rules);
	TSharedRef<FJsonObject> ValidateStep = MakeShared<FJsonObject>();
	ValidateStep->SetStringField(TEXT("op"), TEXT("validate"));
	ValidateStep->SetObjectField(TEXT("payload"), ValidatePayload);
	TSharedRef<FJsonObject> InspectStep = MakeShared<FJsonObject>();
	InspectStep->SetStringField(TEXT("op"), TEXT("inspect"));
	InspectStep->SetStringField(TEXT("path"), EmptyMesh->GetPathName());

	TSharedRef<FJsonObject> Request = MakeShared<FJsonObject>();
	Request->SetArrayField(TEXT("steps"), {
		MakeShared<FJsonValueObject>(ValidateStep),
		MakeShared<FJsonValueObject>(InspectStep)
	});
	Request->SetBoolField(TEXT("continue_on_error"), false);

	FBlueprintAutomationToolkitModule Module;
	const FAssetPipelineService Service;
	const FAutomationResult Result = Service.ExecutePipeline(Module, Request);
	const TSharedPtr<FJsonObject> Root = GetStructuredRoot(Result);
	bool bPipelineOk = true;
	double FailureCount = 0.0;
	double StepCount = 0.0;
	TestTrue(TEXT("Validation-gated pipeline returns structured data"), Result.bSuccess && Root.IsValid());
	TestEqual(TEXT("Structured pipeline failure reports the canonical error code"), Result.ErrorCode, FString(TEXT("asset_pipeline_failed")));
	TestTrue(TEXT("Validation failure marks pipeline as failed"), Root.IsValid() && Root->TryGetBoolField(TEXT("ok"), bPipelineOk) && !bPipelineOk);
	TestTrue(TEXT("Validation failure is counted"), Root.IsValid() && Root->TryGetNumberField(TEXT("failureCount"), FailureCount) && FailureCount == 1.0);
	TestTrue(TEXT("Pipeline stops before the next step"), Root.IsValid() && Root->TryGetNumberField(TEXT("stepCount"), StepCount) && StepCount == 1.0);

	const FString JobId = TEXT("asset-pipeline-validation-gate-test");
	Module.Test_AddOrUpdateJob(
		JobId,
		TEXT("asset-pipeline-validation-gate-request"),
		TEXT("asset.pipeline"),
		FBlueprintAutomationToolkitModule::EAutomationTestJobState::Queued,
		false,
		Request);
	Module.Test_ExecuteJob(JobId);
	FBlueprintAutomationToolkitModule::FAutomationTestJobSnapshot Snapshot;
	TestTrue(TEXT("Asynchronous-style validation-gated job remains queryable"), Module.Test_GetJobSnapshot(JobId, Snapshot));
	TestEqual(TEXT("Validation-gated job is marked failed"), Snapshot.State, FBlueprintAutomationToolkitModule::EAutomationTestJobState::Failed);
	TestTrue(TEXT("Validation-gated job logs its failure"), Snapshot.Logs.ContainsByPredicate([](const FString& Line)
	{
		return Line.Contains(TEXT("job_failed:asset_pipeline_failed"));
	}));
	EmptyMesh->MarkAsGarbage();
	return true;
}

bool FBATAssetPipelineDryRunSkipsDependentMutationsTest::RunTest(const FString& Parameters)
{
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("BlueprintAutomationToolkit"));
	if (!TestTrue(TEXT("Plugin exists for pipeline dry-run"), Plugin.IsValid()))
	{
		return false;
	}
	const FString Source = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources"), TEXT("Icon128.png"));

	TSharedRef<FJsonObject> ImportPayload = MakeShared<FJsonObject>();
	ImportPayload->SetStringField(TEXT("source"), Source);
	ImportPayload->SetStringField(TEXT("destination"), TEXT("/Game/__BAT_Automation/AssetPipelineDryRun"));
	ImportPayload->SetStringField(TEXT("destination_name"), TEXT("T_BATDryRunMustNotExist"));
	TSharedRef<FJsonObject> ImportStep = MakeShared<FJsonObject>();
	ImportStep->SetStringField(TEXT("op"), TEXT("import"));
	ImportStep->SetObjectField(TEXT("payload"), ImportPayload);

	TSharedRef<FJsonObject> RepairStep = MakeShared<FJsonObject>();
	RepairStep->SetStringField(TEXT("op"), TEXT("repair"));
	RepairStep->SetStringField(TEXT("path"), TEXT("$imported"));
	RepairStep->SetStringField(TEXT("mode"), TEXT("safe_auto"));
	TSharedRef<FJsonObject> ShowcaseStep = MakeShared<FJsonObject>();
	ShowcaseStep->SetStringField(TEXT("op"), TEXT("showcase"));
	ShowcaseStep->SetBoolField(TEXT("capture"), true);

	TSharedRef<FJsonObject> Request = MakeShared<FJsonObject>();
	Request->SetBoolField(TEXT("dry_run"), true);
	Request->SetArrayField(TEXT("steps"), {
		MakeShared<FJsonValueObject>(ImportStep),
		MakeShared<FJsonValueObject>(RepairStep),
		MakeShared<FJsonValueObject>(ShowcaseStep)
	});

	FBlueprintAutomationToolkitModule Module;
	const FAssetPipelineService Service;
	const FAutomationResult Result = Service.ExecutePipeline(Module, Request);
	const TSharedPtr<FJsonObject> Root = GetStructuredRoot(Result);
	double StepCount = 0.0;
	double FailureCount = -1.0;
	const TArray<TSharedPtr<FJsonValue>>* CreatedPaths = nullptr;
	TestTrue(TEXT("Full pipeline dry-run succeeds"), Result.bSuccess && Root.IsValid());
	TestTrue(TEXT("Dry-run reports every planned step"), Root.IsValid() && Root->TryGetNumberField(TEXT("stepCount"), StepCount) && StepCount == 3.0);
	TestTrue(TEXT("Dependent dry-run skips are not failures"), Root.IsValid() && Root->TryGetNumberField(TEXT("failureCount"), FailureCount) && FailureCount == 0.0);
	TestTrue(TEXT("Dry-run creates no object paths"), Root.IsValid()
		&& Root->TryGetArrayField(TEXT("createdObjectPaths"), CreatedPaths)
		&& CreatedPaths
		&& CreatedPaths->Num() == 0);
	TestNull(TEXT("Dry-run destination asset does not exist"), FindObject<UObject>(nullptr, TEXT("/Game/__BAT_Automation/AssetPipelineDryRun/T_BATDryRunMustNotExist.T_BATDryRunMustNotExist")));

	UStaticMesh* PreviewMesh = NewObject<UStaticMesh>(GetTransientPackage(), TEXT("BATDryRunShowcaseMesh"));
	TSharedRef<FJsonObject> ShowcaseRequest = MakeShared<FJsonObject>();
	ShowcaseRequest->SetStringField(TEXT("asset"), PreviewMesh->GetPathName());
	ShowcaseRequest->SetBoolField(TEXT("dry_run"), true);
	ShowcaseRequest->SetBoolField(TEXT("capture"), true);
	ShowcaseRequest->SetStringField(TEXT("output_folder"), TEXT("BATTests/DryRunPreview"));
	const FAutomationResult ShowcaseResult = Service.CreateShowcaseAndCapture(Module, ShowcaseRequest);
	const TSharedPtr<FJsonObject> ShowcaseRoot = GetStructuredRoot(ShowcaseResult);
	bool bWouldSpawn = false;
	bool bWouldCapture = false;
	TestTrue(TEXT("Explicit showcase dry-run succeeds without a viewport"), ShowcaseResult.bSuccess && ShowcaseRoot.IsValid());
	TestTrue(TEXT("Showcase dry-run reports actor preview"), ShowcaseRoot.IsValid() && ShowcaseRoot->TryGetBoolField(TEXT("wouldSpawnActor"), bWouldSpawn) && bWouldSpawn);
	TestTrue(TEXT("Showcase dry-run reports capture preview"), ShowcaseRoot.IsValid() && ShowcaseRoot->TryGetBoolField(TEXT("wouldCapture"), bWouldCapture) && bWouldCapture);
	PreviewMesh->MarkAsGarbage();
	return true;
}

bool FBATAssetPipelineBatchFailureRollsBackNewAssetsTest::RunTest(const FString& Parameters)
{
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("BlueprintAutomationToolkit"));
	if (!TestTrue(TEXT("Plugin exists for batch rollback"), Plugin.IsValid()))
	{
		return false;
	}
	const FString ValidSource = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources"), TEXT("Icon128.png"));
	const FString MissingSource = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources"), TEXT("BAT_Missing_Rollback_Source.png"));

	TSharedRef<FJsonObject> Request = MakeShared<FJsonObject>();
	Request->SetArrayField(TEXT("sources"), {
		MakeShared<FJsonValueString>(ValidSource),
		MakeShared<FJsonValueString>(MissingSource)
	});
	Request->SetStringField(TEXT("destination"), TEXT("/Game/__BAT_Automation/AssetPipelineRollback"));
	Request->SetStringField(TEXT("destination_name"), TEXT("T_BATMustRollback"));
	Request->SetBoolField(TEXT("replace_existing"), true);
	Request->SetBoolField(TEXT("skip_unchanged"), false);
	Request->SetBoolField(TEXT("save"), false);

	FBlueprintAutomationToolkitModule Module;
	const FAssetPipelineService Service;
	const FAutomationResult Result = Service.ImportAssets(Module, Request);
	TestFalse(TEXT("Batch fails when a later source is missing"), Result.bSuccess);
	TestEqual(TEXT("Missing later source reports source_denied"), Result.ErrorCode, FString(TEXT("source_denied")));
	TestTrue(TEXT("Failure reports best-effort cleanup"), Result.ErrorMessage.Contains(TEXT("Rolled back 1 newly created asset")));
	TestNull(TEXT("Earlier batch asset is removed"), FindObject<UObject>(nullptr, TEXT("/Game/__BAT_Automation/AssetPipelineRollback/T_BATMustRollback.T_BATMustRollback")));
	return true;
}

#endif

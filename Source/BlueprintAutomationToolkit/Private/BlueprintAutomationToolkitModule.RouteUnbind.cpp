// Copyright 2026 AkaSoft. All Rights Reserved.

#include "BlueprintAutomationToolkitModule.h"

#include "IHttpRouter.h"

void FBlueprintAutomationToolkitModule::UnbindRoutes()
{
	if (!Router.IsValid())
	{
		DynamicAutomationRoutes.Reset();
		return;
	}

	UnbindRegisteredAutomationRoutes();

	if (HealthRoute.IsValid())
	{
		Router->UnbindRoute(HealthRoute);
		HealthRoute.Reset();
	}
	if (EditorMapRoute.IsValid())
	{
		Router->UnbindRoute(EditorMapRoute);
		EditorMapRoute.Reset();
	}
	if (EditorQuitRoute.IsValid())
	{
		Router->UnbindRoute(EditorQuitRoute);
		EditorQuitRoute.Reset();
	}
	if (EditorLayoutApplyRoute.IsValid())
	{
		Router->UnbindRoute(EditorLayoutApplyRoute);
		EditorLayoutApplyRoute.Reset();
	}
	if (CapabilitiesRoute.IsValid())
	{
		Router->UnbindRoute(CapabilitiesRoute);
		CapabilitiesRoute.Reset();
	}
	if (EngineDiscoverRoute.IsValid())
	{
		Router->UnbindRoute(EngineDiscoverRoute);
		EngineDiscoverRoute.Reset();
	}
	if (PlanValidateRoute.IsValid())
	{
		Router->UnbindRoute(PlanValidateRoute);
		PlanValidateRoute.Reset();
	}
	if (PlanApplyRoute.IsValid())
	{
		Router->UnbindRoute(PlanApplyRoute);
		PlanApplyRoute.Reset();
	}
	if (OpenApiRoute.IsValid())
	{
		Router->UnbindRoute(OpenApiRoute);
		OpenApiRoute.Reset();
	}
	if (JobsSubmitRoute.IsValid())
	{
		Router->UnbindRoute(JobsSubmitRoute);
		JobsSubmitRoute.Reset();
	}
	if (JobGetRoute.IsValid())
	{
		Router->UnbindRoute(JobGetRoute);
		JobGetRoute.Reset();
	}
	if (JobCancelRoute.IsValid())
	{
		Router->UnbindRoute(JobCancelRoute);
		JobCancelRoute.Reset();
	}
	if (LogsTailRoute.IsValid())
	{
		Router->UnbindRoute(LogsTailRoute);
		LogsTailRoute.Reset();
	}
	if (ExecRoute.IsValid())
	{
		Router->UnbindRoute(ExecRoute);
		ExecRoute.Reset();
	}
	if (ExecAliasRoute.IsValid())
	{
		Router->UnbindRoute(ExecAliasRoute);
		ExecAliasRoute.Reset();
	}
	if (PieStartRoute.IsValid())
	{
		Router->UnbindRoute(PieStartRoute);
		PieStartRoute.Reset();
	}
	if (PieStopRoute.IsValid())
	{
		Router->UnbindRoute(PieStopRoute);
		PieStopRoute.Reset();
	}
	if (ActorIntrospectRoute.IsValid())
	{
		Router->UnbindRoute(ActorIntrospectRoute);
		ActorIntrospectRoute.Reset();
	}
	if (ActorPropertiesRoute.IsValid())
	{
		Router->UnbindRoute(ActorPropertiesRoute);
		ActorPropertiesRoute.Reset();
	}
	if (BlueprintCreateRoute.IsValid())
	{
		Router->UnbindRoute(BlueprintCreateRoute);
		BlueprintCreateRoute.Reset();
	}
	if (BlueprintApplyRoute.IsValid())
	{
		Router->UnbindRoute(BlueprintApplyRoute);
		BlueprintApplyRoute.Reset();
	}
	if (BlueprintSetDefaultsRoute.IsValid())
	{
		Router->UnbindRoute(BlueprintSetDefaultsRoute);
		BlueprintSetDefaultsRoute.Reset();
	}
	if (BlueprintComponentsAddRoute.IsValid())
	{
		Router->UnbindRoute(BlueprintComponentsAddRoute);
		BlueprintComponentsAddRoute.Reset();
	}
	if (BlueprintComponentsSetRoute.IsValid())
	{
		Router->UnbindRoute(BlueprintComponentsSetRoute);
		BlueprintComponentsSetRoute.Reset();
	}
	if (BlueprintComponentsInstancesAddRoute.IsValid())
	{
		Router->UnbindRoute(BlueprintComponentsInstancesAddRoute);
		BlueprintComponentsInstancesAddRoute.Reset();
	}
	if (BlueprintComponentsRemoveRoute.IsValid())
	{
		Router->UnbindRoute(BlueprintComponentsRemoveRoute);
		BlueprintComponentsRemoveRoute.Reset();
	}
	if (BlueprintComponentsReplaceRoute.IsValid())
	{
		Router->UnbindRoute(BlueprintComponentsReplaceRoute);
		BlueprintComponentsReplaceRoute.Reset();
	}
	if (BlueprintSchemaRoute.IsValid())
	{
		Router->UnbindRoute(BlueprintSchemaRoute);
		BlueprintSchemaRoute.Reset();
	}
	if (BlueprintGraphsRoute.IsValid())
	{
		Router->UnbindRoute(BlueprintGraphsRoute);
		BlueprintGraphsRoute.Reset();
	}
	if (BlueprintGraphNodesRoute.IsValid())
	{
		Router->UnbindRoute(BlueprintGraphNodesRoute);
		BlueprintGraphNodesRoute.Reset();
	}
	if (BlueprintGraphLinksRoute.IsValid())
	{
		Router->UnbindRoute(BlueprintGraphLinksRoute);
		BlueprintGraphLinksRoute.Reset();
	}
	if (BlueprintGraphApplyRoute.IsValid())
	{
		Router->UnbindRoute(BlueprintGraphApplyRoute);
		BlueprintGraphApplyRoute.Reset();
	}
	if (BlueprintNodeAddCallFunctionRoute.IsValid())
	{
		Router->UnbindRoute(BlueprintNodeAddCallFunctionRoute);
		BlueprintNodeAddCallFunctionRoute.Reset();
	}
	if (BlueprintNodeAddCustomEventRoute.IsValid())
	{
		Router->UnbindRoute(BlueprintNodeAddCustomEventRoute);
		BlueprintNodeAddCustomEventRoute.Reset();
	}
	if (BlueprintNodeAddBranchRoute.IsValid())
	{
		Router->UnbindRoute(BlueprintNodeAddBranchRoute);
		BlueprintNodeAddBranchRoute.Reset();
	}
	if (BlueprintPinConnectRoute.IsValid())
	{
		Router->UnbindRoute(BlueprintPinConnectRoute);
		BlueprintPinConnectRoute.Reset();
	}
	if (BlueprintPinSetDefaultRoute.IsValid())
	{
		Router->UnbindRoute(BlueprintPinSetDefaultRoute);
		BlueprintPinSetDefaultRoute.Reset();
	}
	if (BlueprintNodeDeleteRoute.IsValid())
	{
		Router->UnbindRoute(BlueprintNodeDeleteRoute);
		BlueprintNodeDeleteRoute.Reset();
	}
	if (BlueprintNodeDescribeRoute.IsValid())
	{
		Router->UnbindRoute(BlueprintNodeDescribeRoute);
		BlueprintNodeDescribeRoute.Reset();
	}
	if (ObjectResolveRoute.IsValid())
	{
		Router->UnbindRoute(ObjectResolveRoute);
		ObjectResolveRoute.Reset();
	}
	if (ObjectDescribeGetRoute.IsValid())
	{
		Router->UnbindRoute(ObjectDescribeGetRoute);
		ObjectDescribeGetRoute.Reset();
	}
	if (ObjectGetPropertyRoute.IsValid())
	{
		Router->UnbindRoute(ObjectGetPropertyRoute);
		ObjectGetPropertyRoute.Reset();
	}
	if (ObjectSetPropertyAliasRoute.IsValid())
	{
		Router->UnbindRoute(ObjectSetPropertyAliasRoute);
		ObjectSetPropertyAliasRoute.Reset();
	}
	if (ObjectCallFunctionAliasRoute.IsValid())
	{
		Router->UnbindRoute(ObjectCallFunctionAliasRoute);
		ObjectCallFunctionAliasRoute.Reset();
	}
	if (ActorSpawnRoute.IsValid())
	{
		Router->UnbindRoute(ActorSpawnRoute);
		ActorSpawnRoute.Reset();
	}
	if (ActorDestroyRoute.IsValid())
	{
		Router->UnbindRoute(ActorDestroyRoute);
		ActorDestroyRoute.Reset();
	}
	if (ActorFindRoute.IsValid())
	{
		Router->UnbindRoute(ActorFindRoute);
		ActorFindRoute.Reset();
	}
	if (BlueprintGraphReadRoute.IsValid())
	{
		Router->UnbindRoute(BlueprintGraphReadRoute);
		BlueprintGraphReadRoute.Reset();
	}
	if (BlueprintCompileSaveRoute.IsValid())
	{
		Router->UnbindRoute(BlueprintCompileSaveRoute);
		BlueprintCompileSaveRoute.Reset();
	}
	if (UMGSchemaRoute.IsValid())
	{
		Router->UnbindRoute(UMGSchemaRoute);
		UMGSchemaRoute.Reset();
	}
	if (UMGCreateRoute.IsValid())
	{
		Router->UnbindRoute(UMGCreateRoute);
		UMGCreateRoute.Reset();
	}
	if (UMGDesignerReadRoute.IsValid())
	{
		Router->UnbindRoute(UMGDesignerReadRoute);
		UMGDesignerReadRoute.Reset();
	}
	if (UMGDesignerApplyRoute.IsValid())
	{
		Router->UnbindRoute(UMGDesignerApplyRoute);
		UMGDesignerApplyRoute.Reset();
	}
	if (EditorSelectRoute.IsValid())
	{
		Router->UnbindRoute(EditorSelectRoute);
		EditorSelectRoute.Reset();
	}
	if (EditorFocusRoute.IsValid())
	{
		Router->UnbindRoute(EditorFocusRoute);
		EditorFocusRoute.Reset();
	}
	if (AssetDuplicateRoute.IsValid())
	{
		Router->UnbindRoute(AssetDuplicateRoute);
		AssetDuplicateRoute.Reset();
	}
	if (AssetSaveRoute.IsValid())
	{
		Router->UnbindRoute(AssetSaveRoute);
		AssetSaveRoute.Reset();
	}
	if (AssetCreateRoute.IsValid())
	{
		Router->UnbindRoute(AssetCreateRoute);
		AssetCreateRoute.Reset();
	}
	if (AssetImportFormatsRoute.IsValid())
	{
		Router->UnbindRoute(AssetImportFormatsRoute);
		AssetImportFormatsRoute.Reset();
	}
	if (AssetImportRoute.IsValid())
	{
		Router->UnbindRoute(AssetImportRoute);
		AssetImportRoute.Reset();
	}
	if (AssetInspectRoute.IsValid())
	{
		Router->UnbindRoute(AssetInspectRoute);
		AssetInspectRoute.Reset();
	}
	if (AssetConfigureRoute.IsValid())
	{
		Router->UnbindRoute(AssetConfigureRoute);
		AssetConfigureRoute.Reset();
	}
	if (AssetValidateRoute.IsValid())
	{
		Router->UnbindRoute(AssetValidateRoute);
		AssetValidateRoute.Reset();
	}
	if (AssetPipelineExecuteRoute.IsValid())
	{
		Router->UnbindRoute(AssetPipelineExecuteRoute);
		AssetPipelineExecuteRoute.Reset();
	}
	if (AssetShowcaseCaptureRoute.IsValid())
	{
		Router->UnbindRoute(AssetShowcaseCaptureRoute);
		AssetShowcaseCaptureRoute.Reset();
	}
	if (PcgSpawnSpheresRoute.IsValid())
	{
		Router->UnbindRoute(PcgSpawnSpheresRoute);
		PcgSpawnSpheresRoute.Reset();
	}
	if (ActionsListRoute.IsValid())
	{
		Router->UnbindRoute(ActionsListRoute);
		ActionsListRoute.Reset();
	}
	if (ActionsRunRoute.IsValid())
	{
		Router->UnbindRoute(ActionsRunRoute);
		ActionsRunRoute.Reset();
	}
}

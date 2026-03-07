#include "Services/BlueprintGraphService.h"

#include "Commands/AutomationCommand.h"
#include "Routes/Blueprint/BlueprintGraphApplyRequest.h"
#include "ScopedTransaction.h"
#include "Services/BlueprintGraph/BlueprintGraphFinalizeService.h"
#include "Services/BlueprintGraph/BlueprintGraphLayoutService.h"
#include "Services/BlueprintGraph/BlueprintGraphLinkService.h"
#include "Services/BlueprintGraph/BlueprintGraphNodeService.h"
#include "Services/BlueprintGraph/BlueprintGraphValidationService.h"

FAutomationResult FBlueprintGraphService::ApplyGraphPatch(const FBlueprintGraphApplyRequest& Request)
{
	FBlueprintGraphApplyResult ApplyResult;
	FBlueprintGraphResolvedTarget Target;
	if (!FBlueprintGraphValidationService::PrepareApplyTarget(Request, Target, ApplyResult))
	{
		return FAutomationResult::Ok(FBlueprintGraphValidationService::MakeApplyResultData(Target.BlueprintObjectPath, Request.GraphName, ApplyResult), 400);
	}

	const bool bWillMutate = !Request.Options.bDryRun;
	TOptional<FScopedTransaction> Transaction;
	if (bWillMutate && Request.Options.bUseTransaction)
	{
		Transaction.Emplace(FText::FromString(TEXT("BAT Apply Blueprint Graph")));
	}

	if (bWillMutate)
	{
		Target.Blueprint->Modify();
		Target.Graph->Modify();
	}

	TMap<FString, UEdGraphNode*> NodeById;
	TSet<FString> CreatedNodeIds;
	FBlueprintGraphNodeService::ApplyNodes(Target.Blueprint, Target.Graph, Request.Nodes, bWillMutate, Request.Options.bCreateMissingNodes, ApplyResult, NodeById, CreatedNodeIds);
	FBlueprintGraphLinkService::ApplyLinks(Target.Graph, Request.Links, NodeById, Request.Options.bDryRun, ApplyResult);
	if (bWillMutate)
	{
		FBlueprintGraphLayoutService::AutoArrangeCreatedNodes(Target.Graph, Request.Nodes, Request.Links, NodeById, CreatedNodeIds);
	}
	if (bWillMutate)
	{
		FBlueprintGraphFinalizeService::Finalize(Target.Blueprint, Request.Options, ApplyResult);
	}

	ApplyResult.bOk = ApplyResult.Errors.Num() == 0 && ApplyResult.CompileErrors.Num() == 0;
	return FAutomationResult::Ok(FBlueprintGraphValidationService::MakeApplyResultData(Target.BlueprintObjectPath, Request.GraphName, ApplyResult));
}

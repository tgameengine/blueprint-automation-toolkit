#include "Services/BlueprintGraph/BlueprintGraphFinalizeService.h"

#include "Engine/Blueprint.h"
#include "FileHelpers.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Routes/Blueprint/BlueprintGraphApplyRequest.h"
#include "Services/BlueprintGraphService.h"

void FBlueprintGraphFinalizeService::Finalize(UBlueprint* Blueprint, const FBlueprintGraphApplyOptions& Options, FBlueprintGraphApplyResult& InOutResult)
{
	if (!Blueprint)
	{
		return;
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	if (Options.bCompile)
	{
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
	}
	if (Options.bSave)
	{
		TArray<UPackage*> Packages;
		Packages.Add(Blueprint->GetOutermost());
		if (!UEditorLoadingAndSavingUtils::SavePackages(Packages, false))
		{
			InOutResult.Errors.Add(TEXT("save_failed"));
		}
	}
}
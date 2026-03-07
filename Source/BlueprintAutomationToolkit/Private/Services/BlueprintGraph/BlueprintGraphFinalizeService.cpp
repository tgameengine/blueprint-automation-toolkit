#include "Services/BlueprintGraph/BlueprintGraphFinalizeService.h"

#include "Engine/Blueprint.h"
#include "FileHelpers.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Routes/Blueprint/BlueprintGraphApplyRequest.h"
#include "Services/BlueprintCompileDiagnosticsService.h"
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
		const BAT::BlueprintCompileDiagnostics::FDiagnostics Diagnostics = BAT::BlueprintCompileDiagnostics::Compile(Blueprint);
		InOutResult.CompileStatus = Diagnostics.CompileStatus;
		InOutResult.CompileErrorCount = Diagnostics.ErrorCount;
		InOutResult.CompileWarningCount = Diagnostics.WarningCount;
		InOutResult.CompileErrors = Diagnostics.Errors;
		InOutResult.CompileWarnings = Diagnostics.Warnings;
		if (!Diagnostics.bCompileSucceeded)
		{
			InOutResult.Errors.Add(TEXT("compile_failed"));
		}
	}
	else
	{
		InOutResult.CompileStatus = TEXT("not_requested");
	}
	if (Options.bSave)
	{
		if (InOutResult.CompileErrorCount > 0)
		{
			InOutResult.SaveStatus = TEXT("skipped_compile_failed");
		}
		else
		{
			TArray<UPackage*> Packages;
			Packages.Add(Blueprint->GetOutermost());
			if (!UEditorLoadingAndSavingUtils::SavePackages(Packages, false))
			{
				InOutResult.SaveStatus = TEXT("save_failed");
				InOutResult.Errors.Add(TEXT("save_failed"));
			}
			else
			{
				InOutResult.SaveStatus = TEXT("saved");
			}
		}
	}
	else
	{
		InOutResult.SaveStatus = TEXT("not_requested");
	}
}
#include "Services/PCG/PcgGraphAssetService.h"

#include "Routes/PCG/PcgApplyRequest.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "PCGGraph.h"
#include "ScopedTransaction.h"
#include "UObject/SavePackage.h"

namespace
{
	static bool SaveObjectPackage(UObject* ObjectToSave)
	{
		if (!ObjectToSave)
		{
			return false;
		}

		UPackage* Package = ObjectToSave->GetOutermost();
		if (!Package)
		{
			return false;
		}

		const FString PackageName = Package->GetName();
		if (!FPackageName::IsValidLongPackageName(PackageName))
		{
			return false;
		}

		const FString Filename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		Package->MarkPackageDirty();
		return UPackage::SavePackage(Package, ObjectToSave, *Filename, SaveArgs);
	}
}

FAutomationResult FPcgGraphAssetService::AcquireGraphAsset(const FPcgApplyRequest& Request, FPcgGraphAssetHandle& OutHandle)
{
	OutHandle = FPcgGraphAssetHandle();
	OutHandle.GraphObjectPath = Request.GraphPath;
	OutHandle.PackagePath = FPackageName::ObjectPathToPackageName(Request.GraphPath);
	OutHandle.AssetName = FPackageName::GetLongPackageAssetName(OutHandle.PackagePath);

	if (!FPackageName::IsValidLongPackageName(OutHandle.PackagePath) || OutHandle.AssetName.IsEmpty())
	{
		return FAutomationResult::Error(TEXT("invalid_graph_path"), TEXT("'graph' must be a valid writable project asset path."), 400);
	}

	if (UPCGGraphInterface* ExistingGraph = LoadObject<UPCGGraphInterface>(nullptr, *Request.GraphPath))
	{
		OutHandle.GraphInterface = ExistingGraph;
		OutHandle.Graph = ExistingGraph->GetMutablePCGGraph();
		OutHandle.bLoadedExisting = true;

		return FAutomationResult::Ok(nullptr);
	}

	if (!Request.Options.bCreateIfMissing)
	{
		return FAutomationResult::Error(TEXT("graph_not_found"), TEXT("Target PCG graph asset could not be loaded and create_if_missing is false."), 404);
	}

	const FScopedTransaction Transaction(
		Request.Options.bUseTransaction ? NSLOCTEXT("BlueprintAutomationToolkit", "PcgApplyCreateGraph", "BAT Create PCG Graph") : FText::GetEmpty());

	UPackage* Package = CreatePackage(*OutHandle.PackagePath);
	if (!Package)
	{
		return FAutomationResult::Error(TEXT("graph_create_failed"), TEXT("Failed to create package for PCG graph asset."), 500);
	}

	Package->Modify();
	UPCGGraph* NewGraph = NewObject<UPCGGraph>(Package, UPCGGraph::StaticClass(), FName(*OutHandle.AssetName), RF_Public | RF_Standalone | RF_Transactional);
	if (!NewGraph)
	{
		return FAutomationResult::Error(TEXT("graph_create_failed"), TEXT("Failed to create PCG graph asset."), 500);
	}

	NewGraph->Modify();
	FAssetRegistryModule::AssetCreated(NewGraph);
	Package->MarkPackageDirty();

	OutHandle.GraphInterface = NewGraph;
	OutHandle.Graph = NewGraph;
	OutHandle.bCreated = true;

	return FAutomationResult::Ok(nullptr);
}

FAutomationResult FPcgGraphAssetService::SaveGraphAsset(FPcgGraphAssetHandle& Handle)
{
	if (!Handle.GraphInterface)
	{
		return FAutomationResult::Error(TEXT("save_failed"), TEXT("No PCG graph asset is available to save."), 500);
	}

	if (!SaveObjectPackage(Handle.GraphInterface))
	{
		return FAutomationResult::Error(TEXT("save_failed"), TEXT("Failed to save PCG graph asset."), 500);
	}

	Handle.bSaved = true;
	return FAutomationResult::Ok(nullptr);
}
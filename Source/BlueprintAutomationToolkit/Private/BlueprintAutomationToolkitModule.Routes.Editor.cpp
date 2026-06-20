#include "BlueprintAutomationToolkitModule.h"

#include "AssetToolsModule.h"
#include "Async/Async.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextRenderActor.h"
#include "Elements/PCGStaticMeshSpawner.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/PlatformMisc.h"
#include "HttpPath.h"
#include "HttpResultCallback.h"
#include "Http/HttpRequestUtils.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "Misc/Guid.h"
#include "IHttpRouter.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Materials/MaterialInterface.h"
#include "MeshSelectors/PCGMeshSelectorWeighted.h"
#include "MeshSelectors/PCGMeshSelectorWeightedByCategory.h"
#include "Misc/DefaultValueHelper.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeLock.h"
#include "PCGComponent.h"
#include "PCGGraph.h"
#include "PCGVolume.h"
#include "ScopedTransaction.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/SavePackage.h"

namespace
{
	static const TCHAR* BAT_DefaultPcgTemplateGraphObjectPath = TEXT("/PCG/SampleContent/SimpleForest/PCGGraphs/SimpleForestWorldRayHits.SimpleForestWorldRayHits");
	static const TCHAR* BAT_DefaultPcgProjectGraphPackagePath = TEXT("/Game/PCG/Graphs/BAT_SpheresOnLandscape");

	enum class EBATLayoutActorKind : uint8
	{
		Unsupported,
		StaticMeshActor,
		PointLight,
		DirectionalLight,
		SkyLight,
		SkyAtmosphere,
		PlayerStart,
		TextRenderActor,
		PcgVolume,
	};

	static void ApplyMeshesToStaticMeshSpawners(UPCGGraph* Graph, const TArray<TSoftObjectPtr<UStaticMesh>>& Meshes)
	{
		if (!Graph || Meshes.IsEmpty())
		{
			return;
		}

		Graph->ForEachNodeRecursively([&Meshes](UPCGNode* Node)
		{
			if (!Node)
			{
				return true;
			}

			UPCGStaticMeshSpawnerSettings* SpawnerSettings = Cast<UPCGStaticMeshSpawnerSettings>(Node->GetSettings());
			if (!SpawnerSettings || !SpawnerSettings->MeshSelectorParameters)
			{
				return true;
			}

			if (UPCGMeshSelectorWeighted* Weighted = Cast<UPCGMeshSelectorWeighted>(SpawnerSettings->MeshSelectorParameters))
			{
				Weighted->MeshEntries.Reset();
				for (const TSoftObjectPtr<UStaticMesh>& Mesh : Meshes)
				{
					Weighted->MeshEntries.Add(FPCGMeshSelectorWeightedEntry(Mesh, /*Weight*/ 1));
				}
			}
			else if (UPCGMeshSelectorWeightedByCategory* WeightedByCategory = Cast<UPCGMeshSelectorWeightedByCategory>(SpawnerSettings->MeshSelectorParameters))
			{
				for (FPCGWeightedByCategoryEntryList& EntryList : WeightedByCategory->Entries)
				{
					EntryList.WeightedMeshEntries.Reset();
					for (const TSoftObjectPtr<UStaticMesh>& Mesh : Meshes)
					{
						EntryList.WeightedMeshEntries.Add(FPCGMeshSelectorWeightedEntry(Mesh, /*Weight*/ 1));
					}
				}
			}

			return true;
		});
	}

	static UPCGGraphInterface* EnsureDefaultPcgGraphAsset()
	{
		const FString PackagePath(BAT_DefaultPcgProjectGraphPackagePath);
		const FString AssetName = FPackageName::GetLongPackageAssetName(PackagePath);
		const FString AssetPath = FPackageName::GetLongPackagePath(PackagePath);
		const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *PackagePath, *AssetName);

		if (UPCGGraphInterface* Existing = LoadObject<UPCGGraphInterface>(nullptr, *ObjectPath))
		{
			return Existing;
		}

		UPCGGraphInterface* TemplateGraph = LoadObject<UPCGGraphInterface>(nullptr, BAT_DefaultPcgTemplateGraphObjectPath);
		if (!TemplateGraph)
		{
			return nullptr;
		}

		if (!FPackageName::IsValidLongPackageName(PackagePath) || AssetName.IsEmpty() || AssetPath.IsEmpty())
		{
			return nullptr;
		}

		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
		UObject* Duplicated = AssetToolsModule.Get().DuplicateAsset(AssetName, AssetPath, TemplateGraph);
		UPCGGraphInterface* Graph = Cast<UPCGGraphInterface>(Duplicated);
		if (!Graph)
		{
			return nullptr;
		}

		if (UPackage* Package = Graph->GetOutermost())
		{
			Package->MarkPackageDirty();
			const FString Filename = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);

			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			SaveArgs.SaveFlags = SAVE_NoError;
			UPackage::SavePackage(Package, Graph, *Filename, SaveArgs);
		}

		return Graph;
	}

	static TUniquePtr<FHttpServerResponse> MakeJsonResponse(EHttpServerResponseCodes Code, const FString& JsonString)
	{
		return BAT::Http::MakeJsonResponseFromString(static_cast<int32>(Code), JsonString);
	}

	static FString ToJsonString(const TSharedRef<FJsonObject>& Object)
	{
		FString Out;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Object, Writer);
		return Out;
	}

	static bool TryParseJsonBody(const TArray<uint8>& Body, TSharedPtr<FJsonObject>& OutObj)
	{
		if (Body.Num() <= 0)
		{
			OutObj.Reset();
			return true;
		}

		FString BodyString = FString(Body.Num(), reinterpret_cast<const ANSICHAR*>(Body.GetData()));
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(BodyString);
		return FJsonSerializer::Deserialize(Reader, OutObj) && OutObj.IsValid();
	}

	static bool TryParseVector3(const TSharedPtr<FJsonValue>& Value, FVector& OutVec)
	{
		if (!Value.IsValid())
		{
			return false;
		}

		if (Value->Type == EJson::Array)
		{
			const TArray<TSharedPtr<FJsonValue>>& Arr = Value->AsArray();
			if (Arr.Num() != 3)
			{
				return false;
			}

			OutVec.X = (float)Arr[0]->AsNumber();
			OutVec.Y = (float)Arr[1]->AsNumber();
			OutVec.Z = (float)Arr[2]->AsNumber();
			return true;
		}

		if (Value->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject> Obj = Value->AsObject();
			if (!Obj.IsValid())
			{
				return false;
			}

			double X = 0.0;
			double Y = 0.0;
			double Z = 0.0;
			if (!(Obj->TryGetNumberField(TEXT("x"), X) || Obj->TryGetNumberField(TEXT("X"), X))) return false;
			if (!(Obj->TryGetNumberField(TEXT("y"), Y) || Obj->TryGetNumberField(TEXT("Y"), Y))) return false;
			if (!(Obj->TryGetNumberField(TEXT("z"), Z) || Obj->TryGetNumberField(TEXT("Z"), Z))) return false;

			OutVec = FVector((float)X, (float)Y, (float)Z);
			return true;
		}

		return false;
	}

	static EBATLayoutActorKind ResolveLayoutActorKind(const FString& InClass)
	{
		FString ClassValue = InClass;
		ClassValue.TrimStartAndEndInline();

		if (ClassValue.Equals(TEXT("/Script/Engine.StaticMeshActor"), ESearchCase::IgnoreCase)
			|| ClassValue.Equals(TEXT("AStaticMeshActor"), ESearchCase::IgnoreCase)
			|| ClassValue.Equals(TEXT("StaticMeshActor"), ESearchCase::IgnoreCase))
		{
			return EBATLayoutActorKind::StaticMeshActor;
		}

		if (ClassValue.Equals(TEXT("/Script/Engine.PointLight"), ESearchCase::IgnoreCase)
			|| ClassValue.Equals(TEXT("APointLight"), ESearchCase::IgnoreCase)
			|| ClassValue.Equals(TEXT("PointLight"), ESearchCase::IgnoreCase))
		{
			return EBATLayoutActorKind::PointLight;
		}

		if (ClassValue.Equals(TEXT("/Script/Engine.DirectionalLight"), ESearchCase::IgnoreCase)
			|| ClassValue.Equals(TEXT("ADirectionalLight"), ESearchCase::IgnoreCase)
			|| ClassValue.Equals(TEXT("DirectionalLight"), ESearchCase::IgnoreCase))
		{
			return EBATLayoutActorKind::DirectionalLight;
		}

		if (ClassValue.Equals(TEXT("/Script/Engine.SkyLight"), ESearchCase::IgnoreCase)
			|| ClassValue.Equals(TEXT("ASkyLight"), ESearchCase::IgnoreCase)
			|| ClassValue.Equals(TEXT("SkyLight"), ESearchCase::IgnoreCase))
		{
			return EBATLayoutActorKind::SkyLight;
		}

		if (ClassValue.Equals(TEXT("/Script/Engine.SkyAtmosphere"), ESearchCase::IgnoreCase)
			|| ClassValue.Equals(TEXT("ASkyAtmosphere"), ESearchCase::IgnoreCase)
			|| ClassValue.Equals(TEXT("SkyAtmosphere"), ESearchCase::IgnoreCase))
		{
			return EBATLayoutActorKind::SkyAtmosphere;
		}

		if (ClassValue.Equals(TEXT("/Script/Engine.PlayerStart"), ESearchCase::IgnoreCase)
			|| ClassValue.Equals(TEXT("APlayerStart"), ESearchCase::IgnoreCase)
			|| ClassValue.Equals(TEXT("PlayerStart"), ESearchCase::IgnoreCase))
		{
			return EBATLayoutActorKind::PlayerStart;
		}

		if (ClassValue.Equals(TEXT("/Script/Engine.TextRenderActor"), ESearchCase::IgnoreCase)
			|| ClassValue.Equals(TEXT("ATextRenderActor"), ESearchCase::IgnoreCase)
			|| ClassValue.Equals(TEXT("TextRenderActor"), ESearchCase::IgnoreCase))
		{
			return EBATLayoutActorKind::TextRenderActor;
		}

		if (ClassValue.Equals(TEXT("/Script/PCG.PCGVolume"), ESearchCase::IgnoreCase)
			|| ClassValue.Equals(TEXT("APCGVolume"), ESearchCase::IgnoreCase)
			|| ClassValue.Equals(TEXT("PCGVolume"), ESearchCase::IgnoreCase))
		{
			return EBATLayoutActorKind::PcgVolume;
		}

		return EBATLayoutActorKind::Unsupported;
	}

	static bool TryGetNumeric(const TSharedPtr<FJsonValue>& Value, double& OutNumber)
	{
		if (!Value.IsValid())
		{
			return false;
		}

		if (Value->Type == EJson::Number)
		{
			OutNumber = Value->AsNumber();
			return true;
		}

		if (Value->Type == EJson::String)
		{
			return FDefaultValueHelper::ParseDouble(Value->AsString(), OutNumber);
		}

		return false;
	}

	static bool TryGetBool(const TSharedPtr<FJsonValue>& Value, bool& OutBool)
	{
		if (!Value.IsValid())
		{
			return false;
		}

		if (Value->Type == EJson::Boolean)
		{
			OutBool = Value->AsBool();
			return true;
		}

		if (Value->Type == EJson::String)
		{
			const FString S = Value->AsString();
			if (S.Equals(TEXT("true"), ESearchCase::IgnoreCase) || S.Equals(TEXT("1"), ESearchCase::IgnoreCase))
			{
				OutBool = true;
				return true;
			}
			if (S.Equals(TEXT("false"), ESearchCase::IgnoreCase) || S.Equals(TEXT("0"), ESearchCase::IgnoreCase))
			{
				OutBool = false;
				return true;
			}
		}

		return false;
	}

	static void ApplyActorMetadata(AActor* Actor, const FString& Label, const TSharedPtr<FJsonObject>& ActorObj, int32& InOutApplied, int32& InOutRejected)
	{
		if (!Actor || !ActorObj.IsValid())
		{
			return;
		}

#if WITH_EDITOR
		if (!Label.IsEmpty())
		{
			Actor->SetActorLabel(Label, true);
		}

		FString FolderPath;
		if (!ActorObj->TryGetStringField(TEXT("folder"), FolderPath))
		{
			ActorObj->TryGetStringField(TEXT("folderPath"), FolderPath);
		}
		FolderPath.TrimStartAndEndInline();
		if (!FolderPath.IsEmpty())
		{
			Actor->SetFolderPath(FName(*FolderPath));
			++InOutApplied;
		}
#endif

		const TArray<TSharedPtr<FJsonValue>>* TagsArray = nullptr;
		if (ActorObj->TryGetArrayField(TEXT("tags"), TagsArray) && TagsArray)
		{
			for (const TSharedPtr<FJsonValue>& TagValue : *TagsArray)
			{
				if (!TagValue.IsValid() || TagValue->Type != EJson::String)
				{
					++InOutRejected;
					continue;
				}

				FString TagText = TagValue->AsString();
				TagText.TrimStartAndEndInline();
				if (TagText.IsEmpty())
				{
					++InOutRejected;
					continue;
				}

				Actor->Tags.AddUnique(FName(*TagText));
				++InOutApplied;
			}
		}
	}

	static void ApplyAllowedLightProperties(UPointLightComponent* LightComp, const TSharedPtr<FJsonObject>& PropertiesObj, int32& InOutApplied, int32& InOutRejected)
	{
		if (!LightComp || !PropertiesObj.IsValid())
		{
			return;
		}

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : PropertiesObj->Values)
		{
			const FString& Name = Pair.Key;
			const TSharedPtr<FJsonValue>& Value = Pair.Value;

			if (Name.Equals(TEXT("Intensity"), ESearchCase::IgnoreCase))
			{
				double Parsed = 0.0;
				if (TryGetNumeric(Value, Parsed))
				{
					LightComp->SetIntensity((float)Parsed);
					++InOutApplied;
				}
				else
				{
					++InOutRejected;
				}
				continue;
			}

			if (Name.Equals(TEXT("AttenuationRadius"), ESearchCase::IgnoreCase))
			{
				double Parsed = 0.0;
				if (TryGetNumeric(Value, Parsed))
				{
					LightComp->SetAttenuationRadius((float)Parsed);
					++InOutApplied;
				}
				else
				{
					++InOutRejected;
				}
				continue;
			}

			if (Name.Equals(TEXT("SourceRadius"), ESearchCase::IgnoreCase))
			{
				double Parsed = 0.0;
				if (TryGetNumeric(Value, Parsed))
				{
					LightComp->SetSourceRadius((float)Parsed);
					++InOutApplied;
				}
				else
				{
					++InOutRejected;
				}
				continue;
			}

			if (Name.Equals(TEXT("bCastShadows"), ESearchCase::IgnoreCase))
			{
				bool bParsed = false;
				if (TryGetBool(Value, bParsed))
				{
					LightComp->SetCastShadows(bParsed);
					++InOutApplied;
				}
				else
				{
					++InOutRejected;
				}
				continue;
			}

			++InOutRejected;
		}
	}

	static void ApplyAllowedDirectionalLightProperties(UDirectionalLightComponent* LightComp, const TSharedPtr<FJsonObject>& PropertiesObj, int32& InOutApplied, int32& InOutRejected)
	{
		if (!LightComp || !PropertiesObj.IsValid())
		{
			return;
		}

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : PropertiesObj->Values)
		{
			const FString& Name = Pair.Key;
			const TSharedPtr<FJsonValue>& Value = Pair.Value;

			if (Name.Equals(TEXT("Intensity"), ESearchCase::IgnoreCase))
			{
				double Parsed = 0.0;
				if (TryGetNumeric(Value, Parsed))
				{
					LightComp->SetIntensity((float)Parsed);
					++InOutApplied;
				}
				else
				{
					++InOutRejected;
				}
				continue;
			}

			if (Name.Equals(TEXT("LightSourceAngle"), ESearchCase::IgnoreCase))
			{
				double Parsed = 0.0;
				if (TryGetNumeric(Value, Parsed))
				{
					LightComp->SetLightSourceAngle((float)Parsed);
					++InOutApplied;
				}
				else
				{
					++InOutRejected;
				}
				continue;
			}

			if (Name.Equals(TEXT("bCastShadows"), ESearchCase::IgnoreCase))
			{
				bool bParsed = false;
				if (TryGetBool(Value, bParsed))
				{
					LightComp->SetCastShadows(bParsed);
					++InOutApplied;
				}
				else
				{
					++InOutRejected;
				}
				continue;
			}

			++InOutRejected;
		}
	}

	static void ApplyAllowedSkyLightProperties(USkyLightComponent* LightComp, const TSharedPtr<FJsonObject>& PropertiesObj, int32& InOutApplied, int32& InOutRejected)
	{
		if (!LightComp || !PropertiesObj.IsValid())
		{
			return;
		}

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : PropertiesObj->Values)
		{
			const FString& Name = Pair.Key;
			const TSharedPtr<FJsonValue>& Value = Pair.Value;

			if (Name.Equals(TEXT("Intensity"), ESearchCase::IgnoreCase))
			{
				double Parsed = 0.0;
				if (TryGetNumeric(Value, Parsed))
				{
					LightComp->SetIntensity((float)Parsed);
					++InOutApplied;
				}
				else
				{
					++InOutRejected;
				}
				continue;
			}

			++InOutRejected;
		}
	}

	static void ApplyAllowedTextRenderProperties(UTextRenderComponent* TextComp, const TSharedPtr<FJsonObject>& PropertiesObj, int32& InOutApplied, int32& InOutRejected)
	{
		if (!TextComp || !PropertiesObj.IsValid())
		{
			return;
		}

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : PropertiesObj->Values)
		{
			const FString& Name = Pair.Key;
			const TSharedPtr<FJsonValue>& Value = Pair.Value;

			if (Name.Equals(TEXT("Text"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("text"), ESearchCase::IgnoreCase))
			{
				FString Text;
				if (Value.IsValid() && Value->Type == EJson::String && Value->TryGetString(Text))
				{
					TextComp->SetText(FText::FromString(Text));
					++InOutApplied;
				}
				else
				{
					++InOutRejected;
				}
				continue;
			}

			if (Name.Equals(TEXT("WorldSize"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("worldSize"), ESearchCase::IgnoreCase))
			{
				double Parsed = 0.0;
				if (TryGetNumeric(Value, Parsed))
				{
					TextComp->SetWorldSize((float)Parsed);
					++InOutApplied;
				}
				else
				{
					++InOutRejected;
				}
				continue;
			}

			++InOutRejected;
		}
	}

	static void AddOpError(TArray<TSharedPtr<FJsonValue>>& OutErrors, int32 OpIndex, const FString& Error)
	{
		TSharedRef<FJsonObject> Err = MakeShared<FJsonObject>();
		Err->SetNumberField(TEXT("op_index"), OpIndex);
		Err->SetStringField(TEXT("error"), Error);
		OutErrors.Add(MakeShared<FJsonValueObject>(Err));
	}
}

bool FBlueprintAutomationToolkitModule::ExecuteEditorLayoutApply(const TArray<TSharedPtr<FJsonValue>>& Actors, bool bApply, TSharedRef<FJsonObject>& OutResult, TArray<TSharedPtr<FJsonValue>>& OutErrors) const
{
	OutResult->SetStringField(TEXT("op"), TEXT("editor.layout.apply"));

	if (Actors.Num() > MaxActorsPerLayout)
	{
		AddOpError(OutErrors, -1, FString::Printf(TEXT("max_actors_per_layout_exceeded:%d"), MaxActorsPerLayout));
		OutResult->SetBoolField(TEXT("ok"), false);
		return false;
	}

	UWorld* World = nullptr;
	if (bApply)
	{
		if (!GEditor)
		{
			AddOpError(OutErrors, -1, TEXT("no_editor"));
			OutResult->SetBoolField(TEXT("ok"), false);
			return false;
		}
		World = GEditor->GetEditorWorldContext().World();
		if (!World)
		{
			AddOpError(OutErrors, -1, TEXT("no_world"));
			OutResult->SetBoolField(TEXT("ok"), false);
			return false;
		}
	}

	int32 SpawnedActors = 0;
	int32 RejectedActors = 0;
	int32 AppliedProperties = 0;
	int32 RejectedProperties = 0;

	for (int32 ActorIndex = 0; ActorIndex < Actors.Num(); ++ActorIndex)
	{
		const TSharedPtr<FJsonValue>& ActorValue = Actors[ActorIndex];
		if (!ActorValue.IsValid() || ActorValue->Type != EJson::Object)
		{
			++RejectedActors;
			AddOpError(OutErrors, ActorIndex, TEXT("actor_entry_must_be_object"));
			continue;
		}

		const TSharedPtr<FJsonObject> ActorObj = ActorValue->AsObject();
		if (!ActorObj.IsValid())
		{
			++RejectedActors;
			AddOpError(OutErrors, ActorIndex, TEXT("actor_entry_invalid_object"));
			continue;
		}

		FString ClassName;
		if (!ActorObj->TryGetStringField(TEXT("class"), ClassName))
		{
			++RejectedActors;
			AddOpError(OutErrors, ActorIndex, TEXT("missing_class"));
			continue;
		}

		const EBATLayoutActorKind Kind = ResolveLayoutActorKind(ClassName);
		if (Kind == EBATLayoutActorKind::Unsupported)
		{
			++RejectedActors;
			AddOpError(OutErrors, ActorIndex, TEXT("class_not_allowed"));
			continue;
		}

		FVector Location = FVector::ZeroVector;
		FRotator Rotation = FRotator::ZeroRotator;
		FVector Scale = FVector(1.0f, 1.0f, 1.0f);

		const TSharedPtr<FJsonObject>* TransformObjPtr = nullptr;
		if (ActorObj->TryGetObjectField(TEXT("transform"), TransformObjPtr) && TransformObjPtr && TransformObjPtr->IsValid())
		{
			const TSharedPtr<FJsonObject>& TransformObj = *TransformObjPtr;

			if (const TSharedPtr<FJsonValue>* LocationValue = TransformObj->Values.Find(TEXT("location")))
			{
				FVector Parsed;
				if (!TryParseVector3(*LocationValue, Parsed))
				{
					++RejectedActors;
					AddOpError(OutErrors, ActorIndex, TEXT("invalid_transform_location"));
					continue;
				}
				Location = Parsed;
			}

			if (const TSharedPtr<FJsonValue>* RotationValue = TransformObj->Values.Find(TEXT("rotation")))
			{
				FVector Parsed;
				if (!TryParseVector3(*RotationValue, Parsed))
				{
					++RejectedActors;
					AddOpError(OutErrors, ActorIndex, TEXT("invalid_transform_rotation"));
					continue;
				}
				Rotation = FRotator(Parsed.X, Parsed.Y, Parsed.Z);
			}

			if (const TSharedPtr<FJsonValue>* ScaleValue = TransformObj->Values.Find(TEXT("scale")))
			{
				FVector Parsed;
				if (!TryParseVector3(*ScaleValue, Parsed))
				{
					++RejectedActors;
					AddOpError(OutErrors, ActorIndex, TEXT("invalid_transform_scale"));
					continue;
				}
				Scale = Parsed;
			}
		}

		FString Label;
		ActorObj->TryGetStringField(TEXT("label"), Label);

		const TSharedPtr<FJsonObject>* AssetsObjPtr = nullptr;
		ActorObj->TryGetObjectField(TEXT("assets"), AssetsObjPtr);
		const TSharedPtr<FJsonObject>* PropertiesObjPtr = nullptr;
		ActorObj->TryGetObjectField(TEXT("properties"), PropertiesObjPtr);
		const TSharedPtr<FJsonObject> AssetsObj = (AssetsObjPtr && AssetsObjPtr->IsValid()) ? *AssetsObjPtr : nullptr;
		const TSharedPtr<FJsonObject> PropertiesObj = (PropertiesObjPtr && PropertiesObjPtr->IsValid()) ? *PropertiesObjPtr : nullptr;

		if (!bApply)
		{
			if (Kind == EBATLayoutActorKind::StaticMeshActor && AssetsObj.IsValid())
			{
				FString MeshPath;
				if (AssetsObj->TryGetStringField(TEXT("static_mesh"), MeshPath) && !MeshPath.IsEmpty() && !LoadObject<UStaticMesh>(nullptr, *MeshPath))
				{
					++RejectedProperties;
					AddOpError(OutErrors, ActorIndex, TEXT("invalid_static_mesh"));
				}

				FString MaterialPath;
				if (AssetsObj->TryGetStringField(TEXT("material0"), MaterialPath) && !MaterialPath.IsEmpty() && !LoadObject<UMaterialInterface>(nullptr, *MaterialPath))
				{
					++RejectedProperties;
					AddOpError(OutErrors, ActorIndex, TEXT("invalid_material0"));
				}
			}

			if (Kind == EBATLayoutActorKind::PointLight && PropertiesObj.IsValid())
			{
				for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : PropertiesObj->Values)
				{
					const FString& Name = Pair.Key;
					if (Name.Equals(TEXT("Intensity"), ESearchCase::IgnoreCase)
						|| Name.Equals(TEXT("AttenuationRadius"), ESearchCase::IgnoreCase)
						|| Name.Equals(TEXT("SourceRadius"), ESearchCase::IgnoreCase)
						|| Name.Equals(TEXT("bCastShadows"), ESearchCase::IgnoreCase))
					{
						continue;
					}
					++RejectedProperties;
					AddOpError(OutErrors, ActorIndex, TEXT("property_not_allowed"));
				}
			}

			if (Kind == EBATLayoutActorKind::DirectionalLight && PropertiesObj.IsValid())
			{
				for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : PropertiesObj->Values)
				{
					const FString& Name = Pair.Key;
					if (Name.Equals(TEXT("Intensity"), ESearchCase::IgnoreCase)
						|| Name.Equals(TEXT("LightSourceAngle"), ESearchCase::IgnoreCase)
						|| Name.Equals(TEXT("bCastShadows"), ESearchCase::IgnoreCase))
					{
						continue;
					}
					++RejectedProperties;
					AddOpError(OutErrors, ActorIndex, TEXT("property_not_allowed"));
				}
			}

			if (Kind == EBATLayoutActorKind::SkyLight && PropertiesObj.IsValid())
			{
				for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : PropertiesObj->Values)
				{
					if (Pair.Key.Equals(TEXT("Intensity"), ESearchCase::IgnoreCase))
					{
						continue;
					}
					++RejectedProperties;
					AddOpError(OutErrors, ActorIndex, TEXT("property_not_allowed"));
				}
			}

			if (Kind == EBATLayoutActorKind::SkyAtmosphere && PropertiesObj.IsValid())
			{
				RejectedProperties += PropertiesObj->Values.Num();
				AddOpError(OutErrors, ActorIndex, TEXT("properties_not_supported_for_skyatmosphere"));
			}

			if (Kind == EBATLayoutActorKind::TextRenderActor && PropertiesObj.IsValid())
			{
				for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : PropertiesObj->Values)
				{
					const FString& Name = Pair.Key;
					if (Name.Equals(TEXT("Text"), ESearchCase::IgnoreCase)
						|| Name.Equals(TEXT("text"), ESearchCase::IgnoreCase)
						|| Name.Equals(TEXT("WorldSize"), ESearchCase::IgnoreCase)
						|| Name.Equals(TEXT("worldSize"), ESearchCase::IgnoreCase))
					{
						continue;
					}
					++RejectedProperties;
					AddOpError(OutErrors, ActorIndex, TEXT("property_not_allowed"));
				}
			}

			if (Kind == EBATLayoutActorKind::PlayerStart && PropertiesObj.IsValid())
			{
				RejectedProperties += PropertiesObj->Values.Num();
				AddOpError(OutErrors, ActorIndex, TEXT("properties_not_supported_for_playerstart"));
			}

			if (Kind == EBATLayoutActorKind::PcgVolume)
			{
				if (AssetsObj.IsValid())
				{
					for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : AssetsObj->Values)
					{
						if (!Pair.Key.Equals(TEXT("static_mesh"), ESearchCase::IgnoreCase))
						{
							++RejectedProperties;
							AddOpError(OutErrors, ActorIndex, TEXT("property_not_allowed"));
							continue;
						}

						FString MeshPath;
						if (!Pair.Value.IsValid() || Pair.Value->Type != EJson::String || !Pair.Value->TryGetString(MeshPath) || !LoadObject<UStaticMesh>(nullptr, *MeshPath))
						{
							++RejectedProperties;
							AddOpError(OutErrors, ActorIndex, TEXT("invalid_static_mesh"));
						}
					}
				}

				if (PropertiesObj.IsValid())
				{
					for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : PropertiesObj->Values)
					{
						const FString& Name = Pair.Key;
						if (Name.Equals(TEXT("graph"), ESearchCase::IgnoreCase))
						{
							FString GraphPath;
							if (!Pair.Value.IsValid() || Pair.Value->Type != EJson::String || !Pair.Value->TryGetString(GraphPath) || !LoadObject<UPCGGraphInterface>(nullptr, *GraphPath))
							{
								++RejectedProperties;
								AddOpError(OutErrors, ActorIndex, TEXT("invalid_graph"));
							}
							continue;
						}

						if (Name.Equals(TEXT("generate"), ESearchCase::IgnoreCase))
						{
							bool bParsed = false;
							if (!TryGetBool(Pair.Value, bParsed))
							{
								++RejectedProperties;
								AddOpError(OutErrors, ActorIndex, TEXT("invalid_generate"));
							}
							continue;
						}

						++RejectedProperties;
						AddOpError(OutErrors, ActorIndex, TEXT("property_not_allowed"));
					}
				}
			}

			++SpawnedActors;
			continue;
		}

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		if (Kind == EBATLayoutActorKind::StaticMeshActor)
		{
			AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(Location, Rotation, Params);
			if (!Actor || !Actor->GetStaticMeshComponent())
			{
				++RejectedActors;
				AddOpError(OutErrors, ActorIndex, TEXT("spawn_failed"));
				continue;
			}

			Actor->SetActorScale3D(Scale);
			ApplyActorMetadata(Actor, Label, ActorObj, AppliedProperties, RejectedProperties);

			UStaticMeshComponent* MeshComp = Actor->GetStaticMeshComponent();
			if (AssetsObj.IsValid())
			{
				FString MeshPath;
				if (AssetsObj->TryGetStringField(TEXT("static_mesh"), MeshPath))
				{
					if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath))
					{
						MeshComp->SetStaticMesh(Mesh);
					}
					else
					{
						++RejectedProperties;
						AddOpError(OutErrors, ActorIndex, TEXT("invalid_static_mesh"));
					}
				}

				FString MaterialPath;
				if (AssetsObj->TryGetStringField(TEXT("material0"), MaterialPath))
				{
					if (UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, *MaterialPath))
					{
						MeshComp->SetMaterial(0, Mat);
					}
					else
					{
						++RejectedProperties;
						AddOpError(OutErrors, ActorIndex, TEXT("invalid_material0"));
					}
				}
			}

			if (PropertiesObj.IsValid())
			{
				RejectedProperties += PropertiesObj->Values.Num();
				AddOpError(OutErrors, ActorIndex, TEXT("properties_not_supported_for_staticmeshactor"));
			}

			++SpawnedActors;
			continue;
		}

		if (Kind == EBATLayoutActorKind::PointLight)
		{
			APointLight* Actor = World->SpawnActor<APointLight>(Location, Rotation, Params);
			if (!Actor)
			{
				++RejectedActors;
				AddOpError(OutErrors, ActorIndex, TEXT("spawn_failed"));
				continue;
			}

			Actor->SetActorScale3D(Scale);
			ApplyActorMetadata(Actor, Label, ActorObj, AppliedProperties, RejectedProperties);

			if (UPointLightComponent* LightComp = Cast<UPointLightComponent>(Actor->GetLightComponent()))
			{
				ApplyAllowedLightProperties(LightComp, PropertiesObj, AppliedProperties, RejectedProperties);
			}

			++SpawnedActors;
			continue;
		}

		if (Kind == EBATLayoutActorKind::DirectionalLight)
		{
			ADirectionalLight* Actor = World->SpawnActor<ADirectionalLight>(Location, Rotation, Params);
			if (!Actor)
			{
				++RejectedActors;
				AddOpError(OutErrors, ActorIndex, TEXT("spawn_failed"));
				continue;
			}

			Actor->SetActorScale3D(Scale);
			ApplyActorMetadata(Actor, Label, ActorObj, AppliedProperties, RejectedProperties);

			if (UDirectionalLightComponent* LightComp = Cast<UDirectionalLightComponent>(Actor->GetLightComponent()))
			{
				ApplyAllowedDirectionalLightProperties(LightComp, PropertiesObj, AppliedProperties, RejectedProperties);
			}

			++SpawnedActors;
			continue;
		}

		if (Kind == EBATLayoutActorKind::SkyLight)
		{
			ASkyLight* Actor = World->SpawnActor<ASkyLight>(Location, Rotation, Params);
			if (!Actor)
			{
				++RejectedActors;
				AddOpError(OutErrors, ActorIndex, TEXT("spawn_failed"));
				continue;
			}

			Actor->SetActorScale3D(Scale);
			ApplyActorMetadata(Actor, Label, ActorObj, AppliedProperties, RejectedProperties);

			if (USkyLightComponent* LightComp = Actor->GetLightComponent())
			{
				ApplyAllowedSkyLightProperties(LightComp, PropertiesObj, AppliedProperties, RejectedProperties);
			}

			++SpawnedActors;
			continue;
		}

		if (Kind == EBATLayoutActorKind::SkyAtmosphere)
		{
			ASkyAtmosphere* Actor = World->SpawnActor<ASkyAtmosphere>(Location, Rotation, Params);
			if (!Actor)
			{
				++RejectedActors;
				AddOpError(OutErrors, ActorIndex, TEXT("spawn_failed"));
				continue;
			}

			Actor->SetActorScale3D(Scale);
			ApplyActorMetadata(Actor, Label, ActorObj, AppliedProperties, RejectedProperties);
			if (PropertiesObj.IsValid())
			{
				RejectedProperties += PropertiesObj->Values.Num();
				AddOpError(OutErrors, ActorIndex, TEXT("properties_not_supported_for_skyatmosphere"));
			}

			++SpawnedActors;
			continue;
		}

		if (Kind == EBATLayoutActorKind::PlayerStart)
		{
			APlayerStart* Actor = World->SpawnActor<APlayerStart>(Location, Rotation, Params);
			if (!Actor)
			{
				++RejectedActors;
				AddOpError(OutErrors, ActorIndex, TEXT("spawn_failed"));
				continue;
			}

			Actor->SetActorScale3D(Scale);
			ApplyActorMetadata(Actor, Label, ActorObj, AppliedProperties, RejectedProperties);
			if (PropertiesObj.IsValid())
			{
				RejectedProperties += PropertiesObj->Values.Num();
				AddOpError(OutErrors, ActorIndex, TEXT("properties_not_supported_for_playerstart"));
			}

			++SpawnedActors;
			continue;
		}

		if (Kind == EBATLayoutActorKind::TextRenderActor)
		{
			ATextRenderActor* Actor = World->SpawnActor<ATextRenderActor>(Location, Rotation, Params);
			if (!Actor)
			{
				++RejectedActors;
				AddOpError(OutErrors, ActorIndex, TEXT("spawn_failed"));
				continue;
			}

			Actor->SetActorScale3D(Scale);
			ApplyActorMetadata(Actor, Label, ActorObj, AppliedProperties, RejectedProperties);

			if (UTextRenderComponent* TextComp = Actor->GetTextRender())
			{
				ApplyAllowedTextRenderProperties(TextComp, PropertiesObj, AppliedProperties, RejectedProperties);
			}

			++SpawnedActors;
			continue;
		}

		APCGVolume* Volume = World->SpawnActor<APCGVolume>(Location, Rotation, Params);
		if (!Volume)
		{
			++RejectedActors;
			AddOpError(OutErrors, ActorIndex, TEXT("spawn_failed"));
			continue;
		}

		Volume->SetActorScale3D(Scale);
		ApplyActorMetadata(Volume, Label, ActorObj, AppliedProperties, RejectedProperties);

		UPCGComponent* PCGComponent = Volume->FindComponentByClass<UPCGComponent>();
		if (!PCGComponent)
		{
			++RejectedProperties;
			AddOpError(OutErrors, ActorIndex, TEXT("pcg_component_missing"));
			++SpawnedActors;
			continue;
		}

		FString GraphPath;
		bool bHasExplicitGraph = false;
		bool bGenerate = true;
		if (PropertiesObj.IsValid())
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : PropertiesObj->Values)
			{
				const FString& Name = Pair.Key;
				if (Name.Equals(TEXT("graph"), ESearchCase::IgnoreCase))
				{
					if (!Pair.Value.IsValid() || Pair.Value->Type != EJson::String || !Pair.Value->TryGetString(GraphPath))
					{
						++RejectedProperties;
						AddOpError(OutErrors, ActorIndex, TEXT("invalid_graph"));
					}
					else
					{
						bHasExplicitGraph = true;
					}
					continue;
				}

				if (Name.Equals(TEXT("generate"), ESearchCase::IgnoreCase))
				{
					if (TryGetBool(Pair.Value, bGenerate))
					{
						++AppliedProperties;
					}
					else
					{
						++RejectedProperties;
						AddOpError(OutErrors, ActorIndex, TEXT("invalid_generate"));
					}
					continue;
				}

				++RejectedProperties;
				AddOpError(OutErrors, ActorIndex, TEXT("property_not_allowed"));
			}
		}

		UPCGGraphInterface* GraphInterface = bHasExplicitGraph
			? LoadObject<UPCGGraphInterface>(nullptr, *GraphPath)
			: EnsureDefaultPcgGraphAsset();
		if (!GraphInterface)
		{
			++RejectedProperties;
			AddOpError(OutErrors, ActorIndex, TEXT("invalid_graph"));
			++SpawnedActors;
			continue;
		}

		if (AssetsObj.IsValid())
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : AssetsObj->Values)
			{
				if (!Pair.Key.Equals(TEXT("static_mesh"), ESearchCase::IgnoreCase))
				{
					++RejectedProperties;
					AddOpError(OutErrors, ActorIndex, TEXT("property_not_allowed"));
					continue;
				}

				FString MeshPath;
				if (!Pair.Value.IsValid() || Pair.Value->Type != EJson::String || !Pair.Value->TryGetString(MeshPath))
				{
					++RejectedProperties;
					AddOpError(OutErrors, ActorIndex, TEXT("invalid_static_mesh"));
					continue;
				}

				if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath))
				{
					if (UPCGGraph* MutableGraph = GraphInterface->GetMutablePCGGraph())
					{
						ApplyMeshesToStaticMeshSpawners(MutableGraph, { TSoftObjectPtr<UStaticMesh>(Mesh) });
						++AppliedProperties;
					}
				}
				else
				{
					++RejectedProperties;
					AddOpError(OutErrors, ActorIndex, TEXT("invalid_static_mesh"));
				}
			}
		}

		PCGComponent->bActivated = true;
		PCGComponent->SetGraph(GraphInterface);
		++AppliedProperties;

		if (bGenerate)
		{
			PCGComponent->GenerateLocal(/*bForce*/ true);
		}

		++SpawnedActors;
	}

	if (bApply && World)
	{
		World->MarkPackageDirty();
		if (GEditor)
		{
			GEditor->RedrawLevelEditingViewports(true);
		}
	}

	OutResult->SetBoolField(TEXT("ok"), OutErrors.Num() == 0);
	OutResult->SetNumberField(TEXT("created"), SpawnedActors);
	OutResult->SetNumberField(TEXT("spawned_actors"), SpawnedActors);
	OutResult->SetNumberField(TEXT("rejected_actors"), RejectedActors);
	OutResult->SetNumberField(TEXT("applied_properties"), AppliedProperties);
	OutResult->SetNumberField(TEXT("rejected_properties"), RejectedProperties);
	return OutErrors.Num() == 0;
}

void FBlueprintAutomationToolkitModule::BindEditorRoutes()
{
	EditorMapRoute = Router->BindRoute(
		FHttpPath(TEXT("/ai/editor/map")),
		EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, TEXT("/ai/editor/map")))
			{
				return true;
			}

			FString PackageName;
			FString WorldName;
			bool bHasWorld = false;
			const bool bRan = RunOnGameThreadWait([this, &PackageName, &WorldName, &bHasWorld]()
			{
				UWorld* World = GetEditorWorld();
				if (!World)
				{
					return;
				}
				UPackage* Package = World->GetOutermost();
				PackageName = Package ? Package->GetName() : FString();
				WorldName = World->GetName();
				bHasWorld = true;
			});
			if (!bRan || !bHasWorld)
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::ServerError, TEXT("no_world"), TEXT("Editor world not available")));
				return true;
			}

			TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetBoolField(TEXT("ok"), true);
			Obj->SetStringField(TEXT("world"), TEXT("editor"));
			Obj->SetStringField(TEXT("map_package"), PackageName);
			Obj->SetStringField(TEXT("world_object"), WorldName);
			const FString RequestId = ResolveOrCreateRequestId(Request);
			OnComplete(BAT::Http::MakeJsonResponseFromString(static_cast<int32>(EHttpServerResponseCodes::Ok), ToJsonString(Obj), RequestId));
			return true;
		}));

	EditorQuitRoute = Router->BindRoute(
		FHttpPath(TEXT("/ai/editor/quit")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, TEXT("/ai/editor/quit")))
			{
				return true;
			}

			const FString RequestId = ResolveOrCreateRequestId(Request);
			const bool bCompleted = RunOnGameThreadWait([]()
			{
				FPlatformMisc::RequestExit(false);
			}, 10.0);

			if (!bCompleted)
			{
				OnComplete(MakeErrorResponse(504, RequestId, TEXT("game_thread_timeout"), TEXT("Timed out waiting for GameThread execution")));
				return true;
			}

			TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetBoolField(TEXT("ok"), true);
			Obj->SetBoolField(TEXT("exit_requested"), true);
			OnComplete(MakeJsonResponse(EHttpServerResponseCodes::Ok, ToJsonString(Obj)));
			return true;
		}));

	EditorLayoutApplyRoute = Router->BindRoute(
		FHttpPath(TEXT("/ai/editor/layout/apply")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, TEXT("/ai/editor/layout/apply")))
			{
				return true;
			}

			TSharedPtr<FJsonObject> BodyObj;
			if (!TryParseJsonBody(Request.Body, BodyObj) || !BodyObj.IsValid())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("invalid_json"), TEXT("Request body must be a valid JSON object")));
				return true;
			}

			const TArray<TSharedPtr<FJsonValue>>* ActorsArray = nullptr;
			if (!BodyObj->TryGetArrayField(TEXT("actors"), ActorsArray) || !ActorsArray)
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("missing_actors"), TEXT("Body must include 'actors' array")));
				return true;
			}

			TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
			Payload->SetArrayField(TEXT("actors"), *ActorsArray);

			const FString RequestId = ResolveOrCreateRequestId(Request);
			const FString JobId = SubmitJob(TEXT("editor.layout.apply"), RequestId, Payload);

			TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("jobId"), JobId);
			Obj->SetStringField(TEXT("requestId"), RequestId);
			OnComplete(BAT::Http::MakeJsonResponseFromString(static_cast<int32>(EHttpServerResponseCodes::Accepted), ToJsonString(Obj), RequestId));

			return true;
		}));

	JobsSubmitRoute = Router->BindRoute(
		FHttpPath(TEXT("/jobs/submit")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, TEXT("/jobs/submit")))
			{
				return true;
			}

			TSharedPtr<FJsonObject> BodyObj;
			if (!TryParseJsonBody(Request.Body, BodyObj) || !BodyObj.IsValid())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("invalid_json"), TEXT("Request body must be a valid JSON object")));
				return true;
			}

			FString Kind;
			if (!BodyObj->TryGetStringField(TEXT("kind"), Kind) || Kind.TrimStartAndEnd().IsEmpty())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("missing_kind"), TEXT("Body must include non-empty 'kind'")));
				return true;
			}

			const FString RequestId = ResolveOrCreateRequestId(Request);
			const FString IdempotencyKey = ReadHeaderValueCaseInsensitive(Request.Headers, TEXT("Idempotency-Key"));
			const FString ScopedIdempotencyKey = IdempotencyKey.IsEmpty() ? FString() : FString::Printf(TEXT("/jobs/submit:%s"), *IdempotencyKey);
			if (!ScopedIdempotencyKey.IsEmpty())
			{
				FScopeLock Lock(&JobMutex);
				if (const FString* ExistingJobId = IdempotencyToJob.Find(ScopedIdempotencyKey))
				{
					TSharedRef<FJsonObject> Cached = MakeShared<FJsonObject>();
					Cached->SetStringField(TEXT("jobId"), *ExistingJobId);
					Cached->SetBoolField(TEXT("idempotent_replay"), true);
					OnComplete(BAT::Http::MakeJsonResponseFromString(static_cast<int32>(EHttpServerResponseCodes::Accepted), ToJsonString(Cached), RequestId));
					return true;
				}
			}

			const TSharedPtr<FJsonObject>* PayloadPtr = nullptr;
			if (!BodyObj->TryGetObjectField(TEXT("payload"), PayloadPtr) || !PayloadPtr || !PayloadPtr->IsValid())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("missing_payload"), TEXT("Body must include object field 'payload'")));
				return true;
			}

			const FString JobId = SubmitJob(Kind, RequestId, *PayloadPtr);
			if (!ScopedIdempotencyKey.IsEmpty())
			{
				FScopeLock Lock(&JobMutex);
				IdempotencyToJob.Add(ScopedIdempotencyKey, JobId);
			}

			TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("jobId"), JobId);
			Obj->SetStringField(TEXT("requestId"), RequestId);
			OnComplete(BAT::Http::MakeJsonResponseFromString(static_cast<int32>(EHttpServerResponseCodes::Accepted), ToJsonString(Obj), RequestId));
			return true;
		}));

	JobGetRoute = Router->BindRoute(
		FHttpPath(TEXT("/jobs/:jobId")),
		EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, TEXT("/jobs/get")))
			{
				return true;
			}

			const FString* JobIdPtr = Request.PathParams.Find(TEXT("jobId"));
			if (!JobIdPtr || JobIdPtr->IsEmpty())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("missing_job_id"), TEXT("Path requires jobId")));
				return true;
			}

			FJobRecord Job;
			if (!TryGetJob(*JobIdPtr, Job))
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::NotFound, TEXT("job_not_found"), TEXT("Job not found")));
				return true;
			}

			auto StateToString = [](EJobState State) -> FString
			{
				switch (State)
				{
				case EJobState::Queued: return TEXT("queued");
				case EJobState::Running: return TEXT("running");
				case EJobState::Succeeded: return TEXT("succeeded");
				case EJobState::Failed: return TEXT("failed");
				case EJobState::Canceled: return TEXT("canceled");
				default: return TEXT("unknown");
				}
			};

			TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("jobId"), Job.JobId);
			Obj->SetStringField(TEXT("state"), StateToString(Job.State));
			Obj->SetNumberField(TEXT("progress"), Job.Progress);
			Obj->SetStringField(TEXT("requestId"), Job.RequestId);
			if (Job.Result.IsValid())
			{
				Obj->SetObjectField(TEXT("result"), Job.Result.ToSharedRef());
			}
			if (Job.Error.IsValid())
			{
				Obj->SetObjectField(TEXT("error"), Job.Error.ToSharedRef());
			}

			TArray<TSharedPtr<FJsonValue>> Logs;
			for (const FString& Line : Job.Logs)
			{
				Logs.Add(MakeShared<FJsonValueString>(Line));
			}
			Obj->SetArrayField(TEXT("logs"), Logs);
			OnComplete(MakeJsonResponse(EHttpServerResponseCodes::Ok, ToJsonString(Obj)));
			return true;
		}));

	JobCancelRoute = Router->BindRoute(
		FHttpPath(TEXT("/jobs/:jobId/cancel")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, TEXT("/jobs/cancel")))
			{
				return true;
			}

			const FString* JobIdPtr = Request.PathParams.Find(TEXT("jobId"));
			if (!JobIdPtr || JobIdPtr->IsEmpty())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("missing_job_id"), TEXT("Path requires jobId")));
				return true;
			}

			if (!CancelJob(*JobIdPtr))
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::NotFound, TEXT("job_not_found"), TEXT("Job not found")));
				return true;
			}

			TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetBoolField(TEXT("ok"), true);
			Obj->SetStringField(TEXT("jobId"), *JobIdPtr);
			Obj->SetBoolField(TEXT("cancel_requested"), true);
			OnComplete(MakeJsonResponse(EHttpServerResponseCodes::Ok, ToJsonString(Obj)));
			return true;
		}));

	LogsTailRoute = Router->BindRoute(
		FHttpPath(TEXT("/logs/tail")),
		EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, TEXT("/logs/tail")))
			{
				return true;
			}

			int32 N = 200;
			if (const FString* NParam = Request.QueryParams.Find(TEXT("n")))
			{
				N = FMath::Clamp(FCString::Atoi(**NParam), 1, 2000);
			}

			const TArray<FStructuredLogEntry> Tail = GetRecentLogs(N);
			TArray<TSharedPtr<FJsonValue>> Items;
			for (const FStructuredLogEntry& E : Tail)
			{
				TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("timestamp"), E.TimestampUtc.ToIso8601());
				Entry->SetStringField(TEXT("requestId"), E.RequestId);
				Entry->SetStringField(TEXT("route"), E.Route);
				Entry->SetStringField(TEXT("subject"), E.Subject);
				Entry->SetNumberField(TEXT("durationMs"), E.DurationMs);
				Entry->SetNumberField(TEXT("status"), E.Status);
				Entry->SetStringField(TEXT("error"), E.ErrorCode);
				Items.Add(MakeShared<FJsonValueObject>(Entry));
			}

			TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetArrayField(TEXT("items"), Items);
			OnComplete(MakeJsonResponse(EHttpServerResponseCodes::Ok, ToJsonString(Obj)));
			return true;
		}));

	PlanValidateRoute = Router->BindRoute(
		FHttpPath(TEXT("/ai/plan/validate")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, TEXT("/ai/plan/validate")))
			{
				return true;
			}

			TSharedPtr<FJsonObject> BodyObj;
			if (!TryParseJsonBody(Request.Body, BodyObj) || !BodyObj.IsValid())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("invalid_json"), TEXT("Request body must be a valid JSON object")));
				return true;
			}

			const TArray<TSharedPtr<FJsonValue>>* OpsArray = nullptr;
			if (!BodyObj->TryGetArrayField(TEXT("ops"), OpsArray) || !OpsArray)
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("missing_ops"), TEXT("Body must include 'ops' array")));
				return true;
			}

			const TArray<TSharedPtr<FJsonValue>> Ops = *OpsArray;
			const FString RequestId = ResolveOrCreateRequestId(Request);
			TSharedRef<TAtomic<bool>> bResponded = MakeShared<TAtomic<bool>>(false);
			FHttpResultCallback Complete = [OnComplete, bResponded](TUniquePtr<FHttpServerResponse> Response) mutable
			{
				if (!bResponded->Exchange(true))
				{
					OnComplete(MoveTemp(Response));
				}
			};
			const bool bCompleted = RunOnGameThreadWait([this, Complete, Ops, RequestId]()
			{
				TArray<TSharedPtr<FJsonValue>> Errors;
				int32 TotalInstances = 0;

				if (Ops.Num() > MaxOpsPerPlan)
				{
					AddOpError(Errors, -1, FString::Printf(TEXT("max_ops_per_plan_exceeded:%d"), MaxOpsPerPlan));
				}

				for (int32 OpIndex = 0; OpIndex < Ops.Num(); ++OpIndex)
				{
					if (!Ops[OpIndex].IsValid() || Ops[OpIndex]->Type != EJson::Object)
					{
						AddOpError(Errors, OpIndex, TEXT("op_entry_must_be_object"));
						continue;
					}

					const TSharedPtr<FJsonObject> OpObj = Ops[OpIndex]->AsObject();
					FString OpName;
					if (!OpObj.IsValid() || !OpObj->TryGetStringField(TEXT("op"), OpName) || OpName.IsEmpty())
					{
						AddOpError(Errors, OpIndex, TEXT("missing_op"));
						continue;
					}

					const TSharedPtr<FJsonObject>* PayloadObjPtr = nullptr;
					if (!OpObj->TryGetObjectField(TEXT("payload"), PayloadObjPtr) || !PayloadObjPtr || !PayloadObjPtr->IsValid())
					{
						AddOpError(Errors, OpIndex, TEXT("missing_payload_object"));
						continue;
					}

					TSharedRef<FJsonObject> OpResult = MakeShared<FJsonObject>();
					TArray<TSharedPtr<FJsonValue>> OpErrors;
					if (OpName.Equals(TEXT("editor.layout.apply"), ESearchCase::CaseSensitive))
					{
						const TArray<TSharedPtr<FJsonValue>>* ActorsArray = nullptr;
						if (!(*PayloadObjPtr)->TryGetArrayField(TEXT("actors"), ActorsArray) || !ActorsArray)
						{
							AddOpError(Errors, OpIndex, TEXT("missing_actors"));
							continue;
						}
						ExecuteEditorLayoutApply(*ActorsArray, false, OpResult, OpErrors);
					}
					else if (OpName.Equals(TEXT("blueprint.apply"), ESearchCase::CaseSensitive))
					{
						ExecuteBlueprintPatch(*PayloadObjPtr, false, TotalInstances, OpResult, OpErrors);
					}
					else
					{
						AddOpError(Errors, OpIndex, TEXT("unsupported_op"));
						continue;
					}

					for (const TSharedPtr<FJsonValue>& Err : OpErrors)
					{
						if (!Err.IsValid() || Err->Type != EJson::Object)
						{
							continue;
						}
						const TSharedPtr<FJsonObject> ErrObj = Err->AsObject();
						if (!ErrObj.IsValid())
						{
							continue;
						}
						ErrObj->SetNumberField(TEXT("op_index"), OpIndex);
						Errors.Add(MakeShared<FJsonValueObject>(ErrObj));
					}
				}

				TSharedRef<FJsonObject> ResponseObj = MakeShared<FJsonObject>();
				ResponseObj->SetBoolField(TEXT("ok"), Errors.Num() == 0);
				ResponseObj->SetArrayField(TEXT("errors"), Errors);
				Complete(BAT::Http::MakeJsonResponseFromString(static_cast<int32>(EHttpServerResponseCodes::Ok), ToJsonString(ResponseObj), RequestId));
			}, 10.0);

			if (!bCompleted && !bResponded->Exchange(true))
			{
				OnComplete(MakeErrorResponse(504, RequestId, TEXT("game_thread_timeout"), TEXT("Timed out waiting for GameThread execution")));
			}
			return true;
		}));

	PlanApplyRoute = Router->BindRoute(
		FHttpPath(TEXT("/ai/plan/apply")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, TEXT("/ai/plan/apply")))
			{
				return true;
			}

			TSharedPtr<FJsonObject> BodyObj;
			if (!TryParseJsonBody(Request.Body, BodyObj) || !BodyObj.IsValid())
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("invalid_json"), TEXT("Request body must be a valid JSON object")));
				return true;
			}

			const TArray<TSharedPtr<FJsonValue>>* OpsArray = nullptr;
			if (!BodyObj->TryGetArrayField(TEXT("ops"), OpsArray) || !OpsArray)
			{
				OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("missing_ops"), TEXT("Body must include 'ops' array")));
				return true;
			}

			TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
			Payload->SetArrayField(TEXT("ops"), *OpsArray);

			const FString RequestId = ResolveOrCreateRequestId(Request);
			const FString IdempotencyKey = ReadHeaderValueCaseInsensitive(Request.Headers, TEXT("Idempotency-Key"));
			const FString ScopedIdempotencyKey = IdempotencyKey.IsEmpty() ? FString() : FString::Printf(TEXT("/ai/plan/apply:%s"), *IdempotencyKey);
			if (!ScopedIdempotencyKey.IsEmpty())
			{
				FScopeLock Lock(&JobMutex);
				if (const FString* ExistingJobId = IdempotencyToJob.Find(ScopedIdempotencyKey))
				{
					TSharedRef<FJsonObject> Cached = MakeShared<FJsonObject>();
					Cached->SetStringField(TEXT("jobId"), *ExistingJobId);
					Cached->SetStringField(TEXT("requestId"), RequestId);
					Cached->SetBoolField(TEXT("idempotent_replay"), true);
					OnComplete(BAT::Http::MakeJsonResponseFromString(static_cast<int32>(EHttpServerResponseCodes::Accepted), ToJsonString(Cached), RequestId));
					return true;
				}
			}
			const FString JobId = SubmitJob(TEXT("plan.apply"), RequestId, Payload);
			if (!ScopedIdempotencyKey.IsEmpty())
			{
				FScopeLock Lock(&JobMutex);
				IdempotencyToJob.Add(ScopedIdempotencyKey, JobId);
			}

			TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("jobId"), JobId);
			Obj->SetStringField(TEXT("requestId"), RequestId);
			OnComplete(BAT::Http::MakeJsonResponseFromString(static_cast<int32>(EHttpServerResponseCodes::Accepted), ToJsonString(Obj), RequestId));

			return true;
		}));
}

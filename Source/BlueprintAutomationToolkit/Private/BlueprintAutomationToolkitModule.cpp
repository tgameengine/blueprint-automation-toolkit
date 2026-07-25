// Copyright 2026 AkaSoft. All Rights Reserved.

#include "BlueprintAutomationToolkitModule.h"

#include "Auth/TokenAuthMiddleware.h"
#include "BlueprintAutomationToolkitSettings.h"
#include "Commands/Actor/SpawnActorCommand.h"
#include "Commands/Actor/DestroyActorCommand.h"
#include "Commands/Blueprint/ApplyGraphCommand.h"
#include "Commands/Blueprint/CompileSaveBlueprintCommand.h"
#include "Commands/Blueprint/ReadGraphCommand.h"
#include "Commands/CommandDispatcher.h"
#include "Commands/Editor/FocusEditorTargetCommand.h"
#include "Commands/Editor/LevelEditorCommands.h"
#include "Commands/Editor/SelectEditorTargetCommand.h"
#include "Commands/Material/SetMaterialTextureSamplesCommand.h"
#include "Commands/Reflection/CallFunctionCommand.h"
#include "Commands/Reflection/DescribeObjectCommand.h"
#include "Commands/Reflection/GetObjectCommand.h"
#include "Commands/Reflection/ListFunctionsCommand.h"
#include "Commands/Reflection/ListPropertiesCommand.h"
#include "Commands/Reflection/SetPropertyCommand.h"
#include "Commands/Object/CallObjectFunctionCommand.h"
#include "Commands/Object/SetObjectPropertyCommand.h"
#include "Http/HttpRequestUtils.h"
#include "Services/ObjectAutomationService.h"

#include "Async/Async.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "CoreGlobals.h"
#include "Containers/Ticker.h"
#include "Containers/UnrealString.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/PlayerController.h"
#include "HttpPath.h"
#include "HttpResultCallback.h"
#include "HttpServerModule.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"
#include "Interfaces/IPluginManager.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "EngineUtils.h"
#include "InputCoreTypes.h"
#include "LevelEditor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Base64.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Guid.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Misc/DateTime.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "HAL/PlatformTime.h"
#include "InputKeyEventArgs.h"
#include "Misc/EngineVersionComparison.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Docking/SDockTab.h"
#include "ToolMenus.h"
#include "TimerManager.h"
#include "Tests/AutomationEditorCommon.h"
#include "FileHelpers.h"
#include "Factories/WorldFactory.h"
#include "UObject/SavePackage.h"
#include "Engine/DirectionalLight.h"
#include "Engine/GameViewportClient.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Engine.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "GameFramework/PlayerStart.h"
#include "Components/StaticMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/CollisionProfile.h"
#include "Materials/MaterialInterface.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/TopLevelAssetPath.h"
#include "UObject/UnrealType.h"
#include "UObject/StructOnScope.h"
#include "PCGComponent.h"
#include "PCGVolume.h"
#include "PCGGraph.h"
#include "Elements/PCGStaticMeshSpawner.h"
#include "MeshSelectors/PCGMeshSelectorWeighted.h"
#include "MeshSelectors/PCGMeshSelectorWeightedByCategory.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_IfThenElse.h"

#include "HAL/PlatformMisc.h"

#include "Subsystems/AssetEditorSubsystem.h"



#include "UDynamicMesh.h"

#include "Components/DynamicMeshComponent.h"

#include "VectorTypes.h"

DEFINE_LOG_CATEGORY(LogBlueprintAutomationToolkit);

namespace
{
	static const FName BATControlPanelTabName(TEXT("BlueprintAutomationToolkit"));

	static FString GetProjectDefaultEditorIniPath()
	{
		return FConfigCacheIni::NormalizeConfigIniPath(FPaths::Combine(FPaths::ProjectConfigDir(), TEXT("DefaultEditor.ini")));
	}

	static FString DescribeTokenForLog(const FString& Token)
	{
		return Token.IsEmpty() ? TEXT("empty") : FString::Printf(TEXT("present(len=%d)"), Token.Len());
	}

	enum class EProjectAuthTokenWriteMode : uint8
	{
		Preserve,
		Set,
		Clear,
	};

	static void ReadProjectAuthSettingsFromDisk(const FString& IniPath, FString& OutToken, bool& bOutSaveToProject)
	{
		OutToken.Reset();
		bOutSaveToProject = false;

		if (!IFileManager::Get().FileExists(*IniPath))
		{
			return;
		}

		FConfigFile ConfigFile;
		ConfigFile.Read(IniPath);

		ConfigFile.GetString(TEXT("BlueprintAutomationToolkit"), TEXT("AuthToken"), OutToken);
		ConfigFile.GetBool(TEXT("BlueprintAutomationToolkit"), TEXT("bSaveTokenInProjectSettings"), bOutSaveToProject);
	}

	static bool WriteProjectAuthSettingsToDisk(
		const FString& IniPath,
		const bool bSaveToProject,
		const EProjectAuthTokenWriteMode AuthTokenWriteMode,
		const FString& TokenValue)
	{
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(IniPath), true);

		FConfigFile ConfigFile;
		ConfigFile.Read(IniPath);
		ConfigFile.SetBool(TEXT("BlueprintAutomationToolkit"), TEXT("bSaveTokenInProjectSettings"), bSaveToProject);

		switch (AuthTokenWriteMode)
		{
		case EProjectAuthTokenWriteMode::Set:
			ConfigFile.SetString(TEXT("BlueprintAutomationToolkit"), TEXT("AuthToken"), *TokenValue);
			break;
		case EProjectAuthTokenWriteMode::Clear:
			ConfigFile.SetString(TEXT("BlueprintAutomationToolkit"), TEXT("AuthToken"), TEXT(""));
			break;
		case EProjectAuthTokenWriteMode::Preserve:
		default:
			break;
		}

		return ConfigFile.Write(IniPath);
	}

	// -----------------------------------------------------------------------------
	// Functional Module: Map Save Helpers
	// -----------------------------------------------------------------------------
	static bool TrySaveCurrentEditorMap(FOutputDevice& Ar);

	// -----------------------------------------------------------------------------
	// Functional Module: Reflection Helpers
	// These keep BlueprintAutomationToolkit independent of GeometryMathMesh at build time.
	// -----------------------------------------------------------------------------
	static bool TrySetBoolProp(UObject* Obj, const FName PropName, const bool bValue)
	{
		if (!Obj)
		{
			return false;
		}
		if (FBoolProperty* P = FindFProperty<FBoolProperty>(Obj->GetClass(), PropName))
		{
			P->SetPropertyValue_InContainer(Obj, bValue);
			return true;
		}
		return false;
	}

	static bool TryGetBoolProp(UObject* Obj, const FName PropName, bool& OutValue)
	{
		OutValue = false;
		if (!Obj)
		{
			return false;
		}
		if (FBoolProperty* P = FindFProperty<FBoolProperty>(Obj->GetClass(), PropName))
		{
			OutValue = P->GetPropertyValue_InContainer(Obj);
			return true;
		}
		return false;
	}

	static bool TrySetNumericProp(UObject* Obj, const FName PropName, const double Value)
	{
		if (!Obj)
		{
			return false;
		}
		FProperty* Prop = Obj->GetClass()->FindPropertyByName(PropName);
		if (!Prop)
		{
			return false;
		}
		FNumericProperty* Num = CastField<FNumericProperty>(Prop);
		if (!Num)
		{
			return false;
		}
		void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Obj);
		if (Num->IsInteger())
		{
			Num->SetIntPropertyValue(ValuePtr, (int64)Value);
		}
		else
		{
			Num->SetFloatingPointPropertyValue(ValuePtr, Value);
		}
		return true;
	}

	static bool TryGetNumericProp(UObject* Obj, const FName PropName, double& OutValue)
	{
		OutValue = 0.0;
		if (!Obj)
		{
			return false;
		}
		FProperty* Prop = Obj->GetClass()->FindPropertyByName(PropName);
		if (!Prop)
		{
			return false;
		}
		FNumericProperty* Num = CastField<FNumericProperty>(Prop);
		if (!Num)
		{
			return false;
		}
		void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Obj);
		OutValue = Num->IsInteger() ? (double)Num->GetSignedIntPropertyValue(ValuePtr) : Num->GetFloatingPointPropertyValue(ValuePtr);
		return true;
	}

	static bool TrySetStructNumericField(UObject* Obj, const FName StructPropName, const FName FieldName, const double Value)
	{
		if (!Obj)
		{
			return false;
		}
		FStructProperty* StructProp = FindFProperty<FStructProperty>(Obj->GetClass(), StructPropName);
		if (!StructProp || !StructProp->Struct)
		{
			return false;
		}
		void* StructPtr = StructProp->ContainerPtrToValuePtr<void>(Obj);
		FProperty* FieldProp = StructProp->Struct->FindPropertyByName(FieldName);
		if (!FieldProp)
		{
			return false;
		}
		FNumericProperty* Num = CastField<FNumericProperty>(FieldProp);
		if (!Num)
		{
			return false;
		}
		void* FieldPtr = FieldProp->ContainerPtrToValuePtr<void>(StructPtr);
		if (Num->IsInteger())
		{
			Num->SetIntPropertyValue(FieldPtr, (int64)Value);
		}
		else
		{
			Num->SetFloatingPointPropertyValue(FieldPtr, Value);
		}
		return true;
	}

	static bool TrySetStructEnumFieldByName(UObject* Obj, const FName StructPropName, const FName FieldName, const FString& EnumValueName)
	{
		if (!Obj)
		{
			return false;
		}
		FStructProperty* StructProp = FindFProperty<FStructProperty>(Obj->GetClass(), StructPropName);
		if (!StructProp || !StructProp->Struct)
		{
			return false;
		}
		void* StructPtr = StructProp->ContainerPtrToValuePtr<void>(Obj);
		FProperty* FieldProp = StructProp->Struct->FindPropertyByName(FieldName);
		if (!FieldProp)
		{
			return false;
		}

		if (FEnumProperty* EnumProp = CastField<FEnumProperty>(FieldProp))
		{
			UEnum* Enum = EnumProp->GetEnum();
			FNumericProperty* Underlying = EnumProp->GetUnderlyingProperty();
			if (!Enum || !Underlying)
			{
				return false;
			}
			const int64 Value = Enum->GetValueByNameString(EnumValueName);
			if (Value == INDEX_NONE)
			{
				return false;
			}
			void* FieldPtr = FieldProp->ContainerPtrToValuePtr<void>(StructPtr);
			Underlying->SetIntPropertyValue(FieldPtr, Value);
			return true;
		}
		if (FByteProperty* ByteProp = CastField<FByteProperty>(FieldProp))
		{
			UEnum* Enum = ByteProp->GetIntPropertyEnum();
			if (!Enum)
			{
				return false;
			}
			const int64 Value = Enum->GetValueByNameString(EnumValueName);
			if (Value == INDEX_NONE)
			{
				return false;
			}
			ByteProp->SetPropertyValue_InContainer(StructPtr, (uint8)Value);
			return true;
		}

		return false;
	}

	static void CallNoArgUFunctionIfPresent(UObject* Obj, const FName FunctionName)
	{
		if (!Obj)
		{
			return;
		}
		UFunction* Func = Obj->FindFunction(FunctionName);
		if (!Func)
		{
			return;
		}
		FStructOnScope Params(Func);
		Obj->ProcessEvent(Func, Params.GetStructMemory());
	}

	static UDynamicMeshComponent* FindDynamicMeshComponent(AActor* Actor)
	{
		return Actor ? Actor->FindComponentByClass<UDynamicMeshComponent>() : nullptr;
	}

	// -----------------------------------------------------------------------------
	// Functional Module: Soft Reference Sanitizer
	// -----------------------------------------------------------------------------
	static bool SanitizePiePrefixInPathString(FString& InOut)
	{
		bool bChanged = false;
		const FString Token(TEXT("UEDPIE_"));
		int32 SearchFrom = 0;
		while (true)
		{
			const int32 Found = InOut.Find(Token, ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchFrom);
			if (Found == INDEX_NONE)
			{
				break;
			}

			const bool bAtBoundary = (Found == 0) || InOut[Found - 1] == TEXT('/') || InOut[Found - 1] == TEXT('.') || InOut[Found - 1] == TEXT(':');
			if (!bAtBoundary)
			{
				SearchFrom = Found + Token.Len();
				continue;
			}

			const int32 NumStart = Found + Token.Len();
			const int32 UnderscoreAfterNum = InOut.Find(TEXT("_"), ESearchCase::CaseSensitive, ESearchDir::FromStart, NumStart);
			if (UnderscoreAfterNum == INDEX_NONE)
			{
				SearchFrom = Found + Token.Len();
				continue;
			}

			bool bAllDigits = (UnderscoreAfterNum > NumStart);
			for (int32 i = NumStart; i < UnderscoreAfterNum; ++i)
			{
				if (!FChar::IsDigit(InOut[i]))
				{
					bAllDigits = false;
					break;
				}
			}
			if (!bAllDigits)
			{
				SearchFrom = Found + Token.Len();
				continue;
			}

			// Remove "UEDPIE_<digits>_".
			const int32 RemoveStart = Found;
			const int32 RemoveLen = (UnderscoreAfterNum - RemoveStart) + 1;
			InOut.RemoveAt(RemoveStart, RemoveLen);
			bChanged = true;
			SearchFrom = FMath::Max(0, RemoveStart - 1);
		}

		return bChanged;
	}

	static int32 SanitizeSoftRefsInPropertyValue(FProperty* Property, void* ValuePtr, int32& InOutExampleCount, FOutputDevice& Ar)
	{
		if (!Property || !ValuePtr)
		{
			return 0;
		}

		// Skip transient-ish properties; we only want serialized asset state.
		if (Property->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient | CPF_NonPIEDuplicateTransient | CPF_SkipSerialization))
		{
			return 0;
		}

		int32 Changes = 0;

		if (FSoftObjectProperty* SoftProp = CastField<FSoftObjectProperty>(Property))
		{
			FSoftObjectPtr* SoftPtr = static_cast<FSoftObjectPtr*>(ValuePtr);
			const FString Before = SoftPtr->ToSoftObjectPath().ToString();
			FString After = Before;
			if (SanitizePiePrefixInPathString(After) && !After.Equals(Before, ESearchCase::CaseSensitive))
			{
				*SoftPtr = FSoftObjectPtr(FSoftObjectPath(After));
				++Changes;
				if (InOutExampleCount < 8)
				{
					Ar.Logf(TEXT("BAT.SanitizeSoftRefs: %s -> %s"), *Before, *After);
					++InOutExampleCount;
				}
			}
			return Changes;
		}
		if (FNameProperty* NameProp = CastField<FNameProperty>(Property))
		{
			FName* Name = static_cast<FName*>(ValuePtr);
			const FString Before = Name->ToString();
			FString After = Before;
			if (SanitizePiePrefixInPathString(After) && !After.Equals(Before, ESearchCase::CaseSensitive))
			{
				*Name = FName(*After);
				++Changes;
				if (InOutExampleCount < 8)
				{
					Ar.Logf(TEXT("BAT.SanitizeSoftRefs: %s -> %s"), *Before, *After);
					++InOutExampleCount;
				}
			}
			return Changes;
		}
		if (FStrProperty* StrProp = CastField<FStrProperty>(Property))
		{
			FString* Str = static_cast<FString*>(ValuePtr);
			const FString Before = *Str;
			FString After = Before;
			if (SanitizePiePrefixInPathString(After) && !After.Equals(Before, ESearchCase::CaseSensitive))
			{
				*Str = After;
				++Changes;
				if (InOutExampleCount < 8)
				{
					Ar.Logf(TEXT("BAT.SanitizeSoftRefs: %s -> %s"), *Before, *After);
					++InOutExampleCount;
				}
			}
			return Changes;
		}
		if (FSoftClassProperty* SoftClassProp = CastField<FSoftClassProperty>(Property))
		{
			FSoftObjectPtr* SoftPtr = static_cast<FSoftObjectPtr*>(ValuePtr);
			const FString Before = SoftPtr->ToSoftObjectPath().ToString();
			FString After = Before;
			if (SanitizePiePrefixInPathString(After) && !After.Equals(Before, ESearchCase::CaseSensitive))
			{
				*SoftPtr = FSoftObjectPtr(FSoftObjectPath(After));
				++Changes;
				if (InOutExampleCount < 8)
				{
					Ar.Logf(TEXT("BAT.SanitizeSoftRefs: %s -> %s"), *Before, *After);
					++InOutExampleCount;
				}
			}
			return Changes;
		}
		if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
		{
			if (StructProp->Struct == TBaseStructure<FSoftObjectPath>::Get())
			{
				FSoftObjectPath* Path = static_cast<FSoftObjectPath*>(ValuePtr);
				const FString Before = Path->ToString();
				FString After = Before;
				if (SanitizePiePrefixInPathString(After) && !After.Equals(Before, ESearchCase::CaseSensitive))
				{
					*Path = FSoftObjectPath(After);
					++Changes;
					if (InOutExampleCount < 8)
					{
						Ar.Logf(TEXT("BAT.SanitizeSoftRefs: %s -> %s"), *Before, *After);
						++InOutExampleCount;
					}
				}
				return Changes;
			}

			// FTopLevelAssetPath is exposed to reflection via NoExportTypes.h.
			// It commonly backs UE5 soft references (AssetValidator uses these too).
			if (StructProp->Struct && StructProp->Struct->GetName().Equals(TEXT("TopLevelAssetPath"), ESearchCase::CaseSensitive))
			{
				FTopLevelAssetPath* Path = static_cast<FTopLevelAssetPath*>(ValuePtr);
				const FString Before = Path->ToString();
				FString After = Before;
				if (SanitizePiePrefixInPathString(After) && !After.Equals(Before, ESearchCase::CaseSensitive))
				{
					*Path = FTopLevelAssetPath(After);
					++Changes;
					if (InOutExampleCount < 8)
					{
						Ar.Logf(TEXT("BAT.SanitizeSoftRefs: %s -> %s"), *Before, *After);
						++InOutExampleCount;
					}
				}
				return Changes;
			}

			// Generic struct recursion: scan/sanitize inside nested struct fields.
			if (StructProp->Struct)
			{
				uint8* StructData = static_cast<uint8*>(ValuePtr);
				for (TFieldIterator<FProperty> It(StructProp->Struct, EFieldIteratorFlags::IncludeSuper); It; ++It)
				{
					FProperty* Inner = *It;
					void* InnerValuePtr = Inner ? Inner->ContainerPtrToValuePtr<void>(StructData) : nullptr;
					Changes += SanitizeSoftRefsInPropertyValue(Inner, InnerValuePtr, InOutExampleCount, Ar);
				}
				return Changes;
			}
		}
		if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property))
		{
			FScriptArrayHelper Helper(ArrayProp, ValuePtr);
			for (int32 i = 0; i < Helper.Num(); ++i)
			{
				Changes += SanitizeSoftRefsInPropertyValue(ArrayProp->Inner, Helper.GetRawPtr(i), InOutExampleCount, Ar);
			}
			return Changes;
		}
		if (FSetProperty* SetProp = CastField<FSetProperty>(Property))
		{
			FScriptSetHelper Helper(SetProp, ValuePtr);
			for (int32 i = 0; i < Helper.Num(); ++i)
			{
				if (!Helper.IsValidIndex(i))
				{
					continue;
				}
				Changes += SanitizeSoftRefsInPropertyValue(SetProp->ElementProp, Helper.GetElementPtr(i), InOutExampleCount, Ar);
			}
			return Changes;
		}
		if (FMapProperty* MapProp = CastField<FMapProperty>(Property))
		{
			FScriptMapHelper Helper(MapProp, ValuePtr);
			for (int32 i = 0; i < Helper.Num(); ++i)
			{
				if (!Helper.IsValidIndex(i))
				{
					continue;
				}
				Changes += SanitizeSoftRefsInPropertyValue(MapProp->KeyProp, Helper.GetKeyPtr(i), InOutExampleCount, Ar);
				Changes += SanitizeSoftRefsInPropertyValue(MapProp->ValueProp, Helper.GetValuePtr(i), InOutExampleCount, Ar);
			}
			return Changes;
		}

		return 0;
	}

	static bool SanitizeSoftRefsInCurrentEditorMap(UWorld* World, bool bSave, FOutputDevice& Ar)
	{
		if (!World)
		{
			Ar.Logf(TEXT("BAT.SanitizeSoftRefs: no world"));
			return false;
		}
		if (World->WorldType == EWorldType::PIE || World->WorldType == EWorldType::Game)
		{
			Ar.Logf(TEXT("BAT.SanitizeSoftRefs: run this in the editor world (world=editor), not PIE"));
			return false;
		}

		UPackage* Package = World->GetOutermost();
		if (!Package)
		{
			Ar.Logf(TEXT("BAT.SanitizeSoftRefs: no package"));
			return false;
		}

		TArray<UObject*> Objects;
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8)
		GetObjectsWithOuter(Package, Objects, EGetObjectsFlags::IncludeNestedObjects);
#else
		GetObjectsWithOuter(Package, Objects, /*bIncludeNestedObjects*/ true);
#endif

		int32 TotalChanges = 0;
		int32 ExampleCount = 0;
		for (UObject* Obj : Objects)
		{
			if (!Obj)
			{
				continue;
			}

			int32 ObjChanges = 0;
			for (TFieldIterator<FProperty> It(Obj->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
			{
				FProperty* Prop = *It;
				void* ValuePtr = Prop ? Prop->ContainerPtrToValuePtr<void>(Obj) : nullptr;
				ObjChanges += SanitizeSoftRefsInPropertyValue(Prop, ValuePtr, ExampleCount, Ar);
			}
			if (ObjChanges > 0)
			{
				Obj->MarkPackageDirty();
				TotalChanges += ObjChanges;
			}
		}

		Ar.Logf(TEXT("BAT.SanitizeSoftRefs: package=%s objects=%d changes=%d"), *Package->GetName(), Objects.Num(), TotalChanges);

		if (bSave)
		{
			if (!TrySaveCurrentEditorMap(Ar))
			{
				Ar.Logf(TEXT("BAT.SanitizeSoftRefs: save requested but failed"));
				return false;
			}
			Ar.Logf(TEXT("BAT.SanitizeSoftRefs: saved current editor map"));
		}

		return true;
	}

	static bool SanitizeSoftRefsInPackageByName(const FString& PackageName, bool bSave, FOutputDevice& Ar)
	{
		if (PackageName.IsEmpty())
		{
			Ar.Logf(TEXT("BAT.SanitizeSoftRefs: missing package name"));
			return false;
		}

		UPackage* Package = FindPackage(nullptr, *PackageName);
		if (!Package)
		{
			Package = LoadPackage(nullptr, *PackageName, LOAD_None);
		}
		if (!Package)
		{
			Ar.Logf(TEXT("BAT.SanitizeSoftRefs: failed to load package: %s"), *PackageName);
			return false;
		}

		TArray<UObject*> Objects;
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8)
		GetObjectsWithOuter(Package, Objects, EGetObjectsFlags::IncludeNestedObjects);
#else
		GetObjectsWithOuter(Package, Objects, /*bIncludeNestedObjects*/ true);
#endif

		int32 TotalChanges = 0;
		int32 ExampleCount = 0;
		for (UObject* Obj : Objects)
		{
			if (!Obj)
			{
				continue;
			}

			int32 ObjChanges = 0;
			for (TFieldIterator<FProperty> It(Obj->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
			{
				FProperty* Prop = *It;
				void* ValuePtr = Prop ? Prop->ContainerPtrToValuePtr<void>(Obj) : nullptr;
				ObjChanges += SanitizeSoftRefsInPropertyValue(Prop, ValuePtr, ExampleCount, Ar);
			}
			if (ObjChanges > 0)
			{
				Obj->MarkPackageDirty();
				TotalChanges += ObjChanges;
			}
		}

		Ar.Logf(TEXT("BAT.SanitizeSoftRefs: package=%s objects=%d changes=%d"), *Package->GetName(), Objects.Num(), TotalChanges);

		if (bSave)
		{
			UWorld* WorldInPkg = UWorld::FindWorldInPackage(Package);
			const FString Filename = FPackageName::LongPackageNameToFilename(PackageName, WorldInPkg ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension());
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);

			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			SaveArgs.SaveFlags = SAVE_NoError;
			SaveArgs.Error = &Ar;
			const bool bSaved = UPackage::SavePackage(Package, WorldInPkg, *Filename, SaveArgs);
			if (!bSaved)
			{
				Ar.Logf(TEXT("BAT.SanitizeSoftRefs: failed to save: %s"), *Filename);
				return false;
			}
			Ar.Logf(TEXT("BAT.SanitizeSoftRefs: saved %s"), *Filename);
		}

		return true;
	}

	static bool FindTextInPackageByName(const FString& PackageName, const FString& Needle, FOutputDevice& Ar)
	{
		if (PackageName.IsEmpty() || Needle.IsEmpty())
		{
			Ar.Logf(TEXT("BAT.FindInPackage: usage: BAT.FindInPackage <LongPackageName> <Needle>"));
			return false;
		}

		UPackage* Package = FindPackage(nullptr, *PackageName);
		if (!Package)
		{
			Package = LoadPackage(nullptr, *PackageName, LOAD_None);
		}
		if (!Package)
		{
			Ar.Logf(TEXT("BAT.FindInPackage: failed to load package: %s"), *PackageName);
			return false;
		}

		TArray<UObject*> Objects;
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8)
		GetObjectsWithOuter(Package, Objects, EGetObjectsFlags::IncludeNestedObjects);
#else
		GetObjectsWithOuter(Package, Objects, /*bIncludeNestedObjects*/ true);
#endif

		int32 HitCount = 0;
		for (UObject* Obj : Objects)
		{
			if (!Obj)
			{
				continue;
			}

			for (TFieldIterator<FProperty> It(Obj->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
			{
				FProperty* Prop = *It;
				void* ValuePtr = Prop ? Prop->ContainerPtrToValuePtr<void>(Obj) : nullptr;
				if (!Prop || !ValuePtr)
				{
					continue;
				}

				FString Text;
				Prop->ExportTextItem_Direct(Text, ValuePtr, ValuePtr, Obj, PPF_None);
				if (!Text.Contains(Needle, ESearchCase::IgnoreCase))
				{
					continue;
				}

				Ar.Logf(TEXT("BAT.FindInPackage: hit obj=%s class=%s prop=%s val=%s"), *GetNameSafe(Obj), *GetNameSafe(Obj->GetClass()), *Prop->GetName(), *Text.Left(512));
				++HitCount;
				if (HitCount >= 50)
				{
					Ar.Logf(TEXT("BAT.FindInPackage: stopping after %d hits"), HitCount);
					return true;
				}
			}
		}

		Ar.Logf(TEXT("BAT.FindInPackage: package=%s objects=%d hits=%d"), *Package->GetName(), Objects.Num(), HitCount);
		return true;
	}

	static bool TryCreateAndSaveMapAsset(const FString& LongPackageName, FOutputDevice& Ar);
	static bool TrySaveCurrentEditorMap(FOutputDevice& Ar);
	static bool TryOpenMapInEditorByExec(const FString& LongPackageName, FOutputDevice& Ar);
	static bool TryApplyPcginstancedTestToCurrentEditorMap(FOutputDevice& Ar);
	static bool GetPIEPlayer(UWorld*& OutWorld, APlayerController*& OutPC, APawn*& OutPawn, FOutputDevice& Ar);
	static void HandleBAT_PCG_ApplyPcginstancedTestCmd(const TArray<FString>& Args, FOutputDevice& Ar);

	static AActor* FindActorByNameOrLabel(UWorld* World, const FString& Target);

	// -----------------------------------------------------------------------------
	// Functional Module: PCG Graph Utilities
	// -----------------------------------------------------------------------------
	static void GatherProjectStaticMeshes(TArray<TSoftObjectPtr<UStaticMesh>>& OutMeshes, FOutputDevice& Ar)
	{
		OutMeshes.Reset();

		FAssetRegistryModule* AssetRegistryModule = FModuleManager::GetModulePtr<FAssetRegistryModule>(TEXT("AssetRegistry"));
		if (!AssetRegistryModule)
		{
			Ar.Logf(TEXT("BAT.PCG.ApplyPcginstancedTest: AssetRegistry module not available"));
			return;
		}

		auto TryAddMeshesFromPath = [&](const FName Path, bool bRecursive)
		{
			TArray<FAssetData> Assets;
			AssetRegistryModule->Get().GetAssetsByPath(Path, Assets, bRecursive);
			for (const FAssetData& Asset : Assets)
			{
				if (Asset.AssetClassPath == UStaticMesh::StaticClass()->GetClassPathName())
				{
					OutMeshes.AddUnique(TSoftObjectPtr<UStaticMesh>(Asset.ToSoftObjectPath()));
				}
			}
		};

		// Prefer the PCG demo meshes folder if present.
		TryAddMeshesFromPath(FName(TEXT("/Game/PCG/Assets/Meshes")), /*bRecursive*/ true);

		// Fallback: common project mesh root.
		if (OutMeshes.IsEmpty())
		{
			TryAddMeshesFromPath(FName(TEXT("/Game/Meshes")), /*bRecursive*/ true);
		}

		Ar.Logf(TEXT("BAT.PCG.ApplyPcginstancedTest: found %d project static meshes"), OutMeshes.Num());
	}

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

	// -----------------------------------------------------------------------------
	// Functional Module: Console Command Dispatch
	// -----------------------------------------------------------------------------
	static bool TryExecBatCommandDirect(UWorld* World, const FString& FullCommand, FStringOutputDevice& Out, bool& bOutOk)
	{
		bOutOk = false;
		if (!World)
		{
			return false;
		}

		FString CmdTrimmed = FullCommand;
		CmdTrimmed.TrimStartAndEndInline();
		if (!CmdTrimmed.StartsWith(TEXT("BAT."), ESearchCase::IgnoreCase))
		{
			return false;
		}

		TArray<FString> Tokens;
		CmdTrimmed.ParseIntoArrayWS(Tokens);
		if (Tokens.Num() == 0)
		{
			return false;
		}

		const FString& CommandName = Tokens[0];
		TArray<FString> Args;
		if (Tokens.Num() > 1)
		{
			Args = Tokens;
			Args.RemoveAt(0);
		}

		// Map population commands - call directly so Live Coding changes take effect immediately.
		if (CommandName.Equals(TEXT("BAT.QuitEditor"), ESearchCase::IgnoreCase))
		{
			// Best-effort graceful shutdown: stop PIE first, then request exit on a later tick.
			// Immediate RequestExit during PIE can trigger shutdown-time ensures in tickable world subsystems.
			if (GEditor && GEditor->PlayWorld)
			{
				Out.Logf(TEXT("BAT.QuitEditor: PIE active; requesting EndPlay then exit"));
				GEditor->RequestEndPlayMap();
			}

			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
			{
				FPlatformMisc::RequestExit(false);
				return false;
			}), 0.25f);

			Out.Logf(TEXT("BAT.QuitEditor: exit requested"));
			bOutOk = true;
			return true;
		}

		// Minimal cvar helpers for automation scripts.
		// Usage:
		//   BAT.CVar.Get <Name>
		//   BAT.CVar.Set <Name> <Value>
		if (CommandName.Equals(TEXT("BAT.CVar.Get"), ESearchCase::IgnoreCase))
		{
			if (Args.Num() < 1)
			{
				Out.Logf(TEXT("BAT.CVar.Get: missing arg <Name>"));
				bOutOk = false;
				return true;
			}

			const FString Name = Args[0].TrimStartAndEnd();
			IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(*Name);
			if (!Var)
			{
				Out.Logf(TEXT("BAT.CVar.Get: not found: %s"), *Name);
				bOutOk = false;
				return true;
			}

			Out.Logf(TEXT("%s=%s"), *Name, *Var->GetString());
			bOutOk = true;
			return true;
		}

		if (CommandName.Equals(TEXT("BAT.CVar.Set"), ESearchCase::IgnoreCase))
		{
			if (Args.Num() < 2)
			{
				Out.Logf(TEXT("BAT.CVar.Set: missing args <Name> <Value>"));
				bOutOk = false;
				return true;
			}

			const FString Name = Args[0].TrimStartAndEnd();
			const FString Value = Args[1].TrimStartAndEnd();
			IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(*Name);
			if (!Var)
			{
				Out.Logf(TEXT("BAT.CVar.Set: not found: %s"), *Name);
				bOutOk = false;
				return true;
			}

			Var->Set(*Value, ECVF_SetByConsole);
			Out.Logf(TEXT("%s=%s"), *Name, *Var->GetString());
			bOutOk = true;
			return true;
		}



		// Editor helper: reliably load a map in the editor world (does not rely on console 'Open').
		// Usage: BAT.Editor.LoadMap <LongPackageName>
		if (CommandName.Equals(TEXT("BAT.Editor.LoadMap"), ESearchCase::IgnoreCase))
		{
			if (Args.Num() < 1)
			{
				Out.Logf(TEXT("BAT.Editor.LoadMap: missing arg <LongPackageName>"));
				Out.Logf(TEXT("Usage: BAT.Editor.LoadMap /Game/Levels/ElectricDreams_Env"));
				bOutOk = false;
				return true;
			}

			#if WITH_EDITOR
			if (!GEditor)
			{
				Out.Logf(TEXT("BAT.Editor.LoadMap: no GEditor"));
				bOutOk = false;
				return true;
			}

			const FString LongPackageName = Args[0].TrimStartAndEnd();
			FString Filename;
			if (!FPackageName::DoesPackageExist(LongPackageName, &Filename))
			{
				Filename = FPackageName::LongPackageNameToFilename(LongPackageName, FPackageName::GetMapPackageExtension());
			}

			const bool bLoaded = FEditorFileUtils::LoadMap(Filename, /*LoadAsTemplate*/ false, /*bShowProgress*/ true);
			UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
			const FString CurrentPackage = EditorWorld ? EditorWorld->GetOutermost()->GetName() : FString(TEXT("<null>"));
			Out.Logf(TEXT("BAT.Editor.LoadMap: package=%s file=%s ok=%s current=%s"), *LongPackageName, *Filename, bLoaded ? TEXT("true") : TEXT("false"), *CurrentPackage);
			bOutOk = bLoaded;
			return true;
			#else
			Out.Logf(TEXT("BAT.Editor.LoadMap: not supported (WITH_EDITOR=0)"));
			bOutOk = false;
			return true;
			#endif
		}

	
		if (CommandName.Equals(TEXT("BAT.PCG.ApplyPcginstancedTest"), ESearchCase::IgnoreCase))
		{
			HandleBAT_PCG_ApplyPcginstancedTestCmd(Args, Out);
			bOutOk = true;
			return true;
		}

	
		// BAT.* command we don't handle directly - fall back to console execution.
		return false;
	}

	// -----------------------------------------------------------------------------
	// Functional Module: Editor Map + PCG Workflows
	// -----------------------------------------------------------------------------
	static void HandleBAT_PCG_ApplyPcginstancedTestCmd(const TArray<FString>& Args, FOutputDevice& Ar)
	{
		// If a map package is provided, open it first. Otherwise operate on the current editor map.
		if (Args.Num() > 0 && !Args[0].TrimStartAndEnd().IsEmpty())
		{
			const FString PackageName = Args[0].TrimStartAndEnd();
			if (!TryOpenMapInEditorByExec(PackageName, Ar))
			{
				Ar.Logf(TEXT("BAT.PCG.ApplyPcginstancedTest: failed to open map: %s"), *PackageName);
				return;
			}
		}

		if (!TryApplyPcginstancedTestToCurrentEditorMap(Ar))
		{
			Ar.Logf(TEXT("BAT.PCG.ApplyPcginstancedTest: failed"));
			return;
		}
		TrySaveCurrentEditorMap(Ar);
	}

	static bool TryOpenMapInEditorByExec(const FString& LongPackageName, FOutputDevice& Ar)
	{
		if (!GEngine || !GEditor)
		{
			Ar.Logf(TEXT("BAT.Map.OpenByExec: editor not available"));
			return false;
		}

		UWorld* CurrentWorld = GEditor->GetEditorWorldContext().World();
		FStringOutputDevice ExecOut;
		const FString OpenCmd = FString::Printf(TEXT("Open %s"), *LongPackageName);
		const bool bExecOk = GEngine->Exec(CurrentWorld, *OpenCmd, ExecOut);
		const FString CurrentPackage = CurrentWorld ? CurrentWorld->GetOutermost()->GetName() : FString(TEXT("<null>"));
		Ar.Logf(TEXT("BAT.Map.OpenByExec: target=%s command='%s' result=%s current_editor_map=%s"),
			*LongPackageName,
			*OpenCmd,
			bExecOk ? TEXT("ok") : TEXT("failed"),
			*CurrentPackage);
		if (!ExecOut.IsEmpty())
		{
			Ar.Logf(TEXT("BAT.Map.OpenByExec: exec output: %s"), *ExecOut);
		}
		return bExecOk;
	}

	static bool TryApplyPcginstancedTestToCurrentEditorMap(FOutputDevice& Ar)
	{
		if (!GEditor)
		{
			Ar.Logf(TEXT("BAT.PCG.ApplyPcginstancedTest: no GEditor"));
			return false;
		}

		UWorld* World = GEditor->GetEditorWorldContext().World();
		if (!World)
		{
			Ar.Logf(TEXT("BAT.PCG.ApplyPcginstancedTest: no editor world"));
			return false;
		}

		// Geometry plugin content includes this PCG graph.
		UPCGGraphInterface* GraphInterface = LoadObject<UPCGGraphInterface>(nullptr, TEXT("/Geometry/Pcg/PcginstancedTest.PcginstancedTest"));
		if (!GraphInterface)
		{
			Ar.Logf(TEXT("BAT.PCG.ApplyPcginstancedTest: failed to load graph '/Geometry/Pcg/PcginstancedTest.PcginstancedTest'"));
			return false;
		}

		// Ensure the graph uses project meshes (not engine/plugin meshes) for any StaticMeshSpawner nodes.
		TArray<TSoftObjectPtr<UStaticMesh>> ProjectMeshes;
		GatherProjectStaticMeshes(ProjectMeshes, Ar);
		ApplyMeshesToStaticMeshSpawners(GraphInterface->GetMutablePCGGraph(), ProjectMeshes);

		int32 VolumeCount = 0;
		int32 AppliedCount = 0;
		for (TActorIterator<APCGVolume> It(World); It; ++It)
		{
			APCGVolume* Volume = *It;
			if (!Volume)
			{
				continue;
			}
			++VolumeCount;

			UPCGComponent* PCGComponent = Volume->FindComponentByClass<UPCGComponent>();
			if (!PCGComponent)
			{
				Ar.Logf(TEXT("BAT.PCG.ApplyPcginstancedTest: PCGVolume '%s' has no PCGComponent"), *Volume->GetActorLabel());
				continue;
			}

			PCGComponent->bActivated = true;
			PCGComponent->SetGraph(GraphInterface);
			PCGComponent->GenerateLocal(/*bForce*/ true);
			++AppliedCount;
			Ar.Logf(TEXT("BAT.PCG.ApplyPcginstancedTest: applied to '%s'"), *Volume->GetActorLabel());
		}

		if (VolumeCount == 0)
		{
			Ar.Logf(TEXT("BAT.PCG.ApplyPcginstancedTest: no PCGVolume actors found in the editor world"));
			return false;
		}

		World->MarkPackageDirty();
		Ar.Logf(TEXT("BAT.PCG.ApplyPcginstancedTest: volumes=%d applied=%d"), VolumeCount, AppliedCount);
		return AppliedCount > 0;
	}

	static bool TrySaveCurrentEditorMap(FOutputDevice& Ar)
	{
		if (!GEditor)
		{
			return false;
		}
		UWorld* World = GEditor->GetEditorWorldContext().World();
		if (!World)
		{
			return false;
		}

		// IMPORTANT: In World Partition levels with External Actors enabled, actors are stored in separate
		// external packages (not the main .umap package). Saving only the .umap package results in an
		// "empty" map when reopened. Save dirty packages so external actor packages get saved too.
		World->MarkPackageDirty();
		const bool bOk = FEditorFileUtils::SaveDirtyPackages(
			/*bPromptUserToSave*/ false,
			/*bSaveMapPackages*/ true,
			/*bSaveContentPackages*/ true,
			/*bFastSave*/ false,
			/*bNotifyNoPackagesSaved*/ false,
			/*bCanBeDeclined*/ false);
		Ar.Logf(TEXT("BAT.SaveCurrentEditorMap: SaveDirtyPackages -> %s"), bOk ? TEXT("ok") : TEXT("failed"));
		return bOk;
	}

	static bool TryCreateAndSaveMapAsset(const FString& LongPackageName, FOutputDevice& Ar)
	{
		FString PackageName = LongPackageName;
		PackageName.TrimStartAndEndInline();

		if (PackageName.IsEmpty())
		{
			Ar.Logf(TEXT("BAT.Map.CreateAsset: missing package name"));
			return false;
		}

		// Allow /Game/... (project) and plugin mount points like /Geometry/...
		if (!PackageName.StartsWith(TEXT("/")))
		{
			Ar.Logf(TEXT("BAT.Map.CreateAsset: expected a long package name like /Game/Geometry/Maps/MyMap"));
			return false;
		}

		if (!FPackageName::IsValidLongPackageName(PackageName))
		{
			Ar.Logf(TEXT("BAT.Map.CreateAsset: invalid long package name: %s"), *PackageName);
			return false;
		}

		if (FPackageName::DoesPackageExist(PackageName))
		{
			Ar.Logf(TEXT("BAT.Map.CreateAsset: package already exists: %s"), *PackageName);
			return false;
		}

		const FString AssetName = FPackageName::GetLongPackageAssetName(PackageName);
		const FString AssetPath = FPackageName::GetLongPackagePath(PackageName);
		if (AssetName.IsEmpty() || AssetPath.IsEmpty())
		{
			Ar.Logf(TEXT("BAT.Map.CreateAsset: could not parse asset name/path from: %s"), *PackageName);
			return false;
		}

		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
		UWorldFactory* WorldFactory = NewObject<UWorldFactory>();
		if (!WorldFactory)
		{
			Ar.Logf(TEXT("BAT.Map.CreateAsset: failed to allocate UWorldFactory"));
			return false;
		}

		UObject* Created = AssetToolsModule.Get().CreateAsset(AssetName, AssetPath, UWorld::StaticClass(), WorldFactory);
		UWorld* CreatedWorld = Cast<UWorld>(Created);
		if (!CreatedWorld)
		{
			Ar.Logf(TEXT("BAT.Map.CreateAsset: failed to create map asset: %s"), *PackageName);
			return false;
		}

		UPackage* Package = CreatedWorld->GetOutermost();
		if (!Package)
		{
			Ar.Logf(TEXT("BAT.Map.CreateAsset: created world has no package"));
			return false;
		}

		Package->MarkPackageDirty();

		const FString Filename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetMapPackageExtension());
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		SaveArgs.Error = &Ar;

		const bool bSaved = UPackage::SavePackage(Package, CreatedWorld, *Filename, SaveArgs);
		if (!bSaved)
		{
			Ar.Logf(TEXT("BAT.Map.CreateAsset: failed to save: %s"), *Filename);
			return false;
		}

		Ar.Logf(TEXT("BAT.Map.CreateAsset: created and saved %s"), *PackageName);
		Ar.Logf(TEXT("BAT.Map.CreateAsset: file %s"), *Filename);
		return true;
	}


	// -----------------------------------------------------------------------------
	// Functional Module: Console Command Registration + Motion State
	// -----------------------------------------------------------------------------
	static TUniquePtr<FAutoConsoleCommandWithArgsAndOutputDevice> GBAT_PCG_ApplyPcginstancedTestCmd;
	static TUniquePtr<FAutoConsoleCommandWithArgsAndOutputDevice> GBAT_MoveCmd;
	static TUniquePtr<FAutoConsoleCommandWithArgsAndOutputDevice> GBAT_TurnCmd;
	static TUniquePtr<FAutoConsoleCommandWithArgsAndOutputDevice> GBAT_StopMotionCmd;
	static TUniquePtr<FAutoConsoleCommandWithArgsAndOutputDevice> GBAT_KeyDownCmd;
	static TUniquePtr<FAutoConsoleCommandWithArgsAndOutputDevice> GBAT_KeyUpCmd;
	static TUniquePtr<FAutoConsoleCommandWithArgsAndOutputDevice> GBAT_DebugPlayerCmd;

	struct FBATMotionState
	{
		FTSTicker::FDelegateHandle TickerHandle;
		double EndTimeSec = 0.0;
		float Forward = 0.0f;
		float Right = 0.0f;
		float YawDegPerSec = 0.0f;
		TWeakObjectPtr<APlayerController> PlayerController;
		TWeakObjectPtr<APawn> Pawn;
	};

	static FBATMotionState GBATMotion;

	static void StopBATMotion()
	{
		if (GBATMotion.TickerHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(GBATMotion.TickerHandle);
			GBATMotion.TickerHandle.Reset();
		}
		GBATMotion.PlayerController.Reset();
		GBATMotion.Pawn.Reset();
		GBATMotion.EndTimeSec = 0.0;
		GBATMotion.Forward = 0.0f;
		GBATMotion.Right = 0.0f;
		GBATMotion.YawDegPerSec = 0.0f;
	}

	static bool TickBATMotion(float DeltaTime)
	{
		const double Now = FPlatformTime::Seconds();
		if (Now >= GBATMotion.EndTimeSec)
		{
			GBATMotion.TickerHandle.Reset();
			return false;
		}

		APlayerController* PC = GBATMotion.PlayerController.Get();
		APawn* Pawn = GBATMotion.Pawn.Get();
		if (!PC || !Pawn)
		{
			GBATMotion.TickerHandle.Reset();
			return false;
		}

		if (!FMath::IsNearlyZero(GBATMotion.YawDegPerSec))
		{
			FRotator Rot = PC->GetControlRotation();
			Rot.Yaw = FRotator::ClampAxis(Rot.Yaw + (GBATMotion.YawDegPerSec * DeltaTime));
			PC->SetControlRotation(Rot);
		}

		if (!FMath::IsNearlyZero(GBATMotion.Forward) || !FMath::IsNearlyZero(GBATMotion.Right))
		{
			Pawn->AddMovementInput(Pawn->GetActorForwardVector(), GBATMotion.Forward);
			Pawn->AddMovementInput(Pawn->GetActorRightVector(), GBATMotion.Right);
		}

		return true;
	}

	static bool StartBATMotion(float DurationSec, float Forward, float Right, float YawDegPerSec, FOutputDevice& Ar)
	{
		if (!GEditor)
		{
			Ar.Logf(TEXT("BAT.Motion: editor not available"));
			return false;
		}
		if (DurationSec <= 0.0f)
		{
			Ar.Logf(TEXT("BAT.Motion: duration must be > 0"));
			return false;
		}

		UWorld* World = GEditor->PlayWorld;
		if (!World)
		{
			Ar.Logf(TEXT("BAT.Motion: PIE must be running"));
			return false;
		}

		APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
		APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		if (!PC || !Pawn)
		{
			Ar.Logf(TEXT("BAT.Motion: missing PlayerController or Pawn"));
			return false;
		}

		StopBATMotion();

		GBATMotion.PlayerController = PC;
		GBATMotion.Pawn = Pawn;
		GBATMotion.Forward = Forward;
		GBATMotion.Right = Right;
		GBATMotion.YawDegPerSec = YawDegPerSec;
		GBATMotion.EndTimeSec = FPlatformTime::Seconds() + (double)DurationSec;
		GBATMotion.TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickBATMotion), 0.0f);

		Ar.Logf(TEXT("BAT.Motion: started duration=%.2fs forward=%.2f right=%.2f yawDegPerSec=%.2f"), DurationSec, Forward, Right, YawDegPerSec);
		return true;
	}

	static bool GetPIEPlayer(UWorld*& OutWorld, APlayerController*& OutPC, APawn*& OutPawn, FOutputDevice& Ar)
	{
		OutWorld = nullptr;
		OutPC = nullptr;
		OutPawn = nullptr;
		if (!GEditor)
		{
			Ar.Logf(TEXT("BAT: editor not available"));
			return false;
		}
		UWorld* World = GEditor->PlayWorld;
		if (!World)
		{
			Ar.Logf(TEXT("BAT: PIE must be running"));
			return false;
		}
		APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
		APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		if (!PC)
		{
			Ar.Logf(TEXT("BAT: missing PlayerController"));
			return false;
		}
		OutWorld = World;
		OutPC = PC;
		OutPawn = Pawn;
		return true;
	}

	static bool InputKeyOnPIEPlayer(const TCHAR* OpName, const FString& KeyNameStr, EInputEvent EventType, FOutputDevice& Ar)
	{
		UWorld* World = nullptr;
		APlayerController* PC = nullptr;
		APawn* Pawn = nullptr;
		if (!GetPIEPlayer(World, PC, Pawn, Ar))
		{
			return false;
		}

		const FString Trimmed = KeyNameStr.TrimStartAndEnd();
		if (Trimmed.IsEmpty())
		{
			Ar.Logf(TEXT("%s: missing key name"), OpName);
			return false;
		}

		const FKey Key(*Trimmed);
		if (!Key.IsValid())
		{
			Ar.Logf(TEXT("%s: invalid key '%s'"), OpName, *Trimmed);
			return false;
		}

		// UE5.5+: Route through PlayerInput to avoid deprecated APlayerController::InputKey overloads.
		if (!PC->PlayerInput)
		{
			Ar.Logf(TEXT("%s: missing PlayerInput"), OpName);
			return false;
		}
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6)
		PC->PlayerInput->InputKey(FInputKeyEventArgs(
			/*Viewport*/ nullptr,
			FInputDeviceId::CreateFromInternalId(0),
			Key,
			EventType,
			1.0f,
			/*bIsTouchEvent*/ false,
			FPlatformTime::Cycles64()));
#else
		PC->PlayerInput->InputKey(FInputKeyParams(Key, EventType, 1.0, /*bGamepadOverride*/ false));
#endif
		Ar.Logf(TEXT("%s: %s"), OpName, *Trimmed);
		return true;
	}

	static void RegisterBATConsoleCommands()
	{
		if (GBAT_PCG_ApplyPcginstancedTestCmd.IsValid()
			&& GBAT_MoveCmd.IsValid()
			&& GBAT_TurnCmd.IsValid()
			&& GBAT_StopMotionCmd.IsValid())
		{
			return;
		}

		GBAT_PCG_ApplyPcginstancedTestCmd = MakeUnique<FAutoConsoleCommandWithArgsAndOutputDevice>(
			TEXT("BAT.PCG.ApplyPcginstancedTest"),
			TEXT("Applies the PcginstancedTest PCG graph to placed PCGVolumes in the current (or specified) editor map, triggers generation, and saves.\n")
			TEXT("Usage: BAT.PCG.ApplyPcginstancedTest [OptionalMapPackage]\n")
			TEXT("Example: BAT.PCG.ApplyPcginstancedTest /Game/Maps/MyMap"),
			FConsoleCommandWithArgsAndOutputDeviceDelegate::CreateStatic(&HandleBAT_PCG_ApplyPcginstancedTestCmd)
		);

		GBAT_MoveCmd = MakeUnique<FAutoConsoleCommandWithArgsAndOutputDevice>(
			TEXT("BAT.Move"),
			TEXT("Simulates WASD-style movement during PIE by feeding AddMovementInput over time (no Python).\n")
			TEXT("Usage: BAT.Move <duration_sec> <forward> <right>\n")
			TEXT("Example: BAT.Move 0.8 1 0   (hold W for 0.8s)\n")
			TEXT("Example: BAT.Move 0.5 0 1   (hold D for 0.5s)"),
			FConsoleCommandWithArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, FOutputDevice& Ar)
			{
				if (Args.Num() < 3)
				{
					Ar.Logf(TEXT("BAT.Move: requires <duration_sec> <forward> <right>"));
					return;
				}
				const float Duration = FCString::Atof(*Args[0]);
				const float Forward = FCString::Atof(*Args[1]);
				const float Right = FCString::Atof(*Args[2]);
				StartBATMotion(Duration, Forward, Right, 0.0f, Ar);
			})
		);

		GBAT_TurnCmd = MakeUnique<FAutoConsoleCommandWithArgsAndOutputDevice>(
			TEXT("BAT.Turn"),
			TEXT("Simulates mouse-style yaw turning during PIE by adjusting control rotation over time (no Python).\n")
			TEXT("Usage: BAT.Turn <duration_sec> <degrees>\n")
			TEXT("Example: BAT.Turn 0.8 90"),
			FConsoleCommandWithArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, FOutputDevice& Ar)
			{
				if (Args.Num() < 2)
				{
					Ar.Logf(TEXT("BAT.Turn: requires <duration_sec> <degrees>"));
					return;
				}
				const float Duration = FCString::Atof(*Args[0]);
				const float Degrees = FCString::Atof(*Args[1]);
				const float YawRate = (Duration > 0.0f) ? (Degrees / Duration) : 0.0f;
				StartBATMotion(Duration, 0.0f, 0.0f, YawRate, Ar);
			})
		);

		GBAT_StopMotionCmd = MakeUnique<FAutoConsoleCommandWithArgsAndOutputDevice>(
			TEXT("BAT.StopMotion"),
			TEXT("Stops any active BAT.Move/BAT.Turn motion tick."),
			FConsoleCommandWithArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>&, FOutputDevice& Ar)
			{
				StopBATMotion();
				Ar.Logf(TEXT("BAT.StopMotion: stopped"));
			})
		);

		GBAT_KeyDownCmd = MakeUnique<FAutoConsoleCommandWithArgsAndOutputDevice>(
			TEXT("BAT.KeyDown"),
			TEXT("Simulates a key press on the PIE PlayerController (no Python).\n")
			TEXT("Usage: BAT.KeyDown <KeyName>   (example: BAT.KeyDown W)"),
			FConsoleCommandWithArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, FOutputDevice& Ar)
			{
				if (Args.Num() < 1)
				{
					Ar.Logf(TEXT("BAT.KeyDown: requires <KeyName>"));
					return;
				}
				InputKeyOnPIEPlayer(TEXT("BAT.KeyDown"), Args[0], IE_Pressed, Ar);
			})
		);

		GBAT_KeyUpCmd = MakeUnique<FAutoConsoleCommandWithArgsAndOutputDevice>(
			TEXT("BAT.KeyUp"),
			TEXT("Simulates a key release on the PIE PlayerController (no Python).\n")
			TEXT("Usage: BAT.KeyUp <KeyName>     (example: BAT.KeyUp W)"),
			FConsoleCommandWithArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, FOutputDevice& Ar)
			{
				if (Args.Num() < 1)
				{
					Ar.Logf(TEXT("BAT.KeyUp: requires <KeyName>"));
					return;
				}
				InputKeyOnPIEPlayer(TEXT("BAT.KeyUp"), Args[0], IE_Released, Ar);
			})
		);

		GBAT_DebugPlayerCmd = MakeUnique<FAutoConsoleCommandWithArgsAndOutputDevice>(
			TEXT("BAT.DebugPlayer"),
			TEXT("Logs PIE player controller/pawn location/rotation for debugging automation."),
			FConsoleCommandWithArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>&, FOutputDevice& Ar)
			{
				UWorld* World = nullptr;
				APlayerController* PC = nullptr;
				APawn* Pawn = nullptr;
				if (!GetPIEPlayer(World, PC, Pawn, Ar))
				{
					return;
				}
				const FRotator CR = PC->GetControlRotation();
				if (Pawn)
				{
					Ar.Logf(TEXT("BAT.DebugPlayer: PC=%s Pawn=%s Loc=%s Rot=%s ControlRot=%s"), *PC->GetClass()->GetName(), *Pawn->GetClass()->GetName(), *Pawn->GetActorLocation().ToString(), *Pawn->GetActorRotation().ToString(), *CR.ToString());
				}
				else
				{
					Ar.Logf(TEXT("BAT.DebugPlayer: PC=%s Pawn=<null> ControlRot=%s"), *PC->GetClass()->GetName(), *CR.ToString());
				}
			})
		);
	}

	static void UnregisterBATConsoleCommands()
	{
		StopBATMotion();

		GBAT_PCG_ApplyPcginstancedTestCmd.Reset();
		GBAT_MoveCmd.Reset();
		GBAT_TurnCmd.Reset();
		GBAT_StopMotionCmd.Reset();
		GBAT_KeyDownCmd.Reset();
		GBAT_KeyUpCmd.Reset();
		GBAT_DebugPlayerCmd.Reset();
	}

	// -----------------------------------------------------------------------------
	// Functional Module: Actor/Component Lookup
	// -----------------------------------------------------------------------------
	static bool ActorMatchesName(const AActor* Actor, const FString& Target)
	{
		if (!Actor)
		{
			return false;
		}

		if (Actor->GetName().Equals(Target, ESearchCase::IgnoreCase))
		{
			return true;
		}

#if WITH_EDITOR
		if (Actor->GetActorLabel().Equals(Target, ESearchCase::IgnoreCase))
		{
			return true;
		}
#endif

		return false;
	}

	static AActor* FindActorByNameOrLabel(UWorld* World, const FString& Target)
	{
		if (!World)
		{
			return nullptr;
		}

		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (ActorMatchesName(Actor, Target))
			{
				return Actor;
			}
		}

		return nullptr;
	}

	static bool ComponentMatchesName(const UActorComponent* Comp, const FString& Target)
	{
		if (!Comp)
		{
			return false;
		}

		return Comp->GetName().Equals(Target, ESearchCase::IgnoreCase);
	}

	static UInstancedStaticMeshComponent* FindInstancedStaticMeshComponentByName(UWorld* World, const FString& Target)
	{
		if (!World)
		{
			return nullptr;
		}

		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!Actor)
			{
				continue;
			}

			TArray<UInstancedStaticMeshComponent*> ISMComps;
			Actor->GetComponents<UInstancedStaticMeshComponent>(ISMComps);
			for (UInstancedStaticMeshComponent* Comp : ISMComps)
			{
				if (ComponentMatchesName(Comp, Target))
				{
					return Comp;
				}
			}
		}

		return nullptr;
	}

	static FString NormalizeBlueprintObjectPath(const FString& InPath)
	{
		FString Path = InPath;
		Path.TrimStartAndEndInline();
		if (Path.IsEmpty())
		{
			return Path;
		}

		// If it's a package path like /Game/Foo/BP_Bar, convert to /Game/Foo/BP_Bar.BP_Bar
		if (!Path.Contains(TEXT(".")))
		{
			const FString AssetName = FPackageName::GetLongPackageAssetName(Path);
			if (!AssetName.IsEmpty())
			{
				Path = FString::Printf(TEXT("%s.%s"), *Path, *AssetName);
			}
		}

		return Path;
	}

	static bool TryLoadBlueprint(const FString& InPath, UBlueprint*& OutBlueprint, FString& OutObjectPath)
	{
		OutBlueprint = nullptr;
		OutObjectPath = NormalizeBlueprintObjectPath(InPath);
		if (OutObjectPath.IsEmpty())
		{
			return false;
		}

		OutBlueprint = LoadObject<UBlueprint>(nullptr, *OutObjectPath);
		return (OutBlueprint != nullptr);
	}

	// -----------------------------------------------------------------------------
	// Functional Module: JSON Parsing + Blueprint Graph Helpers
	// -----------------------------------------------------------------------------
	static bool TryParseVector3(const TSharedPtr<FJsonValue>& Value, FVector& OutVec)
	{
		if (!Value.IsValid())
		{
			return false;
		}

		if (Value->Type == EJson::Array)
		{
			const TArray<TSharedPtr<FJsonValue>>& Arr = Value->AsArray();
			if (Arr.Num() == 3)
			{
				OutVec.X = (float)Arr[0]->AsNumber();
				OutVec.Y = (float)Arr[1]->AsNumber();
				OutVec.Z = (float)Arr[2]->AsNumber();
				return true;
			}
			return false;
		}

		if (Value->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject> Obj = Value->AsObject();
			if (!Obj.IsValid())
			{
				return false;
			}
			double X = 0.0, Y = 0.0, Z = 0.0;
			if (!(Obj->TryGetNumberField(TEXT("x"), X) || Obj->TryGetNumberField(TEXT("X"), X))) return false;
			if (!(Obj->TryGetNumberField(TEXT("y"), Y) || Obj->TryGetNumberField(TEXT("Y"), Y))) return false;
			if (!(Obj->TryGetNumberField(TEXT("z"), Z) || Obj->TryGetNumberField(TEXT("Z"), Z))) return false;
			OutVec = FVector((float)X, (float)Y, (float)Z);
			return true;
		}

		return false;
	}

	static bool TryParseRotator(const TSharedPtr<FJsonValue>& Value, FRotator& OutRot)
	{
		if (!Value.IsValid())
		{
			return false;
		}

		if (Value->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject> Obj = Value->AsObject();
			if (!Obj.IsValid())
			{
				return false;
			}
			double Pitch = 0.0, Yaw = 0.0, Roll = 0.0;
			// Accept either (pitch,yaw,roll) or (p,y,r)
			if (!(Obj->TryGetNumberField(TEXT("pitch"), Pitch) || Obj->TryGetNumberField(TEXT("p"), Pitch) || Obj->TryGetNumberField(TEXT("Pitch"), Pitch))) return false;
			if (!(Obj->TryGetNumberField(TEXT("yaw"), Yaw) || Obj->TryGetNumberField(TEXT("y"), Yaw) || Obj->TryGetNumberField(TEXT("Yaw"), Yaw))) return false;
			if (!(Obj->TryGetNumberField(TEXT("roll"), Roll) || Obj->TryGetNumberField(TEXT("r"), Roll) || Obj->TryGetNumberField(TEXT("Roll"), Roll))) return false;
			OutRot = FRotator((float)Pitch, (float)Yaw, (float)Roll);
			return true;
		}

		return false;
	}

	static bool SetObjectPropertyFromJson(UObject* Obj, FProperty* Prop, const TSharedPtr<FJsonValue>& JsonValue, FString& OutError)
	{
		if (!Obj || !Prop || !JsonValue.IsValid())
		{
			OutError = TEXT("Invalid args");
			return false;
		}

		void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Obj);

		if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
		{
			if (JsonValue->Type != EJson::Boolean)
			{
				OutError = TEXT("Expected boolean");
				return false;
			}
			BoolProp->SetPropertyValue(ValuePtr, JsonValue->AsBool());
			return true;
		}

		if (FNumericProperty* NumProp = CastField<FNumericProperty>(Prop))
		{
			if (JsonValue->Type != EJson::Number)
			{
				OutError = TEXT("Expected number");
				return false;
			}
			const double Num = JsonValue->AsNumber();
			if (NumProp->IsInteger())
			{
				NumProp->SetIntPropertyValue(ValuePtr, (int64)Num);
			}
			else
			{
				NumProp->SetFloatingPointPropertyValue(ValuePtr, Num);
			}
			return true;
		}

		if (FStrProperty* StrProp = CastField<FStrProperty>(Prop))
		{
			if (JsonValue->Type != EJson::String)
			{
				OutError = TEXT("Expected string");
				return false;
			}
			StrProp->SetPropertyValue(ValuePtr, JsonValue->AsString());
			return true;
		}

		if (FNameProperty* NameProp = CastField<FNameProperty>(Prop))
		{
			if (JsonValue->Type != EJson::String)
			{
				OutError = TEXT("Expected string (name)");
				return false;
			}
			NameProp->SetPropertyValue(ValuePtr, FName(*JsonValue->AsString()));
			return true;
		}

		if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
		{
			if (StructProp->Struct == TBaseStructure<FVector>::Get())
			{
				FVector V;
				if (!TryParseVector3(JsonValue, V))
				{
					OutError = TEXT("Expected vector {x,y,z} or [x,y,z]");
					return false;
				}
				*static_cast<FVector*>(ValuePtr) = V;
				return true;
			}
			if (StructProp->Struct == TBaseStructure<FRotator>::Get())
			{
				FRotator R;
				if (!TryParseRotator(JsonValue, R))
				{
					OutError = TEXT("Expected rotator {pitch,yaw,roll}");
					return false;
				}
				*static_cast<FRotator*>(ValuePtr) = R;
				return true;
			}
		}

		OutError = FString::Printf(TEXT("Unsupported property type: %s"), *Prop->GetClass()->GetName());
		return false;
	}

	static bool SaveBlueprintPackage(UBlueprint* Blueprint, FString& OutError)
	{
		if (!Blueprint)
		{
			OutError = TEXT("Blueprint is null");
			return false;
		}
		UPackage* Package = Blueprint->GetOutermost();
		if (!Package)
		{
			OutError = TEXT("Blueprint package is null");
			return false;
		}

		TArray<UPackage*> Packages;
		Packages.Add(Package);
		const bool bOk = UEditorLoadingAndSavingUtils::SavePackages(Packages, /*bOnlyDirty*/ false);
		if (!bOk)
		{
			OutError = TEXT("SavePackages returned false");
			return false;
		}

		return true;
	}

	static bool TryParseGuidString(const FString& InGuid, FGuid& OutGuid)
	{
		return FGuid::Parse(InGuid, OutGuid);
	}

	static void GetAllBlueprintGraphs(UBlueprint* Blueprint, TArray<UEdGraph*>& OutGraphs)
	{
		OutGraphs.Reset();
		if (!Blueprint)
		{
			return;
		}

		auto AddGraphRecursive = [&OutGraphs](UEdGraph* RootGraph)
		{
			if (!RootGraph || OutGraphs.Contains(RootGraph))
			{
				return;
			}

			OutGraphs.Add(RootGraph);

			TArray<UEdGraph*> ChildGraphs;
			RootGraph->GetAllChildrenGraphs(ChildGraphs);
			for (UEdGraph* ChildGraph : ChildGraphs)
			{
				if (ChildGraph && !OutGraphs.Contains(ChildGraph))
				{
					OutGraphs.Add(ChildGraph);
				}
			}
		};

		for (UEdGraph* Graph : Blueprint->UbergraphPages)
		{
			AddGraphRecursive(Graph);
		}
		for (UEdGraph* Graph : Blueprint->FunctionGraphs)
		{
			AddGraphRecursive(Graph);
		}
		for (UEdGraph* Graph : Blueprint->MacroGraphs)
		{
			AddGraphRecursive(Graph);
		}
	}

	static UEdGraph* FindGraphByName(UBlueprint* Blueprint, const FString& GraphName)
	{
		if (!Blueprint)
		{
			return nullptr;
		}

		TArray<UEdGraph*> Graphs;
		GetAllBlueprintGraphs(Blueprint, Graphs);

		if (GraphName.IsEmpty())
		{
			return Graphs.Num() > 0 ? Graphs[0] : nullptr;
		}

		for (UEdGraph* Graph : Graphs)
		{
			if (Graph && Graph->GetName() == GraphName)
			{
				return Graph;
			}
		}

		// Fallback: match by display name
		for (UEdGraph* Graph : Graphs)
		{
			if (Graph && Graph->GetFName().ToString() == GraphName)
			{
				return Graph;
			}
		}

		return nullptr;
	}

	static UEdGraphNode* FindNodeByGuid(UBlueprint* Blueprint, const FGuid& NodeGuid)
	{
		if (!Blueprint)
		{
			return nullptr;
		}

		TArray<UEdGraph*> Graphs;
		GetAllBlueprintGraphs(Blueprint, Graphs);
		for (UEdGraph* Graph : Graphs)
		{
			if (!Graph)
			{
				continue;
			}
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (Node && Node->NodeGuid == NodeGuid)
				{
					return Node;
				}
			}
		}

		return nullptr;
	}

	static UEdGraphPin* FindPinByName(UEdGraphNode* Node, const FString& PinName)
	{
		if (!Node)
		{
			return nullptr;
		}

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->PinName.ToString() == PinName)
			{
				return Pin;
			}
		}
		return Node->FindPin(*PinName);
	}

	static TSharedPtr<FJsonObject> DescribeNodeJson(UEdGraphNode* Node)
	{
		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		if (!Node)
		{
			Obj->SetBoolField(TEXT("ok"), false);
			return Obj;
		}

		Obj->SetBoolField(TEXT("ok"), true);
		Obj->SetStringField(TEXT("node_guid"), Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
		Obj->SetStringField(TEXT("class"), Node->GetClass()->GetPathName());
		Obj->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
		Obj->SetNumberField(TEXT("x"), Node->NodePosX);
		Obj->SetNumberField(TEXT("y"), Node->NodePosY);
		if (!Node->NodeComment.IsEmpty())
		{
			Obj->SetStringField(TEXT("comment"), Node->NodeComment);
		}

		TArray<TSharedPtr<FJsonValue>> Pins;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin)
			{
				continue;
			}
			TSharedRef<FJsonObject> PinObj = MakeShared<FJsonObject>();
			PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
			PinObj->SetStringField(TEXT("dir"), (Pin->Direction == EGPD_Input) ? TEXT("input") : TEXT("output"));
			PinObj->SetStringField(TEXT("category"), Pin->PinType.PinCategory.ToString());
			PinObj->SetStringField(TEXT("sub_category"), Pin->PinType.PinSubCategory.ToString());
			PinObj->SetStringField(TEXT("default"), Pin->DefaultValue);
			PinObj->SetBoolField(TEXT("linked"), Pin->LinkedTo.Num() > 0);
			Pins.Add(MakeShared<FJsonValueObject>(PinObj));
		}
		Obj->SetArrayField(TEXT("pins"), Pins);
		return Obj;
	}
}

FBlueprintAutomationToolkitModule::~FBlueprintAutomationToolkitModule() = default;

bool FBlueprintAutomationToolkitModule::TryExecBatCommandDirect(UWorld* World, const FString& FullCommand, FStringOutputDevice& Out, bool& bOutOk)
{
	return ::TryExecBatCommandDirect(World, FullCommand, Out, bOutOk);
}

void FBlueprintAutomationToolkitModule::ApplyDefaultSandboxPolicy()
{
	if (CommandSandboxAllowPrefixes.Num() == 0)
	{
		CommandSandboxAllowPrefixes = {
			TEXT("BAT."),
			TEXT("stat "),
			TEXT("show "),
			TEXT("r."),
			TEXT("t."),
			TEXT("sg."),
			TEXT("wp."),
			TEXT("ke "),
			TEXT("ce "),
			TEXT("open "),
		};
	}

	if (CommandSandboxBlockSubstrings.Num() == 0)
	{
		CommandSandboxBlockSubstrings = {
			TEXT("\n"),
			TEXT("\r"),
			TEXT("|"),
			TEXT("&&"),
			TEXT(";"),
			TEXT("py "),
			TEXT("python "),
			TEXT("quit"),
			TEXT("exit"),
		};
	}
}

bool FBlueprintAutomationToolkitModule::IsCommandAllowedBySandbox(const FString& Command, FString& OutReason) const
{
	OutReason.Reset();
	if (!bSafeModeEnabled)
	{
		FString TrimmedUnsafe = Command;
		TrimmedUnsafe.TrimStartAndEndInline();
		const FString LowerUnsafe = TrimmedUnsafe.ToLower();
		for (const FString& RawNeedle : CommandSandboxBlockSubstrings)
		{
			const FString Needle = RawNeedle.ToLower();
			if (!Needle.IsEmpty() && LowerUnsafe.Contains(Needle))
			{
				OutReason = FString::Printf(TEXT("Command blocked by unsafe-mode blocklist: contains '%s'"), *RawNeedle);
				return false;
			}
		}
		return true;
	}

	FString Trimmed = Command;
	Trimmed.TrimStartAndEndInline();
	if (Trimmed.IsEmpty())
	{
		OutReason = TEXT("Command is empty.");
		return false;
	}

	const FString LowerCommand = Trimmed.ToLower();

	for (const FString& RawNeedle : CommandSandboxBlockSubstrings)
	{
		const FString Needle = RawNeedle.ToLower();
		if (!Needle.IsEmpty() && LowerCommand.Contains(Needle))
		{
			OutReason = FString::Printf(TEXT("Command blocked by sandbox rule: contains '%s'"), *RawNeedle);
			return false;
		}
	}

	for (const FString& RawPrefix : CommandSandboxAllowPrefixes)
	{
		const FString Prefix = RawPrefix.ToLower();
		if (!Prefix.IsEmpty() && LowerCommand.StartsWith(Prefix))
		{
			return true;
		}
	}

	OutReason = TEXT("Command is not in sandbox allow-list.");
	return false;
}

bool FBlueprintAutomationToolkitModule::EnsureServerPermissionGranted()
{
	if (bPermissionPromptAnswered)
	{
		return true;
	}

	if (FApp::IsUnattended())
	{
		bServerEnabled = false;
		UE_LOG(LogBlueprintAutomationToolkit, Warning, TEXT("Server disabled: permission dialog has not been answered and session is unattended."));
		return false;
	}

	const EAppReturnType::Type Choice = FMessageDialog::Open(
		EAppMsgType::YesNo,
		FText::FromString(TEXT("This opens a local HTTP server on localhost only. Requires a token. Proceed?")));

	bPermissionPromptAnswered = true;
	if (Choice != EAppReturnType::Yes)
	{
		bServerEnabled = false;
		GConfig->SetBool(TEXT("BlueprintAutomationToolkit"), TEXT("bPermissionPromptAnswered"), bPermissionPromptAnswered, GEditorPerProjectIni);
		GConfig->SetBool(TEXT("BlueprintAutomationToolkit"), TEXT("bServerEnabled"), bServerEnabled, GEditorPerProjectIni);
		GConfig->Flush(false, GEditorPerProjectIni);
		return false;
	}

	GConfig->SetBool(TEXT("BlueprintAutomationToolkit"), TEXT("bPermissionPromptAnswered"), bPermissionPromptAnswered, GEditorPerProjectIni);
	GConfig->Flush(false, GEditorPerProjectIni);

	return true;
}

void FBlueprintAutomationToolkitModule::PersistSettings(bool bPersistAuthToken) const
{
	const FString ProjectDefaultEditorIni = GetProjectDefaultEditorIniPath();
	FString ProjectTokenBefore;
	bool bProjectSaveFlagBefore = false;
	FString DiskProjectTokenBefore;
	bool bDiskProjectSaveFlagBefore = false;
	GConfig->GetString(TEXT("BlueprintAutomationToolkit"), TEXT("AuthToken"), ProjectTokenBefore, ProjectDefaultEditorIni);
	GConfig->GetBool(TEXT("BlueprintAutomationToolkit"), TEXT("bSaveTokenInProjectSettings"), bProjectSaveFlagBefore, ProjectDefaultEditorIni);
	ReadProjectAuthSettingsFromDisk(ProjectDefaultEditorIni, DiskProjectTokenBefore, bDiskProjectSaveFlagBefore);

	GConfig->SetInt(TEXT("BlueprintAutomationToolkit"), TEXT("Port"), Port, GEditorPerProjectIni);
	GConfig->SetBool(TEXT("BlueprintAutomationToolkit"), TEXT("bServerEnabled"), bServerEnabled, GEditorPerProjectIni);
	GConfig->SetBool(TEXT("BlueprintAutomationToolkit"), TEXT("bRequireAuthToken"), bRequireAuthToken, GEditorPerProjectIni);
	GConfig->SetBool(TEXT("BlueprintAutomationToolkit"), TEXT("bPermissionPromptAnswered"), bPermissionPromptAnswered, GEditorPerProjectIni);
	GConfig->SetBool(TEXT("BlueprintAutomationToolkit"), TEXT("bAllowPythonExec"), bAllowPythonExec, GEditorPerProjectIni);
	GConfig->SetBool(TEXT("BlueprintAutomationToolkit"), TEXT("bEnableExecRoute"), bEnableExecRoute, GEditorPerProjectIni);
	GConfig->SetBool(TEXT("BlueprintAutomationToolkit"), TEXT("bSafeModeEnabled"), bSafeModeEnabled, GEditorPerProjectIni);
	GConfig->SetBool(TEXT("BlueprintAutomationToolkit"), TEXT("bAllowFilesystemInSafeMode"), bAllowFilesystemInSafeMode, GEditorPerProjectIni);
	GConfig->SetBool(TEXT("BlueprintAutomationToolkit"), TEXT("bEnableSandbox"), bSafeModeEnabled, GEditorPerProjectIni);
	GConfig->SetBool(TEXT("BlueprintAutomationToolkit"), TEXT("bCommandSandboxEnabled"), bSafeModeEnabled, GEditorPerProjectIni);
	GConfig->SetInt(TEXT("BlueprintAutomationToolkit"), TEXT("MaxRequestBodyBytes"), MaxRequestBodyBytes, GEditorPerProjectIni);
	GConfig->SetInt(TEXT("BlueprintAutomationToolkit"), TEXT("RateLimitPerSecond"), RateLimitPerSecond, GEditorPerProjectIni);
	GConfig->SetInt(TEXT("BlueprintAutomationToolkit"), TEXT("RateLimitBurst"), RateLimitBurst, GEditorPerProjectIni);
	GConfig->SetBool(TEXT("BlueprintAutomationToolkit"), TEXT("permissions.editor"), bPermissionEditor, GEditorPerProjectIni);
	GConfig->SetBool(TEXT("BlueprintAutomationToolkit"), TEXT("permissions.blueprint"), bPermissionBlueprint, GEditorPerProjectIni);
	GConfig->SetBool(TEXT("BlueprintAutomationToolkit"), TEXT("permissions.pie"), bPermissionPie, GEditorPerProjectIni);
	GConfig->SetBool(TEXT("BlueprintAutomationToolkit"), TEXT("permissions.exec"), bPermissionExec, GEditorPerProjectIni);
	GConfig->SetBool(TEXT("BlueprintAutomationToolkit"), TEXT("permissions.python"), bPermissionPython, GEditorPerProjectIni);
	GConfig->SetBool(TEXT("BlueprintAutomationToolkit"), TEXT("permissions.filesystem"), bPermissionFilesystem, GEditorPerProjectIni);
	GConfig->SetBool(TEXT("BlueprintAutomationToolkit"), TEXT("bEnableHmacAuth"), bEnableHmacAuth, GEditorPerProjectIni);
	GConfig->SetInt(TEXT("BlueprintAutomationToolkit"), TEXT("MaxClockSkewSeconds"), MaxClockSkewSeconds, GEditorPerProjectIni);
	GConfig->SetInt(TEXT("BlueprintAutomationToolkit"), TEXT("LogRingSize"), LogRingSize, GEditorPerProjectIni);
	GConfig->SetInt(TEXT("BlueprintAutomationToolkit"), TEXT("MaxOpsPerPlan"), MaxOpsPerPlan, GEditorPerProjectIni);
	GConfig->SetInt(TEXT("BlueprintAutomationToolkit"), TEXT("MaxActorsPerLayout"), MaxActorsPerLayout, GEditorPerProjectIni);
	GConfig->SetInt(TEXT("BlueprintAutomationToolkit"), TEXT("MaxInstancesPerOp"), MaxInstancesPerOp, GEditorPerProjectIni);
	GConfig->SetInt(TEXT("BlueprintAutomationToolkit"), TEXT("MaxTotalInstancesPerPlan"), MaxTotalInstancesPerPlan, GEditorPerProjectIni);
	GConfig->SetBool(TEXT("BlueprintAutomationToolkit"), TEXT("bUnsafeModeConfirmationAccepted"), bUnsafeModeConfirmationAccepted, GEditorPerProjectIni);
	GConfig->SetBool(TEXT("BlueprintAutomationToolkit"), TEXT("bExecRouteConfirmationAccepted"), bExecRouteConfirmationAccepted, GEditorPerProjectIni);
	GConfig->SetBool(TEXT("BlueprintAutomationToolkit"), TEXT("bPythonConfirmationAccepted"), bPythonConfirmationAccepted, GEditorPerProjectIni);
	GConfig->SetArray(TEXT("BlueprintAutomationToolkit"), TEXT("CommandSandboxAllowPrefixes"), CommandSandboxAllowPrefixes, GEditorPerProjectIni);
	GConfig->SetArray(TEXT("BlueprintAutomationToolkit"), TEXT("CommandSandboxBlockSubstrings"), CommandSandboxBlockSubstrings, GEditorPerProjectIni);
	GConfig->SetBool(TEXT("BlueprintAutomationToolkit"), TEXT("bSaveTokenInProjectSettings"), bSaveTokenInProjectSettings, ProjectDefaultEditorIni);
	UE_LOG(
		LogBlueprintAutomationToolkit,
		Log,
		TEXT("PersistSettings begin: project_ini=%s persist_auth=%s save_to_project=%s from_env=%s auth=%s runtime_auth=%s project_flag_before=%s project_auth_before=%s disk_project_flag_before=%s disk_project_auth_before=%s"),
		*ProjectDefaultEditorIni,
		bPersistAuthToken ? TEXT("true") : TEXT("false"),
		bSaveTokenInProjectSettings ? TEXT("true") : TEXT("false"),
		bAuthTokenFromEnv ? TEXT("true") : TEXT("false"),
		*DescribeTokenForLog(AuthToken),
		*DescribeTokenForLog(RuntimeAuthToken),
		bProjectSaveFlagBefore ? TEXT("true") : TEXT("false"),
		*DescribeTokenForLog(ProjectTokenBefore),
		bDiskProjectSaveFlagBefore ? TEXT("true") : TEXT("false"),
		*DescribeTokenForLog(DiskProjectTokenBefore));

	EProjectAuthTokenWriteMode DiskAuthTokenWriteMode = EProjectAuthTokenWriteMode::Preserve;
	if (bPersistAuthToken)
	{
		if (bSaveTokenInProjectSettings && !bAuthTokenFromEnv)
		{
			GConfig->SetString(TEXT("BlueprintAutomationToolkit"), TEXT("AuthToken"), *AuthToken, ProjectDefaultEditorIni);
			DiskAuthTokenWriteMode = EProjectAuthTokenWriteMode::Set;
			UE_LOG(LogBlueprintAutomationToolkit, Log, TEXT("PersistSettings action: wrote AuthToken to project ini"));
		}
		else
		{
			GConfig->RemoveKey(TEXT("BlueprintAutomationToolkit"), TEXT("AuthToken"), ProjectDefaultEditorIni);
			DiskAuthTokenWriteMode = EProjectAuthTokenWriteMode::Clear;
			UE_LOG(LogBlueprintAutomationToolkit, Log, TEXT("PersistSettings action: removed AuthToken from project ini"));
		}
	}
	else
	{
		UE_LOG(LogBlueprintAutomationToolkit, Log, TEXT("PersistSettings action: left AuthToken unchanged because persist_auth=false"));
	}
	GConfig->RemoveKey(TEXT("BlueprintAutomationToolkit"), TEXT("AuthToken"), GEditorPerProjectIni);
	GConfig->RemoveKey(TEXT("BlueprintAutomationToolkit"), TEXT("bSaveTokenInProjectSettings"), GEditorPerProjectIni);
	const bool bWroteProjectSettingsToDisk = WriteProjectAuthSettingsToDisk(ProjectDefaultEditorIni, bSaveTokenInProjectSettings, DiskAuthTokenWriteMode, AuthToken);
	GConfig->Flush(false, ProjectDefaultEditorIni);
	GConfig->Flush(false, GEditorPerProjectIni);
	if (UBlueprintAutomationToolkitSettings* Settings = GetMutableDefault<UBlueprintAutomationToolkitSettings>())
	{
		Settings->Port = Port;
		Settings->bEnableServer = bServerEnabled;
		Settings->bRequireAuthToken = bRequireAuthToken;
		Settings->bSafeMode = bSafeModeEnabled;
		Settings->SaveConfig();
	}

	FString ProjectTokenAfter;
	bool bProjectSaveFlagAfter = false;
	FString DiskProjectTokenAfter;
	bool bDiskProjectSaveFlagAfter = false;
	GConfig->GetString(TEXT("BlueprintAutomationToolkit"), TEXT("AuthToken"), ProjectTokenAfter, ProjectDefaultEditorIni);
	GConfig->GetBool(TEXT("BlueprintAutomationToolkit"), TEXT("bSaveTokenInProjectSettings"), bProjectSaveFlagAfter, ProjectDefaultEditorIni);
	ReadProjectAuthSettingsFromDisk(ProjectDefaultEditorIni, DiskProjectTokenAfter, bDiskProjectSaveFlagAfter);
	UE_LOG(
		LogBlueprintAutomationToolkit,
		Log,
		TEXT("PersistSettings end: write_disk=%s project_flag_after=%s project_auth_after=%s disk_project_flag_after=%s disk_project_auth_after=%s"),
		bWroteProjectSettingsToDisk ? TEXT("true") : TEXT("false"),
		bProjectSaveFlagAfter ? TEXT("true") : TEXT("false"),
		*DescribeTokenForLog(ProjectTokenAfter),
		bDiskProjectSaveFlagAfter ? TEXT("true") : TEXT("false"),
		*DescribeTokenForLog(DiskProjectTokenAfter));
}

FString FBlueprintAutomationToolkitModule::GenerateStrongAuthToken() const
{
	const FString Seed = FString::Printf(TEXT("%s|%f|%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits), FPlatformTime::Seconds(), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	return FMD5::HashAnsiString(*Seed);
}

bool FBlueprintAutomationToolkitModule::RotateAuthToken(bool bRestartIfRunning)
{
	AuthToken = GenerateStrongAuthToken();
	if (!bAuthTokenFromEnv)
	{
		RuntimeAuthToken = AuthToken;
	}
	UE_LOG(
		LogBlueprintAutomationToolkit,
		Log,
		TEXT("RotateAuthToken: restart_if_running=%s from_env=%s save_to_project=%s auth=%s runtime_auth=%s"),
		bRestartIfRunning ? TEXT("true") : TEXT("false"),
		bAuthTokenFromEnv ? TEXT("true") : TEXT("false"),
		bSaveTokenInProjectSettings ? TEXT("true") : TEXT("false"),
		*DescribeTokenForLog(AuthToken),
		*DescribeTokenForLog(RuntimeAuthToken));
	PersistSettings(true);

	UE_LOG(LogBlueprintAutomationToolkit, Log, TEXT("Auth token rotated (%s)."), bAuthTokenFromEnv ? TEXT("runtime still uses ENV override") : TEXT("active"));

	if (bRestartIfRunning && bServerRunning)
	{
		StopServer();
		return StartServer(false);
	}
	return true;
}

bool FBlueprintAutomationToolkitModule::ConfirmUnsafeOption(const FText& Message, const TCHAR* ConfigKey, bool& bConfirmationState)
{
	if (bConfirmationState)
	{
		return true;
	}

	if (FApp::IsUnattended())
	{
		return false;
	}

	const EAppReturnType::Type Choice = FMessageDialog::Open(EAppMsgType::YesNo, Message);
	if (Choice != EAppReturnType::Yes)
	{
		return false;
	}

	bConfirmationState = true;
	if (ConfigKey)
	{
		GConfig->SetBool(TEXT("BlueprintAutomationToolkit"), ConfigKey, true, GEditorPerProjectIni);
		GConfig->Flush(false, GEditorPerProjectIni);
	}
	return true;
}

void FBlueprintAutomationToolkitModule::NotifySettingChanged()
{
	if (UBlueprintAutomationToolkitSettings* Settings = GetMutableDefault<UBlueprintAutomationToolkitSettings>())
	{
		Settings->Port = Port;
		Settings->bEnableServer = bServerEnabled;
		Settings->bRequireAuthToken = bRequireAuthToken;
		Settings->bSafeMode = bSafeModeEnabled;
		Settings->SaveConfig();
	}

	PersistSettings(true);
	if (bServerRunning)
	{
		StopServer();
		StartServer(false);
	}
}

void FBlueprintAutomationToolkitModule::RegisterAutomationCommands()
{
	delete CommandDispatcher;
	CommandDispatcher = new FCommandDispatcher();

	RegisterBuiltInAutomationCommand({
		TEXT("/blueprint/graph/apply"),
		[]() -> TUniquePtr<FAutomationCommand> { return MakeUnique<FApplyGraphCommand>(); },
		EBATAutomationPermissionTier::Edit,
		EBATAutomationPermission::Blueprint,
		false,
		true,
		false
	});
	RegisterBuiltInAutomationCommand({
		TEXT("/blueprint/graph/read"),
		[]() -> TUniquePtr<FAutomationCommand> { return MakeUnique<FReadGraphCommand>(); },
		EBATAutomationPermissionTier::Read,
		EBATAutomationPermission::Blueprint,
		false,
		false,
		false
	});
	RegisterBuiltInAutomationCommand({
		TEXT("/blueprint/compile_save"),
		[]() -> TUniquePtr<FAutomationCommand> { return MakeUnique<FCompileSaveBlueprintCommand>(); },
		EBATAutomationPermissionTier::Edit,
		EBATAutomationPermission::Blueprint | EBATAutomationPermission::Filesystem,
		false,
		true,
		false
	});
	RegisterBuiltInAutomationCommand({
		TEXT("/object/resolve"),
		[]() -> TUniquePtr<FAutomationCommand> { return MakeUnique<FGetObjectCommand>(); },
		EBATAutomationPermissionTier::Read,
		EBATAutomationPermission::Editor,
		false,
		false,
		false
	});
	RegisterBuiltInAutomationCommand({
		TEXT("/object/get_property"),
		[]() -> TUniquePtr<FAutomationCommand> { return MakeUnique<FGetObjectCommand>(); },
		EBATAutomationPermissionTier::Read,
		EBATAutomationPermission::Editor,
		false,
		false,
		false
	});
	RegisterBuiltInAutomationCommand({
		TEXT("/object/describe"),
		[]() -> TUniquePtr<FAutomationCommand> { return MakeUnique<FDescribeObjectCommand>(); },
		EBATAutomationPermissionTier::Read,
		EBATAutomationPermission::Editor,
		false,
		false,
		false
	});
	RegisterBuiltInAutomationCommand({
		TEXT("/object/set_property"),
		[]() -> TUniquePtr<FAutomationCommand> { return MakeUnique<FSetPropertyCommand>(); },
		EBATAutomationPermissionTier::Edit,
		EBATAutomationPermission::Editor,
		false,
		true,
		false
	});
	RegisterBuiltInAutomationCommand({
		TEXT("/object/call_function"),
		[]() -> TUniquePtr<FAutomationCommand> { return MakeUnique<FCallFunctionCommand>(); },
		EBATAutomationPermissionTier::Admin,
		EBATAutomationPermission::Editor,
		false,
		false,
		false
	});
	RegisterBuiltInAutomationCommand({
		TEXT("/actor/spawn"),
		[]() -> TUniquePtr<FAutomationCommand>
		{
			static const FObjectAutomationService Service;
			return MakeUnique<FSpawnActorCommand>(Service);
		},
		EBATAutomationPermissionTier::Edit,
		EBATAutomationPermission::Editor,
		false,
		false,
		false
	});
	RegisterBuiltInAutomationCommand({
		TEXT("/actor/destroy"),
		[]() -> TUniquePtr<FAutomationCommand> { return MakeUnique<FDestroyActorCommand>(); },
		EBATAutomationPermissionTier::Edit,
		EBATAutomationPermission::Editor,
		false,
		true,
		false
	});
	RegisterBuiltInAutomationCommand({
		TEXT("/editor/select"),
		[]() -> TUniquePtr<FAutomationCommand> { return MakeUnique<FSelectEditorTargetCommand>(); },
		EBATAutomationPermissionTier::Edit,
		EBATAutomationPermission::Editor,
		false,
		false,
		false
	});
	RegisterBuiltInAutomationCommand({
		TEXT("/editor/focus"),
		[]() -> TUniquePtr<FAutomationCommand> { return MakeUnique<FFocusEditorTargetCommand>(); },
		EBATAutomationPermissionTier::Edit,
		EBATAutomationPermission::Editor,
		false,
		false,
		false
	});
	RegisterBuiltInAutomationCommand({
		TEXT("/material/texture_samples/set"),
		[]() -> TUniquePtr<FAutomationCommand> { return MakeUnique<FSetMaterialTextureSamplesCommand>(); },
		EBATAutomationPermissionTier::Edit,
		EBATAutomationPermission::Editor | EBATAutomationPermission::Filesystem,
		true,
		true,
		false
	});
	RegisterBuiltInAutomationCommand({
		TEXT("/editor/level/audit"),
		[]() -> TUniquePtr<FAutomationCommand> { return MakeUnique<FLevelAuditCommand>(); },
		EBATAutomationPermissionTier::Read,
		EBATAutomationPermission::Editor,
		true,
		false,
		false
	});
	RegisterBuiltInAutomationCommand({
		TEXT("/editor/level/destroy_actors"),
		[]() -> TUniquePtr<FAutomationCommand> { return MakeUnique<FLevelDestroyActorsCommand>(); },
		EBATAutomationPermissionTier::Edit,
		EBATAutomationPermission::Editor,
		true,
		true,
		false
	});
	RegisterBuiltInAutomationCommand({
		TEXT("/editor/level/save"),
		[]() -> TUniquePtr<FAutomationCommand> { return MakeUnique<FLevelSaveCommand>(); },
		EBATAutomationPermissionTier::Edit,
		EBATAutomationPermission::Editor | EBATAutomationPermission::Filesystem,
		true,
		true,
		false
	});
	RegisterBuiltInAutomationCommand({
		TEXT("/editor/level/create"),
		[]() -> TUniquePtr<FAutomationCommand> { return MakeUnique<FLevelCreateCommand>(); },
		EBATAutomationPermissionTier::Edit,
		EBATAutomationPermission::Editor | EBATAutomationPermission::Filesystem,
		true,
		true,
		false
	});
	RegisterBuiltInAutomationCommand({
		TEXT("/editor/level/load"),
		[]() -> TUniquePtr<FAutomationCommand> { return MakeUnique<FLevelLoadCommand>(); },
		EBATAutomationPermissionTier::Edit,
		EBATAutomationPermission::Editor | EBATAutomationPermission::Filesystem,
		true,
		true,
		false
	});
}

bool FBlueprintAutomationToolkitModule::RegisterBuiltInAutomationCommand(FBATAutomationCommandRegistration Registration)
{
	FString Error;
	const bool bRegistered = CommandDispatcher && CommandDispatcher->Register(MoveTemp(Registration), true, &Error);
	if (!bRegistered)
	{
		UE_LOG(LogBlueprintAutomationToolkit, Warning, TEXT("Failed to register built-in automation command: %s"), *Error);
	}
	return bRegistered;
}

bool FBlueprintAutomationToolkitModule::RegisterAutomationCommand(FBATAutomationCommandRegistration Registration, FString* OutError)
{
	if (OutError)
	{
		OutError->Reset();
	}

	if (!CommandDispatcher)
	{
		CommandDispatcher = new FCommandDispatcher();
	}

	const FString Endpoint = Registration.Endpoint;
	FCommandDispatcher::FRegistration PreviousRegistration;
	const bool bHadPreviousRegistration = CommandDispatcher->TryGetRegistration(Endpoint, PreviousRegistration);

	if (!CommandDispatcher->Register(MoveTemp(Registration), false, OutError))
	{
		return false;
	}

	FCommandDispatcher::FRegistration UpdatedRegistration;
	if (!CommandDispatcher->TryGetRegistration(Endpoint, UpdatedRegistration))
	{
		if (OutError)
		{
			*OutError = FString::Printf(TEXT("Endpoint '%s' was registered but could not be queried."), *Endpoint);
		}
		return false;
	}

	if (DynamicAutomationRoutes.Contains(Endpoint) && (!UpdatedRegistration.bBindRoute || UpdatedRegistration.bBuiltIn))
	{
		if (const FHttpRouteHandle* ExistingHandle = DynamicAutomationRoutes.Find(Endpoint); ExistingHandle && ExistingHandle->IsValid() && Router.IsValid())
		{
			Router->UnbindRoute(*ExistingHandle);
		}
		DynamicAutomationRoutes.Remove(Endpoint);
	}

	if (UpdatedRegistration.bBindRoute && bServerRunning && Router.IsValid() && !BindRegisteredAutomationRoute(Endpoint))
	{
		FString RevertError;
		CommandDispatcher->Unregister(Endpoint, false, &RevertError);
		if (OutError)
		{
			*OutError = FString::Printf(TEXT("Endpoint '%s' was registered but its HTTP route could not be bound."), *Endpoint);
		}
		return false;
	}

	if (bHadPreviousRegistration && PreviousRegistration.bBindRoute && !UpdatedRegistration.bBindRoute)
	{
		DynamicAutomationRoutes.Remove(Endpoint);
	}

	return true;
}

bool FBlueprintAutomationToolkitModule::UnregisterAutomationCommand(const FString& Endpoint, FString* OutError)
{
	if (OutError)
	{
		OutError->Reset();
	}

	if (!CommandDispatcher)
	{
		if (OutError)
		{
			*OutError = TEXT("Command dispatcher is not initialized.");
		}
		return false;
	}

	if (const FHttpRouteHandle* ExistingHandle = DynamicAutomationRoutes.Find(Endpoint); ExistingHandle && ExistingHandle->IsValid() && Router.IsValid())
	{
		Router->UnbindRoute(*ExistingHandle);
	}
	DynamicAutomationRoutes.Remove(Endpoint);

	return CommandDispatcher->Unregister(Endpoint, false, OutError);
}

bool FBlueprintAutomationToolkitModule::HasAutomationCommand(const FString& Endpoint) const
{
	return CommandDispatcher && CommandDispatcher->HasCommand(Endpoint);
}

void FBlueprintAutomationToolkitModule::GetAutomationCommandInfos(TArray<FBATAutomationCommandInfo>& OutCommands) const
{
	if (!CommandDispatcher)
	{
		OutCommands.Reset();
		return;
	}

	CommandDispatcher->GetRegisteredCommands(OutCommands);
}

void FBlueprintAutomationToolkitModule::BindRegisteredAutomationRoutes()
{
	TArray<FBATAutomationCommandInfo> Commands;
	GetAutomationCommandInfos(Commands);
	for (const FBATAutomationCommandInfo& CommandInfo : Commands)
	{
		if (CommandInfo.bBindRoute)
		{
			BindRegisteredAutomationRoute(CommandInfo.Endpoint);
		}
	}
}

bool FBlueprintAutomationToolkitModule::BindRegisteredAutomationRoute(const FString& Endpoint)
{
	if (!Router.IsValid() || Endpoint.IsEmpty() || DynamicAutomationRoutes.Contains(Endpoint))
	{
		return Router.IsValid() && !Endpoint.IsEmpty();
	}

	const FString EndpointCopy = Endpoint;
	FHttpRouteHandle RouteHandle = Router->BindRoute(
		FHttpPath(EndpointCopy),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda([this, EndpointCopy](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			if (!ValidateAndHandleRequest(Request, OnComplete, *EndpointCopy))
			{
				return true;
			}

			TSharedPtr<FJsonObject> BodyObj;
			if (!BAT::Http::TryParseJsonBody(Request.Body, BodyObj) || !BodyObj.IsValid())
			{
				OnComplete(MakeCanonicalErrorResponse(400, ResolveOrCreateRequestId(Request), TEXT("bad_json"), TEXT("Invalid JSON body.")));
				return true;
			}

			return DispatchAutomationCommandRoute(EndpointCopy, Request, OnComplete, BodyObj, true);
		}));

	if (!RouteHandle.IsValid())
	{
		return false;
	}

	DynamicAutomationRoutes.Add(EndpointCopy, RouteHandle);
	return true;
}

void FBlueprintAutomationToolkitModule::UnbindRegisteredAutomationRoutes()
{
	if (!Router.IsValid())
	{
		DynamicAutomationRoutes.Reset();
		return;
	}

	for (const TPair<FString, FHttpRouteHandle>& Pair : DynamicAutomationRoutes)
	{
		if (Pair.Value.IsValid())
		{
			Router->UnbindRoute(Pair.Value);
		}
	}

	DynamicAutomationRoutes.Reset();
}

FAutomationResult FBlueprintAutomationToolkitModule::ExecuteAutomationCommand(const FString& Endpoint, const FString& RequestId, const TSharedPtr<FJsonObject>& BodyObj, bool bReturnRawObject) const
{
	if (CommandDispatcher == nullptr)
	{
		return FAutomationResult::Error(TEXT("dispatcher_unavailable"), TEXT("Command dispatcher is not initialized"), 500);
	}

	FAutomationContext Context;
	Context.RequestId = RequestId;
	Context.Endpoint = Endpoint;
	Context.Body = BodyObj;
	Context.Module = const_cast<FBlueprintAutomationToolkitModule*>(this);
	Context.bReturnRawObject = bReturnRawObject;
	return CommandDispatcher->Dispatch(Endpoint, Context);
}

bool FBlueprintAutomationToolkitModule::DispatchAutomationCommandRoute(const FString& Endpoint, const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete, const TSharedPtr<FJsonObject>& BodyObj, bool bReturnRawObject)
{
	const FString RequestId = ResolveOrCreateRequestId(Request);
	const FAutomationResult Result = ExecuteAutomationCommand(Endpoint, RequestId, BodyObj, bReturnRawObject);

	auto CompleteWithOptionalResponseExport = [&](TUniquePtr<FHttpServerResponse> Response)
	{
		if (!Response)
		{
			OnComplete(MakeErrorResponse(500, RequestId, TEXT("internal_error"), TEXT("No HTTP response was produced.")));
			return;
		}

		FString OutputPath;
		FString OutputError;
		if (!BAT::Http::TryWriteResponseToDisk(BodyObj, RequestId, *Response, OutputPath, OutputError))
		{
			OnComplete(MakeErrorResponse(500, RequestId, TEXT("response_export_failed"), OutputError.IsEmpty() ? TEXT("Failed to export response to disk.") : OutputError));
			return;
		}

		OnComplete(MoveTemp(Response));
	};
	if (!Result.bSuccess)
	{
		if (bReturnRawObject && Result.ErrorData.IsValid() && Result.ErrorData->Type == EJson::Object)
		{
			CompleteWithOptionalResponseExport(BAT::Http::MakeJsonResponse(Result.StatusCode, Result.ErrorData->AsObject().ToSharedRef(), RequestId));
		}
		else if (bReturnRawObject && Result.Data.IsValid() && Result.Data->Type == EJson::Object)
		{
			CompleteWithOptionalResponseExport(BAT::Http::MakeJsonResponse(Result.StatusCode, Result.Data->AsObject().ToSharedRef(), RequestId));
		}
		else
		{
			CompleteWithOptionalResponseExport(MakeErrorResponse(Result.StatusCode, RequestId, Result.ErrorCode, Result.ErrorMessage));
		}
		return true;
	}

	if (bReturnRawObject && Result.Data.IsValid() && Result.Data->Type == EJson::Object)
	{
		CompleteWithOptionalResponseExport(BAT::Http::MakeJsonResponse(Result.StatusCode, Result.Data->AsObject().ToSharedRef(), RequestId));
	}
	else
	{
		CompleteWithOptionalResponseExport(BAT::Http::MakeJsonOk(Result.Data, Result.StatusCode, RequestId));
	}

	return true;
}

void FBlueprintAutomationToolkitModule::RegisterControlPanelTab()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		BATControlPanelTabName,
		FOnSpawnTab::CreateRaw(this, &FBlueprintAutomationToolkitModule::SpawnControlPanelTab))
		.SetDisplayName(FText::FromString(TEXT("Blueprint Automation Toolkit")))
		.SetMenuType(ETabSpawnerMenuType::Enabled);

	if (UToolMenus* Menus = UToolMenus::TryGet())
	{
		UToolMenu* Menu = Menus->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
		if (Menu)
		{
			FToolMenuSection& Section = Menu->FindOrAddSection(TEXT("BlueprintAutomationToolkit"));
			Section.AddMenuEntry(
				TEXT("OpenBATPanel"),
				FText::FromString(TEXT("Blueprint Automation Toolkit")),
				FText::FromString(TEXT("Open Blueprint Automation Toolkit control panel.")),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([]()
				{
					FGlobalTabmanager::Get()->TryInvokeTab(BATControlPanelTabName);
				})));
		}
	}
}

void FBlueprintAutomationToolkitModule::UnregisterControlPanelTab()
{
	if (FGlobalTabmanager::Get()->HasTabSpawner(BATControlPanelTabName))
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(BATControlPanelTabName);
	}
}

TSharedRef<SDockTab> FBlueprintAutomationToolkitModule::SpawnControlPanelTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(8)
				[
					SNew(STextBlock)
					.Text_Lambda([this]()
					{
						return FText::FromString(FString::Printf(TEXT("Server: %s | Running: %s | Port: %d | Bind: localhost"), bServerEnabled ? TEXT("Enabled") : TEXT("Disabled"), bServerRunning ? TEXT("Yes") : TEXT("No"), Port));
					})
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(8)
				[
					SNew(STextBlock)
					.Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT("Token: %s"), *GetTokenStatusText())); })
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(8, 0, 8, 8)
				[
					SNew(SCheckBox)
					.IsEnabled_Lambda([this]() { return !bAuthTokenFromEnv; })
					.IsChecked_Lambda([this]() { return bSaveTokenInProjectSettings ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
					{
						bSaveTokenInProjectSettings = (NewState == ECheckBoxState::Checked);
						PersistSettings(true);
					})
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("Save Token In Project Settings")))
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(8)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(2)
					[
						SNew(SButton)
						.Text(FText::FromString(TEXT("Start Server")))
						.OnClicked_Lambda([this]()
						{
							bServerEnabled = true;
							PersistSettings(false);
							StartServer(true);
							return FReply::Handled();
						})
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(2)
					[
						SNew(SButton)
						.Text(FText::FromString(TEXT("Stop Server")))
						.OnClicked_Lambda([this]()
						{
							bServerEnabled = false;
							PersistSettings(false);
							StopServer();
							return FReply::Handled();
						})
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(2)
					[
						SNew(SButton)
						.Text(FText::FromString(TEXT("Rotate Token")))
						.OnClicked_Lambda([this]()
						{
							RotateAuthToken(true);
							return FReply::Handled();
						})
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(2)
					[
						SNew(SButton)
						.Text(FText::FromString(TEXT("Copy Auth Token")))
						.OnClicked_Lambda([this]()
						{
							if (RuntimeAuthToken.IsEmpty())
							{
								FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("Auth token is not set.")));
								return FReply::Handled();
							}

							FPlatformApplicationMisc::ClipboardCopy(*RuntimeAuthToken);
							FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("Auth token copied to clipboard.")));
							return FReply::Handled();
						})
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(2)
					[
						SNew(SButton)
						.Text(FText::FromString(TEXT("Copy Local Endpoint URL")))
						.OnClicked_Lambda([this]()
						{
							const FString Url = FString::Printf(TEXT("http://127.0.0.1:%d"), Port);
							FPlatformApplicationMisc::ClipboardCopy(*Url);
							return FReply::Handled();
						})
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(8)
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([this]() { return bSafeModeEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
					{
						const bool bEnableSafe = (NewState == ECheckBoxState::Checked);
						if (!bEnableSafe)
						{
							if (!ConfirmUnsafeOption(FText::FromString(TEXT("Safe mode OFF allows broader command execution. I understand.")), TEXT("bUnsafeModeConfirmationAccepted"), bUnsafeModeConfirmationAccepted))
							{
								return;
							}
						}
						bSafeModeEnabled = bEnableSafe;
						NotifySettingChanged();
					})
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("Safe Mode (default ON)")))
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(24, 0, 8, 8)
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([this]() { return bAllowFilesystemInSafeMode ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
					{
						bAllowFilesystemInSafeMode = (NewState == ECheckBoxState::Checked);
						NotifySettingChanged();
					})
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("Allow Filesystem Routes In Safe Mode")))
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(24, 0, 8, 8)
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.Text(FText::FromString(TEXT("Keeps /asset/* and other filesystem-backed editor routes available while Safe Mode is on.")))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(8)
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([this]() { return bEnableExecRoute ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
					{
						const bool bEnable = (NewState == ECheckBoxState::Checked);
						if (bEnable)
						{
							if (!ConfirmUnsafeOption(FText::FromString(TEXT("Enable /ai/exec route? This allows remote command execution on localhost.")), TEXT("bExecRouteConfirmationAccepted"), bExecRouteConfirmationAccepted))
							{
								return;
							}
						}
						bEnableExecRoute = bEnable;
						NotifySettingChanged();
					})
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("Enable Exec Route")))
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(8)
				[
					SNew(SCheckBox)
					.IsEnabled_Lambda([this]() { return !bSafeModeEnabled; })
					.IsChecked_Lambda([this]() { return bAllowPythonExec ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
					{
						const bool bEnable = (NewState == ECheckBoxState::Checked);
						if (bEnable)
						{
							if (!ConfirmUnsafeOption(FText::FromString(TEXT("Enable Python execution? This requires unsafe mode and explicit opt-in.")), TEXT("bPythonConfirmationAccepted"), bPythonConfirmationAccepted))
							{
								return;
							}
						}
						bAllowPythonExec = bEnable;
						NotifySettingChanged();
					})
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("Enable Python Exec")))
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(8, 12, 8, 2)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("Permissions")))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("Requests must satisfy both route toggles above and permission toggles below.")))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(8)
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([this]() { return bPermissionEditor ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
					{
						bPermissionEditor = (NewState == ECheckBoxState::Checked);
						NotifySettingChanged();
					})
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("Permission: Editor")))
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(8)
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([this]() { return bPermissionBlueprint ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
					{
						bPermissionBlueprint = (NewState == ECheckBoxState::Checked);
						NotifySettingChanged();
					})
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("Permission: Blueprint")))
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(8)
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([this]() { return bPermissionPie ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
					{
						bPermissionPie = (NewState == ECheckBoxState::Checked);
						NotifySettingChanged();
					})
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("Permission: PIE")))
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(8)
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([this]() { return bPermissionExec ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
					{
						bPermissionExec = (NewState == ECheckBoxState::Checked);
						NotifySettingChanged();
					})
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("Permission: Exec")))
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(8)
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([this]() { return bPermissionPython ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
					{
						bPermissionPython = (NewState == ECheckBoxState::Checked);
						NotifySettingChanged();
					})
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("Permission: Python")))
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(8)
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([this]() { return bPermissionFilesystem ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
					{
						bPermissionFilesystem = (NewState == ECheckBoxState::Checked);
						NotifySettingChanged();
					})
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("Permission: Filesystem")))
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(8)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("Last 20 requests:")))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(8)
				[
					SNew(STextBlock)
					.Text_Lambda([this]() { return FText::FromString(BuildRequestStatsText()); })
				]
			]
		];
}

// -----------------------------------------------------------------------------
// Functional Module: HTTP Server Module Lifecycle
// -----------------------------------------------------------------------------
void FBlueprintAutomationToolkitModule::StartupModule()
{
	RegisterBATConsoleCommands();
	RegisterAutomationCommands();
	delete TokenAuthMiddleware;
	TokenAuthMiddleware = new FTokenAuthMiddleware();

	// In unattended (UAT) automation runs, restoring previously open asset tabs can open Blueprints/etc
	// and has caused shutdown-time asserts (BlueprintEditor preview world teardown).
	// Force-disable restore+save of open asset editors for these runs.
	if (FApp::IsUnattended())
	{
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8)
		FCoreDelegates::GetOnPostEngineInit().AddLambda([]()
#else
		FCoreDelegates::OnPostEngineInit.AddLambda([]()
#endif
		{
			if (GEditor)
			{
				if (UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
				{
					AssetEditorSubsystem->SetAutoRestoreAndDisableSavingOverride(false);
				}
			}
		});
	}

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda([this]()
	{
		RegisterControlPanelTab();
	}));

	GConfig->GetInt(TEXT("BlueprintAutomationToolkit"), TEXT("Port"), Port, GEditorPerProjectIni);
	const FString ProjectDefaultEditorIni = GetProjectDefaultEditorIniPath();
	bProjectConfigTokenAvailable = false;
	if (const UBlueprintAutomationToolkitSettings* Settings = GetDefault<UBlueprintAutomationToolkitSettings>())
	{
		Port = Settings->Port;
		bServerEnabled = Settings->bEnableServer;
		bRequireAuthToken = Settings->bRequireAuthToken;
		bSafeModeEnabled = Settings->bSafeMode;
	}

	GConfig->GetInt(TEXT("BlueprintAutomationToolkit"), TEXT("Port"), Port, GEditorPerProjectIni);
	if (!GConfig->GetBool(TEXT("BlueprintAutomationToolkit"), TEXT("bSaveTokenInProjectSettings"), bSaveTokenInProjectSettings, ProjectDefaultEditorIni))
	{
		GConfig->GetBool(TEXT("BlueprintAutomationToolkit"), TEXT("bSaveTokenInProjectSettings"), bSaveTokenInProjectSettings, GEditorPerProjectIni);
	}
	if (!GConfig->GetString(TEXT("BlueprintAutomationToolkit"), TEXT("AuthToken"), AuthToken, ProjectDefaultEditorIni) || AuthToken.IsEmpty())
	{
		GConfig->GetString(TEXT("BlueprintAutomationToolkit"), TEXT("AuthToken"), AuthToken, GEditorPerProjectIni);
	}
	else
	{
		bProjectConfigTokenAvailable = true;
	}
	GConfig->GetBool(TEXT("BlueprintAutomationToolkit"), TEXT("bServerEnabled"), bServerEnabled, GEditorPerProjectIni);
	GConfig->GetBool(TEXT("BlueprintAutomationToolkit"), TEXT("bRequireAuthToken"), bRequireAuthToken, GEditorPerProjectIni);
	GConfig->GetBool(TEXT("BlueprintAutomationToolkit"), TEXT("bPermissionPromptAnswered"), bPermissionPromptAnswered, GEditorPerProjectIni);
	GConfig->GetBool(TEXT("BlueprintAutomationToolkit"), TEXT("bAllowPythonExec"), bAllowPythonExec, GEditorPerProjectIni);
	GConfig->GetBool(TEXT("BlueprintAutomationToolkit"), TEXT("bEnableExecRoute"), bEnableExecRoute, GEditorPerProjectIni);
	if (!GConfig->GetBool(TEXT("BlueprintAutomationToolkit"), TEXT("bSafeModeEnabled"), bSafeModeEnabled, GEditorPerProjectIni))
	{
		if (!GConfig->GetBool(TEXT("BlueprintAutomationToolkit"), TEXT("bEnableSandbox"), bSafeModeEnabled, GEditorPerProjectIni))
		{
			GConfig->GetBool(TEXT("BlueprintAutomationToolkit"), TEXT("bCommandSandboxEnabled"), bSafeModeEnabled, GEditorPerProjectIni);
		}
	}
	GConfig->GetBool(TEXT("BlueprintAutomationToolkit"), TEXT("bAllowFilesystemInSafeMode"), bAllowFilesystemInSafeMode, GEditorPerProjectIni);
	GConfig->GetInt(TEXT("BlueprintAutomationToolkit"), TEXT("MaxRequestBodyBytes"), MaxRequestBodyBytes, GEditorPerProjectIni);
	GConfig->GetInt(TEXT("BlueprintAutomationToolkit"), TEXT("RateLimitPerSecond"), RateLimitPerSecond, GEditorPerProjectIni);
	GConfig->GetInt(TEXT("BlueprintAutomationToolkit"), TEXT("RateLimitBurst"), RateLimitBurst, GEditorPerProjectIni);
	GConfig->GetBool(TEXT("BlueprintAutomationToolkit"), TEXT("permissions.editor"), bPermissionEditor, GEditorPerProjectIni);
	GConfig->GetBool(TEXT("BlueprintAutomationToolkit"), TEXT("permissions.blueprint"), bPermissionBlueprint, GEditorPerProjectIni);
	GConfig->GetBool(TEXT("BlueprintAutomationToolkit"), TEXT("permissions.pie"), bPermissionPie, GEditorPerProjectIni);
	GConfig->GetBool(TEXT("BlueprintAutomationToolkit"), TEXT("permissions.exec"), bPermissionExec, GEditorPerProjectIni);
	GConfig->GetBool(TEXT("BlueprintAutomationToolkit"), TEXT("permissions.python"), bPermissionPython, GEditorPerProjectIni);
	GConfig->GetBool(TEXT("BlueprintAutomationToolkit"), TEXT("permissions.filesystem"), bPermissionFilesystem, GEditorPerProjectIni);
	GConfig->GetBool(TEXT("BlueprintAutomationToolkit"), TEXT("bEnableHmacAuth"), bEnableHmacAuth, GEditorPerProjectIni);
	GConfig->GetInt(TEXT("BlueprintAutomationToolkit"), TEXT("MaxClockSkewSeconds"), MaxClockSkewSeconds, GEditorPerProjectIni);
	GConfig->GetInt(TEXT("BlueprintAutomationToolkit"), TEXT("LogRingSize"), LogRingSize, GEditorPerProjectIni);
	GConfig->GetInt(TEXT("BlueprintAutomationToolkit"), TEXT("MaxOpsPerPlan"), MaxOpsPerPlan, GEditorPerProjectIni);
	GConfig->GetInt(TEXT("BlueprintAutomationToolkit"), TEXT("MaxActorsPerLayout"), MaxActorsPerLayout, GEditorPerProjectIni);
	GConfig->GetInt(TEXT("BlueprintAutomationToolkit"), TEXT("MaxInstancesPerOp"), MaxInstancesPerOp, GEditorPerProjectIni);
	GConfig->GetInt(TEXT("BlueprintAutomationToolkit"), TEXT("MaxTotalInstancesPerPlan"), MaxTotalInstancesPerPlan, GEditorPerProjectIni);
	GConfig->GetBool(TEXT("BlueprintAutomationToolkit"), TEXT("bUnsafeModeConfirmationAccepted"), bUnsafeModeConfirmationAccepted, GEditorPerProjectIni);
	GConfig->GetBool(TEXT("BlueprintAutomationToolkit"), TEXT("bExecRouteConfirmationAccepted"), bExecRouteConfirmationAccepted, GEditorPerProjectIni);
	GConfig->GetBool(TEXT("BlueprintAutomationToolkit"), TEXT("bPythonConfirmationAccepted"), bPythonConfirmationAccepted, GEditorPerProjectIni);
	GConfig->GetArray(TEXT("BlueprintAutomationToolkit"), TEXT("CommandSandboxAllowPrefixes"), CommandSandboxAllowPrefixes, GEditorPerProjectIni);
	GConfig->GetArray(TEXT("BlueprintAutomationToolkit"), TEXT("CommandSandboxBlockSubstrings"), CommandSandboxBlockSubstrings, GEditorPerProjectIni);

	{
		TArray<FString> AllowedFunctions;
		GConfig->GetArray(TEXT("BlueprintAutomationToolkit"), TEXT("AllowedUObjectFunctions"), AllowedFunctions, GEditorPerProjectIni);
		AllowedUObjectFunctions.Reset();
		for (FString Name : AllowedFunctions)
		{
			Name.TrimStartAndEndInline();
			if (!Name.IsEmpty())
			{
				AllowedUObjectFunctions.Add(FName(*Name));
			}
		}
		if (AllowedUObjectFunctions.Num() == 0)
		{
			AllowedUObjectFunctions.Add(FName(TEXT("K2_SetActorLocation")));
			AllowedUObjectFunctions.Add(FName(TEXT("SetActorHiddenInGame")));
			AllowedUObjectFunctions.Add(FName(TEXT("SetActorScale3D")));
		}

		TArray<FString> AllowedProperties;
		GConfig->GetArray(TEXT("BlueprintAutomationToolkit"), TEXT("AllowedUObjectProperties"), AllowedProperties, GEditorPerProjectIni);
		AllowedUObjectProperties.Reset();
		for (FString Name : AllowedProperties)
		{
			Name.TrimStartAndEndInline();
			if (!Name.IsEmpty())
			{
				AllowedUObjectProperties.Add(FName(*Name));
			}
		}

		auto NormalizeClassList = [](const TArray<FString>& Source, TSet<FString>& Destination)
		{
			Destination.Reset();
			for (FString Value : Source)
			{
				Value.TrimStartAndEndInline();
				Value = Value.ToLower();
				if (!Value.IsEmpty())
				{
					Destination.Add(Value);
				}
			}
		};

		auto NormalizeNameList = [](const TArray<FString>& Source, TSet<FName>& Destination)
		{
			Destination.Reset();
			for (FString Value : Source)
			{
				Value.TrimStartAndEndInline();
				if (!Value.IsEmpty())
				{
					Destination.Add(FName(*Value));
				}
			}
		};

		TArray<FString> ReflectionAllowedClassesList;
		TArray<FString> ReflectionDeniedClassesList;
		TArray<FString> ReflectionAllowedFunctionsList;
		TArray<FString> ReflectionDeniedFunctionsList;
		TArray<FString> ReflectionAllowedPropertiesList;
		TArray<FString> ReflectionDeniedPropertiesList;
		GConfig->GetArray(TEXT("BlueprintAutomationToolkit"), TEXT("ReflectionAllowedClasses"), ReflectionAllowedClassesList, GEditorPerProjectIni);
		GConfig->GetArray(TEXT("BlueprintAutomationToolkit"), TEXT("ReflectionDeniedClasses"), ReflectionDeniedClassesList, GEditorPerProjectIni);
		GConfig->GetArray(TEXT("BlueprintAutomationToolkit"), TEXT("ReflectionAllowedFunctions"), ReflectionAllowedFunctionsList, GEditorPerProjectIni);
		GConfig->GetArray(TEXT("BlueprintAutomationToolkit"), TEXT("ReflectionDeniedFunctions"), ReflectionDeniedFunctionsList, GEditorPerProjectIni);
		GConfig->GetArray(TEXT("BlueprintAutomationToolkit"), TEXT("ReflectionAllowedProperties"), ReflectionAllowedPropertiesList, GEditorPerProjectIni);
		GConfig->GetArray(TEXT("BlueprintAutomationToolkit"), TEXT("ReflectionDeniedProperties"), ReflectionDeniedPropertiesList, GEditorPerProjectIni);

		NormalizeClassList(ReflectionAllowedClassesList, AllowedReflectionClasses);
		NormalizeClassList(ReflectionDeniedClassesList, DeniedReflectionClasses);
		NormalizeNameList(ReflectionAllowedFunctionsList, AllowedReflectionFunctions);
		NormalizeNameList(ReflectionDeniedFunctionsList, DeniedReflectionFunctions);
		NormalizeNameList(ReflectionAllowedPropertiesList, AllowedReflectionProperties);
		NormalizeNameList(ReflectionDeniedPropertiesList, DeniedReflectionProperties);

		if (AllowedReflectionClasses.Num() == 0)
		{
			AllowedReflectionClasses.Add(TEXT("/script/engine.actor"));
			AllowedReflectionClasses.Add(TEXT("/script/engine.actorcomponent"));
			AllowedReflectionClasses.Add(TEXT("/script/engine.scenecomponent"));
		}

		if (DeniedReflectionFunctions.Num() == 0)
		{
			DeniedReflectionFunctions.Add(FName(TEXT("ExecuteConsoleCommand")));
			DeniedReflectionFunctions.Add(FName(TEXT("ConsoleCommand")));
			DeniedReflectionFunctions.Add(FName(TEXT("CallFunctionByNameWithArguments")));
		}

		if (AllowedReflectionFunctions.Num() == 0)
		{
			AllowedReflectionFunctions.Add(FName(TEXT("UpdatePoseFromAnimation")));
		}

		GConfig->GetArray(TEXT("BlueprintAutomationToolkit"), TEXT("AllowedActionAssetPrefixes"), AllowedActionAssetPrefixes, GEditorPerProjectIni);
		if (AllowedActionAssetPrefixes.Num() == 0)
		{
			AllowedActionAssetPrefixes = {
				TEXT("/Game/BAT/Actions"),
				TEXT("/BlueprintAutomationToolkit/Actions")
			};
		}
	}

	// Scoped token format:
	// +ScopedTokenEntries=name|token|scope1,scope2|2026-12-31T23:59:59Z|optionalSecret
	ScopedTokens.Reset();
	TArray<FString> ScopedTokenEntries;
	GConfig->GetArray(TEXT("BlueprintAutomationToolkit"), TEXT("ScopedTokenEntries"), ScopedTokenEntries, GEditorPerProjectIni);
	for (const FString& Entry : ScopedTokenEntries)
	{
		TArray<FString> Parts;
		Entry.ParseIntoArray(Parts, TEXT("|"), false);
		if (Parts.Num() < 3)
		{
			continue;
		}

		FTokenRecord Token;
		Token.Name = Parts[0].TrimStartAndEnd();
		Token.Token = Parts[1].TrimStartAndEnd();
		Token.PermissionsMask = 0u;

		TArray<FString> Scopes;
		Parts[2].ParseIntoArray(Scopes, TEXT(","), true);
		for (FString Scope : Scopes)
		{
			Scope.TrimStartAndEndInline();
			if (Scope.Equals(TEXT("editor"), ESearchCase::IgnoreCase)) Token.PermissionsMask |= static_cast<uint32>(EBATPermission::Editor);
			else if (Scope.Equals(TEXT("blueprint"), ESearchCase::IgnoreCase)) Token.PermissionsMask |= static_cast<uint32>(EBATPermission::Blueprint);
			else if (Scope.Equals(TEXT("pie"), ESearchCase::IgnoreCase)) Token.PermissionsMask |= static_cast<uint32>(EBATPermission::Pie);
			else if (Scope.Equals(TEXT("exec"), ESearchCase::IgnoreCase)) Token.PermissionsMask |= static_cast<uint32>(EBATPermission::Exec);
			else if (Scope.Equals(TEXT("python"), ESearchCase::IgnoreCase)) Token.PermissionsMask |= static_cast<uint32>(EBATPermission::Python);
			else if (Scope.Equals(TEXT("filesystem"), ESearchCase::IgnoreCase)) Token.PermissionsMask |= static_cast<uint32>(EBATPermission::Filesystem);
		}

		if (Parts.Num() >= 4)
		{
			const FString Expiry = Parts[3].TrimStartAndEnd();
			if (!Expiry.IsEmpty())
			{
				FDateTime Parsed;
				if (FDateTime::ParseIso8601(*Expiry, Parsed))
				{
					Token.bHasExpiry = true;
					Token.ExpiresUtc = Parsed;
				}
			}
		}

		if (Parts.Num() >= 5)
		{
			Token.Secret = Parts[4].TrimStartAndEnd();
		}

		if (!Token.Token.IsEmpty())
		{
			ScopedTokens.Add(MoveTemp(Token));
		}
	}
	ApplyDefaultSandboxPolicy();

	const FString EnvToken = FPlatformMisc::GetEnvironmentVariable(TEXT("BAT_AUTH_TOKEN"));
	bAuthTokenFromEnv = !EnvToken.IsEmpty();
	RuntimeAuthToken = bAuthTokenFromEnv ? EnvToken : AuthToken;
	UE_LOG(
		LogBlueprintAutomationToolkit,
		Log,
		TEXT("StartupModule auth state: project_ini=%s save_to_project=%s project_auth=%s env_override=%s env_auth=%s runtime_auth=%s"),
		*ProjectDefaultEditorIni,
		bSaveTokenInProjectSettings ? TEXT("true") : TEXT("false"),
		*DescribeTokenForLog(AuthToken),
		bAuthTokenFromEnv ? TEXT("true") : TEXT("false"),
		*DescribeTokenForLog(EnvToken),
		*DescribeTokenForLog(RuntimeAuthToken));

	if (RuntimeAuthToken.IsEmpty() && !bAuthTokenFromEnv)
	{
		AuthToken = GenerateStrongAuthToken();
		RuntimeAuthToken = AuthToken;
		PersistSettings(true);
		UE_LOG(LogBlueprintAutomationToolkit, Warning, TEXT("AuthToken was empty. Generated a new token (%s)."), bSaveTokenInProjectSettings ? TEXT("saved to project settings") : TEXT("session only"));
	}

	PersistSettings(!bAuthTokenFromEnv);

	if (IsRunningCommandlet())
	{
		UE_LOG(LogBlueprintAutomationToolkit, Log, TEXT("Server startup skipped while running commandlet."));
		return;
	}

	if (!bServerEnabled)
	{
			UE_LOG(LogBlueprintAutomationToolkit, Log, TEXT("Server disabled by default. Use the Blueprint Automation Toolkit panel to start it manually."));
		return;
	}

	if (!EnsureServerPermissionGranted())
	{
		UE_LOG(LogBlueprintAutomationToolkit, Warning, TEXT("Server startup skipped: user has not granted permission."));
		return;
	}

	StartServer(false);
}

void FBlueprintAutomationToolkitModule::ShutdownModule()
{
	UnregisterBATConsoleCommands();
	UnregisterControlPanelTab();
	delete CommandDispatcher;
	CommandDispatcher = nullptr;
	delete TokenAuthMiddleware;
	TokenAuthMiddleware = nullptr;

	if (Wander && Wander->Handle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(Wander->Handle);
		Wander.Reset();
	}

	StopServer();
}

// -----------------------------------------------------------------------------
// Functional Module: HTTP Route Binding
// -----------------------------------------------------------------------------

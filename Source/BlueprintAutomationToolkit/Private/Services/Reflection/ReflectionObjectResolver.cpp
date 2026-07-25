// Copyright 2026 AkaSoft. All Rights Reserved.

#include "Services/Reflection/ReflectionObjectResolver.h"

#include "BlueprintAutomationToolkitModule.h"
#include "Services/Reflection/ReflectionTypes.h"

#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/Selection.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Misc/PackageName.h"
#include "UObject/SoftObjectPath.h"

namespace
{
	static FString NormalizeObjectPath(const FString& InPath)
	{
		FString Path = InPath;
		Path.TrimStartAndEndInline();
		if (Path.IsEmpty())
		{
			return Path;
		}

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

	static FString MakeGeneratedClassPath(const FString& InPath)
	{
		const FString AssetObjectPath = NormalizeObjectPath(InPath);
		int32 DotIndex = INDEX_NONE;
		if (!AssetObjectPath.FindLastChar(TEXT('.'), DotIndex) || DotIndex + 1 >= AssetObjectPath.Len())
		{
			return AssetObjectPath;
		}

		const FString ObjectName = AssetObjectPath.Mid(DotIndex + 1);
		if (ObjectName.EndsWith(TEXT("_C"), ESearchCase::CaseSensitive))
		{
			return AssetObjectPath;
		}

		return FString::Printf(TEXT("%s.%s_C"), *AssetObjectPath.Left(DotIndex), *ObjectName);
	}

	static UObject* ResolveObjectByPathInternal(const FString& InPath)
	{
		FString Path = InPath;
		Path.TrimStartAndEndInline();
		if (Path.IsEmpty())
		{
			return nullptr;
		}

		if (UObject* Found = FindObject<UObject>(nullptr, *Path))
		{
			return Found;
		}

		if (UObject* Found = StaticFindObject(UObject::StaticClass(), nullptr, *Path))
		{
			return Found;
		}

		if (UObject* Loaded = LoadObject<UObject>(nullptr, *Path))
		{
			return Loaded;
		}

		const FString Normalized = NormalizeObjectPath(Path);
		if (!Normalized.Equals(Path, ESearchCase::CaseSensitive))
		{
			if (UObject* Found = FindObject<UObject>(nullptr, *Normalized))
			{
				return Found;
			}
			if (UObject* Found = StaticFindObject(UObject::StaticClass(), nullptr, *Normalized))
			{
				return Found;
			}
			if (UObject* Loaded = LoadObject<UObject>(nullptr, *Normalized))
			{
				return Loaded;
			}
		}

		FSoftObjectPath SoftPath(Path);
		if (UObject* Resolved = SoftPath.ResolveObject())
		{
			return Resolved;
		}

		if (UObject* Loaded = SoftPath.TryLoad())
		{
			return Loaded;
		}

		if (!Normalized.Equals(Path, ESearchCase::CaseSensitive))
		{
			FSoftObjectPath NormalizedSoftPath(Normalized);
			if (UObject* Resolved = NormalizedSoftPath.ResolveObject())
			{
				return Resolved;
			}
			return NormalizedSoftPath.TryLoad();
		}

		return nullptr;
	}

	static AActor* ResolveActorByNameInWorld(UWorld* World, const FString& ActorName)
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

			if (Actor->GetName().Equals(ActorName, ESearchCase::IgnoreCase)
				|| Actor->GetPathName().Equals(ActorName, ESearchCase::IgnoreCase))
			{
				return Actor;
			}

#if WITH_EDITOR
			if (Actor->GetActorLabel().Equals(ActorName, ESearchCase::IgnoreCase))
			{
				return Actor;
			}
#endif
		}

		return nullptr;
	}
}

void FReflectionObjectResolver::SetLastError(const FString& Message) const
{
	LastErrorMessage = Message;
}

UObject* FReflectionObjectResolver::ResolveObjectByPath(const FString& ObjectPath) const
{
	SetLastError(TEXT(""));

	const FString TrimmedPath = ObjectPath.TrimStartAndEnd();
	if (TrimmedPath.IsEmpty())
	{
		SetLastError(TEXT("Object path must be non-empty."));
		return nullptr;
	}

	if (UObject* ResolvedObject = ResolveObjectByPathInternal(TrimmedPath))
	{
		return ResolvedObject;
	}

	SetLastError(FString::Printf(TEXT("Object '%s' could not be resolved from full object path or soft object path."), *TrimmedPath));
	return nullptr;
}

AActor* FReflectionObjectResolver::ResolveActorByName(const FString& ActorName) const
{
	SetLastError(TEXT(""));

	const FString TrimmedName = ActorName.TrimStartAndEnd();
	if (TrimmedName.IsEmpty())
	{
		SetLastError(TEXT("Actor name must be non-empty."));
		return nullptr;
	}

#if WITH_EDITOR
	if (!GEditor)
	{
		SetLastError(TEXT("GEditor is unavailable."));
		return nullptr;
	}

	UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
	if (!EditorWorld)
	{
		SetLastError(TEXT("Editor world context could not be resolved."));
		return nullptr;
	}

	if (AActor* Actor = ResolveActorByNameInWorld(EditorWorld, TrimmedName))
	{
		return Actor;
	}
#else
	SetLastError(TEXT("Actor name resolution requires editor world access."));
	return nullptr;
#endif

	SetLastError(FString::Printf(TEXT("Actor '%s' was not found in the current editor world."), *TrimmedName));
	return nullptr;
}

bool FReflectionObjectResolver::ResolveObjectReference(const FString& ReferencePath, UClass* RequiredClass, UObject*& OutObject) const
{
	OutObject = ResolveObjectByPath(ReferencePath);
	return OutObject != nullptr && (!RequiredClass || OutObject->IsA(RequiredClass));
}

bool FReflectionObjectResolver::ResolveClassReference(const FString& ReferencePath, UClass*& OutClass) const
{
	OutClass = nullptr;

	auto TryClassPath = [&OutClass](const FString& Candidate) -> bool
	{
		FString Path = Candidate;
		Path.TrimStartAndEndInline();
		if (Path.IsEmpty())
		{
			return false;
		}

		OutClass = FindObject<UClass>(nullptr, *Path);
		if (!OutClass)
		{
			OutClass = LoadObject<UClass>(nullptr, *Path);
		}
		if (!OutClass)
		{
			OutClass = LoadClass<UObject>(nullptr, *Path);
		}

		return OutClass != nullptr;
	};

	const FString Trimmed = ReferencePath.TrimStartAndEnd();
	if (TryClassPath(Trimmed))
	{
		return true;
	}

	const FString Normalized = NormalizeObjectPath(Trimmed);
	if (!Normalized.Equals(Trimmed, ESearchCase::CaseSensitive) && TryClassPath(Normalized))
	{
		return true;
	}

	const FString GeneratedClassPath = MakeGeneratedClassPath(Trimmed);
	if (!GeneratedClassPath.Equals(Trimmed, ESearchCase::CaseSensitive) && TryClassPath(GeneratedClassPath))
	{
		return true;
	}

	UBlueprint* Blueprint = FindObject<UBlueprint>(nullptr, *Normalized);
	if (!Blueprint)
	{
		Blueprint = LoadObject<UBlueprint>(nullptr, *Normalized);
	}
	if (Blueprint && Blueprint->GeneratedClass)
	{
		OutClass = Blueprint->GeneratedClass;
		return true;
	}

	return false;
}

bool FReflectionObjectResolver::Resolve(FBlueprintAutomationToolkitModule& Module, const TSharedPtr<FJsonObject>& BodyObj, const FString& RequestId, BAT::Reflection::FResolvedObject& OutResolved, FAutomationResult& OutFailure) const
{
	OutResolved = BAT::Reflection::FResolvedObject();
	if (!BodyObj.IsValid())
	{
		OutFailure = BAT::Reflection::MakeStructuredError(RequestId, TEXT("bad_args"), TEXT("Invalid JSON body"), 400);
		return false;
	}

	FString RequiredClassPath;
	BodyObj->TryGetStringField(TEXT("requiredClass"), RequiredClassPath);
	if (RequiredClassPath.IsEmpty())
	{
		BodyObj->TryGetStringField(TEXT("requiredClassPath"), RequiredClassPath);
	}
	if (!RequiredClassPath.TrimStartAndEnd().IsEmpty())
	{
		if (!ResolveClassReference(RequiredClassPath, OutResolved.RequiredClass))
		{
			OutFailure = BAT::Reflection::MakeStructuredError(RequestId, TEXT("invalid_type"), FString::Printf(TEXT("Required class '%s' could not be resolved."), *RequiredClassPath), 400);
			return false;
		}
	}

	FString ObjectPath;
	FString SoftObjectPath;
	FString BlueprintAssetPath;
	FString ClassPath;
	FString ActorName;
	FString WorldContext = TEXT("Editor");
	double PieIndexRaw = 0.0;
	bool bSelectedActor = false;

	BodyObj->TryGetStringField(TEXT("objectPath"), ObjectPath);
	BodyObj->TryGetStringField(TEXT("softObjectPath"), SoftObjectPath);
	BodyObj->TryGetStringField(TEXT("blueprintAssetPath"), BlueprintAssetPath);
	BodyObj->TryGetStringField(TEXT("classPath"), ClassPath);
	BodyObj->TryGetStringField(TEXT("actorName"), ActorName);
	BodyObj->TryGetStringField(TEXT("worldContext"), WorldContext);
	BodyObj->TryGetBoolField(TEXT("selectedActor"), bSelectedActor);
	BodyObj->TryGetNumberField(TEXT("pieIndex"), PieIndexRaw);

	if (!ObjectPath.TrimStartAndEnd().IsEmpty())
	{
		OutResolved.Object = ResolveObjectByPath(ObjectPath);
		OutResolved.ResolutionSource = TEXT("objectPath");
	}
	else if (!SoftObjectPath.TrimStartAndEnd().IsEmpty())
	{
		OutResolved.Object = ResolveObjectByPath(SoftObjectPath);
		OutResolved.ResolutionSource = TEXT("softObjectPath");
	}
	else if (!BlueprintAssetPath.TrimStartAndEnd().IsEmpty())
	{
		OutResolved.Object = ResolveObjectByPath(NormalizeObjectPath(BlueprintAssetPath));
		OutResolved.ResolutionSource = TEXT("blueprintAssetPath");
	}
	else if (!ClassPath.TrimStartAndEnd().IsEmpty())
	{
		UClass* ResolvedClass = nullptr;
		if (!ResolveClassReference(ClassPath, ResolvedClass))
		{
			OutFailure = BAT::Reflection::MakeStructuredError(RequestId, TEXT("not_found"), FString::Printf(TEXT("Class '%s' could not be resolved."), *ClassPath), 404);
			return false;
		}

		OutResolved.Object = ResolvedClass;
		OutResolved.ResolutionSource = TEXT("classPath");
	}
	else if (!ActorName.TrimStartAndEnd().IsEmpty() || bSelectedActor)
	{
		FString ResolveWorldError;
		bool bIsPie = false;
		int32 ResolvedPieIndex = 0;
		UWorld* World = Module.ResolveWorld(WorldContext, static_cast<int32>(PieIndexRaw), bIsPie, ResolvedPieIndex, ResolveWorldError);
		if (!World)
		{
			OutFailure = BAT::Reflection::MakeStructuredError(RequestId, TEXT("not_found"), ResolveWorldError.IsEmpty() ? TEXT("World context could not be resolved.") : ResolveWorldError, 404);
			return false;
		}

		if (bSelectedActor)
		{
#if WITH_EDITOR
			if (GEditor && GEditor->GetSelectedActors())
			{
				OutResolved.Object = Cast<AActor>(GEditor->GetSelectedActors()->GetTop(AActor::StaticClass()));
			}
#endif
			OutResolved.ResolutionSource = TEXT("selectedActor");
		}
		else
		{
			OutResolved.Object = ResolveActorByNameInWorld(World, ActorName);
			OutResolved.ResolutionSource = TEXT("actorName");
		}
	}
	else
	{
		OutFailure = BAT::Reflection::MakeStructuredError(RequestId, TEXT("bad_args"), TEXT("Request must include one of: objectPath, softObjectPath, actorName, selectedActor, blueprintAssetPath, or classPath."), 400);
		return false;
	}

	if (!OutResolved.Object)
	{
		const FString ResolverMessage = GetLastErrorMessage();
		OutFailure = BAT::Reflection::MakeStructuredError(RequestId, TEXT("not_found"), ResolverMessage.IsEmpty() ? TEXT("Requested object could not be resolved.") : ResolverMessage, 404);
		return false;
	}

	if (OutResolved.RequiredClass && !OutResolved.Object->IsA(OutResolved.RequiredClass))
	{
		OutFailure = BAT::Reflection::MakeStructuredError(
			RequestId,
			TEXT("invalid_type"),
			FString::Printf(TEXT("Resolved object '%s' is not of required type '%s'."), *OutResolved.Object->GetPathName(), *OutResolved.RequiredClass->GetPathName()),
			400);
		return false;
	}

	OutResolved.ResolvedObjectPath = OutResolved.Object->GetPathName();
	return true;
}
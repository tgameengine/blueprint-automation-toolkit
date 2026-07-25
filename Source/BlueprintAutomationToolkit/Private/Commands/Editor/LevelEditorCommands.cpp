// Copyright 2026 AkaSoft. All Rights Reserved.

#include "Commands/Editor/LevelEditorCommands.h"

#include "BlueprintAutomationToolkitModule.h"
#include "Core/EditorExecution.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ScopedTransaction.h"
#include "Services/Reflection/ReflectionTypes.h"

namespace
{
	struct FBATLevelActorFilter
	{
		TArray<FString> Labels;
		TArray<FString> Names;
		TArray<FString> ClassNames;
		TArray<FString> Tags;
		FString Label;
		FString LabelPrefix;
		FString LabelSuffix;
		FString LabelContains;
		FString LabelNotContains;
		FString Name;
		FString ClassName;
		FString ClassContains;
		FString FolderPath;
		FString FolderPrefix;
		FString FolderContains;
		bool bIncludeHidden = true;
		bool bIncludeTransient = false;
		bool bHasAnyFilter = false;
		int32 Limit = 200;
	};

	static FString Trimmed(FString Value)
	{
		Value.TrimStartAndEndInline();
		return Value;
	}

	static bool IsNonEmpty(const FString& Value)
	{
		return !Value.TrimStartAndEnd().IsEmpty();
	}

	static bool TryGetTrimmedString(const TSharedPtr<FJsonObject>& Body, const TCHAR* FieldName, FString& OutValue)
	{
		if (!Body.IsValid() || !Body->TryGetStringField(FieldName, OutValue))
		{
			return false;
		}

		OutValue.TrimStartAndEndInline();
		return !OutValue.IsEmpty();
	}

	static void ReadStringArray(const TSharedPtr<FJsonObject>& Body, const TCHAR* FieldName, TArray<FString>& OutValues, bool& bHasAnyFilter)
	{
		if (!Body.IsValid())
		{
			return;
		}

		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Body->TryGetArrayField(FieldName, Values) || !Values)
		{
			return;
		}

		for (const TSharedPtr<FJsonValue>& Value : *Values)
		{
			if (!Value.IsValid() || Value->Type != EJson::String)
			{
				continue;
			}

			FString Text = Value->AsString();
			Text.TrimStartAndEndInline();
			if (!Text.IsEmpty())
			{
				OutValues.Add(Text);
			}
		}

		bHasAnyFilter = bHasAnyFilter || OutValues.Num() > 0;
	}

	static bool AnyStringEquals(const TArray<FString>& Values, const FString& Candidate)
	{
		for (const FString& Value : Values)
		{
			if (Candidate.Equals(Value, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	}

	static bool AnyStringEqualsEither(const TArray<FString>& Values, const FString& CandidateA, const FString& CandidateB)
	{
		for (const FString& Value : Values)
		{
			if (CandidateA.Equals(Value, ESearchCase::IgnoreCase) || CandidateB.Equals(Value, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	}

	static FString GetActorLabelSafe(const AActor* Actor)
	{
		if (!Actor)
		{
			return FString();
		}

#if WITH_EDITOR
		return Actor->GetActorLabel();
#else
		return Actor->GetName();
#endif
	}

	static FString GetActorFolderPathSafe(const AActor* Actor)
	{
		if (!Actor)
		{
			return FString();
		}

#if WITH_EDITOR
		return Actor->GetFolderPath().ToString();
#else
		return FString();
#endif
	}

	static bool ClassMatches(const AActor* Actor, const FString& Query)
	{
		if (!Actor || Query.TrimStartAndEnd().IsEmpty())
		{
			return false;
		}

		const UClass* Class = Actor->GetClass();
		if (!Class)
		{
			return false;
		}

		const FString TrimmedQuery = Query.TrimStartAndEnd();
		const FString ClassName = Class->GetName();
		const FString ClassPath = Class->GetPathName();
		const FString ClassScriptPath = Class->GetClassPathName().ToString();
		const FString APrefixedClassName = FString::Printf(TEXT("A%s"), *ClassName);

		return ClassName.Equals(TrimmedQuery, ESearchCase::IgnoreCase)
			|| APrefixedClassName.Equals(TrimmedQuery, ESearchCase::IgnoreCase)
			|| ClassPath.Equals(TrimmedQuery, ESearchCase::IgnoreCase)
			|| ClassScriptPath.Equals(TrimmedQuery, ESearchCase::IgnoreCase)
			|| ClassPath.Contains(TrimmedQuery, ESearchCase::IgnoreCase)
			|| ClassScriptPath.Contains(TrimmedQuery, ESearchCase::IgnoreCase);
	}

	static bool ClassContains(const AActor* Actor, const FString& Query)
	{
		if (!Actor || Query.TrimStartAndEnd().IsEmpty())
		{
			return false;
		}

		const UClass* Class = Actor->GetClass();
		if (!Class)
		{
			return false;
		}

		const FString TrimmedQuery = Query.TrimStartAndEnd();
		return Class->GetName().Contains(TrimmedQuery, ESearchCase::IgnoreCase)
			|| Class->GetPathName().Contains(TrimmedQuery, ESearchCase::IgnoreCase)
			|| Class->GetClassPathName().ToString().Contains(TrimmedQuery, ESearchCase::IgnoreCase);
	}

	static bool ActorHasAnyTag(const AActor* Actor, const TArray<FString>& Tags)
	{
		if (!Actor || Tags.Num() == 0)
		{
			return false;
		}

		for (const FString& Tag : Tags)
		{
			if (Actor->ActorHasTag(FName(*Tag)))
			{
				return true;
			}
		}
		return false;
	}

	static void MarkFilterIfSet(const FString& Value, bool& bHasAnyFilter)
	{
		bHasAnyFilter = bHasAnyFilter || IsNonEmpty(Value);
	}

	static FBATLevelActorFilter BuildLevelActorFilter(const TSharedPtr<FJsonObject>& Body)
	{
		FBATLevelActorFilter Filter;
		if (!Body.IsValid())
		{
			return Filter;
		}

		TryGetTrimmedString(Body, TEXT("label"), Filter.Label);
		TryGetTrimmedString(Body, TEXT("actorLabel"), Filter.Label);
		TryGetTrimmedString(Body, TEXT("labelPrefix"), Filter.LabelPrefix);
		TryGetTrimmedString(Body, TEXT("labelSuffix"), Filter.LabelSuffix);
		TryGetTrimmedString(Body, TEXT("labelContains"), Filter.LabelContains);
		TryGetTrimmedString(Body, TEXT("labelNotContains"), Filter.LabelNotContains);
		TryGetTrimmedString(Body, TEXT("name"), Filter.Name);
		TryGetTrimmedString(Body, TEXT("actorName"), Filter.Name);
		TryGetTrimmedString(Body, TEXT("class"), Filter.ClassName);
		TryGetTrimmedString(Body, TEXT("className"), Filter.ClassName);
		TryGetTrimmedString(Body, TEXT("classContains"), Filter.ClassContains);
		TryGetTrimmedString(Body, TEXT("folder"), Filter.FolderPath);
		TryGetTrimmedString(Body, TEXT("folderPath"), Filter.FolderPath);
		TryGetTrimmedString(Body, TEXT("folderPrefix"), Filter.FolderPrefix);
		TryGetTrimmedString(Body, TEXT("folderContains"), Filter.FolderContains);

		ReadStringArray(Body, TEXT("labels"), Filter.Labels, Filter.bHasAnyFilter);
		ReadStringArray(Body, TEXT("names"), Filter.Names, Filter.bHasAnyFilter);
		ReadStringArray(Body, TEXT("classes"), Filter.ClassNames, Filter.bHasAnyFilter);
		ReadStringArray(Body, TEXT("classNames"), Filter.ClassNames, Filter.bHasAnyFilter);
		ReadStringArray(Body, TEXT("tags"), Filter.Tags, Filter.bHasAnyFilter);

		FString SingleTag;
		if (TryGetTrimmedString(Body, TEXT("tag"), SingleTag))
		{
			Filter.Tags.AddUnique(SingleTag);
			Filter.bHasAnyFilter = true;
		}

		FString By;
		FString Value;
		if (TryGetTrimmedString(Body, TEXT("by"), By) && TryGetTrimmedString(Body, TEXT("value"), Value))
		{
			if (By.Equals(TEXT("label"), ESearchCase::IgnoreCase))
			{
				Filter.Label = Value;
			}
			else if (By.Equals(TEXT("name"), ESearchCase::IgnoreCase))
			{
				Filter.Name = Value;
			}
			else if (By.Equals(TEXT("class"), ESearchCase::IgnoreCase))
			{
				Filter.ClassName = Value;
			}
			else if (By.Equals(TEXT("tag"), ESearchCase::IgnoreCase))
			{
				Filter.Tags.AddUnique(Value);
			}
			else if (By.Equals(TEXT("folder"), ESearchCase::IgnoreCase))
			{
				Filter.FolderPath = Value;
			}
		}

		Body->TryGetBoolField(TEXT("includeHidden"), Filter.bIncludeHidden);
		Body->TryGetBoolField(TEXT("includeTransient"), Filter.bIncludeTransient);

		double LimitValue = Filter.Limit;
		if (Body->TryGetNumberField(TEXT("limit"), LimitValue))
		{
			Filter.Limit = FMath::Clamp(static_cast<int32>(LimitValue), 1, 5000);
		}

		MarkFilterIfSet(Filter.Label, Filter.bHasAnyFilter);
		MarkFilterIfSet(Filter.LabelPrefix, Filter.bHasAnyFilter);
		MarkFilterIfSet(Filter.LabelSuffix, Filter.bHasAnyFilter);
		MarkFilterIfSet(Filter.LabelContains, Filter.bHasAnyFilter);
		MarkFilterIfSet(Filter.LabelNotContains, Filter.bHasAnyFilter);
		MarkFilterIfSet(Filter.Name, Filter.bHasAnyFilter);
		MarkFilterIfSet(Filter.ClassName, Filter.bHasAnyFilter);
		MarkFilterIfSet(Filter.ClassContains, Filter.bHasAnyFilter);
		MarkFilterIfSet(Filter.FolderPath, Filter.bHasAnyFilter);
		MarkFilterIfSet(Filter.FolderPrefix, Filter.bHasAnyFilter);
		MarkFilterIfSet(Filter.FolderContains, Filter.bHasAnyFilter);

		return Filter;
	}

	static bool ActorMatchesFilter(const AActor* Actor, const FBATLevelActorFilter& Filter)
	{
		if (!Actor || !IsValid(Actor) || Actor->IsTemplate())
		{
			return false;
		}

		if (!Filter.bIncludeTransient && Actor->HasAnyFlags(RF_Transient))
		{
			return false;
		}

#if WITH_EDITOR
		if (!Filter.bIncludeHidden && Actor->IsHiddenEd())
		{
			return false;
		}
#endif

		const FString Label = GetActorLabelSafe(Actor);
		const FString Name = Actor->GetName();
		const FString FolderPath = GetActorFolderPathSafe(Actor);

		if (Filter.Labels.Num() > 0 && !AnyStringEqualsEither(Filter.Labels, Label, Name))
		{
			return false;
		}

		if (Filter.Names.Num() > 0 && !AnyStringEquals(Filter.Names, Name))
		{
			return false;
		}

		if (IsNonEmpty(Filter.Label) && !(Label.Equals(Filter.Label, ESearchCase::IgnoreCase) || Name.Equals(Filter.Label, ESearchCase::IgnoreCase)))
		{
			return false;
		}

		if (IsNonEmpty(Filter.LabelPrefix) && !Label.StartsWith(Filter.LabelPrefix, ESearchCase::IgnoreCase))
		{
			return false;
		}

		if (IsNonEmpty(Filter.LabelSuffix) && !Label.EndsWith(Filter.LabelSuffix, ESearchCase::IgnoreCase))
		{
			return false;
		}

		if (IsNonEmpty(Filter.LabelContains) && !Label.Contains(Filter.LabelContains, ESearchCase::IgnoreCase))
		{
			return false;
		}

		if (IsNonEmpty(Filter.LabelNotContains) && Label.Contains(Filter.LabelNotContains, ESearchCase::IgnoreCase))
		{
			return false;
		}

		if (IsNonEmpty(Filter.Name) && !Name.Equals(Filter.Name, ESearchCase::IgnoreCase))
		{
			return false;
		}

		if (Filter.ClassNames.Num() > 0)
		{
			bool bAnyClassMatch = false;
			for (const FString& ClassQuery : Filter.ClassNames)
			{
				if (ClassMatches(Actor, ClassQuery))
				{
					bAnyClassMatch = true;
					break;
				}
			}

			if (!bAnyClassMatch)
			{
				return false;
			}
		}

		if (IsNonEmpty(Filter.ClassName) && !ClassMatches(Actor, Filter.ClassName))
		{
			return false;
		}

		if (IsNonEmpty(Filter.ClassContains) && !ClassContains(Actor, Filter.ClassContains))
		{
			return false;
		}

		if (Filter.Tags.Num() > 0 && !ActorHasAnyTag(Actor, Filter.Tags))
		{
			return false;
		}

		if (IsNonEmpty(Filter.FolderPath) && !FolderPath.Equals(Filter.FolderPath, ESearchCase::IgnoreCase))
		{
			return false;
		}

		if (IsNonEmpty(Filter.FolderPrefix) && !FolderPath.StartsWith(Filter.FolderPrefix, ESearchCase::IgnoreCase))
		{
			return false;
		}

		if (IsNonEmpty(Filter.FolderContains) && !FolderPath.Contains(Filter.FolderContains, ESearchCase::IgnoreCase))
		{
			return false;
		}

		return true;
	}

	static TSharedRef<FJsonObject> MakeVectorObject(const FVector& Value)
	{
		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("x"), Value.X);
		Obj->SetNumberField(TEXT("y"), Value.Y);
		Obj->SetNumberField(TEXT("z"), Value.Z);
		return Obj;
	}

	static TSharedRef<FJsonObject> MakeActorSummary(AActor* Actor)
	{
		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		if (!Actor)
		{
			return Obj;
		}

		const UClass* Class = Actor->GetClass();
		Obj->SetStringField(TEXT("name"), Actor->GetName());
		Obj->SetStringField(TEXT("label"), GetActorLabelSafe(Actor));
		Obj->SetStringField(TEXT("path"), Actor->GetPathName());
		Obj->SetStringField(TEXT("className"), Class ? Class->GetName() : FString());
		Obj->SetStringField(TEXT("classPath"), Class ? Class->GetPathName() : FString());
		Obj->SetStringField(TEXT("classScriptPath"), Class ? Class->GetClassPathName().ToString() : FString());
		Obj->SetStringField(TEXT("folderPath"), GetActorFolderPathSafe(Actor));
		Obj->SetObjectField(TEXT("location"), MakeVectorObject(Actor->GetActorLocation()));
		Obj->SetObjectField(TEXT("rotation"), MakeVectorObject(FVector(Actor->GetActorRotation().Pitch, Actor->GetActorRotation().Yaw, Actor->GetActorRotation().Roll)));
		Obj->SetObjectField(TEXT("scale"), MakeVectorObject(Actor->GetActorScale3D()));
		Obj->SetBoolField(TEXT("transient"), Actor->HasAnyFlags(RF_Transient));
#if WITH_EDITOR
		Obj->SetBoolField(TEXT("hidden"), Actor->IsHiddenEd());
		Obj->SetBoolField(TEXT("selected"), Actor->IsSelected());
#else
		Obj->SetBoolField(TEXT("hidden"), false);
		Obj->SetBoolField(TEXT("selected"), false);
#endif

		TArray<TSharedPtr<FJsonValue>> TagValues;
		for (const FName& Tag : Actor->Tags)
		{
			TagValues.Add(MakeShared<FJsonValueString>(Tag.ToString()));
		}
		Obj->SetArrayField(TEXT("tags"), TagValues);

		return Obj;
	}

	static void SetWorldSummary(TSharedRef<FJsonObject> Data, UWorld* World)
	{
		Data->SetStringField(TEXT("worldObject"), World ? World->GetName() : FString());
		Data->SetStringField(TEXT("mapPackage"), (World && World->GetOutermost()) ? World->GetOutermost()->GetName() : FString());
	}

	static TSharedRef<FJsonObject> MakeFilterDetails(const FBATLevelActorFilter& Filter)
	{
		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("label"), Filter.Label);
		Obj->SetStringField(TEXT("labelPrefix"), Filter.LabelPrefix);
		Obj->SetStringField(TEXT("labelSuffix"), Filter.LabelSuffix);
		Obj->SetStringField(TEXT("labelContains"), Filter.LabelContains);
		Obj->SetStringField(TEXT("labelNotContains"), Filter.LabelNotContains);
		Obj->SetStringField(TEXT("name"), Filter.Name);
		Obj->SetStringField(TEXT("className"), Filter.ClassName);
		Obj->SetStringField(TEXT("classContains"), Filter.ClassContains);
		Obj->SetStringField(TEXT("folderPath"), Filter.FolderPath);
		Obj->SetStringField(TEXT("folderPrefix"), Filter.FolderPrefix);
		Obj->SetStringField(TEXT("folderContains"), Filter.FolderContains);
		Obj->SetBoolField(TEXT("includeHidden"), Filter.bIncludeHidden);
		Obj->SetBoolField(TEXT("includeTransient"), Filter.bIncludeTransient);
		Obj->SetBoolField(TEXT("hasAnyFilter"), Filter.bHasAnyFilter);
		Obj->SetNumberField(TEXT("limit"), Filter.Limit);
		return Obj;
	}

	static UWorld* GetEditorWorld()
	{
		return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	}

	static FString ResolveMapRequest(const TSharedPtr<FJsonObject>& Body)
	{
		FString Map;
		if (!TryGetTrimmedString(Body, TEXT("map"), Map)
			&& !TryGetTrimmedString(Body, TEXT("mapPackage"), Map)
			&& !TryGetTrimmedString(Body, TEXT("package"), Map)
			&& !TryGetTrimmedString(Body, TEXT("path"), Map)
			&& !TryGetTrimmedString(Body, TEXT("filename"), Map))
		{
			return FString();
		}

		FString PackagePart;
		FString ObjectPart;
		if (Map.StartsWith(TEXT("/")) && Map.Split(TEXT("."), &PackagePart, &ObjectPart))
		{
			Map = PackagePart;
		}

		return Map;
	}

	static bool ResolveMapFilename(const FString& MapRequest, FString& OutPackageName, FString& OutFilename)
	{
		OutPackageName.Reset();
		OutFilename.Reset();

		if (MapRequest.IsEmpty())
		{
			return false;
		}

		if (MapRequest.StartsWith(TEXT("/")))
		{
			OutPackageName = MapRequest;
			if (!FPackageName::DoesPackageExist(OutPackageName, &OutFilename))
			{
				if (!FPackageName::IsValidLongPackageName(OutPackageName, false))
				{
					return false;
				}

				OutFilename = FPackageName::LongPackageNameToFilename(OutPackageName, FPackageName::GetMapPackageExtension());
			}
			return true;
		}

		OutFilename = MapRequest;
		return FPaths::FileExists(OutFilename);
	}

	static bool ResolveCreateMapPackage(const TSharedPtr<FJsonObject>& Body, FString& OutPackageName)
	{
		OutPackageName = ResolveMapRequest(Body);
		if (OutPackageName.IsEmpty())
		{
			return false;
		}

		if (OutPackageName.Contains(TEXT(".")))
		{
			FString PackagePart;
			FString ObjectPart;
			if (OutPackageName.Split(TEXT("."), &PackagePart, &ObjectPart))
			{
				OutPackageName = PackagePart;
			}
		}

		return FPackageName::IsValidLongPackageName(OutPackageName, false);
	}
}

FAutomationResult FLevelAuditCommand::Execute(FAutomationContext& Context)
{
	if (!Context.Module || !Context.Body.IsValid())
	{
		return BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("bad_args"), TEXT("Missing request context."), 400);
	}

	TOptional<FAutomationResult> Result;
	const bool bCompleted = BAT::EditorExecution::RunOnGameThreadAndWaitVoid([&Context, &Result]()
	{
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Result = BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("world_not_found"), TEXT("Editor world is not available."), 404);
			return;
		}

		const FBATLevelActorFilter Filter = BuildLevelActorFilter(Context.Body);
		TArray<TSharedPtr<FJsonValue>> Actors;
		TMap<FString, int32> CountsByClass;
		int32 MatchedCount = 0;

		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!ActorMatchesFilter(Actor, Filter))
			{
				continue;
			}

			++MatchedCount;
			const UClass* Class = Actor->GetClass();
			const FString ClassName = Class ? Class->GetName() : TEXT("<unknown>");
			CountsByClass.FindOrAdd(ClassName)++;

			if (Actors.Num() < Filter.Limit)
			{
				Actors.Add(MakeShared<FJsonValueObject>(MakeActorSummary(Actor)));
			}
		}

		TSharedRef<FJsonObject> CountsObj = MakeShared<FJsonObject>();
		for (const TPair<FString, int32>& Pair : CountsByClass)
		{
			CountsObj->SetNumberField(Pair.Key, Pair.Value);
		}

		TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
		SetWorldSummary(Data, World);
		Data->SetObjectField(TEXT("filters"), MakeFilterDetails(Filter));
		Data->SetNumberField(TEXT("matchedCount"), MatchedCount);
		Data->SetNumberField(TEXT("returnedCount"), Actors.Num());
		Data->SetBoolField(TEXT("truncated"), MatchedCount > Actors.Num());
		Data->SetObjectField(TEXT("countsByClass"), CountsObj);
		Data->SetArrayField(TEXT("actors"), Actors);
		Result = BAT::Reflection::MakeStructuredSuccess(Context.RequestId, Data);
	}, 10.0f);

	return (bCompleted && Result.IsSet()) ? Result.GetValue() : BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("game_thread_timeout"), TEXT("Timed out waiting for game thread execution."), 504);
}

FAutomationResult FLevelDestroyActorsCommand::Execute(FAutomationContext& Context)
{
	if (!Context.Module || !Context.Body.IsValid())
	{
		return BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("bad_args"), TEXT("Missing request context."), 400);
	}

	TOptional<FAutomationResult> Result;
	const bool bCompleted = BAT::EditorExecution::RunOnGameThreadAndWaitVoid([&Context, &Result]()
	{
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Result = BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("world_not_found"), TEXT("Editor world is not available."), 404);
			return;
		}

		const FBATLevelActorFilter Filter = BuildLevelActorFilter(Context.Body);
		bool bAllowAll = false;
		Context.Body->TryGetBoolField(TEXT("allowAll"), bAllowAll);
		if (!Filter.bHasAnyFilter && !bAllowAll)
		{
			Result = BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("missing_filter"), TEXT("Refusing to destroy actors without at least one filter. Pass allowAll=true only for intentional broad cleanup."), 400);
			return;
		}

		bool bDryRun = false;
		Context.Body->TryGetBoolField(TEXT("dryRun"), bDryRun);

		bool bAllowPartial = false;
		Context.Body->TryGetBoolField(TEXT("allowPartial"), bAllowPartial);

		double MaxDeleteRaw = 100.0;
		Context.Body->TryGetNumberField(TEXT("maxDelete"), MaxDeleteRaw);
		const int32 MaxDelete = FMath::Clamp(static_cast<int32>(MaxDeleteRaw), 1, 5000);

		TArray<TWeakObjectPtr<AActor>> MatchedActors;
		TArray<TSharedPtr<FJsonValue>> ActorSummaries;
		int32 MatchedCount = 0;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!ActorMatchesFilter(Actor, Filter))
			{
				continue;
			}

			++MatchedCount;
			if (MatchedActors.Num() < MaxDelete)
			{
				MatchedActors.Add(Actor);
				ActorSummaries.Add(MakeShared<FJsonValueObject>(MakeActorSummary(Actor)));
			}
		}

		if (!bDryRun && MatchedCount > MaxDelete && !bAllowPartial)
		{
			TSharedRef<FJsonObject> Details = MakeShared<FJsonObject>();
			Details->SetNumberField(TEXT("matchedCount"), MatchedCount);
			Details->SetNumberField(TEXT("maxDelete"), MaxDelete);
			Details->SetArrayField(TEXT("sampleActors"), ActorSummaries);
			Result = BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("max_delete_exceeded"), TEXT("Matched actor count exceeds maxDelete. Increase maxDelete, narrow the filter, or pass allowPartial=true."), 400, Details);
			return;
		}

		int32 DestroyedCount = 0;
		int32 FailedCount = 0;
		if (!bDryRun)
		{
			const FScopedTransaction Transaction(NSLOCTEXT("BlueprintAutomationToolkit", "DestroyLevelActors", "BAT Destroy Level Actors"));
			for (const TWeakObjectPtr<AActor>& ActorPtr : MatchedActors)
			{
				AActor* Actor = ActorPtr.Get();
				if (!Actor || !IsValid(Actor))
				{
					++FailedCount;
					continue;
				}

				Actor->Modify();
				if (World->EditorDestroyActor(Actor, true))
				{
					++DestroyedCount;
				}
				else
				{
					++FailedCount;
				}
			}

			if (DestroyedCount > 0)
			{
				World->MarkPackageDirty();
				if (GEditor)
				{
					GEditor->RedrawLevelEditingViewports(true);
				}
			}
		}

		TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
		SetWorldSummary(Data, World);
		Data->SetObjectField(TEXT("filters"), MakeFilterDetails(Filter));
		Data->SetBoolField(TEXT("dryRun"), bDryRun);
		Data->SetNumberField(TEXT("matchedCount"), MatchedCount);
		Data->SetNumberField(TEXT("returnedCount"), ActorSummaries.Num());
		Data->SetNumberField(TEXT("maxDelete"), MaxDelete);
		Data->SetBoolField(TEXT("truncated"), MatchedCount > ActorSummaries.Num());
		Data->SetNumberField(TEXT("destroyedCount"), DestroyedCount);
		Data->SetNumberField(TEXT("failedCount"), FailedCount);
		Data->SetArrayField(TEXT("actors"), ActorSummaries);
		Result = BAT::Reflection::MakeStructuredSuccess(Context.RequestId, Data);
	}, 10.0f);

	return (bCompleted && Result.IsSet()) ? Result.GetValue() : BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("game_thread_timeout"), TEXT("Timed out waiting for game thread execution."), 504);
}

FAutomationResult FLevelSaveCommand::Execute(FAutomationContext& Context)
{
	if (!Context.Module || !Context.Body.IsValid())
	{
		return BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("bad_args"), TEXT("Missing request context."), 400);
	}

	TOptional<FAutomationResult> Result;
	const bool bCompleted = BAT::EditorExecution::RunOnGameThreadAndWaitVoid([&Context, &Result]()
	{
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Result = BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("world_not_found"), TEXT("Editor world is not available."), 404);
			return;
		}

		bool bSaveContentPackages = true;
		Context.Body->TryGetBoolField(TEXT("saveContentPackages"), bSaveContentPackages);

		World->MarkPackageDirty();
		const bool bSaved = FEditorFileUtils::SaveDirtyPackages(
			/*bPromptUserToSave*/ false,
			/*bSaveMapPackages*/ true,
			/*bSaveContentPackages*/ bSaveContentPackages,
			/*bFastSave*/ false,
			/*bNotifyNoPackagesSaved*/ false,
			/*bCanBeDeclined*/ false);

		TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
		SetWorldSummary(Data, World);
		Data->SetBoolField(TEXT("saved"), bSaved);
		Data->SetBoolField(TEXT("saveContentPackages"), bSaveContentPackages);
		Result = bSaved
			? BAT::Reflection::MakeStructuredSuccess(Context.RequestId, Data)
			: BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("save_failed"), TEXT("Unreal Editor failed to save dirty map/content packages."), 500, Data);
	}, 60.0f);

	return (bCompleted && Result.IsSet()) ? Result.GetValue() : BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("game_thread_timeout"), TEXT("Timed out waiting for game thread execution."), 504);
}

FAutomationResult FLevelCreateCommand::Execute(FAutomationContext& Context)
{
	if (!Context.Module || !Context.Body.IsValid())
	{
		return BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("bad_args"), TEXT("Missing request context."), 400);
	}

	TOptional<FAutomationResult> Result;
	const bool bCompleted = BAT::EditorExecution::RunOnGameThreadAndWaitVoid([&Context, &Result]()
	{
		FString PackageName;
		if (!ResolveCreateMapPackage(Context.Body, PackageName))
		{
			Result = BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("bad_args"), TEXT("Body must include a valid long package path in 'map', 'mapPackage', 'package', or 'path'."), 400);
			return;
		}

		bool bOverwrite = false;
		Context.Body->TryGetBoolField(TEXT("overwrite"), bOverwrite);

		FString ExistingFilename;
		const bool bAlreadyExists = FPackageName::DoesPackageExist(PackageName, &ExistingFilename);
		if (bAlreadyExists && !bOverwrite)
		{
			TSharedRef<FJsonObject> Details = MakeShared<FJsonObject>();
			Details->SetStringField(TEXT("package"), PackageName);
			Details->SetStringField(TEXT("filename"), ExistingFilename);
			Result = BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("map_already_exists"), TEXT("Map package already exists. Pass overwrite=true to replace it."), 409, Details);
			return;
		}

		bool bSaveExistingMap = true;
		Context.Body->TryGetBoolField(TEXT("saveExistingMap"), bSaveExistingMap);
		Context.Body->TryGetBoolField(TEXT("saveBeforeCreate"), bSaveExistingMap);

		FString TemplateRequest;
		Context.Body->TryGetStringField(TEXT("template"), TemplateRequest);
		Context.Body->TryGetStringField(TEXT("templateMap"), TemplateRequest);

		UWorld* World = nullptr;
		FString TemplateFilename;
		FString TemplatePackage;
		if (!TemplateRequest.TrimStartAndEnd().IsEmpty())
		{
			TemplateRequest.TrimStartAndEndInline();
			if (!ResolveMapFilename(TemplateRequest, TemplatePackage, TemplateFilename) || TemplateFilename.IsEmpty())
			{
				Result = BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("template_not_found"), FString::Printf(TEXT("Could not resolve template map '%s'."), *TemplateRequest), 404);
				return;
			}
			World = UEditorLoadingAndSavingUtils::NewMapFromTemplate(TemplateFilename, bSaveExistingMap);
		}
		else
		{
			World = UEditorLoadingAndSavingUtils::NewBlankMap(bSaveExistingMap);
		}

		if (!World)
		{
			Result = BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("create_failed"), TEXT("Unreal Editor failed to create a new editor map."), 500);
			return;
		}

		IFileManager::Get().MakeDirectory(*FPaths::GetPath(FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetMapPackageExtension())), true);
		const bool bSaved = UEditorLoadingAndSavingUtils::SaveMap(World, PackageName);
		UWorld* CurrentWorld = GetEditorWorld();

		TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
		SetWorldSummary(Data, CurrentWorld);
		Data->SetStringField(TEXT("package"), PackageName);
		Data->SetStringField(TEXT("filename"), FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetMapPackageExtension()));
		Data->SetBoolField(TEXT("created"), true);
		Data->SetBoolField(TEXT("saved"), bSaved);
		Data->SetBoolField(TEXT("overwroteExisting"), bAlreadyExists && bOverwrite);
		Data->SetBoolField(TEXT("saveExistingMap"), bSaveExistingMap);
		Data->SetStringField(TEXT("template"), TemplateRequest);
		Data->SetStringField(TEXT("templateFilename"), TemplateFilename);

		Result = bSaved
			? BAT::Reflection::MakeStructuredSuccess(Context.RequestId, Data)
			: BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("save_failed"), TEXT("New map was created but could not be saved."), 500, Data);
	}, 60.0f);

	return (bCompleted && Result.IsSet()) ? Result.GetValue() : BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("game_thread_timeout"), TEXT("Timed out waiting for game thread execution."), 504);
}

FAutomationResult FLevelLoadCommand::Execute(FAutomationContext& Context)
{
	if (!Context.Module || !Context.Body.IsValid())
	{
		return BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("bad_args"), TEXT("Missing request context."), 400);
	}

	TOptional<FAutomationResult> Result;
	const bool bCompleted = BAT::EditorExecution::RunOnGameThreadAndWaitVoid([&Context, &Result]()
	{
		const FString MapRequest = ResolveMapRequest(Context.Body);
		if (MapRequest.IsEmpty())
		{
			Result = BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("bad_args"), TEXT("Body must include 'map', 'mapPackage', 'package', 'path', or 'filename'."), 400);
			return;
		}

		FString PackageName;
		FString Filename;
		if (!ResolveMapFilename(MapRequest, PackageName, Filename) || Filename.IsEmpty())
		{
			Result = BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("map_not_found"), FString::Printf(TEXT("Could not resolve map '%s' to a package or filename."), *MapRequest), 404);
			return;
		}

		bool bSaveBeforeLoad = false;
		Context.Body->TryGetBoolField(TEXT("saveBeforeLoad"), bSaveBeforeLoad);

		bool bShowProgress = false;
		Context.Body->TryGetBoolField(TEXT("showProgress"), bShowProgress);

		bool bSavedBeforeLoad = false;
		if (bSaveBeforeLoad)
		{
			bSavedBeforeLoad = FEditorFileUtils::SaveDirtyPackages(
				/*bPromptUserToSave*/ false,
				/*bSaveMapPackages*/ true,
				/*bSaveContentPackages*/ true,
				/*bFastSave*/ false,
				/*bNotifyNoPackagesSaved*/ false,
				/*bCanBeDeclined*/ false);
			if (!bSavedBeforeLoad)
			{
				Result = BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("save_before_load_failed"), TEXT("Failed to save dirty packages before loading the requested map."), 500);
				return;
			}
		}

		const bool bLoaded = FEditorFileUtils::LoadMap(Filename, /*LoadAsTemplate*/ false, bShowProgress);
		UWorld* World = GetEditorWorld();

		TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
		SetWorldSummary(Data, World);
		Data->SetStringField(TEXT("requestedMap"), MapRequest);
		Data->SetStringField(TEXT("resolvedPackage"), PackageName);
		Data->SetStringField(TEXT("filename"), Filename);
		Data->SetBoolField(TEXT("loaded"), bLoaded);
		Data->SetBoolField(TEXT("saveBeforeLoad"), bSaveBeforeLoad);
		Data->SetBoolField(TEXT("savedBeforeLoad"), bSavedBeforeLoad);
		Result = bLoaded
			? BAT::Reflection::MakeStructuredSuccess(Context.RequestId, Data)
			: BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("load_failed"), TEXT("Unreal Editor failed to load the requested map."), 500, Data);
	}, 60.0f);

	return (bCompleted && Result.IsSet()) ? Result.GetValue() : BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("game_thread_timeout"), TEXT("Timed out waiting for game thread execution."), 504);
}

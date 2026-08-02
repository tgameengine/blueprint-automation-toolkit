// Copyright 2026 AkaSoft. All Rights Reserved.

#include "Services/RuntimeAutomationService.h"

#include "BlueprintAutomationToolkitModule.h"
#include "Services/Reflection/ReflectionSerializationService.h"
#include "Services/Reflection/ReflectionTypes.h"
#include "Services/Reflection/ReflectionValidationService.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "InputCoreTypes.h"
#include "InputKeyEventArgs.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/EngineVersionComparison.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	static TSharedPtr<FJsonValue> ObjectValue(const TSharedRef<FJsonObject>& Object)
	{
		return MakeShared<FJsonValueObject>(Object);
	}

	static bool CompareJson(const TSharedPtr<FJsonValue>& Actual, const TSharedPtr<FJsonValue>& Expected, FString Operator, double Tolerance)
	{
		if (!Actual.IsValid() || !Expected.IsValid())
		{
			return false;
		}
		Operator.ToLowerInline();
		if (Operator.IsEmpty()) Operator = TEXT("eq");

		if (Actual->Type == EJson::Number && Expected->Type == EJson::Number)
		{
			const double A = Actual->AsNumber();
			const double E = Expected->AsNumber();
			if (Operator == TEXT("eq")) return FMath::Abs(A - E) <= Tolerance;
			if (Operator == TEXT("ne")) return FMath::Abs(A - E) > Tolerance;
			if (Operator == TEXT("gt")) return A > E;
			if (Operator == TEXT("gte")) return A >= E;
			if (Operator == TEXT("lt")) return A < E;
			if (Operator == TEXT("lte")) return A <= E;
			return false;
		}
		if (Actual->Type == EJson::Boolean && Expected->Type == EJson::Boolean)
		{
			const bool bEqual = Actual->AsBool() == Expected->AsBool();
			return Operator == TEXT("ne") ? !bEqual : Operator == TEXT("eq") && bEqual;
		}
		if (Actual->Type == EJson::String && Expected->Type == EJson::String)
		{
			const FString A = Actual->AsString();
			const FString E = Expected->AsString();
			if (Operator == TEXT("contains")) return A.Contains(E, ESearchCase::IgnoreCase);
			const bool bEqual = A.Equals(E, ESearchCase::CaseSensitive);
			return Operator == TEXT("ne") ? !bEqual : Operator == TEXT("eq") && bEqual;
		}
		if (Operator == TEXT("eq") || Operator == TEXT("ne"))
		{
			FString A;
			FString E;
			const bool bComparable = Actual->TryGetString(A) && Expected->TryGetString(E);
			const bool bEqual = bComparable && A == E;
			return Operator == TEXT("ne") ? !bEqual : bEqual;
		}
		return false;
	}

	static bool ActorMatches(const AActor* Actor, const TSharedPtr<FJsonObject>& Assertion)
	{
		if (!Actor || !Assertion.IsValid()) return false;
		FString ClassPath;
		if (Assertion->TryGetStringField(TEXT("class"), ClassPath) && !ClassPath.IsEmpty())
		{
			UClass* RequiredClass = ClassPath.StartsWith(TEXT("/")) ? LoadObject<UClass>(nullptr, *ClassPath) : nullptr;
			bool bMatchesClass = RequiredClass ? Actor->IsA(RequiredClass) : false;
			for (const UClass* Class = Actor->GetClass(); !bMatchesClass && Class; Class = Class->GetSuperClass())
			{
				bMatchesClass = Class->GetName().Equals(ClassPath, ESearchCase::IgnoreCase)
					|| Class->GetPathName().Equals(ClassPath, ESearchCase::IgnoreCase);
			}
			if (!bMatchesClass) return false;
		}

		FString LabelContains;
		if (Assertion->TryGetStringField(TEXT("label_contains"), LabelContains)
			&& !Actor->GetActorLabel().Contains(LabelContains, ESearchCase::IgnoreCase)) return false;
		FString NameContains;
		if (Assertion->TryGetStringField(TEXT("name_contains"), NameContains)
			&& !Actor->GetName().Contains(NameContains, ESearchCase::IgnoreCase)) return false;
		FString Tag;
		if (Assertion->TryGetStringField(TEXT("tag"), Tag) && !Tag.IsEmpty() && !Actor->ActorHasTag(FName(*Tag))) return false;
		return true;
	}

	static UObject* ResolveAssertionObject(FBlueprintAutomationToolkitModule& Module, UWorld* World, const TSharedPtr<FJsonObject>& Assertion)
	{
		FString ObjectPath;
		if (Assertion->TryGetStringField(TEXT("object_path"), ObjectPath) && !ObjectPath.IsEmpty())
		{
			if (UObject* Existing = StaticFindObject(UObject::StaticClass(), nullptr, *ObjectPath)) return Existing;
			return LoadObject<UObject>(nullptr, *ObjectPath);
		}
		FString Actor;
		FString Tag;
		Assertion->TryGetStringField(TEXT("actor"), Actor);
		Assertion->TryGetStringField(TEXT("tag"), Tag);
		if (Actor.IsEmpty() && Tag.IsEmpty()) return nullptr;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Candidate = *It;
			const bool bNameMatches = Actor.IsEmpty()
				|| Candidate->GetName().Equals(Actor, ESearchCase::IgnoreCase)
				|| Candidate->GetActorLabel().Equals(Actor, ESearchCase::IgnoreCase)
				|| Candidate->GetPathName().Equals(Actor, ESearchCase::IgnoreCase);
			const bool bTagMatches = Tag.IsEmpty() || Candidate->ActorHasTag(FName(*Tag));
			if (bNameMatches && bTagMatches) return Candidate;
		}
		return nullptr;
	}

	static TSharedRef<FJsonObject> AssertionResult(int32 Index, const FString& Type, bool bPassed)
	{
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("index"), Index);
		Result->SetStringField(TEXT("type"), Type);
		Result->SetBoolField(TEXT("passed"), bPassed);
		return Result;
	}
}

FAutomationResult FRuntimeAutomationService::ApplyPieInput(FBlueprintAutomationToolkitModule& Module, const TSharedPtr<FJsonObject>& Request) const
{
	if (!Request.IsValid()) return FAutomationResult::Error(TEXT("bad_json"), TEXT("Request body is required"), 400);
	double Number = 0.0;
	int32 PieIndex = 0;
	int32 PlayerIndex = 0;
	if (Request->TryGetNumberField(TEXT("pie_index"), Number)) PieIndex = FMath::Max(0, FMath::RoundToInt(Number));
	if (Request->TryGetNumberField(TEXT("player_index"), Number)) PlayerIndex = FMath::Max(0, FMath::RoundToInt(Number));
	UWorld* World = Module.GetPIEWorld(PieIndex);
	if (!World) return FAutomationResult::Error(TEXT("pie_world_unavailable"), TEXT("The requested PIE world is not running"), 409);
	APlayerController* Controller = UGameplayStatics::GetPlayerController(World, PlayerIndex);
	if (!Controller || !Controller->PlayerInput)
	{
		return FAutomationResult::Error(TEXT("player_input_unavailable"), TEXT("The requested PIE player has no PlayerInput"), 404);
	}

	FString KeyName;
	FString Event = TEXT("tap");
	Request->TryGetStringField(TEXT("key"), KeyName);
	Request->TryGetStringField(TEXT("event"), Event);
	KeyName.TrimStartAndEndInline();
	Event.ToLowerInline();
	const FKey Key(*KeyName);
	if (KeyName.IsEmpty() || !Key.IsValid()) return FAutomationResult::Error(TEXT("invalid_key"), TEXT("key must be a valid Unreal input key"), 400);
	if (Event != TEXT("press") && Event != TEXT("release") && Event != TEXT("tap"))
	{
		return FAutomationResult::Error(TEXT("invalid_input_event"), TEXT("event must be press, release, or tap"), 400);
	}

	auto Send = [Controller, Key](EInputEvent InputEvent)
	{
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6)
		Controller->PlayerInput->InputKey(FInputKeyEventArgs(nullptr, FInputDeviceId::CreateFromInternalId(0), Key, InputEvent, 1.0f, false, FPlatformTime::Cycles64()));
#else
		Controller->PlayerInput->InputKey(FInputKeyParams(Key, InputEvent, 1.0, false));
#endif
	};
	if (Event == TEXT("press") || Event == TEXT("tap")) Send(IE_Pressed);
	if (Event == TEXT("release") || Event == TEXT("tap")) Send(IE_Released);

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("key"), KeyName);
	Data->SetStringField(TEXT("event"), Event);
	Data->SetNumberField(TEXT("pie_index"), PieIndex);
	Data->SetNumberField(TEXT("player_index"), PlayerIndex);
	Data->SetBoolField(TEXT("python_used"), false);
	return FAutomationResult::Ok(ObjectValue(Data));
}

FAutomationResult FRuntimeAutomationService::EvaluateAssertions(FBlueprintAutomationToolkitModule& Module, const TSharedPtr<FJsonObject>& Request) const
{
	if (!Request.IsValid()) return FAutomationResult::Error(TEXT("bad_json"), TEXT("Request body is required"), 400);
	const TArray<TSharedPtr<FJsonValue>>* Assertions = nullptr;
	if (!Request->TryGetArrayField(TEXT("assertions"), Assertions) || !Assertions || Assertions->Num() == 0 || Assertions->Num() > 100)
	{
		return FAutomationResult::Error(TEXT("invalid_assertions"), TEXT("assertions must contain between 1 and 100 entries"), 400);
	}

	FString WorldMode = TEXT("pie");
	Request->TryGetStringField(TEXT("world"), WorldMode);
	double Number = 0.0;
	int32 PieIndex = 0;
	if (Request->TryGetNumberField(TEXT("pie_index"), Number)) PieIndex = FMath::Max(0, FMath::RoundToInt(Number));
	bool bIsPie = false;
	int32 ResolvedPieIndex = 0;
	FString WorldError;
	UWorld* World = Module.ResolveWorld(WorldMode, PieIndex, bIsPie, ResolvedPieIndex, WorldError);
	if (!World) return FAutomationResult::Error(TEXT("world_unavailable"), WorldError, 409);

	TArray<TSharedPtr<FJsonValue>> Results;
	int32 FailureCount = 0;
	for (int32 Index = 0; Index < Assertions->Num(); ++Index)
	{
		const TSharedPtr<FJsonObject> Assertion = (*Assertions)[Index].IsValid() && (*Assertions)[Index]->Type == EJson::Object
			? (*Assertions)[Index]->AsObject() : nullptr;
		FString Type;
		if (!Assertion.IsValid() || !Assertion->TryGetStringField(TEXT("type"), Type))
		{
			TSharedRef<FJsonObject> Result = AssertionResult(Index, TEXT("invalid"), false);
			Result->SetStringField(TEXT("message"), TEXT("Assertion must be an object with a type"));
			Results.Add(MakeShared<FJsonValueObject>(Result));
			++FailureCount;
			continue;
		}
		Type.ToLowerInline();
		bool bPassed = false;
		TSharedRef<FJsonObject> Result = AssertionResult(Index, Type, false);

		if (Type == TEXT("actor_count"))
		{
			int32 Count = 0;
			for (TActorIterator<AActor> It(World); It; ++It) if (ActorMatches(*It, Assertion)) ++Count;
			FString Operator = TEXT("eq");
			Assertion->TryGetStringField(TEXT("operator"), Operator);
			double Expected = 0.0;
			if (!Assertion->TryGetNumberField(TEXT("expected"), Expected))
			{
				Result->SetStringField(TEXT("message"), TEXT("actor_count requires numeric expected"));
			}
			else
			{
				bPassed = CompareJson(MakeShared<FJsonValueNumber>(Count), MakeShared<FJsonValueNumber>(Expected), Operator, 0.0);
				Result->SetNumberField(TEXT("actual"), Count);
				Result->SetNumberField(TEXT("expected"), Expected);
				Result->SetStringField(TEXT("operator"), Operator);
			}
		}
		else if (Type == TEXT("actor_exists"))
		{
			int32 Count = 0;
			for (TActorIterator<AActor> It(World); It; ++It) if (ActorMatches(*It, Assertion)) ++Count;
			bPassed = Count > 0;
			Result->SetNumberField(TEXT("actual_count"), Count);
		}
		else if (Type == TEXT("property"))
		{
			UObject* Object = ResolveAssertionObject(Module, World, Assertion);
			FString PropertyPath;
			Assertion->TryGetStringField(TEXT("property"), PropertyPath);
			const TSharedPtr<FJsonValue> Expected = Assertion->TryGetField(TEXT("expected"));
			if (!Object || PropertyPath.IsEmpty() || !Expected.IsValid())
			{
				Result->SetStringField(TEXT("message"), TEXT("property requires actor/tag or object_path, property, and expected"));
			}
			else
			{
				BAT::Reflection::FResolvedProperty Resolved;
				FAutomationResult Failure;
				const FReflectionValidationService Validation;
				if (!Validation.ResolveProperty(Object, PropertyPath, TEXT("runtime-assert"), Resolved, Failure)
					|| !Validation.ValidatePropertyRead(Module, Object, Resolved, TEXT("runtime-assert"), Failure))
				{
					Result->SetStringField(TEXT("message"), TEXT("Property is missing or not safe to read"));
				}
				else
				{
					const TSharedPtr<FJsonValue> Actual = FReflectionSerializationService().SerializeValue(Resolved.Property, Resolved.ValuePtr);
					FString Operator = TEXT("eq");
					Assertion->TryGetStringField(TEXT("operator"), Operator);
					double Tolerance = 0.0001;
					Assertion->TryGetNumberField(TEXT("tolerance"), Tolerance);
					bPassed = CompareJson(Actual, Expected, Operator, FMath::Max(0.0, Tolerance));
					Result->SetField(TEXT("actual"), Actual);
					Result->SetField(TEXT("expected"), Expected);
					Result->SetStringField(TEXT("operator"), Operator);
					Result->SetStringField(TEXT("object_path"), Object->GetPathName());
					Result->SetStringField(TEXT("property"), PropertyPath);
				}
			}
		}
		else
		{
			Result->SetStringField(TEXT("message"), TEXT("Supported types: actor_count, actor_exists, property"));
		}

		Result->SetBoolField(TEXT("passed"), bPassed);
		if (!bPassed) ++FailureCount;
		Results.Add(MakeShared<FJsonValueObject>(Result));
	}

	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("all_passed"), FailureCount == 0);
	Data->SetNumberField(TEXT("failure_count"), FailureCount);
	Data->SetStringField(TEXT("world"), bIsPie ? TEXT("pie") : TEXT("editor"));
	Data->SetNumberField(TEXT("pie_index"), ResolvedPieIndex);
	Data->SetArrayField(TEXT("assertions"), Results);
	Data->SetBoolField(TEXT("python_used"), false);
	if (FailureCount > 0)
	{
		return FAutomationResult::ErrorWithData(TEXT("runtime_assertion_failed"), TEXT("One or more runtime assertions failed"), 422, ObjectValue(Data));
	}
	return FAutomationResult::Ok(ObjectValue(Data));
}

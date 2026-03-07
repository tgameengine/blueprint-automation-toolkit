#include "Commands/Reflection/DescribeObjectCommand.h"

#include "BlueprintAutomationToolkitModule.h"
#include "Core/EditorExecution.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "GameFramework/Actor.h"
#include "Services/Reflection/ReflectionFunctionService.h"
#include "Services/Reflection/ReflectionObjectResolver.h"
#include "Services/Reflection/ReflectionSerializationService.h"
#include "Services/Reflection/ReflectionTypes.h"
#include "Services/Reflection/ReflectionValidationService.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace
{
	static bool IsSimpleReadableProperty(const FProperty* Property)
	{
		return Property
			&& !CastField<FArrayProperty>(Property)
			&& !CastField<FMapProperty>(Property)
			&& !CastField<FSetProperty>(Property);
	}

	static int32 ReadPositiveLimit(const TSharedPtr<FJsonObject>& BodyObj, const TCHAR* FieldName, int32 DefaultValue)
	{
		double RawValue = static_cast<double>(DefaultValue);
		if (BodyObj.IsValid() && BodyObj->TryGetNumberField(FieldName, RawValue))
		{
			return FMath::Clamp(static_cast<int32>(RawValue), 1, 250);
		}

		return DefaultValue;
	}
}

FAutomationResult FDescribeObjectCommand::Execute(FAutomationContext& Context)
{
	if (!Context.Module || !Context.Body.IsValid())
	{
		return BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("bad_args"), TEXT("Missing request context."), 400);
	}

	TOptional<FAutomationResult> Result;
	const bool bCompleted = BAT::EditorExecution::RunOnGameThreadAndWaitVoid([&Context, &Result]()
	{
		FReflectionObjectResolver Resolver;
		FReflectionValidationService Validation;
		FReflectionSerializationService Serialization;

		BAT::Reflection::FResolvedObject ResolvedObject;
		FAutomationResult Failure;
		if (!Resolver.Resolve(*Context.Module, Context.Body, Context.RequestId, ResolvedObject, Failure))
		{
			Result = Failure;
			return;
		}

		bool bVerbose = false;
		Context.Body->TryGetBoolField(TEXT("verbose"), bVerbose);

		const int32 PropertyLimit = ReadPositiveLimit(Context.Body, TEXT("propertyLimit"), bVerbose ? 100 : 25);
		const int32 FunctionLimit = ReadPositiveLimit(Context.Body, TEXT("functionLimit"), bVerbose ? 100 : 25);
		const int32 ValueLimit = ReadPositiveLimit(Context.Body, TEXT("valueLimit"), bVerbose ? 32 : 12);

		UObject* Object = ResolvedObject.Object;
		TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
		TSharedRef<FJsonObject> Target = Serialization.SerializeObjectReference(Object);
		Target->SetStringField(TEXT("resolutionSource"), ResolvedObject.ResolutionSource);

		Data->SetObjectField(TEXT("target"), Target);
		Data->SetStringField(TEXT("objectPath"), Object->GetPathName());
		Data->SetStringField(TEXT("objectName"), Object->GetName());
		Data->SetStringField(TEXT("classPath"), Object->GetClass()->GetPathName());
		Data->SetStringField(TEXT("className"), Object->GetClass()->GetName());

		if (UObject* Outer = Object->GetOuter())
		{
			Data->SetStringField(TEXT("outerPath"), Outer->GetPathName());
			Data->SetStringField(TEXT("outerName"), Outer->GetName());
		}

		if (UPackage* Package = Object->GetOutermost())
		{
			Data->SetStringField(TEXT("packagePath"), Package->GetName());
		}

		if (const AActor* Actor = Cast<AActor>(Object))
		{
			if (UWorld* World = Actor->GetWorld())
			{
				TSharedRef<FJsonObject> WorldObj = MakeShared<FJsonObject>();
				WorldObj->SetStringField(TEXT("path"), World->GetPathName());
				WorldObj->SetStringField(TEXT("name"), World->GetName());
				WorldObj->SetBoolField(TEXT("pie"), World->WorldType == EWorldType::PIE);
				Data->SetObjectField(TEXT("worldContext"), WorldObj);
			}
		}

		TSharedRef<FJsonObject> Flags = MakeShared<FJsonObject>();
		Flags->SetBoolField(TEXT("transient"), Object->HasAnyFlags(RF_Transient));
		Flags->SetBoolField(TEXT("pendingKill"), Object->IsUnreachable() || Object->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed));
		Flags->SetBoolField(TEXT("blueprintGeneratedClass"), Object->GetClass()->ClassGeneratedBy != nullptr);
		Flags->SetBoolField(TEXT("isActor"), Object->IsA(AActor::StaticClass()));
		Flags->SetBoolField(TEXT("isAsset"), Object->GetOutermost() != nullptr && Object->GetOutermost() != GetTransientPackage());
		Data->SetObjectField(TEXT("flags"), Flags);

		TArray<TSharedPtr<FJsonValue>> PropertySummaries;
		TSharedRef<FJsonObject> CurrentValues = MakeShared<FJsonObject>();
		int32 TotalProperties = 0;
		int32 EmittedValues = 0;
		for (TFieldIterator<FProperty> It(Object->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			FProperty* Property = *It;
			if (!Validation.ShouldListProperty(Property))
			{
				continue;
			}

			++TotalProperties;
			if (PropertySummaries.Num() < PropertyLimit)
			{
				TSharedRef<FJsonObject> PropertyInfo = Serialization.DescribeProperty(Property);
				BAT::Reflection::FResolvedProperty ResolvedProperty;
				if (Validation.ResolveProperty(Object, Property->GetName(), Context.RequestId, ResolvedProperty, Failure))
				{
					PropertyInfo->SetBoolField(TEXT("safeWritable"), Validation.ValidatePropertyWrite(*Context.Module, ResolvedObject, ResolvedProperty, Context.RequestId, Failure));
				}
				else
				{
					PropertyInfo->SetBoolField(TEXT("safeWritable"), false);
				}

				PropertySummaries.Add(MakeShared<FJsonValueObject>(PropertyInfo));
			}

			if (EmittedValues < ValueLimit && Validation.ShouldReadProperty(Property) && Serialization.IsSupportedPropertyType(Property, false) && IsSimpleReadableProperty(Property))
			{
				const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Object);
				CurrentValues->SetField(Property->GetName(), Serialization.SerializeValue(Property, ValuePtr));
				++EmittedValues;
			}
		}

		Data->SetArrayField(TEXT("editablePropertiesSummary"), PropertySummaries);
		Data->SetObjectField(TEXT("currentValues"), CurrentValues);
		Data->SetNumberField(TEXT("totalProperties"), TotalProperties);
		Data->SetBoolField(TEXT("propertiesTruncated"), TotalProperties > PropertyLimit);

		TArray<TSharedPtr<FJsonValue>> FunctionSummaries;
		int32 TotalFunctions = 0;
		for (TFieldIterator<UFunction> It(Object->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			UFunction* Function = *It;
			if (!Validation.ShouldListFunction(Function))
			{
				continue;
			}

			++TotalFunctions;
			if (FunctionSummaries.Num() >= FunctionLimit)
			{
				continue;
			}

			FunctionSummaries.Add(MakeShared<FJsonValueObject>(Serialization.DescribeFunction(Function, Validation.IsFunctionCallableInSafeMode(*Context.Module, Object->GetClass(), Function))));
		}

		Data->SetArrayField(TEXT("callableFunctionsSummary"), FunctionSummaries);
		Data->SetNumberField(TEXT("totalFunctions"), TotalFunctions);
		Data->SetBoolField(TEXT("functionsTruncated"), TotalFunctions > FunctionLimit);

		Result = BAT::Reflection::MakeStructuredSuccess(Context.RequestId, Data);
	}, 10.0f);

	return (bCompleted && Result.IsSet()) ? Result.GetValue() : BAT::Reflection::MakeStructuredError(Context.RequestId, TEXT("game_thread_timeout"), TEXT("Timed out waiting for game thread execution."), 504);
}
#include "Services/Reflection/ReflectionPropertyService.h"

#include "BlueprintAutomationToolkitModule.h"
#include "Core/EditorExecution.h"
#include "ScopedTransaction.h"
#include "Services/Reflection/ReflectionObjectResolver.h"
#include "Services/Reflection/ReflectionSerializationService.h"
#include "Services/Reflection/ReflectionTypes.h"
#include "Services/Reflection/ReflectionValidationService.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Runtime/Launch/Resources/Version.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace
{
	static void MarkDirtyIfPersistent(UObject* Object)
	{
		if (!Object)
		{
			return;
		}

		if (UPackage* Package = Object->GetOutermost())
		{
			if (Package != GetTransientPackage())
			{
				Object->MarkPackageDirty();
			}
		}
	}

	static TSharedRef<FJsonObject> BuildObjectSummary(const FReflectionSerializationService& Serialization, const BAT::Reflection::FResolvedObject& ResolvedObject)
	{
		TSharedRef<FJsonObject> ObjectInfo = Serialization.SerializeObjectReference(ResolvedObject.Object);
		ObjectInfo->SetStringField(TEXT("resolutionSource"), ResolvedObject.ResolutionSource);
		return ObjectInfo;
	}
}

TSharedPtr<FJsonObject> FReflectionPropertyService::ListProperties(UObject* Object, FString& OutError) const
{
	OutError.Reset();
	if (!Object)
	{
		OutError = TEXT("Object must be non-null.");
		return nullptr;
	}

	FReflectionValidationService Validation;
	FReflectionSerializationService Serialization;

	TArray<TSharedPtr<FJsonValue>> Properties;
	for (TFieldIterator<FProperty> It(Object->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
	{
		FProperty* Property = *It;
		if (!Validation.ShouldListProperty(Property))
		{
			continue;
		}

		Properties.Add(MakeShared<FJsonValueObject>(Serialization.DescribeProperty(Property)));
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetObjectField(TEXT("object"), Serialization.SerializeObjectReference(Object));
	Root->SetArrayField(TEXT("properties"), Properties);
	return Root;
}

FAutomationResult FReflectionPropertyService::GetObject(FBlueprintAutomationToolkitModule& Module, const FString& RequestId, const TSharedPtr<FJsonObject>& BodyObj) const
{
	TOptional<FAutomationResult> Result;
	const bool bCompleted = BAT::EditorExecution::RunOnGameThreadAndWaitVoid([&Module, RequestId, BodyObj, &Result]()
	{
		FReflectionObjectResolver Resolver;
		FReflectionValidationService Validation;
		FReflectionSerializationService Serialization;

		BAT::Reflection::FResolvedObject ResolvedObject;
		FAutomationResult Failure;
		if (!Resolver.Resolve(Module, BodyObj, RequestId, ResolvedObject, Failure))
		{
			if (Failure.ErrorCode == TEXT("not_found"))
			{
				Result = BAT::Reflection::MakeStructuredError(RequestId, TEXT("ObjectNotFound"), Failure.ErrorMessage, Failure.StatusCode);
			}
			else if (Failure.ErrorCode == TEXT("bad_args"))
			{
				Result = BAT::Reflection::MakeStructuredError(RequestId, TEXT("InvalidArguments"), Failure.ErrorMessage, Failure.StatusCode);
			}
			else
			{
				Result = Failure;
			}
			return;
		}

		TArray<FString> RequestedProperties;
		const TArray<TSharedPtr<FJsonValue>>* PropertiesField = nullptr;
		if (BodyObj.IsValid() && BodyObj->TryGetArrayField(TEXT("properties"), PropertiesField) && PropertiesField)
		{
			for (const TSharedPtr<FJsonValue>& PropertyValue : *PropertiesField)
			{
				if (!PropertyValue.IsValid() || PropertyValue->Type != EJson::String)
				{
					Result = BAT::Reflection::MakeStructuredError(RequestId, TEXT("bad_args"), TEXT("'properties' must contain only strings."), 400);
					return;
				}
				RequestedProperties.Add(PropertyValue->AsString().TrimStartAndEnd());
			}
		}

		TSharedRef<FJsonObject> PropertyValues = MakeShared<FJsonObject>();
		if (RequestedProperties.Num() > 0)
		{
			for (const FString& PropertyPath : RequestedProperties)
			{
				BAT::Reflection::FResolvedProperty ResolvedProperty;
				if (!Validation.ResolveProperty(ResolvedObject.Object, PropertyPath, RequestId, ResolvedProperty, Failure))
				{
					Result = Failure;
					return;
				}

				if (!Serialization.IsSupportedPropertyType(ResolvedProperty.Property, false))
				{
					Result = BAT::Reflection::MakeStructuredError(RequestId, TEXT("unsupported_type"), FString::Printf(TEXT("Property '%s' uses an unsupported reflected type."), *PropertyPath), 400);
					return;
				}

				PropertyValues->SetField(PropertyPath, Serialization.SerializeValue(ResolvedProperty.Property, ResolvedProperty.ValuePtr));
			}
		}
		else
		{
			for (TFieldIterator<FProperty> It(ResolvedObject.Object->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
			{
				FProperty* Property = *It;
				if (!Validation.ShouldReadProperty(Property) || !Serialization.IsSupportedPropertyType(Property, false))
				{
					continue;
				}

				const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(ResolvedObject.Object);
				PropertyValues->SetField(Property->GetName(), Serialization.SerializeValue(Property, ValuePtr));
			}
		}

		TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetObjectField(TEXT("object"), BuildObjectSummary(Serialization, ResolvedObject));
		Data->SetObjectField(TEXT("properties"), PropertyValues);
		Data->SetNumberField(TEXT("propertyCount"), static_cast<double>(PropertyValues->Values.Num()));
		Result = BAT::Reflection::MakeStructuredSuccess(RequestId, Data);
	}, 10.0f);

	return (bCompleted && Result.IsSet()) ? Result.GetValue() : BAT::Reflection::MakeStructuredError(RequestId, TEXT("game_thread_timeout"), TEXT("Timed out waiting for game thread execution."), 504);
}

FAutomationResult FReflectionPropertyService::ListProperties(FBlueprintAutomationToolkitModule& Module, const FString& RequestId, const TSharedPtr<FJsonObject>& BodyObj) const
{
	TOptional<FAutomationResult> Result;
	const bool bCompleted = BAT::EditorExecution::RunOnGameThreadAndWaitVoid([&Module, RequestId, BodyObj, &Result]()
	{
		FReflectionObjectResolver Resolver;
		FReflectionValidationService Validation;
		FReflectionSerializationService Serialization;

		BAT::Reflection::FResolvedObject ResolvedObject;
		FAutomationResult Failure;
		if (!Resolver.Resolve(Module, BodyObj, RequestId, ResolvedObject, Failure))
		{
			Result = Failure;
			return;
		}

		TArray<TSharedPtr<FJsonValue>> Properties;
		for (TFieldIterator<FProperty> It(ResolvedObject.Object->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			FProperty* Property = *It;
			if (!Validation.ShouldListProperty(Property))
			{
				continue;
			}

			TSharedRef<FJsonObject> PropertyInfo = Serialization.DescribeProperty(Property);
			BAT::Reflection::FResolvedProperty ResolvedProperty;
			if (Validation.ResolveProperty(ResolvedObject.Object, Property->GetName(), RequestId, ResolvedProperty, Failure))
			{
				FAutomationResult WriteFailure;
				PropertyInfo->SetBoolField(TEXT("safeWritable"), Validation.ValidatePropertyWrite(Module, ResolvedObject, ResolvedProperty, RequestId, WriteFailure));
			}
			else
			{
				PropertyInfo->SetBoolField(TEXT("safeWritable"), false);
			}
			Properties.Add(MakeShared<FJsonValueObject>(PropertyInfo));
		}

		TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetObjectField(TEXT("object"), BuildObjectSummary(Serialization, ResolvedObject));
		Data->SetArrayField(TEXT("properties"), Properties);
		Result = BAT::Reflection::MakeStructuredSuccess(RequestId, Data);
	}, 10.0f);

	return (bCompleted && Result.IsSet()) ? Result.GetValue() : BAT::Reflection::MakeStructuredError(RequestId, TEXT("game_thread_timeout"), TEXT("Timed out waiting for game thread execution."), 504);
}

FAutomationResult FReflectionPropertyService::SetProperty(FBlueprintAutomationToolkitModule& Module, const FString& RequestId, const TSharedPtr<FJsonObject>& BodyObj) const
{
	TOptional<FAutomationResult> Result;
	const bool bCompleted = BAT::EditorExecution::RunOnGameThreadAndWaitVoid([&Module, RequestId, BodyObj, &Result]()
	{
		FReflectionObjectResolver Resolver;
		FReflectionValidationService Validation;
		FReflectionSerializationService Serialization;

		BAT::Reflection::FResolvedObject ResolvedObject;
		FAutomationResult Failure;
		if (!Resolver.Resolve(Module, BodyObj, RequestId, ResolvedObject, Failure))
		{
			Result = Failure;
			return;
		}

		TMap<FString, TSharedPtr<FJsonValue>> Assignments;
		const TSharedPtr<FJsonObject>* ValuesObject = nullptr;
		if (BodyObj.IsValid() && BodyObj->TryGetObjectField(TEXT("values"), ValuesObject) && ValuesObject && ValuesObject->IsValid())
		{
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8)
			for (const TPair<UE::FSharedString, TSharedPtr<FJsonValue>>& Pair : (*ValuesObject)->Values)
			{
				Assignments.Add(FString(Pair.Key), Pair.Value);
			}
#else
			Assignments = (*ValuesObject)->Values;
#endif
		}
		else
		{
			FString PropertyName;
			BodyObj->TryGetStringField(TEXT("property"), PropertyName);
			const TSharedPtr<FJsonValue>* ValueField = BodyObj->Values.Find(TEXT("value"));
			if (PropertyName.TrimStartAndEnd().IsEmpty() || !ValueField)
			{
				Result = BAT::Reflection::MakeStructuredError(RequestId, TEXT("InvalidArguments"), TEXT("Body must include either 'values' object or 'property' plus 'value'."), 400);
				return;
			}
			Assignments.Add(PropertyName.TrimStartAndEnd(), *ValueField);
		}

		if (Assignments.Num() == 0)
		{
			Result = BAT::Reflection::MakeStructuredError(RequestId, TEXT("InvalidArguments"), TEXT("No property assignments were provided."), 400);
			return;
		}

		const FScopedTransaction Transaction(NSLOCTEXT("BlueprintAutomationToolkit", "ReflectionSetProperty", "Reflection Set Property"));
		ResolvedObject.Object->Modify();

		TSharedRef<FJsonObject> UpdatedValues = MakeShared<FJsonObject>();
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Assignment : Assignments)
		{
			BAT::Reflection::FResolvedProperty ResolvedProperty;
			if (!Validation.ResolveProperty(ResolvedObject.Object, Assignment.Key, RequestId, ResolvedProperty, Failure))
			{
				Result = Failure;
				return;
			}

			if (!Serialization.IsSupportedPropertyType(ResolvedProperty.Property, true))
			{
				Result = BAT::Reflection::MakeStructuredError(RequestId, TEXT("InvalidType"), FString::Printf(TEXT("Property '%s' uses an unsupported reflected type."), *Assignment.Key), 400);
				return;
			}

			if (!Validation.ValidatePropertyWrite(Module, ResolvedObject, ResolvedProperty, RequestId, Failure))
			{
				Result = Failure;
				return;
			}

			FString CompatibilityCode;
			FString CompatibilityMessage;
			if (!Validation.ValidateValueTypeCompatibility(ResolvedProperty.Property, Assignment.Value, Resolver, CompatibilityCode, CompatibilityMessage))
			{
				Result = BAT::Reflection::MakeStructuredError(RequestId, CompatibilityCode, FString::Printf(TEXT("Property '%s': %s"), *Assignment.Key, *CompatibilityMessage), 400);
				return;
			}

			if (ResolvedProperty.OwnerObject && ResolvedProperty.OwnerObject != ResolvedObject.Object)
			{
				ResolvedProperty.OwnerObject->Modify();
			}

			FString ErrorCode;
			FString ErrorMessage;
			if (!Serialization.DeserializeValue(ResolvedProperty.Property, ResolvedProperty.ValuePtr, Assignment.Value, Resolver, ErrorCode, ErrorMessage))
			{
				Result = BAT::Reflection::MakeStructuredError(RequestId, ErrorCode, FString::Printf(TEXT("Property '%s': %s"), *Assignment.Key, *ErrorMessage), 400);
				return;
			}

			UpdatedValues->SetField(Assignment.Key, Serialization.SerializeValue(ResolvedProperty.Property, ResolvedProperty.ValuePtr));
			MarkDirtyIfPersistent(ResolvedProperty.OwnerObject ? ResolvedProperty.OwnerObject : ResolvedObject.Object);
		}

#if WITH_EDITOR
		ResolvedObject.Object->PostEditChange();
#endif

		TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetObjectField(TEXT("object"), BuildObjectSummary(Serialization, ResolvedObject));
		Data->SetObjectField(TEXT("updatedProperties"), UpdatedValues);
		Result = BAT::Reflection::MakeStructuredSuccess(RequestId, Data);
	}, 10.0f);

	return (bCompleted && Result.IsSet()) ? Result.GetValue() : BAT::Reflection::MakeStructuredError(RequestId, TEXT("game_thread_timeout"), TEXT("Timed out waiting for game thread execution."), 504);
}

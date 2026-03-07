#include "Services/Reflection/ReflectionFunctionService.h"

#include "BlueprintAutomationToolkitModule.h"
#include "Core/EditorExecution.h"
#include "ScopedTransaction.h"
#include "Services/Reflection/ReflectionObjectResolver.h"
#include "Services/Reflection/ReflectionSerializationService.h"
#include "Services/Reflection/ReflectionTypes.h"
#include "Services/Reflection/ReflectionValidationService.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/ScopeExit.h"
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
}

TSharedPtr<FJsonObject> FReflectionFunctionService::ListFunctions(UObject* Object, FString& OutError) const
{
	OutError.Reset();
	if (!Object)
	{
		OutError = TEXT("Object must be non-null.");
		return nullptr;
	}

	FReflectionValidationService Validation;
	FReflectionSerializationService Serialization;

	TArray<TSharedPtr<FJsonValue>> Functions;
	for (TFieldIterator<UFunction> It(Object->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
	{
		UFunction* Function = *It;
		if (!Validation.ShouldListFunction(Function))
		{
			continue;
		}

		Functions.Add(MakeShared<FJsonValueObject>(Serialization.DescribeFunction(Function, false)));
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetObjectField(TEXT("object"), Serialization.SerializeObjectReference(Object));
	Root->SetArrayField(TEXT("functions"), Functions);
	return Root;
}

FAutomationResult FReflectionFunctionService::ListFunctions(FBlueprintAutomationToolkitModule& Module, const FString& RequestId, const TSharedPtr<FJsonObject>& BodyObj) const
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

		TArray<TSharedPtr<FJsonValue>> Functions;
		for (TFieldIterator<UFunction> It(ResolvedObject.Object->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			UFunction* Function = *It;
			if (!Validation.ShouldListFunction(Function))
			{
				continue;
			}

			Functions.Add(MakeShared<FJsonValueObject>(Serialization.DescribeFunction(Function, Validation.IsFunctionCallableInSafeMode(Module, ResolvedObject.Object->GetClass(), Function))));
		}

		TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetObjectField(TEXT("object"), Serialization.SerializeObjectReference(ResolvedObject.Object));
		Data->SetArrayField(TEXT("functions"), Functions);
		Result = BAT::Reflection::MakeStructuredSuccess(RequestId, Data);
	}, 10.0f);

	return (bCompleted && Result.IsSet()) ? Result.GetValue() : BAT::Reflection::MakeStructuredError(RequestId, TEXT("game_thread_timeout"), TEXT("Timed out waiting for game thread execution."), 504);
}

FAutomationResult FReflectionFunctionService::CallFunction(FBlueprintAutomationToolkitModule& Module, const FString& RequestId, const TSharedPtr<FJsonObject>& BodyObj) const
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

		FString FunctionName;
		BodyObj->TryGetStringField(TEXT("function"), FunctionName);
		if (FunctionName.TrimStartAndEnd().IsEmpty())
		{
			Result = BAT::Reflection::MakeStructuredError(RequestId, TEXT("InvalidArguments"), TEXT("Body must include non-empty 'function'."), 400);
			return;
		}

		UFunction* Function = nullptr;
		if (!Validation.ResolveFunction(ResolvedObject.Object, FunctionName, RequestId, Function, Failure))
		{
			Result = Failure;
			return;
		}

		if (!Validation.ValidateFunctionCall(Module, ResolvedObject, Function, RequestId, Failure))
		{
			Result = Failure;
			return;
		}

		const TSharedPtr<FJsonObject>* ArgumentsObject = nullptr;
		BodyObj->TryGetObjectField(TEXT("arguments"), ArgumentsObject);
		if ((!ArgumentsObject || !ArgumentsObject->IsValid()) && BodyObj.IsValid())
		{
			BodyObj->TryGetObjectField(TEXT("args"), ArgumentsObject);
		}
		const TSharedPtr<FJsonObject> Arguments = (ArgumentsObject && ArgumentsObject->IsValid()) ? *ArgumentsObject : MakeShared<FJsonObject>();

		uint8* Params = static_cast<uint8*>(FMemory::Malloc(Function->ParmsSize));
		FMemory::Memzero(Params, Function->ParmsSize);
		TArray<FProperty*> ParameterProperties;
		for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			FProperty* Parameter = *It;
			if (!Parameter)
			{
				continue;
			}

			ParameterProperties.Add(Parameter);
			Parameter->InitializeValue_InContainer(Params);
		}

		ON_SCOPE_EXIT
		{
			for (FProperty* Parameter : ParameterProperties)
			{
				if (Parameter)
				{
					Parameter->DestroyValue_InContainer(Params);
				}
			}
			FMemory::Free(Params);
		};

		for (FProperty* Parameter : ParameterProperties)
		{
			if (!Parameter)
			{
				continue;
			}

			if (!Serialization.IsSupportedPropertyType(Parameter, true))
			{
				Result = BAT::Reflection::MakeStructuredError(RequestId, TEXT("InvalidType"), FString::Printf(TEXT("Function '%s' contains unsupported parameter '%s'."), *Function->GetName(), *Parameter->GetName()), 400);
				return;
			}

			const bool bIsReturn = Parameter->HasAnyPropertyFlags(CPF_ReturnParm);
			const bool bIsOut = Parameter->HasAnyPropertyFlags(CPF_OutParm);
			const bool bIsReference = Parameter->HasAnyPropertyFlags(CPF_ReferenceParm);
			if (bIsReturn)
			{
				continue;
			}

			const bool bRequiresInput = !bIsOut || bIsReference;
			const TSharedPtr<FJsonValue>* ArgumentValue = Arguments->Values.Find(Parameter->GetName());
			if (bRequiresInput)
			{
				if (!ArgumentValue)
				{
					Result = BAT::Reflection::MakeStructuredError(RequestId, TEXT("InvalidArguments"), FString::Printf(TEXT("Function '%s' requires argument '%s'."), *Function->GetName(), *Parameter->GetName()), 400);
					return;
				}

				FString CompatibilityCode;
				FString CompatibilityMessage;
				if (!Validation.ValidateValueTypeCompatibility(Parameter, *ArgumentValue, Resolver, CompatibilityCode, CompatibilityMessage))
				{
					Result = BAT::Reflection::MakeStructuredError(RequestId, CompatibilityCode, FString::Printf(TEXT("Argument '%s': %s"), *Parameter->GetName(), *CompatibilityMessage), 400);
					return;
				}

				void* ParameterPtr = Parameter->ContainerPtrToValuePtr<void>(Params);
				FString ErrorCode;
				FString ErrorMessage;
				if (!Serialization.DeserializeValue(Parameter, ParameterPtr, *ArgumentValue, Resolver, ErrorCode, ErrorMessage))
				{
					Result = BAT::Reflection::MakeStructuredError(RequestId, ErrorCode, FString::Printf(TEXT("Argument '%s': %s"), *Parameter->GetName(), *ErrorMessage), 400);
					return;
				}
			}
		}

		TUniquePtr<FScopedTransaction> Transaction;
		if (!Function->HasAnyFunctionFlags(FUNC_Const))
		{
			Transaction = MakeUnique<FScopedTransaction>(NSLOCTEXT("BlueprintAutomationToolkit", "ReflectionCallFunction", "Reflection Call Function"));
			ResolvedObject.Object->Modify();
		}

		ResolvedObject.Object->ProcessEvent(Function, Params);
		if (!Function->HasAnyFunctionFlags(FUNC_Const))
		{
			MarkDirtyIfPersistent(ResolvedObject.Object);
		}

		TSharedRef<FJsonObject> OutputValues = MakeShared<FJsonObject>();
		TSharedPtr<FJsonValue> ReturnValue = MakeShared<FJsonValueNull>();
		for (FProperty* Parameter : ParameterProperties)
		{
			if (!Parameter)
			{
				continue;
			}

			void* ParameterPtr = Parameter->ContainerPtrToValuePtr<void>(Params);
			if (Parameter->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				ReturnValue = Serialization.SerializeValue(Parameter, ParameterPtr);
				continue;
			}

			if (Parameter->HasAnyPropertyFlags(CPF_OutParm))
			{
				OutputValues->SetField(Parameter->GetName(), Serialization.SerializeValue(Parameter, ParameterPtr));
			}
		}

		TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetObjectField(TEXT("object"), Serialization.SerializeObjectReference(ResolvedObject.Object));
		Data->SetStringField(TEXT("function"), Function->GetName());
		Data->SetField(TEXT("returnValue"), ReturnValue);
		Data->SetObjectField(TEXT("outParameters"), OutputValues);
		Result = BAT::Reflection::MakeStructuredSuccess(RequestId, Data);
	}, 10.0f);

	return (bCompleted && Result.IsSet()) ? Result.GetValue() : BAT::Reflection::MakeStructuredError(RequestId, TEXT("game_thread_timeout"), TEXT("Timed out waiting for game thread execution."), 504);
}
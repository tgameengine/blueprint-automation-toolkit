// Copyright 2026 AkaSoft. All Rights Reserved.

#include "Services/Reflection/ReflectionValidationService.h"

#include "BlueprintAutomationToolkitModule.h"

#include "Services/Reflection/ReflectionObjectResolver.h"
#include "Services/Reflection/ReflectionSerializationService.h"
#include "Services/Reflection/ReflectionTypes.h"

#include "UObject/UnrealType.h"

namespace
{
	static bool MatchesClassEntry(const TSet<FString>& Entries, const UClass* Class)
	{
		for (const UClass* Current = Class; Current; Current = Current->GetSuperClass())
		{
			const FString PathName = Current->GetPathName().ToLower();
			const FString ShortName = Current->GetName().ToLower();
			if (Entries.Contains(PathName) || Entries.Contains(ShortName))
			{
				return true;
			}
		}
		return false;
	}

	static FName GetRootPropertyName(const FString& PropertyPath)
	{
		FString RootName = PropertyPath;
		int32 DotIndex = INDEX_NONE;
		if (PropertyPath.FindChar(TEXT('.'), DotIndex))
		{
			RootName = PropertyPath.Left(DotIndex);
		}
		RootName.TrimStartAndEndInline();
		return FName(*RootName);
	}
}

bool FReflectionValidationService::ValidateObject(UObject* Object, const FString& RequestId, FAutomationResult& OutFailure) const
{
	if (Object)
	{
		return true;
	}

	OutFailure = BAT::Reflection::MakeStructuredError(RequestId, TEXT("ObjectNotFound"), TEXT("Resolved object is null."), 404);
	return false;
}

bool FReflectionValidationService::ResolveProperty(UObject* RootObject, const FString& PropertyPath, const FString& RequestId, BAT::Reflection::FResolvedProperty& OutResolved, FAutomationResult& OutFailure) const
{
	OutResolved = BAT::Reflection::FResolvedProperty();
	if (!ValidateObject(RootObject, RequestId, OutFailure))
	{
		return false;
	}

	TArray<FString> Segments;
	PropertyPath.ParseIntoArray(Segments, TEXT("."), true);
	if (Segments.Num() == 0)
	{
		OutFailure = BAT::Reflection::MakeStructuredError(RequestId, TEXT("InvalidArguments"), TEXT("Property path must be non-empty."), 400);
		return false;
	}

	void* ContainerPtr = RootObject;
	UStruct* ContainerStruct = RootObject->GetClass();
	UObject* OwnerObject = RootObject;

	for (int32 Index = 0; Index < Segments.Num(); ++Index)
	{
		const FString Segment = Segments[Index].TrimStartAndEnd();
		if (Segment.IsEmpty())
		{
			OutFailure = BAT::Reflection::MakeStructuredError(RequestId, TEXT("InvalidArguments"), TEXT("Property path contains an empty segment."), 400);
			return false;
		}

		FProperty* Property = ContainerStruct ? FindFProperty<FProperty>(ContainerStruct, FName(*Segment)) : nullptr;
		if (!Property)
		{
			OutFailure = BAT::Reflection::MakeStructuredError(RequestId, TEXT("PropertyNotFound"), FString::Printf(TEXT("Property '%s' does not exist on object '%s'."), *PropertyPath, *RootObject->GetPathName()), 404);
			return false;
		}

		if (Index == Segments.Num() - 1)
		{
			OutResolved.Property = Property;
			OutResolved.ValuePtr = Property->ContainerPtrToValuePtr<void>(ContainerPtr);
			OutResolved.OwnerStruct = ContainerStruct;
			OutResolved.OwnerObject = OwnerObject;
			OutResolved.PropertyPath = PropertyPath;
			return OutResolved.ValuePtr != nullptr;
		}

		if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			ContainerPtr = StructProperty->ContainerPtrToValuePtr<void>(ContainerPtr);
			ContainerStruct = StructProperty->Struct;
			continue;
		}

		if (FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property))
		{
			UObject* NextObject = ObjectProperty->GetObjectPropertyValue_InContainer(ContainerPtr);
			if (!NextObject)
			{
				OutFailure = BAT::Reflection::MakeStructuredError(RequestId, TEXT("ObjectNotFound"), FString::Printf(TEXT("Property '%s' resolves through a null object reference."), *PropertyPath), 404);
				return false;
			}
			ContainerPtr = NextObject;
			ContainerStruct = NextObject->GetClass();
			OwnerObject = NextObject;
			continue;
		}

		OutFailure = BAT::Reflection::MakeStructuredError(RequestId, TEXT("InvalidType"), FString::Printf(TEXT("Property path '%s' traverses through unsupported type '%s'."), *PropertyPath, *Property->GetCPPType()), 400);
		return false;
	}

	OutFailure = BAT::Reflection::MakeStructuredError(RequestId, TEXT("PropertyNotFound"), FString::Printf(TEXT("Property '%s' does not exist on object '%s'."), *PropertyPath, *RootObject->GetPathName()), 404);
	return false;
}

bool FReflectionValidationService::ResolveFunction(UObject* RootObject, const FString& FunctionName, const FString& RequestId, UFunction*& OutFunction, FAutomationResult& OutFailure) const
{
	OutFunction = nullptr;
	if (!ValidateObject(RootObject, RequestId, OutFailure))
	{
		return false;
	}

	const FString TrimmedName = FunctionName.TrimStartAndEnd();
	if (TrimmedName.IsEmpty())
	{
		OutFailure = BAT::Reflection::MakeStructuredError(RequestId, TEXT("InvalidArguments"), TEXT("Function name must be non-empty."), 400);
		return false;
	}

	OutFunction = RootObject->FindFunction(FName(*TrimmedName));
	if (!OutFunction)
	{
		OutFailure = BAT::Reflection::MakeStructuredError(RequestId, TEXT("FunctionNotFound"), FString::Printf(TEXT("Function '%s' does not exist on object '%s'."), *TrimmedName, *RootObject->GetPathName()), 404);
		return false;
	}

	return true;
}

bool FReflectionValidationService::ValidateValueTypeCompatibility(FProperty* Property, const TSharedPtr<FJsonValue>& JsonValue, const FReflectionObjectResolver& Resolver, FString& OutErrorCode, FString& OutErrorMessage) const
{
	OutErrorCode = TEXT("InvalidType");
	OutErrorMessage = TEXT("Unsupported reflected value type.");

	if (!Property)
	{
		OutErrorCode = TEXT("PropertyNotFound");
		OutErrorMessage = TEXT("Property could not be resolved.");
		return false;
	}

	FReflectionSerializationService Serialization;
	void* Storage = FMemory::Malloc(Property->GetSize(), Property->GetMinAlignment());
	Property->InitializeValue(Storage);
	const bool bCompatible = Serialization.DeserializeValue(Property, Storage, JsonValue, Resolver, OutErrorCode, OutErrorMessage);
	Property->DestroyValue(Storage);
	FMemory::Free(Storage);
	return bCompatible;
}

bool FReflectionValidationService::ShouldListProperty(const FProperty* Property) const
{
	return Property
		&& !Property->HasAnyPropertyFlags(CPF_Deprecated | CPF_Transient)
		&& (Property->HasAnyPropertyFlags(CPF_Edit) || Property->HasAnyPropertyFlags(CPF_BlueprintVisible) || Property->HasAnyPropertyFlags(CPF_BlueprintReadOnly));
}

bool FReflectionValidationService::ShouldReadProperty(const FProperty* Property) const
{
	return ShouldListProperty(Property) && !Property->HasAnyPropertyFlags(CPF_NonTransactional);
}

bool FReflectionValidationService::IsClassDenied(const FBlueprintAutomationToolkitModule& Module, const UClass* Class) const
{
	return Class && MatchesClassEntry(Module.GetDeniedReflectionClasses(), Class);
}

bool FReflectionValidationService::IsClassAllowed(const FBlueprintAutomationToolkitModule& Module, const UClass* Class) const
{
	const TSet<FString>& AllowedClasses = Module.GetAllowedReflectionClasses();
	return Class && (AllowedClasses.Num() == 0 || MatchesClassEntry(AllowedClasses, Class));
}

bool FReflectionValidationService::IsPropertyDenied(const FBlueprintAutomationToolkitModule& Module, const FName PropertyName) const
{
	return Module.GetDeniedReflectionProperties().Contains(PropertyName);
}

bool FReflectionValidationService::IsFunctionDenied(const FBlueprintAutomationToolkitModule& Module, const FName FunctionName) const
{
	return Module.GetDeniedReflectionFunctions().Contains(FunctionName);
}

bool FReflectionValidationService::HasUnsafePropertyFlags(const FProperty* Property) const
{
	return Property && Property->HasAnyPropertyFlags(CPF_EditConst | CPF_DisableEditOnInstance | CPF_Deprecated | CPF_Transient | CPF_NonTransactional);
}

bool FReflectionValidationService::ValidatePropertyWrite(const FBlueprintAutomationToolkitModule& Module, const BAT::Reflection::FResolvedObject& ResolvedObject, const BAT::Reflection::FResolvedProperty& ResolvedProperty, const FString& RequestId, FAutomationResult& OutFailure) const
{
	if (!ResolvedProperty.Property)
	{
		OutFailure = BAT::Reflection::MakeStructuredError(RequestId, TEXT("PropertyNotFound"), TEXT("Property could not be resolved."), 404);
		return false;
	}

	const FName RootPropertyName = GetRootPropertyName(ResolvedProperty.PropertyPath);
	if (IsPropertyDenied(Module, RootPropertyName))
	{
		UE_LOG(LogBlueprintAutomationToolkit, Warning, TEXT("Reflection blocked property write '%s': property denylist"), *ResolvedProperty.PropertyPath);
		OutFailure = BAT::Reflection::MakeStructuredError(RequestId, TEXT("safe_mode_denied"), FString::Printf(TEXT("Property '%s' is denied by reflection policy."), *ResolvedProperty.PropertyPath), 403);
		return false;
	}

	if (!Module.IsSafeModeEnabled())
	{
		return true;
	}

	if (IsClassDenied(Module, ResolvedObject.Object->GetClass()))
	{
		UE_LOG(LogBlueprintAutomationToolkit, Warning, TEXT("Reflection blocked property write '%s': class denylist"), *ResolvedProperty.PropertyPath);
		OutFailure = BAT::Reflection::MakeStructuredError(RequestId, TEXT("safe_mode_denied"), TEXT("Target class is denied by reflection policy."), 403);
		return false;
	}

	if (!IsClassAllowed(Module, ResolvedObject.Object->GetClass()) && !Module.GetAllowedReflectionProperties().Contains(RootPropertyName))
	{
		UE_LOG(LogBlueprintAutomationToolkit, Warning, TEXT("Reflection blocked property write '%s': class not allowlisted"), *ResolvedProperty.PropertyPath);
		OutFailure = BAT::Reflection::MakeStructuredError(RequestId, TEXT("safe_mode_denied"), TEXT("Target class is not allowlisted for reflection writes in Safe Mode."), 403);
		return false;
	}

	if (!ResolvedProperty.Property->HasAnyPropertyFlags(CPF_Edit) || HasUnsafePropertyFlags(ResolvedProperty.Property))
	{
		UE_LOG(LogBlueprintAutomationToolkit, Warning, TEXT("Reflection blocked property write '%s': property is not safely editable"), *ResolvedProperty.PropertyPath);
		OutFailure = BAT::Reflection::MakeStructuredError(RequestId, TEXT("safe_mode_denied"), FString::Printf(TEXT("Property '%s' is not safely editable in Safe Mode."), *ResolvedProperty.PropertyPath), 403);
		return false;
	}

	return true;
}

bool FReflectionValidationService::ShouldListFunction(const UFunction* Function) const
{
	return Function && (Function->HasAnyFunctionFlags(FUNC_BlueprintCallable) || Function->HasMetaData(TEXT("CallInEditor")));
}

bool FReflectionValidationService::IsLatentFunction(const UFunction* Function) const
{
	if (!Function || Function->HasMetaData(TEXT("Latent")))
	{
		return Function != nullptr;
	}

	for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
	{
		const FProperty* Property = *It;
		const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
		if (StructProperty && StructProperty->Struct && StructProperty->Struct->GetName().Equals(TEXT("LatentActionInfo"), ESearchCase::CaseSensitive))
		{
			return true;
		}
	}

	return false;
}

bool FReflectionValidationService::ValidateFunctionCall(const FBlueprintAutomationToolkitModule& Module, const BAT::Reflection::FResolvedObject& ResolvedObject, UFunction* Function, const FString& RequestId, FAutomationResult& OutFailure) const
{
	if (!Function)
	{
		OutFailure = BAT::Reflection::MakeStructuredError(RequestId, TEXT("FunctionNotFound"), TEXT("Requested function does not exist on the resolved object."), 404);
		return false;
	}

	if (!ShouldListFunction(Function))
	{
		OutFailure = BAT::Reflection::MakeStructuredError(RequestId, TEXT("safe_mode_denied"), TEXT("Function is not BlueprintCallable or CallInEditor."), 403);
		return false;
	}

	if (Function->HasAnyFunctionFlags(FUNC_Exec | FUNC_Net | FUNC_NetClient | FUNC_NetServer | FUNC_NetMulticast) || IsLatentFunction(Function))
	{
		OutFailure = BAT::Reflection::MakeStructuredError(RequestId, TEXT("safe_mode_denied"), TEXT("Function uses unsupported or unsafe Unreal function flags."), 403);
		return false;
	}

	if (IsFunctionDenied(Module, Function->GetFName()))
	{
		UE_LOG(LogBlueprintAutomationToolkit, Warning, TEXT("Reflection blocked function call '%s': function denylist"), *Function->GetName());
		OutFailure = BAT::Reflection::MakeStructuredError(RequestId, TEXT("safe_mode_denied"), TEXT("Function is denied by reflection policy."), 403);
		return false;
	}

	if (!Module.IsSafeModeEnabled())
	{
		return true;
	}

	if (IsClassDenied(Module, ResolvedObject.Object->GetClass()))
	{
		UE_LOG(LogBlueprintAutomationToolkit, Warning, TEXT("Reflection blocked function call '%s': class denylist"), *Function->GetName());
		OutFailure = BAT::Reflection::MakeStructuredError(RequestId, TEXT("safe_mode_denied"), TEXT("Target class is denied by reflection policy."), 403);
		return false;
	}

	const bool bClassAllowed = IsClassAllowed(Module, ResolvedObject.Object->GetClass());
	const bool bFunctionAllowed = Module.GetAllowedReflectionFunctions().Contains(Function->GetFName());
	if (!bClassAllowed && !bFunctionAllowed)
	{
		UE_LOG(LogBlueprintAutomationToolkit, Warning, TEXT("Reflection blocked function call '%s': class not allowlisted"), *Function->GetName());
		OutFailure = BAT::Reflection::MakeStructuredError(RequestId, TEXT("safe_mode_denied"), TEXT("Target class is not allowlisted for reflection function calls in Safe Mode."), 403);
		return false;
	}

	return true;
}

bool FReflectionValidationService::IsFunctionCallableInSafeMode(const FBlueprintAutomationToolkitModule& Module, const UClass* OwnerClass, const UFunction* Function) const
{
	if (!Function || !OwnerClass)
	{
		return false;
	}

	if (IsFunctionDenied(Module, Function->GetFName()) || IsClassDenied(Module, OwnerClass))
	{
		return false;
	}

	if (!Module.IsSafeModeEnabled())
	{
		return true;
	}

	return IsClassAllowed(Module, OwnerClass) || Module.GetAllowedReflectionFunctions().Contains(Function->GetFName());
}
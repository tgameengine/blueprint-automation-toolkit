#pragma once

#include "Services/Reflection/ReflectionTypes.h"

class FBlueprintAutomationToolkitModule;
class UFunction;

class FReflectionValidationService
{
public:
	bool ResolveProperty(UObject* RootObject, const FString& PropertyPath, const FString& RequestId, BAT::Reflection::FResolvedProperty& OutResolved, FAutomationResult& OutFailure) const;
	bool ShouldListProperty(const FProperty* Property) const;
	bool ShouldReadProperty(const FProperty* Property) const;
	bool ValidatePropertyWrite(const FBlueprintAutomationToolkitModule& Module, const BAT::Reflection::FResolvedObject& ResolvedObject, const BAT::Reflection::FResolvedProperty& ResolvedProperty, const FString& RequestId, FAutomationResult& OutFailure) const;
	bool ShouldListFunction(const UFunction* Function) const;
	bool ValidateFunctionCall(const FBlueprintAutomationToolkitModule& Module, const BAT::Reflection::FResolvedObject& ResolvedObject, UFunction* Function, const FString& RequestId, FAutomationResult& OutFailure) const;
	bool IsFunctionCallableInSafeMode(const FBlueprintAutomationToolkitModule& Module, const UClass* OwnerClass, const UFunction* Function) const;

private:
	bool IsClassDenied(const FBlueprintAutomationToolkitModule& Module, const UClass* Class) const;
	bool IsClassAllowed(const FBlueprintAutomationToolkitModule& Module, const UClass* Class) const;
	bool IsPropertyDenied(const FBlueprintAutomationToolkitModule& Module, const FName PropertyName) const;
	bool IsFunctionDenied(const FBlueprintAutomationToolkitModule& Module, const FName FunctionName) const;
	bool HasUnsafePropertyFlags(const FProperty* Property) const;
	bool IsLatentFunction(const UFunction* Function) const;
};
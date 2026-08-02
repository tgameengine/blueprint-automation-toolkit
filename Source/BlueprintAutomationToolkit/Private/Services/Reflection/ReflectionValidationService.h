// Copyright 2026 AkaSoft. All Rights Reserved.

#pragma once

#include "Services/Reflection/ReflectionTypes.h"

class FBlueprintAutomationToolkitModule;
class UFunction;

class FReflectionValidationService
{
public:
	bool ValidateObject(UObject* Object, const FString& RequestId, FAutomationResult& OutFailure) const;
	bool ResolveProperty(UObject* RootObject, const FString& PropertyPath, const FString& RequestId, BAT::Reflection::FResolvedProperty& OutResolved, FAutomationResult& OutFailure) const;
	bool ResolveFunction(UObject* RootObject, const FString& FunctionName, const FString& RequestId, UFunction*& OutFunction, FAutomationResult& OutFailure) const;
	bool ValidateValueTypeCompatibility(FProperty* Property, const TSharedPtr<class FJsonValue>& JsonValue, const class FReflectionObjectResolver& Resolver, FString& OutErrorCode, FString& OutErrorMessage) const;
	bool ShouldListProperty(const FProperty* Property) const;
	bool ShouldReadProperty(const FProperty* Property) const;
	bool ValidatePropertyRead(const FBlueprintAutomationToolkitModule& Module, UObject* Object, const BAT::Reflection::FResolvedProperty& ResolvedProperty, const FString& RequestId, FAutomationResult& OutFailure) const;
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

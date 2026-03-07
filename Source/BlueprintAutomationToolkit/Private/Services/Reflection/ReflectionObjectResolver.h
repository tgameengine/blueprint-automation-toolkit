#pragma once

#include "Services/Reflection/ReflectionTypes.h"

class FBlueprintAutomationToolkitModule;

class FReflectionObjectResolver
{
public:
	bool Resolve(FBlueprintAutomationToolkitModule& Module, const TSharedPtr<class FJsonObject>& BodyObj, const FString& RequestId, BAT::Reflection::FResolvedObject& OutResolved, FAutomationResult& OutFailure) const;
	bool ResolveObjectReference(const FString& ReferencePath, UClass* RequiredClass, UObject*& OutObject) const;
	bool ResolveClassReference(const FString& ReferencePath, UClass*& OutClass) const;
};
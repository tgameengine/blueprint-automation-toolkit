#pragma once

#include "Services/Reflection/ReflectionTypes.h"

class FBlueprintAutomationToolkitModule;
class AActor;

class FReflectionObjectResolver
{
public:
	UObject* ResolveObjectByPath(const FString& ObjectPath) const;
	AActor* ResolveActorByName(const FString& ActorName) const;
	const FString& GetLastErrorMessage() const { return LastErrorMessage; }

	bool Resolve(FBlueprintAutomationToolkitModule& Module, const TSharedPtr<class FJsonObject>& BodyObj, const FString& RequestId, BAT::Reflection::FResolvedObject& OutResolved, FAutomationResult& OutFailure) const;
	bool ResolveObjectReference(const FString& ReferencePath, UClass* RequiredClass, UObject*& OutObject) const;
	bool ResolveClassReference(const FString& ReferencePath, UClass*& OutClass) const;

private:
	void SetLastError(const FString& Message) const;

	mutable FString LastErrorMessage;
};
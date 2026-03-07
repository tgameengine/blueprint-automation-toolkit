#pragma once

#include "CoreMinimal.h"

class FBlueprintAutomationToolkitModule;
class FReflectionObjectResolver;
class UFunction;
class UObject;
class FProperty;

class FReflectionSerializationService
{
public:
	bool IsSupportedPropertyType(const FProperty* Property, bool bForWrite) const;
	TSharedRef<class FJsonObject> SerializeObjectReference(const UObject* Object) const;
	TSharedPtr<class FJsonObject> SerializeReflectedValue(const FProperty* Property, const void* ValuePtr) const;
	TSharedRef<class FJsonObject> DescribeProperty(const FProperty* Property) const;
	TSharedRef<class FJsonObject> DescribeFunction(const UFunction* Function, bool bCallableInSafeMode) const;
	TSharedPtr<class FJsonValue> SerializeValue(const FProperty* Property, const void* ValuePtr) const;
	bool DeserializeValue(FProperty* Property, void* ValuePtr, const TSharedPtr<class FJsonValue>& JsonValue, const FReflectionObjectResolver& Resolver, FString& OutCode, FString& OutMessage) const;
};
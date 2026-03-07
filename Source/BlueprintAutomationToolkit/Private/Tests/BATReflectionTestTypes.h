#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "BATReflectionTestTypes.generated.h"

UCLASS()
class UBATReflectionTestObject : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category="Reflection Test")
	int32 MaxHealth = 100;

	UPROPERTY(EditAnywhere, Category="Reflection Test")
	bool bCanAttack = true;

	UPROPERTY(EditAnywhere, Category="Reflection Test")
	FVector SpawnOffset = FVector(10.0f, 20.0f, 30.0f);

	UPROPERTY(EditAnywhere, Category="Reflection Test")
	FLinearColor Tint = FLinearColor::White;

	UFUNCTION(BlueprintCallable, CallInEditor, Category="Reflection Test")
	int32 AddToHealth(int32 Delta);

	UFUNCTION(BlueprintCallable, CallInEditor, Category="Reflection Test")
	void SetSpawnOffset(FVector NewOffset);
};
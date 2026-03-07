#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "BATAction.generated.h"

UCLASS(BlueprintType)
class BLUEPRINTAUTOMATIONTOOLKIT_API UBATAction : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BAT")
	FName ActionName;

	UFUNCTION(BlueprintNativeEvent, CallInEditor, Category = "BAT")
	void Execute(const FString& ArgsJson, FString& OutResultJson, bool& bOutOk, FString& OutError) const;
};

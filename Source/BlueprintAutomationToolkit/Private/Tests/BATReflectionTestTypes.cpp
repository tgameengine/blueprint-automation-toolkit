#include "Tests/BATReflectionTestTypes.h"

int32 UBATReflectionTestObject::AddToHealth(int32 Delta)
{
	MaxHealth += Delta;
	return MaxHealth;
}

void UBATReflectionTestObject::SetSpawnOffset(FVector NewOffset)
{
	SpawnOffset = NewOffset;
}
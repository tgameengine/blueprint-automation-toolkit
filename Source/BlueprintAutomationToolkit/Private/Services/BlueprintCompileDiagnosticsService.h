#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"

class UBlueprint;
class FJsonObject;

namespace BAT::BlueprintCompileDiagnostics
{
	struct FDiagnostics
	{
		FString CompileStatus = TEXT("not_requested");
		bool bCompileSucceeded = false;
		int32 ErrorCount = 0;
		int32 WarningCount = 0;
		TArray<TSharedPtr<FJsonValue>> Errors;
		TArray<TSharedPtr<FJsonValue>> Warnings;
	};

	FDiagnostics Compile(UBlueprint* Blueprint);
	TSharedRef<FJsonObject> MakeDiagnosticsObject(const FDiagnostics& Diagnostics);
}
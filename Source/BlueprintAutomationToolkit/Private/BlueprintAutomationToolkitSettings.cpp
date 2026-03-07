#include "BlueprintAutomationToolkitSettings.h"

UBlueprintAutomationToolkitSettings::UBlueprintAutomationToolkitSettings()
	: bEnableServer(false)
	, Port(9876)
	, bRequireAuthToken(true)
	, bSafeMode(true)
{
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("BlueprintAutomationToolkit");
}

FName UBlueprintAutomationToolkitSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}
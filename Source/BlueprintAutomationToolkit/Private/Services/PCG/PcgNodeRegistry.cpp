#include "Services/PCG/PcgNodeRegistry.h"

#include "Elements/ControlFlow/PCGBranch.h"
#include "Elements/PCGAttributeFilter.h"
#include "Elements/PCGDifferenceElement.h"
#include "Elements/PCGStaticMeshSpawner.h"
#include "Elements/PCGSurfaceSampler.h"
#include "Elements/PCGTransformPoints.h"
#include "PCGSettings.h"

namespace
{
	static const TArray<FPcgNodeFamilySpec>& GetRegistry()
	{
		static const TArray<FPcgNodeFamilySpec> Registry = {
			{
				TEXT("StaticMeshSpawner"),
				UPCGStaticMeshSpawnerSettings::StaticClass(),
				TSet<FString>{ TEXT("density"), TEXT("placement_mode"), TEXT("seed") },
				true
			},
			{
				TEXT("SurfaceSampler"),
				UPCGSurfaceSamplerSettings::StaticClass(),
				TSet<FString>{ TEXT("density"), TEXT("seed") },
				false
			},
			{
				TEXT("TransformPoints"),
				UPCGTransformPointsSettings::StaticClass(),
				TSet<FString>{ TEXT("offset"), TEXT("rotation"), TEXT("scale_min"), TEXT("scale_max") },
				false
			},
			{
				TEXT("Difference"),
				UPCGDifferenceSettings::StaticClass(),
				TSet<FString>{ TEXT("density_function"), TEXT("mode") },
				false
			},
			{
				TEXT("AttributeFilter"),
				UPCGAttributeFilteringSettings::StaticClass(),
				TSet<FString>{ TEXT("target_attribute"), TEXT("operator"), TEXT("value") },
				false
			},
			{
				TEXT("Branch"),
				UPCGBranchSettings::StaticClass(),
				TSet<FString>{ TEXT("condition_attribute") },
				false
			}
		};

		return Registry;
	}
}

const FPcgNodeFamilySpec* FPcgNodeRegistry::FindByExternalType(const FString& ExternalType)
{
	FString Normalized = ExternalType;
	Normalized.TrimStartAndEndInline();

	for (const FPcgNodeFamilySpec& Spec : GetRegistry())
	{
		if (Spec.ExternalType.Equals(Normalized, ESearchCase::CaseSensitive))
		{
			return &Spec;
		}
	}

	return nullptr;
}

bool FPcgNodeRegistry::IsSupportedType(const FString& ExternalType)
{
	return FindByExternalType(ExternalType) != nullptr;
}

bool FPcgNodeRegistry::IsSettingKeySupported(const FString& ExternalType, const FString& SettingKey)
{
	const FPcgNodeFamilySpec* Spec = FindByExternalType(ExternalType);
	if (!Spec)
	{
		return false;
	}

	FString NormalizedKey = SettingKey;
	NormalizedKey.TrimStartAndEndInline();
	return Spec->SupportedSettingKeys.Contains(NormalizedKey);
}

void FPcgNodeRegistry::GetSupportedFamilies(TArray<FPcgNodeFamilySpec>& OutFamilies)
{
	OutFamilies = GetRegistry();
}
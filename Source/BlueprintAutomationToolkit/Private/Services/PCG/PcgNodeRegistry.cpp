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
				TSet<FString>{
					TEXT("out_attribute_name"),
					TEXT("apply_mesh_bounds_to_points"),
					TEXT("synchronous_load"),
					TEXT("allow_merge_different_data_in_same_instanced_components"),
					TEXT("silence_override_attribute_not_found_errors"),
					TEXT("warn_on_identical_spawn")
				},
				true
			},
			{
				TEXT("SurfaceSampler"),
				UPCGSurfaceSamplerSettings::StaticClass(),
				TSet<FString>{
					TEXT("points_per_squared_meter"),
					TEXT("looseness"),
					TEXT("unbounded"),
					TEXT("apply_density_to_points"),
					TEXT("point_steepness"),
					TEXT("use_legacy_grid_creation_method")
				},
				false
			},
			{
				TEXT("TransformPoints"),
				UPCGTransformPointsSettings::StaticClass(),
				TSet<FString>{
					TEXT("apply_to_attribute"),
					TEXT("attribute_name"),
					TEXT("absolute_offset"),
					TEXT("absolute_rotation"),
					TEXT("absolute_scale"),
					TEXT("uniform_scale"),
					TEXT("recompute_seed")
				},
				false
			},
			{
				TEXT("Difference"),
				UPCGDifferenceSettings::StaticClass(),
				TSet<FString>{ TEXT("diff_metadata"), TEXT("keep_zero_density_points") },
				false
			},
			{
				TEXT("AttributeFilter"),
				UPCGAttributeFilteringSettings::StaticClass(),
				TSet<FString>{
					TEXT("use_constant_threshold"),
					TEXT("warn_on_data_missing_attribute"),
					TEXT("generate_output_data_even_if_empty")
				},
				false
			},
			{
				TEXT("Branch"),
				UPCGBranchSettings::StaticClass(),
				TSet<FString>{ TEXT("output_to_b") },
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

const FPcgNodeFamilySpec* FPcgNodeRegistry::FindBySettingsClass(const UClass* SettingsClass)
{
	if (!SettingsClass)
	{
		return nullptr;
	}

	for (const FPcgNodeFamilySpec& Spec : GetRegistry())
	{
		if (*Spec.SettingsClass && SettingsClass->IsChildOf(Spec.SettingsClass.Get()))
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
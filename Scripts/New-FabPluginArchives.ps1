# Copyright (c) AkaSoft 2026. All rights reserved.

[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string]$BuildRootPattern,

	[Parameter(Mandatory = $true)]
	[string]$OutputDirectory,

	[string[]]$EngineVersions = @("5.5", "5.6", "5.7", "5.8"),

	[string]$Revision = "fab-r2"
)

$ErrorActionPreference = "Stop"
$PluginName = "BlueprintAutomationToolkit"
$DescriptorName = "$PluginName.uplugin"
$DescriptorPath = Join-Path $PSScriptRoot "..\$DescriptorName"
$SourceDescriptor = Get-Content -LiteralPath $DescriptorPath -Raw | ConvertFrom-Json
$PluginVersion = [string]$SourceDescriptor.VersionName
$OutputRoot = [System.IO.Path]::GetFullPath($OutputDirectory)
$OutputPrefix = $OutputRoot.TrimEnd(
	[System.IO.Path]::DirectorySeparatorChar,
	[System.IO.Path]::AltDirectorySeparatorChar
) + [System.IO.Path]::DirectorySeparatorChar

New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null

function Reset-StageDirectory {
	param([Parameter(Mandatory = $true)][string]$Path)

	$FullPath = [System.IO.Path]::GetFullPath($Path)
	if (-not $FullPath.StartsWith(
		$script:OutputPrefix,
		[System.StringComparison]::OrdinalIgnoreCase
	)) {
		throw "Refusing to reset a staging path outside the output directory: $FullPath"
	}

	if (Test-Path -LiteralPath $FullPath) {
		Remove-Item -LiteralPath $FullPath -Recurse -Force
	}
	New-Item -ItemType Directory -Path $FullPath -Force | Out-Null
}

function Set-FabDescriptorMetadata {
	param(
		[Parameter(Mandatory = $true)][string]$Path,
		[Parameter(Mandatory = $true)][string]$EngineVersion
	)

	$Descriptor = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
	if ([string]$Descriptor.VersionName -ne $script:PluginVersion) {
		throw "VersionName mismatch in ${Path}: expected $script:PluginVersion"
	}

	if ($null -eq $Descriptor.PSObject.Properties["EngineVersion"]) {
		$Descriptor | Add-Member -NotePropertyName EngineVersion -NotePropertyValue "$EngineVersion.0"
	} else {
		$Descriptor.EngineVersion = "$EngineVersion.0"
	}
	$Descriptor.Installed = $true

	$Descriptor |
		ConvertTo-Json -Depth 100 |
		Set-Content -LiteralPath $Path -Encoding utf8
}

function Test-FabArchive {
	param(
		[Parameter(Mandatory = $true)][string]$Path,
		[Parameter(Mandatory = $true)][string]$EngineVersion
	)

	Add-Type -AssemblyName System.IO.Compression.FileSystem
	$Archive = [System.IO.Compression.ZipFile]::OpenRead($Path)
	try {
		$ExpectedDescriptor = "$script:PluginName/$script:DescriptorName"
		$DescriptorEntry = $Archive.Entries |
			Where-Object FullName -eq $ExpectedDescriptor
		if ($null -eq $DescriptorEntry) {
			throw "Archive is missing $ExpectedDescriptor"
		}

		$Reader = [System.IO.StreamReader]::new($DescriptorEntry.Open())
		try {
			$Descriptor = $Reader.ReadToEnd() | ConvertFrom-Json
		} finally {
			$Reader.Dispose()
		}

		if ([string]$Descriptor.VersionName -ne $script:PluginVersion) {
			throw "Archive VersionName mismatch: $($Descriptor.VersionName)"
		}
		if ([string]$Descriptor.EngineVersion -ne "$EngineVersion.0") {
			throw "Archive EngineVersion mismatch: $($Descriptor.EngineVersion)"
		}
		if ($Descriptor.Installed -ne $true) {
			throw "Archive descriptor must set Installed to true"
		}

		$RequiredEntries = @(
			"$script:PluginName/Binaries/Win64/UnrealEditor-$script:PluginName.dll",
			"$script:PluginName/Binaries/Win64/UnrealEditor.modules",
			"$script:PluginName/Source/BlueprintAutomationToolkit/BlueprintAutomationToolkit.Build.cs"
		)
		foreach ($RequiredEntry in $RequiredEntries) {
			if ($null -eq ($Archive.Entries | Where-Object FullName -eq $RequiredEntry)) {
				throw "Archive is missing $RequiredEntry"
			}
		}

		$Forbidden = @($Archive.Entries | Where-Object {
			$_.FullName -match "(^|/)(Intermediate|Build|Saved|\.git)(/|$)"
		})
		if ($Forbidden.Count -gt 0) {
			throw "Archive contains forbidden generated paths: $($Forbidden[0].FullName)"
		}

		return $Archive.Entries.Count
	} finally {
		$Archive.Dispose()
	}
}

$Results = foreach ($EngineVersion in $EngineVersions) {
	if ($EngineVersion -notmatch "^\d+\.\d+$") {
		throw "Engine version must use major.minor form: $EngineVersion"
	}

	$BuildRoot = [System.IO.Path]::GetFullPath(
		($BuildRootPattern -f $PluginVersion, $EngineVersion)
	)
	if (-not (Test-Path -LiteralPath $BuildRoot -PathType Container)) {
		throw "BuildPlugin output does not exist: $BuildRoot"
	}

	$RequiredBuildPaths = @(
		(Join-Path $BuildRoot $DescriptorName),
		(Join-Path $BuildRoot "Binaries\Win64"),
		(Join-Path $BuildRoot "Source")
	)
	foreach ($RequiredBuildPath in $RequiredBuildPaths) {
		if (-not (Test-Path -LiteralPath $RequiredBuildPath)) {
			throw "BuildPlugin output is incomplete: $RequiredBuildPath"
		}
	}

	$StageRoot = Join-Path $OutputRoot "stage-UE$EngineVersion"
	$StagePlugin = Join-Path $StageRoot $PluginName
	Reset-StageDirectory -Path $StageRoot
	New-Item -ItemType Directory -Path $StagePlugin -Force | Out-Null

	foreach ($Directory in @("Binaries", "Config", "Content", "Docs", "Resources", "Source")) {
		$SourcePath = Join-Path $BuildRoot $Directory
		if (Test-Path -LiteralPath $SourcePath) {
			Copy-Item -LiteralPath $SourcePath -Destination $StagePlugin -Recurse -Force
		}
	}
	foreach ($File in @($DescriptorName, "LICENSE", "README.md")) {
		$SourcePath = Join-Path $BuildRoot $File
		if (Test-Path -LiteralPath $SourcePath) {
			Copy-Item -LiteralPath $SourcePath -Destination $StagePlugin -Force
		}
	}

	$StagedDescriptor = Join-Path $StagePlugin $DescriptorName
	Set-FabDescriptorMetadata -Path $StagedDescriptor -EngineVersion $EngineVersion

	$ArchiveName = "$PluginName-$PluginVersion-UE$EngineVersion-Win64-$Revision.zip"
	$ArchivePath = Join-Path $OutputRoot $ArchiveName
	Compress-Archive -LiteralPath $StagePlugin -DestinationPath $ArchivePath -CompressionLevel Optimal -Force

	$EntryCount = Test-FabArchive -Path $ArchivePath -EngineVersion $EngineVersion
	$Hash = (Get-FileHash -LiteralPath $ArchivePath -Algorithm SHA256).Hash
	[pscustomobject]@{
		EngineVersion = "$EngineVersion.0"
		Archive = $ArchivePath
		Entries = $EntryCount
		Bytes = (Get-Item -LiteralPath $ArchivePath).Length
		SHA256 = $Hash
	}
}

$Results

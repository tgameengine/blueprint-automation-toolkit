// Copyright 2026 AkaSoft. All Rights Reserved.

#pragma once

#include "Automation/AutomationCommand.h"

#include "Templates/Function.h"
#include "Templates/UniquePtr.h"

using FBATAutomationCommandFactory = TFunction<TUniquePtr<FAutomationCommand>()>;

enum class EBATAutomationPermissionTier : uint8
{
	Read,
	Edit,
	Admin,
};

enum class EBATAutomationPermission : uint32
{
	None = 0,
	Editor = 1u << 0,
	Blueprint = 1u << 1,
	Pie = 1u << 2,
	Exec = 1u << 3,
	Python = 1u << 4,
	Filesystem = 1u << 5,
};

ENUM_CLASS_FLAGS(EBATAutomationPermission);

struct FBATAutomationCommandRegistration
{
	FString Endpoint;
	FBATAutomationCommandFactory Factory;
	EBATAutomationPermissionTier PermissionTier = EBATAutomationPermissionTier::Read;
	EBATAutomationPermission RequiredPermissions = EBATAutomationPermission::Editor;
	bool bBindRoute = true;
	bool bBlockDuringPie = false;
	bool bAllowReplace = false;
};

struct FBATAutomationCommandInfo
{
	FString Endpoint;
	EBATAutomationPermissionTier PermissionTier = EBATAutomationPermissionTier::Read;
	EBATAutomationPermission RequiredPermissions = EBATAutomationPermission::None;
	bool bBindRoute = false;
	bool bBlockDuringPie = false;
	bool bBuiltIn = false;
};
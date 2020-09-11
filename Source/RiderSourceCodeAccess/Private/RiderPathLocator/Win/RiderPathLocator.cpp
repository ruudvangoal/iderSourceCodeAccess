// Copyright Epic Games, Inc. All Rights Reserved.
#if PLATFORM_WINDOWS

#include "RiderPathLocator/RiderPathLocator.h"

#include "Internationalization/Regex.h"
#include "Misc/Paths.h"

#include "Windows/WindowsPlatformMisc.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include <winreg.h>
#include "Windows/HideWindowsPlatformTypes.h"

static TArray<FInstallInfo> CollectPathsFromToolbox(const Windows::HKEY RootKey)
{
	FString ToolboxBinPath;

	if (!FWindowsPlatformMisc::QueryRegKey(RootKey, TEXT("Software\\JetBrains\\Toolbox\\"), TEXT(""), ToolboxBinPath)) return {};

	FPaths::NormalizeDirectoryName(ToolboxBinPath);
	const FString PatternString(TEXT("(.*)/bin"));
	const FRegexPattern Pattern(PatternString);
	FRegexMatcher ToolboxPathMatcher(Pattern, ToolboxBinPath);
	if (!ToolboxPathMatcher.FindNext()) return {};

	const FString ToolboxPath = ToolboxPathMatcher.GetCaptureGroup(1);
	return FRiderPathLocator::GetInstallInfosFromToolbox(ToolboxPath, "rider64.exe");
}

static bool EnumerateRegistryKeys(HKEY Key, TArray<FString> &OutNames)
{
	for (DWORD Index = 0;; Index++)
	{
		TCHAR KeyName[256];
		DWORD KeyNameLength = sizeof(KeyName) / sizeof(KeyName[0]);

		const LONG Result = RegEnumKeyEx(Key, Index, KeyName, &KeyNameLength, nullptr, nullptr, nullptr, nullptr);
		if (Result == ERROR_NO_MORE_ITEMS)
		{
			break;
		}

		if (Result != ERROR_SUCCESS)
		{
			return false;
		}

		OutNames.Add(KeyName);
	}
	return true;
}

static bool EnumerateRegistryValues(HKEY Key, TArray<FString> &OutNames)
{
	for (DWORD Index = 0;; Index++)
	{
		// Query the value
		wchar_t ValueName[256];
		DWORD ValueNameLength = sizeof(ValueName) / sizeof(ValueName[0]);

		const LONG Result = RegEnumValue(Key, Index, ValueName, &ValueNameLength, nullptr, nullptr, nullptr, nullptr);
		if (Result == ERROR_NO_MORE_ITEMS)
		{
			break;
		}
		
		if (Result != ERROR_SUCCESS)
		{
			return false;
		}

		// Add it to the array
		OutNames.Add(ValueName);
	}
	return true;
}

static LONG GetStringRegKey(const HKEY Key, const FString& ValueName, FString& Value)
{
	WCHAR Buffer[512];
	DWORD BufferSize = sizeof(Buffer);
	const ULONG Result = RegQueryValueExW(Key, *ValueName, nullptr, nullptr, reinterpret_cast<LPBYTE>(Buffer), &BufferSize);
	if (Result == ERROR_SUCCESS)
	{
		Value = Buffer;
	}
	return Result;
}

static TArray<FInstallInfo> CollectPathsFromRegistry( const Windows::HKEY RootKey, const FString& RegistryKey)
{
	TArray<FInstallInfo> InstallInfos;
	HKEY Key;
	const LONG Result = RegOpenKeyEx(RootKey, *RegistryKey, 0, KEY_READ, &Key);
	if (Result == ERROR_SUCCESS)
	{
		TArray<FString> Keys;
		if (EnumerateRegistryKeys(Key, Keys))
		{
			for (const FString& key : Keys)
			{
				if (!key.Contains(TEXT("Rider"))) continue;

				HKEY SubKey;
				const LONG SubResult = RegOpenKeyEx(Key, *key, 0, KEY_READ, &SubKey);
				if (SubResult != ERROR_SUCCESS) continue;

				TArray<FString> Values;
				if (!EnumerateRegistryValues(SubKey, Values)) continue;

				for (const auto& Value : Values)
				{
					if (Value != TEXT("InstallLocation")) continue;
					FString InstallLocation;
					if (GetStringRegKey(SubKey, Value, InstallLocation) != ERROR_SUCCESS) continue;
					const FString ExePath = FPaths::Combine(InstallLocation, TEXT("bin"), TEXT("rider64.exe"));
					const FString RiderCppPluginPath = FPaths::Combine(InstallLocation, TEXT("plugins"), TEXT("rider-cpp"));
					if (FPaths::FileExists(ExePath) && FPaths::DirectoryExists(RiderCppPluginPath))
					{
						FInstallInfo Info;
						Info.IsToolbox = false;
						Info.Path = ExePath;
						GetStringRegKey(SubKey, TEXT("DisplayVersion"), Info.Version);
						InstallInfos.Add(Info);
					}
				}

			}
		}
	}

	return InstallInfos;
}

TSet<FInstallInfo> FRiderPathLocator::CollectAllPaths()
{
	TSet<FInstallInfo> InstallInfos;
	InstallInfos.Append(CollectPathsFromToolbox(HKEY_CURRENT_USER));
	InstallInfos.Append(CollectPathsFromToolbox(HKEY_LOCAL_MACHINE));
	InstallInfos.Append(CollectPathsFromRegistry(HKEY_CURRENT_USER, TEXT("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall")));
	InstallInfos.Append(CollectPathsFromRegistry(HKEY_LOCAL_MACHINE, TEXT("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall")));
	InstallInfos.Append(CollectPathsFromRegistry(HKEY_CURRENT_USER, TEXT("SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall")));
	InstallInfos.Append(CollectPathsFromRegistry(HKEY_LOCAL_MACHINE, TEXT("SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall")));
	return InstallInfos;
}
#endif
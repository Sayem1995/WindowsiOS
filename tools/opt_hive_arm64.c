/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Freestanding ARM64 Windows PE tool (`opt_hive_arm64.exe`) for offline WinPE
 * registry surgery using `REG_OPTION_BACKUP_RESTORE` (bypasses TrustedInstaller
 * Registry DACLs cleanly on offline SYSTEM/SOFTWARE hives).
 */
#include <stdint.h>
#include <stddef.h>

typedef void *HANDLE;
typedef void *HKEY;
typedef uint16_t WCHAR;
typedef uint32_t DWORD;
typedef int32_t BOOL;
typedef int32_t LONG;

#define HKEY_LOCAL_MACHINE ((HKEY)(uintptr_t)0x80000002ULL)
#define REG_OPTION_BACKUP_RESTORE 0x00000004U
#define KEY_SET_VALUE 0x0002U
#define REG_DWORD 4U
#define REG_MULTI_SZ 7U
#define TOKEN_ADJUST_PRIVILEGES 0x0020U
#define TOKEN_QUERY 0x0008U
#define SE_PRIVILEGE_ENABLED 0x00000002U
#define STD_OUTPUT_HANDLE ((DWORD)-11)

typedef struct LUID {
    DWORD LowPart;
    int32_t HighPart;
} LUID;

typedef struct LUID_AND_ATTRIBUTES {
    LUID Luid;
    DWORD Attributes;
} LUID_AND_ATTRIBUTES;

typedef struct TOKEN_PRIVILEGES {
    DWORD PrivilegeCount;
    LUID_AND_ATTRIBUTES Privileges[1];
} TOKEN_PRIVILEGES;

__declspec(dllimport) HANDLE GetCurrentProcess(void);
__declspec(dllimport) HANDLE GetStdHandle(DWORD nStdHandle);
__declspec(dllimport) BOOL WriteFile(HANDLE hFile, const void *lpBuffer, DWORD nNumberOfBytesToWrite,
                                     DWORD *lpNumberOfBytesWritten, void *lpOverlapped);
__declspec(dllimport) void ExitProcess(DWORD uExitCode);

__declspec(dllimport) BOOL OpenProcessToken(HANDLE ProcessHandle, DWORD DesiredAccess, HANDLE *TokenHandle);
__declspec(dllimport) BOOL LookupPrivilegeValueW(const WCHAR *lpSystemName, const WCHAR *lpName, LUID *lpLuid);
__declspec(dllimport) BOOL AdjustTokenPrivileges(HANDLE TokenHandle, BOOL DisableAllPrivileges,
                                                 const TOKEN_PRIVILEGES *NewState, DWORD BufferLength,
                                                 void *PreviousState, DWORD *ReturnLength);
__declspec(dllimport) LONG RegOpenKeyExW(HKEY hKey, const WCHAR *lpSubKey, DWORD ulOptions,
                                         DWORD samDesired, HKEY *phkResult);
__declspec(dllimport) LONG RegCreateKeyExW(HKEY hKey, const WCHAR *lpSubKey, DWORD Reserved,
                                           WCHAR *lpClass, DWORD dwOptions, DWORD samDesired,
                                           void *lpSecurityAttributes, HKEY *phkResult, DWORD *lpdwDisposition);
__declspec(dllimport) LONG RegSetValueExW(HKEY hKey, const WCHAR *lpValueName, DWORD Reserved,
                                          DWORD dwType, const void *lpData, DWORD cbData);
__declspec(dllimport) LONG RegCloseKey(HKEY hKey);

static void puts_ascii(const char *s) {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD len = 0, written = 0;
    while (s[len]) len++;
    if (out && len) WriteFile(out, s, len, &written, NULL);
}

static void enable_priv(const WCHAR *name) {
    HANDLE tok = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tok)) return;
    TOKEN_PRIVILEGES tp;
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    if (LookupPrivilegeValueW(NULL, name, &tp.Privileges[0].Luid)) {
        AdjustTokenPrivileges(tok, 0, &tp, sizeof(tp), NULL, NULL);
    }
}

static int set_dword(const WCHAR *subkey, const WCHAR *valname, DWORD val, int create_if_missing) {
    HKEY hk = NULL;
    LONG rc;
    if (create_if_missing) {
        rc = RegCreateKeyExW(HKEY_LOCAL_MACHINE, subkey, 0, NULL,
                             REG_OPTION_BACKUP_RESTORE, KEY_SET_VALUE, NULL, &hk, NULL);
    } else {
        rc = RegOpenKeyExW(HKEY_LOCAL_MACHINE, subkey, REG_OPTION_BACKUP_RESTORE, KEY_SET_VALUE, &hk);
    }
    if (rc != 0 || !hk) return -1;
    rc = RegSetValueExW(hk, valname, 0, REG_DWORD, &val, sizeof(val));
    RegCloseKey(hk);
    return rc == 0 ? 0 : -1;
}

typedef struct SvcDisable {
    const WCHAR *subkey;
    const char *label;
} SvcDisable;

static const SvcDisable kDisabledServices[] = {
    {u"OFFSYS\\ControlSet001\\Services\\WinDefend", "WinDefend"},
    {u"OFFSYS\\ControlSet001\\Services\\WdFilter", "WdFilter"},
    {u"OFFSYS\\ControlSet001\\Services\\WdBoot", "WdBoot"},
    {u"OFFSYS\\ControlSet001\\Services\\WdNisSvc", "WdNisSvc"},
    {u"OFFSYS\\ControlSet001\\Services\\WdNisDrv", "WdNisDrv"},
    {u"OFFSYS\\ControlSet001\\Services\\Sense", "Sense"},
    {u"OFFSYS\\ControlSet001\\Services\\SecurityHealthService", "SecurityHealthService"},
    {u"OFFSYS\\ControlSet001\\Services\\wscsvc", "wscsvc"},
    {u"OFFSYS\\ControlSet001\\Services\\MDCoreSvc", "MDCoreSvc"},
    {u"OFFSYS\\ControlSet001\\Services\\MsSecCore", "MsSecCore"},
    {u"OFFSYS\\ControlSet001\\Services\\MsSecFlt", "MsSecFlt"},
    {u"OFFSYS\\ControlSet001\\Services\\webthreatdefsvc", "webthreatdefsvc"},
    {u"OFFSYS\\ControlSet001\\Services\\webthreatdefusersvc", "webthreatdefusersvc"},
    {u"OFFSYS\\ControlSet001\\Services\\SysMain", "SysMain"},
    {u"OFFSYS\\ControlSet001\\Services\\WSearch", "WSearch"},
    {u"OFFSYS\\ControlSet001\\Services\\DiagTrack", "DiagTrack"},
    {u"OFFSYS\\ControlSet001\\Services\\dmwappushservice", "dmwappushservice"},
    {u"OFFSYS\\ControlSet001\\Services\\WerSvc", "WerSvc"},
    {u"OFFSYS\\ControlSet001\\Services\\PcaSvc", "PcaSvc"},
    {u"OFFSYS\\ControlSet001\\Services\\DPS", "DPS"},
    {u"OFFSYS\\ControlSet001\\Services\\WdiServiceHost", "WdiServiceHost"},
    {u"OFFSYS\\ControlSet001\\Services\\WdiSystemHost", "WdiSystemHost"},
    {u"OFFSYS\\ControlSet001\\Services\\MapsBroker", "MapsBroker"},
    {u"OFFSYS\\ControlSet001\\Services\\lfsvc", "lfsvc"},
    {u"OFFSYS\\ControlSet001\\Services\\wisvc", "wisvc"},
    {u"OFFSYS\\ControlSet001\\Services\\RetailDemo", "RetailDemo"},
    {u"OFFSYS\\ControlSet001\\Services\\WbioSrvc", "WbioSrvc"},
    {u"OFFSYS\\ControlSet001\\Services\\wuauserv", "wuauserv"},
    {u"OFFSYS\\ControlSet001\\Services\\UsoSvc", "UsoSvc"},
    {u"OFFSYS\\ControlSet001\\Services\\WaaSMedicSvc", "WaaSMedicSvc"},
    {u"OFFSYS\\ControlSet001\\Services\\DoSvc", "DoSvc"},
    {u"OFFSYS\\ControlSet001\\Services\\XblAuthManager", "XblAuthManager"},
    {u"OFFSYS\\ControlSet001\\Services\\XblGameSave", "XblGameSave"},
    {u"OFFSYS\\ControlSet001\\Services\\XboxNetApiSvc", "XboxNetApiSvc"},
    {u"OFFSYS\\ControlSet001\\Services\\XboxGipSvc", "XboxGipSvc"},
};

void mainCRTStartup(void) {
    enable_priv(u"SeBackupPrivilege");
    enable_priv(u"SeRestorePrivilege");
    enable_priv(u"SeTakeOwnershipPrivilege");

    puts_ascii("[opt_hive] Applying SeRestorePrivilege offline SYSTEM hive tuning...\r\n");

    const WCHAR *mm = u"OFFSYS\\ControlSet001\\Control\\Session Manager\\Memory Management";
    set_dword(mm, u"FeatureSettingsOverride", 3, 1);
    set_dword(mm, u"FeatureSettingsOverrideMask", 3, 1);
    set_dword(mm, u"DisablePagingExecutive", 1, 1);

    /* Compact 512 MB initial / 2048 MB max pagefile instead of multi-GB auto pagefile */
    static const WCHAR kPageFile[] = u"c:\\pagefile.sys 512 2048\0";
    HKEY hk = NULL;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, mm, 0, NULL, REG_OPTION_BACKUP_RESTORE,
                        KEY_SET_VALUE, NULL, &hk, NULL) == 0 && hk) {
        RegSetValueExW(hk, u"PagingFiles", 0, REG_MULTI_SZ, kPageFile, sizeof(kPageFile));
        RegCloseKey(hk);
    }

    set_dword(u"OFFSYS\\ControlSet001\\Control\\PriorityControl", u"Win32PrioritySeparation", 0x26, 1);
    set_dword(u"OFFSYS\\ControlSet001\\Control\\Power", u"HibernateEnabled", 0, 1);
    set_dword(u"OFFSYS\\ControlSet001\\Control\\Power", u"HibernateEnabledDefault", 0, 1);
    /* Disable Smart App Control / CI background cloud verification overhead */
    set_dword(u"OFFSYS\\ControlSet001\\Control\\CI\\Policy", u"VerifiedAndReputablePolicyState", 0, 1);

    int ok_count = 0;
    for (size_t i = 0; i < sizeof(kDisabledServices) / sizeof(kDisabledServices[0]); i++) {
        if (set_dword(kDisabledServices[i].subkey, u"Start", 4, 0) == 0) {
            puts_ascii("  [OK] Disabled service: ");
            puts_ascii(kDisabledServices[i].label);
            puts_ascii("\r\n");
            ok_count++;
        }
    }

    /* Also disable Defender TamperProtection & Real-Time Protection in OFFSYS\OFFSOFT if mounted */
    set_dword(u"OFFSOFT\\Policies\\Microsoft\\Windows Defender", u"DisableAntiSpyware", 1, 1);
    set_dword(u"OFFSOFT\\Policies\\Microsoft\\Windows Defender", u"DisableAntiVirus", 1, 1);
    set_dword(u"OFFSOFT\\Policies\\Microsoft\\Windows Defender\\Real-Time Protection", u"DisableRealtimeMonitoring", 1, 1);
    set_dword(u"OFFSOFT\\Policies\\Microsoft\\Windows Defender\\Real-Time Protection", u"DisableBehaviorMonitoring", 1, 1);
    set_dword(u"OFFSOFT\\Policies\\Microsoft\\Windows Defender\\Real-Time Protection", u"DisableOnAccessProtection", 1, 1);
    set_dword(u"OFFSOFT\\Policies\\Microsoft\\Windows Defender\\Real-Time Protection", u"DisableScanOnRealtimeEnable", 1, 1);
    set_dword(u"OFFSOFT\\Microsoft\\Windows Defender\\Features", u"TamperProtection", 0, 1);

    puts_ascii("[opt_hive] Completed offline SeRestorePrivilege surgery!\r\n");
    ExitProcess(ok_count > 0 ? 0 : 2);
}

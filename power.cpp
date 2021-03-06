#include "power.h"
#include <powrprof.h>

using namespace std;

// https://docs.microsoft.com/en-us/windows-hardware/customize/power-settings/power-button-and-lid-settings-lid-switch-close-action
// https://docs.microsoft.com/en-us/windows/win32/power/power-setting-guids
// https://docs.microsoft.com/en-us/windows/win32/power/power-management-functions

DWORD writeLidCloseActionIndexDC(DWORD index) {
    GUID *curPowerScheme = NULL;
    DWORD ret = PowerGetActiveScheme(NULL, &curPowerScheme);
    if (ret != ERROR_SUCCESS) {
        return ret;
    }
    ret = PowerWriteDCValueIndex(NULL, curPowerScheme,
                                 &GUID_SYSTEM_BUTTON_SUBGROUP, &GUID_LIDCLOSE_ACTION,
                                 index);
    if (ret != ERROR_SUCCESS) {
        return ret;
    }
    return PowerSetActiveScheme(NULL, curPowerScheme);
}

DWORD writeLidCloseActionIndexAC(DWORD index) {
    GUID *curPowerScheme = NULL;
    DWORD ret = PowerGetActiveScheme(NULL, &curPowerScheme);
    if (ret != ERROR_SUCCESS) {
        return ret;
    }
    ret = PowerWriteACValueIndex(NULL, curPowerScheme,
                                 &GUID_SYSTEM_BUTTON_SUBGROUP, &GUID_LIDCLOSE_ACTION,
                                 index);
    if (ret != ERROR_SUCCESS) {
        return ret;
    }
    return PowerSetActiveScheme(NULL, curPowerScheme);
}

DWORD readLidCloseActionIndexDC(DWORD *index) {
    GUID *curPowerScheme = NULL;
    DWORD ret = PowerGetActiveScheme(NULL, &curPowerScheme);
    if (ret != ERROR_SUCCESS) {
        return ret;
    }
    return PowerReadDCValueIndex(NULL, curPowerScheme,
                                 &GUID_SYSTEM_BUTTON_SUBGROUP, &GUID_LIDCLOSE_ACTION,
                                 index);
}

DWORD readLidCloseActionIndexAC(DWORD *index) {
    GUID *curPowerScheme = NULL;
    DWORD ret = PowerGetActiveScheme(NULL, &curPowerScheme);
    if (ret != ERROR_SUCCESS) {
        return ret;
    }
    return PowerReadACValueIndex(NULL, curPowerScheme,
                                 &GUID_SYSTEM_BUTTON_SUBGROUP, &GUID_LIDCLOSE_ACTION,
                                 index);
}

#include "power.h"
#include <powrprof.h>

using namespace std;

// https://docs.microsoft.com/en-us/windows-hardware/customize/power-settings/power-button-and-lid-settings-lid-switch-close-action
// https://docs.microsoft.com/en-us/windows/win32/power/power-setting-guids
// https://docs.microsoft.com/en-us/windows/win32/power/power-management-functions

DWORD writeLidCloseActionIndex_Battery(DWORD index) {
    GUID *curPowerScheme = NULL;
    DWORD ret = PowerGetActiveScheme(NULL, &curPowerScheme);
    if (ret != ERROR_SUCCESS) {
        return ret;
    }
    return PowerWriteDCValueIndex(NULL, curPowerScheme,
                                  &GUID_SYSTEM_BUTTON_SUBGROUP, &GUID_LIDCLOSE_ACTION,
                                  index);
}

DWORD writeLidCloseActionIndex_PluggedIn(DWORD index) {
    GUID *curPowerScheme = NULL;
    DWORD ret = PowerGetActiveScheme(NULL, &curPowerScheme);
    if (ret != ERROR_SUCCESS) {
        return ret;
    }
    return PowerWriteACValueIndex(NULL, curPowerScheme,
                                  &GUID_SYSTEM_BUTTON_SUBGROUP, &GUID_LIDCLOSE_ACTION,
                                  index);
}

DWORD readLidCloseActionIndex_Battery(DWORD *index) {
    GUID *curPowerScheme = NULL;
    DWORD ret = PowerGetActiveScheme(NULL, &curPowerScheme);
    if (ret != ERROR_SUCCESS) {
        return ret;
    }
    return PowerReadDCValueIndex(NULL, curPowerScheme,
                                 &GUID_SYSTEM_BUTTON_SUBGROUP, &GUID_LIDCLOSE_ACTION,
                                 index);
}

DWORD readLidCloseActionIndex_PluggedIn(DWORD *index) {
    GUID *curPowerScheme = NULL;
    DWORD ret = PowerGetActiveScheme(NULL, &curPowerScheme);
    if (ret != ERROR_SUCCESS) {
        return ret;
    }
    return PowerReadACValueIndex(NULL, curPowerScheme,
                                 &GUID_SYSTEM_BUTTON_SUBGROUP, &GUID_LIDCLOSE_ACTION,
                                 index);
}

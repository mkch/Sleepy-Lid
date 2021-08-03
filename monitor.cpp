#include <windows.h>
#include <devguid.h>
#include <dbt.h>
#include "monitor.h"

using namespace std;

BOOL RegisterMonitorNotification(HWND hwnd) {
    DEV_BROADCAST_DEVICEINTERFACE NotificationFilter = {0};

    NotificationFilter.dbcc_size = sizeof(DEV_BROADCAST_DEVICEINTERFACE);
    NotificationFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    NotificationFilter.dbcc_classguid = GUID_DEVCLASS_MONITOR;

    return RegisterDeviceNotification(
               hwnd,                        // events recipient
               &NotificationFilter,         // type of device
               DEVICE_NOTIFY_WINDOW_HANDLE  // type of recipient handle
               ) != NULL;
}

// https://stackoverflow.com/questions/4958683/how-do-i-get-the-actual-monitor-name-as-seen-in-the-resolution-dialog
LONG connectedMonitors(vector<wstring>& names) {
    const UINT32 MAX_COUNT = 0xFF;
    UINT32 pathCount = MAX_COUNT;
    UINT32 modeCount = MAX_COUNT;

    DISPLAYCONFIG_PATH_INFO pathes[MAX_COUNT];
    DISPLAYCONFIG_MODE_INFO modes[MAX_COUNT];

    LONG ret = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, pathes, &modeCount, modes, NULL);
    if (ret != ERROR_SUCCESS) {
        return ret;
    }

    for (UINT32 i = 0; i < modeCount; i++) {
        if (modes[i].infoType != DISPLAYCONFIG_MODE_INFO_TYPE_TARGET) {
            continue;
        }

        DISPLAYCONFIG_TARGET_DEVICE_NAME deviceName;
        deviceName.header.size = sizeof deviceName;
        deviceName.header.adapterId = modes[i].adapterId;
        deviceName.header.id = modes[i].id;
        deviceName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        ret = DisplayConfigGetDeviceInfo(&deviceName.header);
        if (ret != ERROR_SUCCESS) {
            return ret;
        }
        names.push_back(deviceName.monitorFriendlyDeviceName);
    }
    return ERROR_SUCCESS;
}

LONG isExternalMonitorsConnected(bool* connected) {
    const UINT32 MAX_COUNT = 0xFF;

    DISPLAYCONFIG_PATH_INFO pathes[MAX_COUNT];
    DISPLAYCONFIG_MODE_INFO modes[MAX_COUNT];
    UINT32 Pathcount = sizeof(pathes) / sizeof(pathes[0]);
    UINT32 modeCount = sizeof(modes) / sizeof(modes[0]);

    DISPLAYCONFIG_TOPOLOGY_ID id = DISPLAYCONFIG_TOPOLOGY_INTERNAL;
    LONG ret = QueryDisplayConfig(QDC_DATABASE_CURRENT, &Pathcount, pathes, &modeCount, modes, &id);
    if (ret == ERROR_SUCCESS) {
        *connected = id != DISPLAYCONFIG_TOPOLOGY_INTERNAL;
    }
    return ret;
}
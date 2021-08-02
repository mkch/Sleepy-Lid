#include <windef.h>
#include <string>
#include <vector>
// Get the names of connected displays.
LONG connectedMonitors(std::vector<std::wstring> &names);
// Register the window to receive WM_DEVICE_CHANGED of display devices.
BOOL RegisterMonitorNotification(HWND hwnd);
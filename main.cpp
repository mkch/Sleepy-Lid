#include <windows.h>
#include <Dbt.h>
#include <commctrl.h>
#include <strsafe.h>
#include <shellapi.h>

#include <algorithm>
#include <string>
#include <vector>
#include <set>
#include <array>
#include <map>

#include "monitor.h"
#include "power.h"
#include "res.h"

using namespace std;

static HINSTANCE hInstance = NULL;
// Silent mode: do not show notification on start.
static bool silentMode = false;

// ID of Shell_NotifyIconW.
static const UINT NOTIFY_ID = 1;
// Notify message used by Shell_NotifyIconW.
static const UINT UM_NOTIFY = WM_USER + 1;

// The extension of config file.
static const auto CONFIG_FILE_NAME = L"SleepyLid.ini";

// The arv[0]. GetModuleFileNameW(0).
wstring moduleFilePath;
// The path of config file. Initialized in entry point.
wstring configFilePath;
// The command written to registry to enable-start-on-boot.
wstring startOnBootCmd;

static const auto START_ON_BOOT_REG_SUB_KEY =
    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";
// The registry key under
static const auto START_ON_BOOT_REG_VALUE_NAME = L"SleepyLid";

int _main(HINSTANCE hInstance, int argc, wchar_t *argv[], int nCmdShow);
void showNotification(HWND hwnd, bool silent = false);
void removeNotification(HWND hwnd);
void applyDisplayConnectivity();
void showError(const wchar_t *msg);
void showError(const wchar_t *msg, const wchar_t *file, int line);
void showError(DWORD lastError, const wchar_t *file, int line);

#define _WT(str) L""##str
// Should be:
// #define W__FILE__ L""##__FILE__
// But the linter of VSCode does not like it(bug?).
#define W__FILE__ _WT(__FILE__)

#define SHOW_ERROR(err) showError(err, W__FILE__, __LINE__)
#define SHOW_LAST_ERROR() SHOW_ERROR(GetLastError())

#ifdef WINDOWS
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    int argc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    return _main(hInstance, argc, argv, nCmdShow);
}
#else
int wmain(int argc, wchar_t *argv[]) {
    return _main(GetModuleHandle(NULL), argc, argv, SW_SHOW);
}
#endif

class monitorActions {
private:
    typedef std::array<unsigned char, 4> arrayType;
    arrayType actions;

public:
    monitorActions() {}
    bool set(const wstring str) {
        if (str.length() != actions.size()) {
            return false;
        }
        arrayType temp;
        for (int i = 0; i < actions.size(); i++) {
            const int n = str[i] - L'0';
            if (n < INDEX_DO_NOTHING || n > INDEX_SHUT_DOWN) {
                return false;
            }
            temp[i] = (char)n;
        }
        actions = temp;
        return true;
    }

    wstring toString() {
        wstring ret(actions.size(), L'0');
        std::transform(actions.begin(), actions.end(), ret.begin(), [](auto a) { return L'0' + a; });
        return ret;
    }

    int connectedDC() {
        return actions[0];
    }
    void setConnectedDC(int index) {
        if (index < INDEX_DO_NOTHING || index > INDEX_SHUT_DOWN)
            return;
        actions[0] = index;
    }

    int connectedAC() {
        return actions[1];
    }
    void setConnectedAC(int index) {
        if (index < INDEX_DO_NOTHING || index > INDEX_SHUT_DOWN)
            return;
        actions[1] = index;
    }

    int disconnectedDC() {
        return actions[2];
    }
    void setDisconnectedDC(int index) {
        if (index < INDEX_DO_NOTHING || index > INDEX_SHUT_DOWN)
            return;
        actions[2] = index;
    }

    int disconnectedAC() {
        return actions[3];
    }
    void setDisconnectedAC(int index) {
        if (index < INDEX_DO_NOTHING || index > INDEX_SHUT_DOWN)
            return;
        actions[3] = index;
    }
};

static map<UINT, wstring> stringResMap;
const wstring &loadStringRes(UINT resId) {
    const auto it = stringResMap.find(resId);
    if (it != stringResMap.end()) {
        return it->second;
    }
    wchar_t *buf = NULL;
    const int n = LoadStringW(hInstance, resId, (LPWSTR)&buf, 0);
    if (n == 0) {
        SHOW_LAST_ERROR();
        std::exit(1);
    }
    stringResMap[resId] = wstring(buf, n);
    return stringResMap[resId];
}

static bool syncMonitor = false;
static monitorActions actions;

// ini section name.
static const auto CONFIG_LID_CLOSING = L"LidClosing";
// ini key.
static const auto CONFIG_SYNC_MONITOR = L"SyncMonitor";
static const auto CONFIG_MONITOR_POWER_ACTIONS = L"MonitorActions";

// Read settings from config file.
void readConfig() {
    syncMonitor = GetPrivateProfileIntW(CONFIG_LID_CLOSING, CONFIG_SYNC_MONITOR, 0, configFilePath.c_str()) != 0;
    std::array<wchar_t, 5> buf;
    if (GetPrivateProfileStringW(CONFIG_LID_CLOSING, CONFIG_MONITOR_POWER_ACTIONS, L"0000",
                                 buf.data(), buf.size(),
                                 configFilePath.c_str()) == buf.size() - 1) {
        actions.set(buf.data());
    }
}

// Write settings to config file.
void writeConfig() {
    WritePrivateProfileStringW(CONFIG_LID_CLOSING, CONFIG_SYNC_MONITOR,
                               syncMonitor ? L"1" : L"0",
                               configFilePath.c_str());
    const wstring str = actions.toString();
    WritePrivateProfileStringW(CONFIG_LID_CLOSING, CONFIG_MONITOR_POWER_ACTIONS,
                               str.c_str(),
                               configFilePath.c_str());
}

void showError(const wchar_t *msg) {
    MessageBoxW(NULL, msg, loadStringRes(STR_APP_NAME).c_str(), MB_ICONERROR);
}

void showError(const wchar_t *msg, const wchar_t *file, int line) {
    wchar_t message[1024] = {0};
    StringCbPrintfW(message, sizeof message, L"%s:%d\n%s", file, line, msg);
    showError(message);
}

// Show a message box with the error description of lastError.
void showError(DWORD lastError, const wchar_t *file, int line) {
    wchar_t *msg = NULL;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, lastError, 0, (LPWSTR)&msg, 0, NULL);
    showError(msg, file, line);
    LocalFree(msg);
}

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static bool alreadyRunning();

int _main(HINSTANCE instanceHandle, int argc, wchar_t *argv[], int nCmdShow) {
    if (alreadyRunning()) {
        MessageBoxW(NULL, loadStringRes(STR_ALREADY_RUNNING).c_str(), loadStringRes(STR_APP_NAME).c_str(), MB_ICONERROR);
        return 1;
    }
    if (argc > 1) {
        const auto argv1 = wstring(argv[1]);
        silentMode = (argv1 == L"/silent" || argv1 == L"-silent");
    }
    hInstance = instanceHandle;

    // Initialize configFilePath.
    moduleFilePath = argv[0];
    configFilePath = moduleFilePath;
    const auto sep = configFilePath.rfind(L'\\');
    if (sep != string::npos) {
        configFilePath = configFilePath.substr(0, sep + 1) + CONFIG_FILE_NAME;
    }
    startOnBootCmd = wstring(L"\"") + moduleFilePath + L"\" /silent";

    readConfig();

    applyDisplayConnectivity();

    const wchar_t *classsName = L"MainWindow";
    WNDCLASSEXW cls = {0};
    cls.cbSize = sizeof cls;
    cls.hInstance = hInstance;
    cls.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(ICON_MAIN));
    cls.lpszClassName = classsName;
    cls.lpfnWndProc = MainWndProc;
    auto clsAtom = RegisterClassExW(&cls);
    if (clsAtom == NULL) {
        SHOW_LAST_ERROR();
        return 1;
    }

    auto hwnd = CreateWindowW(classsName, L"Main Window", WS_OVERLAPPEDWINDOW,
                              CW_USEDEFAULT, CW_USEDEFAULT, 150, 150,
                              NULL, NULL,
                              hInstance, NULL);
    if (hwnd == NULL) {
        SHOW_LAST_ERROR();
        return 1;
    }

    if (!RegisterMonitorNotification(hwnd)) {
        SHOW_LAST_ERROR();
        return 1;
    }

    // ShowWindow(hwnd, nCmdShow);
    // UpdateWindow(hwnd);

    MSG msg;
    BOOL fGotMessage;
    while ((fGotMessage = GetMessage(&msg, (HWND)NULL, 0, 0)) != 0 &&
           fGotMessage != -1) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return msg.wParam;
}

static const auto RUNNING_MUTEX_NAME = L"Sleepy Lid is running";
static bool alreadyRunning() {
    if (CreateMutexW(NULL, FALSE, RUNNING_MUTEX_NAME) == NULL) {
        SHOW_LAST_ERROR();
        std::exit(1);
    }
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

// Timer id for delaying the WM_DEVICECHANGE message processing.
static const UINT_PTR DELAY_DEVICE_CHANGE_TIMER = 1;
// Batch interval of  WM_DEVICECHANGE message processing.
static const UINT DEVICE_CHANGE_DELAY = 3000;

// Set power action based on the current display connectivity.
void applyDisplayConnectivity() {
    if (!syncMonitor)
        return;
    // Last state of external monitors.
    static int lastConnected = -1;
    bool connected = false;
    auto ret = isExternalMonitorsConnected(&connected);
    if (ret != ERROR_SUCCESS)
        goto handle_error;
    const bool unchanged = connected == (lastConnected == 1);
    lastConnected = connected ? 1 : 0;
    // External monitors state was not changed.
    if (unchanged) {
        return;
    }
    if (connected) {
        ret = writeLidCloseActionIndexDC(actions.connectedDC());
        if (ret != ERROR_SUCCESS)
            goto handle_error;

        ret = writeLidCloseActionIndexAC(actions.connectedAC());
        if (ret != ERROR_SUCCESS)
            goto handle_error;
    } else {
        ret = writeLidCloseActionIndexDC(actions.disconnectedDC());
        if (ret != ERROR_SUCCESS)
            goto handle_error;

        ret = writeLidCloseActionIndexAC(actions.disconnectedAC());
        if (ret != ERROR_SUCCESS)
            goto handle_error;
    }
    return;

handle_error:
    SHOW_ERROR(ret);
    exit(1);
}

void CALLBACK DelayDeviceChangeTimerProc(HWND hwnd, UINT msg, UINT_PTR id, DWORD time) {
    KillTimer(hwnd, DELAY_DEVICE_CHANGE_TIMER);  // Make it one time timer.
    applyDisplayConnectivity();
}

// Menu item IDs.
enum {
    ID_EXIT = 1,
    ID_AUTO_RUN,

    ID_DC_DO_NOTHING,
    ID_DC_SLEEP,
    ID_DC_HIBERNATE,
    ID_DC_SHUT_DOWN,

    ID_AC_DO_NOTHING,
    ID_AC_SLEEP,
    ID_AC_HIBERNATE,
    ID_AC_SHUT_DOWN,

    ID_SYNC_MONITOR,

    ID_MONITOR_CONNECTED_DC_DO_NOTHING,
    ID_MONITOR_CONNECTED_DC_SLEEP,
    ID_MONITOR_CONNECTED_DC_HIBERNATE,
    ID_MONITOR_CONNECTED_DC_SHUT_DOWN,

    ID_MONITOR_CONNECTED_AC_DO_NOTHING,
    ID_MONITOR_CONNECTED_AC_SLEEP,
    ID_MONITOR_CONNECTED_AC_HIBERNATE,
    ID_MONITOR_CONNECTED_AC_SHUT_DOWN,

    ID_MONITOR_DISCONNECTED_DC_DO_NOTHING,
    ID_MONITOR_DISCONNECTED_DC_SLEEP,
    ID_MONITOR_DISCONNECTED_DC_HIBERNATE,
    ID_MONITOR_DISCONNECTED_DC_SHUT_DOWN,

    ID_MONITOR_DISCONNECTED_AC_DO_NOTHING,
    ID_MONITOR_DISCONNECTED_AC_SLEEP,
    ID_MONITOR_DISCONNECTED_AC_HIBERNATE,
    ID_MONITOR_DISCONNECTED_AC_SHUT_DOWN,
};

static const wstring &powerActionToString(DWORD index) {
    switch (index) {
    case INDEX_DO_NOTHING:
        return loadStringRes(STR_DO_NOTHING);
    case INDEX_SLEEP:
        return loadStringRes(STR_SLEEP);
    case INDEX_HIBERNATE:
        return loadStringRes(STR_HIBERNATE);
    case INDEX_SHUT_DOWN:
        return loadStringRes(STR_SHUT_DOWN);
    default:
        return loadStringRes(STR_UNKNOWN);
    }
}

bool startOnBootEnabled() {
    wchar_t value[1024] = {0};
    DWORD read = sizeof(value);
    const auto ret = RegGetValueW(HKEY_CURRENT_USER, START_ON_BOOT_REG_SUB_KEY, START_ON_BOOT_REG_VALUE_NAME,
                                  RRF_RT_REG_SZ, NULL,
                                  value, &read);
    if (ret != ERROR_SUCCESS) {
        return false;
    } else {
        auto equal =
            std::equal(startOnBootCmd.begin(), startOnBootCmd.end(),
                       value, value + read / sizeof(wchar_t) - 1,
                       [](auto a, auto b) { return tolower(a) == tolower(b); });
        return equal;
    }
}

void enableStartOnBoot() {
    HKEY key = NULL;
    auto ret = RegOpenKeyExW(HKEY_CURRENT_USER, START_ON_BOOT_REG_SUB_KEY,
                             0, KEY_WRITE,
                             &key);
    if (ret != ERROR_SUCCESS) {
        SHOW_ERROR(ret);
    } else {
        ret = RegSetValueExW(key, START_ON_BOOT_REG_VALUE_NAME,
                             0, REG_SZ,
                             (const BYTE *)startOnBootCmd.c_str(), startOnBootCmd.size() * sizeof(wchar_t));
        if (ret != ERROR_SUCCESS) {
            SHOW_ERROR(ret);
        }
    }
}

void disableStartOnBoot() {
    HKEY key = NULL;
    auto ret = RegOpenKeyExW(HKEY_CURRENT_USER, START_ON_BOOT_REG_SUB_KEY,
                             0, KEY_WRITE,
                             &key);
    if (ret != ERROR_SUCCESS) {
        SHOW_ERROR(ret);
    } else {
        ret = RegDeleteValueW(key, START_ON_BOOT_REG_VALUE_NAME);
        if (ret != ERROR_SUCCESS) {
            SHOW_ERROR(ret);
        }
    }
}

// Creates a popup menu which can be used to show when notification icon is
// clicked.
HMENU createNotifyPopupMenu() {
    DWORD actionBattery = 0;
    DWORD ret = readLidCloseActionIndexDC(&actionBattery);
    if (ret != ERROR_SUCCESS) {
        SHOW_ERROR(ret);
        exit(1);
    }

    DWORD actionPluggedIn = 0;
    ret = readLidCloseActionIndexAC(&actionPluggedIn);
    if (ret != ERROR_SUCCESS) {
        SHOW_ERROR(ret);
        exit(1);
    }

    const HMENU onBattery = CreateMenu();
    AppendMenuW(onBattery,
                MF_STRING | (actionBattery == INDEX_DO_NOTHING ? MF_CHECKED : 0),
                ID_DC_DO_NOTHING, loadStringRes(STR_DO_NOTHING).c_str());
    AppendMenuW(onBattery,
                MF_STRING | (actionBattery == INDEX_SLEEP ? MF_CHECKED : 0),
                ID_DC_SLEEP, loadStringRes(STR_SLEEP).c_str());
    AppendMenuW(onBattery,
                MF_STRING | (actionBattery == INDEX_HIBERNATE ? MF_CHECKED : 0),
                ID_DC_HIBERNATE, loadStringRes(STR_HIBERNATE).c_str());
    AppendMenuW(onBattery,
                MF_STRING | (actionBattery == INDEX_SHUT_DOWN ? MF_CHECKED : 0),
                ID_DC_SHUT_DOWN, loadStringRes(STR_SHUT_DOWN).c_str());

    HMENU pluggedIn = CreateMenu();
    AppendMenuW(pluggedIn,
                MF_STRING | (actionPluggedIn == INDEX_DO_NOTHING ? MF_CHECKED : 0),
                ID_AC_DO_NOTHING, loadStringRes(STR_DO_NOTHING).c_str());
    AppendMenuW(pluggedIn,
                MF_STRING | (actionPluggedIn == INDEX_SLEEP ? MF_CHECKED : 0),
                ID_AC_SLEEP, loadStringRes(STR_SLEEP).c_str());
    AppendMenuW(pluggedIn,
                MF_STRING | (actionPluggedIn == INDEX_HIBERNATE ? MF_CHECKED : 0),
                ID_AC_HIBERNATE, loadStringRes(STR_HIBERNATE).c_str());
    AppendMenuW(pluggedIn,
                MF_STRING | (actionPluggedIn == INDEX_SHUT_DOWN ? MF_CHECKED : 0),
                ID_AC_SHUT_DOWN, loadStringRes(STR_SHUT_DOWN).c_str());

    const HMENU monitorConnectedDC = CreateMenu();
    AppendMenuW(monitorConnectedDC,
                MF_STRING | (actions.connectedDC() == INDEX_DO_NOTHING ? MF_CHECKED : 0),
                ID_MONITOR_CONNECTED_DC_DO_NOTHING, loadStringRes(STR_DO_NOTHING).c_str());
    AppendMenuW(monitorConnectedDC,
                MF_STRING | (actions.connectedDC() == INDEX_SLEEP ? MF_CHECKED : 0),
                ID_MONITOR_CONNECTED_DC_SLEEP, loadStringRes(STR_SLEEP).c_str());
    AppendMenuW(monitorConnectedDC,
                MF_STRING | (actions.connectedDC() == INDEX_HIBERNATE ? MF_CHECKED : 0),
                ID_MONITOR_CONNECTED_DC_HIBERNATE, loadStringRes(STR_HIBERNATE).c_str());
    AppendMenuW(monitorConnectedDC,
                MF_STRING | (actions.connectedDC() == INDEX_SHUT_DOWN ? MF_CHECKED : 0),
                ID_MONITOR_CONNECTED_DC_SHUT_DOWN, loadStringRes(STR_SHUT_DOWN).c_str());

    const HMENU monitorConnectedAC = CreateMenu();
    AppendMenuW(monitorConnectedAC,
                MF_STRING | (actions.connectedAC() == INDEX_DO_NOTHING ? MF_CHECKED : 0),
                ID_MONITOR_CONNECTED_AC_DO_NOTHING, loadStringRes(STR_DO_NOTHING).c_str());
    AppendMenuW(monitorConnectedAC,
                MF_STRING | (actions.connectedAC() == INDEX_SLEEP ? MF_CHECKED : 0),
                ID_MONITOR_CONNECTED_AC_SLEEP, loadStringRes(STR_SLEEP).c_str());
    AppendMenuW(monitorConnectedAC,
                MF_STRING | (actions.connectedAC() == INDEX_HIBERNATE ? MF_CHECKED : 0),
                ID_MONITOR_CONNECTED_AC_HIBERNATE, loadStringRes(STR_HIBERNATE).c_str());
    AppendMenuW(monitorConnectedAC,
                MF_STRING | (actions.connectedAC() == INDEX_SHUT_DOWN ? MF_CHECKED : 0),
                ID_MONITOR_CONNECTED_AC_SHUT_DOWN, loadStringRes(STR_SHUT_DOWN).c_str());

    const HMENU monitorDisconnectedDC = CreateMenu();
    AppendMenuW(monitorDisconnectedDC,
                MF_STRING | (actions.disconnectedDC() == INDEX_DO_NOTHING ? MF_CHECKED : 0),
                ID_MONITOR_DISCONNECTED_DC_DO_NOTHING, loadStringRes(STR_DO_NOTHING).c_str());
    AppendMenuW(monitorDisconnectedDC,
                MF_STRING | (actions.disconnectedDC() == INDEX_SLEEP ? MF_CHECKED : 0),
                ID_MONITOR_DISCONNECTED_DC_SLEEP, loadStringRes(STR_SLEEP).c_str());
    AppendMenuW(monitorDisconnectedDC,
                MF_STRING | (actions.disconnectedDC() == INDEX_HIBERNATE ? MF_CHECKED : 0),
                ID_MONITOR_DISCONNECTED_DC_HIBERNATE, loadStringRes(STR_HIBERNATE).c_str());
    AppendMenuW(monitorDisconnectedDC,
                MF_STRING | (actions.disconnectedDC() == INDEX_SHUT_DOWN ? MF_CHECKED : 0),
                ID_MONITOR_DISCONNECTED_DC_SHUT_DOWN, loadStringRes(STR_SHUT_DOWN).c_str());

    const HMENU monitorDisconnectedAC = CreateMenu();
    AppendMenuW(monitorDisconnectedAC,
                MF_STRING | (actions.disconnectedAC() == INDEX_DO_NOTHING ? MF_CHECKED : 0),
                ID_MONITOR_DISCONNECTED_AC_DO_NOTHING, loadStringRes(STR_DO_NOTHING).c_str());
    AppendMenuW(monitorDisconnectedAC,
                MF_STRING | (actions.disconnectedAC() == INDEX_SLEEP ? MF_CHECKED : 0),
                ID_MONITOR_DISCONNECTED_AC_SLEEP, loadStringRes(STR_SLEEP).c_str());
    AppendMenuW(monitorDisconnectedAC,
                MF_STRING | (actions.disconnectedAC() == INDEX_HIBERNATE ? MF_CHECKED : 0),
                ID_MONITOR_DISCONNECTED_AC_HIBERNATE, loadStringRes(STR_HIBERNATE).c_str());
    AppendMenuW(monitorDisconnectedAC,
                MF_STRING | (actions.disconnectedAC() == INDEX_SHUT_DOWN ? MF_CHECKED : 0),
                ID_MONITOR_DISCONNECTED_AC_SHUT_DOWN, loadStringRes(STR_SHUT_DOWN).c_str());

    std::array<wchar_t, 1024> buf;

    const HMENU monitor = CreateMenu();
    AppendMenuW(monitor, MF_STRING | (syncMonitor ? MF_CHECKED : 0), ID_SYNC_MONITOR, loadStringRes(syncMonitor ? STR_ON : STR_OFF).c_str());
    SetMenuDefaultItem(monitor, 0, TRUE);

    StringCbPrintfW(buf.data(), buf.size() * sizeof(wchar_t),
                    loadStringRes(STR_FMT_MONITOR_CONNECTED_DC).c_str(),
                    powerActionToString(actions.connectedDC()).c_str());
    AppendMenuW(monitor, MF_STRING | MF_POPUP | MF_MENUBARBREAK | (syncMonitor ? 0 : MF_GRAYED), (UINT_PTR)monitorConnectedDC, buf.data());
    StringCbPrintfW(buf.data(), buf.size() * sizeof(wchar_t),
                    loadStringRes(STR_FMT_MONITOR_CONNECTED_AC).c_str(),
                    powerActionToString(actions.connectedAC()).c_str());
    AppendMenuW(monitor, MF_STRING | MF_POPUP | (syncMonitor ? 0 : MF_GRAYED), (UINT_PTR)monitorConnectedAC, buf.data());

    AppendMenuW(monitor, MF_SEPARATOR, 0, NULL);

    StringCbPrintfW(buf.data(), buf.size() * sizeof(wchar_t),
                    loadStringRes(STR_FMT_MONITOR_DISCONNECTED_DC).c_str(),
                    powerActionToString(actions.disconnectedDC()).c_str());
    AppendMenuW(monitor, MF_STRING | MF_POPUP | (syncMonitor ? 0 : MF_GRAYED), (UINT_PTR)monitorDisconnectedDC, buf.data());
    StringCbPrintfW(buf.data(), buf.size() * sizeof(wchar_t),
                    loadStringRes(STR_FMT_MONITOR_DISCONNECTED_AC).c_str(),
                    powerActionToString(actions.disconnectedAC()).c_str());
    AppendMenuW(monitor, MF_STRING | MF_POPUP | (syncMonitor ? 0 : MF_GRAYED), (UINT_PTR)monitorDisconnectedAC, buf.data());

    const HMENU lidClosing = CreateMenu();

    StringCbPrintfW(buf.data(), buf.size() * sizeof(wchar_t),
                    loadStringRes(STR_FMT_ON_BATTERY).c_str(),
                    powerActionToString(actionBattery).c_str());
    AppendMenuW(lidClosing, MF_STRING | MF_POPUP, (UINT_PTR)onBattery, buf.data());
    StringCbPrintfW(buf.data(), buf.size() * sizeof(wchar_t),
                    loadStringRes(STR_FMT_PLUGGED_IN).c_str(),
                    powerActionToString(actionPluggedIn).c_str());
    AppendMenuW(lidClosing, MF_STRING | MF_POPUP, (UINT_PTR)pluggedIn, buf.data());
    AppendMenuW(lidClosing, MF_SEPARATOR, 0, NULL);

    AppendMenuW(lidClosing, MF_STRING | MF_POPUP | (syncMonitor ? MF_CHECKED : 0), (UINT_PTR)monitor, loadStringRes(STR_SYNC_MONITOR).c_str());

    const HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)lidClosing, loadStringRes(STR_WHEN_LID_CLOSING).c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);

    AppendMenuW(menu, MF_STRING | (startOnBootEnabled() ? MF_CHECKED : 0),
                ID_AUTO_RUN, loadStringRes(STR_START_ON_BOOT).c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, ID_EXIT, loadStringRes(STR_EXIT).c_str());
    return menu;
}

void processNotifyMenuCmd(HWND hwnd, UINT_PTR cmd) {
    DWORD ret = ERROR_SUCCESS;
    switch (cmd) {
    case ID_EXIT:
        SendMessage(hwnd, WM_CLOSE, 0, 0);
        break;
    case ID_AUTO_RUN:
        if (startOnBootEnabled()) {
            disableStartOnBoot();
        } else {
            enableStartOnBoot();
        }
        break;
    case ID_DC_DO_NOTHING:
        ret = writeLidCloseActionIndexDC(INDEX_DO_NOTHING);
        break;
    case ID_DC_SLEEP:
        ret = writeLidCloseActionIndexDC(INDEX_SLEEP);
        break;
    case ID_DC_HIBERNATE:
        ret = writeLidCloseActionIndexDC(INDEX_HIBERNATE);
        break;
    case ID_DC_SHUT_DOWN:
        ret = writeLidCloseActionIndexDC(INDEX_SHUT_DOWN);
        break;
    case ID_AC_DO_NOTHING:
        ret = writeLidCloseActionIndexAC(INDEX_DO_NOTHING);
        break;
    case ID_AC_SLEEP:
        ret = writeLidCloseActionIndexAC(INDEX_SLEEP);
        break;
    case ID_AC_HIBERNATE:
        ret = writeLidCloseActionIndexAC(INDEX_HIBERNATE);
        break;
    case ID_AC_SHUT_DOWN:
        ret = writeLidCloseActionIndexAC(INDEX_SHUT_DOWN);
        break;
    case ID_SYNC_MONITOR:
        syncMonitor = !syncMonitor;
        if (syncMonitor) {
            applyDisplayConnectivity();
        }
        writeConfig();
        break;
    case ID_MONITOR_CONNECTED_DC_DO_NOTHING:
        actions.setConnectedDC(INDEX_DO_NOTHING);
        applyDisplayConnectivity();
        writeConfig();
        break;
    case ID_MONITOR_CONNECTED_DC_SLEEP:
        actions.setConnectedDC(INDEX_SLEEP);
        applyDisplayConnectivity();
        writeConfig();
        break;
    case ID_MONITOR_CONNECTED_DC_HIBERNATE:
        actions.setConnectedDC(INDEX_HIBERNATE);
        applyDisplayConnectivity();
        writeConfig();
        break;
    case ID_MONITOR_CONNECTED_DC_SHUT_DOWN:
        actions.setConnectedDC(INDEX_SHUT_DOWN);
        applyDisplayConnectivity();
        writeConfig();
        break;
    case ID_MONITOR_CONNECTED_AC_DO_NOTHING:
        actions.setConnectedAC(INDEX_DO_NOTHING);
        applyDisplayConnectivity();
        writeConfig();
        break;
    case ID_MONITOR_CONNECTED_AC_SLEEP:
        actions.setConnectedAC(INDEX_SLEEP);
        applyDisplayConnectivity();
        writeConfig();
        break;
    case ID_MONITOR_CONNECTED_AC_HIBERNATE:
        actions.setConnectedAC(INDEX_HIBERNATE);
        applyDisplayConnectivity();
        writeConfig();
        break;
    case ID_MONITOR_CONNECTED_AC_SHUT_DOWN:
        actions.setConnectedAC(INDEX_SHUT_DOWN);
        applyDisplayConnectivity();
        writeConfig();
        break;
    case ID_MONITOR_DISCONNECTED_DC_DO_NOTHING:
        actions.setDisconnectedDC(INDEX_DO_NOTHING);
        applyDisplayConnectivity();
        writeConfig();
        break;
    case ID_MONITOR_DISCONNECTED_DC_SLEEP:
        actions.setDisconnectedDC(INDEX_SLEEP);
        applyDisplayConnectivity();
        writeConfig();
        break;
    case ID_MONITOR_DISCONNECTED_DC_HIBERNATE:
        actions.setDisconnectedDC(INDEX_HIBERNATE);
        applyDisplayConnectivity();
        writeConfig();
        break;
    case ID_MONITOR_DISCONNECTED_DC_SHUT_DOWN:
        actions.setDisconnectedDC(INDEX_SHUT_DOWN);
        applyDisplayConnectivity();
        writeConfig();
        break;
    case ID_MONITOR_DISCONNECTED_AC_DO_NOTHING:
        actions.setDisconnectedAC(INDEX_DO_NOTHING);
        applyDisplayConnectivity();
        writeConfig();
        break;
    case ID_MONITOR_DISCONNECTED_AC_SLEEP:
        actions.setDisconnectedAC(INDEX_SLEEP);
        applyDisplayConnectivity();
        writeConfig();
        break;
    case ID_MONITOR_DISCONNECTED_AC_HIBERNATE:
        actions.setDisconnectedAC(INDEX_HIBERNATE);
        writeConfig();
    case ID_MONITOR_DISCONNECTED_AC_SHUT_DOWN:
        actions.setDisconnectedAC(INDEX_SHUT_DOWN);
        applyDisplayConnectivity();
        writeConfig();
        break;
    default:
        break;
    }
    if (ret != ERROR_SUCCESS) {
        SHOW_ERROR(ret);
        exit(1);
    }
}

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case UM_NOTIFY:
        if (lParam == WM_LBUTTONUP || lParam == WM_RBUTTONUP) {
            SetForegroundWindow(hwnd);
            HMENU menu = createNotifyPopupMenu();
            POINT pt = {0};
            GetCursorPos(&pt);
            UINT_PTR cmd = TrackPopupMenu(menu,
                                          TPM_RETURNCMD | GetSystemMetrics(SM_MENUDROPALIGNMENT),
                                          pt.x, pt.y,
                                          0,
                                          hwnd, NULL);
            DestroyMenu(menu);
            if (cmd != 0) {
                processNotifyMenuCmd(hwnd, cmd);
            }
        }
        return 0;
    case WM_DEVICECHANGE:
        if (wParam == DBT_DEVNODES_CHANGED) {
            if (!SetTimer(hwnd, DELAY_DEVICE_CHANGE_TIMER, DEVICE_CHANGE_DELAY, DelayDeviceChangeTimerProc)) {
                SHOW_LAST_ERROR();
            }
        }
        break;
    case WM_CREATE:
        showNotification(hwnd, silentMode);
        break;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        removeNotification(hwnd);
        PostQuitMessage(0);
        writeConfig();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void showNotification(HWND hwnd, bool silent) {
    NOTIFYICONDATAW data = {0};
    data.cbSize = sizeof data;
    data.hWnd = hwnd;
    data.uID = NOTIFY_ID;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    if (!silent) {
        data.uFlags |= NIF_INFO;
        StringCbCopyW(data.szInfo, sizeof(data.szInfo), loadStringRes(STR_RUNNING_IN_SYSTEM_TRAY).c_str());
        StringCbCopyW(data.szInfoTitle, sizeof(data.szInfoTitle), loadStringRes(STR_APP_NAME).c_str());
        data.dwInfoFlags = NIIF_INFO;
    }
    data.uCallbackMessage = UM_NOTIFY;
    data.hIcon = LoadIconW(GetModuleHandle(NULL), MAKEINTRESOURCEW(ICON_MAIN));
    StringCchCopyW(data.szTip, sizeof data.szTip / sizeof data.szTip[0], loadStringRes(STR_APP_NAME).c_str());
    if (!Shell_NotifyIconW(NIM_ADD, &data)) {
        SHOW_LAST_ERROR();
        return;
    }
}

void removeNotification(HWND hwnd) {
    NOTIFYICONDATAW data = {0};
    data.cbSize = sizeof data;
    data.hWnd = hwnd;
    data.uID = NOTIFY_ID;
    if (!Shell_NotifyIconW(NIM_DELETE, &data)) {
        SHOW_LAST_ERROR();
        return;
    }
}

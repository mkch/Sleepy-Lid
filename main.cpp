#include <windows.h>
#include <Dbt.h>
#include <commctrl.h>
#include <strsafe.h>
#include <shellapi.h>

#include <algorithm>
#include <string>
#include <vector>
#include <set>

#include "monitor.h"
#include "power.h"
#include "res.h"

#ifdef DEBUG
@
#endif

using namespace std;

// ID of Shell_NotifyIconW.
static const UINT NOTIFY_ID = 1;
// Notify message used by Shell_NotifyIconW.
static const UINT UM_NOTIFY = WM_USER + 1;

// Name of displays to keep the machine awake when the lid is closing.
static set<wstring> keepAwakeDisplays;
// Power actions before automatically turning power actions to "Do nothing".
// Used to restore the the previous value after external displays disconnected.
static DWORD lastPowerAction_Battery = INDEX_SLEEP;
static DWORD lastPowerAction_PluggedIn = INDEX_SLEEP;

// The extension of config file.
static const auto CONFIG_FILE_NAME = L"SleepyLid.ini";

// The arv[0]. GetModuleFileNameW(0).
wstring moduleFilePath;
// The path of config file. Initialized in entry point.
wstring configFilePath;

static const auto START_ON_BOOT_REG_SUB_KEY =
    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";
// The registry key under
static const auto START_ON_BOOT_REG_VALUE_NAME = L"SleepyLid";

int _main(HINSTANCE hInstance, int argc, wchar_t *argv[], int nCmdShow);
void showNotification(HWND hwnd);
void removeNotification(HWND hwnd);
void applyDisplayConnectivity();

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

// ini section name.
static const auto CONFIG_LID_CLOSING = L"LidClosing";
// ini key.
static const auto CONFIG_KEEP_AWAKE_DISPLAY_BASE = L"KeepAwakeDisplay";

// Max count of external displays to save to config file.
static const int MAX_DISPLAY_COUNT = 255;

// Read settings from config file.
void readConfig() {
    for (int i = 0; i < MAX_DISPLAY_COUNT; i++) {
        wstring key(CONFIG_KEEP_AWAKE_DISPLAY_BASE);
        key.append(std::to_wstring(i + 1));
        wchar_t display[1024] = {0};
        if (!GetPrivateProfileStringW(CONFIG_LID_CLOSING, key.c_str(), L"",
                                      display, sizeof(display) / sizeof(display[0]),
                                      configFilePath.c_str())) {
            break;
        }
        keepAwakeDisplays.insert(display);
    }
}

// Write settings to config file.
void writeConfig() {
    WritePrivateProfileStringW(CONFIG_LID_CLOSING, NULL, NULL,
                               configFilePath.c_str());
    int count = min(MAX_DISPLAY_COUNT, keepAwakeDisplays.size());
    int i = 0;
    for_each(keepAwakeDisplays.begin(), keepAwakeDisplays.end(),
             [&i](const auto &value) {
                 wstring key(CONFIG_KEEP_AWAKE_DISPLAY_BASE);
                 key.append(std::to_wstring(i + 1));
                 WritePrivateProfileStringW(
                     CONFIG_LID_CLOSING, key.c_str(), value.c_str(), configFilePath.c_str());
                 i++;
             });
}

// Show a message box with the error description of lastError.
void showError(DWORD lastError, char *file, int line) {
    char *msg = NULL;
    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, lastError, 0, (LPSTR)&msg, 0, NULL);

    char message[1024] = {0};
    _snprintf(message, sizeof message / sizeof message[0], "%s:%d\n%s", file, line, msg);
    MessageBoxA(NULL, message, "Error", MB_ICONERROR);
    LocalFree(msg);
}

#define SHOW_ERROR(err) showError(err, __FILE__, __LINE__)
#define SHOW_LAST_ERROR() SHOW_ERROR(GetLastError())

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

int _main(HINSTANCE hInstance, int argc, wchar_t *argv[], int nCmdShow) {
    // SetProcessDPIAware();
    // Initialize configFilePath.
    moduleFilePath = argv[0];
    configFilePath = moduleFilePath;
    const auto sep = configFilePath.rfind(L'\\');
    if (sep != string::npos) {
        configFilePath = configFilePath.substr(0, sep + 1) + CONFIG_FILE_NAME;
    }

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

// Timer id for delaying the WM_DEVICECHANGE message processing.
static const UINT_PTR DELAY_DEVICE_CHANGE_TIMER = 1;
// Batch interval of  WM_DEVICECHANGE message processing.
static const UINT DEVICE_CHANGE_DELAY = 3000;

// Any displays of interest ate connected currently.
static bool displayConnected = false;

// Set power action based on the current display connectivity.
void applyDisplayConnectivity() {
    vector<wstring> monitors;
    auto ret = connectedMonitors(monitors);
    if (ret != ERROR_SUCCESS) {
        goto handle_error;
    }
    sort(monitors.begin(), monitors.end());
    bool found = false;
    for (auto it = keepAwakeDisplays.begin(); it != keepAwakeDisplays.end(); it++) {
        if (binary_search(monitors.begin(), monitors.end(), *it)) {
            found = true;
            break;
        }
    }

    if (found) {
        if (!displayConnected) {
            displayConnected = true;
            ret = readLidCloseActionIndex_Battery(&lastPowerAction_Battery);
            if (ret != ERROR_SUCCESS) {
                goto handle_error;
            }
            ret = writeLidCloseActionIndex_Battery(INDEX_DO_NOTHING);
            if (ret != ERROR_SUCCESS) {
                goto handle_error;
            }

            ret = readLidCloseActionIndex_PluggedIn(&lastPowerAction_PluggedIn);
            if (ret != ERROR_SUCCESS) {
                goto handle_error;
            }
            ret = writeLidCloseActionIndex_PluggedIn(INDEX_DO_NOTHING);
            if (ret != ERROR_SUCCESS) {
                goto handle_error;
            }
        }
    } else {
        if (displayConnected) {
            displayConnected = false;
            ret = writeLidCloseActionIndex_Battery(lastPowerAction_Battery);
            if (ret != ERROR_SUCCESS) {
                goto handle_error;
            }
            ret = writeLidCloseActionIndex_PluggedIn(lastPowerAction_PluggedIn);
            if (ret != ERROR_SUCCESS) {
                goto handle_error;
            }
        }
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

    ID_BATTERY_DO_NOTHING,
    ID_BATTERY_SLEEP,
    ID_BATTERY_HIBERNATE,
    ID_BATTERY_SHUT_DOWN,

    ID_PLUGGED_IN_DO_NOTHING,
    ID_PLUGGED_IN_SLEEP,
    ID_PLUGGED_IN_HIBERNATE,
    ID_PLUGGED_IN_SHUT_DOWN,

    ID_MONITOR_FIRST,  // Must be the last one.
};

static const auto LABEL_DO_NOTHING = L"Do nothing";
static const auto LABEL_SLEEP = L"Sleep";
static const auto LABEL_HIBERNATE = L"Hibernate";
static const auto LABEL_SHUT_DOWN = L"Shut down";
static const auto LABEL_USING_BATTERY = L"Using battery";
static const auto LABEL_PLUGGED_IN = L"Plugged in";
static const auto FMT_ON_BATTERY = L"On battery: [%s]";
static const auto FMT_PLUGGED_IN = L"Plugged in: [%s]";
static const auto LABEL_AWAKE_DISPLAY = L"Keep awake if connected:";
static const auto LABEL_WHEN_LID_CLOSING = L"When lid closing";
static const auto LABEL_EXIT = L"Exit";
static const auto LABEL_START_ON_BOOT = L"Start on boot";

static const auto LABEL_UNKNOWN = L"???";

static const wchar_t *powerActionToString(DWORD index) {
    switch (index) {
    case INDEX_DO_NOTHING:
        return LABEL_DO_NOTHING;
    case INDEX_SLEEP:
        return LABEL_SLEEP;
    case INDEX_HIBERNATE:
        return LABEL_HIBERNATE;
    case INDEX_SHUT_DOWN:
        return LABEL_SHUT_DOWN;
    default:
        return LABEL_UNKNOWN;
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
            std::equal(moduleFilePath.begin(), moduleFilePath.end(),
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
                             (const BYTE *)moduleFilePath.c_str(), moduleFilePath.size() * sizeof(wchar_t));
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
// If the return value is not NULL, the memory pointed by displayList is set
// to the pointer of display name list in sub menu. This vector MUST be deleted
// after use.
HMENU createNotifyPopupMenu(vector<wstring> **displayList) {
    DWORD actionBattery = 0;
    DWORD ret = readLidCloseActionIndex_Battery(&actionBattery);
    if (ret != ERROR_SUCCESS) {
        SHOW_ERROR(ret);
        exit(1);
    }

    DWORD actionPluggedIn = 0;
    ret = readLidCloseActionIndex_PluggedIn(&actionPluggedIn);
    if (ret != ERROR_SUCCESS) {
        SHOW_ERROR(ret);
        exit(1);
    }

    HMENU onBattery = CreateMenu();
    AppendMenuW(onBattery,
                MF_STRING | (actionBattery == INDEX_DO_NOTHING ? MF_CHECKED : 0),
                ID_BATTERY_DO_NOTHING, LABEL_DO_NOTHING);
    AppendMenuW(onBattery,
                MF_STRING | (actionBattery == INDEX_SLEEP ? MF_CHECKED : 0),
                ID_BATTERY_SLEEP, LABEL_SLEEP);
    AppendMenuW(onBattery,
                MF_STRING | (actionBattery == INDEX_HIBERNATE ? MF_CHECKED : 0),
                ID_BATTERY_HIBERNATE, LABEL_HIBERNATE);
    AppendMenuW(onBattery,
                MF_STRING | (actionBattery == INDEX_SHUT_DOWN ? MF_CHECKED : 0),
                ID_BATTERY_SHUT_DOWN, LABEL_SHUT_DOWN);

    HMENU pluggedIn = CreateMenu();
    AppendMenuW(pluggedIn,
                MF_STRING | (actionPluggedIn == INDEX_DO_NOTHING ? MF_CHECKED : 0),
                ID_PLUGGED_IN_DO_NOTHING, LABEL_DO_NOTHING);
    AppendMenuW(pluggedIn,
                MF_STRING | (actionPluggedIn == INDEX_SLEEP ? MF_CHECKED : 0),
                ID_PLUGGED_IN_SLEEP, LABEL_SLEEP);
    AppendMenuW(pluggedIn,
                MF_STRING | (actionPluggedIn == INDEX_HIBERNATE ? MF_CHECKED : 0),
                ID_PLUGGED_IN_HIBERNATE, LABEL_HIBERNATE);
    AppendMenuW(pluggedIn,
                MF_STRING | (actionPluggedIn == INDEX_SHUT_DOWN ? MF_CHECKED : 0),
                ID_PLUGGED_IN_SHUT_DOWN, LABEL_SHUT_DOWN);

    HMENU monitors = CreateMenu();

    vector<wstring> connected;
    ret = connectedMonitors(connected);
    if (ret != ERROR_SUCCESS) {
        SHOW_ERROR(ret);
        exit(1);
    }

    sort(connected.begin(), connected.end());
    const auto list = new vector<wstring>(connected);
    *displayList = list;
    // Append all the elements in keepAwakeDisplays.
    list->insert(list->end(), keepAwakeDisplays.begin(), keepAwakeDisplays.end());
    // Erase duplicated elements in connected.
    // https://en.cppreference.com/w/cpp/algorithm/unique
    sort(list->begin(), list->end());
    list->erase(unique(list->begin(), list->end()), list->end());

    for (int i = 0; i < list->size(); i++) {
        const wstring &item = list->at(i);
        UINT flag = 0;
        wstring title = item;
        if (keepAwakeDisplays.find(item) != keepAwakeDisplays.end()) {
            flag = MF_CHECKED;
        }
        if (binary_search(connected.begin(), connected.end(), item)) {
            title += L" *";  // Connected mark.
        }
        AppendMenuW(monitors, MF_STRING | flag, ID_MONITOR_FIRST + i, title.c_str());
    }

    HMENU lidClosing = CreateMenu();

    wchar_t buf[1024] = {0};
    StringCbPrintfW(buf, sizeof(buf), FMT_ON_BATTERY, powerActionToString(actionBattery));
    AppendMenuW(lidClosing, MF_POPUP, (UINT_PTR)onBattery, buf);
    StringCbPrintfW(buf, sizeof(buf), FMT_PLUGGED_IN, powerActionToString(actionPluggedIn));
    AppendMenuW(lidClosing, MF_POPUP, (UINT_PTR)pluggedIn, buf);
    AppendMenuW(lidClosing, MF_SEPARATOR, 0, NULL);
    AppendMenuW(lidClosing, MF_SEPARATOR, 0, NULL);

    wstring title(LABEL_AWAKE_DISPLAY);
    for_each(keepAwakeDisplays.begin(), keepAwakeDisplays.end(),
             [&title, &connected](auto const &m) {
                 title.append(L" [").append(m);
                 if (binary_search(connected.begin(), connected.end(), m)) {
                     title.append(L" *");  // Connected mark.
                 }
                 title.append(L"]");
             });
    AppendMenuW(lidClosing,
                MF_POPUP | (keepAwakeDisplays.empty() ? 0 : MF_CHECKED),
                (UINT_PTR)monitors, title.c_str());

    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)lidClosing, LABEL_WHEN_LID_CLOSING);
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);

    AppendMenuW(menu, MF_STRING | (startOnBootEnabled() ? MF_CHECKED : 0),
                ID_AUTO_RUN, LABEL_START_ON_BOOT);
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, ID_EXIT, LABEL_EXIT);
    return menu;
}

void processNotifyMenuCmd(HWND hwnd, UINT_PTR cmd,
                          vector<wstring> *displayList) {
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
    case ID_BATTERY_DO_NOTHING:
        ret = writeLidCloseActionIndex_Battery(INDEX_DO_NOTHING);
        break;
    case ID_BATTERY_SLEEP:
        ret = writeLidCloseActionIndex_Battery(INDEX_SLEEP);
        break;
    case ID_BATTERY_HIBERNATE:
        ret = writeLidCloseActionIndex_Battery(INDEX_HIBERNATE);
        break;
    case ID_BATTERY_SHUT_DOWN:
        ret = writeLidCloseActionIndex_Battery(INDEX_SHUT_DOWN);
        break;
    case ID_PLUGGED_IN_DO_NOTHING:
        ret = writeLidCloseActionIndex_PluggedIn(INDEX_DO_NOTHING);
        break;
    case ID_PLUGGED_IN_SLEEP:
        ret = writeLidCloseActionIndex_PluggedIn(INDEX_SLEEP);
        break;
    case ID_PLUGGED_IN_HIBERNATE:
        ret = writeLidCloseActionIndex_PluggedIn(INDEX_HIBERNATE);
        break;
    case ID_PLUGGED_IN_SHUT_DOWN:
        ret = writeLidCloseActionIndex_PluggedIn(INDEX_SHUT_DOWN);
        break;
    default:
        if (cmd >= ID_MONITOR_FIRST) {
            const int index = cmd - ID_MONITOR_FIRST;
            const wstring &clicked = displayList->at(index);
            const auto found = keepAwakeDisplays.find(clicked);
            if (found != keepAwakeDisplays.end()) {
                keepAwakeDisplays.erase(found);
            } else {
                keepAwakeDisplays.insert(clicked);
            }
            applyDisplayConnectivity();
            writeConfig();
        }
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
            vector<wstring> *displayList = NULL;
            HMENU menu = createNotifyPopupMenu(&displayList);
            POINT pt = {0};
            GetCursorPos(&pt);
            UINT_PTR cmd = TrackPopupMenu(menu,
                                          TPM_RETURNCMD | GetSystemMetrics(SM_MENUDROPALIGNMENT),
                                          pt.x, pt.y,
                                          0,
                                          hwnd, NULL);
            DestroyMenu(menu);
            if (cmd != 0) {
                processNotifyMenuCmd(hwnd, cmd, displayList);
            }
            delete displayList;
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
        showNotification(hwnd);
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

void showNotification(HWND hwnd) {
    NOTIFYICONDATAW data = {0};
    data.cbSize = sizeof data;
    data.hWnd = hwnd;
    data.uID = NOTIFY_ID;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    data.uCallbackMessage = UM_NOTIFY;
    data.hIcon = LoadIconW(GetModuleHandle(NULL), MAKEINTRESOURCEW(ICON_MAIN));
    StringCchCopyW(data.szTip, sizeof data.szTip / sizeof data.szTip[0], L"Lid closing");
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

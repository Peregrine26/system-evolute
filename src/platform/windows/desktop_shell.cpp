#include "desktop/shell.h"

#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>

#include <QCoreApplication>
#include <QDir>
#include <QLibrary>
#include <QMutex>
#include <QMutexLocker>
#include <QDateTime>
#include <algorithm>

namespace crossdesk::desktop {
namespace {
class ComScope {
public:
    ComScope() : initialized_(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {}
    ~ComScope() { if (initialized_) CoUninitialize(); }
private:
    bool initialized_;
};
bool shellVerb(const QString& path, const wchar_t* verb, std::uintptr_t owner) {
    const auto native = QDir::toNativeSeparators(path).toStdWString();
    SHELLEXECUTEINFOW operation{};
    operation.cbSize = sizeof(operation);
    operation.fMask = SEE_MASK_INVOKEIDLIST;
    operation.hwnd = reinterpret_cast<HWND>(owner);
    operation.lpVerb = verb;
    operation.lpFile = native.c_str();
    operation.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&operation) != FALSE;
}
IAudioEndpointVolume* endpoint() {
    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    IAudioEndpointVolume* volume = nullptr;
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator)))) {
        if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device))) {
            device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
                             reinterpret_cast<void**>(&volume));
            device->Release();
        }
        enumerator->Release();
    }
    return volume;
}
}
bool fileProperties(const QString& path, std::uintptr_t owner) { return shellVerb(path, L"properties", owner); }
bool openWith(const QString& path, std::uintptr_t owner) { return shellVerb(path, L"openas", owner); }
bool systemFileMenu(const QString& path, std::uintptr_t owner, const QPoint& position) {
    ComScope scope;
    PIDLIST_ABSOLUTE item = nullptr;
    const auto native = QDir::toNativeSeparators(path).toStdWString();
    if (FAILED(SHParseDisplayName(native.c_str(), nullptr, &item, 0, nullptr))) return false;
    IShellFolder* folder = nullptr;
    PCUITEMID_CHILD child = nullptr;
    IContextMenu* context = nullptr;
    bool success = false;
    const auto window = reinterpret_cast<HWND>(owner);
    if (SUCCEEDED(SHBindToParent(item, IID_IShellFolder, reinterpret_cast<void**>(&folder), &child))) {
        if (SUCCEEDED(folder->GetUIObjectOf(window, 1, &child, IID_IContextMenu, nullptr,
                                           reinterpret_cast<void**>(&context)))) {
            HMENU menu = CreatePopupMenu();
            if (menu && SUCCEEDED(context->QueryContextMenu(menu, 0, 1, 0x7FFF, CMF_NORMAL))) {
                POINT point{};
                GetCursorPos(&point); // Native pixels, independent of Qt logical DPI.
                const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                                     point.x, point.y, 0, window, nullptr);
                success = true; // Dismissing a valid menu is not an error.
                if (command) {
                    CMINVOKECOMMANDINFO invoke{};
                    invoke.cbSize = sizeof(invoke);
                    invoke.hwnd = window;
                    invoke.lpVerb = MAKEINTRESOURCEA(command - 1);
                    invoke.nShow = SW_SHOWNORMAL;
                    success = SUCCEEDED(context->InvokeCommand(&invoke));
                }
            }
            if (menu) DestroyMenu(menu);
            context->Release();
        }
        folder->Release();
    }
    CoTaskMemFree(item);
    Q_UNUSED(position);
    return success;
}
bool mediaKey(const QString& command) {
    const WORD key = command == "previous" ? VK_MEDIA_PREV_TRACK :
                     command == "next" ? VK_MEDIA_NEXT_TRACK : VK_MEDIA_PLAY_PAUSE;
    INPUT input[2]{};
    input[0].type = INPUT_KEYBOARD;
    input[0].ki.wVk = key;
    input[1] = input[0];
    input[1].ki.dwFlags = KEYEVENTF_KEYUP;
    return SendInput(2, input, sizeof(INPUT)) == 2;
}
int systemVolume() {
    ComScope scope;
    auto* volume = endpoint();
    if (!volume) return -1;
    float value = 0;
    const bool ok = SUCCEEDED(volume->GetMasterVolumeLevelScalar(&value));
    volume->Release();
    return ok ? qRound(value * 100) : -1;
}
bool setSystemVolume(int value) {
    ComScope scope;
    auto* volume = endpoint();
    if (!volume) return false;
    const bool ok = SUCCEEDED(volume->SetMasterVolumeLevelScalar(std::clamp(value, 0, 100) / 100.0f, nullptr));
    volume->Release();
    return ok;
}
bool setAutoStart(bool enabled, const QString& executable, QString* error) {
    HKEY key = nullptr;
    LSTATUS result = RegCreateKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, nullptr, 0,
        KEY_SET_VALUE, nullptr, &key, nullptr);
    if (result == ERROR_SUCCESS) {
        const auto command = (QStringLiteral("\"") + QDir::toNativeSeparators(executable) + "\" --background").toStdWString();
        result = enabled ? RegSetValueExW(key, L"CrossDesk", 0, REG_SZ,
            reinterpret_cast<const BYTE*>(command.c_str()), DWORD((command.size() + 1) * sizeof(wchar_t)))
            : RegDeleteValueW(key, L"CrossDesk");
        if (!enabled && result == ERROR_FILE_NOT_FOUND) result = ERROR_SUCCESS;
        RegCloseKey(key);
    }
    if (result != ERROR_SUCCESS && error) *error = QStringLiteral("开机启动设置失败（%1）").arg(result);
    return result == ERROR_SUCCESS;
}
QStringList everythingSearch(const QString& query, QString* error) {
    // Everything's SDK owns process-global query state; serialize worker queries.
    static QMutex mutex;
    QMutexLocker lock(&mutex);
    QLibrary library(QCoreApplication::applicationDirPath() + "/EverythingSdk.dll");
    if (!library.load()) {
        if (error) *error = QStringLiteral("未找到 Everything 接口，将搜索已映射的文件夹");
        return {};
    }
    using SetSearch = void (WINAPI*)(const wchar_t*);
    using SetFlags = void (WINAPI*)(DWORD);
    using SetMax = void (WINAPI*)(DWORD);
    using Query = BOOL (WINAPI*)(BOOL);
    using Count = DWORD (WINAPI*)();
    using GetPath = DWORD (WINAPI*)(DWORD, wchar_t*, DWORD);
    auto search = reinterpret_cast<SetSearch>(library.resolve("Everything_SetSearchW"));
    auto flags = reinterpret_cast<SetFlags>(library.resolve("Everything_SetRequestFlags"));
    auto maximum = reinterpret_cast<SetMax>(library.resolve("Everything_SetMax"));
    auto execute = reinterpret_cast<Query>(library.resolve("Everything_QueryW"));
    auto count = reinterpret_cast<Count>(library.resolve("Everything_GetNumResults"));
    auto path = reinterpret_cast<GetPath>(library.resolve("Everything_GetResultFullPathNameW"));
    if (!search || !flags || !maximum || !execute || !count || !path) {
        if (error) *error = QStringLiteral("Everything 接口版本不兼容");
        return {};
    }
    const auto text = query.toStdWString();
    search(text.c_str()); flags(1 | 2); maximum(500);
    if (!execute(TRUE)) {
        if (error) *error = QStringLiteral("Everything 未运行或无法连接，将搜索已映射的文件夹");
        return {};
    }
    QStringList results;
    wchar_t buffer[32768]{};
    for (DWORD i = 0; i < count(); ++i) if (path(i, buffer, 32768)) results << QString::fromWCharArray(buffer);
    return results;
}
QString lunarDate(const QDate& date) {
    // Windows 10+ ships ICU; query its Chinese calendar without a second runtime.
    static HMODULE library = LoadLibraryExW(L"icu.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!library) return {};
    using Open = void* (*)(const wchar_t*, int, const char*, int, int*);
    using Set = void (*)(void*, double, int*);
    using Get = int (*)(const void*, int, int*);
    using Close = void (*)(void*);
    const auto open = reinterpret_cast<Open>(GetProcAddress(library, "ucal_open"));
    const auto set = reinterpret_cast<Set>(GetProcAddress(library, "ucal_setMillis"));
    const auto get = reinterpret_cast<Get>(GetProcAddress(library, "ucal_get"));
    const auto close = reinterpret_cast<Close>(GetProcAddress(library, "ucal_close"));
    if (!open || !set || !get || !close || !date.isValid()) return {};
    int error = 0; void* calendar = open(nullptr, 0, "zh_CN@calendar=chinese", 0, &error);
    if (!calendar || error > 0) return {};
    set(calendar, double(QDateTime(date, QTime(12, 0)).toMSecsSinceEpoch()), &error);
    const int month = get(calendar, 2, &error) + 1;
    const int day = get(calendar, 5, &error);
    const bool leap = get(calendar, 22, &error) != 0; close(calendar);
    if (error > 0 || month < 1 || month > 12 || day < 1 || day > 30) return {};
    const QStringList months{QStringLiteral("正"), QStringLiteral("二"), QStringLiteral("三"), QStringLiteral("四"),
        QStringLiteral("五"), QStringLiteral("六"), QStringLiteral("七"), QStringLiteral("八"), QStringLiteral("九"),
        QStringLiteral("十"), QStringLiteral("冬"), QStringLiteral("腊")};
    const QString digits = QStringLiteral("一二三四五六七八九十"); QString dayName;
    if (day <= 10) dayName = QStringLiteral("初") + digits[day - 1];
    else if (day < 20) dayName = QStringLiteral("十") + digits[day - 11];
    else if (day == 20) dayName = QStringLiteral("二十");
    else if (day < 30) dayName = QStringLiteral("廿") + digits[day - 21];
    else dayName = QStringLiteral("三十");
    return QStringLiteral("农历 ") + (leap ? QStringLiteral("闰") : QString{}) + months[month - 1] + QStringLiteral("月") + dayName;
}
} // namespace crossdesk::desktop

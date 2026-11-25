#include "desktop/search_apps.h"
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <QDir>
namespace crossdesk::desktop {
QList<SearchHit> installedApplications() {
    QList<SearchHit> result;
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IShellItem* folder = nullptr;
    if (SUCCEEDED(SHCreateItemFromParsingName(L"shell:AppsFolder", nullptr, IID_IShellItem, reinterpret_cast<void**>(&folder)))) {
        IEnumShellItems* items = nullptr;
        if (SUCCEEDED(folder->BindToHandler(nullptr, BHID_EnumItems, IID_IEnumShellItems, reinterpret_cast<void**>(&items)))) {
            IShellItem* item = nullptr;
            while (result.size() < 4096 && items->Next(1, &item, nullptr) == S_OK) {
                PWSTR name = nullptr; PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &name)) &&
                    SUCCEEDED(item->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &path)))
                    result.append({QString::fromWCharArray(name), "app", QString::fromWCharArray(path), {}, {}, 0});
                CoTaskMemFree(name); CoTaskMemFree(path); item->Release();
            }
            items->Release();
        }
        folder->Release();
    }
    if (SUCCEEDED(initialized)) CoUninitialize();
    return result;
}
bool openApplication(const QString& location) {
    const auto native = location.toStdWString();
    return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", native.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
}
}

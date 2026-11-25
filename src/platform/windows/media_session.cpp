#include <QCoreApplication>
#include "desktop/media_session.h"
#include <windows.h>
#include <unknwn.h>
#include <QElapsedTimer>
#include <algorithm>
#include <utility>

// WinRT ABI declarations follow Microsoft's Windows.Media.Control contract.
// Keeping this small ABI boundary avoids a C++/WinRT compiler/runtime dependency.
// Contract: https://learn.microsoft.com/uwp/api/windows.media.control
// Slot order verified against microsoft/windows-rs Windows/Media/Control and
// Windows/Storage/Streams metadata projections (no projection code is bundled).
namespace crossdesk::desktop {
namespace {
template<class Result, class... Args> HRESULT call(void* object, int slot, Result* result, Args... args) {
    if (!object) return E_POINTER;
    using Function = HRESULT (STDMETHODCALLTYPE*)(void*, Args..., Result*);
    auto table = *static_cast<void***>(object);
    return reinterpret_cast<Function>(table[slot])(object, args..., result);
}
struct Ptr {
    void* p = nullptr;
    Ptr() = default; Ptr(const Ptr&) = delete; Ptr& operator=(const Ptr&) = delete;
    Ptr(Ptr&& other) noexcept : p(std::exchange(other.p, nullptr)) {}
    Ptr& operator=(Ptr&& other) noexcept { reset(); p = std::exchange(other.p, nullptr); return *this; }
    ~Ptr() { reset(); }
    void reset() { if (p) { static_cast<IUnknown*>(p)->Release(); p = nullptr; } }
    explicit operator bool() const { return p != nullptr; }
    void** out() { reset(); return &p; }
    HRESULT query(const GUID& id, Ptr& result) const {
        return p ? static_cast<IUnknown*>(p)->QueryInterface(id, result.out()) : E_POINTER;
    }
};
GUID guid(const wchar_t* text) { GUID result{}; CLSIDFromString(text, &result); return result; }
struct Runtime {
    using Init = HRESULT (WINAPI*)(int);
    using Uninit = void (WINAPI*)();
    using CreateString = HRESULT (WINAPI*)(const wchar_t*, UINT32, void**);
    using DeleteString = HRESULT (WINAPI*)(void*);
    using GetString = const wchar_t* (WINAPI*)(void*, UINT32*);
    using GetFactory = HRESULT (WINAPI*)(void*, REFIID, void**);
    HMODULE library = LoadLibraryExW(L"combase.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    Init init = resolve<Init>("RoInitialize"); Uninit uninit = resolve<Uninit>("RoUninitialize");
    CreateString create = resolve<CreateString>("WindowsCreateString");
    DeleteString remove = resolve<DeleteString>("WindowsDeleteString");
    GetString raw = resolve<GetString>("WindowsGetStringRawBuffer");
    GetFactory factory = resolve<GetFactory>("RoGetActivationFactory");
    HRESULT status = E_NOTIMPL;
    template<class T> T resolve(const char* name) { return library ? reinterpret_cast<T>(GetProcAddress(library, name)) : nullptr; }
    Runtime() { if (init && uninit && create && remove && raw && factory) status = init(1); }
    ~Runtime() { if (SUCCEEDED(status)) uninit(); if (library) FreeLibrary(library); }
    bool available() const { return SUCCEEDED(status); }
    HRESULT activate(const wchar_t* name, const wchar_t* iid, Ptr& output) {
        void* text = nullptr; auto status = create(name, UINT32(wcslen(name)), &text);
        if (SUCCEEDED(status)) status = factory(text, guid(iid), output.out());
        if (text) remove(text); return status;
    }
    QString stringProperty(void* object, int slot) {
        void* value = nullptr; if (FAILED(call(object, slot, &value)) || !value) return {};
        UINT32 length = 0; const auto* text = raw(value, &length);
        const auto result = QString::fromWCharArray(text, int(length)); remove(value); return result;
    }
};
QString failure(HRESULT status) { return QCoreApplication::translate("CrossDesk", "媒体会话不可用（0x%1）").arg(quint32(status), 8, 16, QChar('0')); }
HRESULT waitOperation(Ptr& operation, void** result, bool progress = false) {
    if (!operation) return E_POINTER;
    Ptr info; auto status = operation.query(guid(L"{00000036-0000-0000-C000-000000000046}"), info);
    if (FAILED(status)) return status;
    QElapsedTimer timer; timer.start(); int state = 0;
    do {
        status = call(info.p, 7, &state);
        if (FAILED(status)) return status;
        if (!state) Sleep(8);
    } while (!state && timer.elapsed() < 2000);
    if (!state) {
        using Cancel = HRESULT (STDMETHODCALLTYPE*)(void*);
        reinterpret_cast<Cancel>((*static_cast<void***>(info.p))[9])(info.p);
        return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    }
    if (state != 1) { HRESULT error = E_ABORT; call(info.p, 8, &error); return FAILED(error) ? error : E_ABORT; }
    return call(operation.p, progress ? 10 : 8, result);
}
HRESULT waitBoolean(Ptr& operation, bool& result) {
    // GetResults is generic; a WinRT Boolean is one byte, not Win32 BOOL.
    Ptr info; if (!operation || FAILED(operation.query(guid(L"{00000036-0000-0000-C000-000000000046}"), info))) return E_POINTER;
    QElapsedTimer timer; timer.start(); int state = 0; HRESULT status = S_OK;
    do { status = call(info.p, 7, &state); if (FAILED(status)) return status; if (!state) Sleep(8); }
    while (!state && timer.elapsed() < 2000);
    if (!state) {
        using Cancel = HRESULT (STDMETHODCALLTYPE*)(void*);
        reinterpret_cast<Cancel>((*static_cast<void***>(info.p))[9])(info.p); return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    }
    if (state != 1) return E_ABORT;
    return call(operation.p, 8, &result);
}
HRESULT manager(Runtime& runtime, Ptr& output) {
    if (!runtime.available()) return runtime.status;
    Ptr factory, operation;
    auto status = runtime.activate(L"Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager",
        L"{2050C4EE-11A0-57DE-AED7-C97C70338245}", factory);
    if (SUCCEEDED(status)) status = call(factory.p, 6, operation.out());
    return SUCCEEDED(status) ? waitOperation(operation, output.out()) : status;
}
Ptr session(Runtime& runtime, void* manager, const QString& selected, QList<MediaSessionName>* names) {
    Ptr selectedSession;
    if (selected.isEmpty()) call(manager, 6, selectedSession.out());
    Ptr sessions;
    if (SUCCEEDED(call(manager, 7, sessions.out())) && sessions) {
        unsigned count = 0; call(sessions.p, 7, &count);
        for (unsigned i = 0; i < std::min(count, 128u); ++i) {
            Ptr item;
            if (FAILED(call(sessions.p, 6, item.out(), i)) || !item) continue;
            const auto id = runtime.stringProperty(item.p, 6);
            if (names) names->append({id, id});
            if (!selected.isEmpty() && id == selected) selectedSession = std::move(item);
        }
    }
    return selectedSession;
}
QByteArray artwork(Runtime& runtime, void* properties) {
    Ptr reference, opening, content, stream, input, factory, buffer, reading, received, access;
    if (FAILED(call(properties, 15, reference.out())) || !reference ||
        FAILED(call(reference.p, 6, opening.out())) || FAILED(waitOperation(opening, content.out())) ||
        FAILED(content.query(guid(L"{905A0FE1-BC53-11DF-8C49-001E4FC686DA}"), stream))) return {};
    unsigned long long size = 0;
    if (FAILED(call(stream.p, 6, &size)) || !size || size > 8 * 1024 * 1024 ||
        FAILED(call(stream.p, 8, input.out(), 0ull))) return {};
    if (FAILED(runtime.activate(L"Windows.Storage.Streams.Buffer", L"{71AF914D-C10F-484B-BC50-14BC623B3A27}", factory)) ||
        FAILED(call(factory.p, 6, buffer.out(), unsigned(size))) ||
        FAILED(call(input.p, 6, reading.out(), buffer.p, unsigned(size), 0)) ||
        FAILED(waitOperation(reading, received.out(), true)) ||
        FAILED(received.query(guid(L"{905A0FE0-BC53-11DF-8C49-001E4FC686DA}"), buffer))) return {};
    unsigned length = 0; call(buffer.p, 7, &length);
    if (length > size || FAILED(buffer.query(guid(L"{905A0FEF-BC53-11DF-8C49-001E4FC686DA}"), access))) return {};
    // IBufferByteAccess derives from IUnknown (slot 3), unlike IInspectable.
    unsigned char* bytes = nullptr;
    return SUCCEEDED(call(access.p, 3, &bytes)) && bytes ? QByteArray(reinterpret_cast<char*>(bytes), int(length)) : QByteArray{};
}
bool capability(void* controls, int offset) { bool value = false; call(controls, 6 + offset, &value); return value; }
}

MediaSnapshot readMediaSession(const QString& selected, const QString& previousTrack) {
    MediaSnapshot result; Runtime runtime; Ptr owner;
    const auto status = manager(runtime, owner);
    if (FAILED(status)) { result.error = failure(status); return result; }
    auto current = session(runtime, owner.p, selected, &result.sessions);
    if (!current) { result.error = selected.isEmpty() ? QCoreApplication::translate("CrossDesk", "没有活动的媒体会话") : QCoreApplication::translate("CrossDesk", "所选媒体会话已关闭"); return result; }
    result.source = runtime.stringProperty(current.p, 6); result.available = true;
    Ptr operation, properties, timeline, playback, controls;
    if (SUCCEEDED(call(current.p, 7, operation.out())) && SUCCEEDED(waitOperation(operation, properties.out())) && properties) {
        result.title = runtime.stringProperty(properties.p, 6); result.artist = runtime.stringProperty(properties.p, 9);
        result.album = runtime.stringProperty(properties.p, 10);
        if (result.source + '\n' + result.title + '\n' + result.artist != previousTrack) result.cover = artwork(runtime, properties.p);
    }
    if (SUCCEEDED(call(current.p, 8, timeline.out())) && timeline) {
        call(timeline.p, 6, &result.start); call(timeline.p, 7, &result.end);
        call(timeline.p, 8, &result.minimum); call(timeline.p, 9, &result.maximum); call(timeline.p, 10, &result.position);
    }
    if (SUCCEEDED(call(current.p, 9, playback.out())) && playback) {
        int state = 0; call(playback.p, 7, &state); result.playing = state == 4;
        if (SUCCEEDED(call(playback.p, 6, controls.out())) && controls) {
            result.canToggle = capability(controls.p, 10); result.canPrevious = capability(controls.p, 7);
            result.canNext = capability(controls.p, 6); result.canShuffle = capability(controls.p, 11);
            result.canRepeat = capability(controls.p, 12); result.canSeek = capability(controls.p, 14);
        }
        Ptr value;
        if (SUCCEEDED(call(playback.p, 9, value.out())) && value) call(value.p, 6, &result.repeat);
        value.reset();
        if (SUCCEEDED(call(playback.p, 11, value.out())) && value) call(value.p, 6, &result.shuffle);
    }
    return result;
}
bool controlMediaSession(const QString& selected, const QString& action, qint64 value, QString* error) {
    Runtime runtime; Ptr owner; HRESULT status = manager(runtime, owner);
    auto current = SUCCEEDED(status) ? session(runtime, owner.p, selected, nullptr) : Ptr{};
    if (!current) { if (error) *error = SUCCEEDED(status) ? QCoreApplication::translate("CrossDesk", "媒体会话已关闭") : failure(status); return false; }
    Ptr operation;
    if (action == "play") status = call(current.p, 20, operation.out());
    else if (action == "next") status = call(current.p, 16, operation.out());
    else if (action == "previous") status = call(current.p, 17, operation.out());
    else if (action == "seek") status = call(current.p, 24, operation.out(), static_cast<long long>(value));
    else if (action == "repeat") status = call(current.p, 21, operation.out(), int(std::clamp(value, 0ll, 2ll)));
    else if (action == "shuffle") status = call(current.p, 23, operation.out(), value != 0);
    else status = E_INVALIDARG;
    bool accepted = false; if (SUCCEEDED(status)) status = waitBoolean(operation, accepted);
    if ((!accepted || FAILED(status)) && error) *error = FAILED(status) ? failure(status) : QCoreApplication::translate("CrossDesk", "播放器不支持或拒绝了此操作");
    return SUCCEEDED(status) && accepted;
}
}

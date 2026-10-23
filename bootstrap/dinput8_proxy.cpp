#define DIRECTINPUT_VERSION 0x0800

#include <Windows.h>
#include <dinput.h>
#include <MinHook.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <mutex>
#include <string>
#include <system_error>

namespace
{
    namespace fs = std::filesystem;

    HMODULE g_proxyModule = nullptr;
    HMODULE g_realDInput = nullptr;
    HMODULE g_gzfxModule = nullptr;

    std::once_flag g_realOnce;
    std::once_flag g_gzfxOnce;
    std::once_flag g_minHookOnce;

    std::mutex g_hookMutex;

    using DirectInput8CreateFn = HRESULT(WINAPI*)(
        HINSTANCE,
        DWORD,
        REFIID,
        LPVOID*,
        LPUNKNOWN);

    using DllCanUnloadNowFn = HRESULT(WINAPI*)();
    using DllGetClassObjectFn = HRESULT(WINAPI*)(
        REFCLSID,
        REFIID,
        LPVOID*);

    using DllRegisterServerFn = HRESULT(WINAPI*)();
    using DllUnregisterServerFn = HRESULT(WINAPI*)();
    using GetdfDIJoystickFn = LPCDIDATAFORMAT(WINAPI*)();

    using GzfxInitializeFn = bool (*)();
    using GzfxIsMenuOpenFn = bool (*)();

    using GzfxSubmitMouseInputFn = void (*)(
        LONG,
        LONG,
        LONG,
        std::uint32_t);

    using DirectInputCreateDeviceFn = HRESULT(
        STDMETHODCALLTYPE*)(
            void*,
            REFGUID,
            void**,
            LPUNKNOWN);

    using DirectInputGetDeviceStateFn = HRESULT(
        STDMETHODCALLTYPE*)(
            void*,
            DWORD,
            LPVOID);

    using DirectInputGetDeviceDataFn = HRESULT(
        STDMETHODCALLTYPE*)(
            void*,
            DWORD,
            LPDIDEVICEOBJECTDATA,
            LPDWORD,
            DWORD);

    DirectInput8CreateFn g_directInput8Create = nullptr;
    DllCanUnloadNowFn g_dllCanUnloadNow = nullptr;
    DllGetClassObjectFn g_dllGetClassObject = nullptr;
    DllRegisterServerFn g_dllRegisterServer = nullptr;
    DllUnregisterServerFn g_dllUnregisterServer = nullptr;
    GetdfDIJoystickFn g_getdfDIJoystick = nullptr;

    GzfxIsMenuOpenFn g_isMenuOpen = nullptr;
    GzfxSubmitMouseInputFn g_submitMouseInput = nullptr;

    DirectInputCreateDeviceFn g_originalCreateDevice = nullptr;
    DirectInputGetDeviceStateFn g_originalGetDeviceState = nullptr;
    DirectInputGetDeviceDataFn g_originalGetDeviceData = nullptr;

    void* g_createDeviceTarget = nullptr;
    void* g_getDeviceStateTarget = nullptr;
    void* g_getDeviceDataTarget = nullptr;

    std::atomic<ULONGLONG> g_lastStateSubmitTick{ 0 };
    std::atomic<std::uint32_t> g_bufferedButtonMask{ 0 };

    fs::path GetModulePath(HMODULE module)
    {
        std::wstring buffer(32768, L'\0');

        const DWORD written = GetModuleFileNameW(
            module,
            buffer.data(),
            static_cast<DWORD>(buffer.size()));

        if (written == 0 ||
            static_cast<std::size_t>(written) >=
                buffer.size())
        {
            return {};
        }

        buffer.resize(written);
        return fs::path(buffer);
    }

    template <class Function>
    void* FunctionAddress(Function function)
    {
        return reinterpret_cast<void*>(
            reinterpret_cast<std::uintptr_t>(
                function));
    }

    void ResolveRealDInput()
    {
        const fs::path proxyPath =
            GetModulePath(g_proxyModule);

        if (proxyPath.empty())
        {
            return;
        }

        const fs::path chainPath =
            proxyPath.parent_path() /
            L"dinput8_gzfx_chain.dll";

        std::error_code existsError;

        const bool chainExists =
            fs::exists(
                chainPath,
                existsError);

        if (!existsError &&
            chainExists)
        {
            g_realDInput =
                LoadLibraryW(
                    chainPath.c_str());
        }

        if (g_realDInput == nullptr)
        {
            std::wstring systemDirectory(
                32768,
                L'\0');

            const UINT written =
                GetSystemDirectoryW(
                    systemDirectory.data(),
                    static_cast<UINT>(
                        systemDirectory.size()));

            if (written == 0 ||
                static_cast<std::size_t>(written) >=
                    systemDirectory.size())
            {
                return;
            }

            systemDirectory.resize(written);

            const fs::path systemDInput =
                fs::path(systemDirectory) /
                L"dinput8.dll";

            g_realDInput =
                LoadLibraryW(
                    systemDInput.c_str());
        }

        if (g_realDInput == nullptr)
        {
            return;
        }

        g_directInput8Create =
            reinterpret_cast<
                DirectInput8CreateFn>(
                    GetProcAddress(
                        g_realDInput,
                        "DirectInput8Create"));

        g_dllCanUnloadNow =
            reinterpret_cast<
                DllCanUnloadNowFn>(
                    GetProcAddress(
                        g_realDInput,
                        "DllCanUnloadNow"));

        g_dllGetClassObject =
            reinterpret_cast<
                DllGetClassObjectFn>(
                    GetProcAddress(
                        g_realDInput,
                        "DllGetClassObject"));

        g_dllRegisterServer =
            reinterpret_cast<
                DllRegisterServerFn>(
                    GetProcAddress(
                        g_realDInput,
                        "DllRegisterServer"));

        g_dllUnregisterServer =
            reinterpret_cast<
                DllUnregisterServerFn>(
                    GetProcAddress(
                        g_realDInput,
                        "DllUnregisterServer"));

        g_getdfDIJoystick =
            reinterpret_cast<
                GetdfDIJoystickFn>(
                    GetProcAddress(
                        g_realDInput,
                        "GetdfDIJoystick"));
    }

    void EnsureRealDInput()
    {
        std::call_once(
            g_realOnce,
            ResolveRealDInput);
    }

    void LoadGzfx()
    {
        const fs::path proxyPath =
            GetModulePath(g_proxyModule);

        if (proxyPath.empty())
        {
            return;
        }

        const fs::path gzfxPath =
            proxyPath.parent_path() /
            L"GZFX.dll";

        g_gzfxModule =
            LoadLibraryW(
                gzfxPath.c_str());

        if (g_gzfxModule == nullptr)
        {
            return;
        }

        const auto initialize =
            reinterpret_cast<
                GzfxInitializeFn>(
                    GetProcAddress(
                        g_gzfxModule,
                        "GZFX_Initialize"));

        g_isMenuOpen =
            reinterpret_cast<
                GzfxIsMenuOpenFn>(
                    GetProcAddress(
                        g_gzfxModule,
                        "GZFX_IsSettingsMenuOpen"));

        g_submitMouseInput =
            reinterpret_cast<
                GzfxSubmitMouseInputFn>(
                    GetProcAddress(
                        g_gzfxModule,
                        "GZFX_SubmitMouseInput"));

        if (initialize != nullptr)
        {
            initialize();
        }
    }

    void EnsureGzfx()
    {
        std::call_once(
            g_gzfxOnce,
            LoadGzfx);
    }

    void InitializeMinHook()
    {
        const MH_STATUS result =
            MH_Initialize();

        (void)result;
    }

    void EnsureMinHook()
    {
        std::call_once(
            g_minHookOnce,
            InitializeMinHook);
    }

    bool MenuOpen()
    {
        return
            g_isMenuOpen != nullptr &&
            g_isMenuOpen();
    }

    std::uint32_t MouseButtonMask(
        const BYTE* buttons,
        const std::size_t count)
    {
        if (buttons == nullptr)
        {
            return 0;
        }

        std::uint32_t mask = 0;

        const std::size_t capped =
            (std::min)(
                count,
                static_cast<std::size_t>(8));

        for (std::size_t i = 0;
             i < capped;
             ++i)
        {
            if ((buttons[i] & 0x80u) != 0u)
            {
                mask |=
                    1u <<
                    static_cast<std::uint32_t>(i);
            }
        }

        return mask;
    }

    void SubmitMouse(
        const LONG x,
        const LONG y,
        const LONG wheel,
        const std::uint32_t buttons)
    {
        if (g_submitMouseInput != nullptr)
        {
            g_submitMouseInput(
                x,
                y,
                wheel,
                buttons);
        }
    }

    HRESULT STDMETHODCALLTYPE HookGetDeviceState(
        void* self,
        const DWORD byteCount,
        LPVOID data)
    {
        if (g_originalGetDeviceState == nullptr)
        {
            return DIERR_GENERIC;
        }

        const HRESULT result =
            g_originalGetDeviceState(
                self,
                byteCount,
                data);

        if (FAILED(result) ||
            data == nullptr ||
            !MenuOpen())
        {
            return result;
        }

        LONG x = 0;
        LONG y = 0;
        LONG wheel = 0;
        std::uint32_t buttons = 0;

        if (byteCount >=
            sizeof(DIMOUSESTATE2))
        {
            const auto* state =
                static_cast<
                    const DIMOUSESTATE2*>(
                        data);

            x = state->lX;
            y = state->lY;
            wheel = state->lZ;

            buttons =
                MouseButtonMask(
                    state->rgbButtons,
                    std::size(
                        state->rgbButtons));
        }
        else if (byteCount >=
                 sizeof(DIMOUSESTATE))
        {
            const auto* state =
                static_cast<
                    const DIMOUSESTATE*>(
                        data);

            x = state->lX;
            y = state->lY;
            wheel = state->lZ;

            buttons =
                MouseButtonMask(
                    state->rgbButtons,
                    std::size(
                        state->rgbButtons));
        }

        SubmitMouse(
            x,
            y,
            wheel,
            buttons);

        g_lastStateSubmitTick.store(
            GetTickCount64(),
            std::memory_order_relaxed);

        std::memset(
            data,
            0,
            byteCount);

        return result;
    }

    HRESULT STDMETHODCALLTYPE HookGetDeviceData(
        void* self,
        const DWORD objectDataSize,
        LPDIDEVICEOBJECTDATA objectData,
        LPDWORD inOutCount,
        const DWORD flags)
    {
        if (g_originalGetDeviceData == nullptr)
        {
            return DIERR_GENERIC;
        }

        const HRESULT result =
            g_originalGetDeviceData(
                self,
                objectDataSize,
                objectData,
                inOutCount,
                flags);

        if (FAILED(result) ||
            inOutCount == nullptr ||
            !MenuOpen())
        {
            return result;
        }

        const DWORD count =
            *inOutCount;

        if (objectData != nullptr &&
            count > 0 &&
            (flags & DIGDD_PEEK) == 0)
        {
            LONG x = 0;
            LONG y = 0;
            LONG wheel = 0;

            std::uint32_t buttons =
                g_bufferedButtonMask.load(
                    std::memory_order_relaxed);

            if (objectDataSize >=
                sizeof(DWORD) * 2)
            {
                const auto* bytes =
                    reinterpret_cast<
                        const std::uint8_t*>(
                            objectData);

                for (DWORD i = 0;
                     i < count;
                     ++i)
                {
                    const auto* eventBytes =
                        bytes +
                        static_cast<std::size_t>(i) *
                        objectDataSize;

                    DWORD offset = 0;
                    DWORD value = 0;

                    std::memcpy(
                        &offset,
                        eventBytes,
                        sizeof(offset));

                    std::memcpy(
                        &value,
                        eventBytes + sizeof(DWORD),
                        sizeof(value));

                    if (offset == DIMOFS_X)
                    {
                        x +=
                            static_cast<LONG>(
                                value);
                    }
                    else if (offset == DIMOFS_Y)
                    {
                        y +=
                            static_cast<LONG>(
                                value);
                    }
                    else if (offset == DIMOFS_Z)
                    {
                        wheel +=
                            static_cast<LONG>(
                                value);
                    }
                    else if (offset >=
                                 DIMOFS_BUTTON0 &&
                             offset <=
                                 DIMOFS_BUTTON7)
                    {
                        const std::uint32_t index =
                            static_cast<std::uint32_t>(
                                offset -
                                DIMOFS_BUTTON0);

                        const std::uint32_t bit =
                            1u << index;

                        if ((value & 0x80u) != 0u)
                        {
                            buttons |= bit;
                        }
                        else
                        {
                            buttons &= ~bit;
                        }
                    }
                }
            }

            g_bufferedButtonMask.store(
                buttons,
                std::memory_order_relaxed);

            const ULONGLONG now =
                GetTickCount64();

            const ULONGLONG lastState =
                g_lastStateSubmitTick.load(
                    std::memory_order_relaxed);

            if (now - lastState > 8)
            {
                SubmitMouse(
                    x,
                    y,
                    wheel,
                    buttons);
            }
        }

        *inOutCount = 0;
        return result;
    }

    bool InstallMouseHooks(
        void* deviceObject)
    {
        if (deviceObject == nullptr)
        {
            return false;
        }

        EnsureMinHook();

        std::scoped_lock lock(
            g_hookMutex);

        auto** vtable =
            *reinterpret_cast<
                void***>(
                    deviceObject);

        if (vtable == nullptr)
        {
            return false;
        }

        void* stateTarget =
            vtable[9];

        void* dataTarget =
            vtable[10];

        bool success = true;

        if (g_getDeviceStateTarget == nullptr)
        {
            void* original = nullptr;

            const MH_STATUS create =
                MH_CreateHook(
                    stateTarget,
                    FunctionAddress(
                        &HookGetDeviceState),
                    &original);

            if (create == MH_OK)
            {
                g_originalGetDeviceState =
                    reinterpret_cast<
                        DirectInputGetDeviceStateFn>(
                            original);

                if (MH_EnableHook(
                        stateTarget) == MH_OK)
                {
                    g_getDeviceStateTarget =
                        stateTarget;
                }
                else
                {
                    success = false;
                }
            }
            else
            {
                success = false;
            }
        }

        if (g_getDeviceDataTarget == nullptr)
        {
            void* original = nullptr;

            const MH_STATUS create =
                MH_CreateHook(
                    dataTarget,
                    FunctionAddress(
                        &HookGetDeviceData),
                    &original);

            if (create == MH_OK)
            {
                g_originalGetDeviceData =
                    reinterpret_cast<
                        DirectInputGetDeviceDataFn>(
                            original);

                if (MH_EnableHook(
                        dataTarget) == MH_OK)
                {
                    g_getDeviceDataTarget =
                        dataTarget;
                }
                else
                {
                    success = false;
                }
            }
            else
            {
                success = false;
            }
        }

        return success;
    }

    HRESULT STDMETHODCALLTYPE HookCreateDevice(
        void* self,
        REFGUID deviceGuid,
        void** device,
        LPUNKNOWN outer)
    {
        if (g_originalCreateDevice == nullptr)
        {
            return DIERR_GENERIC;
        }

        const HRESULT result =
            g_originalCreateDevice(
                self,
                deviceGuid,
                device,
                outer);

        if (SUCCEEDED(result) &&
            device != nullptr &&
            *device != nullptr &&
            IsEqualGUID(
                deviceGuid,
                GUID_SysMouse))
        {
            InstallMouseHooks(
                *device);
        }

        return result;
    }

    bool InstallCreateDeviceHook(
        void* directInputObject)
    {
        if (directInputObject == nullptr)
        {
            return false;
        }

        EnsureMinHook();

        std::scoped_lock lock(
            g_hookMutex);

        if (g_createDeviceTarget != nullptr)
        {
            return true;
        }

        auto** vtable =
            *reinterpret_cast<
                void***>(
                    directInputObject);

        if (vtable == nullptr)
        {
            return false;
        }

        void* target =
            vtable[3];

        void* original = nullptr;

        const MH_STATUS create =
            MH_CreateHook(
                target,
                FunctionAddress(
                    &HookCreateDevice),
                &original);

        if (create != MH_OK)
        {
            return false;
        }

        g_originalCreateDevice =
            reinterpret_cast<
                DirectInputCreateDeviceFn>(
                    original);

        if (MH_EnableHook(
                target) != MH_OK)
        {
            return false;
        }

        g_createDeviceTarget =
            target;

        return true;
    }

    void Prepare() noexcept
    {
        try
        {
            EnsureRealDInput();
            EnsureGzfx();
        }
        catch (...)
        {
        }
    }
}

extern "C" HRESULT WINAPI DirectInput8Create(
    HINSTANCE instance,
    DWORD version,
    REFIID interfaceId,
    LPVOID* output,
    LPUNKNOWN outer)
{
    Prepare();

    if (g_directInput8Create == nullptr)
    {
        return E_FAIL;
    }

    const HRESULT result =
        g_directInput8Create(
            instance,
            version,
            interfaceId,
            output,
            outer);

    if (SUCCEEDED(result) &&
        output != nullptr &&
        *output != nullptr)
    {
        InstallCreateDeviceHook(
            *output);
    }

    return result;
}

extern "C" HRESULT WINAPI DllCanUnloadNow()
{
    Prepare();

    return g_dllCanUnloadNow != nullptr
        ? g_dllCanUnloadNow()
        : E_FAIL;
}

extern "C" HRESULT WINAPI DllGetClassObject(
    REFCLSID classId,
    REFIID interfaceId,
    LPVOID* output)
{
    Prepare();

    return g_dllGetClassObject != nullptr
        ? g_dllGetClassObject(
            classId,
            interfaceId,
            output)
        : E_FAIL;
}

extern "C" HRESULT WINAPI DllRegisterServer()
{
    Prepare();

    return g_dllRegisterServer != nullptr
        ? g_dllRegisterServer()
        : E_FAIL;
}

extern "C" HRESULT WINAPI DllUnregisterServer()
{
    Prepare();

    return g_dllUnregisterServer != nullptr
        ? g_dllUnregisterServer()
        : E_FAIL;
}

extern "C" LPCDIDATAFORMAT WINAPI GetdfDIJoystick()
{
    Prepare();

    return g_getdfDIJoystick != nullptr
        ? g_getdfDIJoystick()
        : nullptr;
}

BOOL APIENTRY DllMain(
    HMODULE module,
    DWORD reason,
    LPVOID reserved)
{
    (void)reserved;

    if (reason ==
        DLL_PROCESS_ATTACH)
    {
        g_proxyModule = module;

        DisableThreadLibraryCalls(
            module);
    }

    return TRUE;
}

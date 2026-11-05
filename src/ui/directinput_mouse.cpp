#include "gzfx/ui/directinput_mouse.hpp"

#include "gzfx/paths.hpp"
#include "gzfx/ui/settings_menu.hpp"

#include <MinHook.h>

#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>

namespace gzfx::ui::directinput_mouse
{
    namespace fs = std::filesystem;

    namespace
    {
        using DirectInput8CreateFn =
            HRESULT(WINAPI*)(
                HINSTANCE,
                DWORD,
                REFIID,
                LPVOID*,
                LPUNKNOWN);

        using DirectInputCreateDeviceFn =
            HRESULT(STDMETHODCALLTYPE*)(
                void*,
                REFGUID,
                void**,
                LPUNKNOWN);

        using DirectInputGetDeviceStateFn =
            HRESULT(STDMETHODCALLTYPE*)(
                void*,
                DWORD,
                LPVOID);

        using DirectInputGetDeviceDataFn =
            HRESULT(STDMETHODCALLTYPE*)(
                void*,
                DWORD,
                LPDIDEVICEOBJECTDATA,
                LPDWORD,
                DWORD);

        std::once_flag g_armOnce;
        std::mutex g_hookMutex;
        std::mutex g_diagnosticMutex;

        std::atomic<bool> g_armResult{ false };
        std::string g_lastError;

        DirectInput8CreateFn
            g_originalDirectInput8Create =
                nullptr;

        DirectInputCreateDeviceFn
            g_originalCreateDevice =
                nullptr;

        DirectInputGetDeviceStateFn
            g_originalGetDeviceState =
                nullptr;

        DirectInputGetDeviceDataFn
            g_originalGetDeviceData =
                nullptr;

        std::atomic<bool> g_armAttempted{ false };
        std::atomic<bool>
            g_directInput8CreateHooked{ false };
        std::atomic<bool>
            g_createDeviceHooked{ false };
        std::atomic<bool>
            g_systemMouseSeen{ false };
        std::atomic<bool>
            g_getDeviceStateHooked{ false };
        std::atomic<bool>
            g_getDeviceDataHooked{ false };

        std::atomic<std::uintptr_t>
            g_dinput8Module{ 0 };
        std::atomic<std::uintptr_t>
            g_directInput8CreateTarget{ 0 };
        std::atomic<std::uintptr_t>
            g_mouseDevice{ 0 };

        std::atomic<std::uint64_t>
            g_directInput8CreateCalls{ 0 };
        std::atomic<std::uint64_t>
            g_createDeviceCalls{ 0 };
        std::atomic<std::uint64_t>
            g_stateReads{ 0 };
        std::atomic<std::uint64_t>
            g_dataReads{ 0 };
        std::atomic<std::uint64_t>
            g_menuPacketsForwarded{ 0 };
        std::atomic<std::uint64_t>
            g_stateReadsSuppressed{ 0 };
        std::atomic<std::uint64_t>
            g_dataReadsSuppressed{ 0 };

        std::atomic<LONG> g_lastDeltaX{ 0 };
        std::atomic<LONG> g_lastDeltaY{ 0 };
        std::atomic<LONG> g_lastWheel{ 0 };
        std::atomic<std::uint32_t>
            g_lastButtonMask{ 0 };

        std::atomic<std::uint32_t>
            g_bufferedButtonMask{ 0 };

        std::atomic<ULONGLONG>
            g_lastStateSubmitTick{ 0 };

        void* g_createDeviceTarget = nullptr;
        void* g_getDeviceStateTarget = nullptr;
        void* g_getDeviceDataTarget = nullptr;

        template <class Function>
        void* FunctionAddress(
            Function function) noexcept
        {
            return reinterpret_cast<void*>(
                reinterpret_cast<std::uintptr_t>(
                    function));
        }

        void SetError(
            const std::string& value)
        {
            std::scoped_lock lock(
                g_diagnosticMutex);

            g_lastError = value;
        }

        const char* StatusName(
            const Status& status) noexcept
        {
            if (status.armed)
            {
                return "armed";
            }

            return
                g_armAttempted.load(
                    std::memory_order_acquire)
                    ? "failed"
                    : "disabled";
        }

        void WriteDiagnostic(
            const char* const event) noexcept
        {
            try
            {
                const Status status =
                    GetStatus();

                const fs::path root =
                    paths::DiagnosticsRoot();

                if (root.empty())
                {
                    return;
                }

                std::error_code error;
                fs::create_directories(
                    root,
                    error);

                if (error)
                {
                    return;
                }

                std::scoped_lock lock(
                    g_diagnosticMutex);

                std::ofstream out(
                    root /
                        L"directinput_mouse.txt",
                    std::ios::out |
                        std::ios::trunc);

                if (!out)
                {
                    return;
                }

                out
                    << "GZFX DirectInput mouse ownership\n"
                    << "event="
                    << (event != nullptr
                        ? event
                        : "unknown")
                    << "\n"
                    << "status="
                    << StatusName(status)
                    << "\n"
                    << "dinput8_module=0x"
                    << std::hex
                    << std::uppercase
                    << status.dinput8Module
                    << "\n"
                    << "directinput8_create_target=0x"
                    << status.directInput8CreateTarget
                    << std::dec
                    << "\n"
                    << "directinput8_create_hooked="
                    << (status.directInput8CreateHooked
                        ? "true"
                        : "false")
                    << "\n"
                    << "directinput8_create_calls="
                    << status.directInput8CreateCalls
                    << "\n"
                    << "create_device_hooked="
                    << (status.createDeviceHooked
                        ? "true"
                        : "false")
                    << "\n"
                    << "create_device_calls="
                    << status.createDeviceCalls
                    << "\n"
                    << "system_mouse_seen="
                    << (status.systemMouseSeen
                        ? "true"
                        : "false")
                    << "\n"
                    << "mouse_device=0x"
                    << std::hex
                    << std::uppercase
                    << status.mouseDevice
                    << std::dec
                    << "\n"
                    << "get_device_state_hooked="
                    << (status.getDeviceStateHooked
                        ? "true"
                        : "false")
                    << "\n"
                    << "get_device_data_hooked="
                    << (status.getDeviceDataHooked
                        ? "true"
                        : "false")
                    << "\n"
                    << "state_reads="
                    << status.stateReads
                    << "\n"
                    << "data_reads="
                    << status.dataReads
                    << "\n"
                    << "menu_packets_forwarded="
                    << status.menuPacketsForwarded
                    << "\n"
                    << "state_reads_suppressed="
                    << status.stateReadsSuppressed
                    << "\n"
                    << "data_reads_suppressed="
                    << status.dataReadsSuppressed
                    << "\n"
                    << "last_delta="
                    << status.lastDeltaX
                    << ","
                    << status.lastDeltaY
                    << "\n"
                    << "last_wheel="
                    << status.lastWheel
                    << "\n"
                    << "last_button_mask=0x"
                    << std::hex
                    << std::uppercase
                    << status.lastButtonMask
                    << std::dec
                    << "\n"
                    << "last_error="
                    << g_lastError
                    << "\n";
            }
            catch (...)
            {
            }
        }

        HMODULE ResolveDInputModule() noexcept
        {
            HMODULE module =
                GetModuleHandleW(
                    L"dinput8.dll");

            if (module != nullptr)
            {
                return module;
            }

            try
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
                    static_cast<std::size_t>(
                        written) >=
                        systemDirectory.size())
                {
                    return nullptr;
                }

                systemDirectory.resize(
                    written);

                const fs::path path =
                    fs::path(
                        systemDirectory) /
                    L"dinput8.dll";

                return
                    LoadLibraryW(
                        path.c_str());
            }
            catch (...)
            {
                return nullptr;
            }
        }

        bool InstallCreateDeviceHook(
            void* directInputObject) noexcept;

        bool InstallMouseHooks(
            void* deviceObject) noexcept;

        HRESULT STDMETHODCALLTYPE
        HookGetDeviceState(
            void* self,
            const DWORD byteCount,
            LPVOID data)
        {
            if (g_originalGetDeviceState ==
                nullptr)
            {
                return DIERR_GENERIC;
            }

            const HRESULT result =
                g_originalGetDeviceState(
                    self,
                    byteCount,
                    data);

            if (FAILED(result) ||
                data == nullptr)
            {
                return result;
            }

            if (reinterpret_cast<
                    std::uintptr_t>(self) !=
                    g_mouseDevice.load(
                        std::memory_order_acquire))
            {
                return result;
            }

            ++g_stateReads;

            if (!gzfx::ui::IsMenuVisible())
            {
                return result;
            }

            MousePacket packet{};

            if (!detail::
                    DecodeImmediateState(
                        byteCount,
                        data,
                        &packet))
            {
                return result;
            }

            g_bufferedButtonMask.store(
                packet.buttons,
                std::memory_order_release);

            gzfx::ui::SubmitDirectInputMouse(
                packet.x,
                packet.y,
                packet.wheel,
                packet.buttons);

            g_lastStateSubmitTick.store(
                GetTickCount64(),
                std::memory_order_release);

            g_lastDeltaX.store(
                packet.x,
                std::memory_order_release);
            g_lastDeltaY.store(
                packet.y,
                std::memory_order_release);
            g_lastWheel.store(
                packet.wheel,
                std::memory_order_release);
            g_lastButtonMask.store(
                packet.buttons,
                std::memory_order_release);

            const std::uint64_t forwarded =
                ++g_menuPacketsForwarded;

            const std::uint64_t suppressed =
                ++g_stateReadsSuppressed;

            std::memset(
                data,
                0,
                byteCount);

            if (forwarded == 1 ||
                (forwarded % 120) == 0 ||
                suppressed == 1)
            {
                WriteDiagnostic(
                    "immediate_menu_packet");
            }

            return result;
        }

        HRESULT STDMETHODCALLTYPE
        HookGetDeviceData(
            void* self,
            const DWORD objectDataSize,
            LPDIDEVICEOBJECTDATA objectData,
            LPDWORD inOutCount,
            const DWORD flags)
        {
            if (g_originalGetDeviceData ==
                nullptr)
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
                inOutCount == nullptr)
            {
                return result;
            }

            if (reinterpret_cast<
                    std::uintptr_t>(self) !=
                    g_mouseDevice.load(
                        std::memory_order_acquire))
            {
                return result;
            }

            ++g_dataReads;

            if (!gzfx::ui::IsMenuVisible() ||
                (flags & DIGDD_PEEK) != 0 ||
                objectDataSize !=
                    sizeof(DIDEVICEOBJECTDATA))
            {
                return result;
            }

            const DWORD count =
                *inOutCount;

            MousePacket packet{};
            std::uint32_t nextButtons =
                g_bufferedButtonMask.load(
                    std::memory_order_acquire);

            if (!detail::
                    DecodeBufferedEvents(
                        objectDataSize,
                        objectData,
                        count,
                        nextButtons,
                        &packet,
                        &nextButtons))
            {
                return result;
            }

            g_bufferedButtonMask.store(
                nextButtons,
                std::memory_order_release);

            const ULONGLONG now =
                GetTickCount64();

            const ULONGLONG lastState =
                g_lastStateSubmitTick.load(
                    std::memory_order_acquire);

            bool forwarded = false;

            if (now - lastState > 8)
            {
                gzfx::ui::SubmitDirectInputMouse(
                    packet.x,
                    packet.y,
                    packet.wheel,
                    packet.buttons);

                g_lastDeltaX.store(
                    packet.x,
                    std::memory_order_release);
                g_lastDeltaY.store(
                    packet.y,
                    std::memory_order_release);
                g_lastWheel.store(
                    packet.wheel,
                    std::memory_order_release);
                g_lastButtonMask.store(
                    packet.buttons,
                    std::memory_order_release);

                ++g_menuPacketsForwarded;
                forwarded = true;
            }

            *inOutCount = 0;

            const std::uint64_t suppressed =
                ++g_dataReadsSuppressed;

            if ((forwarded &&
                 g_menuPacketsForwarded.load(
                    std::memory_order_acquire) == 1) ||
                suppressed == 1 ||
                (suppressed % 120) == 0)
            {
                WriteDiagnostic(
                    "buffered_menu_packet");
            }

            return result;
        }

        bool InstallMouseHooks(
            void* const deviceObject) noexcept
        {
            if (deviceObject == nullptr)
            {
                return false;
            }

            bool stateOk = false;
            bool dataOk = false;

            {
                std::scoped_lock lock(
                    g_hookMutex);

                auto** const vtable =
                    *reinterpret_cast<
                        void***>(
                            deviceObject);

                if (vtable == nullptr)
                {
                    SetError(
                        "mouse_vtable_missing");
                    return false;
                }

                void* const stateTarget =
                    vtable[9];

                void* const dataTarget =
                    vtable[10];

                if (g_getDeviceStateTarget ==
                    nullptr)
                {
                    void* original = nullptr;

                    if (MH_CreateHook(
                            stateTarget,
                            FunctionAddress(
                                &HookGetDeviceState),
                            &original) ==
                            MH_OK &&
                        MH_EnableHook(
                            stateTarget) ==
                            MH_OK)
                    {
                        g_originalGetDeviceState =
                            reinterpret_cast<
                                DirectInputGetDeviceStateFn>(
                                    original);

                        g_getDeviceStateTarget =
                            stateTarget;

                        g_getDeviceStateHooked.store(
                            true,
                            std::memory_order_release);
                    }
                }

                if (g_getDeviceDataTarget ==
                    nullptr)
                {
                    void* original = nullptr;

                    if (MH_CreateHook(
                            dataTarget,
                            FunctionAddress(
                                &HookGetDeviceData),
                            &original) ==
                            MH_OK &&
                        MH_EnableHook(
                            dataTarget) ==
                            MH_OK)
                    {
                        g_originalGetDeviceData =
                            reinterpret_cast<
                                DirectInputGetDeviceDataFn>(
                                    original);

                        g_getDeviceDataTarget =
                            dataTarget;

                        g_getDeviceDataHooked.store(
                            true,
                            std::memory_order_release);
                    }
                }

                stateOk =
                    g_getDeviceStateHooked.load(
                        std::memory_order_acquire);

                dataOk =
                    g_getDeviceDataHooked.load(
                        std::memory_order_acquire);
            }

            if (!stateOk && !dataOk)
            {
                SetError(
                    "mouse_method_hooks_failed");
            }

            WriteDiagnostic(
                stateOk || dataOk
                    ? "mouse_hooks_installed"
                    : "mouse_hooks_failed");

            return stateOk || dataOk;
        }

        HRESULT STDMETHODCALLTYPE
        HookCreateDevice(
            void* self,
            REFGUID deviceGuid,
            void** device,
            LPUNKNOWN outer)
        {
            if (g_originalCreateDevice ==
                nullptr)
            {
                return DIERR_GENERIC;
            }

            ++g_createDeviceCalls;

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
                g_systemMouseSeen.store(
                    true,
                    std::memory_order_release);

                g_mouseDevice.store(
                    reinterpret_cast<
                        std::uintptr_t>(
                            *device),
                    std::memory_order_release);

                InstallMouseHooks(
                    *device);

                WriteDiagnostic(
                    "system_mouse_seen");
            }

            return result;
        }

        bool InstallCreateDeviceHook(
            void* const directInputObject) noexcept
        {
            if (directInputObject == nullptr)
            {
                return false;
            }

            bool hooked = false;

            {
                std::scoped_lock lock(
                    g_hookMutex);

                if (g_createDeviceTarget !=
                    nullptr)
                {
                    return true;
                }

                auto** const vtable =
                    *reinterpret_cast<
                        void***>(
                            directInputObject);

                if (vtable == nullptr)
                {
                    SetError(
                        "directinput_vtable_missing");
                    return false;
                }

                void* const target =
                    vtable[3];

                void* original = nullptr;

                if (MH_CreateHook(
                        target,
                        FunctionAddress(
                            &HookCreateDevice),
                        &original) ==
                        MH_OK &&
                    MH_EnableHook(
                        target) ==
                        MH_OK)
                {
                    g_originalCreateDevice =
                        reinterpret_cast<
                            DirectInputCreateDeviceFn>(
                                original);

                    g_createDeviceTarget =
                        target;

                    g_createDeviceHooked.store(
                        true,
                        std::memory_order_release);

                    hooked = true;
                }
            }

            if (!hooked)
            {
                SetError(
                    "create_device_hook_failed");
            }

            WriteDiagnostic(
                hooked
                    ? "create_device_hooked"
                    : "create_device_hook_failed");

            return hooked;
        }

        HRESULT WINAPI
        HookDirectInput8Create(
            HINSTANCE instance,
            DWORD version,
            REFIID interfaceId,
            LPVOID* output,
            LPUNKNOWN outer)
        {
            if (g_originalDirectInput8Create ==
                nullptr)
            {
                return DIERR_GENERIC;
            }

            const std::uint64_t call =
                ++g_directInput8CreateCalls;

            const HRESULT result =
                g_originalDirectInput8Create(
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

            if (call == 1)
            {
                WriteDiagnostic(
                    "directinput8_create");
            }

            return result;
        }

        void ArmImpl() noexcept
        {
            g_armAttempted.store(
                true,
                std::memory_order_release);

            const HMODULE module =
                ResolveDInputModule();

            if (module == nullptr)
            {
                SetError(
                    "dinput8_module_missing");
                WriteDiagnostic(
                    "arm_failed");
                return;
            }

            g_dinput8Module.store(
                reinterpret_cast<
                    std::uintptr_t>(
                        module),
                std::memory_order_release);

            void* const target =
                reinterpret_cast<void*>(
                    GetProcAddress(
                        module,
                        "DirectInput8Create"));

            if (target == nullptr)
            {
                SetError(
                    "directinput8_create_missing");
                WriteDiagnostic(
                    "arm_failed");
                return;
            }

            g_directInput8CreateTarget.store(
                reinterpret_cast<
                    std::uintptr_t>(
                        target),
                std::memory_order_release);

            const MH_STATUS initialized =
                MH_Initialize();

            if (initialized != MH_OK &&
                initialized !=
                    MH_ERROR_ALREADY_INITIALIZED)
            {
                SetError(
                    "minhook_init_failed");
                WriteDiagnostic(
                    "arm_failed");
                return;
            }

            void* original = nullptr;

            if (MH_CreateHook(
                    target,
                    FunctionAddress(
                        &HookDirectInput8Create),
                    &original) !=
                    MH_OK)
            {
                SetError(
                    "directinput8_create_hook_failed");
                WriteDiagnostic(
                    "arm_failed");
                return;
            }

            g_originalDirectInput8Create =
                reinterpret_cast<
                    DirectInput8CreateFn>(
                        original);

            if (MH_EnableHook(
                    target) != MH_OK)
            {
                g_originalDirectInput8Create =
                    nullptr;

                SetError(
                    "directinput8_create_enable_failed");
                WriteDiagnostic(
                    "arm_failed");
                return;
            }

            g_directInput8CreateHooked.store(
                true,
                std::memory_order_release);

            g_armResult.store(
                true,
                std::memory_order_release);

            WriteDiagnostic(
                "armed");
        }
    }

    bool Arm() noexcept
    {
        try
        {
            std::call_once(
                g_armOnce,
                ArmImpl);

            return
                g_armResult.load(
                    std::memory_order_acquire);
        }
        catch (...)
        {
            SetError(
                "arm_exception");

            WriteDiagnostic(
                "arm_exception");

            return false;
        }
    }

    Status GetStatus() noexcept
    {
        Status status{};

        status.armed =
            g_armResult.load(
                std::memory_order_acquire);

        status.directInput8CreateHooked =
            g_directInput8CreateHooked.load(
                std::memory_order_acquire);

        status.createDeviceHooked =
            g_createDeviceHooked.load(
                std::memory_order_acquire);

        status.systemMouseSeen =
            g_systemMouseSeen.load(
                std::memory_order_acquire);

        status.getDeviceStateHooked =
            g_getDeviceStateHooked.load(
                std::memory_order_acquire);

        status.getDeviceDataHooked =
            g_getDeviceDataHooked.load(
                std::memory_order_acquire);

        status.dinput8Module =
            g_dinput8Module.load(
                std::memory_order_acquire);

        status.directInput8CreateTarget =
            g_directInput8CreateTarget.load(
                std::memory_order_acquire);

        status.mouseDevice =
            g_mouseDevice.load(
                std::memory_order_acquire);

        status.directInput8CreateCalls =
            g_directInput8CreateCalls.load(
                std::memory_order_acquire);

        status.createDeviceCalls =
            g_createDeviceCalls.load(
                std::memory_order_acquire);

        status.stateReads =
            g_stateReads.load(
                std::memory_order_acquire);

        status.dataReads =
            g_dataReads.load(
                std::memory_order_acquire);

        status.menuPacketsForwarded =
            g_menuPacketsForwarded.load(
                std::memory_order_acquire);

        status.stateReadsSuppressed =
            g_stateReadsSuppressed.load(
                std::memory_order_acquire);

        status.dataReadsSuppressed =
            g_dataReadsSuppressed.load(
                std::memory_order_acquire);

        status.lastDeltaX =
            g_lastDeltaX.load(
                std::memory_order_acquire);

        status.lastDeltaY =
            g_lastDeltaY.load(
                std::memory_order_acquire);

        status.lastWheel =
            g_lastWheel.load(
                std::memory_order_acquire);

        status.lastButtonMask =
            g_lastButtonMask.load(
                std::memory_order_acquire);

        return status;
    }
}

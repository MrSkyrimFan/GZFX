#define DIRECTINPUT_VERSION 0x0800

#include <Windows.h>
#include <dinput.h>

#include <filesystem>
#include <mutex>
#include <string>

namespace
{
    namespace fs = std::filesystem;

    HMODULE g_proxyModule = nullptr;
    HMODULE g_realDInput = nullptr;
    HMODULE g_gzfxModule = nullptr;

    std::once_flag g_realOnce;
    std::once_flag g_gzfxOnce;

    using DirectInput8CreateFn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
    using DllCanUnloadNowFn = HRESULT(WINAPI*)();
    using DllGetClassObjectFn = HRESULT(WINAPI*)(REFCLSID, REFIID, LPVOID*);
    using DllRegisterServerFn = HRESULT(WINAPI*)();
    using DllUnregisterServerFn = HRESULT(WINAPI*)();
    using GetdfDIJoystickFn = LPCDIDATAFORMAT(WINAPI*)();
    using GzfxInitializeFn = bool (*)();

    DirectInput8CreateFn g_directInput8Create = nullptr;
    DllCanUnloadNowFn g_dllCanUnloadNow = nullptr;
    DllGetClassObjectFn g_dllGetClassObject = nullptr;
    DllRegisterServerFn g_dllRegisterServer = nullptr;
    DllUnregisterServerFn g_dllUnregisterServer = nullptr;
    GetdfDIJoystickFn g_getdfDIJoystick = nullptr;

    fs::path GetModulePath(HMODULE module)
    {
        std::wstring buffer(32768, L'\0');
        const DWORD written = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (written == 0 || written >= buffer.size())
        {
            return {};
        }

        buffer.resize(written);
        return fs::path(buffer);
    }

    void ResolveRealDInput()
    {
        const fs::path proxyPath = GetModulePath(g_proxyModule);
        if (proxyPath.empty())
        {
            return;
        }

        const fs::path chainPath = proxyPath.parent_path() / L"dinput8_gzfx_chain.dll";
        if (fs::exists(chainPath))
        {
            g_realDInput = LoadLibraryW(chainPath.c_str());
        }

        if (g_realDInput == nullptr)
        {
            std::wstring systemDirectory(32768, L'\0');
            const UINT written = GetSystemDirectoryW(systemDirectory.data(), static_cast<UINT>(systemDirectory.size()));
            if (written == 0 || written >= systemDirectory.size())
            {
                return;
            }

            systemDirectory.resize(written);
            const fs::path systemDInput = fs::path(systemDirectory) / L"dinput8.dll";
            g_realDInput = LoadLibraryW(systemDInput.c_str());
        }

        if (g_realDInput == nullptr)
        {
            return;
        }

        g_directInput8Create = reinterpret_cast<DirectInput8CreateFn>(
            GetProcAddress(g_realDInput, "DirectInput8Create"));
        g_dllCanUnloadNow = reinterpret_cast<DllCanUnloadNowFn>(
            GetProcAddress(g_realDInput, "DllCanUnloadNow"));
        g_dllGetClassObject = reinterpret_cast<DllGetClassObjectFn>(
            GetProcAddress(g_realDInput, "DllGetClassObject"));
        g_dllRegisterServer = reinterpret_cast<DllRegisterServerFn>(
            GetProcAddress(g_realDInput, "DllRegisterServer"));
        g_dllUnregisterServer = reinterpret_cast<DllUnregisterServerFn>(
            GetProcAddress(g_realDInput, "DllUnregisterServer"));
        g_getdfDIJoystick = reinterpret_cast<GetdfDIJoystickFn>(
            GetProcAddress(g_realDInput, "GetdfDIJoystick"));
    }

    void EnsureRealDInput()
    {
        std::call_once(g_realOnce, ResolveRealDInput);
    }

    void LoadGzfx()
    {
        const fs::path proxyPath = GetModulePath(g_proxyModule);
        if (proxyPath.empty())
        {
            return;
        }

        const fs::path gzfxPath = proxyPath.parent_path() / L"GZFX.dll";
        g_gzfxModule = LoadLibraryW(gzfxPath.c_str());
        if (g_gzfxModule == nullptr)
        {
            return;
        }

        const auto initialize = reinterpret_cast<GzfxInitializeFn>(
            GetProcAddress(g_gzfxModule, "GZFX_Initialize"));
        if (initialize != nullptr)
        {
            initialize();
        }
    }

    void EnsureGzfx()
    {
        std::call_once(g_gzfxOnce, LoadGzfx);
    }

    void Prepare()
    {
        EnsureRealDInput();
        EnsureGzfx();
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

    return g_directInput8Create(instance, version, interfaceId, output, outer);
}

extern "C" HRESULT WINAPI DllCanUnloadNow()
{
    Prepare();
    return g_dllCanUnloadNow != nullptr ? g_dllCanUnloadNow() : E_FAIL;
}

extern "C" HRESULT WINAPI DllGetClassObject(REFCLSID classId, REFIID interfaceId, LPVOID* output)
{
    Prepare();
    return g_dllGetClassObject != nullptr
        ? g_dllGetClassObject(classId, interfaceId, output)
        : E_FAIL;
}

extern "C" HRESULT WINAPI DllRegisterServer()
{
    Prepare();
    return g_dllRegisterServer != nullptr ? g_dllRegisterServer() : E_FAIL;
}

extern "C" HRESULT WINAPI DllUnregisterServer()
{
    Prepare();
    return g_dllUnregisterServer != nullptr ? g_dllUnregisterServer() : E_FAIL;
}

extern "C" LPCDIDATAFORMAT WINAPI GetdfDIJoystick()
{
    Prepare();
    return g_getdfDIJoystick != nullptr ? g_getdfDIJoystick() : nullptr;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved)
{
    (void)reserved;

    if (reason == DLL_PROCESS_ATTACH)
    {
        g_proxyModule = module;
        DisableThreadLibraryCalls(module);
    }

    return TRUE;
}

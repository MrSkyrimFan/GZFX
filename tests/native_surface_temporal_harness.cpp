#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

using InitializeFn = bool (*)();

namespace
{
    LRESULT CALLBACK WindowProc(
        HWND hwnd,
        UINT message,
        WPARAM wParam,
        LPARAM lParam)
    {
        return DefWindowProcW(
            hwnd,
            message,
            wParam,
            lParam);
    }

    std::string ReadAll(
        const std::filesystem::path& path)
    {
        std::ifstream in(path);

        return std::string(
            std::istreambuf_iterator<char>(in),
            std::istreambuf_iterator<char>());
    }

    void SetIdentity(float* matrix)
    {
        for (int i = 0; i < 16; ++i)
        {
            matrix[i] = 0.0f;
        }

        matrix[0] = 1.0f;
        matrix[5] = 1.0f;
        matrix[10] = 1.0f;
        matrix[15] = 1.0f;
    }
}

int wmain(
    int argc,
    wchar_t** argv)
{
    if (argc != 3)
    {
        return 2;
    }

    const std::filesystem::path dllPath(argv[1]);
    const std::filesystem::path outputRoot(argv[2]);

    std::error_code error;
    std::filesystem::remove_all(
        outputRoot,
        error);

    error.clear();

    std::filesystem::create_directories(
        outputRoot,
        error);

    if (error ||
        !SetEnvironmentVariableW(
            L"GZFX_TEST_OUTPUT_ROOT",
            outputRoot.c_str()) ||
        !SetEnvironmentVariableW(
            L"GZFX_TEST_FAST_CAPTURE",
            L"1"))
    {
        return 3;
    }

    const HMODULE gzfx =
        LoadLibraryW(
            dllPath.c_str());

    if (!gzfx)
    {
        return 4;
    }

    const auto initialize =
        reinterpret_cast<InitializeFn>(
            GetProcAddress(
                gzfx,
                "GZFX_Initialize"));

    if (!initialize ||
        !initialize())
    {
        return 5;
    }

    const wchar_t* className =
        L"GZFX.NativeTemporalHarness";

    WNDCLASSEXW windowClass{};
    windowClass.cbSize =
        sizeof(windowClass);

    windowClass.lpfnWndProc =
        WindowProc;

    windowClass.hInstance =
        GetModuleHandleW(nullptr);

    windowClass.lpszClassName =
        className;

    if (!RegisterClassExW(
            &windowClass) &&
        GetLastError() !=
            ERROR_CLASS_ALREADY_EXISTS)
    {
        return 6;
    }

    const HWND window =
        CreateWindowExW(
            0,
            className,
            L"GZFX native temporal",
            WS_OVERLAPPEDWINDOW,
            0,
            0,
            640,
            360,
            nullptr,
            nullptr,
            windowClass.hInstance,
            nullptr);

    if (!window)
    {
        return 7;
    }

    DXGI_SWAP_CHAIN_DESC swapDesc{};
    swapDesc.BufferDesc.Width = 640;
    swapDesc.BufferDesc.Height = 360;
    swapDesc.BufferDesc.Format =
        DXGI_FORMAT_R8G8B8A8_UNORM;
    swapDesc.SampleDesc.Count = 1;
    swapDesc.BufferUsage =
        DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDesc.BufferCount = 2;
    swapDesc.OutputWindow = window;
    swapDesc.Windowed = TRUE;
    swapDesc.SwapEffect =
        DXGI_SWAP_EFFECT_DISCARD;

    IDXGISwapChain* swap = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    D3D_FEATURE_LEVEL level{};

    if (FAILED(
            D3D11CreateDeviceAndSwapChain(
                nullptr,
                D3D_DRIVER_TYPE_WARP,
                nullptr,
                0,
                nullptr,
                0,
                D3D11_SDK_VERSION,
                &swapDesc,
                &swap,
                &device,
                &level,
                &context)))
    {
        return 8;
    }

    ID3D11Texture2D* backbuffer = nullptr;
    ID3D11RenderTargetView* backRtv = nullptr;

    if (FAILED(
            swap->GetBuffer(
                0,
                __uuidof(ID3D11Texture2D),
                reinterpret_cast<void**>(
                    &backbuffer))) ||
        FAILED(
            device->CreateRenderTargetView(
                backbuffer,
                nullptr,
                &backRtv)))
    {
        return 9;
    }

    auto createTarget =
        [&](DXGI_FORMAT format,
            ID3D11Texture2D** texture,
            ID3D11RenderTargetView** rtv)
        {
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = 640;
            desc.Height = 360;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = format;
            desc.SampleDesc.Count = 1;
            desc.Usage =
                D3D11_USAGE_DEFAULT;
            desc.BindFlags =
                D3D11_BIND_RENDER_TARGET |
                D3D11_BIND_SHADER_RESOURCE;

            return
                SUCCEEDED(
                    device->CreateTexture2D(
                        &desc,
                        nullptr,
                        texture)) &&
                SUCCEEDED(
                    device->CreateRenderTargetView(
                        *texture,
                        nullptr,
                        rtv));
        };

    ID3D11Texture2D* scene = nullptr;
    ID3D11RenderTargetView* sceneRtv = nullptr;

    ID3D11Texture2D* motion = nullptr;
    ID3D11RenderTargetView* motionRtv = nullptr;

    ID3D11Texture2D* normal = nullptr;
    ID3D11RenderTargetView* normalRtv = nullptr;

    ID3D11Texture2D* aux = nullptr;
    ID3D11RenderTargetView* auxRtv = nullptr;

    if (!createTarget(
            DXGI_FORMAT_R16G16B16A16_FLOAT,
            &scene,
            &sceneRtv) ||
        !createTarget(
            DXGI_FORMAT_R16G16_FLOAT,
            &motion,
            &motionRtv) ||
        !createTarget(
            DXGI_FORMAT_R8G8B8A8_UNORM,
            &normal,
            &normalRtv) ||
        !createTarget(
            DXGI_FORMAT_R8G8_UNORM,
            &aux,
            &auxRtv))
    {
        return 10;
    }

    D3D11_TEXTURE2D_DESC depthDesc{};
    depthDesc.Width = 640;
    depthDesc.Height = 360;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format =
        DXGI_FORMAT_R24G8_TYPELESS;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Usage =
        D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags =
        D3D11_BIND_DEPTH_STENCIL |
        D3D11_BIND_SHADER_RESOURCE;

    ID3D11Texture2D* depth = nullptr;

    if (FAILED(
            device->CreateTexture2D(
                &depthDesc,
                nullptr,
                &depth)))
    {
        return 11;
    }

    D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format =
        DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsvDesc.ViewDimension =
        D3D11_DSV_DIMENSION_TEXTURE2D;

    ID3D11DepthStencilView* dsv = nullptr;

    if (FAILED(
            device->CreateDepthStencilView(
                depth,
                &dsvDesc,
                &dsv)))
    {
        return 12;
    }

    std::array<float, 180> camera{};

    for (int matrix = 0;
         matrix < 10;
         ++matrix)
    {
        SetIdentity(
            camera.data() +
            matrix * 16);
    }

    camera[168] = 1.0f;
    camera[169] = 1.0f;
    camera[170] = 1.0f;
    camera[171] = 1.0f;

    D3D11_BUFFER_DESC cameraDesc{};
    cameraDesc.ByteWidth =
        static_cast<UINT>(
            camera.size() *
            sizeof(float));
    cameraDesc.Usage =
        D3D11_USAGE_DEFAULT;
    cameraDesc.BindFlags =
        D3D11_BIND_CONSTANT_BUFFER;

    D3D11_SUBRESOURCE_DATA cameraData{};
    cameraData.pSysMem =
        camera.data();

    ID3D11Buffer* cameraBuffer = nullptr;

    if (FAILED(
            device->CreateBuffer(
                &cameraDesc,
                &cameraData,
                &cameraBuffer)))
    {
        return 13;
    }

    // Acquire GZFX's swap chain/context and arm the fast test path.
    swap->Present(0, 0);

    const FLOAT sceneClear[4] =
    {
        1.6f,
        0.65f,
        0.20f,
        1.0f
    };

    const FLOAT motionClear[4] =
    {
        0.0f,
        0.0f,
        0.0f,
        0.0f
    };

    // Vanilla-normal encoding of a view-facing normal is centered in RG.
    const FLOAT normalClear[4] =
    {
        0.5f,
        0.5f,
        0.0f,
        1.0f
    };

    const FLOAT auxClear[4] =
    {
        0.25f,
        0.75f,
        0.0f,
        0.0f
    };

    auto runSceneFrame =
        [&]()
        {
            context->ClearRenderTargetView(
                sceneRtv,
                sceneClear);

            context->ClearRenderTargetView(
                motionRtv,
                motionClear);

            context->ClearRenderTargetView(
                normalRtv,
                normalClear);

            context->ClearRenderTargetView(
                auxRtv,
                auxClear);

            context->ClearDepthStencilView(
                dsv,
                D3D11_CLEAR_DEPTH |
                    D3D11_CLEAR_STENCIL,
                0.45f,
                0);

            context->VSSetConstantBuffers(
                12,
                1,
                &cameraBuffer);

            context->PSSetConstantBuffers(
                12,
                1,
                &cameraBuffer);

            ID3D11RenderTargetView*
                mainTargets[4] =
            {
                sceneRtv,
                motionRtv,
                normalRtv,
                auxRtv
            };

            for (int i = 0;
                 i < 10;
                 ++i)
            {
                context->OMSetRenderTargets(
                    4,
                    mainTargets,
                    dsv);

                context->DrawIndexed(
                    3,
                    0,
                    0);
            }

            for (int i = 0;
                 i < 25;
                 ++i)
            {
                context->DrawIndexed(
                    3,
                    0,
                    0);
            }

            context->OMSetRenderTargets(
                1,
                &sceneRtv,
                nullptr);

            context->DrawIndexed(
                3,
                0,
                0);

            // Leaving terminal color-only state triggers the pre-UI stack.
            context->OMSetRenderTargets(
                1,
                &backRtv,
                nullptr);
        };

    runSceneFrame();
    swap->Present(0, 0);

    runSceneFrame();
    swap->Present(0, 0);

    runSceneFrame();
    swap->Present(0, 0);

    const std::filesystem::path diagnostics =
        outputRoot /
        L"GZFX" /
        L"Diagnostics";

    const std::string stackLog =
        ReadAll(
            diagnostics /
            L"ambient_depth_stack.txt");

    const std::string nativeInputs =
        ReadAll(
            diagnostics /
            L"native_surface_inputs.txt");

    const bool nativeNormalActive =
        stackLog.find(
            "native_normal=true") !=
        std::string::npos;

    const bool motionActive =
        stackLog.find(
            "motion=true") !=
        std::string::npos;

    const bool temporalActive =
        stackLog.find(
            "temporal=true") !=
        std::string::npos;

    const bool historyActive =
        stackLog.find(
            "history=true") !=
        std::string::npos;

    const bool nativeInputsFound =
        nativeInputs.find(
            "motion=0x0") ==
            std::string::npos &&
        nativeInputs.find(
            "normal=0x0") ==
            std::string::npos;

    if (!nativeNormalActive ||
        !motionActive ||
        !temporalActive ||
        !historyActive ||
        !nativeInputsFound)
    {
        std::cerr
            << "native temporal foundation failed\n"
            << "normal=" << nativeNormalActive
            << " motion=" << motionActive
            << " temporal=" << temporalActive
            << " history=" << historyActive
            << " native_inputs="
            << nativeInputsFound
            << "\n"
            << stackLog
            << "\n"
            << nativeInputs
            << "\n";

        return 14;
    }

    if (cameraBuffer) cameraBuffer->Release();
    if (dsv) dsv->Release();
    if (depth) depth->Release();

    if (auxRtv) auxRtv->Release();
    if (aux) aux->Release();

    if (normalRtv) normalRtv->Release();
    if (normal) normal->Release();

    if (motionRtv) motionRtv->Release();
    if (motion) motion->Release();

    if (sceneRtv) sceneRtv->Release();
    if (scene) scene->Release();

    if (backRtv) backRtv->Release();
    if (backbuffer) backbuffer->Release();

    if (context) context->Release();
    if (device) device->Release();
    if (swap) swap->Release();

    DestroyWindow(window);
    FreeLibrary(gzfx);

    std::wcout
        << L"Native surface temporal harness passed.\n";

    return 0;
}

#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

using InitializeFn = bool (*)();

namespace
{
    LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l)
    {
        return DefWindowProcW(hwnd, msg, w, l);
    }

    std::string ReadAll(const std::filesystem::path& path)
    {
        std::ifstream in(path);
        return std::string(
            std::istreambuf_iterator<char>(in),
            std::istreambuf_iterator<char>());
    }

    std::uint64_t FindUnsignedOnLine(
        const std::string& text,
        const std::string& lineMarker,
        const std::string& fieldMarker)
    {
        const auto lineStart =
            text.find(lineMarker);

        if (lineStart == std::string::npos)
        {
            return 0;
        }

        const auto lineEnd =
            text.find('\n', lineStart);

        const auto field =
            text.find(
                fieldMarker,
                lineStart);

        if (field == std::string::npos ||
            (lineEnd != std::string::npos &&
             field >= lineEnd))
        {
            return 0;
        }

        const auto start =
            field + fieldMarker.size();

        const auto end =
            text.find_first_not_of(
                "0123456789",
                start);

        return std::stoull(
            text.substr(
                start,
                end == std::string::npos
                    ? std::string::npos
                    : end - start));
    }

    void SetIdentity(float* matrix)
    {
        for (int i = 0; i < 16; ++i) matrix[i] = 0.0f;
        matrix[0] = 1.0f;
        matrix[5] = 1.0f;
        matrix[10] = 1.0f;
        matrix[15] = 1.0f;
    }

    struct Target
    {
        ID3D11Texture2D* texture = nullptr;
        ID3D11RenderTargetView* rtv = nullptr;
        ID3D11ShaderResourceView* srv = nullptr;
    };

    Target CreateTarget(
        ID3D11Device* device,
        UINT width,
        UINT height,
        DXGI_FORMAT format,
        UINT mipLevels = 1,
        bool unorderedAccess = false)
    {
        Target result{};
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = mipLevels;
        desc.ArraySize = 1;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags =
            D3D11_BIND_RENDER_TARGET |
            D3D11_BIND_SHADER_RESOURCE |
            (unorderedAccess
                ? D3D11_BIND_UNORDERED_ACCESS
                : 0u);

        if (FAILED(device->CreateTexture2D(&desc, nullptr, &result.texture)))
        {
            return {};
        }

        D3D11_RENDER_TARGET_VIEW_DESC rtv{};
        rtv.Format = format;
        rtv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        rtv.Texture2D.MipSlice = 0;

        if (FAILED(device->CreateRenderTargetView(result.texture, &rtv, &result.rtv)))
        {
            result.texture->Release();
            return {};
        }

        if (FAILED(
                device->CreateShaderResourceView(
                    result.texture,
                    nullptr,
                    &result.srv)))
        {
            result.rtv->Release();
            result.texture->Release();
            return {};
        }

        return result;
    }

    void Release(Target& target)
    {
        if (target.srv) target.srv->Release();
        if (target.rtv) target.rtv->Release();
        if (target.texture) target.texture->Release();
        target = {};
    }

    bool ReadRgba8Pair(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        ID3D11Texture2D* source,
        UINT width,
        UINT height,
        float* left,
        float* right)
    {
        if (!device || !context || !source || !left || !right)
        {
            return false;
        }

        D3D11_TEXTURE2D_DESC desc{};
        source->GetDesc(&desc);

        D3D11_TEXTURE2D_DESC staging = desc;
        staging.Usage = D3D11_USAGE_STAGING;
        staging.BindFlags = 0;
        staging.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        staging.MiscFlags = 0;

        ID3D11Texture2D* copy = nullptr;

        if (FAILED(device->CreateTexture2D(&staging, nullptr, &copy)))
        {
            return false;
        }

        context->CopyResource(copy, source);

        D3D11_MAPPED_SUBRESOURCE mapped{};

        if (FAILED(context->Map(copy, 0, D3D11_MAP_READ, 0, &mapped)))
        {
            copy->Release();
            return false;
        }

        const UINT y = height / 2;
        const auto* row =
            static_cast<const std::uint8_t*>(mapped.pData) +
            static_cast<std::size_t>(y) * mapped.RowPitch;

        *left =
            row[(width / 4) * 4] /
            255.0f;

        *right =
            row[(width * 3 / 4) * 4] /
            255.0f;

        context->Unmap(copy, 0);
        copy->Release();
        return true;
    }
}

int wmain(int argc, wchar_t** argv)
{
    if (argc != 3) return 2;

    const std::filesystem::path dllPath(argv[1]);
    const std::filesystem::path outputRoot(argv[2]);

    std::error_code ec;
    std::filesystem::remove_all(outputRoot, ec);
    ec.clear();
    std::filesystem::create_directories(outputRoot, ec);

    if (ec ||
        !SetEnvironmentVariableW(
            L"GZFX_TEST_OUTPUT_ROOT",
            outputRoot.c_str()) ||
        !SetEnvironmentVariableW(
            L"GZFX_TEST_WAVE3_STATUS_EVERY_FRAME",
            L"1") ||
        !SetEnvironmentVariableW(
            L"GZFX_TEST_WAVE3_CAPTURE_REQUEST",
            L"1"))
    {
        return 3;
    }

    const HMODULE gzfx = LoadLibraryW(dllPath.c_str());
    if (!gzfx) return 4;

    const auto initialize = reinterpret_cast<InitializeFn>(
        GetProcAddress(gzfx, "GZFX_Initialize"));
    if (!initialize || !initialize()) return 5;

    const wchar_t* className = L"GZFX.Wave3PipelineHarness";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = className;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        return 6;
    }

    const HWND window = CreateWindowExW(
        0, className, L"GZFX Wave3", WS_OVERLAPPEDWINDOW,
        0, 0, 320, 180, nullptr, nullptr, wc.hInstance, nullptr);
    if (!window) return 7;

    constexpr UINT width = 320;
    constexpr UINT height = 180;

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferDesc.Width = width;
    sd.BufferDesc.Height = height;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.OutputWindow = window;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    IDXGISwapChain* swap = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    D3D_FEATURE_LEVEL level{};

    if (FAILED(D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
            nullptr, 0, D3D11_SDK_VERSION,
            &sd, &swap, &device, &level, &context)))
    {
        return 8;
    }

    // Present once so renderer_forensics installs the primary-context vtable hooks.
    swap->Present(0, 0);

    Target hdr = CreateTarget(device, width, height, DXGI_FORMAT_R16G16B16A16_FLOAT);
    Target motion = CreateTarget(device, width, height, DXGI_FORMAT_R16G16_FLOAT);
    Target normal = CreateTarget(device, width, height, DXGI_FORMAT_R8G8B8A8_UNORM);
    Target normalB = CreateTarget(device, width, height, DXGI_FORMAT_R8G8B8A8_UNORM);
    Target aux = CreateTarget(device, width, height, DXGI_FORMAT_R8G8_UNORM);
    Target aoA = CreateTarget(device, width, height, DXGI_FORMAT_R8G8B8A8_UNORM);
    Target aoB = CreateTarget(device, width, height, DXGI_FORMAT_R8G8B8A8_UNORM);
    Target aoTemp = CreateTarget(device, width, height, DXGI_FORMAT_R8G8B8A8_UNORM);
    Target aoFinal = CreateTarget(
        device,
        width,
        height,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        1,
        true);
    Target rawIndirect = CreateTarget(device, width, height, DXGI_FORMAT_R11G11B10_FLOAT);

    if (!hdr.texture || !motion.texture || !normal.texture ||
        !normalB.texture || !aux.texture ||
        !aoA.texture || !aoB.texture || !aoTemp.texture ||
        !aoFinal.texture || !rawIndirect.texture)
    {
        return 9;
    }

    D3D11_TEXTURE2D_DESC depthDesc{};
    depthDesc.Width = width;
    depthDesc.Height = height;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags =
        D3D11_BIND_DEPTH_STENCIL |
        D3D11_BIND_SHADER_RESOURCE;

    ID3D11Texture2D* depth = nullptr;
    ID3D11DepthStencilView* dsv = nullptr;
    if (FAILED(device->CreateTexture2D(&depthDesc, nullptr, &depth))) return 10;

    D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    if (FAILED(device->CreateDepthStencilView(depth, &dsvDesc, &dsv))) return 11;

    std::array<float, 180> camera{};
    for (int matrix = 0; matrix < 10; ++matrix)
    {
        SetIdentity(camera.data() + matrix * 16);
    }

    // FrameParams.
    camera[168] = 1.0f;
    // DynamicResolutionParams1 current.xy + previous.zw.
    camera[172] = 1.0f;
    camera[173] = 1.0f;
    camera[174] = 1.0f;
    camera[175] = 1.0f;
    // DynamicResolutionParams2.
    camera[176] = 1.0f;
    camera[177] = 1.0f;
    camera[178] = 1.0f;
    camera[179] = 1.0f;

    D3D11_BUFFER_DESC cbDesc{};
    cbDesc.ByteWidth = static_cast<UINT>(camera.size() * sizeof(float));
    cbDesc.Usage = D3D11_USAGE_DEFAULT;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    D3D11_SUBRESOURCE_DATA cbData{};
    cbData.pSysMem = camera.data();

    ID3D11Buffer* cameraBuffer = nullptr;
    if (FAILED(device->CreateBuffer(&cbDesc, &cbData, &cameraBuffer))) return 12;

    context->VSSetConstantBuffers(12, 1, &cameraBuffer);
    context->PSSetConstantBuffers(12, 1, &cameraBuffer);

    const FLOAT hdrClear[4] = {1.0f, 0.5f, 0.25f, 1.0f};
    const FLOAT motionClear[4] = {0, 0, 0, 0};
    const FLOAT normalClear[4] = {0.5f, 0.5f, 0, 1};
    const FLOAT auxClear[4] = {0.25f, 0.75f, 0, 0};
    const FLOAT stockAoClear[4] = {0.80f, 0.80f, 0.80f, 1.0f};
    const FLOAT indirectClear[4] = {0.10f, 0.10f, 0.10f, 1.0f};

    context->ClearRenderTargetView(hdr.rtv, hdrClear);
    context->ClearRenderTargetView(motion.rtv, motionClear);
    context->ClearRenderTargetView(normal.rtv, normalClear);
    context->ClearRenderTargetView(normalB.rtv, normalClear);
    context->ClearRenderTargetView(aux.rtv, auxClear);
    context->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH, 0.45f, 0);

    constexpr UINT cameraMipLevels = 9;
    D3D11_TEXTURE2D_DESC cameraZDesc{};
    cameraZDesc.Width = width;
    cameraZDesc.Height = height;
    cameraZDesc.MipLevels = cameraMipLevels;
    cameraZDesc.ArraySize = 1;
    cameraZDesc.Format = DXGI_FORMAT_R32_FLOAT;
    cameraZDesc.SampleDesc.Count = 1;
    cameraZDesc.Usage = D3D11_USAGE_DEFAULT;
    cameraZDesc.BindFlags =
        D3D11_BIND_RENDER_TARGET |
        D3D11_BIND_SHADER_RESOURCE;

    ID3D11Texture2D* cameraZ = nullptr;
    if (FAILED(device->CreateTexture2D(&cameraZDesc, nullptr, &cameraZ))) return 13;

    ID3D11ShaderResourceView* cameraZSrv = nullptr;
    if (FAILED(device->CreateShaderResourceView(cameraZ, nullptr, &cameraZSrv))) return 13;

    std::array<ID3D11RenderTargetView*, cameraMipLevels> cameraRtvs{};

    for (UINT mip = 0; mip < cameraMipLevels; ++mip)
    {
        D3D11_RENDER_TARGET_VIEW_DESC view{};
        view.Format = DXGI_FORMAT_R32_FLOAT;
        view.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        view.Texture2D.MipSlice = mip;
        if (FAILED(device->CreateRenderTargetView(cameraZ, &view, &cameraRtvs[mip])))
        {
            return 14;
        }
    }

    const auto diagnostics =
        outputRoot / L"GZFX" / L"Diagnostics" / L"Wave3";

    std::uint64_t syntheticFrameCounter = 0;

    const auto runFrame =
        [&](Target& normalTarget)
        {
            context->ClearRenderTargetView(
                hdr.rtv,
                hdrClear);
            context->ClearRenderTargetView(
                motion.rtv,
                motionClear);
            context->ClearRenderTargetView(
                normalTarget.rtv,
                normalClear);
            context->ClearRenderTargetView(
                aux.rtv,
                auxClear);
            context->ClearDepthStencilView(
                dsv,
                D3D11_CLEAR_DEPTH,
                0.45f,
                0);

            ID3D11RenderTargetView* mrt[4] =
            {
                hdr.rtv,
                motion.rtv,
                normalTarget.rtv,
                aux.rtv
            };

            context->OMSetRenderTargets(
                4,
                mrt,
                dsv);
            context->Draw(3, 0);
            context->Draw(3, 0);

            // Binding CameraZ mip 0 makes Semantic Authority signal
            // AfterOpaqueWorldMRT before the draw executes.
            for (UINT mip = 0;
                 mip < cameraMipLevels;
                 ++mip)
            {
                context->OMSetRenderTargets(
                    1,
                    &cameraRtvs[mip],
                    nullptr);

                if (mip == 0)
                {
                    ID3D11ShaderResourceView* depthSrv = nullptr;
                    context->PSSetShaderResources(0, 1, &depthSrv);
                }

                context->Draw(3, 0);
            }

            const bool even =
                (syntheticFrameCounter++ & 1u) == 0u;

            Target& previousAo =
                even ? aoA : aoB;

            Target& currentAo =
                even ? aoB : aoA;

            context->ClearRenderTargetView(
                currentAo.rtv,
                stockAoClear);

            context->OMSetRenderTargets(
                1,
                &currentAo.rtv,
                nullptr);

            ID3D11ShaderResourceView* aoInputs[4] =
            {
                cameraZSrv,
                motion.srv,
                normalTarget.srv,
                previousAo.srv
            };

            context->PSSetShaderResources(
                0,
                4,
                aoInputs);

            context->Draw(3, 0);

            ID3D11ShaderResourceView* nullSrvs[4] =
            {
                nullptr,
                nullptr,
                nullptr,
                nullptr
            };

            context->PSSetShaderResources(
                0,
                4,
                nullSrvs);

            context->ClearRenderTargetView(
                aoTemp.rtv,
                stockAoClear);

            context->OMSetRenderTargets(
                1,
                &aoTemp.rtv,
                nullptr);

            context->PSSetShaderResources(
                0,
                1,
                &currentAo.srv);

            context->Draw(3, 0);
            context->PSSetShaderResources(
                0,
                1,
                nullSrvs);

            context->ClearRenderTargetView(
                aoFinal.rtv,
                stockAoClear);

            context->OMSetRenderTargets(
                1,
                &aoFinal.rtv,
                nullptr);

            context->PSSetShaderResources(
                0,
                1,
                &aoTemp.srv);

            context->Draw(3, 0);
            context->PSSetShaderResources(
                0,
                1,
                nullSrvs);

            context->ClearRenderTargetView(
                rawIndirect.rtv,
                indirectClear);

            context->OMSetRenderTargets(
                1,
                &rawIndirect.rtv,
                nullptr);

            ID3D11ShaderResourceView* indirectInputs[3] =
            {
                cameraZSrv,
                hdr.srv,
                aux.srv
            };

            context->PSSetShaderResources(
                0,
                3,
                indirectInputs);

            context->Draw(3, 0);

            context->PSSetShaderResources(
                0,
                4,
                nullSrvs);

            context->OMSetRenderTargets(
                1,
                &hdr.rtv,
                nullptr);

            ID3D11ShaderResourceView* compositeInputs[2] =
            {
                rawIndirect.srv,
                aoFinal.srv
            };

            context->PSSetShaderResources(
                0,
                2,
                compositeInputs);

            context->Draw(3, 0);

            context->PSSetShaderResources(
                0,
                4,
                nullSrvs);

            context->OMSetRenderTargets(
                0,
                nullptr,
                nullptr);

            const std::string status =
                ReadAll(
                    diagnostics /
                    L"wave3_status.txt");

            swap->Present(0, 0);
            return status;
        };

    const std::string statusA =
        runFrame(normal);
    const std::string statusB =
        runFrame(normalB);
    const std::string statusC =
        runFrame(normal);

    std::string statusFinal = statusC;

    for (int frame = 3;
         frame < 35;
         ++frame)
    {
        statusFinal =
            runFrame(
                (frame & 1) == 0
                    ? normal
                    : normalB);
    }

    const bool executed =
        statusFinal.find("status=executed") !=
        std::string::npos;

    const bool hierarchy =
        statusFinal.find("hierarchy_valid=true") !=
        std::string::npos;

    const bool temporal =
        statusFinal.find("temporal_valid=true") !=
        std::string::npos;

    const bool reused =
        statusFinal.find(
            "history_reused_candidate=true") !=
        std::string::npos;

    const bool noReset =
        statusFinal.find(
            "history_reset_reason=none") !=
        std::string::npos;

    const bool delayedCapture =
        std::filesystem::exists(
            diagnostics /
            L"manual0001" /
            L"wave3_summary.txt");

    const auto normalGenerationA =
        FindUnsignedOnLine(
            statusA,
            "native_normal=",
            " generation=");

    const auto normalGenerationB =
        FindUnsignedOnLine(
            statusB,
            "native_normal=",
            " generation=");

    const auto normalGenerationC =
        FindUnsignedOnLine(
            statusC,
            "native_normal=",
            " generation=");

    const auto contractA =
        FindUnsignedOnLine(
            statusA,
            "native_normal=",
            " contract=");

    const auto contractB =
        FindUnsignedOnLine(
            statusB,
            "native_normal=",
            " contract=");

    const auto contractC =
        FindUnsignedOnLine(
            statusC,
            "native_normal=",
            " contract=");

    // Physical native-normal identity/generation is allowed to ping-pong.
    // The production contract must remain stable and history must survive.
    const bool physicalPingPong =
        normalGenerationA != 0 &&
        normalGenerationB != 0 &&
        normalGenerationC != 0 &&
        normalGenerationA != normalGenerationB &&
        normalGenerationB != normalGenerationC;

    const bool stableContract =
        contractA != 0 &&
        contractA == contractB &&
        contractB == contractC;

    const auto wave4Root =
        outputRoot /
        L"GZFX" /
        L"Diagnostics" /
        L"AmbientVisibilityV2";

    const std::string wave4Status =
        ReadAll(
            wave4Root /
            L"ambient_visibility_v2_status.txt");

    float splitLeft = 0.0f;
    float splitRight = 0.0f;

    const bool splitRead =
        ReadRgba8Pair(
            device,
            context,
            aoFinal.texture,
            width,
            height,
            &splitLeft,
            &splitRight);

    const bool wave4Applied =
        wave4Status.find(
            "applications=") !=
            std::string::npos &&
        wave4Status.find(
            "last_applied_frame=0") ==
            std::string::npos;

    const bool stageCorrectSplit =
        splitRead &&
        std::abs(splitLeft - 0.80f) <
            0.03f &&
        splitRight >
            splitLeft + 0.05f;

    if (!executed ||
        !hierarchy ||
        !temporal ||
        !reused ||
        !noReset ||
        !physicalPingPong ||
        !stableContract ||
        !delayedCapture ||
        !wave4Applied ||
        !stageCorrectSplit)
    {
        std::cerr
            << "Wave 3 ping-pong continuity failed\n"
            << "statusA:\n" << statusA
            << "\nstatusB:\n" << statusB
            << "\nstatusC:\n" << statusC
            << "\nstatusFinal:\n" << statusFinal
            << "\ndelayed_capture="
            << delayedCapture
            << "\nnormal_generations="
            << normalGenerationA
            << ","
            << normalGenerationB
            << ","
            << normalGenerationC
            << "\nwave4_status:\n"
            << wave4Status
            << "\nsplit_left="
            << splitLeft
            << " split_right="
            << splitRight
            << "\ncontracts="
            << contractA
            << ","
            << contractB
            << ","
            << contractC
            << "\n";

        return 15;
    }

    for (auto*& rtv : cameraRtvs) if (rtv) rtv->Release();
    if (cameraZSrv) cameraZSrv->Release();
    if (cameraZ) cameraZ->Release();
    Release(rawIndirect);
    Release(aoFinal);
    Release(aoTemp);
    Release(aoB);
    Release(aoA);
    if (cameraBuffer) cameraBuffer->Release();
    if (dsv) dsv->Release();
    if (depth) depth->Release();
    Release(aux);
    Release(normalB);
    Release(normal);
    Release(motion);
    Release(hdr);
    if (context) context->Release();
    if (device) device->Release();
    if (swap) swap->Release();
    DestroyWindow(window);
    FreeLibrary(gzfx);

    std::wcout << L"Wave 3 integration harness passed.\n";
    return 0;
}
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <array>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

using InitializeFn = bool (*)();
using ShutdownFn = void (*)();

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

    std::vector<std::string> SplitCsv(
        const std::string& line)
    {
        std::vector<std::string> values;
        std::size_t start = 0;

        for (;;)
        {
            const auto comma =
                line.find(',', start);

            if (comma == std::string::npos)
            {
                values.push_back(
                    line.substr(start));
                break;
            }

            values.push_back(
                line.substr(
                    start,
                    comma - start));

            start = comma + 1;
        }

        return values;
    }

    std::size_t CsvColumn(
        const std::vector<std::string>& header,
        const std::string& name)
    {
        for (std::size_t i = 0;
             i < header.size();
             ++i)
        {
            if (header[i] == name)
            {
                return i;
            }
        }

        return
            std::numeric_limits<
                std::size_t>::max();
    }

    std::uint64_t CanonicalIdentity(
        IUnknown* value)
    {
        if (value == nullptr)
        {
            return 0;
        }

        IUnknown* identity = nullptr;

        if (FAILED(
                value->QueryInterface(
                    __uuidof(IUnknown),
                    reinterpret_cast<void**>(
                        &identity))) ||
            identity == nullptr)
        {
            return
                reinterpret_cast<
                    std::uint64_t>(
                        value);
        }

        const auto result =
            reinterpret_cast<
                std::uint64_t>(
                    identity);

        identity->Release();
        return result;
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
            L"1") ||
        !SetEnvironmentVariableW(
            L"GZFX_TEST_WAVE4_ACTIVE",
            L"1"))
    {
        return 3;
    }

    wchar_t lateStackValue[8]{};
    const bool lateStackActive =
        GetEnvironmentVariableW(
            L"GZFX_TEST_LATE_STACK_ACTIVE",
            lateStackValue,
            static_cast<DWORD>(
                std::size(lateStackValue))) > 0;

    if (lateStackActive)
    {
        SetEnvironmentVariableW(
            L"GZFX_TEST_TELEMETRY_ACTIVE",
            L"1");

        SetEnvironmentVariableW(
            L"GZFX_TEST_TELEMETRY_FULL_PIPELINE",
            L"1");
    }

    wchar_t telemetryEnabledValue[8]{};
    const bool telemetryActive =
        GetEnvironmentVariableW(
            L"GZFX_TEST_TELEMETRY_ACTIVE",
            telemetryEnabledValue,
            static_cast<DWORD>(
                std::size(telemetryEnabledValue))) > 0;

    wchar_t overflowValue[8]{};
    const bool expectTelemetryOverflow =
        GetEnvironmentVariableW(
            L"GZFX_TEST_TELEMETRY_EXPECT_OVERFLOW",
            overflowValue,
            static_cast<DWORD>(
                std::size(overflowValue))) > 0;

    const HMODULE gzfx = LoadLibraryW(dllPath.c_str());
    if (!gzfx) return 4;

    const auto initialize = reinterpret_cast<InitializeFn>(
        GetProcAddress(gzfx, "GZFX_Initialize"));
    if (!initialize || !initialize()) return 5;

    const auto shutdown = reinterpret_cast<ShutdownFn>(
        GetProcAddress(gzfx, "GZFX_Shutdown"));

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

    // A deliberately plausible but wrong early MAIN_HDR reduction root.
    // 0.0.25 latched onto this kind of pass and never recovered.
    Target hdrFalseRoot = CreateTarget(
        device, width / 8, height / 8,
        DXGI_FORMAT_R16G16B16A16_FLOAT);

    Target hdrReduceA = CreateTarget(
        device, width / 4, height / 4,
        DXGI_FORMAT_R16G16B16A16_FLOAT);
    Target hdrReduceB = CreateTarget(
        device, width / 16, height / 16,
        DXGI_FORMAT_R16G16B16A16_FLOAT);
    Target hdrReduceC = CreateTarget(
        device, 5, 3,
        DXGI_FORMAT_R16G16B16A16_FLOAT);
    Target hdrReduceD = CreateTarget(
        device, 2, 2,
        DXGI_FORMAT_R16G16B16A16_FLOAT);

    Target tonemapCurrent = CreateTarget(
        device, width, height,
        DXGI_FORMAT_R8G8B8A8_UNORM);
    Target taaHistory = CreateTarget(
        device, width, height,
        DXGI_FORMAT_R8G8B8A8_UNORM);
    Target taaColor = CreateTarget(
        device, width, height,
        DXGI_FORMAT_R8G8B8A8_UNORM);
    Target taaFeedback = CreateTarget(
        device, width, height,
        DXGI_FORMAT_R8G8B8A8_UNORM);

    std::array<Target, 5> lateExtraTargets{};

    for (auto& target : lateExtraTargets)
    {
        target = CreateTarget(
            device,
            64,
            64,
            DXGI_FORMAT_R8G8B8A8_UNORM);
    }

    if (!hdr.texture || !motion.texture || !normal.texture ||
        !normalB.texture || !aux.texture ||
        !aoA.texture || !aoB.texture || !aoTemp.texture ||
        !aoFinal.texture || !rawIndirect.texture ||
        !hdrFalseRoot.texture ||
        !hdrReduceA.texture || !hdrReduceB.texture ||
        !hdrReduceC.texture || !hdrReduceD.texture ||
        !tonemapCurrent.texture || !taaHistory.texture ||
        !taaColor.texture || !taaFeedback.texture ||
        !lateExtraTargets[0].texture ||
        !lateExtraTargets[1].texture ||
        !lateExtraTargets[2].texture ||
        !lateExtraTargets[3].texture ||
        !lateExtraTargets[4].texture)
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

    D3D11_SHADER_RESOURCE_VIEW_DESC depthSrvDesc{};
    depthSrvDesc.Format =
        DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    depthSrvDesc.ViewDimension =
        D3D11_SRV_DIMENSION_TEXTURE2D;
    depthSrvDesc.Texture2D.MostDetailedMip = 0;
    depthSrvDesc.Texture2D.MipLevels = 1;

    ID3D11ShaderResourceView* depthSrv = nullptr;

    if (FAILED(
            device->CreateShaderResourceView(
                depth,
                &depthSrvDesc,
                &depthSrv)))
    {
        return 11;
    }

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
                    ID3D11ShaderResourceView* nullDepthSrv = nullptr;
                    context->PSSetShaderResources(0, 1, &nullDepthSrv);
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

            if (lateStackActive)
            {
                // Synthetic late image-space spine. First emit a false
                // MAIN_HDR downsample candidate that is smaller than the real
                // chain root. The detector must restart when the later real
                // root appears instead of carrying the poisoned candidate.
                context->OMSetRenderTargets(
                    1,
                    &hdrFalseRoot.rtv,
                    nullptr);

                context->PSSetShaderResources(
                    0,
                    1,
                    &hdr.srv);

                context->Draw(3, 0);

                context->PSSetShaderResources(
                    0,
                    1,
                    nullSrvs);

                // Real HDR reduction -> tonemap entry -> global temporal pass.
                ID3D11ShaderResourceView* reduceInput =
                    hdr.srv;

                Target* reductions[4] =
                {
                    &hdrReduceA,
                    &hdrReduceB,
                    &hdrReduceC,
                    &hdrReduceD
                };

                for (Target* reduction :
                     reductions)
                {
                    context->OMSetRenderTargets(
                        1,
                        &reduction->rtv,
                        nullptr);

                    context->PSSetShaderResources(
                        0,
                        1,
                        &reduceInput);

                    context->Draw(3, 0);

                    context->PSSetShaderResources(
                        0,
                        1,
                        nullSrvs);

                    reduceInput =
                        reduction->srv;
                }

                context->OMSetRenderTargets(
                    1,
                    &tonemapCurrent.rtv,
                    nullptr);

                context->PSSetShaderResources(
                    0,
                    1,
                    &hdrReduceD.srv);

                context->Draw(3, 0);

                context->PSSetShaderResources(
                    0,
                    1,
                    nullSrvs);

                ID3D11RenderTargetView* taaOutputs[2] =
                {
                    taaColor.rtv,
                    taaFeedback.rtv
                };

                context->OMSetRenderTargets(
                    2,
                    taaOutputs,
                    nullptr);

                ID3D11ShaderResourceView* taaInputs[4] =
                {
                    tonemapCurrent.srv,
                    taaHistory.srv,
                    motion.srv,
                    depthSrv
                };

                context->PSSetShaderResources(
                    0,
                    4,
                    taaInputs);

                context->Draw(3, 0);

                context->PSSetShaderResources(
                    0,
                    4,
                    nullSrvs);
            }

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

    std::array<std::uint64_t, 5>
        lateListIdentities{};

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

    if (lateStackActive)
    {
        ID3D11DeviceContext* deferredContext =
            nullptr;

        if (FAILED(
                device->CreateDeferredContext(
                    0,
                    &deferredContext)) ||
            deferredContext == nullptr)
        {
            return 19;
        }

        std::array<ID3D11CommandList*, 5>
            lists{};

        // List 1 first proves concrete-read overflow reporting.
        ID3D11ShaderResourceView* overflowSrvs[25] =
        {
            hdr.srv,
            motion.srv,
            normal.srv,
            normalB.srv,
            aux.srv,
            aoA.srv,
            aoB.srv,
            aoTemp.srv,
            aoFinal.srv,
            rawIndirect.srv,
            hdrReduceA.srv,
            hdrReduceB.srv,
            hdrReduceC.srv,
            hdrReduceD.srv,
            tonemapCurrent.srv,
            taaHistory.srv,
            taaColor.srv,
            taaFeedback.srv,
            cameraZSrv,
            depthSrv,
            lateExtraTargets[0].srv,
            lateExtraTargets[1].srv,
            lateExtraTargets[2].srv,
            lateExtraTargets[3].srv,
            lateExtraTargets[4].srv
        };

        deferredContext->OMSetRenderTargets(
            0,
            nullptr,
            nullptr);

        deferredContext->PSSetShaderResources(
            0,
            25,
            overflowSrvs);

        deferredContext->Draw(3, 0);

        ID3D11ShaderResourceView* clearOverflow[25]{};

        deferredContext->PSSetShaderResources(
            0,
            25,
            clearOverflow);

        // Then bind normal state + explicit copy, before FALSE reset.
        deferredContext->OMSetRenderTargets(
            1,
            &tonemapCurrent.rtv,
            nullptr);

        deferredContext->PSSetShaderResources(
            0,
            1,
            &taaHistory.srv);

        deferredContext->Draw(3, 0);

        deferredContext->CopyResource(
            tonemapCurrent.texture,
            taaColor.texture);

        if (FAILED(
                deferredContext->
                    FinishCommandList(
                        FALSE,
                        &lists[0])) ||
            lists[0] == nullptr)
        {
            return 20;
        }

        // List 2: no rebind. FALSE must have reset tracked state.
        deferredContext->Draw(3, 0);

        if (FAILED(
                deferredContext->
                    FinishCommandList(
                        FALSE,
                        &lists[1])) ||
            lists[1] == nullptr)
        {
            return 21;
        }

        // List 3: bind target and finish TRUE.
        deferredContext->OMSetRenderTargets(
            1,
            &tonemapCurrent.rtv,
            nullptr);

        deferredContext->Draw(3, 0);

        if (FAILED(
                deferredContext->
                    FinishCommandList(
                        TRUE,
                        &lists[2])) ||
            lists[2] == nullptr)
        {
            return 22;
        }

        // List 4: no rebind. TRUE must preserve tracked state.
        deferredContext->Draw(3, 0);

        if (FAILED(
                deferredContext->
                    FinishCommandList(
                        FALSE,
                        &lists[3])) ||
            lists[3] == nullptr)
        {
            return 23;
        }

        // List 5: nested playback of a known child list.
        deferredContext->ExecuteCommandList(
            lists[2],
            TRUE);

        if (FAILED(
                deferredContext->
                    FinishCommandList(
                        FALSE,
                        &lists[4])) ||
            lists[4] == nullptr)
        {
            return 24;
        }

        for (std::size_t i = 0;
             i < lists.size();
             ++i)
        {
            lateListIdentities[i] =
                CanonicalIdentity(
                    lists[i]);

            context->ExecuteCommandList(
                lists[i],
                TRUE);
        }

        swap->Present(0, 0);

        for (auto* list : lists)
        {
            if (list != nullptr)
            {
                list->Release();
            }
        }

        deferredContext->Release();
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

    if (telemetryActive)
    {
        if (shutdown == nullptr)
        {
            std::cerr << "Telemetry shutdown export unavailable\n";
            return 16;
        }

        shutdown();

        const auto telemetryRoot =
            outputRoot /
            L"GZFX" /
            L"Telemetry";

        std::filesystem::path session;

        if (std::filesystem::exists(
                telemetryRoot))
        {
            for (const auto& entry :
                 std::filesystem::directory_iterator(
                     telemetryRoot))
            {
                if (entry.is_directory())
                {
                    session =
                        entry.path();
                }
            }
        }

        const auto existsNonEmpty =
            [](const std::filesystem::path& path)
            {
                std::error_code error;

                return
                    std::filesystem::exists(
                        path,
                        error) &&
                    !error &&
                    std::filesystem::file_size(
                        path,
                        error) > 0 &&
                    !error;
            };

        const bool coreFiles =
            !session.empty() &&
            existsNonEmpty(
                session / L"manifest.txt") &&
            existsNonEmpty(
                session / L"listener_status.txt") &&
            existsNonEmpty(
                session / L"frames.csv") &&
            existsNonEmpty(
                session / L"stages.csv") &&
            existsNonEmpty(
                session / L"semantics.csv") &&
            existsNonEmpty(
                session / L"resources.csv") &&
            existsNonEmpty(
                session / L"pipeline_runs_0001.csv") &&
            existsNonEmpty(
                session / L"transitions.csv") &&
            existsNonEmpty(
                session / L"ownership.csv") &&
            existsNonEmpty(
                session / L"seconds.csv") &&
            existsNonEmpty(
                session / L"present_timeline.csv") &&
            existsNonEmpty(
                session / L"engine_events.csv") &&
            existsNonEmpty(
                session /
                    L"telemetry_session_complete.txt");

        if (!coreFiles)
        {
            std::cerr
                << "Telemetry session files incomplete\n";
            return 17;
        }

        const std::string listenerStatus =
            ReadAll(
                session /
                L"listener_status.txt");

        const std::string stages =
            ReadAll(
                session /
                L"stages.csv");

        const std::string semantics =
            ReadAll(
                session /
                L"semantics.csv");

        const std::string ownership =
            ReadAll(
                session /
                L"ownership.csv");

        const std::string pipeline =
            ReadAll(
                session /
                L"pipeline_runs_0001.csv");

        const std::string presentTimeline =
            ReadAll(
                session /
                L"present_timeline.csv");

        const std::uint64_t dropped =
            FindUnsignedOnLine(
                listenerStatus,
                "dropped=",
                "dropped=");

        const bool stageCoverage =
            stages.find(
                "after_opaque_world_mrt") !=
                    std::string::npos &&
            stages.find(
                "after_cameraz") !=
                    std::string::npos &&
            stages.find(
                "after_stock_ao") !=
                    std::string::npos &&
            stages.find(
                "at_ambient_screen_space_composite") !=
                    std::string::npos;

        const bool lateStageCoverage =
            !lateStackActive ||
            (
                stages.find(
                    "hdr_reduction") !=
                        std::string::npos &&
                stages.find(
                    "before_tonemap") !=
                        std::string::npos &&
                stages.find(
                    "before_global_taa") !=
                        std::string::npos &&
                stages.find(
                    "after_global_taa") !=
                        std::string::npos
            );

        const bool semanticCoverage =
            semantics.find("MAIN_HDR") !=
                    std::string::npos &&
            semantics.find(
                "MAIN_DEPTH_CURRENT") !=
                    std::string::npos &&
            semantics.find("MOTION") !=
                    std::string::npos &&
            semantics.find(
                "NORMAL_MASK_CURRENT") !=
                    std::string::npos &&
            semantics.find(
                "SAO_FILTERED") !=
                    std::string::npos;

        const bool ownershipCoverage =
            ownership.find(",gzfx,") !=
                std::string::npos &&
            ownership.find(",skyrim,") ==
                std::string::npos;

        const bool shaderIdentitySchema =
            pipeline.find("vs_hash") !=
                std::string::npos &&
            pipeline.find("vs_object") !=
                std::string::npos &&
            pipeline.find("shader_hash_known_mask") !=
                std::string::npos;

        const bool presentTimelineCoverage =
            presentTimeline.find("hook_enter") !=
                std::string::npos &&
            presentTimeline.find("callbacks_begin") !=
                std::string::npos &&
            presentTimeline.find("callbacks_end") !=
                std::string::npos &&
            presentTimeline.find("before_original_present") !=
                std::string::npos &&
            presentTimeline.find("after_original_present") !=
                std::string::npos;

        bool deferredCoverage =
            !lateStackActive;

        if (lateStackActive)
        {
            const std::string deferredLists =
                ReadAll(
                    session /
                    L"deferred_lists.csv");

            const std::string deferredRuns =
                ReadAll(
                    session /
                    L"deferred_runs_0001.csv");

            std::unordered_map<
                std::uint64_t,
                std::uint64_t>
                    listGenerations;

            const auto parseRows =
                [](const std::string& text)
                {
                    std::vector<
                        std::vector<
                            std::string>>
                                result;

                    std::size_t start = 0;

                    while (start < text.size())
                    {
                        const auto end =
                            text.find(
                                '\n',
                                start);

                        std::string line =
                            text.substr(
                                start,
                                end ==
                                    std::string::npos
                                    ? std::string::npos
                                    : end - start);

                        if (!line.empty() &&
                            line.back() == '\r')
                        {
                            line.pop_back();
                        }

                        if (!line.empty())
                        {
                            result.push_back(
                                SplitCsv(line));
                        }

                        if (end ==
                            std::string::npos)
                        {
                            break;
                        }

                        start = end + 1;
                    }

                    return result;
                };

            const auto listRows =
                parseRows(
                    deferredLists);

            if (listRows.size() > 1)
            {
                const auto& header =
                    listRows.front();

                const auto idColumn =
                    CsvColumn(
                        header,
                        "command_list");

                const auto generationColumn =
                    CsvColumn(
                        header,
                        "list_generation");

                for (std::size_t i = 1;
                     i < listRows.size();
                     ++i)
                {
                    if (idColumn <
                            listRows[i].size() &&
                        generationColumn <
                            listRows[i].size())
                    {
                        listGenerations[
                            std::stoull(
                                listRows[i][
                                    idColumn])] =
                            std::stoull(
                                listRows[i][
                                    generationColumn]);
                    }
                }
            }

            std::array<std::uint64_t, 5>
                generations{};

            bool allMapped = true;

            for (std::size_t i = 0;
                 i < generations.size();
                 ++i)
            {
                const auto found =
                    listGenerations.find(
                        lateListIdentities[i]);

                if (found ==
                    listGenerations.end())
                {
                    allMapped = false;
                    break;
                }

                generations[i] =
                    found->second;
            }

            bool copyExact = false;
            bool accessOverflow = false;
            bool falseReset = false;
            bool truePreserve = false;
            bool nestedVisible = false;

            const auto runRows =
                parseRows(
                    deferredRuns);

            if (allMapped &&
                runRows.size() > 1)
            {
                const auto& header =
                    runRows.front();

                const auto listColumn =
                    CsvColumn(
                        header,
                        "list_generation");
                const auto kindColumn =
                    CsvColumn(
                        header,
                        "kind");
                const auto rtvColumn =
                    CsvColumn(
                        header,
                        "rtv0");
                const auto explicitReadColumn =
                    CsvColumn(
                        header,
                        "explicit_read0");
                const auto explicitWriteColumn =
                    CsvColumn(
                        header,
                        "explicit_write0");
                const auto explicitReadCountColumn =
                    CsvColumn(
                        header,
                        "explicit_read_count");
                const auto explicitWriteCountColumn =
                    CsvColumn(
                        header,
                        "explicit_write_count");
                const auto totalReadColumn =
                    CsvColumn(
                        header,
                        "total_read_count");
                const auto totalWriteColumn =
                    CsvColumn(
                        header,
                        "total_write_count");
                const auto retainedReadColumn =
                    CsvColumn(
                        header,
                        "retained_read_count");
                const auto readOverflowColumn =
                    CsvColumn(
                        header,
                        "read_overflow");

                std::uint64_t parentRows = 0;

                for (std::size_t i = 1;
                     i < runRows.size();
                     ++i)
                {
                    const auto& row =
                        runRows[i];

                    if (listColumn >= row.size() ||
                        kindColumn >= row.size() ||
                        rtvColumn >= row.size())
                    {
                        continue;
                    }

                    const auto generation =
                        std::stoull(
                            row[listColumn]);

                    const auto kind =
                        std::stoull(
                            row[kindColumn]);

                    const auto rtv =
                        std::stoull(
                            row[rtvColumn]);

                    if (generation ==
                            generations[1] &&
                        kind == 0 &&
                        rtv == 0)
                    {
                        falseReset = true;
                    }

                    if (generation ==
                            generations[3] &&
                        kind == 0 &&
                        rtv != 0)
                    {
                        truePreserve = true;
                    }

                    if (generation ==
                        generations[4])
                    {
                        ++parentRows;
                    }

                    if (generation ==
                            generations[0] &&
                        kind == 0 &&
                        totalReadColumn <
                            row.size() &&
                        retainedReadColumn <
                            row.size() &&
                        readOverflowColumn <
                            row.size() &&
                        std::stoull(
                            row[
                                totalReadColumn]) >
                            24 &&
                        std::stoull(
                            row[
                                retainedReadColumn]) ==
                            24 &&
                        std::stoull(
                            row[
                                readOverflowColumn]) ==
                            1)
                    {
                        accessOverflow = true;
                    }

                    if (generation ==
                            generations[0] &&
                        kind == 2 &&
                        explicitReadColumn <
                            row.size() &&
                        explicitWriteColumn <
                            row.size() &&
                        explicitReadCountColumn <
                            row.size() &&
                        explicitWriteCountColumn <
                            row.size() &&
                        totalReadColumn <
                            row.size() &&
                        totalWriteColumn <
                            row.size())
                    {
                        const auto read =
                            std::stoull(
                                row[
                                    explicitReadColumn]);

                        const auto write =
                            std::stoull(
                                row[
                                    explicitWriteColumn]);

                        copyExact =
                            read != 0 &&
                            write != 0 &&
                            read != write &&
                            std::stoull(
                                row[
                                    explicitReadCountColumn]) ==
                                1 &&
                            std::stoull(
                                row[
                                    explicitWriteCountColumn]) ==
                                1 &&
                            std::stoull(
                                row[
                                    totalReadColumn]) ==
                                0 &&
                            std::stoull(
                                row[
                                    totalWriteColumn]) ==
                                0;
                    }
                }

                nestedVisible =
                    parentRows >= 2;
            }

            const bool deferredStatus =
                listenerStatus.find(
                    "deferred_contexts_attached=0") ==
                    std::string::npos &&
                listenerStatus.find(
                    "deferred_lists_finished=0") ==
                    std::string::npos &&
                listenerStatus.find(
                    "deferred_lists_executed=0") ==
                    std::string::npos;

            deferredCoverage =
                allMapped &&
                copyExact &&
                accessOverflow &&
                falseReset &&
                truePreserve &&
                nestedVisible &&
                deferredStatus;

            if (!deferredCoverage)
            {
                std::cerr
                    << "Deferred telemetry proof failed\n"
                    << "mapped="
                    << allMapped
                    << " copy="
                    << copyExact
                    << " access_overflow="
                    << accessOverflow
                    << " false_reset="
                    << falseReset
                    << " true_preserve="
                    << truePreserve
                    << " nested="
                    << nestedVisible
                    << "\n"
                    << listenerStatus
                    << "\n";
            }
        }

        const bool dropExpectation =
            expectTelemetryOverflow
                ? dropped > 0
                : dropped == 0;

        if (!stageCoverage ||
            !lateStageCoverage ||
            !deferredCoverage ||
            !semanticCoverage ||
            !ownershipCoverage ||
            !shaderIdentitySchema ||
            !presentTimelineCoverage ||
            !dropExpectation)
        {
            std::cerr
                << "Telemetry listener acceptance failed\n"
                << "dropped="
                << dropped
                << " expect_overflow="
                << expectTelemetryOverflow
                << "\n"
                << listenerStatus
                << "\n";

            return 18;
        }
    }

    for (auto*& rtv : cameraRtvs) if (rtv) rtv->Release();
    if (cameraZSrv) cameraZSrv->Release();
    if (cameraZ) cameraZ->Release();
    for (auto& target : lateExtraTargets)
    {
        Release(target);
    }

    Release(taaFeedback);
    Release(taaColor);
    Release(taaHistory);
    Release(tonemapCurrent);
    Release(hdrReduceD);
    Release(hdrReduceC);
    Release(hdrReduceB);
    Release(hdrReduceA);
    Release(hdrFalseRoot);
    Release(rawIndirect);
    Release(aoFinal);
    Release(aoTemp);
    Release(aoB);
    Release(aoA);
    if (cameraBuffer) cameraBuffer->Release();
    if (depthSrv) depthSrv->Release();
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
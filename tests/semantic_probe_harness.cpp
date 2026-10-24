#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

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

    bool ExistsNonEmpty(
        const std::filesystem::path& path)
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
    }

    std::string ReadAll(
        const std::filesystem::path& path)
    {
        std::ifstream in(path);

        return std::string(
            std::istreambuf_iterator<char>(in),
            std::istreambuf_iterator<char>());
    }

    std::string CanonicalPointerString(
        IUnknown* value)
    {
        if (value == nullptr)
        {
            return "0x0";
        }

        IUnknown* identity = nullptr;

        if (FAILED(
                value->QueryInterface(
                    __uuidof(IUnknown),
                    reinterpret_cast<void**>(
                        &identity))) ||
            identity == nullptr)
        {
            identity = value;
            identity->AddRef();
        }

        std::ostringstream stream;
        stream
            << "0x"
            << std::hex
            << std::uppercase
            << reinterpret_cast<
                std::uintptr_t>(
                    identity);

        identity->Release();
        return stream.str();
    }

    std::vector<std::string> SplitCsv(
        const std::string& line)
    {
        std::vector<std::string> values;
        std::stringstream stream(line);
        std::string value;

        while (std::getline(
            stream,
            value,
            ','))
        {
            values.push_back(value);
        }

        return values;
    }

    bool VerifyShaderStageAttribution(
        const std::string& csv)
    {
        std::stringstream stream(csv);
        std::string line;
        bool sawDraw = false;
        bool sawDispatch = false;

        std::getline(stream, line);

        while (std::getline(stream, line))
        {
            const auto values =
                SplitCsv(line);

            if (values.size() < 17)
            {
                continue;
            }

            const std::string& kind =
                values[3];

            if (kind == "draw" &&
                values[5] != "0x0" &&
                values[13] != "0x0")
            {
                sawDraw = true;

                if (values[15] != "0x0")
                {
                    return false;
                }
            }

            if (kind == "dispatch" &&
                values[15] != "0x0")
            {
                sawDispatch = true;

                if (values[5] != "0x0" ||
                    values[7] != "0x0" ||
                    values[9] != "0x0" ||
                    values[11] != "0x0" ||
                    values[13] != "0x0")
                {
                    return false;
                }
            }
        }

        return sawDraw && sawDispatch;
    }

    bool DeviceAlive(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        const char* stage)
    {
        if (device == nullptr ||
            context == nullptr)
        {
            return false;
        }

        context->Flush();

        const HRESULT reason =
            device->GetDeviceRemovedReason();

        if (FAILED(reason))
        {
            std::cerr
                << "device_removed_stage="
                << stage
                << " reason=0x"
                << std::hex
                << std::uppercase
                << static_cast<std::uint32_t>(
                    reason)
                << std::dec
                << "\n";

            return false;
        }

        return true;
    }

    bool CompileShader(
        const char* source,
        const char* entry,
        const char* profile,
        ID3DBlob** blob)
    {
        ID3DBlob* errors = nullptr;

        const HRESULT result =
            D3DCompile(
                source,
                std::strlen(source),
                "GZFXSemanticHarness",
                nullptr,
                nullptr,
                entry,
                profile,
                D3DCOMPILE_OPTIMIZATION_LEVEL3,
                0,
                blob,
                &errors);

        if (errors != nullptr)
        {
            errors->Release();
        }

        return SUCCEEDED(result);
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
            L"GZFX_TEST_SEMANTIC_CAPTURE",
            L"1") ||
        !SetEnvironmentVariableW(
            L"GZFX_TEST_SEMANTIC_MANUAL_CAPTURE",
            L"1"))
    {
        return 3;
    }

    const HMODULE gzfx =
        LoadLibraryW(
            dllPath.c_str());

    if (gzfx == nullptr)
    {
        return 4;
    }

    const auto initialize =
        reinterpret_cast<InitializeFn>(
            GetProcAddress(
                gzfx,
                "GZFX_Initialize"));

    if (initialize == nullptr ||
        !initialize())
    {
        return 5;
    }

    const wchar_t* className =
        L"GZFX.SemanticProbeHarness";

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
            L"GZFX semantic probe",
            WS_OVERLAPPEDWINDOW,
            0,
            0,
            640,
            360,
            nullptr,
            nullptr,
            windowClass.hInstance,
            nullptr);

    if (window == nullptr)
    {
        return 7;
    }

    DXGI_SWAP_CHAIN_DESC swapDescription{};
    swapDescription.BufferDesc.Width = 640;
    swapDescription.BufferDesc.Height = 360;
    swapDescription.BufferDesc.Format =
        DXGI_FORMAT_R8G8B8A8_UNORM;

    swapDescription.SampleDesc.Count = 1;
    swapDescription.BufferUsage =
        DXGI_USAGE_RENDER_TARGET_OUTPUT;

    swapDescription.BufferCount = 2;
    swapDescription.OutputWindow = window;
    swapDescription.Windowed = TRUE;
    swapDescription.SwapEffect =
        DXGI_SWAP_EFFECT_DISCARD;

    IDXGISwapChain* swapChain = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    D3D_FEATURE_LEVEL featureLevel{};

    if (FAILED(
            D3D11CreateDeviceAndSwapChain(
                nullptr,
                D3D_DRIVER_TYPE_WARP,
                nullptr,
                0,
                nullptr,
                0,
                D3D11_SDK_VERSION,
                &swapDescription,
                &swapChain,
                &device,
                &featureLevel,
                &context)))
    {
        return 8;
    }

    // Present once so GZFX acquires the live swap chain/context.
    swapChain->Present(0, 0);

    D3D11_TEXTURE2D_DESC normalDesc{};
    normalDesc.Width = 640;
    normalDesc.Height = 360;
    normalDesc.MipLevels = 1;
    normalDesc.ArraySize = 1;
    normalDesc.Format =
        DXGI_FORMAT_R8G8B8A8_UNORM;
    normalDesc.SampleDesc.Count = 1;
    normalDesc.Usage =
        D3D11_USAGE_DEFAULT;
    normalDesc.BindFlags =
        D3D11_BIND_RENDER_TARGET |
        D3D11_BIND_SHADER_RESOURCE;

    ID3D11Texture2D* normal = nullptr;
    ID3D11RenderTargetView* normalRtv = nullptr;
    ID3D11ShaderResourceView* normalSrv = nullptr;

    if (FAILED(
            device->CreateTexture2D(
                &normalDesc,
                nullptr,
                &normal)) ||
        FAILED(
            device->CreateRenderTargetView(
                normal,
                nullptr,
                &normalRtv)) ||
        FAILED(
            device->CreateShaderResourceView(
                normal,
                nullptr,
                &normalSrv)))
    {
        return 9;
    }

    D3D11_TEXTURE2D_DESC motionDesc =
        normalDesc;

    motionDesc.Format =
        DXGI_FORMAT_R16G16_FLOAT;

    ID3D11Texture2D* motion = nullptr;
    ID3D11RenderTargetView* motionRtv = nullptr;
    ID3D11ShaderResourceView* motionSrv = nullptr;

    if (FAILED(
            device->CreateTexture2D(
                &motionDesc,
                nullptr,
                &motion)) ||
        FAILED(
            device->CreateRenderTargetView(
                motion,
                nullptr,
                &motionRtv)) ||
        FAILED(
            device->CreateShaderResourceView(
                motion,
                nullptr,
                &motionSrv)))
    {
        return 10;
    }

    D3D11_TEXTURE2D_DESC scalarDesc =
        normalDesc;

    scalarDesc.Format =
        DXGI_FORMAT_R32_FLOAT;
    scalarDesc.MipLevels = 4;

    ID3D11Texture2D* scalar = nullptr;
    ID3D11RenderTargetView* scalarRtv = nullptr;
    ID3D11ShaderResourceView* scalarSrv = nullptr;

    if (FAILED(
            device->CreateTexture2D(
                &scalarDesc,
                nullptr,
                &scalar)) ||
        FAILED(
            device->CreateRenderTargetView(
                scalar,
                nullptr,
                &scalarRtv)) ||
        FAILED(
            device->CreateShaderResourceView(
                scalar,
                nullptr,
                &scalarSrv)))
    {
        return 11;
    }

    ID3D11Texture2D* scalarCopy = nullptr;
    ID3D11ShaderResourceView* scalarCopySrv = nullptr;

    if (FAILED(
            device->CreateTexture2D(
                &scalarDesc,
                nullptr,
                &scalarCopy)) ||
        FAILED(
            device->CreateShaderResourceView(
                scalarCopy,
                nullptr,
                &scalarCopySrv)))
    {
        return 18;
    }

    D3D11_TEXTURE2D_DESC hdrDesc =
        normalDesc;

    hdrDesc.Format =
        DXGI_FORMAT_R16G16B16A16_FLOAT;

    ID3D11Texture2D* hdr = nullptr;
    ID3D11RenderTargetView* hdrRtv = nullptr;
    ID3D11ShaderResourceView* hdrSrv = nullptr;

    if (FAILED(
            device->CreateTexture2D(
                &hdrDesc,
                nullptr,
                &hdr)) ||
        FAILED(
            device->CreateRenderTargetView(
                hdr,
                nullptr,
                &hdrRtv)) ||
        FAILED(
            device->CreateShaderResourceView(
                hdr,
                nullptr,
                &hdrSrv)))
    {
        return 22;
    }

    D3D11_TEXTURE2D_DESC auxDesc =
        normalDesc;

    auxDesc.Format =
        DXGI_FORMAT_R8G8_UNORM;

    ID3D11Texture2D* aux = nullptr;
    ID3D11RenderTargetView* auxRtv = nullptr;
    ID3D11ShaderResourceView* auxSrv = nullptr;

    if (FAILED(
            device->CreateTexture2D(
                &auxDesc,
                nullptr,
                &aux)) ||
        FAILED(
            device->CreateRenderTargetView(
                aux,
                nullptr,
                &auxRtv)) ||
        FAILED(
            device->CreateShaderResourceView(
                aux,
                nullptr,
                &auxSrv)))
    {
        return 23;
    }

    D3D11_TEXTURE2D_DESC automaticOnlyDesc =
        normalDesc;

    automaticOnlyDesc.Width = 64;
    automaticOnlyDesc.Height = 64;

    ID3D11Texture2D* automaticOnly = nullptr;
    ID3D11RenderTargetView* automaticOnlyRtv =
        nullptr;

    if (FAILED(
            device->CreateTexture2D(
                &automaticOnlyDesc,
                nullptr,
                &automaticOnly)) ||
        FAILED(
            device->CreateRenderTargetView(
                automaticOnly,
                nullptr,
                &automaticOnlyRtv)))
    {
        return 24;
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
        return 12;
    }

    D3D11_DEPTH_STENCIL_VIEW_DESC
        depthViewDesc{};

    depthViewDesc.Format =
        DXGI_FORMAT_D24_UNORM_S8_UINT;

    depthViewDesc.ViewDimension =
        D3D11_DSV_DIMENSION_TEXTURE2D;

    ID3D11DepthStencilView* depthView = nullptr;

    if (FAILED(
            device->CreateDepthStencilView(
                depth,
                &depthViewDesc,
                &depthView)))
    {
        return 13;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC
        depthSrvDesc{};

    depthSrvDesc.Format =
        DXGI_FORMAT_R24_UNORM_X8_TYPELESS;

    depthSrvDesc.ViewDimension =
        D3D11_SRV_DIMENSION_TEXTURE2D;

    depthSrvDesc.Texture2D.MipLevels = 1;

    ID3D11ShaderResourceView* depthSrv = nullptr;

    if (FAILED(
            device->CreateShaderResourceView(
                depth,
                &depthSrvDesc,
                &depthSrv)))
    {
        return 14;
    }

    D3D11_BUFFER_DESC dynamicBufferDesc{};
    dynamicBufferDesc.ByteWidth = 256;
    dynamicBufferDesc.Usage =
        D3D11_USAGE_DYNAMIC;
    dynamicBufferDesc.BindFlags =
        D3D11_BIND_CONSTANT_BUFFER;
    dynamicBufferDesc.CPUAccessFlags =
        D3D11_CPU_ACCESS_WRITE;

    ID3D11Buffer* dynamicBuffer = nullptr;

    if (FAILED(
            device->CreateBuffer(
                &dynamicBufferDesc,
                nullptr,
                &dynamicBuffer)))
    {
        return 20;
    }

    D3D11_BUFFER_DESC defaultBufferDesc =
        dynamicBufferDesc;

    defaultBufferDesc.Usage =
        D3D11_USAGE_DEFAULT;
    defaultBufferDesc.CPUAccessFlags = 0;

    ID3D11Buffer* defaultBuffer = nullptr;

    if (FAILED(
            device->CreateBuffer(
                &defaultBufferDesc,
                nullptr,
                &defaultBuffer)))
    {
        return 21;
    }

    D3D11_BUFFER_DESC cameraBufferDesc{};
    cameraBufferDesc.ByteWidth = 720;
    cameraBufferDesc.Usage =
        D3D11_USAGE_DEFAULT;
    cameraBufferDesc.BindFlags =
        D3D11_BIND_CONSTANT_BUFFER;

    ID3D11Buffer* cameraBuffer = nullptr;

    if (FAILED(
            device->CreateBuffer(
                &cameraBufferDesc,
                nullptr,
                &cameraBuffer)))
    {
        return 28;
    }

    const char* vertexSource =
        "float4 main(uint id : SV_VertexID) : SV_POSITION {"
        "float2 p = float2((id << 1) & 2, id & 2);"
        "return float4(p * float2(2,-2) + float2(-1,1),0,1);"
        "}";

    const char* pixelSource =
        "float4 main() : SV_TARGET {"
        "return float4(1,1,1,1);"
        "}";

    const char* computeSource =
        "[numthreads(1,1,1)] void main() {}";

    ID3DBlob* vertexBlob = nullptr;
    ID3DBlob* pixelBlob = nullptr;
    ID3DBlob* computeBlob = nullptr;

    if (!CompileShader(
            vertexSource,
            "main",
            "vs_5_0",
            &vertexBlob) ||
        !CompileShader(
            pixelSource,
            "main",
            "ps_5_0",
            &pixelBlob) ||
        !CompileShader(
            computeSource,
            "main",
            "cs_5_0",
            &computeBlob))
    {
        return 25;
    }

    ID3D11VertexShader* vertexShader = nullptr;
    ID3D11PixelShader* pixelShader = nullptr;
    ID3D11ComputeShader* computeShader = nullptr;

    HRESULT shaderResult =
        device->CreateVertexShader(
            vertexBlob->GetBufferPointer(),
            vertexBlob->GetBufferSize(),
            nullptr,
            &vertexShader);

    if (SUCCEEDED(shaderResult))
    {
        shaderResult =
            device->CreatePixelShader(
                pixelBlob->GetBufferPointer(),
                pixelBlob->GetBufferSize(),
                nullptr,
                &pixelShader);
    }

    if (SUCCEEDED(shaderResult))
    {
        shaderResult =
            device->CreateComputeShader(
                computeBlob->GetBufferPointer(),
                computeBlob->GetBufferSize(),
                nullptr,
                &computeShader);
    }

    vertexBlob->Release();
    pixelBlob->Release();
    computeBlob->Release();

    if (FAILED(shaderResult))
    {
        return 26;
    }

    if (!DeviceAlive(
            device,
            context,
            "setup"))
    {
        return 29;
    }

    context->IASetInputLayout(
        nullptr);

    context->IASetPrimitiveTopology(
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    D3D11_VIEWPORT viewport{};
    viewport.Width = 640.0f;
    viewport.Height = 360.0f;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    for (int frame = 0;
         frame < 11;
         ++frame)
    {
        context->RSSetViewports(
            1,
            &viewport);

        context->VSSetConstantBuffers(
            12,
            1,
            &cameraBuffer);

        context->PSSetConstantBuffers(
            12,
            1,
            &cameraBuffer);

        std::array<std::uint32_t, 64>
            mappedValues{};

        for (std::size_t i = 0;
             i < mappedValues.size();
             ++i)
        {
            mappedValues[i] =
                static_cast<std::uint32_t>(
                    frame * 1000 +
                    static_cast<int>(i));
        }

        D3D11_MAPPED_SUBRESOURCE mapped{};

        if (SUCCEEDED(
                context->Map(
                    dynamicBuffer,
                    0,
                    D3D11_MAP_WRITE_DISCARD,
                    0,
                    &mapped)))
        {
            std::memcpy(
                mapped.pData,
                mappedValues.data(),
                sizeof(mappedValues));

            context->Unmap(
                dynamicBuffer,
                0);
        }

        context->VSSetConstantBuffers(
            3,
            1,
            &dynamicBuffer);

        std::array<std::uint32_t, 64>
            updateValues{};

        for (std::size_t i = 0;
             i < updateValues.size();
             ++i)
        {
            updateValues[i] =
                static_cast<std::uint32_t>(
                    0xABC00000u +
                    frame * 100 +
                    static_cast<int>(i));
        }

        context->UpdateSubresource(
            defaultBuffer,
            0,
            nullptr,
            updateValues.data(),
            0,
            0);

        context->PSSetConstantBuffers(
            4,
            1,
            &defaultBuffer);

        if (!DeviceAlive(
                device,
                context,
                "buffer_updates"))
        {
            return 30;
        }

        context->VSSetShader(
            vertexShader,
            nullptr,
            0);

        context->PSSetShader(
            pixelShader,
            nullptr,
            0);

        context->CSSetShader(
            computeShader,
            nullptr,
            0);

        ID3D11RenderTargetView*
            targets[4] =
        {
            hdrRtv,
            motionRtv,
            normalRtv,
            auxRtv
        };

        context->OMSetRenderTargets(
            4,
            targets,
            depthView);

        for (int draw = 0;
             draw < 4;
             ++draw)
        {
            context->Draw(
                3,
                0);
        }

        if (!DeviceAlive(
                device,
                context,
                "mrt_draws"))
        {
            return 31;
        }

        context->OMSetRenderTargets(
            1,
            &scalarRtv,
            nullptr);

        context->Draw(
            3,
            0);

        if (frame < 3)
        {
            context->OMSetRenderTargets(
                1,
                &automaticOnlyRtv,
                nullptr);

            context->Draw(
                3,
                0);
        }

        if (!DeviceAlive(
                device,
                context,
                "scalar_draws"))
        {
            return 32;
        }

        context->OMSetRenderTargets(
            0,
            nullptr,
            nullptr);

        context->Dispatch(
            1,
            1,
            1);

        if (!DeviceAlive(
                device,
                context,
                "dispatch"))
        {
            return 33;
        }

        context->CopyResource(
            scalarCopy,
            scalar);

        if (!DeviceAlive(
                device,
                context,
                "copy_resource"))
        {
            return 34;
        }

        ID3D11ShaderResourceView*
            pixelResources[6] =
        {
            depthSrv,
            normalSrv,
            motionSrv,
            scalarCopySrv,
            hdrSrv,
            auxSrv
        };

        context->PSSetShaderResources(
            0,
            6,
            pixelResources);

        context->Draw(
            3,
            0);

        if (!DeviceAlive(
                device,
                context,
                "srv_draw"))
        {
            return 35;
        }

        ID3D11ShaderResourceView*
            nullResources[6] =
        {
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr
        };

        context->PSSetShaderResources(
            0,
            6,
            nullResources);

        context->VSSetShader(
            nullptr,
            nullptr,
            0);

        context->PSSetShader(
            nullptr,
            nullptr,
            0);

        context->CSSetShader(
            nullptr,
            nullptr,
            0);

        swapChain->Present(
            0,
            0);

        if (!DeviceAlive(
                device,
                context,
                "present"))
        {
            return 36;
        }
    }

    const std::filesystem::path diagnostics =
        outputRoot /
        L"GZFX" /
        L"Diagnostics";

    const auto semantics =
        diagnostics /
        L"renderer_semantics.txt";

    const auto bindings =
        diagnostics /
        L"semantic_bindings.csv";

    const auto manualSemantics =
        diagnostics /
        L"renderer_semantics_manual_0001.txt";

    const auto manualBindings =
        diagnostics /
        L"semantic_bindings_manual_0001.csv";

    const auto pipelineSummary =
        diagnostics /
        L"pipeline_summary.txt";

    const auto pipelinePasses =
        diagnostics /
        L"pipeline_passes.csv";

    const auto pipelineAccesses =
        diagnostics /
        L"pipeline_accesses.csv";

    const auto pipelineResources =
        diagnostics /
        L"pipeline_resources.csv";

    const auto pipelineShaders =
        diagnostics /
        L"pipeline_shaders.csv";

    const auto pipelineSignatures =
        diagnostics /
        L"pipeline_signatures.csv";

    const auto pipelineEdges =
        diagnostics /
        L"pipeline_edges.csv";

    const auto pipelineBufferUpdates =
        diagnostics /
        L"pipeline_buffer_updates.csv";

    const auto pipelineDepthStates =
        diagnostics /
        L"pipeline_depth_states.csv";

    const auto pipelineBlendStates =
        diagnostics /
        L"pipeline_blend_states.csv";

    const auto pipelineRasterStates =
        diagnostics /
        L"pipeline_raster_states.csv";

    const auto pipelineSamplerStates =
        diagnostics /
        L"pipeline_sampler_states.csv";

    const auto manualPipelineSummary =
        diagnostics /
        L"pipeline_summary_manual_0001.txt";

    const auto manualPipelinePasses =
        diagnostics /
        L"pipeline_passes_manual_0001.csv";

    const auto manualPipelineResources =
        diagnostics /
        L"pipeline_resources_manual_0001.csv";

    if (!ExistsNonEmpty(semantics) ||
        !ExistsNonEmpty(bindings) ||
        !ExistsNonEmpty(manualSemantics) ||
        !ExistsNonEmpty(manualBindings) ||
        !ExistsNonEmpty(pipelineSummary) ||
        !ExistsNonEmpty(pipelinePasses) ||
        !ExistsNonEmpty(pipelineAccesses) ||
        !ExistsNonEmpty(pipelineResources) ||
        !ExistsNonEmpty(pipelineShaders) ||
        !ExistsNonEmpty(pipelineSignatures) ||
        !ExistsNonEmpty(pipelineEdges) ||
        !ExistsNonEmpty(pipelineBufferUpdates) ||
        !ExistsNonEmpty(pipelineDepthStates) ||
        !ExistsNonEmpty(pipelineBlendStates) ||
        !ExistsNonEmpty(pipelineRasterStates) ||
        !ExistsNonEmpty(pipelineSamplerStates) ||
        !ExistsNonEmpty(manualPipelineSummary) ||
        !ExistsNonEmpty(manualPipelinePasses) ||
        !ExistsNonEmpty(manualPipelineResources))
    {
        return 15;
    }

    const std::string semanticText =
        ReadAll(semantics);

    const std::string manualSemanticText =
        ReadAll(manualSemantics);

    const std::string pipelineSummaryText =
        ReadAll(pipelineSummary);

    const std::string pipelinePassesText =
        ReadAll(pipelinePasses);

    const std::string pipelineResourcesText =
        ReadAll(pipelineResources);

    const std::string pipelineShadersText =
        ReadAll(pipelineShaders);

    const std::string pipelineSignaturesText =
        ReadAll(pipelineSignatures);

    const std::string pipelineEdgesText =
        ReadAll(pipelineEdges);

    const std::string pipelineBufferUpdatesText =
        ReadAll(pipelineBufferUpdates);

    const std::string manualPipelinePassesText =
        ReadAll(manualPipelinePasses);

    const std::string manualPipelineResourcesText =
        ReadAll(manualPipelineResources);

    const std::string automaticOnlyId =
        CanonicalPointerString(
            automaticOnly);

    if (semanticText.find(
            "format=28") ==
            std::string::npos ||
        semanticText.find(
            "format=34") ==
            std::string::npos ||
        semanticText.find(
            "format=41") ==
            std::string::npos ||
        manualSemanticText.find(
            "trace_frames=") ==
            std::string::npos)
    {
        std::cerr
            << semanticText
            << "\n";

        return 16;
    }

    if (pipelineSummaryText.find(
            "passes=") ==
            std::string::npos ||
        pipelineSummaryText.find(
            "copy_resolve_mip_passes=") ==
            std::string::npos ||
        pipelinePassesText.find(
            "copy_resource") ==
            std::string::npos ||
        pipelineResourcesText.find(
            "texture2d") ==
            std::string::npos ||
        pipelineSummaryText.find(
            "creation_interception_enabled=false") ==
            std::string::npos ||
        pipelineShadersText.find(
            "shader,birth,stage") ==
            std::string::npos ||
        pipelineSignaturesText.find(
            "signature,kind,count") ==
            std::string::npos ||
        pipelineEdgesText.find(
            "resource_lineage") ==
            std::string::npos ||
        pipelineBufferUpdatesText.find(
            "map_unmap") ==
            std::string::npos ||
        pipelineBufferUpdatesText.find(
            "update_subresource") ==
            std::string::npos)
    {
        std::cerr
            << pipelineSummaryText
            << "\n"
            << pipelinePassesText
            << "\n"
            << pipelineResourcesText
            << "\n"
            << pipelineShadersText
            << "\n"
            << pipelineSignaturesText
            << "\n"
            << pipelineEdgesText
            << "\n"
            << pipelineBufferUpdatesText
            << "\n";

        return 19;
    }

    if (manualSemanticText.find(
            "known_motion=0x0") !=
            std::string::npos ||
        manualSemanticText.find(
            "known_normal=0x0") !=
            std::string::npos ||
        manualSemanticText.find(
            "known_aux=0x0") !=
            std::string::npos ||
        manualPipelineResourcesText.find(
            "known_scene_color") ==
            std::string::npos ||
        manualPipelineResourcesText.find(
            "known_main_depth") ==
            std::string::npos ||
        manualPipelineResourcesText.find(
            "known_motion") ==
            std::string::npos ||
        manualPipelineResourcesText.find(
            "known_normal") ==
            std::string::npos ||
        manualPipelineResourcesText.find(
            "known_aux") ==
            std::string::npos ||
        pipelineResourcesText.find(
            automaticOnlyId) ==
            std::string::npos ||
        manualPipelineResourcesText.find(
            automaticOnlyId) !=
            std::string::npos ||
        !VerifyShaderStageAttribution(
            manualPipelinePassesText))
    {
        std::cerr
            << manualSemanticText
            << "\n"
            << manualPipelineResourcesText
            << "\n"
            << manualPipelinePassesText
            << "\n";

        return 27;
    }

    std::size_t previewCount = 0;
    bool sawChannelPreview = false;
    bool sawMipPreview = false;

    for (const auto& entry :
         std::filesystem::directory_iterator(
             diagnostics))
    {
        if (entry.is_regular_file() &&
            entry.path().filename().
                wstring().find(
                    L"semantic_candidate_") ==
                0 &&
            entry.path().extension() ==
                L".bmp")
        {
            ++previewCount;

            const std::wstring filename =
                entry.path().filename().
                    wstring();

            sawChannelPreview =
                sawChannelPreview ||
                filename.find(L"_r.bmp") !=
                    std::wstring::npos;

            sawMipPreview =
                sawMipPreview ||
                filename.find(L"_mip3.bmp") !=
                    std::wstring::npos;
        }
    }

    if (previewCount < 3 ||
        !sawChannelPreview ||
        !sawMipPreview)
    {
        std::cerr
            << semanticText
            << "\n"
            << manualSemanticText
            << "\n";

        return 17;
    }

    if (depthSrv) depthSrv->Release();
    if (depthView) depthView->Release();
    if (depth) depth->Release();

    if (computeShader) computeShader->Release();
    if (pixelShader) pixelShader->Release();
    if (vertexShader) vertexShader->Release();

    if (cameraBuffer) cameraBuffer->Release();
    if (defaultBuffer) defaultBuffer->Release();
    if (dynamicBuffer) dynamicBuffer->Release();

    if (automaticOnlyRtv) automaticOnlyRtv->Release();
    if (automaticOnly) automaticOnly->Release();

    if (auxSrv) auxSrv->Release();
    if (auxRtv) auxRtv->Release();
    if (aux) aux->Release();

    if (hdrSrv) hdrSrv->Release();
    if (hdrRtv) hdrRtv->Release();
    if (hdr) hdr->Release();

    if (scalarCopySrv) scalarCopySrv->Release();
    if (scalarCopy) scalarCopy->Release();

    if (scalarSrv) scalarSrv->Release();
    if (scalarRtv) scalarRtv->Release();
    if (scalar) scalar->Release();

    if (motionSrv) motionSrv->Release();
    if (motionRtv) motionRtv->Release();
    if (motion) motion->Release();

    if (normalSrv) normalSrv->Release();
    if (normalRtv) normalRtv->Release();
    if (normal) normal->Release();

    if (context) context->Release();
    if (device) device->Release();
    if (swapChain) swapChain->Release();

    DestroyWindow(window);
    FreeLibrary(gzfx);

    std::wcout
        << L"Renderer semantic probe harness passed.\n";

    return 0;
}

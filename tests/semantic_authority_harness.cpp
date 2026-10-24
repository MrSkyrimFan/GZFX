#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
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

        const bool exists =
            std::filesystem::exists(path, error);

        if (!exists || error)
        {
            return false;
        }

        const auto size =
            std::filesystem::file_size(path, error);

        return !error && size > 0;
    }

    std::string ReadAll(
        const std::filesystem::path& path)
    {
        std::ifstream in(path);

        return std::string(
            std::istreambuf_iterator<char>(in),
            std::istreambuf_iterator<char>());
    }

    std::size_t CountOccurrences(
        const std::string& text,
        const std::string& value)
    {
        if (value.empty())
        {
            return 0;
        }

        std::size_t count = 0;
        std::size_t position = 0;

        while ((position =
                    text.find(value, position)) !=
               std::string::npos)
        {
            ++count;
            position += value.size();
        }

        return count;
    }

    std::string FindLineStartingWith(
        const std::string& text,
        const std::string& prefix)
    {
        std::istringstream stream(text);
        std::string line;

        while (std::getline(stream, line))
        {
            if (line.rfind(prefix, 0) == 0)
            {
                return line;
            }
        }

        return {};
    }

    std::vector<std::string> SplitCsv(
        const std::string& line)
    {
        std::vector<std::string> values;
        std::stringstream stream(line);
        std::string value;

        while (std::getline(stream, value, ','))
        {
            values.push_back(value);
        }

        return values;
    }

    bool Compile(
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
                "GZFXSemanticAuthorityHarness",
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

    struct Texture2D
    {
        ID3D11Texture2D* texture = nullptr;
        ID3D11RenderTargetView* rtv = nullptr;
        ID3D11ShaderResourceView* srv = nullptr;
    };

    struct DepthTexture
    {
        ID3D11Texture2D* texture = nullptr;
        ID3D11DepthStencilView* dsv = nullptr;
        ID3D11ShaderResourceView* srv = nullptr;
    };

    struct Texture1D
    {
        ID3D11Texture1D* texture = nullptr;
        ID3D11ShaderResourceView* srv = nullptr;
    };

    struct Texture3D
    {
        ID3D11Texture3D* texture = nullptr;
        ID3D11ShaderResourceView* srv = nullptr;
        ID3D11UnorderedAccessView* uav = nullptr;
    };

    Texture2D CreateColor(
        ID3D11Device* device,
        const UINT width,
        const UINT height,
        const DXGI_FORMAT format,
        const UINT mipLevels = 1)
    {
        Texture2D result{};

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
            D3D11_BIND_SHADER_RESOURCE;

        if (FAILED(
                device->CreateTexture2D(
                    &desc,
                    nullptr,
                    &result.texture)))
        {
            return {};
        }

        if (mipLevels == 1)
        {
            if (FAILED(
                    device->CreateRenderTargetView(
                        result.texture,
                        nullptr,
                        &result.rtv)))
            {
                return {};
            }
        }

        if (FAILED(
                device->CreateShaderResourceView(
                    result.texture,
                    nullptr,
                    &result.srv)))
        {
            return {};
        }

        return result;
    }

    DepthTexture CreateDepth(
        ID3D11Device* device,
        const UINT width,
        const UINT height)
    {
        DepthTexture result{};

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R24G8_TYPELESS;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags =
            D3D11_BIND_DEPTH_STENCIL |
            D3D11_BIND_SHADER_RESOURCE;

        if (FAILED(
                device->CreateTexture2D(
                    &desc,
                    nullptr,
                    &result.texture)))
        {
            return {};
        }

        D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
        dsvDesc.Format =
            DXGI_FORMAT_D24_UNORM_S8_UINT;
        dsvDesc.ViewDimension =
            D3D11_DSV_DIMENSION_TEXTURE2D;

        if (FAILED(
                device->CreateDepthStencilView(
                    result.texture,
                    &dsvDesc,
                    &result.dsv)))
        {
            return {};
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format =
            DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        srvDesc.ViewDimension =
            D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;

        if (FAILED(
                device->CreateShaderResourceView(
                    result.texture,
                    &srvDesc,
                    &result.srv)))
        {
            return {};
        }

        return result;
    }

    Texture1D CreateLookup(
        ID3D11Device* device,
        const UINT width)
    {
        Texture1D result{};

        D3D11_TEXTURE1D_DESC desc{};
        desc.Width = width;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R32_FLOAT;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        if (FAILED(
                device->CreateTexture1D(
                    &desc,
                    nullptr,
                    &result.texture)) ||
            FAILED(
                device->CreateShaderResourceView(
                    result.texture,
                    nullptr,
                    &result.srv)))
        {
            return {};
        }

        return result;
    }

    Texture3D CreateVolume(
        ID3D11Device* device)
    {
        Texture3D result{};

        D3D11_TEXTURE3D_DESC desc{};
        desc.Width = 320;
        desc.Height = 192;
        desc.Depth = 90;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R16_FLOAT;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags =
            D3D11_BIND_UNORDERED_ACCESS |
            D3D11_BIND_SHADER_RESOURCE;

        if (FAILED(
                device->CreateTexture3D(
                    &desc,
                    nullptr,
                    &result.texture)) ||
            FAILED(
                device->CreateShaderResourceView(
                    result.texture,
                    nullptr,
                    &result.srv)) ||
            FAILED(
                device->CreateUnorderedAccessView(
                    result.texture,
                    nullptr,
                    &result.uav)))
        {
            return {};
        }

        return result;
    }

    void Release(Texture2D& value)
    {
        if (value.srv) value.srv->Release();
        if (value.rtv) value.rtv->Release();
        if (value.texture) value.texture->Release();
        value = {};
    }

    void Release(DepthTexture& value)
    {
        if (value.srv) value.srv->Release();
        if (value.dsv) value.dsv->Release();
        if (value.texture) value.texture->Release();
        value = {};
    }

    void Release(Texture1D& value)
    {
        if (value.srv) value.srv->Release();
        if (value.texture) value.texture->Release();
        value = {};
    }

    void Release(Texture3D& value)
    {
        if (value.uav) value.uav->Release();
        if (value.srv) value.srv->Release();
        if (value.texture) value.texture->Release();
        value = {};
    }

    bool CurrentAvailabilityIs(
        const std::string& authority,
        const std::string& semantic,
        const bool expected)
    {
        const std::string line =
            FindLineStartingWith(
                authority,
                semantic + ",");

        if (line.empty())
        {
            return false;
        }

        const auto fields = SplitCsv(line);

        if (fields.size() < 4)
        {
            return false;
        }

        return fields[3] ==
            (expected ? "1" : "0");
    }

    bool CaptureObservationFramesBounded(
        const std::string& authority)
    {
        const auto startLine =
            FindLineStartingWith(
                authority,
                "capture_start=");

        const auto endLine =
            FindLineStartingWith(
                authority,
                "capture_end=");

        if (startLine.empty() ||
            endLine.empty())
        {
            return false;
        }

        const std::uint64_t start =
            std::stoull(
                startLine.substr(
                    std::strlen("capture_start=")));

        const std::uint64_t end =
            std::stoull(
                endLine.substr(
                    std::strlen("capture_end=")));

        const std::string marker =
            "[capture_local_observations]\n";

        const auto section =
            authority.find(marker);

        if (section == std::string::npos)
        {
            return false;
        }

        const auto next =
            authority.find(
                "\n[",
                section + marker.size());

        const std::string body =
            authority.substr(
                section + marker.size(),
                next == std::string::npos
                    ? std::string::npos
                    : next -
                        (section + marker.size()));

        std::istringstream lines(body);
        std::string line;
        bool sawData = false;

        while (std::getline(lines, line))
        {
            if (line.empty() ||
                line.rfind("frame,", 0) == 0)
            {
                continue;
            }

            const auto fields = SplitCsv(line);

            if (fields.empty())
            {
                return false;
            }

            const auto frame =
                std::stoull(fields[0]);

            if (frame < start ||
                frame > end)
            {
                return false;
            }

            sawData = true;
        }

        return sawData;
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
            L"1"))
    {
        return 3;
    }

    const HMODULE gzfx =
        LoadLibraryW(dllPath.c_str());

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
        L"GZFX.SemanticAuthorityHarness";

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = className;

    if (!RegisterClassExW(&wc) &&
        GetLastError() !=
            ERROR_CLASS_ALREADY_EXISTS)
    {
        return 6;
    }

    const HWND window =
        CreateWindowExW(
            0,
            className,
            L"GZFX semantic authority",
            WS_OVERLAPPEDWINDOW,
            0,
            0,
            320,
            180,
            nullptr,
            nullptr,
            wc.hInstance,
            nullptr);

    if (window == nullptr)
    {
        return 7;
    }

    DXGI_SWAP_CHAIN_DESC swapDesc{};
    swapDesc.BufferDesc.Width = 320;
    swapDesc.BufferDesc.Height = 180;
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

    swap->Present(0, 0);

    const char* vertexSource =
        "struct O{float4 p:SV_POSITION;};"
        "O main(uint id:SV_VertexID){"
        "float2 p=float2((id<<1)&2,id&2);"
        "O o;o.p=float4(p*float2(2,-2)+float2(-1,1),0,1);"
        "return o;}";

    const char* pixelSource =
        "struct O{float4 a:SV_Target0;float4 b:SV_Target1;"
        "float4 c:SV_Target2;float4 d:SV_Target3;};"
        "O main(){O o;o.a=float4(1,.5,.25,1);"
        "o.b=float4(.1,.2,0,1);"
        "o.c=float4(.5,.5,0,1);"
        "o.d=float4(.25,.75,0,1);return o;}";

    const char* computeSource =
        "RWTexture3D<float> Out:register(u0);"
        "[numthreads(1,1,1)]"
        "void main(uint3 id:SV_DispatchThreadID){Out[id]=1;}";

    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* psBlob = nullptr;
    ID3DBlob* csBlob = nullptr;

    if (!Compile(
            vertexSource,
            "main",
            "vs_5_0",
            &vsBlob) ||
        !Compile(
            pixelSource,
            "main",
            "ps_5_0",
            &psBlob) ||
        !Compile(
            computeSource,
            "main",
            "cs_5_0",
            &csBlob))
    {
        return 9;
    }

    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    ID3D11ComputeShader* cs = nullptr;

    if (FAILED(
            device->CreateVertexShader(
                vsBlob->GetBufferPointer(),
                vsBlob->GetBufferSize(),
                nullptr,
                &vs)) ||
        FAILED(
            device->CreatePixelShader(
                psBlob->GetBufferPointer(),
                psBlob->GetBufferSize(),
                nullptr,
                &ps)) ||
        FAILED(
            device->CreateComputeShader(
                csBlob->GetBufferPointer(),
                csBlob->GetBufferSize(),
                nullptr,
                &cs)))
    {
        return 10;
    }

    vsBlob->Release();
    psBlob->Release();
    csBlob->Release();

    Texture2D hdr =
        CreateColor(
            device,
            320,
            180,
            DXGI_FORMAT_R16G16B16A16_FLOAT);

    Texture2D motion =
        CreateColor(
            device,
            320,
            180,
            DXGI_FORMAT_R16G16_FLOAT);

    Texture2D normal =
        CreateColor(
            device,
            320,
            180,
            DXGI_FORMAT_R8G8B8A8_UNORM);

    Texture2D aux =
        CreateColor(
            device,
            320,
            180,
            DXGI_FORMAT_R8G8_UNORM);

    Texture2D aoA =
        CreateColor(
            device,
            320,
            180,
            DXGI_FORMAT_R8G8B8A8_UNORM);

    Texture2D aoB =
        CreateColor(
            device,
            320,
            180,
            DXGI_FORMAT_R8G8B8A8_UNORM);

    Texture2D aoTemp =
        CreateColor(
            device,
            320,
            180,
            DXGI_FORMAT_R8G8B8A8_UNORM);

    Texture2D aoFinal =
        CreateColor(
            device,
            320,
            180,
            DXGI_FORMAT_R8G8B8A8_UNORM);

    Texture2D rawIndirect =
        CreateColor(
            device,
            320,
            180,
            DXGI_FORMAT_R11G11B10_FLOAT);

    Texture2D cameraZ =
        CreateColor(
            device,
            320,
            180,
            DXGI_FORMAT_R32_FLOAT,
            9);

    Texture2D previousVl =
        CreateColor(
            device,
            320,
            180,
            DXGI_FORMAT_R16_FLOAT);

    Texture2D currentVl =
        CreateColor(
            device,
            320,
            180,
            DXGI_FORMAT_R16_FLOAT);

    Texture2D noise =
        CreateColor(
            device,
            32,
            32,
            DXGI_FORMAT_R8G8B8A8_UNORM);

    DepthTexture depth =
        CreateDepth(device, 320, 180);

    DepthTexture depthEarly =
        CreateDepth(device, 320, 180);

    DepthTexture depthHistory =
        CreateDepth(device, 320, 180);

    Texture1D repartition =
        CreateLookup(device, 4096);

    Texture3D volume =
        CreateVolume(device);

    if (!hdr.texture ||
        !motion.texture ||
        !normal.texture ||
        !aux.texture ||
        !aoA.texture ||
        !aoB.texture ||
        !aoTemp.texture ||
        !aoFinal.texture ||
        !rawIndirect.texture ||
        !cameraZ.texture ||
        !previousVl.texture ||
        !currentVl.texture ||
        !noise.texture ||
        !depth.texture ||
        !depthEarly.texture ||
        !depthHistory.texture ||
        !repartition.texture ||
        !volume.texture)
    {
        return 11;
    }

    std::array<
        ID3D11RenderTargetView*,
        9> cameraMipRtvs{};

    for (UINT mip = 0;
         mip < cameraMipRtvs.size();
         ++mip)
    {
        D3D11_RENDER_TARGET_VIEW_DESC desc{};
        desc.Format =
            DXGI_FORMAT_R32_FLOAT;
        desc.ViewDimension =
            D3D11_RTV_DIMENSION_TEXTURE2D;
        desc.Texture2D.MipSlice = mip;

        if (FAILED(
                device->CreateRenderTargetView(
                    cameraZ.texture,
                    &desc,
                    &cameraMipRtvs[mip])))
        {
            return 12;
        }
    }

    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vs, nullptr, 0);
    context->PSSetShader(ps, nullptr, 0);

    D3D11_VIEWPORT full{};
    full.Width = 320.0f;
    full.Height = 180.0f;
    full.MinDepth = 0.0f;
    full.MaxDepth = 1.0f;

    for (UINT syntheticFrame = 0;
         syntheticFrame < 7;
         ++syntheticFrame)
    {
        const bool exterior =
            syntheticFrame == 0;

        context->RSSetViewports(1, &full);

        context->CopyResource(
            depthEarly.texture,
            depth.texture);

        std::array<
            ID3D11RenderTargetView*,
            4> mrt =
        {
            hdr.rtv,
            motion.rtv,
            normal.rtv,
            aux.rtv
        };

        context->OMSetRenderTargets(
            static_cast<UINT>(mrt.size()),
            mrt.data(),
            depth.dsv);

        context->Draw(3, 0);

        for (UINT mip = 0;
             mip < cameraMipRtvs.size();
             ++mip)
        {
            const UINT width =
                std::max<UINT>(
                    1,
                    320u >> mip);

            const UINT height =
                std::max<UINT>(
                    1,
                    180u >> mip);

            D3D11_VIEWPORT viewport{};
            viewport.Width =
                static_cast<FLOAT>(width);
            viewport.Height =
                static_cast<FLOAT>(height);
            viewport.MinDepth = 0.0f;
            viewport.MaxDepth = 1.0f;

            context->RSSetViewports(
                1,
                &viewport);

            ID3D11RenderTargetView* target =
                cameraMipRtvs[mip];

            context->OMSetRenderTargets(
                1,
                &target,
                nullptr);

            if (mip == 0)
            {
                context->PSSetShaderResources(
                    0,
                    1,
                    &depthEarly.srv);
            }

            context->Draw(3, 0);
        }

        ID3D11ShaderResourceView* nullSrv = nullptr;

        context->PSSetShaderResources(
            0,
            1,
            &nullSrv);

        context->RSSetViewports(1, &full);

        const bool even =
            (syntheticFrame % 2) == 0;

        Texture2D& previousAo =
            even ? aoA : aoB;

        Texture2D& currentAo =
            even ? aoB : aoA;

        context->OMSetRenderTargets(
            1,
            &currentAo.rtv,
            nullptr);

        std::array<
            ID3D11ShaderResourceView*,
            4> aoInputs =
        {
            cameraZ.srv,
            motion.srv,
            normal.srv,
            previousAo.srv
        };

        context->PSSetShaderResources(
            0,
            static_cast<UINT>(aoInputs.size()),
            aoInputs.data());

        context->Draw(3, 0);

        std::array<
            ID3D11ShaderResourceView*,
            7> clearSrvs =
        {
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr
        };

        context->PSSetShaderResources(
            0,
            static_cast<UINT>(clearSrvs.size()),
            clearSrvs.data());

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
            &nullSrv);

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
            &nullSrv);

        context->OMSetRenderTargets(
            1,
            &rawIndirect.rtv,
            nullptr);

        std::array<
            ID3D11ShaderResourceView*,
            3> indirectInputs =
        {
            cameraZ.srv,
            hdr.srv,
            aux.srv
        };

        context->PSSetShaderResources(
            0,
            static_cast<UINT>(
                indirectInputs.size()),
            indirectInputs.data());

        context->Draw(3, 0);

        context->PSSetShaderResources(
            0,
            static_cast<UINT>(clearSrvs.size()),
            clearSrvs.data());

        context->OMSetRenderTargets(
            1,
            &hdr.rtv,
            nullptr);

        std::array<
            ID3D11ShaderResourceView*,
            2> compositeInputs =
        {
            rawIndirect.srv,
            aoFinal.srv
        };

        context->PSSetShaderResources(
            0,
            static_cast<UINT>(
                compositeInputs.size()),
            compositeInputs.data());

        context->Draw(3, 0);

        context->PSSetShaderResources(
            0,
            static_cast<UINT>(clearSrvs.size()),
            clearSrvs.data());

        if (exterior)
        {
            context->OMSetRenderTargets(
                0,
                nullptr,
                nullptr);

            context->CSSetShader(
                cs,
                nullptr,
                0);

            context->CSSetUnorderedAccessViews(
                0,
                1,
                &volume.uav,
                nullptr);

            context->Dispatch(1, 1, 1);

            ID3D11UnorderedAccessView* nullUav =
                nullptr;

            context->CSSetUnorderedAccessViews(
                0,
                1,
                &nullUav,
                nullptr);

            context->CSSetShader(
                nullptr,
                nullptr,
                0);

            context->PSSetShader(ps, nullptr, 0);

            context->OMSetRenderTargets(
                1,
                &currentVl.rtv,
                nullptr);

            std::array<
                ID3D11ShaderResourceView*,
                7> volumetricInputs =
            {
                depth.srv,
                previousVl.srv,
                volume.srv,
                repartition.srv,
                noise.srv,
                motion.srv,
                depthHistory.srv
            };

            context->PSSetShaderResources(
                0,
                static_cast<UINT>(
                    volumetricInputs.size()),
                volumetricInputs.data());

            context->Draw(3, 0);

            context->PSSetShaderResources(
                0,
                static_cast<UINT>(clearSrvs.size()),
                clearSrvs.data());

            context->CopyResource(
                depthHistory.texture,
                depth.texture);
        }

        context->OMSetRenderTargets(
            0,
            nullptr,
            nullptr);

        swap->Present(0, 0);
    }

    const std::filesystem::path diagnostics =
        outputRoot /
        L"GZFX" /
        L"Diagnostics";

    const auto authority =
        diagnostics /
        L"semantic_authority.txt";

    const auto schedule =
        diagnostics /
        L"stage_schedule.csv";

    if (!ExistsNonEmpty(authority) ||
        !ExistsNonEmpty(schedule))
    {
        return 13;
    }

    const std::string authorityText =
        ReadAll(authority);

    const std::string scheduleText =
        ReadAll(schedule);

    const std::array<
        const char*,
        10> requiredSemantics =
    {
        "MAIN_HDR,verified",
        "MAIN_DEPTH_CURRENT,verified",
        "MOTION,verified",
        "NORMAL_MASK_CURRENT,verified",
        "AUXILIARY_RT3,verified",
        "DEPTH_EARLY_COPY,strong",
        "STOCK_CAMERAZ,verified",
        "SAO_RAWAO_CURRENT,strong",
        "SAO_FILTERED,strong",
        "RAW_INDIRECT,strong"
    };

    for (const auto* required :
         requiredSemantics)
    {
        if (authorityText.find(required) ==
            std::string::npos)
        {
            return 14;
        }
    }

    if (authorityText.find(
            "VOLUMETRIC_DENSITY_A,verified") ==
            std::string::npos ||
        authorityText.find(
            "VOLUMETRIC_PREVIOUS_DEPTH,verified") ==
            std::string::npos ||
        authorityText.find(
            "PRE_WATER_DEPTH_CURRENT,verified") ==
            std::string::npos)
    {
        return 15;
    }

    if (authorityText.find(
            "OPAQUE_FINAL_DEPTH") !=
        std::string::npos)
    {
        return 16;
    }

    if (CountOccurrences(
            scheduleText,
            "after_opaque_world_mrt") < 3 ||
        CountOccurrences(
            scheduleText,
            "after_cameraz") < 3 ||
        CountOccurrences(
            scheduleText,
            "after_stock_ao") < 3 ||
        CountOccurrences(
            scheduleText,
            "before_ambient_screen_space_composite") < 3 ||
        CountOccurrences(
            scheduleText,
            "at_ambient_screen_space_composite") < 3 ||
        CountOccurrences(
            scheduleText,
            "after_ambient_screen_space_composite") < 3)
    {
        return 17;
    }

    if (CountOccurrences(
            scheduleText,
            "before_volumetric_apply") < 1 ||
        CountOccurrences(
            scheduleText,
            "after_volumetric_apply") < 1 ||
        CountOccurrences(
            scheduleText,
            "publish_pre_water_depth") < 1)
    {
        return 18;
    }

    if (scheduleText.find(
            "after_opaque_geometry_final_depth") !=
            std::string::npos ||
        scheduleText.find(
            "before_indirect_lighting_composite") !=
            std::string::npos ||
        scheduleText.find(
            "at_lighting_composite") !=
            std::string::npos ||
        scheduleText.find(
            "after_lighting_composite") !=
            std::string::npos)
    {
        return 19;
    }

    if (authorityText.find(
            "VOLUMETRIC_PREVIOUS_DEPTH,PRE_WATER_DEPTH_CURRENT") ==
            std::string::npos ||
        authorityText.find(
            "MAIN_DEPTH_COPY") ==
            std::string::npos)
    {
        return 20;
    }

    if (!CurrentAvailabilityIs(
            authorityText,
            "VOLUMETRIC_DENSITY_A",
            false) ||
        !CurrentAvailabilityIs(
            authorityText,
            "VOLUMETRIC_PREVIOUS_DEPTH",
            false) ||
        !CurrentAvailabilityIs(
            authorityText,
            "PRE_WATER_DEPTH_CURRENT",
            false))
    {
        return 21;
    }

    if (!CurrentAvailabilityIs(
            authorityText,
            "MAIN_HDR",
            true) ||
        !CurrentAvailabilityIs(
            authorityText,
            "STOCK_CAMERAZ",
            true) ||
        !CurrentAvailabilityIs(
            authorityText,
            "SAO_FILTERED",
            true) ||
        !CurrentAvailabilityIs(
            authorityText,
            "RAW_INDIRECT",
            true))
    {
        return 22;
    }

    if (!CaptureObservationFramesBounded(
            authorityText))
    {
        return 23;
    }

    if (authorityText.find(
            "[session_identity_registry]") ==
            std::string::npos ||
        authorityText.find(
            "[current_frame_semantic_views]") ==
            std::string::npos ||
        authorityText.find(
            "[capture_local_observations]") ==
            std::string::npos ||
        authorityText.find(
            "[capture_local_promotions]") ==
            std::string::npos ||
        authorityText.find(
            "[content_epoch_transitions]") ==
            std::string::npos)
    {
        return 24;
    }

    for (auto*& rtv : cameraMipRtvs)
    {
        if (rtv) rtv->Release();
    }

    Release(volume);
    Release(repartition);
    Release(depthHistory);
    Release(depthEarly);
    Release(depth);
    Release(noise);
    Release(currentVl);
    Release(previousVl);
    Release(cameraZ);
    Release(rawIndirect);
    Release(aoFinal);
    Release(aoTemp);
    Release(aoB);
    Release(aoA);
    Release(aux);
    Release(normal);
    Release(motion);
    Release(hdr);

    if (cs) cs->Release();
    if (ps) ps->Release();
    if (vs) vs->Release();

    if (context) context->Release();
    if (device) device->Release();
    if (swap) swap->Release();

    DestroyWindow(window);
    FreeLibrary(gzfx);

    std::wcout
        << L"Corrected semantic authority foundation harness passed.\n";

    return 0;
}

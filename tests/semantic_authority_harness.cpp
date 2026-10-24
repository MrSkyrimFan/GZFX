#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
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

    void Release(Texture2D& texture)
    {
        if (texture.srv) texture.srv->Release();
        if (texture.rtv) texture.rtv->Release();
        if (texture.texture) texture.texture->Release();
        texture = {};
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

    // First present lets GZFX bind the primary swap chain/context and
    // starts the fast semantic evidence window.
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

    if (!hdr.texture ||
        !motion.texture ||
        !normal.texture ||
        !aux.texture ||
        !aoA.texture ||
        !aoB.texture ||
        !aoTemp.texture ||
        !aoFinal.texture ||
        !rawIndirect.texture ||
        !cameraZ.texture)
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

    D3D11_TEXTURE2D_DESC depthDesc{};
    depthDesc.Width = 320;
    depthDesc.Height = 180;
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
    ID3D11Texture2D* depthEarly = nullptr;
    ID3D11Texture2D* depthFinal = nullptr;

    if (FAILED(
            device->CreateTexture2D(
                &depthDesc,
                nullptr,
                &depth)) ||
        FAILED(
            device->CreateTexture2D(
                &depthDesc,
                nullptr,
                &depthEarly)) ||
        FAILED(
            device->CreateTexture2D(
                &depthDesc,
                nullptr,
                &depthFinal)))
    {
        return 13;
    }

    D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format =
        DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsvDesc.ViewDimension =
        D3D11_DSV_DIMENSION_TEXTURE2D;

    ID3D11DepthStencilView* depthDsv = nullptr;

    if (FAILED(
            device->CreateDepthStencilView(
                depth,
                &dsvDesc,
                &depthDsv)))
    {
        return 14;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC depthSrvDesc{};
    depthSrvDesc.Format =
        DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    depthSrvDesc.ViewDimension =
        D3D11_SRV_DIMENSION_TEXTURE2D;
    depthSrvDesc.Texture2D.MipLevels = 1;

    ID3D11ShaderResourceView* depthFinalSrv = nullptr;

    if (FAILED(
            device->CreateShaderResourceView(
                depthFinal,
                &depthSrvDesc,
                &depthFinalSrv)))
    {
        return 15;
    }

    D3D11_TEXTURE3D_DESC volumeDesc{};
    volumeDesc.Width = 320;
    volumeDesc.Height = 192;
    volumeDesc.Depth = 90;
    volumeDesc.MipLevels = 1;
    volumeDesc.Format =
        DXGI_FORMAT_R16_FLOAT;
    volumeDesc.Usage =
        D3D11_USAGE_DEFAULT;
    volumeDesc.BindFlags =
        D3D11_BIND_UNORDERED_ACCESS |
        D3D11_BIND_SHADER_RESOURCE;

    ID3D11Texture3D* volume = nullptr;
    ID3D11UnorderedAccessView* volumeUav = nullptr;

    if (FAILED(
            device->CreateTexture3D(
                &volumeDesc,
                nullptr,
                &volume)) ||
        FAILED(
            device->CreateUnorderedAccessView(
                volume,
                nullptr,
                &volumeUav)))
    {
        return 16;
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

    for (UINT frame = 0;
         frame < 7;
         ++frame)
    {
        context->RSSetViewports(
            1,
            &full);

        // From the second synthetic frame onward, the authority already
        // knows MAIN_DEPTH and can classify the early copy.
        context->CopyResource(
            depthEarly,
            depth);

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
            depthDsv);

        context->Draw(3, 0);

        // First post-MRT depth copy is the synthetic final opaque depth.
        context->CopyResource(
            depthFinal,
            depth);

        // Construct the full CameraZ mip hierarchy.
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
                static_cast<FLOAT>(
                    width);
            viewport.Height =
                static_cast<FLOAT>(
                    height);
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
                    &depthFinalSrv);
            }

            context->Draw(3, 0);
        }

        ID3D11ShaderResourceView* nullSrv = nullptr;
        context->PSSetShaderResources(
            0,
            1,
            &nullSrv);

        context->RSSetViewports(
            1,
            &full);

        const bool even =
            (frame % 2) == 0;

        Texture2D& previous =
            even ? aoA : aoB;

        Texture2D& current =
            even ? aoB : aoA;

        context->OMSetRenderTargets(
            1,
            &current.rtv,
            nullptr);

        std::array<
            ID3D11ShaderResourceView*,
            4> aoInputs =
        {
            cameraZ.srv,
            motion.srv,
            normal.srv,
            previous.srv
        };

        context->PSSetShaderResources(
            0,
            static_cast<UINT>(
                aoInputs.size()),
            aoInputs.data());

        context->Draw(3, 0);

        std::array<
            ID3D11ShaderResourceView*,
            4> clearAo =
        {
            nullptr,
            nullptr,
            nullptr,
            nullptr
        };

        context->PSSetShaderResources(
            0,
            static_cast<UINT>(
                clearAo.size()),
            clearAo.data());

        context->OMSetRenderTargets(
            1,
            &aoTemp.rtv,
            nullptr);

        context->PSSetShaderResources(
            0,
            1,
            &current.srv);

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

        std::array<
            ID3D11ShaderResourceView*,
            3> clearIndirect =
        {
            nullptr,
            nullptr,
            nullptr
        };

        context->PSSetShaderResources(
            0,
            static_cast<UINT>(
                clearIndirect.size()),
            clearIndirect.data());

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

        std::array<
            ID3D11ShaderResourceView*,
            2> clearComposite =
        {
            nullptr,
            nullptr
        };

        context->PSSetShaderResources(
            0,
            static_cast<UINT>(
                clearComposite.size()),
            clearComposite.data());

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
            &volumeUav,
            nullptr);

        context->Dispatch(
            1,
            1,
            1);

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
        return 17;
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
        "MAIN_DEPTH,verified",
        "MOTION,verified",
        "NORMAL_MASK_CURRENT,verified",
        "AUXILIARY_RT3,verified",
        "OPAQUE_FINAL_DEPTH,strong",
        "CAMERAZ,verified",
        "SAO_RAWAO_CURRENT,strong",
        "SAO_FILTERED,strong",
        "RAW_INDIRECT,strong"
    };

    for (const auto* required :
         requiredSemantics)
    {
        if (authorityText.find(
                required) ==
            std::string::npos)
        {
            return 18;
        }
    }

    if (authorityText.find(
            "VOLUMETRIC_DENSITY_A,verified") ==
            std::string::npos ||
        authorityText.find(
            "environment=exterior") ==
            std::string::npos)
    {
        return 19;
    }

    const std::array<
        const char*,
        6> requiredStages =
    {
        "after_opaque_geometry_final_depth",
        "after_cameraz",
        "after_stock_ao",
        "before_indirect_lighting_composite",
        "at_lighting_composite",
        "after_lighting_composite"
    };

    for (const auto* required :
         requiredStages)
    {
        if (scheduleText.find(
                required) ==
            std::string::npos)
        {
            return 20;
        }
    }

    if (volumeUav) volumeUav->Release();
    if (volume) volume->Release();

    if (depthFinalSrv) depthFinalSrv->Release();
    if (depthDsv) depthDsv->Release();
    if (depthFinal) depthFinal->Release();
    if (depthEarly) depthEarly->Release();
    if (depth) depth->Release();

    for (auto*& rtv : cameraMipRtvs)
    {
        if (rtv) rtv->Release();
    }

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
        << L"Semantic authority foundation harness passed.\n";

    return 0;
}

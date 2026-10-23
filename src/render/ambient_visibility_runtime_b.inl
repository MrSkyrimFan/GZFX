        bool EnsureRuntime(
            ID3D11Device* device,
            const D3D11_TEXTURE2D_DESC& sceneDesc,
            std::string& error)
        {
            const UINT halfWidth = (sceneDesc.Width + 1) / 2;
            const UINT halfHeight = (sceneDesc.Height + 1) / 2;

            if (g_runtime.device == device &&
                g_runtime.fullWidth == sceneDesc.Width &&
                g_runtime.fullHeight == sceneDesc.Height &&
                g_runtime.sceneFormat == sceneDesc.Format)
            {
                return true;
            }

            ReleaseRuntime();

            if (!device ||
                sceneDesc.Width == 0 ||
                sceneDesc.Height == 0 ||
                sceneDesc.SampleDesc.Count != 1)
            {
                error = "unsupported scene dimensions or sample count";
                return false;
            }

            g_runtime.device = device;
            device->AddRef();

            HRESULT result = device->CreateDeferredContext(
                0,
                &g_runtime.deferred);

            const std::string shaderSource = ShaderSource();
            ID3DBlob* vertexBlob = nullptr;
            ID3DBlob* analyzeBlob = nullptr;
            ID3DBlob* denoiseBlob = nullptr;
            ID3DBlob* compositeBlob = nullptr;

            if (SUCCEEDED(result) &&
                !Compile(shaderSource.c_str(), "VSMain", "vs_5_0", &vertexBlob, error))
            {
                result = E_FAIL;
            }

            if (SUCCEEDED(result) &&
                !Compile(shaderSource.c_str(), "AnalyzePS", "ps_5_0", &analyzeBlob, error))
            {
                result = E_FAIL;
            }

            if (SUCCEEDED(result) &&
                !Compile(shaderSource.c_str(), "DenoisePS", "ps_5_0", &denoiseBlob, error))
            {
                result = E_FAIL;
            }

            if (SUCCEEDED(result) &&
                !Compile(shaderSource.c_str(), "CompositePS", "ps_5_0", &compositeBlob, error))
            {
                result = E_FAIL;
            }

            if (SUCCEEDED(result))
            {
                result = device->CreateVertexShader(
                    vertexBlob->GetBufferPointer(),
                    vertexBlob->GetBufferSize(),
                    nullptr,
                    &g_runtime.vertexShader);
            }

            if (SUCCEEDED(result))
            {
                result = device->CreatePixelShader(
                    analyzeBlob->GetBufferPointer(),
                    analyzeBlob->GetBufferSize(),
                    nullptr,
                    &g_runtime.analyzeShader);
            }

            if (SUCCEEDED(result))
            {
                result = device->CreatePixelShader(
                    denoiseBlob->GetBufferPointer(),
                    denoiseBlob->GetBufferSize(),
                    nullptr,
                    &g_runtime.denoiseShader);
            }

            if (SUCCEEDED(result))
            {
                result = device->CreatePixelShader(
                    compositeBlob->GetBufferPointer(),
                    compositeBlob->GetBufferSize(),
                    nullptr,
                    &g_runtime.compositeShader);
            }

            D3D11_SAMPLER_DESC linearDesc{};
            linearDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            linearDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
            linearDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
            linearDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            linearDesc.MaxLOD = D3D11_FLOAT32_MAX;

            if (SUCCEEDED(result))
            {
                result = device->CreateSamplerState(
                    &linearDesc,
                    &g_runtime.linearSampler);
            }

            D3D11_SAMPLER_DESC pointDesc = linearDesc;
            pointDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;

            if (SUCCEEDED(result))
            {
                result = device->CreateSamplerState(
                    &pointDesc,
                    &g_runtime.pointSampler);
            }

            D3D11_BUFFER_DESC parameterDesc{};
            parameterDesc.ByteWidth = sizeof(Parameters);
            parameterDesc.Usage = D3D11_USAGE_DEFAULT;
            parameterDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

            if (SUCCEEDED(result))
            {
                result = device->CreateBuffer(
                    &parameterDesc,
                    nullptr,
                    &g_runtime.parameters);
            }

            D3D11_TEXTURE2D_DESC copyDesc = sceneDesc;
            copyDesc.Usage = D3D11_USAGE_DEFAULT;
            copyDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            copyDesc.CPUAccessFlags = 0;
            copyDesc.MiscFlags = 0;

            if (SUCCEEDED(result))
            {
                result = device->CreateTexture2D(
                    &copyDesc,
                    nullptr,
                    &g_runtime.sceneCopy);
            }

            if (SUCCEEDED(result))
            {
                result = device->CreateShaderResourceView(
                    g_runtime.sceneCopy,
                    nullptr,
                    &g_runtime.sceneCopySrv);
            }

            if (SUCCEEDED(result) &&
                !CreateSurface(device, halfWidth, halfHeight, g_runtime.first))
            {
                result = E_FAIL;
            }

            if (SUCCEEDED(result) &&
                !CreateSurface(device, halfWidth, halfHeight, g_runtime.second))
            {
                result = E_FAIL;
            }

            D3D11_TEXTURE2D_DESC compositeDesc = sceneDesc;
            compositeDesc.Usage = D3D11_USAGE_DEFAULT;
            compositeDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
            compositeDesc.CPUAccessFlags = 0;
            compositeDesc.MiscFlags = 0;

            if (SUCCEEDED(result))
            {
                result = device->CreateTexture2D(
                    &compositeDesc,
                    nullptr,
                    &g_runtime.composite);
            }

            if (SUCCEEDED(result))
            {
                result = device->CreateRenderTargetView(
                    g_runtime.composite,
                    nullptr,
                    &g_runtime.compositeRtv);
            }

            if (vertexBlob) vertexBlob->Release();
            if (analyzeBlob) analyzeBlob->Release();
            if (denoiseBlob) denoiseBlob->Release();
            if (compositeBlob) compositeBlob->Release();

            if (FAILED(result))
            {
                if (error.empty())
                {
                    error = "ambient visibility resource allocation failed";
                }

                ReleaseRuntime();
                return false;
            }

            if (!CreateGpuTimers(device))
            {
                error = "GPU timestamp query allocation failed";
                ReleaseRuntime();
                return false;
            }

            g_runtime.fullWidth = sceneDesc.Width;
            g_runtime.fullHeight = sceneDesc.Height;
            g_runtime.halfWidth = halfWidth;
            g_runtime.halfHeight = halfHeight;
            g_runtime.sceneFormat = sceneDesc.Format;
            return true;
        }

        bool RunPass(
            ID3D11DeviceContext* immediate,
            ID3D11RenderTargetView* const* targets,
            const UINT targetCount,
            ID3D11PixelShader* shader,
            ID3D11ShaderResourceView* const* resources,
            const UINT resourceCount,
            const UINT width,
            const UINT height,
            const Parameters& parameters,
            ID3D11Buffer* cameraBuffer,
            std::string& error)
        {
            g_runtime.deferred->ClearState();
            g_runtime.deferred->UpdateSubresource(
                g_runtime.parameters,
                0,
                nullptr,
                &parameters,
                0,
                0);

            D3D11_VIEWPORT viewport{};
            viewport.Width = static_cast<float>(width);
            viewport.Height = static_cast<float>(height);
            viewport.MinDepth = 0.0f;
            viewport.MaxDepth = 1.0f;

            g_runtime.deferred->OMSetRenderTargets(
                targetCount,
                targets,
                nullptr);
            g_runtime.deferred->RSSetViewports(1, &viewport);
            g_runtime.deferred->IASetInputLayout(nullptr);
            g_runtime.deferred->IASetPrimitiveTopology(
                D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            g_runtime.deferred->VSSetShader(
                g_runtime.vertexShader,
                nullptr,
                0);
            g_runtime.deferred->PSSetShader(shader, nullptr, 0);
            g_runtime.deferred->PSSetConstantBuffers(
                0,
                1,
                &g_runtime.parameters);

            if (cameraBuffer)
            {
                g_runtime.deferred->PSSetConstantBuffers(
                    12,
                    1,
                    &cameraBuffer);
            }

            g_runtime.deferred->PSSetShaderResources(
                0,
                resourceCount,
                resources);

            ID3D11SamplerState* samplers[2] =
            {
                g_runtime.linearSampler,
                g_runtime.pointSampler
            };

            g_runtime.deferred->PSSetSamplers(0, 2, samplers);
            g_runtime.deferred->Draw(3, 0);

            ID3D11ShaderResourceView* nullResources[4] =
            {
                nullptr,
                nullptr,
                nullptr,
                nullptr
            };

            g_runtime.deferred->PSSetShaderResources(
                0,
                4,
                nullResources);
            g_runtime.deferred->OMSetRenderTargets(0, nullptr, nullptr);

            ID3D11CommandList* commands = nullptr;
            const HRESULT result = g_runtime.deferred->FinishCommandList(
                FALSE,
                &commands);

            if (SUCCEEDED(result) && commands)
            {
                immediate->ExecuteCommandList(commands, TRUE);
            }

            if (commands)
            {
                commands->Release();
            }

            if (FAILED(result))
            {
                std::ostringstream stream;
                stream
                    << "FinishCommandList HRESULT=0x"
                    << std::hex
                    << std::uppercase
                    << static_cast<std::uint32_t>(result);
                error = stream.str();
                return false;
            }

            return true;
        }


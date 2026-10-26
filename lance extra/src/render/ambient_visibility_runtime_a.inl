        DXGI_FORMAT DepthSrvFormat(const DXGI_FORMAT format)
        {
            if (format == DXGI_FORMAT_R24G8_TYPELESS)
            {
                return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
            }

            if (format == DXGI_FORMAT_R32_TYPELESS)
            {
                return DXGI_FORMAT_R32_FLOAT;
            }

            if (format == DXGI_FORMAT_R16_TYPELESS)
            {
                return DXGI_FORMAT_R16_UNORM;
            }

            return DXGI_FORMAT_UNKNOWN;
        }

        bool CreateSurface(
            ID3D11Device* device,
            const UINT width,
            const UINT height,
            SurfacePair& surface)
        {
            D3D11_TEXTURE2D_DESC aoDesc{};
            aoDesc.Width = width;
            aoDesc.Height = height;
            aoDesc.MipLevels = 1;
            aoDesc.ArraySize = 1;
            aoDesc.Format = DXGI_FORMAT_R16_FLOAT;
            aoDesc.SampleDesc.Count = 1;
            aoDesc.Usage = D3D11_USAGE_DEFAULT;
            aoDesc.BindFlags =
                D3D11_BIND_RENDER_TARGET |
                D3D11_BIND_SHADER_RESOURCE;

            HRESULT result = device->CreateTexture2D(
                &aoDesc,
                nullptr,
                &surface.ao);

            if (SUCCEEDED(result))
            {
                result = device->CreateRenderTargetView(
                    surface.ao,
                    nullptr,
                    &surface.aoRtv);
            }

            if (SUCCEEDED(result))
            {
                result = device->CreateShaderResourceView(
                    surface.ao,
                    nullptr,
                    &surface.aoSrv);
            }

            D3D11_TEXTURE2D_DESC indirectDesc = aoDesc;
            indirectDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;

            if (SUCCEEDED(result))
            {
                result = device->CreateTexture2D(
                    &indirectDesc,
                    nullptr,
                    &surface.indirect);
            }

            if (SUCCEEDED(result))
            {
                result = device->CreateRenderTargetView(
                    surface.indirect,
                    nullptr,
                    &surface.indirectRtv);
            }

            if (SUCCEEDED(result))
            {
                result = device->CreateShaderResourceView(
                    surface.indirect,
                    nullptr,
                    &surface.indirectSrv);
            }

            return SUCCEEDED(result);
        }

        bool CreateHistorySurface(
            ID3D11Device* device,
            const UINT width,
            const UINT height,
            HistorySurface& history)
        {
            if (!CreateSurface(
                    device,
                    width,
                    height,
                    history.signals))
            {
                return false;
            }

            D3D11_TEXTURE2D_DESC geometryDesc{};
            geometryDesc.Width = width;
            geometryDesc.Height = height;
            geometryDesc.MipLevels = 1;
            geometryDesc.ArraySize = 1;
            geometryDesc.Format =
                DXGI_FORMAT_R16G16B16A16_FLOAT;
            geometryDesc.SampleDesc.Count = 1;
            geometryDesc.Usage =
                D3D11_USAGE_DEFAULT;
            geometryDesc.BindFlags =
                D3D11_BIND_RENDER_TARGET |
                D3D11_BIND_SHADER_RESOURCE;

            HRESULT result =
                device->CreateTexture2D(
                    &geometryDesc,
                    nullptr,
                    &history.geometry);

            if (SUCCEEDED(result))
            {
                result =
                    device->CreateRenderTargetView(
                        history.geometry,
                        nullptr,
                        &history.geometryRtv);
            }

            if (SUCCEEDED(result))
            {
                result =
                    device->CreateShaderResourceView(
                        history.geometry,
                        nullptr,
                        &history.geometrySrv);
            }

            return SUCCEEDED(result);
        }


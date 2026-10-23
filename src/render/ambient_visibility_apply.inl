    bool Apply(
        const std::uint64_t frame,
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        const gzfx::diagnostics::resource_intelligence::LiveInputs& inputs) noexcept
    {
        try
        {
            std::scoped_lock lock(g_mutex);
            const std::uint32_t mode =
                gzfx::render::state::GetDevelopmentMode();

            if (mode != 1 && mode != 2)
            {
                WriteLog(frame, "skipped_mode");
                return false;
            }

            const auto& settings = gzfx::settings::Get();
            if (!settings.horizonOcclusionEnabled &&
                !settings.indirectBounceEnabled)
            {
                WriteLog(frame, "skipped_disabled");
                return false;
            }

            if (!inputs.sceneColorTexture ||
                !inputs.depthTexture ||
                !inputs.cameraBuffer)
            {
                WriteLog(frame, "skipped_inputs");
                return false;
            }

            std::string error;
            if (!EnsureRuntime(device, inputs.sceneColorDesc, error))
            {
                WriteLog(frame, "failed_runtime", error);
                return false;
            }

            const DXGI_FORMAT depthFormat =
                DepthSrvFormat(inputs.depthDesc.Format);

            if (depthFormat == DXGI_FORMAT_UNKNOWN)
            {
                WriteLog(frame, "failed_depth_format");
                return false;
            }

            D3D11_SHADER_RESOURCE_VIEW_DESC depthSrvDesc{};
            depthSrvDesc.Format = depthFormat;
            depthSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            depthSrvDesc.Texture2D.MipLevels = 1;

            ID3D11ShaderResourceView* depthSrv = nullptr;
            const HRESULT depthResult = device->CreateShaderResourceView(
                inputs.depthTexture,
                &depthSrvDesc,
                &depthSrv);

            if (FAILED(depthResult) || !depthSrv)
            {
                WriteLog(frame, "failed_depth_srv");
                return false;
            }

            context->CopyResource(
                g_runtime.sceneCopy,
                inputs.sceneColorTexture);

            Parameters parameters{};
            parameters.fullInvWidth =
                1.0f / static_cast<float>(g_runtime.fullWidth);
            parameters.fullInvHeight =
                1.0f / static_cast<float>(g_runtime.fullHeight);
            parameters.halfInvWidth =
                1.0f / static_cast<float>(g_runtime.halfWidth);
            parameters.halfInvHeight =
                1.0f / static_cast<float>(g_runtime.halfHeight);
            parameters.aoStrength =
                std::clamp(
                    settings.horizonOcclusionStrength,
                    0.0f,
                    1.0f);
            parameters.aoRadius =
                std::clamp(
                    settings.horizonOcclusionRadiusPixels,
                    4.0f,
                    96.0f);
            parameters.indirectStrength =
                std::clamp(
                    settings.indirectBounceStrength,
                    0.0f,
                    0.60f);
            parameters.indirectRadius =
                std::clamp(
                    settings.indirectBounceRadiusPixels,
                    4.0f,
                    128.0f);
            parameters.indirectSaturation =
                std::clamp(
                    settings.indirectBounceSaturation,
                    0.0f,
                    1.50f);
            parameters.quality =
                std::min<std::uint32_t>(
                    settings.horizonOcclusionQuality,
                    3u);
            parameters.mode = mode;
            parameters.aoEnabled =
                settings.horizonOcclusionEnabled ? 1u : 0u;
            parameters.indirectEnabled =
                settings.indirectBounceEnabled ? 1u : 0u;
            parameters.aoBias = 0.025f;
            parameters.aoPower = 1.35f;
            parameters.indirectEnergyLimit = 0.22f;

            GpuTimerSlot* gpuTimer = BeginGpuTimer(context, frame);

            ID3D11RenderTargetView* analyzeTargets[2] =
            {
                g_runtime.first.aoRtv,
                g_runtime.first.indirectRtv
            };

            ID3D11ShaderResourceView* analyzeResources[2] =
            {
                g_runtime.sceneCopySrv,
                depthSrv
            };

            if (!RunPass(
                    context,
                    analyzeTargets,
                    2,
                    g_runtime.analyzeShader,
                    analyzeResources,
                    2,
                    g_runtime.halfWidth,
                    g_runtime.halfHeight,
                    parameters,
                    inputs.cameraBuffer,
                    error))
            {
                AbortGpuTimer(context, gpuTimer, 1);
                depthSrv->Release();
                WriteLog(frame, "failed_analyze", error);
                return false;
            }

            StampGpuTimer(context, gpuTimer, 1);

            ID3D11RenderTargetView* horizontalTargets[2] =
            {
                g_runtime.second.aoRtv,
                g_runtime.second.indirectRtv
            };

            ID3D11ShaderResourceView* horizontalResources[4] =
            {
                nullptr,
                nullptr,
                g_runtime.first.aoSrv,
                g_runtime.first.indirectSrv
            };

            parameters.axisX = 1.0f;
            parameters.axisY = 0.0f;

            if (!RunPass(
                    context,
                    horizontalTargets,
                    2,
                    g_runtime.denoiseShader,
                    horizontalResources,
                    4,
                    g_runtime.halfWidth,
                    g_runtime.halfHeight,
                    parameters,
                    nullptr,
                    error))
            {
                AbortGpuTimer(context, gpuTimer, 2);
                depthSrv->Release();
                WriteLog(frame, "failed_denoise_horizontal", error);
                return false;
            }

            StampGpuTimer(context, gpuTimer, 2);

            ID3D11RenderTargetView* verticalTargets[2] =
            {
                g_runtime.first.aoRtv,
                g_runtime.first.indirectRtv
            };

            ID3D11ShaderResourceView* verticalResources[4] =
            {
                nullptr,
                nullptr,
                g_runtime.second.aoSrv,
                g_runtime.second.indirectSrv
            };

            parameters.axisX = 0.0f;
            parameters.axisY = 1.0f;

            if (!RunPass(
                    context,
                    verticalTargets,
                    2,
                    g_runtime.denoiseShader,
                    verticalResources,
                    4,
                    g_runtime.halfWidth,
                    g_runtime.halfHeight,
                    parameters,
                    nullptr,
                    error))
            {
                AbortGpuTimer(context, gpuTimer, 3);
                depthSrv->Release();
                WriteLog(frame, "failed_denoise_vertical", error);
                return false;
            }

            StampGpuTimer(context, gpuTimer, 3);

            ID3D11RenderTargetView* compositeTargets[1] =
            {
                g_runtime.compositeRtv
            };

            ID3D11ShaderResourceView* compositeResources[4] =
            {
                g_runtime.sceneCopySrv,
                depthSrv,
                g_runtime.first.aoSrv,
                g_runtime.first.indirectSrv
            };

            if (!RunPass(
                    context,
                    compositeTargets,
                    1,
                    g_runtime.compositeShader,
                    compositeResources,
                    4,
                    g_runtime.fullWidth,
                    g_runtime.fullHeight,
                    parameters,
                    nullptr,
                    error))
            {
                AbortGpuTimer(context, gpuTimer, 4);
                depthSrv->Release();
                WriteLog(frame, "failed_composite", error);
                return false;
            }

            StampGpuTimer(context, gpuTimer, 4);
            FinishGpuTimer(context, gpuTimer);

            context->CopyResource(
                inputs.sceneColorTexture,
                g_runtime.composite);

            depthSrv->Release();
            ++g_applyCount;
            CaptureFields(frame, device, context);
            WriteLog(frame, "applied");
            return true;
        }
        catch (...)
        {
            WriteLog(frame, "exception");
            return false;
        }
    }
}

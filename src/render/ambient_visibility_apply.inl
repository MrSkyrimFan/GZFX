    bool Apply(
        const std::uint64_t frame,
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        const gzfx::diagnostics::resource_intelligence::LiveInputs& inputs,
        const bool allowAo) noexcept
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

            const bool effectiveAoEnabled =
                allowAo &&
                settings.horizonOcclusionEnabled;

            if (!effectiveAoEnabled &&
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

            if (!EnsureRuntime(
                    device,
                    inputs.sceneColorDesc,
                    error))
            {
                WriteLog(
                    frame,
                    "failed_runtime",
                    error);

                return false;
            }

            const DXGI_FORMAT depthFormat =
                DepthSrvFormat(
                    inputs.depthDesc.Format);

            if (depthFormat ==
                DXGI_FORMAT_UNKNOWN)
            {
                WriteLog(
                    frame,
                    "failed_depth_format");

                return false;
            }

            D3D11_SHADER_RESOURCE_VIEW_DESC
                depthSrvDesc{};

            depthSrvDesc.Format =
                depthFormat;

            depthSrvDesc.ViewDimension =
                D3D11_SRV_DIMENSION_TEXTURE2D;

            depthSrvDesc.Texture2D.MipLevels = 1;

            ID3D11ShaderResourceView* depthSrv = nullptr;

            const HRESULT depthResult =
                device->CreateShaderResourceView(
                    inputs.depthTexture,
                    &depthSrvDesc,
                    &depthSrv);

            if (FAILED(depthResult) ||
                !depthSrv)
            {
                WriteLog(
                    frame,
                    "failed_depth_srv");

                return false;
            }

            ID3D11ShaderResourceView*
                nativeNormalSrv = nullptr;

            ID3D11ShaderResourceView*
                motionSrv = nullptr;

            const bool nativeNormalValid =
                inputs.nativeNormalTexture != nullptr &&
                inputs.nativeNormalDesc.Width ==
                    g_runtime.fullWidth &&
                inputs.nativeNormalDesc.Height ==
                    g_runtime.fullHeight &&
                inputs.nativeNormalDesc.Format ==
                    DXGI_FORMAT_R8G8B8A8_UNORM &&
                inputs.nativeSurfaceFrame == frame;

            const bool motionValid =
                inputs.motionVectorTexture != nullptr &&
                inputs.motionVectorDesc.Width ==
                    g_runtime.fullWidth &&
                inputs.motionVectorDesc.Height ==
                    g_runtime.fullHeight &&
                inputs.motionVectorDesc.Format ==
                    DXGI_FORMAT_R16G16_FLOAT &&
                inputs.nativeSurfaceFrame == frame;

            if (nativeNormalValid)
            {
                if (FAILED(
                        device->CreateShaderResourceView(
                            inputs.nativeNormalTexture,
                            nullptr,
                            &nativeNormalSrv)))
                {
                    nativeNormalSrv = nullptr;
                }
            }

            if (motionValid)
            {
                if (FAILED(
                        device->CreateShaderResourceView(
                            inputs.motionVectorTexture,
                            nullptr,
                            &motionSrv)))
                {
                    motionSrv = nullptr;
                }
            }

            const bool useNativeNormal =
                nativeNormalSrv != nullptr;

            const bool temporalEnabled =
                useNativeNormal &&
                motionSrv != nullptr;

            const auto releaseInputViews =
                [&]()
                {
                    if (motionSrv)
                    {
                        motionSrv->Release();
                        motionSrv = nullptr;
                    }

                    if (nativeNormalSrv)
                    {
                        nativeNormalSrv->Release();
                        nativeNormalSrv = nullptr;
                    }

                    if (depthSrv)
                    {
                        depthSrv->Release();
                        depthSrv = nullptr;
                    }
                };

            context->CopyResource(
                g_runtime.sceneCopy,
                inputs.sceneColorTexture);

            Parameters parameters{};

            parameters.fullInvWidth =
                1.0f /
                static_cast<float>(
                    g_runtime.fullWidth);

            parameters.fullInvHeight =
                1.0f /
                static_cast<float>(
                    g_runtime.fullHeight);

            parameters.halfInvWidth =
                1.0f /
                static_cast<float>(
                    g_runtime.halfWidth);

            parameters.halfInvHeight =
                1.0f /
                static_cast<float>(
                    g_runtime.halfHeight);

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
                effectiveAoEnabled
                ? 1u
                : 0u;

            parameters.indirectEnabled =
                settings.indirectBounceEnabled
                ? 1u
                : 0u;

            parameters.aoBias = 0.025f;
            parameters.aoPower = 1.35f;
            parameters.indirectEnergyLimit = 0.22f;

            parameters.nativeNormalEnabled =
                useNativeNormal
                ? 1u
                : 0u;

            parameters.temporalEnabled =
                temporalEnabled
                ? 1u
                : 0u;

            parameters.historyValid =
                temporalEnabled &&
                g_runtime.historyValid
                ? 1u
                : 0u;

            const bool historyReused =
                parameters.historyValid != 0u;

            parameters.temporalBlend = 0.18f;
            parameters.frameIndex =
                static_cast<std::uint32_t>(
                    frame & 0xFFFFFFFFull);

            GpuTimerSlot* gpuTimer =
                BeginGpuTimer(
                    context,
                    frame);

            ID3D11RenderTargetView*
                analyzeTargets[2] =
            {
                g_runtime.first.aoRtv,
                g_runtime.first.indirectRtv
            };

            ID3D11ShaderResourceView*
                analyzeResources[5] =
            {
                g_runtime.sceneCopySrv,
                depthSrv,
                nullptr,
                nullptr,
                nativeNormalSrv
            };

            if (!RunPass(
                    context,
                    analyzeTargets,
                    2,
                    g_runtime.analyzeShader,
                    analyzeResources,
                    5,
                    g_runtime.halfWidth,
                    g_runtime.halfHeight,
                    parameters,
                    inputs.cameraBuffer,
                    error))
            {
                AbortGpuTimer(
                    context,
                    gpuTimer,
                    1);

                releaseInputViews();

                WriteLog(
                    frame,
                    "failed_analyze",
                    error);

                return false;
            }

            StampGpuTimer(
                context,
                gpuTimer,
                1);

            SurfacePair* spatialInput =
                &g_runtime.first;

            std::size_t historyWriteIndex =
                g_runtime.historyWriteIndex;

            if (temporalEnabled)
            {
                const std::size_t historyReadIndex =
                    1 -
                    historyWriteIndex;

                auto& historyWrite =
                    g_runtime.history[
                        historyWriteIndex];

                auto& historyRead =
                    g_runtime.history[
                        historyReadIndex];

                ID3D11RenderTargetView*
                    temporalTargets[3] =
                {
                    historyWrite.signals.aoRtv,
                    historyWrite.signals.indirectRtv,
                    historyWrite.geometryRtv
                };

                ID3D11ShaderResourceView*
                    temporalResources[9] =
                {
                    g_runtime.sceneCopySrv,
                    depthSrv,
                    g_runtime.first.aoSrv,
                    g_runtime.first.indirectSrv,
                    nativeNormalSrv,
                    motionSrv,
                    g_runtime.historyValid
                        ? historyRead.signals.aoSrv
                        : nullptr,
                    g_runtime.historyValid
                        ? historyRead.signals.indirectSrv
                        : nullptr,
                    g_runtime.historyValid
                        ? historyRead.geometrySrv
                        : nullptr
                };

                if (!RunPass(
                        context,
                        temporalTargets,
                        3,
                        g_runtime.temporalShader,
                        temporalResources,
                        9,
                        g_runtime.halfWidth,
                        g_runtime.halfHeight,
                        parameters,
                        inputs.cameraBuffer,
                        error))
                {
                    AbortGpuTimer(
                        context,
                        gpuTimer,
                        2);

                    g_runtime.historyValid = false;
                    releaseInputViews();

                    WriteLog(
                        frame,
                        "failed_temporal",
                        error);

                    return false;
                }

                spatialInput =
                    &historyWrite.signals;
            }
            else
            {
                g_runtime.historyValid = false;
            }

            StampGpuTimer(
                context,
                gpuTimer,
                2);

            ID3D11RenderTargetView*
                horizontalTargets[2] =
            {
                g_runtime.second.aoRtv,
                g_runtime.second.indirectRtv
            };

            ID3D11ShaderResourceView*
                horizontalResources[4] =
            {
                nullptr,
                nullptr,
                spatialInput->aoSrv,
                spatialInput->indirectSrv
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
                AbortGpuTimer(
                    context,
                    gpuTimer,
                    3);

                releaseInputViews();

                WriteLog(
                    frame,
                    "failed_denoise_horizontal",
                    error);

                return false;
            }

            StampGpuTimer(
                context,
                gpuTimer,
                3);

            ID3D11RenderTargetView*
                verticalTargets[2] =
            {
                g_runtime.first.aoRtv,
                g_runtime.first.indirectRtv
            };

            ID3D11ShaderResourceView*
                verticalResources[4] =
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
                AbortGpuTimer(
                    context,
                    gpuTimer,
                    4);

                releaseInputViews();

                WriteLog(
                    frame,
                    "failed_denoise_vertical",
                    error);

                return false;
            }

            StampGpuTimer(
                context,
                gpuTimer,
                4);

            ID3D11RenderTargetView*
                compositeTargets[1] =
            {
                g_runtime.compositeRtv
            };

            ID3D11ShaderResourceView*
                compositeResources[4] =
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
                AbortGpuTimer(
                    context,
                    gpuTimer,
                    5);

                releaseInputViews();

                WriteLog(
                    frame,
                    "failed_composite",
                    error);

                return false;
            }

            StampGpuTimer(
                context,
                gpuTimer,
                5);

            FinishGpuTimer(
                context,
                gpuTimer);

            context->CopyResource(
                inputs.sceneColorTexture,
                g_runtime.composite);

            if (temporalEnabled)
            {
                g_runtime.historyValid = true;
                g_runtime.historyWriteIndex =
                    1 -
                    historyWriteIndex;
            }

            const bool usedMotion =
                motionSrv != nullptr;

            releaseInputViews();

            ++g_applyCount;

            CaptureFields(
                frame,
                device,
                context);

            std::ostringstream detail;
            detail
                << "native_normal="
                << (useNativeNormal
                    ? "true"
                    : "false")
                << " motion="
                << (usedMotion
                    ? "true"
                    : "false")
                << " temporal="
                << (temporalEnabled
                    ? "true"
                    : "false")
                << " history="
                << (g_runtime.historyValid
                    ? "true"
                    : "false")
                << " history_reused="
                << (historyReused
                    ? "true"
                    : "false");

            WriteLog(
                frame,
                "applied",
                detail.str());

            return true;
        }
        catch (...)
        {
            WriteLog(
                frame,
                "exception");

            return false;
        }
    }
}

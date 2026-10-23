#include "gzfx/render/ambient_depth_stack.hpp"

#include "gzfx/paths.hpp"
#include "gzfx/render/runtime_state.hpp"
#include "gzfx/settings.hpp"
#include "gzfx/version.hpp"

#include <Windows.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace gzfx::render::ambient_depth
{
    namespace fs = std::filesystem;

    namespace
    {
        struct Parameters
        {
            float fullInvWidth = 0.0f;
            float fullInvHeight = 0.0f;
            float halfInvWidth = 0.0f;
            float halfInvHeight = 0.0f;

            float aoStrength = 0.25f;
            float aoRadius = 28.0f;
            float indirectStrength = 0.12f;
            float indirectRadius = 36.0f;

            float indirectSaturation = 1.0f;
            std::uint32_t quality = 2;
            std::uint32_t mode = 1;
            std::uint32_t aoEnabled = 1;

            std::uint32_t indirectEnabled = 1;
            float aoBias = 0.025f;
            float aoPower = 1.35f;
            float indirectEnergyLimit = 0.22f;

            std::uint32_t nativeNormalEnabled = 0;
            std::uint32_t temporalEnabled = 0;
            std::uint32_t historyValid = 0;
            float temporalBlend = 0.18f;

            float axisX = 0.0f;
            float axisY = 0.0f;
            float padding0 = 0.0f;
            float padding1 = 0.0f;
        };

        struct SurfacePair
        {
            ID3D11Texture2D* ao = nullptr;
            ID3D11RenderTargetView* aoRtv = nullptr;
            ID3D11ShaderResourceView* aoSrv = nullptr;

            ID3D11Texture2D* indirect = nullptr;
            ID3D11RenderTargetView* indirectRtv = nullptr;
            ID3D11ShaderResourceView* indirectSrv = nullptr;
        };

        struct HistorySurface
        {
            SurfacePair signals{};

            ID3D11Texture2D* geometry = nullptr;
            ID3D11RenderTargetView* geometryRtv = nullptr;
            ID3D11ShaderResourceView* geometrySrv = nullptr;
        };


        constexpr std::size_t kGpuTimerSlots = 4;
        constexpr std::size_t kGpuTimestampCount = 6;

        struct GpuTimerSlot
        {
            ID3D11Query* disjoint = nullptr;
            std::array<ID3D11Query*, kGpuTimestampCount> timestamps{};
            bool pending = false;
            std::uint64_t frame = 0;
        };

        struct GpuTiming
        {
            bool valid = false;
            std::uint64_t frame = 0;
            double analyzeMs = 0.0;
            double temporalMs = 0.0;
            double denoiseHorizontalMs = 0.0;
            double denoiseVerticalMs = 0.0;
            double compositeMs = 0.0;
            double totalMs = 0.0;
        };

        struct Runtime
        {
            ID3D11Device* device = nullptr;
            ID3D11DeviceContext* deferred = nullptr;

            ID3D11VertexShader* vertexShader = nullptr;
            ID3D11PixelShader* analyzeShader = nullptr;
            ID3D11PixelShader* temporalShader = nullptr;
            ID3D11PixelShader* denoiseShader = nullptr;
            ID3D11PixelShader* compositeShader = nullptr;

            ID3D11SamplerState* linearSampler = nullptr;
            ID3D11SamplerState* pointSampler = nullptr;
            ID3D11Buffer* parameters = nullptr;

            ID3D11Texture2D* sceneCopy = nullptr;
            ID3D11ShaderResourceView* sceneCopySrv = nullptr;

            SurfacePair first{};
            SurfacePair second{};
            std::array<HistorySurface, 2> history{};
            std::size_t historyWriteIndex = 0;
            bool historyValid = false;

            ID3D11Texture2D* composite = nullptr;
            ID3D11RenderTargetView* compositeRtv = nullptr;

            std::array<GpuTimerSlot, kGpuTimerSlots> gpuTimers{};
            std::size_t gpuTimerCursor = 0;
            GpuTiming latestGpuTiming{};

            UINT fullWidth = 0;
            UINT fullHeight = 0;
            UINT halfWidth = 0;
            UINT halfHeight = 0;
            DXGI_FORMAT sceneFormat = DXGI_FORMAT_UNKNOWN;
        };

        std::mutex g_mutex;
        Runtime g_runtime{};
        std::uint64_t g_applyCount = 0;
        bool g_fieldCaptureDone = false;

        bool FastTestMode()
        {
            wchar_t value[8]{};
            return GetEnvironmentVariableW(
                L"GZFX_TEST_FAST_CAPTURE",
                value,
                static_cast<DWORD>(std::size(value))) > 0;
        }

        std::uint64_t FieldCaptureFrame()
        {
            return FastTestMode() ? 1 : 600;
        }

        void WriteLog(
            const std::uint64_t frame,
            const char* status,
            const std::string& detail = {})
        {
            const fs::path root = gzfx::paths::DiagnosticsRoot();
            if (root.empty())
            {
                return;
            }

            std::error_code error;
            fs::create_directories(root, error);
            if (error)
            {
                return;
            }

            const auto& settings = gzfx::settings::Get();
            std::ofstream out(
                root / L"ambient_depth_stack.txt",
                std::ios::out | std::ios::trunc);

            if (!out)
            {
                return;
            }

            out
                << "GZFX ambient visibility stack\n"
                << "version=" << gzfx::kVersion << "\n"
                << "implementation=slice_integrated_visibility_v1\n"
                << "status=" << status << "\n"
                << "frame=" << frame << "\n"
                << "applications=" << g_applyCount << "\n"
                << "half_resolution="
                << g_runtime.halfWidth << "x"
                << g_runtime.halfHeight << "\n"
                << "horizon_enabled="
                << (settings.horizonOcclusionEnabled ? "true" : "false")
                << "\n"
                << "horizon_strength="
                << settings.horizonOcclusionStrength << "\n"
                << "horizon_radius="
                << settings.horizonOcclusionRadiusPixels << "\n"
                << "quality="
                << settings.horizonOcclusionQuality << "\n"
                << "indirect_enabled="
                << (settings.indirectBounceEnabled ? "true" : "false")
                << "\n"
                << "indirect_strength="
                << settings.indirectBounceStrength << "\n"
                << "indirect_radius="
                << settings.indirectBounceRadiusPixels << "\n"
                << "indirect_saturation="
                << settings.indirectBounceSaturation << "\n"
                << "field_capture="
                << (g_fieldCaptureDone ? "complete" : "pending")
                << "\n"
                << "gpu_timing_valid="
                << (g_runtime.latestGpuTiming.valid ? "true" : "false")
                << "\n";

            if (g_runtime.latestGpuTiming.valid)
            {
                out
                    << std::fixed
                    << std::setprecision(4)
                    << "gpu_timing_frame="
                    << g_runtime.latestGpuTiming.frame << "\n"
                    << "gpu_analyze_ms="
                    << g_runtime.latestGpuTiming.analyzeMs << "\n"
                    << "gpu_temporal_ms="
                    << g_runtime.latestGpuTiming.temporalMs << "\n"
                    << "gpu_denoise_horizontal_ms="
                    << g_runtime.latestGpuTiming.denoiseHorizontalMs << "\n"
                    << "gpu_denoise_vertical_ms="
                    << g_runtime.latestGpuTiming.denoiseVerticalMs << "\n"
                    << "gpu_composite_ms="
                    << g_runtime.latestGpuTiming.compositeMs << "\n"
                    << "gpu_total_ms="
                    << g_runtime.latestGpuTiming.totalMs << "\n";
            }

            if (!detail.empty())
            {
                out << "detail=" << detail << "\n";
            }
        }

        void ReleaseSurface(SurfacePair& surface)
        {
            if (surface.indirectSrv) surface.indirectSrv->Release();
            if (surface.indirectRtv) surface.indirectRtv->Release();
            if (surface.indirect) surface.indirect->Release();
            if (surface.aoSrv) surface.aoSrv->Release();
            if (surface.aoRtv) surface.aoRtv->Release();
            if (surface.ao) surface.ao->Release();
            surface = {};
        }

        void ReleaseRuntime()
        {
            for (auto& timer : g_runtime.gpuTimers)
            {
                for (auto*& timestamp : timer.timestamps)
                {
                    if (timestamp)
                    {
                        timestamp->Release();
                        timestamp = nullptr;
                    }
                }

                if (timer.disjoint)
                {
                    timer.disjoint->Release();
                    timer.disjoint = nullptr;
                }

                timer.pending = false;
            }

            if (g_runtime.compositeRtv) g_runtime.compositeRtv->Release();
            if (g_runtime.composite) g_runtime.composite->Release();

            for (auto& history : g_runtime.history)
            {
                if (history.geometrySrv) history.geometrySrv->Release();
                if (history.geometryRtv) history.geometryRtv->Release();
                if (history.geometry) history.geometry->Release();
                ReleaseSurface(history.signals);
            }

            ReleaseSurface(g_runtime.second);
            ReleaseSurface(g_runtime.first);
            if (g_runtime.sceneCopySrv) g_runtime.sceneCopySrv->Release();
            if (g_runtime.sceneCopy) g_runtime.sceneCopy->Release();
            if (g_runtime.parameters) g_runtime.parameters->Release();
            if (g_runtime.pointSampler) g_runtime.pointSampler->Release();
            if (g_runtime.linearSampler) g_runtime.linearSampler->Release();
            if (g_runtime.compositeShader) g_runtime.compositeShader->Release();
            if (g_runtime.denoiseShader) g_runtime.denoiseShader->Release();
            if (g_runtime.temporalShader) g_runtime.temporalShader->Release();
            if (g_runtime.analyzeShader) g_runtime.analyzeShader->Release();
            if (g_runtime.vertexShader) g_runtime.vertexShader->Release();
            if (g_runtime.deferred) g_runtime.deferred->Release();
            if (g_runtime.device) g_runtime.device->Release();

            g_runtime = {};
            g_fieldCaptureDone = false;
        }

        bool CreateGpuTimers(ID3D11Device* device)
        {
            D3D11_QUERY_DESC disjointDesc{};
            disjointDesc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;

            D3D11_QUERY_DESC timestampDesc{};
            timestampDesc.Query = D3D11_QUERY_TIMESTAMP;

            for (auto& slot : g_runtime.gpuTimers)
            {
                if (FAILED(device->CreateQuery(&disjointDesc, &slot.disjoint)))
                {
                    return false;
                }

                for (auto*& timestamp : slot.timestamps)
                {
                    if (FAILED(device->CreateQuery(&timestampDesc, &timestamp)))
                    {
                        return false;
                    }
                }
            }

            return true;
        }

        void ResolveGpuTimers(ID3D11DeviceContext* context)
        {
            for (auto& slot : g_runtime.gpuTimers)
            {
                if (!slot.pending || slot.disjoint == nullptr)
                {
                    continue;
                }

                D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint{};
                if (context->GetData(
                        slot.disjoint,
                        &disjoint,
                        sizeof(disjoint),
                        D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
                {
                    continue;
                }

                std::array<UINT64, kGpuTimestampCount> timestamps{};
                bool ready = true;

                for (std::size_t i = 0; i < timestamps.size(); ++i)
                {
                    if (context->GetData(
                            slot.timestamps[i],
                            &timestamps[i],
                            sizeof(timestamps[i]),
                            D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
                    {
                        ready = false;
                        break;
                    }
                }

                if (!ready)
                {
                    continue;
                }

                slot.pending = false;
                if (disjoint.Disjoint || disjoint.Frequency == 0)
                {
                    continue;
                }

                const double tickToMs =
                    1000.0 / static_cast<double>(disjoint.Frequency);
                const auto deltaMs =
                    [&](const std::size_t from, const std::size_t to)
                    {
                        return static_cast<double>(
                            timestamps[to] - timestamps[from]) * tickToMs;
                    };

                g_runtime.latestGpuTiming.valid = true;
                g_runtime.latestGpuTiming.frame = slot.frame;
                g_runtime.latestGpuTiming.analyzeMs = deltaMs(0, 1);
                g_runtime.latestGpuTiming.temporalMs = deltaMs(1, 2);
                g_runtime.latestGpuTiming.denoiseHorizontalMs = deltaMs(2, 3);
                g_runtime.latestGpuTiming.denoiseVerticalMs = deltaMs(3, 4);
                g_runtime.latestGpuTiming.compositeMs = deltaMs(4, 5);
                g_runtime.latestGpuTiming.totalMs = deltaMs(0, 5);
            }
        }

        GpuTimerSlot* BeginGpuTimer(
            ID3D11DeviceContext* context,
            const std::uint64_t frame)
        {
            ResolveGpuTimers(context);
            auto& slot = g_runtime.gpuTimers[g_runtime.gpuTimerCursor];
            g_runtime.gpuTimerCursor =
                (g_runtime.gpuTimerCursor + 1) % kGpuTimerSlots;

            if (slot.pending ||
                slot.disjoint == nullptr ||
                slot.timestamps[0] == nullptr)
            {
                return nullptr;
            }

            slot.frame = frame;
            context->Begin(slot.disjoint);
            context->End(slot.timestamps[0]);
            return &slot;
        }

        void StampGpuTimer(
            ID3D11DeviceContext* context,
            GpuTimerSlot* slot,
            const std::size_t index)
        {
            if (context && slot && index < slot->timestamps.size())
            {
                context->End(slot->timestamps[index]);
            }
        }

        void FinishGpuTimer(
            ID3D11DeviceContext* context,
            GpuTimerSlot* slot)
        {
            if (context && slot && slot->disjoint)
            {
                context->End(slot->disjoint);
                slot->pending = true;
            }
        }

        void AbortGpuTimer(
            ID3D11DeviceContext* context,
            GpuTimerSlot* slot,
            const std::size_t firstUnwritten)
        {
            if (!context || !slot)
            {
                return;
            }

            for (std::size_t i = firstUnwritten;
                 i < slot->timestamps.size();
                 ++i)
            {
                if (slot->timestamps[i])
                {
                    context->End(slot->timestamps[i]);
                }
            }

            FinishGpuTimer(context, slot);
        }

        bool Compile(
            const char* source,
            const char* entry,
            const char* profile,
            ID3DBlob** blob,
            std::string& error)
        {
            ID3DBlob* errors = nullptr;
            const HRESULT result = D3DCompile(
                source,
                std::strlen(source),
                "GZFXAmbientVisibility",
                nullptr,
                nullptr,
                entry,
                profile,
                D3DCOMPILE_OPTIMIZATION_LEVEL3,
                0,
                blob,
                &errors);

            if (FAILED(result))
            {
                if (errors)
                {
                    error.assign(
                        static_cast<const char*>(errors->GetBufferPointer()),
                        errors->GetBufferSize());
                }
                else
                {
                    error = "D3DCompile failed without diagnostic text.";
                }
            }

            if (errors)
            {
                errors->Release();
            }

            return SUCCEEDED(result);
        }


#include "ambient_visibility_shader.inl"
#include "ambient_visibility_runtime.inl"
#include "ambient_visibility_capture.inl"
#include "ambient_visibility_apply.inl"

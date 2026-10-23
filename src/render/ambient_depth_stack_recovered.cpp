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
            float axisX = 0.0f;
            float axisY = 0.0f;
            float padding0 = 0.0f;
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

        constexpr std::size_t kGpuTimerSlots = 4;
        constexpr std::size_t kGpuTimestampCount = 5;

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
            ID3D11PixelShader* denoiseShader = nullptr;
            ID3D11PixelShader* compositeShader = nullptr;

            ID3D11SamplerState* linearSampler = nullptr;
            ID3D11SamplerState* pointSampler = nullptr;
            ID3D11Buffer* parameters = nullptr;

            ID3D11Texture2D* sceneCopy = nullptr;
            ID3D11ShaderResourceView* sceneCopySrv = nullptr;

            SurfacePair first{};
            SurfacePair second{};

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
            const fs::path root =
                gzfx::paths::DiagnosticsRoot();

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

            const auto& settings =
                gzfx::settings::Get();

            std::ofstream out(
                root / L"ambient_depth_stack.txt",
                std::ios::out | std::ios::trunc);

            if (!out)
            {
                return;
            }

            out
                << "GZFX ambient depth stack\n"
                << "version=" << gzfx::kVersion << "\n"
                << "implementation=direct-full-depth-recovery\n"
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

            ReleaseSurface(g_runtime.second);
            ReleaseSurface(g_runtime.first);

            if (g_runtime.sceneCopySrv) g_runtime.sceneCopySrv->Release();
            if (g_runtime.sceneCopy) g_runtime.sceneCopy->Release();

            if (g_runtime.parameters) g_runtime.parameters->Release();
            if (g_runtime.pointSampler) g_runtime.pointSampler->Release();
            if (g_runtime.linearSampler) g_runtime.linearSampler->Release();

            if (g_runtime.compositeShader) g_runtime.compositeShader->Release();
            if (g_runtime.denoiseShader) g_runtime.denoiseShader->Release();
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
                if (FAILED(device->CreateQuery(
                        &disjointDesc,
                        &slot.disjoint)))
                {
                    return false;
                }

                for (auto*& timestamp : slot.timestamps)
                {
                    if (FAILED(device->CreateQuery(
                            &timestampDesc,
                            &timestamp)))
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
                g_runtime.latestGpuTiming.denoiseHorizontalMs = deltaMs(1, 2);
                g_runtime.latestGpuTiming.denoiseVerticalMs = deltaMs(2, 3);
                g_runtime.latestGpuTiming.compositeMs = deltaMs(3, 4);
                g_runtime.latestGpuTiming.totalMs = deltaMs(0, 4);
            }
        }

        GpuTimerSlot* BeginGpuTimer(
            ID3D11DeviceContext* context,
            const std::uint64_t frame)
        {
            ResolveGpuTimers(context);

            auto& slot =
                g_runtime.gpuTimers[g_runtime.gpuTimerCursor];

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
            if (slot != nullptr && index < slot->timestamps.size())
            {
                context->End(slot->timestamps[index]);
            }
        }

        void FinishGpuTimer(
            ID3D11DeviceContext* context,
            GpuTimerSlot* slot)
        {
            if (slot != nullptr)
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
            if (slot == nullptr)
            {
                return;
            }

            for (std::size_t i = firstUnwritten;
                 i < slot->timestamps.size();
                 ++i)
            {
                context->End(slot->timestamps[i]);
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
                "GZFXAmbientDepthRecovered",
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

        std::string ShaderSource()
        {
            std::string source;
            source.reserve(15000);

            source += R"GZFX(
struct VSOutput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
VSOutput VSMain(uint id : SV_VertexID)
{
    float2 uv = float2((id << 1) & 2, id & 2);
    VSOutput o;
    o.uv = uv;
    o.position = float4(uv * float2(2,-2) + float2(-1,1), 0, 1);
    return o;
}

cbuffer GZFXAmbientDepthParameters : register(b0)
{
    float2 FullInvSize;
    float2 HalfInvSize;
    float AoStrength;
    float AoRadius;
    float IndirectStrength;
    float IndirectRadius;
    float IndirectSaturation;
    uint Quality;
    uint Mode;
    uint AoEnabled;
    uint IndirectEnabled;
    float2 FilterAxis;
    float ParameterPadding0;
};

cbuffer SkyrimPerFrame : register(b12)
{
    row_major float4x4 CameraView : packoffset(c0);
    row_major float4x4 CameraProj : packoffset(c4);
    row_major float4x4 CameraViewProj : packoffset(c8);
    row_major float4x4 CameraViewProjUnjittered : packoffset(c12);
    row_major float4x4 CameraPreviousViewProjUnjittered : packoffset(c16);
    row_major float4x4 CameraProjUnjittered : packoffset(c20);
    row_major float4x4 CameraProjUnjitteredInverse : packoffset(c24);
    row_major float4x4 CameraViewInverse : packoffset(c28);
    row_major float4x4 CameraViewProjInverse : packoffset(c32);
    row_major float4x4 CameraProjInverse : packoffset(c36);
    float4 CameraPosAdjust : packoffset(c40);
    float4 CameraPreviousPosAdjust : packoffset(c41);
    float4 FrameParams : packoffset(c42);
    float4 DynamicResolutionParams1 : packoffset(c43);
    float4 DynamicResolutionParams2 : packoffset(c44);
};

Texture2D<float4> SceneColor : register(t0);
Texture2D<float> SceneDepth : register(t1);
Texture2D<float> AoInput : register(t2);
Texture2D<float4> IndirectInput : register(t3);
SamplerState LinearSampler : register(s0);
SamplerState PointSampler : register(s1);

float2 DepthUv(float2 uv)
{
    return saturate(uv * DynamicResolutionParams1.xy);
}

float3 ReconstructView(float2 uv, float depth)
{
    float4 clip = float4(2.0 * float2(uv.x, 1.0 - uv.y) - 1.0, depth, 1.0);
    float4 view = mul(CameraProjInverse, clip);
    return view.xyz / max(abs(view.w), 1e-6);
}

float DepthAt(float2 uv)
{
    return SceneDepth.SampleLevel(PointSampler, DepthUv(saturate(uv)), 0).x;
}

float3 PositionAt(float2 uv)
{
    return ReconstructView(saturate(uv), DepthAt(uv));
}

float3 ReconstructNormal(float2 uv, float centerDepth)
{
    float3 center = ReconstructView(uv, centerDepth);
    float3 left = PositionAt(uv - float2(FullInvSize.x, 0));
    float3 right = PositionAt(uv + float2(FullInvSize.x, 0));
    float3 up = PositionAt(uv - float2(0, FullInvSize.y));
    float3 down = PositionAt(uv + float2(0, FullInvSize.y));

    float3 dx = length(right - center) < length(center - left)
        ? right - center : center - left;
    float3 dy = length(down - center) < length(center - up)
        ? down - center : center - up;

    float3 normal = normalize(cross(dy, dx));
    if (dot(normal, -center) < 0) normal = -normal;
    return normal;
}

float Luminance(float3 c)
{
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

float3 ApplySaturation(float3 c, float saturation)
{
    return lerp(Luminance(c).xxx, c, saturation);
}

float Hash12(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

uint DirectionCount()
{
    if (Quality == 0) return 4;
    if (Quality == 1) return 6;
    return 8;
}

uint StepCount()
{
    if (Quality == 0) return 3;
    if (Quality == 1) return 3;
    if (Quality == 2) return 4;
    return 5;
}

static const float2 Directions[8] =
{
    float2( 1.0000,  0.0000), float2( 0.7071,  0.7071),
    float2( 0.0000,  1.0000), float2(-0.7071,  0.7071),
    float2(-1.0000,  0.0000), float2(-0.7071, -0.7071),
    float2( 0.0000, -1.0000), float2( 0.7071, -0.7071)
};

struct AnalyzeOutput
{
    float ao : SV_TARGET0;
    float4 indirect : SV_TARGET1;
};

AnalyzeOutput AnalyzePS(VSOutput input)
{
    AnalyzeOutput output;
    float centerDepth = DepthAt(input.uv);

    if (centerDepth >= 0.99999)
    {
        output.ao = 0;
        output.indirect = float4(0,0,0,1e6);
        return output;
    }

    float3 center = ReconstructView(input.uv, centerDepth);
    float3 normal = ReconstructNormal(input.uv, centerDepth);
    float3 centerColor = max(SceneColor.SampleLevel(LinearSampler, input.uv, 0).rgb, 0);

    uint directionCount = DirectionCount();
    uint stepCount = StepCount();

    float noise = Hash12(floor(input.position.xy));
    float angle = noise * 6.28318530718;
    float cs = cos(angle);
    float sn = sin(angle);
    float2x2 rotation = float2x2(cs, -sn, sn, cs);

    float horizonSum = 0;
    float3 indirectSum = 0;
    float indirectWeight = 0;

    [loop]
    for (uint directionIndex = 0; directionIndex < 8; ++directionIndex)
    {
        if (directionIndex >= directionCount) break;
        float2 direction = mul(rotation, Directions[directionIndex]);
        float maxHorizon = 0;

        [loop]
        for (uint stepIndex = 1; stepIndex <= 5; ++stepIndex)
        {
            if (stepIndex > stepCount) break;

            float jitter = Hash12(float2(input.position.xy) + float2(directionIndex * 13 + stepIndex, 7));
            float stepFraction = ((float)stepIndex - 0.35 + jitter * 0.25) / (float)stepCount;
            stepFraction = saturate(stepFraction);

            if (AoEnabled != 0 && AoRadius > 0)
            {
                float2 aoUv = saturate(input.uv + direction * FullInvSize * AoRadius * stepFraction);
                float aoDepth = DepthAt(aoUv);

                if (aoDepth < 0.99999)
                {
                    float3 samplePosition = ReconstructView(aoUv, aoDepth);
                    float3 delta = samplePosition - center;
                    float distanceToSample = length(delta);

                    if (distanceToSample > 1e-4)
                    {
                        float3 direction3 = delta / distanceToSample;
                        float horizon = saturate((dot(normal, direction3) - 0.015) * 1.65);
                        float worldRadius = max(abs(center.z) * 0.20, 8.0);
                        float distanceWeight = saturate(1.0 - distanceToSample / worldRadius);
                        maxHorizon = max(maxHorizon, horizon * distanceWeight);
                    }
                }
            }

            if (IndirectEnabled != 0 && IndirectRadius > 0)
            {
                float2 indirectUv = saturate(input.uv + direction * FullInvSize * IndirectRadius * stepFraction);
                float indirectDepth = DepthAt(indirectUv);

                if (indirectDepth < 0.99999)
                {
                    float3 samplePosition = ReconstructView(indirectUv, indirectDepth);
                    float3 delta = samplePosition - center;
                    float distanceToSample = length(delta);

                    if (distanceToSample > 1e-4)
                    {
                        float3 direction3 = delta / distanceToSample;
                        float receiverFacing = saturate(dot(normal, direction3));
                        float worldRadius = max(abs(center.z) * 0.24, 10.0);
                        float distanceWeight = saturate(1.0 - distanceToSample / worldRadius);
                        float weight = receiverFacing * distanceWeight * (1.0 - 0.35 * stepFraction);

                        if (weight > 0)
                        {
                            float3 radiance = max(SceneColor.SampleLevel(LinearSampler, indirectUv, 0).rgb, 0);
                            radiance = min(radiance, 8.0);
                            float3 contrastRadiance = max(radiance - centerColor * 0.65, 0);
                            indirectSum += contrastRadiance * weight;
                            indirectWeight += weight;
                        }
                    }
                }
            }
        }

        horizonSum += maxHorizon;
    }

    output.ao = AoEnabled != 0
        ? saturate(horizonSum / max((float)directionCount, 1.0))
        : 0;

    float3 bounce = indirectWeight > 0
        ? indirectSum / indirectWeight
        : 0;

    output.indirect = float4(
        ApplySaturation(bounce, IndirectSaturation),
        abs(center.z));

    return output;
}
)GZFX";

            source += R"GZFX(
struct FilterOutput
{
    float ao : SV_TARGET0;
    float4 indirect : SV_TARGET1;
};

FilterOutput DenoisePS(VSOutput input)
{
    FilterOutput output;
    float4 centerIndirect = IndirectInput.SampleLevel(PointSampler, input.uv, 0);
    float centerDepth = centerIndirect.a;

    static const float Weights[5] =
    {
        0.06136, 0.24477, 0.38774, 0.24477, 0.06136
    };

    float aoSum = 0;
    float3 indirectSum = 0;
    float weightSum = 0;

    [unroll]
    for (int offset = -2; offset <= 2; ++offset)
    {
        float2 sampleUv = saturate(input.uv + FilterAxis * HalfInvSize * (float)offset);
        float4 indirectSample = IndirectInput.SampleLevel(PointSampler, sampleUv, 0);
        float aoSample = AoInput.SampleLevel(PointSampler, sampleUv, 0);

        float relativeDepth = abs(indirectSample.a - centerDepth) / max(centerDepth, 1.0);
        float bilateral = exp2(-relativeDepth * 96.0);
        float weight = Weights[offset + 2] * bilateral;

        aoSum += aoSample * weight;
        indirectSum += indirectSample.rgb * weight;
        weightSum += weight;
    }

    weightSum = max(weightSum, 1e-5);
    output.ao = aoSum / weightSum;
    output.indirect = float4(indirectSum / weightSum, centerDepth);
    return output;
}

float4 CompositePS(VSOutput input) : SV_TARGET
{
    float4 original = SceneColor.SampleLevel(LinearSampler, input.uv, 0);

    if (Mode != 1 && Mode != 2) return original;
    if (Mode == 1 && input.uv.x < 0.5) return original;

    float ao = AoInput.SampleLevel(LinearSampler, input.uv, 0);
    float3 indirect = IndirectInput.SampleLevel(LinearSampler, input.uv, 0).rgb;
    float3 color = max(original.rgb, 0);

    float visibility = lerp(1.0, saturate(1.0 - ao), saturate(AoStrength));
    color *= visibility;

    if (IndirectEnabled != 0)
    {
        float centerLuminance = max(Luminance(color), 0.02);
        float bounceLuminance = max(Luminance(indirect), 1e-4);
        float maximumBounce = 0.04 + centerLuminance * 0.40;
        float limiter = min(1.0, maximumBounce / bounceLuminance);
        color += indirect * IndirectStrength * limiter;
    }

    return float4(color, original.a);
}
)GZFX";

            return source;
        }

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

            if (device == nullptr ||
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
                    error = "ambient depth runtime allocation failed";
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
                    << std::hex << std::uppercase
                    << static_cast<std::uint32_t>(result);
                error = stream.str();
                return false;
            }

            return true;
        }

        float HalfToFloat(const std::uint16_t value)
        {
            const std::uint32_t sign =
                static_cast<std::uint32_t>(value >> 15) << 31;
            std::uint32_t exponent = (value >> 10) & 0x1Fu;
            std::uint32_t mantissa = value & 0x3FFu;
            std::uint32_t bits = 0;

            if (exponent == 0)
            {
                if (mantissa == 0)
                {
                    bits = sign;
                }
                else
                {
                    exponent = 1;
                    while ((mantissa & 0x400u) == 0)
                    {
                        mantissa <<= 1;
                        --exponent;
                    }

                    mantissa &= 0x3FFu;
                    bits = sign |
                        ((exponent + 112u) << 23) |
                        (mantissa << 13);
                }
            }
            else if (exponent == 31)
            {
                bits = sign | 0x7F800000u | (mantissa << 13);
            }
            else
            {
                bits = sign |
                    ((exponent + 112u) << 23) |
                    (mantissa << 13);
            }

            return std::bit_cast<float>(bits);
        }

        bool WriteBmp(
            const fs::path& path,
            const UINT width,
            const UINT height,
            const std::vector<std::uint8_t>& rgba)
        {
            if (rgba.size() !=
                static_cast<std::size_t>(width) * height * 4)
            {
                return false;
            }

            BITMAPFILEHEADER fileHeader{};
            BITMAPINFOHEADER infoHeader{};
            const DWORD bytes = width * height * 4;

            fileHeader.bfType = 0x4D42;
            fileHeader.bfOffBits =
                sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
            fileHeader.bfSize = fileHeader.bfOffBits + bytes;

            infoHeader.biSize = sizeof(BITMAPINFOHEADER);
            infoHeader.biWidth = static_cast<LONG>(width);
            infoHeader.biHeight = static_cast<LONG>(height);
            infoHeader.biPlanes = 1;
            infoHeader.biBitCount = 32;
            infoHeader.biCompression = BI_RGB;
            infoHeader.biSizeImage = bytes;

            std::ofstream out(path, std::ios::binary | std::ios::trunc);

            if (!out)
            {
                return false;
            }

            out.write(
                reinterpret_cast<const char*>(&fileHeader),
                sizeof(fileHeader));
            out.write(
                reinterpret_cast<const char*>(&infoHeader),
                sizeof(infoHeader));

            for (UINT y = 0; y < height; ++y)
            {
                const UINT sourceY = height - 1 - y;
                const auto* row =
                    rgba.data() +
                    static_cast<std::size_t>(sourceY) * width * 4;

                out.write(
                    reinterpret_cast<const char*>(row),
                    static_cast<std::streamsize>(width * 4));
            }

            return static_cast<bool>(out);
        }

        bool CaptureFields(
            const std::uint64_t frame,
            ID3D11Device* device,
            ID3D11DeviceContext* context)
        {
            if (g_fieldCaptureDone ||
                frame < FieldCaptureFrame() ||
                device == nullptr ||
                context == nullptr)
            {
                return g_fieldCaptureDone;
            }

            D3D11_TEXTURE2D_DESC aoDesc{};
            D3D11_TEXTURE2D_DESC indirectDesc{};
            g_runtime.first.ao->GetDesc(&aoDesc);
            g_runtime.first.indirect->GetDesc(&indirectDesc);

            D3D11_TEXTURE2D_DESC aoStagingDesc = aoDesc;
            aoStagingDesc.Usage = D3D11_USAGE_STAGING;
            aoStagingDesc.BindFlags = 0;
            aoStagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

            D3D11_TEXTURE2D_DESC indirectStagingDesc = indirectDesc;
            indirectStagingDesc.Usage = D3D11_USAGE_STAGING;
            indirectStagingDesc.BindFlags = 0;
            indirectStagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

            ID3D11Texture2D* aoStaging = nullptr;
            ID3D11Texture2D* indirectStaging = nullptr;

            HRESULT result = device->CreateTexture2D(
                &aoStagingDesc,
                nullptr,
                &aoStaging);

            if (SUCCEEDED(result))
            {
                result = device->CreateTexture2D(
                    &indirectStagingDesc,
                    nullptr,
                    &indirectStaging);
            }

            if (FAILED(result) || !aoStaging || !indirectStaging)
            {
                if (indirectStaging) indirectStaging->Release();
                if (aoStaging) aoStaging->Release();
                return false;
            }

            context->CopyResource(aoStaging, g_runtime.first.ao);
            context->CopyResource(indirectStaging, g_runtime.first.indirect);

            D3D11_MAPPED_SUBRESOURCE aoMapped{};
            D3D11_MAPPED_SUBRESOURCE indirectMapped{};

            result = context->Map(
                aoStaging,
                0,
                D3D11_MAP_READ,
                0,
                &aoMapped);

            if (SUCCEEDED(result))
            {
                result = context->Map(
                    indirectStaging,
                    0,
                    D3D11_MAP_READ,
                    0,
                    &indirectMapped);
            }

            if (FAILED(result))
            {
                if (aoMapped.pData)
                {
                    context->Unmap(aoStaging, 0);
                }

                indirectStaging->Release();
                aoStaging->Release();
                return false;
            }

            float aoMin = std::numeric_limits<float>::max();
            float aoMax = 0.0f;
            double aoSum = 0.0;
            std::uint64_t aoNonZero = 0;

            float indirectMin = std::numeric_limits<float>::max();
            float indirectMax = 0.0f;
            double indirectSum = 0.0;
            std::uint64_t indirectNonZero = 0;

            const std::uint64_t pixelCount =
                static_cast<std::uint64_t>(aoDesc.Width) * aoDesc.Height;

            for (UINT y = 0; y < aoDesc.Height; ++y)
            {
                const auto* aoRow =
                    static_cast<const std::uint8_t*>(aoMapped.pData) +
                    static_cast<std::size_t>(y) * aoMapped.RowPitch;

                const auto* indirectRow =
                    static_cast<const std::uint8_t*>(indirectMapped.pData) +
                    static_cast<std::size_t>(y) * indirectMapped.RowPitch;

                for (UINT x = 0; x < aoDesc.Width; ++x)
                {
                    const auto* aoValue =
                        reinterpret_cast<const std::uint16_t*>(
                            aoRow + static_cast<std::size_t>(x) * 2);

                    const auto* indirectValue =
                        reinterpret_cast<const std::uint16_t*>(
                            indirectRow + static_cast<std::size_t>(x) * 8);

                    const float ao = std::clamp(HalfToFloat(*aoValue), 0.0f, 1.0f);
                    const float red = std::max(HalfToFloat(indirectValue[0]), 0.0f);
                    const float green = std::max(HalfToFloat(indirectValue[1]), 0.0f);
                    const float blue = std::max(HalfToFloat(indirectValue[2]), 0.0f);
                    const float luma =
                        red * 0.2126f + green * 0.7152f + blue * 0.0722f;

                    aoMin = std::min(aoMin, ao);
                    aoMax = std::max(aoMax, ao);
                    aoSum += ao;
                    aoNonZero += ao > 0.0005f ? 1u : 0u;

                    indirectMin = std::min(indirectMin, luma);
                    indirectMax = std::max(indirectMax, luma);
                    indirectSum += luma;
                    indirectNonZero += luma > 0.0005f ? 1u : 0u;
                }
            }

            constexpr UINT previewWidth = 640;
            constexpr UINT previewHeight = 360;
            std::vector<std::uint8_t> aoPixels(
                static_cast<std::size_t>(previewWidth) * previewHeight * 4);
            std::vector<std::uint8_t> indirectPixels(
                static_cast<std::size_t>(previewWidth) * previewHeight * 4);

            for (UINT y = 0; y < previewHeight; ++y)
            {
                const UINT sourceY = std::min(
                    aoDesc.Height - 1,
                    y * aoDesc.Height / previewHeight);

                const auto* aoRow =
                    static_cast<const std::uint8_t*>(aoMapped.pData) +
                    static_cast<std::size_t>(sourceY) * aoMapped.RowPitch;

                const auto* indirectRow =
                    static_cast<const std::uint8_t*>(indirectMapped.pData) +
                    static_cast<std::size_t>(sourceY) * indirectMapped.RowPitch;

                for (UINT x = 0; x < previewWidth; ++x)
                {
                    const UINT sourceX = std::min(
                        aoDesc.Width - 1,
                        x * aoDesc.Width / previewWidth);

                    const auto* aoValue =
                        reinterpret_cast<const std::uint16_t*>(
                            aoRow + static_cast<std::size_t>(sourceX) * 2);

                    const auto* indirectValue =
                        reinterpret_cast<const std::uint16_t*>(
                            indirectRow + static_cast<std::size_t>(sourceX) * 8);

                    const float ao = std::clamp(HalfToFloat(*aoValue), 0.0f, 1.0f);
                    const float red = std::max(HalfToFloat(indirectValue[0]), 0.0f);
                    const float green = std::max(HalfToFloat(indirectValue[1]), 0.0f);
                    const float blue = std::max(HalfToFloat(indirectValue[2]), 0.0f);

                    const auto tone = [](const float value)
                    {
                        const float mapped = value / (1.0f + value);
                        return std::pow(std::clamp(mapped, 0.0f, 1.0f), 1.0f / 2.2f);
                    };

                    const std::size_t offset =
                        (static_cast<std::size_t>(y) * previewWidth + x) * 4;

                    const auto aoByte = static_cast<std::uint8_t>(
                        std::lround(ao * 255.0f));

                    aoPixels[offset + 0] = aoByte;
                    aoPixels[offset + 1] = aoByte;
                    aoPixels[offset + 2] = aoByte;
                    aoPixels[offset + 3] = 255;

                    indirectPixels[offset + 0] = static_cast<std::uint8_t>(
                        std::lround(tone(blue) * 255.0f));
                    indirectPixels[offset + 1] = static_cast<std::uint8_t>(
                        std::lround(tone(green) * 255.0f));
                    indirectPixels[offset + 2] = static_cast<std::uint8_t>(
                        std::lround(tone(red) * 255.0f));
                    indirectPixels[offset + 3] = 255;
                }
            }

            context->Unmap(indirectStaging, 0);
            context->Unmap(aoStaging, 0);
            indirectStaging->Release();
            aoStaging->Release();

            const fs::path root = gzfx::paths::DiagnosticsRoot();
            std::ofstream report(
                root / L"ambient_depth_fields.txt",
                std::ios::out | std::ios::trunc);

            if (!report)
            {
                return false;
            }

            report
                << "GZFX ambient depth fields\n"
                << "version=" << gzfx::kVersion << "\n"
                << "frame=" << frame << "\n"
                << "resolution=" << aoDesc.Width << "x" << aoDesc.Height << "\n"
                << std::fixed << std::setprecision(8)
                << "ao_min=" << aoMin << "\n"
                << "ao_max=" << aoMax << "\n"
                << "ao_mean=" << (pixelCount ? aoSum / pixelCount : 0.0) << "\n"
                << "ao_nonzero_fraction="
                << (pixelCount ? static_cast<double>(aoNonZero) / pixelCount : 0.0)
                << "\n"
                << "ssil_luma_min=" << indirectMin << "\n"
                << "ssil_luma_max=" << indirectMax << "\n"
                << "ssil_luma_mean="
                << (pixelCount ? indirectSum / pixelCount : 0.0) << "\n"
                << "ssil_nonzero_fraction="
                << (pixelCount
                    ? static_cast<double>(indirectNonZero) / pixelCount
                    : 0.0)
                << "\n";

            const bool imagesWritten =
                WriteBmp(
                    root / L"ambient_depth_ao.bmp",
                    previewWidth,
                    previewHeight,
                    aoPixels) &&
                WriteBmp(
                    root / L"ambient_depth_ssil.bmp",
                    previewWidth,
                    previewHeight,
                    indirectPixels);

            g_fieldCaptureDone = static_cast<bool>(report) && imagesWritten;
            return g_fieldCaptureDone;
        }
    }

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

            if (!device || !context ||
                !inputs.sceneColorTexture ||
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

            HRESULT result = device->CreateShaderResourceView(
                inputs.depthTexture,
                &depthSrvDesc,
                &depthSrv);

            if (FAILED(result) || !depthSrv)
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
            parameters.aoStrength = settings.horizonOcclusionStrength;
            parameters.aoRadius = settings.horizonOcclusionRadiusPixels;
            parameters.indirectStrength = settings.indirectBounceStrength;
            parameters.indirectRadius = settings.indirectBounceRadiusPixels;
            parameters.indirectSaturation = settings.indirectBounceSaturation;
            parameters.quality = settings.horizonOcclusionQuality;
            parameters.mode = mode;
            parameters.aoEnabled =
                settings.horizonOcclusionEnabled ? 1u : 0u;
            parameters.indirectEnabled =
                settings.indirectBounceEnabled ? 1u : 0u;

            GpuTimerSlot* timer = BeginGpuTimer(context, frame);

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
                depthSrv->Release();
                AbortGpuTimer(context, timer, 1);
                WriteLog(frame, "failed_analyze", error);
                return false;
            }

            StampGpuTimer(context, timer, 1);
            parameters.axisX = 1.0f;
            parameters.axisY = 0.0f;

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
                depthSrv->Release();
                AbortGpuTimer(context, timer, 2);
                WriteLog(frame, "failed_denoise_horizontal", error);
                return false;
            }

            StampGpuTimer(context, timer, 2);
            parameters.axisX = 0.0f;
            parameters.axisY = 1.0f;

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
                depthSrv->Release();
                AbortGpuTimer(context, timer, 3);
                WriteLog(frame, "failed_denoise_vertical", error);
                return false;
            }

            StampGpuTimer(context, timer, 3);

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
                depthSrv->Release();
                AbortGpuTimer(context, timer, 4);
                WriteLog(frame, "failed_composite", error);
                return false;
            }

            StampGpuTimer(context, timer, 4);
            FinishGpuTimer(context, timer);

            context->CopyResource(
                inputs.sceneColorTexture,
                g_runtime.composite);

            CaptureFields(frame, device, context);
            depthSrv->Release();

            ++g_applyCount;
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

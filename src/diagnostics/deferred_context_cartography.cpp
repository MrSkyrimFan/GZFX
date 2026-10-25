#include "gzfx/diagnostics/deferred_context_cartography.hpp"

#include "gzfx/diagnostics/pipeline_cartography.hpp"
#include "gzfx/diagnostics/telemetry_listener.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace gzfx::diagnostics::deferred_context_cartography
{
    namespace pc =
        gzfx::diagnostics::pipeline_cartography;

    namespace
    {
        constexpr std::size_t kContextVtableCount = 115;
        constexpr std::size_t kMaxRunsPerRecording = 65536;
        constexpr std::size_t kMaxSealedLists = 256;

        constexpr std::size_t kVSSetConstantBuffers = 7;
        constexpr std::size_t kPSSetShaderResources = 8;
        constexpr std::size_t kPSSetShader = 9;
        constexpr std::size_t kPSSetSamplers = 10;
        constexpr std::size_t kVSSetShader = 11;
        constexpr std::size_t kDrawIndexed = 12;
        constexpr std::size_t kDraw = 13;
        constexpr std::size_t kPSSetConstantBuffers = 16;
        constexpr std::size_t kDrawIndexedInstanced = 20;
        constexpr std::size_t kDrawInstanced = 21;
        constexpr std::size_t kGSSetConstantBuffers = 22;
        constexpr std::size_t kGSSetShader = 23;
        constexpr std::size_t kIASetPrimitiveTopology = 24;
        constexpr std::size_t kVSSetShaderResources = 25;
        constexpr std::size_t kVSSetSamplers = 26;
        constexpr std::size_t kGSSetShaderResources = 31;
        constexpr std::size_t kGSSetSamplers = 32;
        constexpr std::size_t kOMSetRenderTargets = 33;
        constexpr std::size_t kOMSetRenderTargetsAndUavs = 34;
        constexpr std::size_t kOMSetBlendState = 35;
        constexpr std::size_t kOMSetDepthStencilState = 36;
        constexpr std::size_t kDrawIndexedInstancedIndirect = 39;
        constexpr std::size_t kDrawInstancedIndirect = 40;
        constexpr std::size_t kDispatch = 41;
        constexpr std::size_t kDispatchIndirect = 42;
        constexpr std::size_t kRSSetState = 43;
        constexpr std::size_t kRSSetViewports = 44;
        constexpr std::size_t kCopySubresourceRegion = 46;
        constexpr std::size_t kCopyResource = 47;
        constexpr std::size_t kClearRenderTargetView = 50;
        constexpr std::size_t kClearUnorderedAccessViewUint = 51;
        constexpr std::size_t kClearUnorderedAccessViewFloat = 52;
        constexpr std::size_t kClearDepthStencilView = 53;
        constexpr std::size_t kGenerateMips = 54;
        constexpr std::size_t kResolveSubresource = 57;
        constexpr std::size_t kExecuteCommandList = 58;
        constexpr std::size_t kHSSetShaderResources = 59;
        constexpr std::size_t kHSSetShader = 60;
        constexpr std::size_t kHSSetSamplers = 61;
        constexpr std::size_t kHSSetConstantBuffers = 62;
        constexpr std::size_t kDSSetShaderResources = 63;
        constexpr std::size_t kDSSetShader = 64;
        constexpr std::size_t kDSSetSamplers = 65;
        constexpr std::size_t kDSSetConstantBuffers = 66;
        constexpr std::size_t kCSSetShaderResources = 67;
        constexpr std::size_t kCSSetUnorderedAccessViews = 68;
        constexpr std::size_t kCSSetShader = 69;
        constexpr std::size_t kCSSetSamplers = 70;
        constexpr std::size_t kCSSetConstantBuffers = 71;
        constexpr std::size_t kClearState = 110;
        constexpr std::size_t kFinishCommandList = 114;

        constexpr std::size_t kStageCount = 6;
        constexpr std::size_t kSrvSlots =
            D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT;
        constexpr std::size_t kCbSlots =
            D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT;
        constexpr std::size_t kSamplerSlots =
            D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT;
        constexpr std::size_t kRtvSlots =
            D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;
        constexpr std::size_t kUavSlots =
            D3D11_PS_CS_UAV_REGISTER_COUNT;

        using SetConstantBuffersFn =
            void(STDMETHODCALLTYPE*)(
                ID3D11DeviceContext*,
                UINT,
                UINT,
                ID3D11Buffer* const*);

        using SetShaderResourcesFn =
            void(STDMETHODCALLTYPE*)(
                ID3D11DeviceContext*,
                UINT,
                UINT,
                ID3D11ShaderResourceView* const*);

        using SetSamplersFn =
            void(STDMETHODCALLTYPE*)(
                ID3D11DeviceContext*,
                UINT,
                UINT,
                ID3D11SamplerState* const*);

        using CSSetUnorderedAccessViewsFn =
            void(STDMETHODCALLTYPE*)(
                ID3D11DeviceContext*,
                UINT,
                UINT,
                ID3D11UnorderedAccessView* const*,
                const UINT*);

        using VSSetShaderFn =
            void(STDMETHODCALLTYPE*)(
                ID3D11DeviceContext*,
                ID3D11VertexShader*,
                ID3D11ClassInstance* const*,
                UINT);

        using HSSetShaderFn =
            void(STDMETHODCALLTYPE*)(
                ID3D11DeviceContext*,
                ID3D11HullShader*,
                ID3D11ClassInstance* const*,
                UINT);

        using DSSetShaderFn =
            void(STDMETHODCALLTYPE*)(
                ID3D11DeviceContext*,
                ID3D11DomainShader*,
                ID3D11ClassInstance* const*,
                UINT);

        using GSSetShaderFn =
            void(STDMETHODCALLTYPE*)(
                ID3D11DeviceContext*,
                ID3D11GeometryShader*,
                ID3D11ClassInstance* const*,
                UINT);

        using PSSetShaderFn =
            void(STDMETHODCALLTYPE*)(
                ID3D11DeviceContext*,
                ID3D11PixelShader*,
                ID3D11ClassInstance* const*,
                UINT);

        using CSSetShaderFn =
            void(STDMETHODCALLTYPE*)(
                ID3D11DeviceContext*,
                ID3D11ComputeShader*,
                ID3D11ClassInstance* const*,
                UINT);

        using DrawIndexedFn =
            void(STDMETHODCALLTYPE*)(
                ID3D11DeviceContext*,
                UINT,
                UINT,
                INT);

        using DrawFn =
            void(STDMETHODCALLTYPE*)(
                ID3D11DeviceContext*,
                UINT,
                UINT);

        using DrawIndexedInstancedFn =
            void(STDMETHODCALLTYPE*)(
                ID3D11DeviceContext*,
                UINT,
                UINT,
                UINT,
                INT,
                UINT);

        using DrawInstancedFn =
            void(STDMETHODCALLTYPE*)(
                ID3D11DeviceContext*,
                UINT,
                UINT,
                UINT,
                UINT);

        using DrawIndexedInstancedIndirectFn =
            void(STDMETHODCALLTYPE*)(
                ID3D11DeviceContext*,
                ID3D11Buffer*,
                UINT);

        using DrawInstancedIndirectFn =
            void(STDMETHODCALLTYPE*)(
                ID3D11DeviceContext*,
                ID3D11Buffer*,
                UINT);

        using IASetPrimitiveTopologyFn =
            void(STDMETHODCALLTYPE*)(
                ID3D11DeviceContext*,
                D3D11_PRIMITIVE_TOPOLOGY);

        using OMSetRenderTargetsFn =
            void(STDMETHODCALLTYPE*)(
                ID3D11DeviceContext*,
                UINT,
                ID3D11RenderTargetView* const*,
                ID3D11DepthStencilView*);

        using OMSetRenderTargetsAndUavsFn =
            void(STDMETHODCALLTYPE*)(
                ID3D11DeviceContext*,
                UINT,
                ID3D11RenderTargetView* const*,
                ID3D11DepthStencilView*,
                UINT,
                UINT,
                ID3D11UnorderedAccessView* const*,
                const UINT*);

        using OMSetBlendStateFn =
            void(STDMETHODCALLTYPE*)(
                ID3D11DeviceContext*,
                ID3D11BlendState*,
                const FLOAT[4],
                UINT);

        using OMSetDepthStencilStateFn =
            void(STDMETHODCALLTYPE*)(
                ID3D11DeviceContext*,
                ID3D11DepthStencilState*,
                UINT);

        using DispatchFn =
            void(STDMETHODCALLTYPE*)(
                ID3D11DeviceContext*,
                UINT,
                UINT,
                UINT);

        using DispatchIndirectFn =
            void(STDMETHODCALLTYPE*)(
                ID3D11DeviceContext*,
                ID3D11Buffer*,
                UINT);

        using RSSetStateFn =
            void(STDMETHODCALLTYPE*)(
                ID3D11DeviceContext*,
                ID3D11RasterizerState*);

        using RSSetViewportsFn =
            void(STDMETHODCALLTYPE*)(
                ID3D11DeviceContext*,
                UINT,
                const D3D11_VIEWPORT*);

        using CopySubresourceRegionFn =
            void(STDMETHODCALLTYPE*)(
                ID3D11DeviceContext*,
                ID3D11Resource*,
                UINT,
                UINT,
                UINT,
                UINT,
                ID3D11Resource*,
                UINT,
                const D3D11_BOX*);

        using CopyResourceFn =
            void(STDMETHODCALLTYPE*)(
                ID3D11DeviceContext*,
                ID3D11Resource*,
                ID3D11Resource*);

        using ClearRenderTargetViewFn =
            void(STDMETHODCALLTYPE*)(
                ID3D11DeviceContext*,
                ID3D11RenderTargetView*,
                const FLOAT[4]);

        using ClearUnorderedAccessViewUintFn =
            void(STDMETHODCALLTYPE*)(
                ID3D11DeviceContext*,
                ID3D11UnorderedAccessView*,
                const UINT[4]);

        using ClearUnorderedAccessViewFloatFn =
            void(STDMETHODCALLTYPE*)(
                ID3D11DeviceContext*,
                ID3D11UnorderedAccessView*,
                const FLOAT[4]);

        using ClearDepthStencilViewFn =
            void(STDMETHODCALLTYPE*)(
                ID3D11DeviceContext*,
                ID3D11DepthStencilView*,
                UINT,
                FLOAT,
                UINT8);

        using GenerateMipsFn =
            void(STDMETHODCALLTYPE*)(
                ID3D11DeviceContext*,
                ID3D11ShaderResourceView*);

        using ResolveSubresourceFn =
            void(STDMETHODCALLTYPE*)(
                ID3D11DeviceContext*,
                ID3D11Resource*,
                UINT,
                ID3D11Resource*,
                UINT,
                DXGI_FORMAT);

        using ExecuteCommandListFn =
            void(STDMETHODCALLTYPE*)(
                ID3D11DeviceContext*,
                ID3D11CommandList*,
                BOOL);

        using ClearStateFn =
            void(STDMETHODCALLTYPE*)(
                ID3D11DeviceContext*);

        using FinishCommandListFn =
            HRESULT(STDMETHODCALLTYPE*)(
                ID3D11DeviceContext*,
                BOOL,
                ID3D11CommandList**);

        struct CurrentState
        {
            std::array<std::uintptr_t, kStageCount> shaders{};
            std::array<
                std::array<std::uintptr_t, kSrvSlots>,
                kStageCount> srvs{};
            std::array<
                std::array<std::uintptr_t, kCbSlots>,
                kStageCount> cbs{};
            std::array<
                std::array<std::uintptr_t, kSamplerSlots>,
                kStageCount> samplers{};

            std::array<std::uintptr_t, kRtvSlots> rtvs{};
            std::uintptr_t dsv = 0;
            std::array<std::uintptr_t, kUavSlots> omUavs{};
            std::array<std::uintptr_t, kUavSlots> csUavs{};

            std::uintptr_t blend = 0;
            std::uintptr_t depthState = 0;
            std::uintptr_t rasterState = 0;
            D3D11_PRIMITIVE_TOPOLOGY topology =
                D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
            UINT viewportWidth = 0;
            UINT viewportHeight = 0;
        };

        struct Run
        {
            pc::PassTelemetry pass{};
            std::uint64_t firstOrdinal = 0;
            std::uint64_t lastOrdinal = 0;
            std::uint64_t repeatCount = 0;
            std::uint64_t signature = 0;
        };

        struct ContextRecord
        {
            std::uintptr_t identity = 0;
            std::uint64_t generation = 0;
            std::uintptr_t* originalVtable = nullptr;
            std::uintptr_t* replacementVtable = nullptr;
            CurrentState state{};
            std::uint64_t recordingGeneration = 1;
            std::uint64_t nextOrdinal = 0;
            std::uint64_t originalPassCount = 0;
            bool overflow = false;
            std::vector<Run> runs{};
        };

        struct CommandListRecord
        {
            std::uintptr_t identity = 0;
            std::uint64_t generation = 0;
            std::uintptr_t contextIdentity = 0;
            std::uint64_t contextGeneration = 0;
            std::uint64_t recordingGeneration = 0;
            bool finishRestoreState = false;
            bool complete = false;
            bool overflow = false;
            std::uint64_t originalPassCount = 0;
            std::vector<Run> runs{};
        };

        std::mutex g_mutex;
        std::unordered_map<
            std::uintptr_t,
            ContextRecord> g_contexts;
        std::unordered_map<
            std::uintptr_t,
            CommandListRecord> g_lists;

        std::uint64_t g_contextGeneration = 0;
        std::uint64_t g_listGeneration = 0;
        Status g_status{};

        std::uintptr_t CanonicalId(
            IUnknown* const value) noexcept
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
                        std::uintptr_t>(
                            value);
            }

            const auto result =
                reinterpret_cast<
                    std::uintptr_t>(
                        identity);

            identity->Release();
            return result;
        }

        std::uint64_t Mix(
            std::uint64_t hash,
            const std::uint64_t value) noexcept
        {
            constexpr std::uint64_t prime =
                1099511628211ull;

            for (std::size_t i = 0; i < 8; ++i)
            {
                hash ^=
                    static_cast<std::uint8_t>(
                        value >> (i * 8));

                hash *= prime;
            }

            return hash;
        }

        std::uintptr_t ResourceId(
            ID3D11Resource* const resource) noexcept
        {
            return
                pc::ObserveResource(
                    resource);
        }

        std::uintptr_t ViewResourceId(
            ID3D11View* const view) noexcept
        {
            if (view == nullptr)
            {
                return 0;
            }

            ID3D11Resource* resource = nullptr;
            view->GetResource(&resource);

            if (resource == nullptr)
            {
                return 0;
            }

            const auto result =
                ResourceId(resource);

            resource->Release();
            return result;
        }

        bool ConflictsWithOutputs(
            const CurrentState& state,
            const std::uintptr_t identity)
        {
            if (identity == 0)
            {
                return false;
            }

            for (const auto output : state.rtvs)
            {
                if (output == identity)
                {
                    return true;
                }
            }

            if (state.dsv == identity)
            {
                return true;
            }

            for (const auto output : state.omUavs)
            {
                if (output == identity)
                {
                    return true;
                }
            }

            for (const auto output : state.csUavs)
            {
                if (output == identity)
                {
                    return true;
                }
            }

            return false;
        }

        void UnbindComputeUavs(
            CurrentState& state,
            const std::uintptr_t identity)
        {
            if (identity == 0)
            {
                return;
            }

            for (auto& output : state.csUavs)
            {
                if (output == identity)
                {
                    output = 0;
                }
            }
        }

        void UnbindReads(
            CurrentState& state,
            const std::uintptr_t identity)
        {
            if (identity == 0)
            {
                return;
            }

            for (auto& stage : state.srvs)
            {
                for (auto& resource : stage)
                {
                    if (resource == identity)
                    {
                        resource = 0;
                    }
                }
            }
        }

        std::uint64_t PassSignature(
            const pc::PassTelemetry& pass)
        {
            std::uint64_t hash =
                14695981039346656037ull;

            hash =
                Mix(
                    hash,
                    static_cast<std::uint64_t>(
                        pass.kind));
            hash =
                Mix(
                    hash,
                    static_cast<std::uint64_t>(
                        pass.drawKind));

            for (std::size_t i = 0;
                 i < pass.shaderHashes.size();
                 ++i)
            {
                const bool stable =
                    (pass.shaderHashKnownMask &
                        (1u << static_cast<std::uint32_t>(i))) != 0;

                hash =
                    Mix(
                        hash,
                        stable
                            ? pass.shaderHashes[i]
                            : static_cast<std::uint64_t>(
                                pass.shaderObjectIds[i]));
            }

            for (const auto value :
                 pass.rtvs)
            {
                hash = Mix(hash, value);
            }

            hash = Mix(hash, pass.dsv);
            hash = Mix(hash, pass.readHash);
            hash = Mix(hash, pass.writeHash);
            hash = Mix(hash, pass.blendHash);
            hash = Mix(hash, pass.depthHash);
            hash = Mix(hash, pass.rasterHash);
            hash = Mix(hash, pass.topology);
            hash = Mix(hash, pass.viewportWidth);
            hash = Mix(hash, pass.viewportHeight);

            for (const auto value :
                 pass.explicitReads)
            {
                hash = Mix(hash, value);
            }

            for (const auto value :
                 pass.explicitWrites)
            {
                hash = Mix(hash, value);
            }

            hash = Mix(hash, pass.explicitReadCount);
            hash = Mix(hash, pass.explicitWriteCount);
            hash = Mix(hash, pass.sourceSubresource);
            hash = Mix(hash, pass.destinationSubresource);
            hash = Mix(hash, pass.explicitFormat);
            hash = Mix(hash, pass.explicitFlags);

            return hash;
        }

        pc::PassTelemetry BuildPass(
            const ContextRecord& context,
            const pc::TelemetryPassKind kind,
            const pc::DrawKind drawKind,
            const std::array<std::uint64_t, 4>& args,
            const std::array<std::uintptr_t, 2>& explicitReads = {},
            const std::array<std::uintptr_t, 2>& explicitWrites = {},
            const std::uint32_t explicitReadCount = 0,
            const std::uint32_t explicitWriteCount = 0,
            const std::uint32_t sourceSubresource = 0,
            const std::uint32_t destinationSubresource = 0,
            const std::uint32_t format = 0,
            const std::uint32_t flags = 0,
            const bool suppressBoundAccess = false)
        {
            pc::PassTelemetry out{};
            out.ordinal =
                context.nextOrdinal;
            out.kind = kind;
            out.drawKind = drawKind;
            out.args = args;
            out.explicitReads =
                explicitReads;
            out.explicitWrites =
                explicitWrites;
            out.explicitReadCount =
                explicitReadCount;
            out.explicitWriteCount =
                explicitWriteCount;
            out.sourceSubresource =
                sourceSubresource;
            out.destinationSubresource =
                destinationSubresource;
            out.explicitFormat =
                format;
            out.explicitFlags =
                flags;

            for (std::size_t i = 0;
                 i < out.shaderHashes.size();
                 ++i)
            {
                out.shaderObjectIds[i] =
                    context.state.shaders[i];

                out.shaderHashes[i] =
                    pc::StableShaderHash(
                        context.state.shaders[i]);

                if (out.shaderHashes[i] != 0)
                {
                    out.shaderHashKnownMask |=
                        (1u << static_cast<std::uint32_t>(i));
                }
            }

            for (std::size_t i = 0;
                 i < out.rtvs.size();
                 ++i)
            {
                out.rtvs[i] =
                    context.state.rtvs[i];
            }

            out.dsv =
                context.state.dsv;
            out.blendHash =
                context.state.blend;
            out.depthHash =
                context.state.depthState;
            out.rasterHash =
                context.state.rasterState;
            out.topology =
                static_cast<std::uint32_t>(
                    context.state.topology);
            out.viewportWidth =
                context.state.viewportWidth;
            out.viewportHeight =
                context.state.viewportHeight;

            std::array<std::uintptr_t, kSrvSlots * kStageCount>
                uniqueReads{};
            std::uint32_t readCount = 0;
            std::uint64_t readHash =
                14695981039346656037ull;

            std::uint64_t cbHash =
                14695981039346656037ull;
            std::uint64_t samplerHash =
                14695981039346656037ull;

            for (std::size_t stage = 0;
                 stage < kStageCount;
                 ++stage)
            {
                for (std::size_t slot = 0;
                     slot < kSrvSlots;
                     ++slot)
                {
                    const auto identity =
                        context.state.srvs[
                            stage][slot];

                    if (identity == 0)
                    {
                        continue;
                    }

                    readHash = Mix(readHash, stage);
                    readHash = Mix(readHash, slot);
                    readHash = Mix(readHash, identity);

                    bool duplicate = false;

                    for (std::uint32_t i = 0;
                         i < readCount;
                         ++i)
                    {
                        if (uniqueReads[i] ==
                            identity)
                        {
                            duplicate = true;
                            break;
                        }
                    }

                    if (!duplicate &&
                        readCount <
                            uniqueReads.size())
                    {
                        uniqueReads[
                            readCount++] =
                            identity;

                        if (out.retainedReadResourceCount <
                            out.readResources.size())
                        {
                            out.readResources[
                                out.retainedReadResourceCount++] =
                                identity;
                        }
                    }
                }

                for (std::size_t slot = 0;
                     slot < kCbSlots;
                     ++slot)
                {
                    const auto identity =
                        context.state.cbs[
                            stage][slot];

                    if (identity != 0)
                    {
                        cbHash = Mix(cbHash, stage);
                        cbHash = Mix(cbHash, slot);
                        cbHash = Mix(cbHash, identity);
                    }
                }

                for (std::size_t slot = 0;
                     slot < kSamplerSlots;
                     ++slot)
                {
                    const auto identity =
                        context.state.samplers[
                            stage][slot];

                    if (identity != 0)
                    {
                        samplerHash =
                            Mix(
                                samplerHash,
                                stage);
                        samplerHash =
                            Mix(
                                samplerHash,
                                slot);
                        samplerHash =
                            Mix(
                                samplerHash,
                                identity);
                    }
                }
            }

            std::array<std::uintptr_t, kRtvSlots + (kUavSlots * 2) + 1>
                uniqueWrites{};
            std::uint32_t writeCount = 0;
            std::uint64_t writeHash =
                14695981039346656037ull;

            const auto addWrite =
                [&](const std::uintptr_t identity)
                {
                    if (identity == 0)
                    {
                        return;
                    }

                    for (std::uint32_t i = 0;
                         i < writeCount;
                         ++i)
                    {
                        if (uniqueWrites[i] ==
                            identity)
                        {
                            return;
                        }
                    }

                    if (writeCount <
                        uniqueWrites.size())
                    {
                        uniqueWrites[
                            writeCount++] =
                            identity;

                        if (out.retainedWriteResourceCount <
                            out.writeResources.size())
                        {
                            out.writeResources[
                                out.retainedWriteResourceCount++] =
                                identity;
                        }
                    }

                    writeHash =
                        Mix(
                            writeHash,
                            identity);
                };

            for (const auto identity :
                 context.state.rtvs)
            {
                addWrite(identity);
            }

            addWrite(
                context.state.dsv);

            std::uint64_t omHash =
                14695981039346656037ull;
            std::uint64_t csHash =
                14695981039346656037ull;

            for (const auto identity :
                 context.state.omUavs)
            {
                if (identity != 0)
                {
                    omHash =
                        Mix(
                            omHash,
                            identity);
                    addWrite(identity);
                }
            }

            for (const auto identity :
                 context.state.csUavs)
            {
                if (identity != 0)
                {
                    csHash =
                        Mix(
                            csHash,
                            identity);
                    addWrite(identity);
                }
            }

            out.totalReadResourceCount =
                readCount;
            out.totalWriteResourceCount =
                writeCount;
            out.readOverflow =
                readCount >
                out.retainedReadResourceCount;
            out.writeOverflow =
                writeCount >
                out.retainedWriteResourceCount;

            out.readHash =
                suppressBoundAccess
                    ? 0
                    : readHash;
            out.writeHash =
                suppressBoundAccess
                    ? 0
                    : writeHash;

            if (suppressBoundAccess)
            {
                out.readResources.fill(0);
                out.writeResources.fill(0);
                out.totalReadResourceCount = 0;
                out.totalWriteResourceCount = 0;
                out.retainedReadResourceCount = 0;
                out.retainedWriteResourceCount = 0;
                out.readOverflow = false;
                out.writeOverflow = false;
            }

            out.omUavHash = omHash;
            out.csUavHash = csHash;
            out.constantBufferHash =
                cbHash;
            out.samplerHash =
                samplerHash;

            return out;
        }

        void AppendPassLocked(
            ContextRecord& context,
            pc::PassTelemetry pass)
        {
            ++context.originalPassCount;
            const auto signature =
                PassSignature(pass);

            if (!context.runs.empty())
            {
                auto& previous =
                    context.runs.back();

                if (previous.signature ==
                    signature)
                {
                    previous.lastOrdinal =
                        pass.ordinal;
                    ++previous.repeatCount;
                    return;
                }
            }

            if (context.runs.size() >=
                kMaxRunsPerRecording)
            {
                if (!context.overflow)
                {
                    context.overflow = true;
                    ++g_status.recordOverflows;
                }

                return;
            }

            Run run{};
            run.pass =
                std::move(pass);
            run.firstOrdinal =
                run.pass.ordinal;
            run.lastOrdinal =
                run.pass.ordinal;
            run.repeatCount = 1;
            run.signature =
                signature;

            context.runs.push_back(
                std::move(run));
        }

        template <class Fn>
        Fn OriginalLocked(
            ContextRecord& context,
            const std::size_t index)
        {
            if (context.originalVtable ==
                nullptr)
            {
                return nullptr;
            }

            return reinterpret_cast<Fn>(
                context.originalVtable[
                    index]);
        }

        ContextRecord* FindContextLocked(
            ID3D11DeviceContext* const context)
        {
            const auto key =
                reinterpret_cast<std::uintptr_t>(
                    context);

            const auto found =
                g_contexts.find(key);

            return
                found != g_contexts.end()
                    ? &found->second
                    : nullptr;
        }

        void SetShader(
            ID3D11DeviceContext* const context,
            const std::size_t stage,
            ID3D11DeviceChild* const shader)
        {
            std::scoped_lock lock(g_mutex);

            auto* record =
                FindContextLocked(context);

            if (record != nullptr)
            {
                record->state.shaders[stage] =
                    CanonicalId(shader);
            }
        }

        void SetResources(
            ID3D11DeviceContext* const context,
            const std::size_t stage,
            const UINT startSlot,
            const UINT count,
            ID3D11ShaderResourceView* const* resources)
        {
            std::scoped_lock lock(g_mutex);

            auto* record =
                FindContextLocked(context);

            if (record == nullptr)
            {
                return;
            }

            for (UINT i = 0;
                 i < count &&
                 startSlot + i < kSrvSlots;
                 ++i)
            {
                const auto identity =
                    ViewResourceId(
                        resources != nullptr
                            ? resources[i]
                            : nullptr);

                record->state.srvs[
                    stage][startSlot + i] =
                    ConflictsWithOutputs(
                        record->state,
                        identity)
                        ? 0
                        : identity;
            }
        }

        void SetBuffers(
            ID3D11DeviceContext* const context,
            const std::size_t stage,
            const UINT startSlot,
            const UINT count,
            ID3D11Buffer* const* buffers)
        {
            std::scoped_lock lock(g_mutex);

            auto* record =
                FindContextLocked(context);

            if (record == nullptr)
            {
                return;
            }

            for (UINT i = 0;
                 i < count &&
                 startSlot + i < kCbSlots;
                 ++i)
            {
                record->state.cbs[
                    stage][startSlot + i] =
                    buffers != nullptr &&
                    buffers[i] != nullptr
                        ? ResourceId(
                            buffers[i])
                        : 0;
            }
        }

        void SetSamplerState(
            ID3D11DeviceContext* const context,
            const std::size_t stage,
            const UINT startSlot,
            const UINT count,
            ID3D11SamplerState* const* samplers)
        {
            std::scoped_lock lock(g_mutex);

            auto* record =
                FindContextLocked(context);

            if (record == nullptr)
            {
                return;
            }

            for (UINT i = 0;
                 i < count &&
                 startSlot + i < kSamplerSlots;
                 ++i)
            {
                record->state.samplers[
                    stage][startSlot + i] =
                    samplers != nullptr
                        ? CanonicalId(
                            samplers[i])
                        : 0;
            }
        }

        void RecordSimple(
            ID3D11DeviceContext* const context,
            const pc::TelemetryPassKind kind,
            const pc::DrawKind drawKind,
            const std::array<std::uint64_t, 4>& args = {},
            const std::array<std::uintptr_t, 2>& explicitReads = {},
            const std::array<std::uintptr_t, 2>& explicitWrites = {},
            const std::uint32_t explicitReadCount = 0,
            const std::uint32_t explicitWriteCount = 0,
            const std::uint32_t sourceSubresource = 0,
            const std::uint32_t destinationSubresource = 0,
            const std::uint32_t format = 0,
            const std::uint32_t flags = 0,
            const bool suppressBoundAccess = false)
        {
            std::scoped_lock lock(g_mutex);

            auto* record =
                FindContextLocked(context);

            if (record == nullptr)
            {
                return;
            }

            ++record->nextOrdinal;

            AppendPassLocked(
                *record,
                BuildPass(
                    *record,
                    kind,
                    drawKind,
                    args,
                    explicitReads,
                    explicitWrites,
                    explicitReadCount,
                    explicitWriteCount,
                    sourceSubresource,
                    destinationSubresource,
                    format,
                    flags,
                    suppressBoundAccess));
        }

        void STDMETHODCALLTYPE HookVSSetConstantBuffers(
            ID3D11DeviceContext* c,
            UINT start,
            UINT count,
            ID3D11Buffer* const* values)
        {
            SetBuffers(c, 0, start, count, values);
            SetConstantBuffersFn original = nullptr;
            {
                std::scoped_lock lock(g_mutex);
                if (auto* r = FindContextLocked(c))
                    original = OriginalLocked<SetConstantBuffersFn>(*r, kVSSetConstantBuffers);
            }
            if (original) original(c, start, count, values);
        }

        void STDMETHODCALLTYPE HookPSSetConstantBuffers(
            ID3D11DeviceContext* c,
            UINT start,
            UINT count,
            ID3D11Buffer* const* values)
        {
            SetBuffers(c, 4, start, count, values);
            SetConstantBuffersFn original = nullptr;
            {
                std::scoped_lock lock(g_mutex);
                if (auto* r = FindContextLocked(c))
                    original = OriginalLocked<SetConstantBuffersFn>(*r, kPSSetConstantBuffers);
            }
            if (original) original(c, start, count, values);
        }

        void STDMETHODCALLTYPE HookGSSetConstantBuffers(
            ID3D11DeviceContext* c,
            UINT start,
            UINT count,
            ID3D11Buffer* const* values)
        {
            SetBuffers(c, 3, start, count, values);
            SetConstantBuffersFn original = nullptr;
            {
                std::scoped_lock lock(g_mutex);
                if (auto* r = FindContextLocked(c))
                    original = OriginalLocked<SetConstantBuffersFn>(*r, kGSSetConstantBuffers);
            }
            if (original) original(c, start, count, values);
        }

        void STDMETHODCALLTYPE HookHSSetConstantBuffers(
            ID3D11DeviceContext* c,
            UINT start,
            UINT count,
            ID3D11Buffer* const* values)
        {
            SetBuffers(c, 1, start, count, values);
            SetConstantBuffersFn original = nullptr;
            {
                std::scoped_lock lock(g_mutex);
                if (auto* r = FindContextLocked(c))
                    original = OriginalLocked<SetConstantBuffersFn>(*r, kHSSetConstantBuffers);
            }
            if (original) original(c, start, count, values);
        }

        void STDMETHODCALLTYPE HookDSSetConstantBuffers(
            ID3D11DeviceContext* c,
            UINT start,
            UINT count,
            ID3D11Buffer* const* values)
        {
            SetBuffers(c, 2, start, count, values);
            SetConstantBuffersFn original = nullptr;
            {
                std::scoped_lock lock(g_mutex);
                if (auto* r = FindContextLocked(c))
                    original = OriginalLocked<SetConstantBuffersFn>(*r, kDSSetConstantBuffers);
            }
            if (original) original(c, start, count, values);
        }

        void STDMETHODCALLTYPE HookCSSetConstantBuffers(
            ID3D11DeviceContext* c,
            UINT start,
            UINT count,
            ID3D11Buffer* const* values)
        {
            SetBuffers(c, 5, start, count, values);
            SetConstantBuffersFn original = nullptr;
            {
                std::scoped_lock lock(g_mutex);
                if (auto* r = FindContextLocked(c))
                    original = OriginalLocked<SetConstantBuffersFn>(*r, kCSSetConstantBuffers);
            }
            if (original) original(c, start, count, values);
        }

#define GZFX_DEFERRED_RESOURCE_HOOK(Name, Stage, Index) \
        void STDMETHODCALLTYPE Name( \
            ID3D11DeviceContext* c, UINT start, UINT count, \
            ID3D11ShaderResourceView* const* values) \
        { \
            SetResources(c, Stage, start, count, values); \
            SetShaderResourcesFn original = nullptr; \
            { \
                std::scoped_lock lock(g_mutex); \
                if (auto* r = FindContextLocked(c)) \
                    original = OriginalLocked<SetShaderResourcesFn>(*r, Index); \
            } \
            if (original) original(c, start, count, values); \
        }

        GZFX_DEFERRED_RESOURCE_HOOK(
            HookVSSetResources, 0, kVSSetShaderResources)
        GZFX_DEFERRED_RESOURCE_HOOK(
            HookHSSetResources, 1, kHSSetShaderResources)
        GZFX_DEFERRED_RESOURCE_HOOK(
            HookDSSetResources, 2, kDSSetShaderResources)
        GZFX_DEFERRED_RESOURCE_HOOK(
            HookGSSetResources, 3, kGSSetShaderResources)
        GZFX_DEFERRED_RESOURCE_HOOK(
            HookPSSetResources, 4, kPSSetShaderResources)
        GZFX_DEFERRED_RESOURCE_HOOK(
            HookCSSetResources, 5, kCSSetShaderResources)

#undef GZFX_DEFERRED_RESOURCE_HOOK

#define GZFX_DEFERRED_SAMPLER_HOOK(Name, Stage, Index) \
        void STDMETHODCALLTYPE Name( \
            ID3D11DeviceContext* c, UINT start, UINT count, \
            ID3D11SamplerState* const* values) \
        { \
            SetSamplerState(c, Stage, start, count, values); \
            SetSamplersFn original = nullptr; \
            { \
                std::scoped_lock lock(g_mutex); \
                if (auto* r = FindContextLocked(c)) \
                    original = OriginalLocked<SetSamplersFn>(*r, Index); \
            } \
            if (original) original(c, start, count, values); \
        }

        GZFX_DEFERRED_SAMPLER_HOOK(
            HookVSSetSamplers, 0, kVSSetSamplers)
        GZFX_DEFERRED_SAMPLER_HOOK(
            HookHSSetSamplers, 1, kHSSetSamplers)
        GZFX_DEFERRED_SAMPLER_HOOK(
            HookDSSetSamplers, 2, kDSSetSamplers)
        GZFX_DEFERRED_SAMPLER_HOOK(
            HookGSSetSamplers, 3, kGSSetSamplers)
        GZFX_DEFERRED_SAMPLER_HOOK(
            HookPSSetSamplers, 4, kPSSetSamplers)
        GZFX_DEFERRED_SAMPLER_HOOK(
            HookCSSetSamplers, 5, kCSSetSamplers)

#undef GZFX_DEFERRED_SAMPLER_HOOK

        void STDMETHODCALLTYPE HookVSSetShader(
            ID3D11DeviceContext* c,
            ID3D11VertexShader* shader,
            ID3D11ClassInstance* const* classes,
            UINT count)
        {
            SetShader(c, 0, shader);
            VSSetShaderFn original = nullptr;
            {
                std::scoped_lock lock(g_mutex);
                if (auto* r = FindContextLocked(c))
                    original = OriginalLocked<VSSetShaderFn>(*r, kVSSetShader);
            }
            if (original) original(c, shader, classes, count);
        }

        void STDMETHODCALLTYPE HookHSSetShader(
            ID3D11DeviceContext* c,
            ID3D11HullShader* shader,
            ID3D11ClassInstance* const* classes,
            UINT count)
        {
            SetShader(c, 1, shader);
            HSSetShaderFn original = nullptr;
            {
                std::scoped_lock lock(g_mutex);
                if (auto* r = FindContextLocked(c))
                    original = OriginalLocked<HSSetShaderFn>(*r, kHSSetShader);
            }
            if (original) original(c, shader, classes, count);
        }

        void STDMETHODCALLTYPE HookDSSetShader(
            ID3D11DeviceContext* c,
            ID3D11DomainShader* shader,
            ID3D11ClassInstance* const* classes,
            UINT count)
        {
            SetShader(c, 2, shader);
            DSSetShaderFn original = nullptr;
            {
                std::scoped_lock lock(g_mutex);
                if (auto* r = FindContextLocked(c))
                    original = OriginalLocked<DSSetShaderFn>(*r, kDSSetShader);
            }
            if (original) original(c, shader, classes, count);
        }

        void STDMETHODCALLTYPE HookGSSetShader(
            ID3D11DeviceContext* c,
            ID3D11GeometryShader* shader,
            ID3D11ClassInstance* const* classes,
            UINT count)
        {
            SetShader(c, 3, shader);
            GSSetShaderFn original = nullptr;
            {
                std::scoped_lock lock(g_mutex);
                if (auto* r = FindContextLocked(c))
                    original = OriginalLocked<GSSetShaderFn>(*r, kGSSetShader);
            }
            if (original) original(c, shader, classes, count);
        }

        void STDMETHODCALLTYPE HookPSSetShader(
            ID3D11DeviceContext* c,
            ID3D11PixelShader* shader,
            ID3D11ClassInstance* const* classes,
            UINT count)
        {
            SetShader(c, 4, shader);
            PSSetShaderFn original = nullptr;
            {
                std::scoped_lock lock(g_mutex);
                if (auto* r = FindContextLocked(c))
                    original = OriginalLocked<PSSetShaderFn>(*r, kPSSetShader);
            }
            if (original) original(c, shader, classes, count);
        }

        void STDMETHODCALLTYPE HookCSSetShader(
            ID3D11DeviceContext* c,
            ID3D11ComputeShader* shader,
            ID3D11ClassInstance* const* classes,
            UINT count)
        {
            SetShader(c, 5, shader);
            CSSetShaderFn original = nullptr;
            {
                std::scoped_lock lock(g_mutex);
                if (auto* r = FindContextLocked(c))
                    original = OriginalLocked<CSSetShaderFn>(*r, kCSSetShader);
            }
            if (original) original(c, shader, classes, count);
        }

        void STDMETHODCALLTYPE HookOMSetRenderTargets(
            ID3D11DeviceContext* c,
            UINT count,
            ID3D11RenderTargetView* const* rtvs,
            ID3D11DepthStencilView* dsv)
        {
            OMSetRenderTargetsFn original = nullptr;

            {
                std::scoped_lock lock(g_mutex);

                if (auto* r = FindContextLocked(c))
                {
                    r->state.rtvs.fill(0);

                    for (UINT i = 0;
                         i < count &&
                         i < kRtvSlots;
                         ++i)
                    {
                        const auto id =
                            ViewResourceId(
                                rtvs != nullptr
                                    ? rtvs[i]
                                    : nullptr);

                        r->state.rtvs[i] = id;
                        UnbindReads(r->state, id);
                        UnbindComputeUavs(
                            r->state,
                            id);
                    }

                    r->state.dsv =
                        ViewResourceId(dsv);

                    UnbindReads(
                        r->state,
                        r->state.dsv);
                    UnbindComputeUavs(
                        r->state,
                        r->state.dsv);

                    r->state.omUavs.fill(0);

                    original =
                        OriginalLocked<
                            OMSetRenderTargetsFn>(
                                *r,
                                kOMSetRenderTargets);
                }
            }

            if (original)
                original(c, count, rtvs, dsv);
        }

        void STDMETHODCALLTYPE HookOMSetRenderTargetsAndUavs(
            ID3D11DeviceContext* c,
            UINT rtvCount,
            ID3D11RenderTargetView* const* rtvs,
            ID3D11DepthStencilView* dsv,
            UINT uavStart,
            UINT uavCount,
            ID3D11UnorderedAccessView* const* uavs,
            const UINT* initialCounts)
        {
            OMSetRenderTargetsAndUavsFn original = nullptr;

            {
                std::scoped_lock lock(g_mutex);

                if (auto* r = FindContextLocked(c))
                {
                    if (rtvCount !=
                        D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL)
                    {
                        r->state.rtvs.fill(0);

                        for (UINT i = 0;
                             i < rtvCount &&
                             i < kRtvSlots;
                             ++i)
                        {
                            const auto id =
                                ViewResourceId(
                                    rtvs != nullptr
                                        ? rtvs[i]
                                        : nullptr);

                            r->state.rtvs[i] =
                                id;
                            UnbindReads(
                                r->state,
                                id);
                            UnbindComputeUavs(
                                r->state,
                                id);
                        }

                        r->state.dsv =
                            ViewResourceId(dsv);

                        UnbindReads(
                            r->state,
                            r->state.dsv);
                        UnbindComputeUavs(
                            r->state,
                            r->state.dsv);
                    }

                    if (uavStart !=
                        D3D11_KEEP_UNORDERED_ACCESS_VIEWS)
                    {
                        for (UINT i = 0;
                             i < uavCount &&
                             uavStart + i <
                                kUavSlots;
                             ++i)
                        {
                            const auto id =
                                ViewResourceId(
                                    uavs != nullptr
                                        ? uavs[i]
                                        : nullptr);

                            r->state.omUavs[
                                uavStart + i] =
                                id;

                            UnbindReads(
                                r->state,
                                id);
                            UnbindComputeUavs(
                                r->state,
                                id);
                        }
                    }

                    original =
                        OriginalLocked<
                            OMSetRenderTargetsAndUavsFn>(
                                *r,
                                kOMSetRenderTargetsAndUavs);
                }
            }

            if (original)
                original(
                    c,
                    rtvCount,
                    rtvs,
                    dsv,
                    uavStart,
                    uavCount,
                    uavs,
                    initialCounts);
        }

        void STDMETHODCALLTYPE HookCSSetUavs(
            ID3D11DeviceContext* c,
            UINT start,
            UINT count,
            ID3D11UnorderedAccessView* const* uavs,
            const UINT* initialCounts)
        {
            CSSetUnorderedAccessViewsFn original = nullptr;

            {
                std::scoped_lock lock(g_mutex);

                if (auto* r = FindContextLocked(c))
                {
                    for (UINT i = 0;
                         i < count &&
                         start + i <
                            kUavSlots;
                         ++i)
                    {
                        const auto id =
                            ViewResourceId(
                                uavs != nullptr
                                    ? uavs[i]
                                    : nullptr);

                        r->state.csUavs[
                            start + i] =
                            id;

                        UnbindReads(
                            r->state,
                            id);
                    }

                    original =
                        OriginalLocked<
                            CSSetUnorderedAccessViewsFn>(
                                *r,
                                kCSSetUnorderedAccessViews);
                }
            }

            if (original)
                original(
                    c,
                    start,
                    count,
                    uavs,
                    initialCounts);
        }

        void STDMETHODCALLTYPE HookTopology(
            ID3D11DeviceContext* c,
            D3D11_PRIMITIVE_TOPOLOGY topology)
        {
            IASetPrimitiveTopologyFn original = nullptr;

            {
                std::scoped_lock lock(g_mutex);
                if (auto* r = FindContextLocked(c))
                {
                    r->state.topology =
                        topology;
                    original =
                        OriginalLocked<
                            IASetPrimitiveTopologyFn>(
                                *r,
                                kIASetPrimitiveTopology);
                }
            }

            if (original)
                original(c, topology);
        }

        void STDMETHODCALLTYPE HookBlendState(
            ID3D11DeviceContext* c,
            ID3D11BlendState* state,
            const FLOAT factor[4],
            UINT mask)
        {
            OMSetBlendStateFn original = nullptr;

            {
                std::scoped_lock lock(g_mutex);
                if (auto* r = FindContextLocked(c))
                {
                    r->state.blend =
                        CanonicalId(state);
                    original =
                        OriginalLocked<
                            OMSetBlendStateFn>(
                                *r,
                                kOMSetBlendState);
                }
            }

            if (original)
                original(c, state, factor, mask);
        }

        void STDMETHODCALLTYPE HookDepthState(
            ID3D11DeviceContext* c,
            ID3D11DepthStencilState* state,
            UINT stencilRef)
        {
            OMSetDepthStencilStateFn original = nullptr;

            {
                std::scoped_lock lock(g_mutex);
                if (auto* r = FindContextLocked(c))
                {
                    r->state.depthState =
                        CanonicalId(state);
                    original =
                        OriginalLocked<
                            OMSetDepthStencilStateFn>(
                                *r,
                                kOMSetDepthStencilState);
                }
            }

            if (original)
                original(c, state, stencilRef);
        }

        void STDMETHODCALLTYPE HookRasterState(
            ID3D11DeviceContext* c,
            ID3D11RasterizerState* state)
        {
            RSSetStateFn original = nullptr;

            {
                std::scoped_lock lock(g_mutex);
                if (auto* r = FindContextLocked(c))
                {
                    r->state.rasterState =
                        CanonicalId(state);
                    original =
                        OriginalLocked<
                            RSSetStateFn>(
                                *r,
                                kRSSetState);
                }
            }

            if (original)
                original(c, state);
        }

        void STDMETHODCALLTYPE HookViewports(
            ID3D11DeviceContext* c,
            UINT count,
            const D3D11_VIEWPORT* viewports)
        {
            RSSetViewportsFn original = nullptr;

            {
                std::scoped_lock lock(g_mutex);
                if (auto* r = FindContextLocked(c))
                {
                    if (count != 0 &&
                        viewports != nullptr)
                    {
                        r->state.viewportWidth =
                            static_cast<UINT>(
                                viewports[0].Width);
                        r->state.viewportHeight =
                            static_cast<UINT>(
                                viewports[0].Height);
                    }
                    else
                    {
                        r->state.viewportWidth = 0;
                        r->state.viewportHeight = 0;
                    }

                    original =
                        OriginalLocked<
                            RSSetViewportsFn>(
                                *r,
                                kRSSetViewports);
                }
            }

            if (original)
                original(c, count, viewports);
        }

        void STDMETHODCALLTYPE HookClearState(
            ID3D11DeviceContext* c)
        {
            ClearStateFn original = nullptr;

            {
                std::scoped_lock lock(g_mutex);
                if (auto* r = FindContextLocked(c))
                {
                    r->state =
                        CurrentState{};
                    original =
                        OriginalLocked<
                            ClearStateFn>(
                                *r,
                                kClearState);
                }
            }

            if (original)
                original(c);
        }

        void STDMETHODCALLTYPE HookDraw(
            ID3D11DeviceContext* c,
            UINT vertexCount,
            UINT startVertex)
        {
            RecordSimple(
                c,
                pc::TelemetryPassKind::Draw,
                pc::DrawKind::Draw,
                {vertexCount, startVertex, 0, 0});

            DrawFn original = nullptr;
            {
                std::scoped_lock lock(g_mutex);
                if (auto* r = FindContextLocked(c))
                    original = OriginalLocked<DrawFn>(*r, kDraw);
            }
            if (original)
                original(c, vertexCount, startVertex);
        }

        void STDMETHODCALLTYPE HookDrawIndexed(
            ID3D11DeviceContext* c,
            UINT indexCount,
            UINT startIndex,
            INT baseVertex)
        {
            RecordSimple(
                c,
                pc::TelemetryPassKind::Draw,
                pc::DrawKind::DrawIndexed,
                {
                    indexCount,
                    startIndex,
                    static_cast<std::uint64_t>(
                        static_cast<std::int64_t>(
                            baseVertex)),
                    0
                });

            DrawIndexedFn original = nullptr;
            {
                std::scoped_lock lock(g_mutex);
                if (auto* r = FindContextLocked(c))
                    original = OriginalLocked<DrawIndexedFn>(*r, kDrawIndexed);
            }
            if (original)
                original(c, indexCount, startIndex, baseVertex);
        }

        void STDMETHODCALLTYPE HookDrawInstanced(
            ID3D11DeviceContext* c,
            UINT vertexCount,
            UINT instanceCount,
            UINT startVertex,
            UINT startInstance)
        {
            RecordSimple(
                c,
                pc::TelemetryPassKind::Draw,
                pc::DrawKind::DrawInstanced,
                {vertexCount, instanceCount, startVertex, startInstance});

            DrawInstancedFn original = nullptr;
            {
                std::scoped_lock lock(g_mutex);
                if (auto* r = FindContextLocked(c))
                    original = OriginalLocked<DrawInstancedFn>(*r, kDrawInstanced);
            }
            if (original)
                original(
                    c,
                    vertexCount,
                    instanceCount,
                    startVertex,
                    startInstance);
        }

        void STDMETHODCALLTYPE HookDrawIndexedInstanced(
            ID3D11DeviceContext* c,
            UINT indexCount,
            UINT instanceCount,
            UINT startIndex,
            INT baseVertex,
            UINT startInstance)
        {
            RecordSimple(
                c,
                pc::TelemetryPassKind::Draw,
                pc::DrawKind::DrawIndexedInstanced,
                {indexCount, instanceCount, startIndex, startInstance});

            DrawIndexedInstancedFn original = nullptr;
            {
                std::scoped_lock lock(g_mutex);
                if (auto* r = FindContextLocked(c))
                    original = OriginalLocked<DrawIndexedInstancedFn>(*r, kDrawIndexedInstanced);
            }
            if (original)
                original(
                    c,
                    indexCount,
                    instanceCount,
                    startIndex,
                    baseVertex,
                    startInstance);
        }

        void STDMETHODCALLTYPE HookDrawInstancedIndirect(
            ID3D11DeviceContext* c,
            ID3D11Buffer* args,
            UINT offset)
        {
            const std::uintptr_t argId =
                args != nullptr
                    ? ResourceId(args)
                    : 0;

            RecordSimple(
                c,
                pc::TelemetryPassKind::Draw,
                pc::DrawKind::DrawInstancedIndirect,
                {argId, offset, 0, 0});

            DrawInstancedIndirectFn original = nullptr;
            {
                std::scoped_lock lock(g_mutex);
                if (auto* r = FindContextLocked(c))
                    original = OriginalLocked<DrawInstancedIndirectFn>(*r, kDrawInstancedIndirect);
            }
            if (original)
                original(c, args, offset);
        }

        void STDMETHODCALLTYPE HookDrawIndexedInstancedIndirect(
            ID3D11DeviceContext* c,
            ID3D11Buffer* args,
            UINT offset)
        {
            const std::uintptr_t argId =
                args != nullptr
                    ? ResourceId(args)
                    : 0;

            RecordSimple(
                c,
                pc::TelemetryPassKind::Draw,
                pc::DrawKind::DrawIndexedInstancedIndirect,
                {argId, offset, 0, 0});

            DrawIndexedInstancedIndirectFn original = nullptr;
            {
                std::scoped_lock lock(g_mutex);
                if (auto* r = FindContextLocked(c))
                    original = OriginalLocked<DrawIndexedInstancedIndirectFn>(*r, kDrawIndexedInstancedIndirect);
            }
            if (original)
                original(c, args, offset);
        }

        void STDMETHODCALLTYPE HookDispatch(
            ID3D11DeviceContext* c,
            UINT x,
            UINT y,
            UINT z)
        {
            RecordSimple(
                c,
                pc::TelemetryPassKind::Dispatch,
                pc::DrawKind::Draw,
                {x, y, z, 0});

            DispatchFn original = nullptr;
            {
                std::scoped_lock lock(g_mutex);
                if (auto* r = FindContextLocked(c))
                    original = OriginalLocked<DispatchFn>(*r, kDispatch);
            }
            if (original)
                original(c, x, y, z);
        }

        void STDMETHODCALLTYPE HookDispatchIndirect(
            ID3D11DeviceContext* c,
            ID3D11Buffer* args,
            UINT offset)
        {
            const std::uintptr_t argId =
                args != nullptr
                    ? ResourceId(args)
                    : 0;

            RecordSimple(
                c,
                pc::TelemetryPassKind::Dispatch,
                pc::DrawKind::Draw,
                {argId, offset, 0, 0});

            DispatchIndirectFn original = nullptr;
            {
                std::scoped_lock lock(g_mutex);
                if (auto* r = FindContextLocked(c))
                    original = OriginalLocked<DispatchIndirectFn>(*r, kDispatchIndirect);
            }
            if (original)
                original(c, args, offset);
        }

        void STDMETHODCALLTYPE HookCopyResource(
            ID3D11DeviceContext* c,
            ID3D11Resource* destination,
            ID3D11Resource* source)
        {
            const std::uintptr_t read =
                ResourceId(source);
            const std::uintptr_t write =
                ResourceId(destination);

            RecordSimple(
                c,
                pc::TelemetryPassKind::CopyResource,
                pc::DrawKind::Draw,
                {},
                {read, 0},
                {write, 0},
                read != 0 ? 1u : 0u,
                write != 0 ? 1u : 0u,
                0,
                0,
                0,
                0,
                true);

            CopyResourceFn original = nullptr;
            {
                std::scoped_lock lock(g_mutex);
                if (auto* r = FindContextLocked(c))
                    original = OriginalLocked<CopyResourceFn>(*r, kCopyResource);
            }
            if (original)
                original(c, destination, source);
        }

        void STDMETHODCALLTYPE HookCopySubresource(
            ID3D11DeviceContext* c,
            ID3D11Resource* destination,
            UINT destinationSubresource,
            UINT destinationX,
            UINT destinationY,
            UINT destinationZ,
            ID3D11Resource* source,
            UINT sourceSubresource,
            const D3D11_BOX* box)
        {
            const std::uintptr_t read =
                ResourceId(source);
            const std::uintptr_t write =
                ResourceId(destination);

            RecordSimple(
                c,
                pc::TelemetryPassKind::CopySubresource,
                pc::DrawKind::Draw,
                {destinationX, destinationY, destinationZ, box ? 1ull : 0ull},
                {read, 0},
                {write, 0},
                read != 0 ? 1u : 0u,
                write != 0 ? 1u : 0u,
                sourceSubresource,
                destinationSubresource,
                0,
                0,
                true);

            CopySubresourceRegionFn original = nullptr;
            {
                std::scoped_lock lock(g_mutex);
                if (auto* r = FindContextLocked(c))
                    original = OriginalLocked<CopySubresourceRegionFn>(*r, kCopySubresourceRegion);
            }
            if (original)
                original(
                    c,
                    destination,
                    destinationSubresource,
                    destinationX,
                    destinationY,
                    destinationZ,
                    source,
                    sourceSubresource,
                    box);
        }

        void STDMETHODCALLTYPE HookResolve(
            ID3D11DeviceContext* c,
            ID3D11Resource* destination,
            UINT destinationSubresource,
            ID3D11Resource* source,
            UINT sourceSubresource,
            DXGI_FORMAT format)
        {
            const std::uintptr_t read =
                ResourceId(source);
            const std::uintptr_t write =
                ResourceId(destination);

            RecordSimple(
                c,
                pc::TelemetryPassKind::Resolve,
                pc::DrawKind::Draw,
                {},
                {read, 0},
                {write, 0},
                read != 0 ? 1u : 0u,
                write != 0 ? 1u : 0u,
                sourceSubresource,
                destinationSubresource,
                static_cast<std::uint32_t>(
                    format),
                0,
                true);

            ResolveSubresourceFn original = nullptr;
            {
                std::scoped_lock lock(g_mutex);
                if (auto* r = FindContextLocked(c))
                    original = OriginalLocked<ResolveSubresourceFn>(*r, kResolveSubresource);
            }
            if (original)
                original(
                    c,
                    destination,
                    destinationSubresource,
                    source,
                    sourceSubresource,
                    format);
        }

        void STDMETHODCALLTYPE HookClearRtv(
            ID3D11DeviceContext* c,
            ID3D11RenderTargetView* view,
            const FLOAT color[4])
        {
            const auto write =
                ViewResourceId(view);

            RecordSimple(
                c,
                pc::TelemetryPassKind::ClearRtv,
                pc::DrawKind::Draw,
                {},
                {},
                {write, 0},
                0,
                write != 0 ? 1u : 0u,
                0, 0, 0, 0, true);

            ClearRenderTargetViewFn original = nullptr;
            {
                std::scoped_lock lock(g_mutex);
                if (auto* r = FindContextLocked(c))
                    original = OriginalLocked<ClearRenderTargetViewFn>(*r, kClearRenderTargetView);
            }
            if (original)
                original(c, view, color);
        }

        void STDMETHODCALLTYPE HookClearDsv(
            ID3D11DeviceContext* c,
            ID3D11DepthStencilView* view,
            UINT flags,
            FLOAT depth,
            UINT8 stencil)
        {
            const auto write =
                ViewResourceId(view);

            RecordSimple(
                c,
                pc::TelemetryPassKind::ClearDsv,
                pc::DrawKind::Draw,
                {},
                {},
                {write, 0},
                0,
                write != 0 ? 1u : 0u,
                0, 0, 0, flags, true);

            ClearDepthStencilViewFn original = nullptr;
            {
                std::scoped_lock lock(g_mutex);
                if (auto* r = FindContextLocked(c))
                    original = OriginalLocked<ClearDepthStencilViewFn>(*r, kClearDepthStencilView);
            }
            if (original)
                original(c, view, flags, depth, stencil);
        }

        void STDMETHODCALLTYPE HookClearUavUint(
            ID3D11DeviceContext* c,
            ID3D11UnorderedAccessView* view,
            const UINT values[4])
        {
            const auto write =
                ViewResourceId(view);

            RecordSimple(
                c,
                pc::TelemetryPassKind::ClearUavUint,
                pc::DrawKind::Draw,
                {},
                {},
                {write, 0},
                0,
                write != 0 ? 1u : 0u,
                0, 0, 0, 0, true);

            ClearUnorderedAccessViewUintFn original = nullptr;
            {
                std::scoped_lock lock(g_mutex);
                if (auto* r = FindContextLocked(c))
                    original = OriginalLocked<ClearUnorderedAccessViewUintFn>(*r, kClearUnorderedAccessViewUint);
            }
            if (original)
                original(c, view, values);
        }

        void STDMETHODCALLTYPE HookClearUavFloat(
            ID3D11DeviceContext* c,
            ID3D11UnorderedAccessView* view,
            const FLOAT values[4])
        {
            const auto write =
                ViewResourceId(view);

            RecordSimple(
                c,
                pc::TelemetryPassKind::ClearUavFloat,
                pc::DrawKind::Draw,
                {},
                {},
                {write, 0},
                0,
                write != 0 ? 1u : 0u,
                0, 0, 0, 0, true);

            ClearUnorderedAccessViewFloatFn original = nullptr;
            {
                std::scoped_lock lock(g_mutex);
                if (auto* r = FindContextLocked(c))
                    original = OriginalLocked<ClearUnorderedAccessViewFloatFn>(*r, kClearUnorderedAccessViewFloat);
            }
            if (original)
                original(c, view, values);
        }

        void STDMETHODCALLTYPE HookGenerateMips(
            ID3D11DeviceContext* c,
            ID3D11ShaderResourceView* view)
        {
            const auto resource =
                ViewResourceId(view);

            RecordSimple(
                c,
                pc::TelemetryPassKind::GenerateMips,
                pc::DrawKind::Draw,
                {},
                {resource, 0},
                {resource, 0},
                resource != 0 ? 1u : 0u,
                resource != 0 ? 1u : 0u,
                0, 0, 0, 0, true);

            GenerateMipsFn original = nullptr;
            {
                std::scoped_lock lock(g_mutex);
                if (auto* r = FindContextLocked(c))
                    original = OriginalLocked<GenerateMipsFn>(*r, kGenerateMips);
            }
            if (original)
                original(c, view);
        }

        void STDMETHODCALLTYPE HookExecuteCommandList(
            ID3D11DeviceContext* c,
            ID3D11CommandList* list,
            BOOL restore)
        {
            ExecuteCommandListFn original = nullptr;
            std::vector<Run> childRuns{};
            std::uint64_t childGeneration = 0;

            {
                std::scoped_lock lock(g_mutex);

                auto* r =
                    FindContextLocked(c);

                if (r != nullptr)
                {
                    const auto listId =
                        CanonicalId(list);

                    const auto found =
                        g_lists.find(listId);

                    if (found !=
                        g_lists.end())
                    {
                        childRuns =
                            found->second.runs;
                        childGeneration =
                            found->second.generation;
                    }

                    ++r->nextOrdinal;

                    AppendPassLocked(
                        *r,
                        BuildPass(
                            *r,
                            pc::TelemetryPassKind::
                                ExecuteCommandList,
                            pc::DrawKind::Draw,
                            {childGeneration, restore ? 1ull : 0ull, 0, 0},
                            {}, {}, 0, 0, 0, 0, 0, 0, true));

                    for (auto child :
                         childRuns)
                    {
                        if (r->runs.size() >=
                            kMaxRunsPerRecording)
                        {
                            r->overflow = true;
                            ++g_status.recordOverflows;
                            break;
                        }

                        r->runs.push_back(
                            std::move(child));
                    }

                    if (!restore)
                    {
                        r->state =
                            CurrentState{};
                    }

                    original =
                        OriginalLocked<
                            ExecuteCommandListFn>(
                                *r,
                                kExecuteCommandList);
                }
            }

            if (original)
                original(c, list, restore);
        }

        HRESULT STDMETHODCALLTYPE HookFinishCommandList(
            ID3D11DeviceContext* c,
            BOOL restore,
            ID3D11CommandList** list)
        {
            FinishCommandListFn original = nullptr;

            {
                std::scoped_lock lock(g_mutex);
                if (auto* r = FindContextLocked(c))
                    original = OriginalLocked<FinishCommandListFn>(*r, kFinishCommandList);
            }

            if (original == nullptr)
            {
                return E_FAIL;
            }

            const HRESULT result =
                original(c, restore, list);

            if (FAILED(result) ||
                list == nullptr ||
                *list == nullptr)
            {
                return result;
            }

            std::scoped_lock lock(g_mutex);

            auto* r =
                FindContextLocked(c);

            if (r == nullptr)
            {
                return result;
            }

            const auto listId =
                CanonicalId(*list);

            if (g_lists.find(listId) ==
                    g_lists.end() &&
                g_lists.size() >=
                    kMaxSealedLists)
            {
                const auto oldest =
                    std::min_element(
                        g_lists.begin(),
                        g_lists.end(),
                        [](const auto& a,
                           const auto& b)
                        {
                            return
                                a.second.generation <
                                b.second.generation;
                        });

                if (oldest != g_lists.end())
                {
                    g_lists.erase(oldest);
                    ++g_status.registryEvictions;
                }
            }

            CommandListRecord sealed{};
            sealed.identity =
                listId;
            sealed.generation =
                ++g_listGeneration;
            sealed.contextIdentity =
                r->identity;
            sealed.contextGeneration =
                r->generation;
            sealed.recordingGeneration =
                r->recordingGeneration;
            sealed.finishRestoreState =
                restore != FALSE;
            sealed.complete =
                !r->overflow;
            sealed.overflow =
                r->overflow;
            sealed.originalPassCount =
                r->originalPassCount;
            sealed.runs =
                std::move(r->runs);

            g_status.recordedRuns +=
                sealed.runs.size();
            ++g_status.listsFinished;

            g_lists[listId] =
                std::move(sealed);

            ++r->recordingGeneration;
            r->nextOrdinal = 0;
            r->originalPassCount = 0;
            r->overflow = false;
            r->runs.clear();

            if (!restore)
            {
                r->state =
                    CurrentState{};
            }

            return result;
        }

        void InstallHooks(
            ContextRecord& record)
        {
            auto& v =
                record.replacementVtable;

            v[kVSSetConstantBuffers] =
                reinterpret_cast<std::uintptr_t>(
                    &HookVSSetConstantBuffers);
            v[kPSSetConstantBuffers] =
                reinterpret_cast<std::uintptr_t>(
                    &HookPSSetConstantBuffers);
            v[kGSSetConstantBuffers] =
                reinterpret_cast<std::uintptr_t>(
                    &HookGSSetConstantBuffers);
            v[kHSSetConstantBuffers] =
                reinterpret_cast<std::uintptr_t>(
                    &HookHSSetConstantBuffers);
            v[kDSSetConstantBuffers] =
                reinterpret_cast<std::uintptr_t>(
                    &HookDSSetConstantBuffers);
            v[kCSSetConstantBuffers] =
                reinterpret_cast<std::uintptr_t>(
                    &HookCSSetConstantBuffers);

            v[kVSSetShaderResources] =
                reinterpret_cast<std::uintptr_t>(
                    &HookVSSetResources);
            v[kHSSetShaderResources] =
                reinterpret_cast<std::uintptr_t>(
                    &HookHSSetResources);
            v[kDSSetShaderResources] =
                reinterpret_cast<std::uintptr_t>(
                    &HookDSSetResources);
            v[kGSSetShaderResources] =
                reinterpret_cast<std::uintptr_t>(
                    &HookGSSetResources);
            v[kPSSetShaderResources] =
                reinterpret_cast<std::uintptr_t>(
                    &HookPSSetResources);
            v[kCSSetShaderResources] =
                reinterpret_cast<std::uintptr_t>(
                    &HookCSSetResources);

            v[kVSSetSamplers] =
                reinterpret_cast<std::uintptr_t>(
                    &HookVSSetSamplers);
            v[kHSSetSamplers] =
                reinterpret_cast<std::uintptr_t>(
                    &HookHSSetSamplers);
            v[kDSSetSamplers] =
                reinterpret_cast<std::uintptr_t>(
                    &HookDSSetSamplers);
            v[kGSSetSamplers] =
                reinterpret_cast<std::uintptr_t>(
                    &HookGSSetSamplers);
            v[kPSSetSamplers] =
                reinterpret_cast<std::uintptr_t>(
                    &HookPSSetSamplers);
            v[kCSSetSamplers] =
                reinterpret_cast<std::uintptr_t>(
                    &HookCSSetSamplers);

            v[kVSSetShader] =
                reinterpret_cast<std::uintptr_t>(
                    &HookVSSetShader);
            v[kHSSetShader] =
                reinterpret_cast<std::uintptr_t>(
                    &HookHSSetShader);
            v[kDSSetShader] =
                reinterpret_cast<std::uintptr_t>(
                    &HookDSSetShader);
            v[kGSSetShader] =
                reinterpret_cast<std::uintptr_t>(
                    &HookGSSetShader);
            v[kPSSetShader] =
                reinterpret_cast<std::uintptr_t>(
                    &HookPSSetShader);
            v[kCSSetShader] =
                reinterpret_cast<std::uintptr_t>(
                    &HookCSSetShader);

            v[kOMSetRenderTargets] =
                reinterpret_cast<std::uintptr_t>(
                    &HookOMSetRenderTargets);
            v[kOMSetRenderTargetsAndUavs] =
                reinterpret_cast<std::uintptr_t>(
                    &HookOMSetRenderTargetsAndUavs);
            v[kCSSetUnorderedAccessViews] =
                reinterpret_cast<std::uintptr_t>(
                    &HookCSSetUavs);
            v[kIASetPrimitiveTopology] =
                reinterpret_cast<std::uintptr_t>(
                    &HookTopology);
            v[kOMSetBlendState] =
                reinterpret_cast<std::uintptr_t>(
                    &HookBlendState);
            v[kOMSetDepthStencilState] =
                reinterpret_cast<std::uintptr_t>(
                    &HookDepthState);
            v[kRSSetState] =
                reinterpret_cast<std::uintptr_t>(
                    &HookRasterState);
            v[kRSSetViewports] =
                reinterpret_cast<std::uintptr_t>(
                    &HookViewports);
            v[kClearState] =
                reinterpret_cast<std::uintptr_t>(
                    &HookClearState);

            v[kDraw] =
                reinterpret_cast<std::uintptr_t>(
                    &HookDraw);
            v[kDrawIndexed] =
                reinterpret_cast<std::uintptr_t>(
                    &HookDrawIndexed);
            v[kDrawInstanced] =
                reinterpret_cast<std::uintptr_t>(
                    &HookDrawInstanced);
            v[kDrawIndexedInstanced] =
                reinterpret_cast<std::uintptr_t>(
                    &HookDrawIndexedInstanced);
            v[kDrawInstancedIndirect] =
                reinterpret_cast<std::uintptr_t>(
                    &HookDrawInstancedIndirect);
            v[kDrawIndexedInstancedIndirect] =
                reinterpret_cast<std::uintptr_t>(
                    &HookDrawIndexedInstancedIndirect);
            v[kDispatch] =
                reinterpret_cast<std::uintptr_t>(
                    &HookDispatch);
            v[kDispatchIndirect] =
                reinterpret_cast<std::uintptr_t>(
                    &HookDispatchIndirect);

            v[kCopyResource] =
                reinterpret_cast<std::uintptr_t>(
                    &HookCopyResource);
            v[kCopySubresourceRegion] =
                reinterpret_cast<std::uintptr_t>(
                    &HookCopySubresource);
            v[kResolveSubresource] =
                reinterpret_cast<std::uintptr_t>(
                    &HookResolve);
            v[kClearRenderTargetView] =
                reinterpret_cast<std::uintptr_t>(
                    &HookClearRtv);
            v[kClearDepthStencilView] =
                reinterpret_cast<std::uintptr_t>(
                    &HookClearDsv);
            v[kClearUnorderedAccessViewUint] =
                reinterpret_cast<std::uintptr_t>(
                    &HookClearUavUint);
            v[kClearUnorderedAccessViewFloat] =
                reinterpret_cast<std::uintptr_t>(
                    &HookClearUavFloat);
            v[kGenerateMips] =
                reinterpret_cast<std::uintptr_t>(
                    &HookGenerateMips);
            v[kExecuteCommandList] =
                reinterpret_cast<std::uintptr_t>(
                    &HookExecuteCommandList);
            v[kFinishCommandList] =
                reinterpret_cast<std::uintptr_t>(
                    &HookFinishCommandList);
        }
    }

    bool AttachContext(
        ID3D11DeviceContext* const context) noexcept
    {
        try
        {
            if (context == nullptr ||
                context->GetType() !=
                    D3D11_DEVICE_CONTEXT_DEFERRED)
            {
                return false;
            }

            std::scoped_lock lock(g_mutex);

            const auto key =
                reinterpret_cast<std::uintptr_t>(
                    context);

            auto existing =
                g_contexts.find(key);

            auto* currentVtable =
                *reinterpret_cast<
                    std::uintptr_t**>(
                        context);

            if (existing !=
                g_contexts.end())
            {
                if (existing->second.replacementVtable ==
                    currentVtable)
                {
                    return true;
                }

                g_contexts.erase(existing);
            }

            if (currentVtable == nullptr)
            {
                return false;
            }

            auto* replacement =
                static_cast<std::uintptr_t*>(
                    VirtualAlloc(
                        nullptr,
                        sizeof(std::uintptr_t) *
                            kContextVtableCount,
                        MEM_COMMIT |
                            MEM_RESERVE,
                        PAGE_READWRITE));

            if (replacement == nullptr)
            {
                return false;
            }

            std::memcpy(
                replacement,
                currentVtable,
                sizeof(std::uintptr_t) *
                    kContextVtableCount);

            ContextRecord record{};
            record.identity = key;
            record.generation =
                ++g_contextGeneration;
            record.originalVtable =
                currentVtable;
            record.replacementVtable =
                replacement;
            record.runs.reserve(1024);

            InstallHooks(record);

            const auto generation =
                record.generation;

            g_contexts.emplace(
                key,
                std::move(record));

            InterlockedExchangePointer(
                reinterpret_cast<
                    PVOID volatile*>(
                        context),
                replacement);

            ++g_status.contextsAttached;
            (void)generation;

            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool ExecuteObservedCommandList(
        const std::uint64_t frame,
        const std::uint64_t executeOrdinal,
        ID3D11CommandList* const commandList,
        const bool restoreContextState) noexcept
    {
        try
        {
            if (commandList == nullptr)
            {
                return false;
            }

            CommandListRecord record{};

            {
                std::scoped_lock lock(g_mutex);

                const auto id =
                    CanonicalId(
                        commandList);

                const auto found =
                    g_lists.find(id);

                if (found == g_lists.end())
                {
                    ++g_status.opaqueExecutions;
                    return false;
                }

                record =
                    found->second;
                ++g_status.listsExecuted;
            }

            telemetry_listener::DeferredListInfo info{};
            info.frame = frame;
            info.executeOrdinal =
                executeOrdinal;
            info.commandListIdentity =
                record.identity;
            info.commandListGeneration =
                record.generation;
            info.contextIdentity =
                record.contextIdentity;
            info.contextGeneration =
                record.contextGeneration;
            info.recordingGeneration =
                record.recordingGeneration;
            info.originalPassCount =
                record.originalPassCount;
            info.retainedRunCount =
                record.runs.size();
            info.finishRestoreState =
                record.finishRestoreState;
            info.executeRestoreState =
                restoreContextState;
            info.complete =
                record.complete;
            info.overflow =
                record.overflow;

            telemetry_listener::
                OnDeferredList(info);

            for (std::size_t i = 0;
                 i < record.runs.size();
                 ++i)
            {
                const auto& run =
                    record.runs[i];

                telemetry_listener::
                    OnDeferredRun(
                        frame,
                        executeOrdinal,
                        record.generation,
                        record.contextGeneration,
                        record.recordingGeneration,
                        i,
                        run.firstOrdinal,
                        run.lastOrdinal,
                        run.repeatCount,
                        run.pass);
            }

            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    void Reset() noexcept
    {
        try
        {
            std::scoped_lock lock(g_mutex);

            g_lists.clear();

            for (auto& [key, context] :
                 g_contexts)
            {
                (void)key;
                context.state =
                    CurrentState{};
                context.runs.clear();
                context.nextOrdinal = 0;
                context.originalPassCount = 0;
                context.overflow = false;
                ++context.recordingGeneration;
            }
        }
        catch (...)
        {
        }
    }

    Status GetStatus() noexcept
    {
        try
        {
            std::scoped_lock lock(g_mutex);
            return g_status;
        }
        catch (...)
        {
            return {};
        }
    }
}

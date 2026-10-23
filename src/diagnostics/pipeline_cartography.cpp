#include "gzfx/diagnostics/pipeline_cartography.hpp"

#include "gzfx/paths.hpp"
#include "gzfx/version.hpp"

#include <Windows.h>
#include <intrin.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#pragma intrinsic(_ReturnAddress)

namespace gzfx::diagnostics::pipeline_cartography
{
    namespace fs = std::filesystem;

    namespace
    {
        constexpr std::size_t kDeviceVtableCount = 43;
        constexpr std::size_t kStageCount = 6;
        constexpr std::size_t kSrvSlots = D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT;
        constexpr std::size_t kCbSlots = D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT;
        constexpr std::size_t kSamplerSlots = D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT;
        constexpr std::size_t kRtvSlots = D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;
        constexpr std::size_t kUavSlots = D3D11_PS_CS_UAV_REGISTER_COUNT;
        constexpr std::size_t kMaxPasses = 32000;

        constexpr std::size_t kCreateBuffer = 3;
        constexpr std::size_t kCreateTexture1D = 4;
        constexpr std::size_t kCreateTexture2D = 5;
        constexpr std::size_t kCreateTexture3D = 6;
        constexpr std::size_t kCreateVertexShader = 12;
        constexpr std::size_t kCreateGeometryShader = 13;
        constexpr std::size_t kCreateGeometryShaderWithStreamOutput = 14;
        constexpr std::size_t kCreatePixelShader = 15;
        constexpr std::size_t kCreateHullShader = 16;
        constexpr std::size_t kCreateDomainShader = 17;
        constexpr std::size_t kCreateComputeShader = 18;
        constexpr std::size_t kCreateDeferredContext = 27;

        using CreateBufferFn = HRESULT(STDMETHODCALLTYPE*)(
            ID3D11Device*,
            const D3D11_BUFFER_DESC*,
            const D3D11_SUBRESOURCE_DATA*,
            ID3D11Buffer**);

        using CreateTexture1DFn = HRESULT(STDMETHODCALLTYPE*)(
            ID3D11Device*,
            const D3D11_TEXTURE1D_DESC*,
            const D3D11_SUBRESOURCE_DATA*,
            ID3D11Texture1D**);

        using CreateTexture2DFn = HRESULT(STDMETHODCALLTYPE*)(
            ID3D11Device*,
            const D3D11_TEXTURE2D_DESC*,
            const D3D11_SUBRESOURCE_DATA*,
            ID3D11Texture2D**);

        using CreateTexture3DFn = HRESULT(STDMETHODCALLTYPE*)(
            ID3D11Device*,
            const D3D11_TEXTURE3D_DESC*,
            const D3D11_SUBRESOURCE_DATA*,
            ID3D11Texture3D**);

        using CreateVertexShaderFn = HRESULT(STDMETHODCALLTYPE*)(
            ID3D11Device*,
            const void*,
            SIZE_T,
            ID3D11ClassLinkage*,
            ID3D11VertexShader**);

        using CreateGeometryShaderFn = HRESULT(STDMETHODCALLTYPE*)(
            ID3D11Device*,
            const void*,
            SIZE_T,
            ID3D11ClassLinkage*,
            ID3D11GeometryShader**);

        using CreateGeometryShaderWithStreamOutputFn = HRESULT(STDMETHODCALLTYPE*)(
            ID3D11Device*,
            const void*,
            SIZE_T,
            const D3D11_SO_DECLARATION_ENTRY*,
            UINT,
            const UINT*,
            UINT,
            UINT,
            ID3D11ClassLinkage*,
            ID3D11GeometryShader**);

        using CreatePixelShaderFn = HRESULT(STDMETHODCALLTYPE*)(
            ID3D11Device*,
            const void*,
            SIZE_T,
            ID3D11ClassLinkage*,
            ID3D11PixelShader**);

        using CreateHullShaderFn = HRESULT(STDMETHODCALLTYPE*)(
            ID3D11Device*,
            const void*,
            SIZE_T,
            ID3D11ClassLinkage*,
            ID3D11HullShader**);

        using CreateDomainShaderFn = HRESULT(STDMETHODCALLTYPE*)(
            ID3D11Device*,
            const void*,
            SIZE_T,
            ID3D11ClassLinkage*,
            ID3D11DomainShader**);

        using CreateComputeShaderFn = HRESULT(STDMETHODCALLTYPE*)(
            ID3D11Device*,
            const void*,
            SIZE_T,
            ID3D11ClassLinkage*,
            ID3D11ComputeShader**);

        using CreateDeferredContextFn = HRESULT(STDMETHODCALLTYPE*)(
            ID3D11Device*,
            UINT,
            ID3D11DeviceContext**);

        enum class ResourceType : std::uint32_t
        {
            Unknown = 0,
            Buffer,
            Texture1D,
            Texture2D,
            Texture3D
        };

        struct ResourceMeta
        {
            std::uintptr_t id = 0;
            std::uint64_t birth = 0;
            ResourceType type = ResourceType::Unknown;
            std::string owner = "unknown/preexisting";
            std::string debugName;

            UINT width = 0;
            UINT height = 0;
            UINT depth = 0;
            UINT mipLevels = 0;
            UINT arraySize = 0;
            DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
            UINT bindFlags = 0;
            UINT miscFlags = 0;
            UINT sampleCount = 0;

            UINT bufferBytes = 0;
            D3D11_USAGE usage = D3D11_USAGE_DEFAULT;
            UINT cpuAccessFlags = 0;
            UINT structureStride = 0;
        };

        struct ShaderMeta
        {
            std::uintptr_t id = 0;
            std::uint64_t birth = 0;
            ShaderStage stage = ShaderStage::Vertex;
            std::uint64_t hash = 0;
            std::uint64_t bytecodeBytes = 0;
            std::string owner = "unknown/preexisting";
            std::string debugName;
        };

        struct DeviceHookState
        {
            std::uintptr_t* originalVtable = nullptr;
            std::uintptr_t* replacementVtable = nullptr;
        };

        struct ViewRange
        {
            DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
            UINT dimension = 0;
            INT firstMip = -1;
            INT mipCount = -1;
            INT firstSlice = -1;
            INT sliceCount = -1;
            INT subresource = -1;
        };

        struct ViewBinding
        {
            std::uintptr_t resource = 0;
            ViewRange view{};
        };

        struct ShaderBinding
        {
            std::uintptr_t id = 0;
            std::uint64_t hash = 0;
        };

        struct BufferBinding
        {
            std::uintptr_t id = 0;
            UINT bytes = 0;
        };

        struct SamplerBinding
        {
            std::uintptr_t id = 0;
            std::uint64_t hash = 0;
        };

        struct CurrentState
        {
            std::array<ShaderBinding, kStageCount> shaders{};
            std::array<std::array<ViewBinding, kSrvSlots>, kStageCount> srvs{};
            std::array<std::array<BufferBinding, kCbSlots>, kStageCount> cbs{};
            std::array<std::array<SamplerBinding, kSamplerSlots>, kStageCount> samplers{};

            std::array<ViewBinding, kRtvSlots> rtvs{};
            ViewBinding dsv{};
            std::array<ViewBinding, kUavSlots> omUavs{};
            std::array<ViewBinding, kUavSlots> csUavs{};

            std::uintptr_t blendState = 0;
            std::uint64_t blendHash = 0;
            FLOAT blendFactor[4]{};
            UINT sampleMask = 0xFFFFFFFFu;

            std::uintptr_t depthState = 0;
            std::uint64_t depthHash = 0;
            UINT stencilRef = 0;
            bool depthWriteEnabled = true;

            std::uintptr_t rasterState = 0;
            std::uint64_t rasterHash = 0;

            D3D11_PRIMITIVE_TOPOLOGY topology =
                D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;

            UINT viewportCount = 0;
            UINT viewportWidth = 0;
            UINT viewportHeight = 0;
        };

        struct Access
        {
            char mode = 'R';
            char stage = '-';
            UINT slot = 0;
            ViewBinding binding{};
        };

        struct ConstantBufferUse
        {
            char stage = '-';
            UINT slot = 0;
            BufferBinding buffer{};
        };

        struct SamplerUse
        {
            char stage = '-';
            UINT slot = 0;
            SamplerBinding sampler{};
        };

        enum class PassKind : std::uint32_t
        {
            Draw = 0,
            Dispatch,
            CopyResource,
            CopySubresource,
            Resolve,
            ClearRtv,
            ClearDsv,
            ClearUavUint,
            ClearUavFloat,
            GenerateMips,
            ExecuteCommandList
        };

        struct PassRecord
        {
            std::uint64_t id = 0;
            std::uint64_t frame = 0;
            std::uint64_t drawOrdinal = 0;
            PassKind kind = PassKind::Draw;
            DrawKind drawKind = DrawKind::Draw;

            std::array<ShaderBinding, kStageCount> shaders{};

            std::uintptr_t blendState = 0;
            std::uint64_t blendHash = 0;
            std::uintptr_t depthState = 0;
            std::uint64_t depthHash = 0;
            std::uintptr_t rasterState = 0;
            std::uint64_t rasterHash = 0;
            D3D11_PRIMITIVE_TOPOLOGY topology =
                D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;

            UINT viewportCount = 0;
            UINT viewportWidth = 0;
            UINT viewportHeight = 0;

            std::uint64_t arg0 = 0;
            std::uint64_t arg1 = 0;
            std::uint64_t arg2 = 0;
            std::uint64_t arg3 = 0;
            std::uint64_t arg4 = 0;
            std::uint64_t arg5 = 0;

            std::vector<Access> accesses;
            std::vector<ConstantBufferUse> constantBuffers;
            std::vector<SamplerUse> samplers;
        };

        std::mutex g_mutex;
        std::unordered_map<std::uintptr_t, DeviceHookState> g_devices;
        std::unordered_map<std::uintptr_t, ResourceMeta> g_resources;
        std::unordered_map<std::uintptr_t, ShaderMeta> g_shaders;

        std::uint64_t g_resourceBirth = 0;
        std::uint64_t g_shaderBirth = 0;
        std::uint64_t g_passId = 0;
        std::uint64_t g_deferredContextsCreated = 0;

        bool g_captureActive = false;
        std::uint64_t g_startFrame = 0;
        std::uint64_t g_endFrame = 0;
        CurrentState g_state{};
        std::vector<PassRecord> g_passes;

        std::uintptr_t g_knownSceneColor = 0;
        std::uintptr_t g_knownDepth = 0;
        std::uintptr_t g_knownMotion = 0;
        std::uintptr_t g_knownNormal = 0;
        std::uintptr_t g_knownAux = 0;

        void HashMix(
            std::uint64_t& hash,
            const std::uint64_t value)
        {
            constexpr std::uint64_t prime =
                1099511628211ull;

            for (std::size_t i = 0;
                 i < sizeof(value);
                 ++i)
            {
                hash ^=
                    static_cast<std::uint8_t>(
                        value >> (i * 8));

                hash *= prime;
            }
        }

        std::uint64_t HashBytes(const void* data, const std::size_t bytes)
        {
            constexpr std::uint64_t offset =
                14695981039346656037ull;
            constexpr std::uint64_t prime =
                1099511628211ull;

            std::uint64_t hash = offset;

            if (data == nullptr)
            {
                return hash;
            }

            const auto* values =
                static_cast<const std::uint8_t*>(data);

            for (std::size_t i = 0; i < bytes; ++i)
            {
                hash ^= values[i];
                hash *= prime;
            }

            return hash;
        }

        std::uintptr_t CanonicalId(IUnknown* value)
        {
            if (value == nullptr)
            {
                return 0;
            }

            IUnknown* identity = nullptr;

            if (FAILED(value->QueryInterface(
                    __uuidof(IUnknown),
                    reinterpret_cast<void**>(&identity))) ||
                identity == nullptr)
            {
                return reinterpret_cast<std::uintptr_t>(value);
            }

            const auto id =
                reinterpret_cast<std::uintptr_t>(identity);

            identity->Release();
            return id;
        }

        std::string Utf8(const std::wstring& value)
        {
            if (value.empty())
            {
                return {};
            }

            const int bytes =
                WideCharToMultiByte(
                    CP_UTF8,
                    0,
                    value.c_str(),
                    static_cast<int>(value.size()),
                    nullptr,
                    0,
                    nullptr,
                    nullptr);

            if (bytes <= 0)
            {
                return {};
            }

            std::string result(
                static_cast<std::size_t>(bytes),
                '\0');

            WideCharToMultiByte(
                CP_UTF8,
                0,
                value.c_str(),
                static_cast<int>(value.size()),
                result.data(),
                bytes,
                nullptr,
                nullptr);

            return result;
        }

        std::string DebugObjectName(
            ID3D11DeviceChild* child)
        {
            if (child == nullptr)
            {
                return {};
            }

            UINT bytes = 0;

            if (FAILED(
                    child->GetPrivateData(
                        WKPDID_D3DDebugObjectName,
                        &bytes,
                        nullptr)) ||
                bytes == 0 ||
                bytes > 4096)
            {
                return {};
            }

            std::string name(
                bytes,
                '\0');

            if (FAILED(
                    child->GetPrivateData(
                        WKPDID_D3DDebugObjectName,
                        &bytes,
                        name.data())))
            {
                return {};
            }

            while (!name.empty() &&
                   name.back() == '\0')
            {
                name.pop_back();
            }

            for (char& value : name)
            {
                if (value == ',' ||
                    value == '\r' ||
                    value == '\n')
                {
                    value = '_';
                }
            }

            return name;
        }

        std::string CallerModuleName(void* address)
        {
            if (address == nullptr)
            {
                return "unknown";
            }

            HMODULE module = nullptr;

            if (!GetModuleHandleExW(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCWSTR>(address),
                    &module) ||
                module == nullptr)
            {
                return "unknown";
            }

            wchar_t path[MAX_PATH]{};
            const DWORD length =
                GetModuleFileNameW(
                    module,
                    path,
                    static_cast<DWORD>(
                        std::size(path)));

            if (length == 0)
            {
                return "unknown";
            }

            const fs::path modulePath(
                std::wstring(
                    path,
                    path + length));

            return Utf8(
                modulePath.filename().wstring());
        }

        std::string PointerString(
            const std::uintptr_t value)
        {
            std::ostringstream stream;
            stream << "0x"
                   << std::hex
                   << std::uppercase
                   << value;
            return stream.str();
        }

        std::string Hex64(
            const std::uint64_t value)
        {
            std::ostringstream stream;
            stream << "0x"
                   << std::hex
                   << std::uppercase
                   << value;
            return stream.str();
        }

        std::size_t StageIndex(
            const ShaderStage stage)
        {
            return static_cast<std::size_t>(stage);
        }

        char StageChar(
            const ShaderStage stage)
        {
            switch (stage)
            {
            case ShaderStage::Vertex: return 'V';
            case ShaderStage::Hull: return 'H';
            case ShaderStage::Domain: return 'D';
            case ShaderStage::Geometry: return 'G';
            case ShaderStage::Pixel: return 'P';
            case ShaderStage::Compute: return 'C';
            }

            return '?';
        }

        const char* StageName(
            const ShaderStage stage)
        {
            switch (stage)
            {
            case ShaderStage::Vertex: return "VS";
            case ShaderStage::Hull: return "HS";
            case ShaderStage::Domain: return "DS";
            case ShaderStage::Geometry: return "GS";
            case ShaderStage::Pixel: return "PS";
            case ShaderStage::Compute: return "CS";
            }

            return "?";
        }

        const char* PassKindName(
            const PassKind kind)
        {
            switch (kind)
            {
            case PassKind::Draw: return "draw";
            case PassKind::Dispatch: return "dispatch";
            case PassKind::CopyResource: return "copy_resource";
            case PassKind::CopySubresource: return "copy_subresource";
            case PassKind::Resolve: return "resolve";
            case PassKind::ClearRtv: return "clear_rtv";
            case PassKind::ClearDsv: return "clear_dsv";
            case PassKind::ClearUavUint: return "clear_uav_uint";
            case PassKind::ClearUavFloat: return "clear_uav_float";
            case PassKind::GenerateMips: return "generate_mips";
            case PassKind::ExecuteCommandList: return "execute_command_list";
            }

            return "?";
        }

        const char* DrawKindName(
            const DrawKind kind)
        {
            switch (kind)
            {
            case DrawKind::Draw: return "draw";
            case DrawKind::DrawIndexed: return "draw_indexed";
            case DrawKind::DrawInstanced: return "draw_instanced";
            case DrawKind::DrawIndexedInstanced:
                return "draw_indexed_instanced";
            case DrawKind::DrawInstancedIndirect:
                return "draw_instanced_indirect";
            case DrawKind::DrawIndexedInstancedIndirect:
                return "draw_indexed_instanced_indirect";
            }

            return "?";
        }

        const char* ResourceTypeName(
            const ResourceType type)
        {
            switch (type)
            {
            case ResourceType::Buffer: return "buffer";
            case ResourceType::Texture1D: return "texture1d";
            case ResourceType::Texture2D: return "texture2d";
            case ResourceType::Texture3D: return "texture3d";
            case ResourceType::Unknown: break;
            }

            return "unknown";
        }

        bool CaptureFrame(const std::uint64_t frame)
        {
            return
                g_captureActive &&
                frame >= g_startFrame &&
                frame <= g_endFrame &&
                g_passes.size() < kMaxPasses;
        }

        void FillResourceMetaLocked(
            ID3D11Resource* resource,
            ResourceMeta& meta)
        {
            if (resource == nullptr)
            {
                return;
            }

            D3D11_RESOURCE_DIMENSION dimension =
                D3D11_RESOURCE_DIMENSION_UNKNOWN;

            resource->GetType(&dimension);

            if (dimension ==
                D3D11_RESOURCE_DIMENSION_BUFFER)
            {
                ID3D11Buffer* buffer = nullptr;

                if (SUCCEEDED(
                        resource->QueryInterface(
                            __uuidof(ID3D11Buffer),
                            reinterpret_cast<void**>(
                                &buffer))) &&
                    buffer != nullptr)
                {
                    D3D11_BUFFER_DESC description{};
                    buffer->GetDesc(&description);

                    meta.type = ResourceType::Buffer;
                    meta.bufferBytes =
                        description.ByteWidth;
                    meta.bindFlags =
                        description.BindFlags;
                    meta.miscFlags =
                        description.MiscFlags;
                    meta.usage =
                        description.Usage;
                    meta.cpuAccessFlags =
                        description.CPUAccessFlags;
                    meta.structureStride =
                        description.StructureByteStride;

                    buffer->Release();
                }

                return;
            }

            if (dimension ==
                D3D11_RESOURCE_DIMENSION_TEXTURE1D)
            {
                ID3D11Texture1D* texture = nullptr;

                if (SUCCEEDED(
                        resource->QueryInterface(
                            __uuidof(ID3D11Texture1D),
                            reinterpret_cast<void**>(
                                &texture))) &&
                    texture != nullptr)
                {
                    D3D11_TEXTURE1D_DESC description{};
                    texture->GetDesc(&description);

                    meta.type = ResourceType::Texture1D;
                    meta.width = description.Width;
                    meta.height = 1;
                    meta.depth = 1;
                    meta.mipLevels =
                        description.MipLevels;
                    meta.arraySize =
                        description.ArraySize;
                    meta.format =
                        description.Format;
                    meta.bindFlags =
                        description.BindFlags;
                    meta.miscFlags =
                        description.MiscFlags;
                    meta.usage =
                        description.Usage;
                    meta.cpuAccessFlags =
                        description.CPUAccessFlags;
                    meta.sampleCount = 1;

                    texture->Release();
                }

                return;
            }

            if (dimension ==
                D3D11_RESOURCE_DIMENSION_TEXTURE2D)
            {
                ID3D11Texture2D* texture = nullptr;

                if (SUCCEEDED(
                        resource->QueryInterface(
                            __uuidof(ID3D11Texture2D),
                            reinterpret_cast<void**>(
                                &texture))) &&
                    texture != nullptr)
                {
                    D3D11_TEXTURE2D_DESC description{};
                    texture->GetDesc(&description);

                    meta.type = ResourceType::Texture2D;
                    meta.width = description.Width;
                    meta.height = description.Height;
                    meta.depth = 1;
                    meta.mipLevels =
                        description.MipLevels;
                    meta.arraySize =
                        description.ArraySize;
                    meta.format =
                        description.Format;
                    meta.bindFlags =
                        description.BindFlags;
                    meta.miscFlags =
                        description.MiscFlags;
                    meta.usage =
                        description.Usage;
                    meta.cpuAccessFlags =
                        description.CPUAccessFlags;
                    meta.sampleCount =
                        description.SampleDesc.Count;

                    texture->Release();
                }

                return;
            }

            if (dimension ==
                D3D11_RESOURCE_DIMENSION_TEXTURE3D)
            {
                ID3D11Texture3D* texture = nullptr;

                if (SUCCEEDED(
                        resource->QueryInterface(
                            __uuidof(ID3D11Texture3D),
                            reinterpret_cast<void**>(
                                &texture))) &&
                    texture != nullptr)
                {
                    D3D11_TEXTURE3D_DESC description{};
                    texture->GetDesc(&description);

                    meta.type = ResourceType::Texture3D;
                    meta.width = description.Width;
                    meta.height = description.Height;
                    meta.depth = description.Depth;
                    meta.mipLevels =
                        description.MipLevels;
                    meta.arraySize = 1;
                    meta.format =
                        description.Format;
                    meta.bindFlags =
                        description.BindFlags;
                    meta.miscFlags =
                        description.MiscFlags;
                    meta.usage =
                        description.Usage;
                    meta.cpuAccessFlags =
                        description.CPUAccessFlags;
                    meta.sampleCount = 1;

                    texture->Release();
                }
            }
        }

        std::uintptr_t RegisterResourceLocked(
            ID3D11Resource* resource,
            const std::string* owner = nullptr)
        {
            if (resource == nullptr)
            {
                return 0;
            }

            const auto id =
                CanonicalId(resource);

            auto found =
                g_resources.find(id);

            if (found == g_resources.end())
            {
                ResourceMeta meta{};
                meta.id = id;
                meta.birth = ++g_resourceBirth;

                if (owner != nullptr &&
                    !owner->empty())
                {
                    meta.owner = *owner;
                }

                FillResourceMetaLocked(
                    resource,
                    meta);

                meta.debugName =
                    DebugObjectName(
                        resource);

                g_resources.emplace(
                    id,
                    std::move(meta));
            }
            else
            {
                if (owner != nullptr &&
                    !owner->empty() &&
                    found->second.owner ==
                        "unknown/preexisting")
                {
                    found->second.owner = *owner;
                }

                if (found->second.debugName.empty())
                {
                    found->second.debugName =
                        DebugObjectName(
                            resource);
                }
            }

            return id;
        }

        void RegisterBufferCreated(
            ID3D11Buffer* buffer,
            const std::string& owner)
        {
            if (buffer == nullptr)
            {
                return;
            }

            std::scoped_lock lock(g_mutex);

            RegisterResourceLocked(
                buffer,
                &owner);
        }

        void RegisterTexture1DCreated(
            ID3D11Texture1D* texture,
            const std::string& owner)
        {
            if (texture == nullptr)
            {
                return;
            }

            std::scoped_lock lock(g_mutex);

            RegisterResourceLocked(
                texture,
                &owner);
        }

        void RegisterTexture2DCreated(
            ID3D11Texture2D* texture,
            const std::string& owner)
        {
            if (texture == nullptr)
            {
                return;
            }

            std::scoped_lock lock(g_mutex);

            RegisterResourceLocked(
                texture,
                &owner);
        }

        void RegisterTexture3DCreated(
            ID3D11Texture3D* texture,
            const std::string& owner)
        {
            if (texture == nullptr)
            {
                return;
            }

            std::scoped_lock lock(g_mutex);

            RegisterResourceLocked(
                texture,
                &owner);
        }

        template <class ShaderT>
        void RegisterShaderCreated(
            ShaderT* shader,
            const ShaderStage stage,
            const void* bytecode,
            const SIZE_T bytecodeBytes,
            const std::string& owner)
        {
            if (shader == nullptr)
            {
                return;
            }

            std::scoped_lock lock(g_mutex);

            const auto id =
                CanonicalId(shader);

            ShaderMeta meta{};
            meta.id = id;
            meta.birth = ++g_shaderBirth;
            meta.stage = stage;
            meta.hash =
                HashBytes(
                    bytecode,
                    static_cast<std::size_t>(
                        bytecodeBytes));
            meta.bytecodeBytes =
                static_cast<std::uint64_t>(
                    bytecodeBytes);
            meta.owner = owner;
            meta.debugName =
                DebugObjectName(shader);

            g_shaders[id] =
                std::move(meta);
        }

        ShaderBinding ShaderBindingLocked(
            ID3D11DeviceChild* shader,
            const ShaderStage stage)
        {
            ShaderBinding result{};

            if (shader == nullptr)
            {
                return result;
            }

            result.id =
                CanonicalId(shader);

            const auto found =
                g_shaders.find(result.id);

            if (found != g_shaders.end())
            {
                result.hash =
                    found->second.hash;

                if (found->second.debugName.empty())
                {
                    found->second.debugName =
                        DebugObjectName(shader);
                }

                return result;
            }

            ShaderMeta meta{};
            meta.id = result.id;
            meta.birth = ++g_shaderBirth;
            meta.stage = stage;
            meta.hash = 0;
            meta.bytecodeBytes = 0;
            meta.owner = "unknown/preexisting";
            meta.debugName =
                DebugObjectName(shader);

            g_shaders.emplace(
                result.id,
                meta);

            return result;
        }

        BufferBinding BufferBindingLocked(
            ID3D11Buffer* buffer)
        {
            BufferBinding result{};

            if (buffer == nullptr)
            {
                return result;
            }

            result.id =
                RegisterResourceLocked(buffer);

            const auto found =
                g_resources.find(result.id);

            if (found != g_resources.end())
            {
                result.bytes =
                    found->second.bufferBytes;
            }

            return result;
        }

        SamplerBinding SamplerBindingLocked(
            ID3D11SamplerState* sampler)
        {
            SamplerBinding result{};

            if (sampler == nullptr)
            {
                return result;
            }

            result.id =
                CanonicalId(sampler);

            D3D11_SAMPLER_DESC description{};
            sampler->GetDesc(&description);

            result.hash =
                HashBytes(
                    &description,
                    sizeof(description));

            return result;
        }

        template <class StateT, class DescT>
        std::uint64_t StateHash(
            StateT* state)
        {
            if (state == nullptr)
            {
                return 0;
            }

            DescT description{};
            state->GetDesc(&description);

            return
                HashBytes(
                    &description,
                    sizeof(description));
        }

        ViewBinding DescribeSrvLocked(
            ID3D11ShaderResourceView* view)
        {
            ViewBinding result{};

            if (view == nullptr)
            {
                return result;
            }

            ID3D11Resource* resource = nullptr;
            view->GetResource(&resource);

            if (resource == nullptr)
            {
                return result;
            }

            result.resource =
                RegisterResourceLocked(resource);

            resource->Release();

            D3D11_SHADER_RESOURCE_VIEW_DESC description{};
            view->GetDesc(&description);

            result.view.format =
                description.Format;
            result.view.dimension =
                static_cast<UINT>(
                    description.ViewDimension);

            switch (description.ViewDimension)
            {
            case D3D11_SRV_DIMENSION_TEXTURE1D:
                result.view.firstMip =
                    static_cast<INT>(
                        description.Texture1D.MostDetailedMip);
                result.view.mipCount =
                    static_cast<INT>(
                        description.Texture1D.MipLevels);
                break;

            case D3D11_SRV_DIMENSION_TEXTURE1DARRAY:
                result.view.firstMip =
                    static_cast<INT>(
                        description.Texture1DArray.MostDetailedMip);
                result.view.mipCount =
                    static_cast<INT>(
                        description.Texture1DArray.MipLevels);
                result.view.firstSlice =
                    static_cast<INT>(
                        description.Texture1DArray.FirstArraySlice);
                result.view.sliceCount =
                    static_cast<INT>(
                        description.Texture1DArray.ArraySize);
                break;

            case D3D11_SRV_DIMENSION_TEXTURE2D:
                result.view.firstMip =
                    static_cast<INT>(
                        description.Texture2D.MostDetailedMip);
                result.view.mipCount =
                    static_cast<INT>(
                        description.Texture2D.MipLevels);
                break;

            case D3D11_SRV_DIMENSION_TEXTURE2DARRAY:
                result.view.firstMip =
                    static_cast<INT>(
                        description.Texture2DArray.MostDetailedMip);
                result.view.mipCount =
                    static_cast<INT>(
                        description.Texture2DArray.MipLevels);
                result.view.firstSlice =
                    static_cast<INT>(
                        description.Texture2DArray.FirstArraySlice);
                result.view.sliceCount =
                    static_cast<INT>(
                        description.Texture2DArray.ArraySize);
                break;

            case D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY:
                result.view.firstSlice =
                    static_cast<INT>(
                        description.Texture2DMSArray.FirstArraySlice);
                result.view.sliceCount =
                    static_cast<INT>(
                        description.Texture2DMSArray.ArraySize);
                break;

            case D3D11_SRV_DIMENSION_TEXTURE3D:
                result.view.firstMip =
                    static_cast<INT>(
                        description.Texture3D.MostDetailedMip);
                result.view.mipCount =
                    static_cast<INT>(
                        description.Texture3D.MipLevels);
                break;

            case D3D11_SRV_DIMENSION_TEXTURECUBE:
                result.view.firstMip =
                    static_cast<INT>(
                        description.TextureCube.MostDetailedMip);
                result.view.mipCount =
                    static_cast<INT>(
                        description.TextureCube.MipLevels);
                result.view.firstSlice = 0;
                result.view.sliceCount = 6;
                break;

            case D3D11_SRV_DIMENSION_TEXTURECUBEARRAY:
                result.view.firstMip =
                    static_cast<INT>(
                        description.TextureCubeArray.MostDetailedMip);
                result.view.mipCount =
                    static_cast<INT>(
                        description.TextureCubeArray.MipLevels);
                result.view.firstSlice =
                    static_cast<INT>(
                        description.TextureCubeArray.First2DArrayFace);
                result.view.sliceCount =
                    static_cast<INT>(
                        description.TextureCubeArray.NumCubes * 6);
                break;

            default:
                break;
            }

            return result;
        }

        ViewBinding DescribeRtvLocked(
            ID3D11RenderTargetView* view)
        {
            ViewBinding result{};

            if (view == nullptr)
            {
                return result;
            }

            ID3D11Resource* resource = nullptr;
            view->GetResource(&resource);

            if (resource == nullptr)
            {
                return result;
            }

            result.resource =
                RegisterResourceLocked(resource);

            resource->Release();

            D3D11_RENDER_TARGET_VIEW_DESC description{};
            view->GetDesc(&description);

            result.view.format =
                description.Format;
            result.view.dimension =
                static_cast<UINT>(
                    description.ViewDimension);

            switch (description.ViewDimension)
            {
            case D3D11_RTV_DIMENSION_TEXTURE1D:
                result.view.firstMip =
                    static_cast<INT>(
                        description.Texture1D.MipSlice);
                result.view.mipCount = 1;
                break;

            case D3D11_RTV_DIMENSION_TEXTURE1DARRAY:
                result.view.firstMip =
                    static_cast<INT>(
                        description.Texture1DArray.MipSlice);
                result.view.mipCount = 1;
                result.view.firstSlice =
                    static_cast<INT>(
                        description.Texture1DArray.FirstArraySlice);
                result.view.sliceCount =
                    static_cast<INT>(
                        description.Texture1DArray.ArraySize);
                break;

            case D3D11_RTV_DIMENSION_TEXTURE2D:
                result.view.firstMip =
                    static_cast<INT>(
                        description.Texture2D.MipSlice);
                result.view.mipCount = 1;
                break;

            case D3D11_RTV_DIMENSION_TEXTURE2DARRAY:
                result.view.firstMip =
                    static_cast<INT>(
                        description.Texture2DArray.MipSlice);
                result.view.mipCount = 1;
                result.view.firstSlice =
                    static_cast<INT>(
                        description.Texture2DArray.FirstArraySlice);
                result.view.sliceCount =
                    static_cast<INT>(
                        description.Texture2DArray.ArraySize);
                break;

            case D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY:
                result.view.firstSlice =
                    static_cast<INT>(
                        description.Texture2DMSArray.FirstArraySlice);
                result.view.sliceCount =
                    static_cast<INT>(
                        description.Texture2DMSArray.ArraySize);
                break;

            case D3D11_RTV_DIMENSION_TEXTURE3D:
                result.view.firstMip =
                    static_cast<INT>(
                        description.Texture3D.MipSlice);
                result.view.mipCount = 1;
                result.view.firstSlice =
                    static_cast<INT>(
                        description.Texture3D.FirstWSlice);
                result.view.sliceCount =
                    static_cast<INT>(
                        description.Texture3D.WSize);
                break;

            default:
                break;
            }

            return result;
        }

        ViewBinding DescribeDsvLocked(
            ID3D11DepthStencilView* view)
        {
            ViewBinding result{};

            if (view == nullptr)
            {
                return result;
            }

            ID3D11Resource* resource = nullptr;
            view->GetResource(&resource);

            if (resource == nullptr)
            {
                return result;
            }

            result.resource =
                RegisterResourceLocked(resource);

            resource->Release();

            D3D11_DEPTH_STENCIL_VIEW_DESC description{};
            view->GetDesc(&description);

            result.view.format =
                description.Format;
            result.view.dimension =
                static_cast<UINT>(
                    description.ViewDimension);

            switch (description.ViewDimension)
            {
            case D3D11_DSV_DIMENSION_TEXTURE1D:
                result.view.firstMip =
                    static_cast<INT>(
                        description.Texture1D.MipSlice);
                result.view.mipCount = 1;
                break;

            case D3D11_DSV_DIMENSION_TEXTURE1DARRAY:
                result.view.firstMip =
                    static_cast<INT>(
                        description.Texture1DArray.MipSlice);
                result.view.mipCount = 1;
                result.view.firstSlice =
                    static_cast<INT>(
                        description.Texture1DArray.FirstArraySlice);
                result.view.sliceCount =
                    static_cast<INT>(
                        description.Texture1DArray.ArraySize);
                break;

            case D3D11_DSV_DIMENSION_TEXTURE2D:
                result.view.firstMip =
                    static_cast<INT>(
                        description.Texture2D.MipSlice);
                result.view.mipCount = 1;
                break;

            case D3D11_DSV_DIMENSION_TEXTURE2DARRAY:
                result.view.firstMip =
                    static_cast<INT>(
                        description.Texture2DArray.MipSlice);
                result.view.mipCount = 1;
                result.view.firstSlice =
                    static_cast<INT>(
                        description.Texture2DArray.FirstArraySlice);
                result.view.sliceCount =
                    static_cast<INT>(
                        description.Texture2DArray.ArraySize);
                break;

            case D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY:
                result.view.firstSlice =
                    static_cast<INT>(
                        description.Texture2DMSArray.FirstArraySlice);
                result.view.sliceCount =
                    static_cast<INT>(
                        description.Texture2DMSArray.ArraySize);
                break;

            default:
                break;
            }

            return result;
        }

        ViewBinding DescribeUavLocked(
            ID3D11UnorderedAccessView* view)
        {
            ViewBinding result{};

            if (view == nullptr)
            {
                return result;
            }

            ID3D11Resource* resource = nullptr;
            view->GetResource(&resource);

            if (resource == nullptr)
            {
                return result;
            }

            result.resource =
                RegisterResourceLocked(resource);

            resource->Release();

            D3D11_UNORDERED_ACCESS_VIEW_DESC description{};
            view->GetDesc(&description);

            result.view.format =
                description.Format;
            result.view.dimension =
                static_cast<UINT>(
                    description.ViewDimension);

            switch (description.ViewDimension)
            {
            case D3D11_UAV_DIMENSION_TEXTURE1D:
                result.view.firstMip =
                    static_cast<INT>(
                        description.Texture1D.MipSlice);
                result.view.mipCount = 1;
                break;

            case D3D11_UAV_DIMENSION_TEXTURE1DARRAY:
                result.view.firstMip =
                    static_cast<INT>(
                        description.Texture1DArray.MipSlice);
                result.view.mipCount = 1;
                result.view.firstSlice =
                    static_cast<INT>(
                        description.Texture1DArray.FirstArraySlice);
                result.view.sliceCount =
                    static_cast<INT>(
                        description.Texture1DArray.ArraySize);
                break;

            case D3D11_UAV_DIMENSION_TEXTURE2D:
                result.view.firstMip =
                    static_cast<INT>(
                        description.Texture2D.MipSlice);
                result.view.mipCount = 1;
                break;

            case D3D11_UAV_DIMENSION_TEXTURE2DARRAY:
                result.view.firstMip =
                    static_cast<INT>(
                        description.Texture2DArray.MipSlice);
                result.view.mipCount = 1;
                result.view.firstSlice =
                    static_cast<INT>(
                        description.Texture2DArray.FirstArraySlice);
                result.view.sliceCount =
                    static_cast<INT>(
                        description.Texture2DArray.ArraySize);
                break;

            case D3D11_UAV_DIMENSION_TEXTURE3D:
                result.view.firstMip =
                    static_cast<INT>(
                        description.Texture3D.MipSlice);
                result.view.mipCount = 1;
                result.view.firstSlice =
                    static_cast<INT>(
                        description.Texture3D.FirstWSlice);
                result.view.sliceCount =
                    static_cast<INT>(
                        description.Texture3D.WSize);
                break;

            default:
                break;
            }

            return result;
        }

        ViewBinding ResourceBindingLocked(
            ID3D11Resource* resource,
            const INT subresource = -1)
        {
            ViewBinding result{};

            if (resource == nullptr)
            {
                return result;
            }

            result.resource =
                RegisterResourceLocked(resource);

            result.view.subresource =
                subresource;

            const auto found =
                g_resources.find(
                    result.resource);

            if (found != g_resources.end())
            {
                result.view.format =
                    found->second.format;
            }

            return result;
        }

        bool RangeOverlaps(
            const INT firstA,
            const INT countA,
            const INT firstB,
            const INT countB)
        {
            if (firstA < 0 ||
                countA < 0 ||
                firstB < 0 ||
                countB < 0)
            {
                return true;
            }

            const std::int64_t endA =
                static_cast<std::int64_t>(firstA) +
                std::max<INT>(countA, 1);

            const std::int64_t endB =
                static_cast<std::int64_t>(firstB) +
                std::max<INT>(countB, 1);

            return
                static_cast<std::int64_t>(firstA) < endB &&
                static_cast<std::int64_t>(firstB) < endA;
        }

        bool ViewsOverlap(
            const ViewBinding& left,
            const ViewBinding& right)
        {
            if (left.resource == 0 ||
                right.resource == 0 ||
                left.resource != right.resource)
            {
                return false;
            }

            if (left.view.subresource >= 0 ||
                right.view.subresource >= 0)
            {
                // Copy/resolve records carry absolute subresource indices.
                // If the other side is a view range we cannot safely derive
                // mip/array coordinates without the resource's mip count,
                // so remain conservative rather than inventing precision.
                if (left.view.subresource >= 0 &&
                    right.view.subresource >= 0)
                {
                    return
                        left.view.subresource ==
                        right.view.subresource;
                }

                return true;
            }

            return
                RangeOverlaps(
                    left.view.firstMip,
                    left.view.mipCount,
                    right.view.firstMip,
                    right.view.mipCount) &&
                RangeOverlaps(
                    left.view.firstSlice,
                    left.view.sliceCount,
                    right.view.firstSlice,
                    right.view.sliceCount);
        }

        bool ConflictsWithOutputsLocked(
            const ViewBinding& binding)
        {
            if (binding.resource == 0)
            {
                return false;
            }

            for (const auto& output :
                 g_state.rtvs)
            {
                if (ViewsOverlap(
                        binding,
                        output))
                {
                    return true;
                }
            }

            if (ViewsOverlap(
                    binding,
                    g_state.dsv))
            {
                return true;
            }

            for (const auto& output :
                 g_state.omUavs)
            {
                if (ViewsOverlap(
                        binding,
                        output))
                {
                    return true;
                }
            }

            for (const auto& output :
                 g_state.csUavs)
            {
                if (ViewsOverlap(
                        binding,
                        output))
                {
                    return true;
                }
            }

            return false;
        }

        void UnbindInputsForOutputLocked(
            const ViewBinding& output)
        {
            if (output.resource == 0)
            {
                return;
            }

            for (auto& stage :
                 g_state.srvs)
            {
                for (auto& input :
                     stage)
                {
                    if (ViewsOverlap(
                            input,
                            output))
                    {
                        input =
                            ViewBinding{};
                    }
                }
            }
        }

        void UnbindComputeUavsForOutputLocked(
            const ViewBinding& output)
        {
            if (output.resource == 0)
            {
                return;
            }

            for (auto& uav :
                 g_state.csUavs)
            {
                if (ViewsOverlap(
                        uav,
                        output))
                {
                    uav =
                        ViewBinding{};
                }
            }
        }

        void UnbindOmOutputsForUavLocked(
            const ViewBinding& uav,
            const UINT uavStartSlot)
        {
            if (uav.resource == 0)
            {
                return;
            }

            for (std::size_t slot = 0;
                 slot < g_state.rtvs.size();
                 ++slot)
            {
                if (slot >= uavStartSlot ||
                    ViewsOverlap(
                        g_state.rtvs[slot],
                        uav))
                {
                    g_state.rtvs[slot] =
                        ViewBinding{};
                }
            }
        }

        void AddAccess(
            PassRecord& pass,
            const char mode,
            const char stage,
            const UINT slot,
            const ViewBinding& binding)
        {
            if (binding.resource == 0)
            {
                return;
            }

            Access access{};
            access.mode = mode;
            access.stage = stage;
            access.slot = slot;
            access.binding = binding;

            pass.accesses.push_back(
                access);
        }

        void SnapshotShadersAndStateLocked(
            PassRecord& pass)
        {
            pass.shaders =
                g_state.shaders;

            pass.blendState =
                g_state.blendState;
            pass.blendHash =
                g_state.blendHash;
            pass.depthState =
                g_state.depthState;
            pass.depthHash =
                g_state.depthHash;
            pass.rasterState =
                g_state.rasterState;
            pass.rasterHash =
                g_state.rasterHash;
            pass.topology =
                g_state.topology;
            pass.viewportCount =
                g_state.viewportCount;
            pass.viewportWidth =
                g_state.viewportWidth;
            pass.viewportHeight =
                g_state.viewportHeight;

            for (std::size_t stage = 0;
                 stage < kStageCount;
                 ++stage)
            {
                for (std::size_t slot = 0;
                     slot < kCbSlots;
                     ++slot)
                {
                    const auto& buffer =
                        g_state.cbs[stage][slot];

                    if (buffer.id == 0)
                    {
                        continue;
                    }

                    ConstantBufferUse use{};
                    use.stage =
                        StageChar(
                            static_cast<ShaderStage>(
                                stage));
                    use.slot =
                        static_cast<UINT>(slot);
                    use.buffer = buffer;

                    pass.constantBuffers.push_back(
                        use);
                }

                for (std::size_t slot = 0;
                     slot < kSamplerSlots;
                     ++slot)
                {
                    const auto& sampler =
                        g_state.samplers[stage][slot];

                    if (sampler.id == 0)
                    {
                        continue;
                    }

                    SamplerUse use{};
                    use.stage =
                        StageChar(
                            static_cast<ShaderStage>(
                                stage));
                    use.slot =
                        static_cast<UINT>(slot);
                    use.sampler = sampler;

                    pass.samplers.push_back(
                        use);
                }
            }
        }

        void AddShaderReadsLocked(
            PassRecord& pass,
            const bool computeOnly)
        {
            for (std::size_t stage = 0;
                 stage < kStageCount;
                 ++stage)
            {
                const auto shaderStage =
                    static_cast<ShaderStage>(stage);

                if (computeOnly &&
                    shaderStage !=
                        ShaderStage::Compute)
                {
                    continue;
                }

                if (!computeOnly &&
                    shaderStage ==
                        ShaderStage::Compute)
                {
                    continue;
                }

                for (std::size_t slot = 0;
                     slot < kSrvSlots;
                     ++slot)
                {
                    AddAccess(
                        pass,
                        'R',
                        StageChar(shaderStage),
                        static_cast<UINT>(slot),
                        g_state.srvs[stage][slot]);
                }
            }
        }

        void AddDrawWritesLocked(
            PassRecord& pass)
        {
            for (std::size_t slot = 0;
                 slot < kRtvSlots;
                 ++slot)
            {
                AddAccess(
                    pass,
                    'W',
                    'O',
                    static_cast<UINT>(slot),
                    g_state.rtvs[slot]);
            }

            if (g_state.depthWriteEnabled)
            {
                AddAccess(
                    pass,
                    'W',
                    'Z',
                    0,
                    g_state.dsv);
            }
            else
            {
                AddAccess(
                    pass,
                    'R',
                    'Z',
                    0,
                    g_state.dsv);
            }

            for (std::size_t slot = 0;
                 slot < kUavSlots;
                 ++slot)
            {
                AddAccess(
                    pass,
                    'W',
                    'U',
                    static_cast<UINT>(slot),
                    g_state.omUavs[slot]);
            }
        }

        void AddDispatchWritesLocked(
            PassRecord& pass)
        {
            for (std::size_t slot = 0;
                 slot < kUavSlots;
                 ++slot)
            {
                AddAccess(
                    pass,
                    'W',
                    'U',
                    static_cast<UINT>(slot),
                    g_state.csUavs[slot]);
            }
        }

        PassRecord& AddPassLocked(
            const std::uint64_t frame,
            const std::uint64_t drawOrdinal,
            const PassKind kind)
        {
            PassRecord pass{};
            pass.id = ++g_passId;
            pass.frame = frame;
            pass.drawOrdinal = drawOrdinal;
            pass.kind = kind;

            SnapshotShadersAndStateLocked(
                pass);

            g_passes.push_back(
                std::move(pass));

            return g_passes.back();
        }

        template <class Fn>
        auto DeviceOriginal(
            ID3D11Device* device,
            const std::size_t index)
        {
            std::scoped_lock lock(g_mutex);

            const auto found =
                g_devices.find(
                    reinterpret_cast<
                        std::uintptr_t>(
                            device));

            if (found == g_devices.end() ||
                found->second.originalVtable ==
                    nullptr)
            {
                return static_cast<Fn>(nullptr);
            }

            return reinterpret_cast<Fn>(
                found->second.originalVtable[
                    index]);
        }

        HRESULT STDMETHODCALLTYPE HookCreateBuffer(
            ID3D11Device* device,
            const D3D11_BUFFER_DESC* description,
            const D3D11_SUBRESOURCE_DATA* data,
            ID3D11Buffer** buffer)
        {
            const auto original =
                DeviceOriginal<CreateBufferFn>(
                    device,
                    kCreateBuffer);

            if (original == nullptr)
            {
                return E_FAIL;
            }

            const std::string owner =
                CallerModuleName(
                    _ReturnAddress());

            const HRESULT result =
                original(
                    device,
                    description,
                    data,
                    buffer);

            if (SUCCEEDED(result) &&
                buffer != nullptr &&
                *buffer != nullptr)
            {
                RegisterBufferCreated(
                    *buffer,
                    owner);
            }

            return result;
        }

        HRESULT STDMETHODCALLTYPE HookCreateTexture1D(
            ID3D11Device* device,
            const D3D11_TEXTURE1D_DESC* description,
            const D3D11_SUBRESOURCE_DATA* data,
            ID3D11Texture1D** texture)
        {
            const auto original =
                DeviceOriginal<CreateTexture1DFn>(
                    device,
                    kCreateTexture1D);

            if (original == nullptr)
            {
                return E_FAIL;
            }

            const std::string owner =
                CallerModuleName(
                    _ReturnAddress());

            const HRESULT result =
                original(
                    device,
                    description,
                    data,
                    texture);

            if (SUCCEEDED(result) &&
                texture != nullptr &&
                *texture != nullptr)
            {
                RegisterTexture1DCreated(
                    *texture,
                    owner);
            }

            return result;
        }

        HRESULT STDMETHODCALLTYPE HookCreateTexture2D(
            ID3D11Device* device,
            const D3D11_TEXTURE2D_DESC* description,
            const D3D11_SUBRESOURCE_DATA* data,
            ID3D11Texture2D** texture)
        {
            const auto original =
                DeviceOriginal<CreateTexture2DFn>(
                    device,
                    kCreateTexture2D);

            if (original == nullptr)
            {
                return E_FAIL;
            }

            const std::string owner =
                CallerModuleName(
                    _ReturnAddress());

            const HRESULT result =
                original(
                    device,
                    description,
                    data,
                    texture);

            if (SUCCEEDED(result) &&
                texture != nullptr &&
                *texture != nullptr)
            {
                RegisterTexture2DCreated(
                    *texture,
                    owner);
            }

            return result;
        }

        HRESULT STDMETHODCALLTYPE HookCreateTexture3D(
            ID3D11Device* device,
            const D3D11_TEXTURE3D_DESC* description,
            const D3D11_SUBRESOURCE_DATA* data,
            ID3D11Texture3D** texture)
        {
            const auto original =
                DeviceOriginal<CreateTexture3DFn>(
                    device,
                    kCreateTexture3D);

            if (original == nullptr)
            {
                return E_FAIL;
            }

            const std::string owner =
                CallerModuleName(
                    _ReturnAddress());

            const HRESULT result =
                original(
                    device,
                    description,
                    data,
                    texture);

            if (SUCCEEDED(result) &&
                texture != nullptr &&
                *texture != nullptr)
            {
                RegisterTexture3DCreated(
                    *texture,
                    owner);
            }

            return result;
        }

        template <class Fn, class ShaderT>
        HRESULT HookCreateShaderCommon(
            ID3D11Device* device,
            const void* bytecode,
            const SIZE_T bytecodeBytes,
            ID3D11ClassLinkage* linkage,
            ShaderT** shader,
            const std::size_t index,
            const ShaderStage stage,
            const std::string& owner)
        {
            const auto original =
                DeviceOriginal<Fn>(
                    device,
                    index);

            if (original == nullptr)
            {
                return E_FAIL;
            }

            const HRESULT result =
                original(
                    device,
                    bytecode,
                    bytecodeBytes,
                    linkage,
                    shader);

            if (SUCCEEDED(result) &&
                shader != nullptr &&
                *shader != nullptr)
            {
                RegisterShaderCreated(
                    *shader,
                    stage,
                    bytecode,
                    bytecodeBytes,
                    owner);
            }

            return result;
        }

        HRESULT STDMETHODCALLTYPE HookCreateVertexShader(
            ID3D11Device* device,
            const void* bytecode,
            SIZE_T bytecodeBytes,
            ID3D11ClassLinkage* linkage,
            ID3D11VertexShader** shader)
        {
            return HookCreateShaderCommon<
                CreateVertexShaderFn>(
                    device,
                    bytecode,
                    bytecodeBytes,
                    linkage,
                    shader,
                    kCreateVertexShader,
                    ShaderStage::Vertex,
                    CallerModuleName(_ReturnAddress()));
        }

        HRESULT STDMETHODCALLTYPE HookCreateGeometryShader(
            ID3D11Device* device,
            const void* bytecode,
            SIZE_T bytecodeBytes,
            ID3D11ClassLinkage* linkage,
            ID3D11GeometryShader** shader)
        {
            return HookCreateShaderCommon<
                CreateGeometryShaderFn>(
                    device,
                    bytecode,
                    bytecodeBytes,
                    linkage,
                    shader,
                    kCreateGeometryShader,
                    ShaderStage::Geometry,
                    CallerModuleName(_ReturnAddress()));
        }

        HRESULT STDMETHODCALLTYPE HookCreateGeometryShaderWithStreamOutput(
            ID3D11Device* device,
            const void* bytecode,
            SIZE_T bytecodeBytes,
            const D3D11_SO_DECLARATION_ENTRY* declaration,
            UINT declarationCount,
            const UINT* bufferStrides,
            UINT strideCount,
            UINT rasterizedStream,
            ID3D11ClassLinkage* linkage,
            ID3D11GeometryShader** shader)
        {
            const auto original =
                DeviceOriginal<
                    CreateGeometryShaderWithStreamOutputFn>(
                        device,
                        kCreateGeometryShaderWithStreamOutput);

            if (original == nullptr)
            {
                return E_FAIL;
            }

            const std::string owner =
                CallerModuleName(
                    _ReturnAddress());

            const HRESULT result =
                original(
                    device,
                    bytecode,
                    bytecodeBytes,
                    declaration,
                    declarationCount,
                    bufferStrides,
                    strideCount,
                    rasterizedStream,
                    linkage,
                    shader);

            if (SUCCEEDED(result) &&
                shader != nullptr &&
                *shader != nullptr)
            {
                RegisterShaderCreated(
                    *shader,
                    ShaderStage::Geometry,
                    bytecode,
                    bytecodeBytes,
                    owner);
            }

            return result;
        }

        HRESULT STDMETHODCALLTYPE HookCreatePixelShader(
            ID3D11Device* device,
            const void* bytecode,
            SIZE_T bytecodeBytes,
            ID3D11ClassLinkage* linkage,
            ID3D11PixelShader** shader)
        {
            return HookCreateShaderCommon<
                CreatePixelShaderFn>(
                    device,
                    bytecode,
                    bytecodeBytes,
                    linkage,
                    shader,
                    kCreatePixelShader,
                    ShaderStage::Pixel,
                    CallerModuleName(_ReturnAddress()));
        }

        HRESULT STDMETHODCALLTYPE HookCreateHullShader(
            ID3D11Device* device,
            const void* bytecode,
            SIZE_T bytecodeBytes,
            ID3D11ClassLinkage* linkage,
            ID3D11HullShader** shader)
        {
            return HookCreateShaderCommon<
                CreateHullShaderFn>(
                    device,
                    bytecode,
                    bytecodeBytes,
                    linkage,
                    shader,
                    kCreateHullShader,
                    ShaderStage::Hull,
                    CallerModuleName(_ReturnAddress()));
        }

        HRESULT STDMETHODCALLTYPE HookCreateDomainShader(
            ID3D11Device* device,
            const void* bytecode,
            SIZE_T bytecodeBytes,
            ID3D11ClassLinkage* linkage,
            ID3D11DomainShader** shader)
        {
            return HookCreateShaderCommon<
                CreateDomainShaderFn>(
                    device,
                    bytecode,
                    bytecodeBytes,
                    linkage,
                    shader,
                    kCreateDomainShader,
                    ShaderStage::Domain,
                    CallerModuleName(_ReturnAddress()));
        }

        HRESULT STDMETHODCALLTYPE HookCreateComputeShader(
            ID3D11Device* device,
            const void* bytecode,
            SIZE_T bytecodeBytes,
            ID3D11ClassLinkage* linkage,
            ID3D11ComputeShader** shader)
        {
            return HookCreateShaderCommon<
                CreateComputeShaderFn>(
                    device,
                    bytecode,
                    bytecodeBytes,
                    linkage,
                    shader,
                    kCreateComputeShader,
                    ShaderStage::Compute,
                    CallerModuleName(_ReturnAddress()));
        }

        HRESULT STDMETHODCALLTYPE HookCreateDeferredContext(
            ID3D11Device* device,
            UINT flags,
            ID3D11DeviceContext** context)
        {
            const auto original =
                DeviceOriginal<CreateDeferredContextFn>(
                    device,
                    kCreateDeferredContext);

            if (original == nullptr)
            {
                return E_FAIL;
            }

            const HRESULT result =
                original(
                    device,
                    flags,
                    context);

            if (SUCCEEDED(result) &&
                context != nullptr &&
                *context != nullptr)
            {
                std::scoped_lock lock(g_mutex);
                ++g_deferredContextsCreated;
            }

            return result;
        }

        void ReleaseViews(
            ID3D11ShaderResourceView** views,
            const std::size_t count)
        {
            for (std::size_t i = 0;
                 i < count;
                 ++i)
            {
                if (views[i] != nullptr)
                {
                    views[i]->Release();
                }
            }
        }

        void ReleaseBuffers(
            ID3D11Buffer** buffers,
            const std::size_t count)
        {
            for (std::size_t i = 0;
                 i < count;
                 ++i)
            {
                if (buffers[i] != nullptr)
                {
                    buffers[i]->Release();
                }
            }
        }

        void ReleaseSamplers(
            ID3D11SamplerState** samplers,
            const std::size_t count)
        {
            for (std::size_t i = 0;
                 i < count;
                 ++i)
            {
                if (samplers[i] != nullptr)
                {
                    samplers[i]->Release();
                }
            }
        }

        template <class ShaderT>
        void ReleaseShader(
            ShaderT* shader)
        {
            if (shader != nullptr)
            {
                shader->Release();
            }
        }

        void SeedStateFromContext(
            ID3D11DeviceContext* context)
        {
            if (context == nullptr)
            {
                return;
            }

            ID3D11VertexShader* vs = nullptr;
            context->VSGetShader(&vs, nullptr, nullptr);
            SetShader(ShaderStage::Vertex, vs);
            ReleaseShader(vs);

            ID3D11HullShader* hs = nullptr;
            context->HSGetShader(&hs, nullptr, nullptr);
            SetShader(ShaderStage::Hull, hs);
            ReleaseShader(hs);

            ID3D11DomainShader* ds = nullptr;
            context->DSGetShader(&ds, nullptr, nullptr);
            SetShader(ShaderStage::Domain, ds);
            ReleaseShader(ds);

            ID3D11GeometryShader* gs = nullptr;
            context->GSGetShader(&gs, nullptr, nullptr);
            SetShader(ShaderStage::Geometry, gs);
            ReleaseShader(gs);

            ID3D11PixelShader* ps = nullptr;
            context->PSGetShader(&ps, nullptr, nullptr);
            SetShader(ShaderStage::Pixel, ps);
            ReleaseShader(ps);

            ID3D11ComputeShader* cs = nullptr;
            context->CSGetShader(&cs, nullptr, nullptr);
            SetShader(ShaderStage::Compute, cs);
            ReleaseShader(cs);

            std::array<ID3D11ShaderResourceView*, kSrvSlots>
                srvs{};

            context->VSGetShaderResources(
                0,
                static_cast<UINT>(srvs.size()),
                srvs.data());
            SetShaderResources(
                ShaderStage::Vertex,
                0,
                static_cast<UINT>(srvs.size()),
                srvs.data());
            ReleaseViews(srvs.data(), srvs.size());
            srvs.fill(nullptr);

            context->HSGetShaderResources(
                0,
                static_cast<UINT>(srvs.size()),
                srvs.data());
            SetShaderResources(
                ShaderStage::Hull,
                0,
                static_cast<UINT>(srvs.size()),
                srvs.data());
            ReleaseViews(srvs.data(), srvs.size());
            srvs.fill(nullptr);

            context->DSGetShaderResources(
                0,
                static_cast<UINT>(srvs.size()),
                srvs.data());
            SetShaderResources(
                ShaderStage::Domain,
                0,
                static_cast<UINT>(srvs.size()),
                srvs.data());
            ReleaseViews(srvs.data(), srvs.size());
            srvs.fill(nullptr);

            context->GSGetShaderResources(
                0,
                static_cast<UINT>(srvs.size()),
                srvs.data());
            SetShaderResources(
                ShaderStage::Geometry,
                0,
                static_cast<UINT>(srvs.size()),
                srvs.data());
            ReleaseViews(srvs.data(), srvs.size());
            srvs.fill(nullptr);

            context->PSGetShaderResources(
                0,
                static_cast<UINT>(srvs.size()),
                srvs.data());
            SetShaderResources(
                ShaderStage::Pixel,
                0,
                static_cast<UINT>(srvs.size()),
                srvs.data());
            ReleaseViews(srvs.data(), srvs.size());
            srvs.fill(nullptr);

            context->CSGetShaderResources(
                0,
                static_cast<UINT>(srvs.size()),
                srvs.data());
            SetShaderResources(
                ShaderStage::Compute,
                0,
                static_cast<UINT>(srvs.size()),
                srvs.data());
            ReleaseViews(srvs.data(), srvs.size());

            std::array<ID3D11Buffer*, kCbSlots>
                buffers{};

            context->VSGetConstantBuffers(
                0,
                static_cast<UINT>(buffers.size()),
                buffers.data());
            SetConstantBuffers(
                ShaderStage::Vertex,
                0,
                static_cast<UINT>(buffers.size()),
                buffers.data());
            ReleaseBuffers(buffers.data(), buffers.size());
            buffers.fill(nullptr);

            context->HSGetConstantBuffers(
                0,
                static_cast<UINT>(buffers.size()),
                buffers.data());
            SetConstantBuffers(
                ShaderStage::Hull,
                0,
                static_cast<UINT>(buffers.size()),
                buffers.data());
            ReleaseBuffers(buffers.data(), buffers.size());
            buffers.fill(nullptr);

            context->DSGetConstantBuffers(
                0,
                static_cast<UINT>(buffers.size()),
                buffers.data());
            SetConstantBuffers(
                ShaderStage::Domain,
                0,
                static_cast<UINT>(buffers.size()),
                buffers.data());
            ReleaseBuffers(buffers.data(), buffers.size());
            buffers.fill(nullptr);

            context->GSGetConstantBuffers(
                0,
                static_cast<UINT>(buffers.size()),
                buffers.data());
            SetConstantBuffers(
                ShaderStage::Geometry,
                0,
                static_cast<UINT>(buffers.size()),
                buffers.data());
            ReleaseBuffers(buffers.data(), buffers.size());
            buffers.fill(nullptr);

            context->PSGetConstantBuffers(
                0,
                static_cast<UINT>(buffers.size()),
                buffers.data());
            SetConstantBuffers(
                ShaderStage::Pixel,
                0,
                static_cast<UINT>(buffers.size()),
                buffers.data());
            ReleaseBuffers(buffers.data(), buffers.size());
            buffers.fill(nullptr);

            context->CSGetConstantBuffers(
                0,
                static_cast<UINT>(buffers.size()),
                buffers.data());
            SetConstantBuffers(
                ShaderStage::Compute,
                0,
                static_cast<UINT>(buffers.size()),
                buffers.data());
            ReleaseBuffers(buffers.data(), buffers.size());

            std::array<ID3D11SamplerState*, kSamplerSlots>
                samplers{};

            context->VSGetSamplers(
                0,
                static_cast<UINT>(samplers.size()),
                samplers.data());
            SetSamplers(
                ShaderStage::Vertex,
                0,
                static_cast<UINT>(samplers.size()),
                samplers.data());
            ReleaseSamplers(samplers.data(), samplers.size());
            samplers.fill(nullptr);

            context->HSGetSamplers(
                0,
                static_cast<UINT>(samplers.size()),
                samplers.data());
            SetSamplers(
                ShaderStage::Hull,
                0,
                static_cast<UINT>(samplers.size()),
                samplers.data());
            ReleaseSamplers(samplers.data(), samplers.size());
            samplers.fill(nullptr);

            context->DSGetSamplers(
                0,
                static_cast<UINT>(samplers.size()),
                samplers.data());
            SetSamplers(
                ShaderStage::Domain,
                0,
                static_cast<UINT>(samplers.size()),
                samplers.data());
            ReleaseSamplers(samplers.data(), samplers.size());
            samplers.fill(nullptr);

            context->GSGetSamplers(
                0,
                static_cast<UINT>(samplers.size()),
                samplers.data());
            SetSamplers(
                ShaderStage::Geometry,
                0,
                static_cast<UINT>(samplers.size()),
                samplers.data());
            ReleaseSamplers(samplers.data(), samplers.size());
            samplers.fill(nullptr);

            context->PSGetSamplers(
                0,
                static_cast<UINT>(samplers.size()),
                samplers.data());
            SetSamplers(
                ShaderStage::Pixel,
                0,
                static_cast<UINT>(samplers.size()),
                samplers.data());
            ReleaseSamplers(samplers.data(), samplers.size());
            samplers.fill(nullptr);

            context->CSGetSamplers(
                0,
                static_cast<UINT>(samplers.size()),
                samplers.data());
            SetSamplers(
                ShaderStage::Compute,
                0,
                static_cast<UINT>(samplers.size()),
                samplers.data());
            ReleaseSamplers(samplers.data(), samplers.size());

            std::array<ID3D11RenderTargetView*, kRtvSlots>
                rtvs{};
            ID3D11DepthStencilView* dsv = nullptr;

            context->OMGetRenderTargets(
                static_cast<UINT>(rtvs.size()),
                rtvs.data(),
                &dsv);

            SetRenderTargets(
                static_cast<UINT>(rtvs.size()),
                rtvs.data(),
                dsv);

            for (auto* view : rtvs)
            {
                if (view != nullptr) view->Release();
            }
            if (dsv != nullptr) dsv->Release();

            std::array<ID3D11UnorderedAccessView*, kUavSlots>
                omUavs{};

            context->OMGetRenderTargetsAndUnorderedAccessViews(
                0,
                nullptr,
                nullptr,
                0,
                static_cast<UINT>(omUavs.size()),
                omUavs.data());

            SetRenderTargetsAndUavs(
                D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL,
                nullptr,
                nullptr,
                0,
                static_cast<UINT>(omUavs.size()),
                omUavs.data());

            for (auto* view : omUavs)
            {
                if (view != nullptr) view->Release();
            }

            std::array<ID3D11UnorderedAccessView*, kUavSlots>
                csUavs{};

            context->CSGetUnorderedAccessViews(
                0,
                static_cast<UINT>(csUavs.size()),
                csUavs.data());

            SetComputeUavs(
                0,
                static_cast<UINT>(csUavs.size()),
                csUavs.data());

            for (auto* view : csUavs)
            {
                if (view != nullptr) view->Release();
            }

            ID3D11BlendState* blend = nullptr;
            FLOAT blendFactor[4]{};
            UINT sampleMask = 0;
            context->OMGetBlendState(
                &blend,
                blendFactor,
                &sampleMask);
            SetBlendState(
                blend,
                blendFactor,
                sampleMask);
            if (blend != nullptr) blend->Release();

            ID3D11DepthStencilState* depth = nullptr;
            UINT stencilRef = 0;
            context->OMGetDepthStencilState(
                &depth,
                &stencilRef);
            SetDepthStencilState(
                depth,
                stencilRef);
            if (depth != nullptr) depth->Release();

            ID3D11RasterizerState* raster = nullptr;
            context->RSGetState(&raster);
            SetRasterizerState(raster);
            if (raster != nullptr) raster->Release();

            D3D11_PRIMITIVE_TOPOLOGY topology{};
            context->IAGetPrimitiveTopology(
                &topology);
            SetPrimitiveTopology(
                topology);

            std::array<D3D11_VIEWPORT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE>
                viewports{};
            UINT viewportCount =
                static_cast<UINT>(
                    viewports.size());

            context->RSGetViewports(
                &viewportCount,
                viewports.data());

            SetViewports(
                viewportCount,
                viewports.data());
        }

        std::string KnownTag(
            const std::uintptr_t id)
        {
            if (id == g_knownSceneColor)
                return "known_scene_color";
            if (id == g_knownDepth)
                return "known_main_depth";
            if (id == g_knownMotion)
                return "known_motion";
            if (id == g_knownNormal)
                return "known_normal";
            if (id == g_knownAux)
                return "known_aux";
            return {};
        }

        std::string CandidateTags(
            const ResourceMeta& resource,
            const UINT screenWidth,
            const UINT screenHeight)
        {
            std::vector<std::string> tags;

            const std::string known =
                KnownTag(resource.id);

            if (!known.empty())
            {
                tags.push_back(known);
            }

            const bool fullResolution =
                screenWidth != 0 &&
                screenHeight != 0 &&
                resource.width == screenWidth &&
                resource.height == screenHeight;

            const bool depthFormat =
                resource.format ==
                    DXGI_FORMAT_R16_TYPELESS ||
                resource.format ==
                    DXGI_FORMAT_R24G8_TYPELESS ||
                resource.format ==
                    DXGI_FORMAT_R32_TYPELESS ||
                resource.format ==
                    DXGI_FORMAT_D16_UNORM ||
                resource.format ==
                    DXGI_FORMAT_D24_UNORM_S8_UINT ||
                resource.format ==
                    DXGI_FORMAT_D32_FLOAT;

            if (depthFormat &&
                resource.width ==
                    resource.height &&
                resource.width >= 512)
            {
                tags.emplace_back(
                    "shadow_depth_candidate");
            }

            if (fullResolution &&
                resource.format ==
                    DXGI_FORMAT_R16G16B16A16_FLOAT)
            {
                tags.emplace_back(
                    "fullres_hdr_candidate");
            }

            if (fullResolution &&
                resource.format ==
                    DXGI_FORMAT_R16G16_FLOAT)
            {
                tags.emplace_back(
                    "motion_vector_candidate");
            }

            if (fullResolution &&
                resource.format ==
                    DXGI_FORMAT_R8G8B8A8_UNORM)
            {
                tags.emplace_back(
                    "normal_or_mask_candidate");
            }

            if (screenWidth != 0 &&
                screenHeight != 0 &&
                resource.width > 0 &&
                resource.height > 0 &&
                resource.width <=
                    std::max<UINT>(
                        1,
                        screenWidth / 2) &&
                resource.height <=
                    std::max<UINT>(
                        1,
                        screenHeight / 2))
            {
                if (resource.format ==
                        DXGI_FORMAT_R16_FLOAT ||
                    resource.format ==
                        DXGI_FORMAT_R32_FLOAT ||
                    resource.format ==
                        DXGI_FORMAT_R8_UNORM ||
                    resource.format ==
                        DXGI_FORMAT_R16_UNORM)
                {
                    tags.emplace_back(
                        "ao_or_scalar_imagespace_candidate");
                }

                if (resource.format ==
                        DXGI_FORMAT_R16G16B16A16_FLOAT ||
                    resource.format ==
                        DXGI_FORMAT_R11G11B10_FLOAT)
                {
                    tags.emplace_back(
                        "reduced_lighting_candidate");
                }
            }

            if (resource.type ==
                    ResourceType::Texture3D &&
                resource.width > 0 &&
                resource.height > 0 &&
                resource.depth > 1)
            {
                tags.emplace_back(
                    resource.width <= 64 &&
                    resource.height <= 64 &&
                    resource.depth <= 64
                    ? "volume_or_color_lut_candidate"
                    : "volume_texture_candidate");
            }

            if (resource.width > 0 &&
                resource.height > 0 &&
                resource.width <= 256 &&
                resource.height <= 256 &&
                (resource.format ==
                    DXGI_FORMAT_R16_FLOAT ||
                 resource.format ==
                    DXGI_FORMAT_R32_FLOAT ||
                 resource.format ==
                    DXGI_FORMAT_R16G16B16A16_FLOAT))
            {
                tags.emplace_back(
                    "exposure_or_bloom_reduction_candidate");
            }

            std::ostringstream stream;

            for (std::size_t i = 0;
                 i < tags.size();
                 ++i)
            {
                if (i != 0)
                {
                    stream << ";";
                }

                stream << tags[i];
            }

            return stream.str();
        }

        std::uint64_t PassSignatureLocked(
            const PassRecord& pass)
        {
            std::uint64_t hash =
                14695981039346656037ull;

            HashMix(
                hash,
                static_cast<std::uint64_t>(
                    pass.kind));

            HashMix(
                hash,
                static_cast<std::uint64_t>(
                    pass.drawKind));

            HashMix(hash, pass.viewportWidth);
            HashMix(hash, pass.viewportHeight);
            HashMix(hash, pass.viewportCount);

            HashMix(
                hash,
                static_cast<std::uint64_t>(
                    pass.topology));

            HashMix(hash, pass.blendHash);
            HashMix(hash, pass.depthHash);
            HashMix(hash, pass.rasterHash);

            for (const auto& shader :
                 pass.shaders)
            {
                // Zero means creation was not observed. Do not substitute a
                // pointer here; pointer values would destroy cross-capture
                // stability.
                HashMix(
                    hash,
                    shader.hash);
            }

            for (const auto& access :
                 pass.accesses)
            {
                HashMix(
                    hash,
                    static_cast<std::uint8_t>(
                        access.mode));

                HashMix(
                    hash,
                    static_cast<std::uint8_t>(
                        access.stage));

                HashMix(
                    hash,
                    access.slot);

                HashMix(
                    hash,
                    static_cast<std::uint64_t>(
                        access.binding.view.format));

                HashMix(
                    hash,
                    access.binding.view.dimension);

                HashMix(
                    hash,
                    static_cast<std::uint64_t>(
                        static_cast<std::int64_t>(
                            access.binding.view.firstMip)));

                HashMix(
                    hash,
                    static_cast<std::uint64_t>(
                        static_cast<std::int64_t>(
                            access.binding.view.mipCount)));

                HashMix(
                    hash,
                    static_cast<std::uint64_t>(
                        static_cast<std::int64_t>(
                            access.binding.view.firstSlice)));

                HashMix(
                    hash,
                    static_cast<std::uint64_t>(
                        static_cast<std::int64_t>(
                            access.binding.view.sliceCount)));

                const auto resource =
                    g_resources.find(
                        access.binding.resource);

                if (resource !=
                    g_resources.end())
                {
                    HashMix(
                        hash,
                        static_cast<std::uint64_t>(
                            resource->second.type));

                    HashMix(
                        hash,
                        resource->second.width);

                    HashMix(
                        hash,
                        resource->second.height);

                    HashMix(
                        hash,
                        resource->second.depth);

                    HashMix(
                        hash,
                        resource->second.mipLevels);

                    HashMix(
                        hash,
                        resource->second.arraySize);

                    HashMix(
                        hash,
                        static_cast<std::uint64_t>(
                            resource->second.format));
                }
            }

            return hash;
        }

        void WriteCaptureLocked(
            const std::wstring& suffix)
        {
            const fs::path root =
                gzfx::paths::DiagnosticsRoot();

            if (root.empty())
            {
                return;
            }

            std::error_code error;
            fs::create_directories(
                root,
                error);

            if (error)
            {
                return;
            }

            const auto file =
                [&](const wchar_t* base,
                    const wchar_t* extension)
                {
                    return
                        root /
                        (
                            std::wstring(base) +
                            suffix +
                            extension
                        );
                };

            std::unordered_map<
                std::uintptr_t,
                std::uint64_t>
                    reads;

            std::unordered_map<
                std::uintptr_t,
                std::uint64_t>
                    writes;

            std::unordered_map<
                std::uintptr_t,
                std::uint64_t>
                    firstPass;

            std::unordered_map<
                std::uintptr_t,
                std::uint64_t>
                    lastPass;

            for (const auto& pass : g_passes)
            {
                for (const auto& access :
                     pass.accesses)
                {
                    if (access.binding.resource == 0)
                    {
                        continue;
                    }

                    if (access.mode == 'R')
                    {
                        ++reads[
                            access.binding.resource];
                    }
                    else
                    {
                        ++writes[
                            access.binding.resource];
                    }

                    if (firstPass.find(
                            access.binding.resource) ==
                        firstPass.end())
                    {
                        firstPass[
                            access.binding.resource] =
                            pass.id;
                    }

                    lastPass[
                        access.binding.resource] =
                        pass.id;
                }
            }

            UINT screenWidth = 0;
            UINT screenHeight = 0;

            const auto knownScene =
                g_resources.find(
                    g_knownSceneColor);

            if (knownScene !=
                g_resources.end())
            {
                screenWidth =
                    knownScene->second.width;
                screenHeight =
                    knownScene->second.height;
            }

            {
                std::ofstream out(
                    file(
                        L"pipeline_passes",
                        L".csv"),
                    std::ios::out |
                        std::ios::trunc);

                if (out)
                {
                    out
                        << "pass_id,frame,draw_ordinal,kind,draw_kind,"
                           "vs,vs_hash,hs,hs_hash,ds,ds_hash,gs,gs_hash,"
                           "ps,ps_hash,cs,cs_hash,"
                           "viewport_count,viewport_width,viewport_height,"
                           "topology,blend_state,blend_hash,depth_state,"
                           "depth_hash,raster_state,raster_hash,"
                           "arg0,arg1,arg2,arg3,arg4,arg5,"
                           "access_count,cb_count,sampler_count\n";

                    for (const auto& pass :
                         g_passes)
                    {
                        out
                            << pass.id << ","
                            << pass.frame << ","
                            << pass.drawOrdinal << ","
                            << PassKindName(
                                pass.kind) << ","
                            << DrawKindName(
                                pass.drawKind);

                        for (std::size_t stage = 0;
                             stage < kStageCount;
                             ++stage)
                        {
                            out
                                << ","
                                << PointerString(
                                    pass.shaders[
                                        stage].id)
                                << ","
                                << Hex64(
                                    pass.shaders[
                                        stage].hash);
                        }

                        out
                            << ","
                            << pass.viewportCount
                            << ","
                            << pass.viewportWidth
                            << ","
                            << pass.viewportHeight
                            << ","
                            << static_cast<
                                std::uint32_t>(
                                    pass.topology)
                            << ","
                            << PointerString(
                                pass.blendState)
                            << ","
                            << Hex64(
                                pass.blendHash)
                            << ","
                            << PointerString(
                                pass.depthState)
                            << ","
                            << Hex64(
                                pass.depthHash)
                            << ","
                            << PointerString(
                                pass.rasterState)
                            << ","
                            << Hex64(
                                pass.rasterHash)
                            << ","
                            << pass.arg0 << ","
                            << pass.arg1 << ","
                            << pass.arg2 << ","
                            << pass.arg3 << ","
                            << pass.arg4 << ","
                            << pass.arg5 << ","
                            << pass.accesses.size()
                            << ","
                            << pass.constantBuffers.size()
                            << ","
                            << pass.samplers.size()
                            << "\n";
                    }
                }
            }

            {
                std::ofstream out(
                    file(
                        L"pipeline_accesses",
                        L".csv"),
                    std::ios::out |
                        std::ios::trunc);

                if (out)
                {
                    out
                        << "pass_id,frame,mode,stage,slot,resource,"
                           "view_format,view_dimension,first_mip,mip_count,"
                           "first_slice,slice_count,subresource\n";

                    for (const auto& pass :
                         g_passes)
                    {
                        for (const auto& access :
                             pass.accesses)
                        {
                            out
                                << pass.id << ","
                                << pass.frame << ","
                                << access.mode << ","
                                << access.stage << ","
                                << access.slot << ","
                                << PointerString(
                                    access.binding.resource)
                                << ","
                                << static_cast<
                                    std::uint32_t>(
                                        access.binding.view.format)
                                << ","
                                << access.binding.view.dimension
                                << ","
                                << access.binding.view.firstMip
                                << ","
                                << access.binding.view.mipCount
                                << ","
                                << access.binding.view.firstSlice
                                << ","
                                << access.binding.view.sliceCount
                                << ","
                                << access.binding.view.subresource
                                << "\n";
                        }
                    }
                }
            }

            {
                std::ofstream out(
                    file(
                        L"pipeline_constant_buffers",
                        L".csv"),
                    std::ios::out |
                        std::ios::trunc);

                if (out)
                {
                    out
                        << "pass_id,frame,stage,slot,buffer,bytes,birth,owner\n";

                    for (const auto& pass :
                         g_passes)
                    {
                        for (const auto& use :
                             pass.constantBuffers)
                        {
                            const auto found =
                                g_resources.find(
                                    use.buffer.id);

                            out
                                << pass.id << ","
                                << pass.frame << ","
                                << use.stage << ","
                                << use.slot << ","
                                << PointerString(
                                    use.buffer.id)
                                << ","
                                << use.buffer.bytes
                                << ",";

                            if (found !=
                                g_resources.end())
                            {
                                out
                                    << found->second.birth
                                    << ","
                                    << found->second.owner;
                            }
                            else
                            {
                                out
                                    << "0,unknown";
                            }

                            out << "\n";
                        }
                    }
                }
            }

            {
                std::ofstream out(
                    file(
                        L"pipeline_samplers",
                        L".csv"),
                    std::ios::out |
                        std::ios::trunc);

                if (out)
                {
                    out
                        << "pass_id,frame,stage,slot,sampler,sampler_hash\n";

                    for (const auto& pass :
                         g_passes)
                    {
                        for (const auto& use :
                             pass.samplers)
                        {
                            out
                                << pass.id << ","
                                << pass.frame << ","
                                << use.stage << ","
                                << use.slot << ","
                                << PointerString(
                                    use.sampler.id)
                                << ","
                                << Hex64(
                                    use.sampler.hash)
                                << "\n";
                        }
                    }
                }
            }

            {
                struct SignatureStats
                {
                    std::uint64_t signature = 0;
                    PassKind kind = PassKind::Draw;
                    std::uint64_t count = 0;
                    std::uint64_t firstPass = 0;
                    std::uint64_t lastPass = 0;
                    std::uint64_t firstFrame = 0;
                    std::uint64_t lastFrame = 0;
                };

                std::map<
                    std::uint64_t,
                    SignatureStats>
                        signatures;

                for (const auto& pass :
                     g_passes)
                {
                    const std::uint64_t signature =
                        PassSignatureLocked(
                            pass);

                    auto& stats =
                        signatures[signature];

                    if (stats.count == 0)
                    {
                        stats.signature =
                            signature;
                        stats.kind =
                            pass.kind;
                        stats.firstPass =
                            pass.id;
                        stats.firstFrame =
                            pass.frame;
                    }

                    ++stats.count;
                    stats.lastPass =
                        pass.id;
                    stats.lastFrame =
                        pass.frame;
                }

                std::ofstream out(
                    file(
                        L"pipeline_signatures",
                        L".csv"),
                    std::ios::out |
                        std::ios::trunc);

                if (out)
                {
                    out
                        << "signature,kind,count,first_pass,last_pass,"
                           "first_frame,last_frame\n";

                    for (const auto& [key, stats] :
                         signatures)
                    {
                        (void)key;

                        out
                            << Hex64(
                                stats.signature)
                            << ","
                            << PassKindName(
                                stats.kind)
                            << ","
                            << stats.count
                            << ","
                            << stats.firstPass
                            << ","
                            << stats.lastPass
                            << ","
                            << stats.firstFrame
                            << ","
                            << stats.lastFrame
                            << "\n";
                    }
                }
            }

            {
                std::ofstream out(
                    file(
                        L"pipeline_resources",
                        L".csv"),
                    std::ios::out |
                        std::ios::trunc);

                if (out)
                {
                    out
                        << "resource,birth,owner,type,width,height,depth,"
                           "mips,array_size,format,bind_flags,misc_flags,"
                           "sample_count,buffer_bytes,usage,cpu_access,"
                           "structure_stride,reads,writes,first_pass,last_pass,debug_name,tags\n";

                    std::vector<ResourceMeta>
                        resources;

                    resources.reserve(
                        g_resources.size());

                    for (const auto& [id, meta] :
                         g_resources)
                    {
                        (void)id;
                        resources.push_back(meta);
                    }

                    std::sort(
                        resources.begin(),
                        resources.end(),
                        [](const ResourceMeta& left,
                           const ResourceMeta& right)
                        {
                            return
                                left.birth <
                                right.birth;
                        });

                    for (const auto& resource :
                         resources)
                    {
                        const auto read =
                            reads.find(resource.id);
                        const auto write =
                            writes.find(resource.id);
                        const auto first =
                            firstPass.find(resource.id);
                        const auto last =
                            lastPass.find(resource.id);

                        out
                            << PointerString(
                                resource.id)
                            << ","
                            << resource.birth
                            << ","
                            << resource.owner
                            << ","
                            << ResourceTypeName(
                                resource.type)
                            << ","
                            << resource.width
                            << ","
                            << resource.height
                            << ","
                            << resource.depth
                            << ","
                            << resource.mipLevels
                            << ","
                            << resource.arraySize
                            << ","
                            << static_cast<
                                std::uint32_t>(
                                    resource.format)
                            << ",0x"
                            << std::hex
                            << resource.bindFlags
                            << ",0x"
                            << resource.miscFlags
                            << std::dec
                            << ","
                            << resource.sampleCount
                            << ","
                            << resource.bufferBytes
                            << ","
                            << static_cast<
                                std::uint32_t>(
                                    resource.usage)
                            << ",0x"
                            << std::hex
                            << resource.cpuAccessFlags
                            << std::dec
                            << ","
                            << resource.structureStride
                            << ","
                            << (read == reads.end()
                                ? 0
                                : read->second)
                            << ","
                            << (write == writes.end()
                                ? 0
                                : write->second)
                            << ","
                            << (first == firstPass.end()
                                ? 0
                                : first->second)
                            << ","
                            << (last == lastPass.end()
                                ? 0
                                : last->second)
                            << ","
                            << resource.debugName
                            << ","
                            << CandidateTags(
                                resource,
                                screenWidth,
                                screenHeight)
                            << "\n";
                    }
                }
            }

            {
                std::ofstream out(
                    file(
                        L"pipeline_shaders",
                        L".csv"),
                    std::ios::out |
                        std::ios::trunc);

                if (out)
                {
                    out
                        << "shader,birth,stage,hash,bytecode_bytes,owner,debug_name\n";

                    std::vector<ShaderMeta>
                        shaders;

                    shaders.reserve(
                        g_shaders.size());

                    for (const auto& [id, meta] :
                         g_shaders)
                    {
                        (void)id;
                        shaders.push_back(meta);
                    }

                    std::sort(
                        shaders.begin(),
                        shaders.end(),
                        [](const ShaderMeta& left,
                           const ShaderMeta& right)
                        {
                            return
                                left.birth <
                                right.birth;
                        });

                    for (const auto& shader :
                         shaders)
                    {
                        out
                            << PointerString(
                                shader.id)
                            << ","
                            << shader.birth
                            << ","
                            << StageName(
                                shader.stage)
                            << ","
                            << Hex64(
                                shader.hash)
                            << ","
                            << shader.bytecodeBytes
                            << ","
                            << shader.owner
                            << ","
                            << shader.debugName
                            << "\n";
                    }
                }
            }

            {
                std::ofstream out(
                    file(
                        L"pipeline_edges",
                        L".csv"),
                    std::ios::out |
                        std::ios::trunc);

                if (out)
                {
                    out
                        << "resource,producer_pass,consumer_pass,"
                           "consumer_stage,consumer_slot,match\n";

                    std::unordered_map<
                        std::uintptr_t,
                        std::uint64_t>
                            lastWriter;

                    std::set<
                        std::tuple<
                            std::uintptr_t,
                            std::uint64_t,
                            std::uint64_t,
                            char,
                            UINT>>
                                emitted;

                    for (const auto& pass :
                         g_passes)
                    {
                        for (const auto& access :
                             pass.accesses)
                        {
                            if (access.mode != 'R' ||
                                access.binding.resource == 0)
                            {
                                continue;
                            }

                            const auto writer =
                                lastWriter.find(
                                    access.binding.resource);

                            if (writer ==
                                lastWriter.end())
                            {
                                continue;
                            }

                            const auto key =
                                std::make_tuple(
                                    access.binding.resource,
                                    writer->second,
                                    pass.id,
                                    access.stage,
                                    access.slot);

                            if (!emitted.insert(
                                    key).second)
                            {
                                continue;
                            }

                            out
                                << PointerString(
                                    access.binding.resource)
                                << ","
                                << writer->second
                                << ","
                                << pass.id
                                << ","
                                << access.stage
                                << ","
                                << access.slot
                                << ",resource_lineage\n";
                        }

                        for (const auto& access :
                             pass.accesses)
                        {
                            if (access.mode == 'W' &&
                                access.binding.resource != 0)
                            {
                                lastWriter[
                                    access.binding.resource] =
                                    pass.id;
                            }
                        }
                    }
                }
            }

            {
                std::ofstream out(
                    file(
                        L"pipeline_summary",
                        L".txt"),
                    std::ios::out |
                        std::ios::trunc);

                if (out)
                {
                    std::uint64_t drawPasses = 0;
                    std::uint64_t dispatchPasses = 0;
                    std::uint64_t copyPasses = 0;
                    std::uint64_t clearPasses = 0;
                    std::uint64_t commandLists = 0;
                    std::set<std::uint64_t>
                        uniqueSignatures;

                    for (const auto& pass :
                         g_passes)
                    {
                        uniqueSignatures.insert(
                            PassSignatureLocked(
                                pass));

                        if (pass.kind ==
                            PassKind::Draw)
                        {
                            ++drawPasses;
                        }
                        else if (pass.kind ==
                            PassKind::Dispatch)
                        {
                            ++dispatchPasses;
                        }
                        else if (
                            pass.kind ==
                                PassKind::CopyResource ||
                            pass.kind ==
                                PassKind::CopySubresource ||
                            pass.kind ==
                                PassKind::Resolve ||
                            pass.kind ==
                                PassKind::GenerateMips)
                        {
                            ++copyPasses;
                        }
                        else if (
                            pass.kind ==
                                PassKind::ClearRtv ||
                            pass.kind ==
                                PassKind::ClearDsv ||
                            pass.kind ==
                                PassKind::ClearUavUint ||
                            pass.kind ==
                                PassKind::ClearUavFloat)
                        {
                            ++clearPasses;
                        }
                        else if (
                            pass.kind ==
                                PassKind::ExecuteCommandList)
                        {
                            ++commandLists;
                        }
                    }

                    out
                        << "GZFX pipeline cartography\n"
                        << "version="
                        << gzfx::kVersion
                        << "\n"
                        << "frames="
                        << g_startFrame
                        << "-"
                        << g_endFrame
                        << "\n"
                        << "passes="
                        << g_passes.size()
                        << "\n"
                        << "unique_pass_signatures="
                        << uniqueSignatures.size()
                        << "\n"
                        << "draw_passes="
                        << drawPasses
                        << "\n"
                        << "dispatch_passes="
                        << dispatchPasses
                        << "\n"
                        << "copy_resolve_mip_passes="
                        << copyPasses
                        << "\n"
                        << "clear_passes="
                        << clearPasses
                        << "\n"
                        << "opaque_command_lists="
                        << commandLists
                        << "\n"
                        << "resources_seen="
                        << g_resources.size()
                        << "\n"
                        << "shaders_seen="
                        << g_shaders.size()
                        << "\n"
                        << "deferred_contexts_created="
                        << g_deferredContextsCreated
                        << "\n"
                        << "known_scene_color="
                        << PointerString(
                            g_knownSceneColor)
                        << "\n"
                        << "known_main_depth="
                        << PointerString(
                            g_knownDepth)
                        << "\n"
                        << "known_motion="
                        << PointerString(
                            g_knownMotion)
                        << "\n"
                        << "known_normal="
                        << PointerString(
                            g_knownNormal)
                        << "\n"
                        << "known_aux="
                        << PointerString(
                            g_knownAux)
                        << "\n\n"
                        << "capture_scope:\n"
                        << "- all graphics shader stages (VS/HS/DS/GS/PS) and compute shader identity\n"
                        << "- stable FNV-1a shader bytecode fingerprint when creation was observed\n"
                        << "- SRV reads for all six shader stages\n"
                        << "- constant-buffer bindings for all six shader stages\n"
                        << "- sampler-state fingerprints for all six shader stages\n"
                        << "- RTV/DSV/graphics-UAV/compute-UAV writes at actual draw/dispatch execution\n"
                        << "- subresource-aware SRV/RTV/DSV/UAV view ranges\n"
                        << "- blend/depth/raster state fingerprints, viewport and primitive topology\n"
                        << "- CopyResource, CopySubresourceRegion, ResolveSubresource, clears and GenerateMips\n"
                        << "- producer-to-consumer resource lineage edges\n"
                        << "- stable pass signatures for cross-capture comparison\n"
                        << "- creation owner module for buffers/textures/shaders observed after device attach\n\n"
                        << "interpretation_rule:\n"
                        << "Candidate tags are hypotheses only. Use controlled day/night/water/interior/shadow captures and compare pass/resource lineage before promoting semantics.\n";

                    if (commandLists != 0)
                    {
                        out
                            << "\nwarning=ExecuteCommandList was observed. Work recorded inside a deferred command list may not be individually visible to the immediate-context hook.\n";
                    }
                }
            }
        }
    }

    bool AttachDevice(
        ID3D11Device* device) noexcept
    {
        try
        {
            if (device == nullptr)
            {
                return false;
            }

            std::scoped_lock lock(g_mutex);

            const auto key =
                reinterpret_cast<std::uintptr_t>(
                    device);

            if (g_devices.find(key) !=
                g_devices.end())
            {
                return true;
            }

            auto* original =
                *reinterpret_cast<
                    std::uintptr_t**>(
                        device);

            if (original == nullptr)
            {
                return false;
            }

            auto* replacement =
                static_cast<std::uintptr_t*>(
                    VirtualAlloc(
                        nullptr,
                        sizeof(std::uintptr_t) *
                            kDeviceVtableCount,
                        MEM_COMMIT |
                            MEM_RESERVE,
                        PAGE_READWRITE));

            if (replacement == nullptr)
            {
                return false;
            }

            std::memcpy(
                replacement,
                original,
                sizeof(std::uintptr_t) *
                    kDeviceVtableCount);

            replacement[kCreateBuffer] =
                reinterpret_cast<
                    std::uintptr_t>(
                        &HookCreateBuffer);

            replacement[kCreateTexture1D] =
                reinterpret_cast<
                    std::uintptr_t>(
                        &HookCreateTexture1D);

            replacement[kCreateTexture2D] =
                reinterpret_cast<
                    std::uintptr_t>(
                        &HookCreateTexture2D);

            replacement[kCreateTexture3D] =
                reinterpret_cast<
                    std::uintptr_t>(
                        &HookCreateTexture3D);

            replacement[kCreateVertexShader] =
                reinterpret_cast<
                    std::uintptr_t>(
                        &HookCreateVertexShader);

            replacement[kCreateGeometryShader] =
                reinterpret_cast<
                    std::uintptr_t>(
                        &HookCreateGeometryShader);

            replacement[
                kCreateGeometryShaderWithStreamOutput] =
                reinterpret_cast<
                    std::uintptr_t>(
                        &HookCreateGeometryShaderWithStreamOutput);

            replacement[kCreatePixelShader] =
                reinterpret_cast<
                    std::uintptr_t>(
                        &HookCreatePixelShader);

            replacement[kCreateHullShader] =
                reinterpret_cast<
                    std::uintptr_t>(
                        &HookCreateHullShader);

            replacement[kCreateDomainShader] =
                reinterpret_cast<
                    std::uintptr_t>(
                        &HookCreateDomainShader);

            replacement[kCreateComputeShader] =
                reinterpret_cast<
                    std::uintptr_t>(
                        &HookCreateComputeShader);

            replacement[kCreateDeferredContext] =
                reinterpret_cast<
                    std::uintptr_t>(
                        &HookCreateDeferredContext);

            DeviceHookState state{};
            state.originalVtable = original;
            state.replacementVtable =
                replacement;

            g_devices.emplace(
                key,
                state);

            InterlockedExchangePointer(
                reinterpret_cast<
                    PVOID volatile*>(
                        device),
                replacement);

            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    void BeginCapture(
        const std::uint64_t startFrame,
        const std::uint64_t endFrame,
        ID3D11DeviceContext* context,
        const std::uintptr_t knownSceneColor,
        const std::uintptr_t knownDepth,
        const std::uintptr_t knownMotion,
        const std::uintptr_t knownNormal,
        const std::uintptr_t knownAux) noexcept
    {
        try
        {
            {
                std::scoped_lock lock(
                    g_mutex);

                g_startFrame =
                    startFrame;
                g_endFrame =
                    endFrame;
                g_passId = 0;
                g_passes.clear();
                g_state =
                    CurrentState{};
                g_captureActive = true;

                g_knownSceneColor =
                    knownSceneColor;
                g_knownDepth =
                    knownDepth;
                g_knownMotion =
                    knownMotion;
                g_knownNormal =
                    knownNormal;
                g_knownAux =
                    knownAux;

                g_passes.reserve(
                    kMaxPasses);
            }

            SeedStateFromContext(
                context);
        }
        catch (...)
        {
        }
    }

    void FinishCapture(
        const std::wstring& suffix) noexcept
    {
        try
        {
            std::scoped_lock lock(
                g_mutex);

            if (!g_captureActive)
            {
                return;
            }

            WriteCaptureLocked(
                suffix);

            g_captureActive = false;
            g_passes.clear();
        }
        catch (...)
        {
        }
    }

    void SetShader(
        const ShaderStage stage,
        ID3D11DeviceChild* shader) noexcept
    {
        try
        {
            std::scoped_lock lock(
                g_mutex);

            if (!g_captureActive)
            {
                return;
            }

            g_state.shaders[
                StageIndex(stage)] =
                ShaderBindingLocked(
                    shader,
                    stage);
        }
        catch (...)
        {
        }
    }

    void SetShaderResources(
        const ShaderStage stage,
        const UINT startSlot,
        const UINT count,
        ID3D11ShaderResourceView* const* resources) noexcept
    {
        try
        {
            std::scoped_lock lock(
                g_mutex);

            if (!g_captureActive)
            {
                return;
            }

            auto& slots =
                g_state.srvs[
                    StageIndex(stage)];

            for (UINT i = 0;
                 i < count &&
                 startSlot + i <
                    slots.size();
                 ++i)
            {
                const auto binding =
                    DescribeSrvLocked(
                        resources
                            ? resources[i]
                            : nullptr);

                slots[startSlot + i] =
                    ConflictsWithOutputsLocked(
                        binding)
                    ? ViewBinding{}
                    : binding;
            }
        }
        catch (...)
        {
        }
    }

    void SetConstantBuffers(
        const ShaderStage stage,
        const UINT startSlot,
        const UINT count,
        ID3D11Buffer* const* buffers) noexcept
    {
        try
        {
            std::scoped_lock lock(
                g_mutex);

            if (!g_captureActive)
            {
                return;
            }

            auto& slots =
                g_state.cbs[
                    StageIndex(stage)];

            for (UINT i = 0;
                 i < count &&
                 startSlot + i <
                    slots.size();
                 ++i)
            {
                slots[startSlot + i] =
                    BufferBindingLocked(
                        buffers
                            ? buffers[i]
                            : nullptr);
            }
        }
        catch (...)
        {
        }
    }

    void SetSamplers(
        const ShaderStage stage,
        const UINT startSlot,
        const UINT count,
        ID3D11SamplerState* const* samplers) noexcept
    {
        try
        {
            std::scoped_lock lock(
                g_mutex);

            if (!g_captureActive)
            {
                return;
            }

            auto& slots =
                g_state.samplers[
                    StageIndex(stage)];

            for (UINT i = 0;
                 i < count &&
                 startSlot + i <
                    slots.size();
                 ++i)
            {
                slots[startSlot + i] =
                    SamplerBindingLocked(
                        samplers
                            ? samplers[i]
                            : nullptr);
            }
        }
        catch (...)
        {
        }
    }

    void SetRenderTargets(
        const UINT count,
        ID3D11RenderTargetView* const* renderTargets,
        ID3D11DepthStencilView* depthTarget) noexcept
    {
        try
        {
            std::scoped_lock lock(
                g_mutex);

            if (!g_captureActive)
            {
                return;
            }

            g_state.rtvs.fill(
                ViewBinding{});

            for (UINT i = 0;
                 i < count &&
                 i < g_state.rtvs.size();
                 ++i)
            {
                g_state.rtvs[i] =
                    DescribeRtvLocked(
                        renderTargets
                            ? renderTargets[i]
                            : nullptr);
            }

            g_state.dsv =
                DescribeDsvLocked(
                    depthTarget);

            for (const auto& output :
                 g_state.rtvs)
            {
                UnbindInputsForOutputLocked(
                    output);

                UnbindComputeUavsForOutputLocked(
                    output);
            }

            UnbindInputsForOutputLocked(
                g_state.dsv);

            UnbindComputeUavsForOutputLocked(
                g_state.dsv);

            g_state.omUavs.fill(
                ViewBinding{});
        }
        catch (...)
        {
        }
    }

    void SetRenderTargetsAndUavs(
        const UINT renderTargetCount,
        ID3D11RenderTargetView* const* renderTargets,
        ID3D11DepthStencilView* depthTarget,
        const UINT uavStartSlot,
        const UINT uavCount,
        ID3D11UnorderedAccessView* const* uavs) noexcept
    {
        try
        {
            std::scoped_lock lock(
                g_mutex);

            if (!g_captureActive)
            {
                return;
            }

            if (renderTargetCount !=
                D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL)
            {
                g_state.rtvs.fill(
                    ViewBinding{});

                for (UINT i = 0;
                     i < renderTargetCount &&
                     i < g_state.rtvs.size();
                     ++i)
                {
                    g_state.rtvs[i] =
                        DescribeRtvLocked(
                            renderTargets
                                ? renderTargets[i]
                                : nullptr);
                }

                g_state.dsv =
                    DescribeDsvLocked(
                        depthTarget);
            }

            if (renderTargetCount !=
                D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL)
            {
                for (const auto& output :
                     g_state.rtvs)
                {
                    UnbindInputsForOutputLocked(
                        output);

                    UnbindComputeUavsForOutputLocked(
                        output);
                }

                UnbindInputsForOutputLocked(
                    g_state.dsv);

                UnbindComputeUavsForOutputLocked(
                    g_state.dsv);
            }

            if (uavCount !=
                D3D11_KEEP_UNORDERED_ACCESS_VIEWS)
            {
                for (UINT i = 0;
                     i < uavCount &&
                     uavStartSlot + i <
                        g_state.omUavs.size();
                     ++i)
                {
                    const auto binding =
                        DescribeUavLocked(
                            uavs
                                ? uavs[i]
                                : nullptr);

                    UnbindInputsForOutputLocked(
                        binding);

                    UnbindComputeUavsForOutputLocked(
                        binding);

                    if (renderTargetCount ==
                        D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL)
                    {
                        UnbindOmOutputsForUavLocked(
                            binding,
                            uavStartSlot);
                    }

                    g_state.omUavs[
                        uavStartSlot + i] =
                        binding;
                }
            }
        }
        catch (...)
        {
        }
    }

    void SetComputeUavs(
        const UINT startSlot,
        const UINT count,
        ID3D11UnorderedAccessView* const* uavs) noexcept
    {
        try
        {
            std::scoped_lock lock(
                g_mutex);

            if (!g_captureActive)
            {
                return;
            }

            for (UINT i = 0;
                 i < count &&
                 startSlot + i <
                    g_state.csUavs.size();
                 ++i)
            {
                const auto binding =
                    DescribeUavLocked(
                        uavs
                            ? uavs[i]
                            : nullptr);

                UnbindInputsForOutputLocked(
                    binding);

                g_state.csUavs[
                    startSlot + i] =
                    binding;
            }
        }
        catch (...)
        {
        }
    }

    void SetBlendState(
        ID3D11BlendState* state,
        const FLOAT blendFactor[4],
        const UINT sampleMask) noexcept
    {
        try
        {
            std::scoped_lock lock(
                g_mutex);

            if (!g_captureActive)
            {
                return;
            }

            g_state.blendState =
                CanonicalId(state);

            g_state.blendHash =
                StateHash<
                    ID3D11BlendState,
                    D3D11_BLEND_DESC>(
                        state);

            if (blendFactor != nullptr)
            {
                std::copy(
                    blendFactor,
                    blendFactor + 4,
                    g_state.blendFactor);
            }
            else
            {
                std::fill(
                    std::begin(
                        g_state.blendFactor),
                    std::end(
                        g_state.blendFactor),
                    1.0f);
            }

            g_state.sampleMask =
                sampleMask;
        }
        catch (...)
        {
        }
    }

    void SetDepthStencilState(
        ID3D11DepthStencilState* state,
        const UINT stencilRef) noexcept
    {
        try
        {
            std::scoped_lock lock(
                g_mutex);

            if (!g_captureActive)
            {
                return;
            }

            g_state.depthState =
                CanonicalId(state);

            g_state.depthHash =
                StateHash<
                    ID3D11DepthStencilState,
                    D3D11_DEPTH_STENCIL_DESC>(
                        state);

            g_state.stencilRef =
                stencilRef;

            if (state == nullptr)
            {
                g_state.depthWriteEnabled =
                    true;
            }
            else
            {
                D3D11_DEPTH_STENCIL_DESC
                    description{};

                state->GetDesc(
                    &description);

                g_state.depthWriteEnabled =
                    description.DepthEnable &&
                    description.DepthWriteMask ==
                        D3D11_DEPTH_WRITE_MASK_ALL;
            }
        }
        catch (...)
        {
        }
    }

    void SetRasterizerState(
        ID3D11RasterizerState* state) noexcept
    {
        try
        {
            std::scoped_lock lock(
                g_mutex);

            if (!g_captureActive)
            {
                return;
            }

            g_state.rasterState =
                CanonicalId(state);

            g_state.rasterHash =
                StateHash<
                    ID3D11RasterizerState,
                    D3D11_RASTERIZER_DESC>(
                        state);
        }
        catch (...)
        {
        }
    }

    void SetPrimitiveTopology(
        const D3D11_PRIMITIVE_TOPOLOGY topology) noexcept
    {
        try
        {
            std::scoped_lock lock(
                g_mutex);

            if (g_captureActive)
            {
                g_state.topology =
                    topology;
            }
        }
        catch (...)
        {
        }
    }

    void SetViewports(
        const UINT count,
        const D3D11_VIEWPORT* viewports) noexcept
    {
        try
        {
            std::scoped_lock lock(
                g_mutex);

            if (!g_captureActive)
            {
                return;
            }

            g_state.viewportCount =
                count;

            if (count != 0 &&
                viewports != nullptr)
            {
                g_state.viewportWidth =
                    static_cast<UINT>(
                        viewports[0].Width);

                g_state.viewportHeight =
                    static_cast<UINT>(
                        viewports[0].Height);
            }
            else
            {
                g_state.viewportWidth = 0;
                g_state.viewportHeight = 0;
            }
        }
        catch (...)
        {
        }
    }

    void ClearState() noexcept
    {
        try
        {
            std::scoped_lock lock(
                g_mutex);

            if (g_captureActive)
            {
                g_state =
                    CurrentState{};
            }
        }
        catch (...)
        {
        }
    }

    void ExecuteDraw(
        const std::uint64_t frame,
        const std::uint64_t drawOrdinal,
        const DrawKind kind,
        const std::uint64_t a,
        const std::uint64_t b,
        const std::uint64_t c,
        const std::uint64_t d,
        const std::uint64_t e,
        ID3D11Buffer* indirectArgs,
        const UINT indirectOffset) noexcept
    {
        try
        {
            std::scoped_lock lock(
                g_mutex);

            if (!CaptureFrame(frame))
            {
                return;
            }

            auto& pass =
                AddPassLocked(
                    frame,
                    drawOrdinal,
                    PassKind::Draw);

            pass.drawKind = kind;
            pass.arg0 = a;
            pass.arg1 = b;
            pass.arg2 = c;
            pass.arg3 = d;
            pass.arg4 = e;
            pass.arg5 =
                indirectOffset;

            AddShaderReadsLocked(
                pass,
                false);

            if (indirectArgs != nullptr)
            {
                AddAccess(
                    pass,
                    'R',
                    'A',
                    0,
                    ResourceBindingLocked(
                        indirectArgs));
            }

            AddDrawWritesLocked(
                pass);
        }
        catch (...)
        {
        }
    }

    void ExecuteDispatch(
        const std::uint64_t frame,
        const std::uint64_t drawOrdinal,
        const UINT x,
        const UINT y,
        const UINT z,
        ID3D11Buffer* indirectArgs,
        const UINT indirectOffset) noexcept
    {
        try
        {
            std::scoped_lock lock(
                g_mutex);

            if (!CaptureFrame(frame))
            {
                return;
            }

            auto& pass =
                AddPassLocked(
                    frame,
                    drawOrdinal,
                    PassKind::Dispatch);

            pass.arg0 = x;
            pass.arg1 = y;
            pass.arg2 = z;
            pass.arg3 =
                indirectArgs
                    ? RegisterResourceLocked(
                        indirectArgs)
                    : 0;
            pass.arg4 =
                indirectOffset;

            AddShaderReadsLocked(
                pass,
                true);

            if (indirectArgs != nullptr)
            {
                AddAccess(
                    pass,
                    'R',
                    'A',
                    0,
                    ResourceBindingLocked(
                        indirectArgs));
            }

            AddDispatchWritesLocked(
                pass);
        }
        catch (...)
        {
        }
    }

    void CopyResource(
        const std::uint64_t frame,
        const std::uint64_t drawOrdinal,
        ID3D11Resource* destination,
        ID3D11Resource* source) noexcept
    {
        try
        {
            std::scoped_lock lock(
                g_mutex);

            if (!CaptureFrame(frame))
            {
                return;
            }

            auto& pass =
                AddPassLocked(
                    frame,
                    drawOrdinal,
                    PassKind::CopyResource);

            AddAccess(
                pass,
                'R',
                'X',
                0,
                ResourceBindingLocked(
                    source));

            AddAccess(
                pass,
                'W',
                'X',
                0,
                ResourceBindingLocked(
                    destination));
        }
        catch (...)
        {
        }
    }

    void CopySubresourceRegion(
        const std::uint64_t frame,
        const std::uint64_t drawOrdinal,
        ID3D11Resource* destination,
        const UINT destinationSubresource,
        const UINT destinationX,
        const UINT destinationY,
        const UINT destinationZ,
        ID3D11Resource* source,
        const UINT sourceSubresource,
        const D3D11_BOX* sourceBox) noexcept
    {
        try
        {
            std::scoped_lock lock(
                g_mutex);

            if (!CaptureFrame(frame))
            {
                return;
            }

            auto& pass =
                AddPassLocked(
                    frame,
                    drawOrdinal,
                    PassKind::CopySubresource);

            pass.arg0 =
                destinationSubresource;
            pass.arg1 =
                sourceSubresource;
            pass.arg2 =
                destinationX;
            pass.arg3 =
                destinationY;
            pass.arg4 =
                destinationZ;
            pass.arg5 =
                sourceBox != nullptr
                    ? 1
                    : 0;

            AddAccess(
                pass,
                'R',
                'X',
                sourceSubresource,
                ResourceBindingLocked(
                    source,
                    static_cast<INT>(
                        sourceSubresource)));

            AddAccess(
                pass,
                'W',
                'X',
                destinationSubresource,
                ResourceBindingLocked(
                    destination,
                    static_cast<INT>(
                        destinationSubresource)));
        }
        catch (...)
        {
        }
    }

    void ResolveSubresource(
        const std::uint64_t frame,
        const std::uint64_t drawOrdinal,
        ID3D11Resource* destination,
        const UINT destinationSubresource,
        ID3D11Resource* source,
        const UINT sourceSubresource,
        const DXGI_FORMAT format) noexcept
    {
        try
        {
            std::scoped_lock lock(
                g_mutex);

            if (!CaptureFrame(frame))
            {
                return;
            }

            auto& pass =
                AddPassLocked(
                    frame,
                    drawOrdinal,
                    PassKind::Resolve);

            pass.arg0 =
                destinationSubresource;
            pass.arg1 =
                sourceSubresource;
            pass.arg2 =
                static_cast<std::uint64_t>(
                    format);

            AddAccess(
                pass,
                'R',
                'X',
                sourceSubresource,
                ResourceBindingLocked(
                    source,
                    static_cast<INT>(
                        sourceSubresource)));

            AddAccess(
                pass,
                'W',
                'X',
                destinationSubresource,
                ResourceBindingLocked(
                    destination,
                    static_cast<INT>(
                        destinationSubresource)));
        }
        catch (...)
        {
        }
    }

    void ClearRenderTarget(
        const std::uint64_t frame,
        const std::uint64_t drawOrdinal,
        ID3D11RenderTargetView* view,
        const FLOAT color[4]) noexcept
    {
        try
        {
            std::scoped_lock lock(
                g_mutex);

            if (!CaptureFrame(frame))
            {
                return;
            }

            auto& pass =
                AddPassLocked(
                    frame,
                    drawOrdinal,
                    PassKind::ClearRtv);

            AddAccess(
                pass,
                'W',
                'O',
                0,
                DescribeRtvLocked(
                    view));

            if (color != nullptr)
            {
                std::uint32_t bits = 0;
                std::memcpy(
                    &bits,
                    &color[0],
                    sizeof(bits));
                pass.arg0 = bits;

                std::memcpy(
                    &bits,
                    &color[1],
                    sizeof(bits));
                pass.arg1 = bits;

                std::memcpy(
                    &bits,
                    &color[2],
                    sizeof(bits));
                pass.arg2 = bits;

                std::memcpy(
                    &bits,
                    &color[3],
                    sizeof(bits));
                pass.arg3 = bits;
            }
        }
        catch (...)
        {
        }
    }

    void ClearDepthStencil(
        const std::uint64_t frame,
        const std::uint64_t drawOrdinal,
        ID3D11DepthStencilView* view,
        const UINT flags,
        const FLOAT depth,
        const UINT8 stencil) noexcept
    {
        try
        {
            std::scoped_lock lock(
                g_mutex);

            if (!CaptureFrame(frame))
            {
                return;
            }

            auto& pass =
                AddPassLocked(
                    frame,
                    drawOrdinal,
                    PassKind::ClearDsv);

            AddAccess(
                pass,
                'W',
                'Z',
                0,
                DescribeDsvLocked(
                    view));

            pass.arg0 = flags;

            std::uint32_t depthBits = 0;
            std::memcpy(
                &depthBits,
                &depth,
                sizeof(depthBits));

            pass.arg1 =
                depthBits;
            pass.arg2 =
                stencil;
        }
        catch (...)
        {
        }
    }

    void ClearUavUint(
        const std::uint64_t frame,
        const std::uint64_t drawOrdinal,
        ID3D11UnorderedAccessView* view,
        const UINT values[4]) noexcept
    {
        try
        {
            std::scoped_lock lock(
                g_mutex);

            if (!CaptureFrame(frame))
            {
                return;
            }

            auto& pass =
                AddPassLocked(
                    frame,
                    drawOrdinal,
                    PassKind::ClearUavUint);

            AddAccess(
                pass,
                'W',
                'U',
                0,
                DescribeUavLocked(
                    view));

            if (values != nullptr)
            {
                pass.arg0 = values[0];
                pass.arg1 = values[1];
                pass.arg2 = values[2];
                pass.arg3 = values[3];
            }
        }
        catch (...)
        {
        }
    }

    void ClearUavFloat(
        const std::uint64_t frame,
        const std::uint64_t drawOrdinal,
        ID3D11UnorderedAccessView* view,
        const FLOAT values[4]) noexcept
    {
        try
        {
            std::scoped_lock lock(
                g_mutex);

            if (!CaptureFrame(frame))
            {
                return;
            }

            auto& pass =
                AddPassLocked(
                    frame,
                    drawOrdinal,
                    PassKind::ClearUavFloat);

            AddAccess(
                pass,
                'W',
                'U',
                0,
                DescribeUavLocked(
                    view));

            if (values != nullptr)
            {
                for (std::size_t i = 0;
                     i < 4;
                     ++i)
                {
                    std::uint32_t bits = 0;
                    std::memcpy(
                        &bits,
                        &values[i],
                        sizeof(bits));

                    if (i == 0) pass.arg0 = bits;
                    if (i == 1) pass.arg1 = bits;
                    if (i == 2) pass.arg2 = bits;
                    if (i == 3) pass.arg3 = bits;
                }
            }
        }
        catch (...)
        {
        }
    }

    void GenerateMips(
        const std::uint64_t frame,
        const std::uint64_t drawOrdinal,
        ID3D11ShaderResourceView* view) noexcept
    {
        try
        {
            std::scoped_lock lock(
                g_mutex);

            if (!CaptureFrame(frame))
            {
                return;
            }

            auto& pass =
                AddPassLocked(
                    frame,
                    drawOrdinal,
                    PassKind::GenerateMips);

            const auto binding =
                DescribeSrvLocked(
                    view);

            AddAccess(
                pass,
                'R',
                'M',
                0,
                binding);

            AddAccess(
                pass,
                'W',
                'M',
                0,
                binding);
        }
        catch (...)
        {
        }
    }

    void ExecuteCommandList(
        const std::uint64_t frame,
        const std::uint64_t drawOrdinal,
        ID3D11CommandList* commandList,
        const BOOL restoreContextState) noexcept
    {
        try
        {
            std::scoped_lock lock(
                g_mutex);

            if (!CaptureFrame(frame))
            {
                return;
            }

            auto& pass =
                AddPassLocked(
                    frame,
                    drawOrdinal,
                    PassKind::ExecuteCommandList);

            pass.arg0 =
                CanonicalId(
                    commandList);
            pass.arg1 =
                restoreContextState
                    ? 1
                    : 0;
        }
        catch (...)
        {
        }
    }
}

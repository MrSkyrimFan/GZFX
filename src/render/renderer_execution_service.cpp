#include "gzfx/render/renderer_execution_service.hpp"

#include <algorithm>
#include <mutex>
#include <unordered_map>

namespace gzfx::render::execution
{
    namespace
    {
        struct OwnedTag
        {
            OwnedPass pass = OwnedPass::Unknown;
            std::uint64_t frame = 0;
            std::uint64_t sequence = 0;
        };

        std::mutex g_mutex;
        ID3D11Device* g_device = nullptr;
        ID3D11DeviceContext* g_immediate = nullptr;
        ID3D11DeviceContext1* g_immediate1 = nullptr;
        IDXGISwapChain* g_swapChain = nullptr;

        std::uint64_t g_deviceGeneration = 0;
        std::uint64_t g_surfaceGeneration = 0;
        std::uint64_t g_tagSequence = 0;
        UINT g_width = 0;
        UINT g_height = 0;

        std::unordered_map<std::uintptr_t, OwnedTag> g_owned;

        std::uintptr_t Identity(IUnknown* value) noexcept
        {
            if (value == nullptr)
            {
                return 0;
            }

            IUnknown* identity = nullptr;

            if (FAILED(
                    value->QueryInterface(
                        __uuidof(IUnknown),
                        reinterpret_cast<void**>(&identity))) ||
                identity == nullptr)
            {
                return reinterpret_cast<std::uintptr_t>(value);
            }

            const auto result =
                reinterpret_cast<std::uintptr_t>(identity);

            identity->Release();
            return result;
        }

        void ReleasePrimaryLocked() noexcept
        {
            if (g_immediate1 != nullptr)
            {
                g_immediate1->Release();
                g_immediate1 = nullptr;
            }

            if (g_immediate != nullptr)
            {
                g_immediate->Release();
                g_immediate = nullptr;
            }

            if (g_swapChain != nullptr)
            {
                g_swapChain->Release();
                g_swapChain = nullptr;
            }

            if (g_device != nullptr)
            {
                g_device->Release();
                g_device = nullptr;
            }

            g_width = 0;
            g_height = 0;
            g_owned.clear();
        }

        void UpdateSurfaceLocked(
            IDXGISwapChain* swapChain) noexcept
        {
            if (swapChain == nullptr)
            {
                return;
            }

            DXGI_SWAP_CHAIN_DESC description{};

            if (FAILED(swapChain->GetDesc(&description)))
            {
                return;
            }

            const UINT width =
                description.BufferDesc.Width;

            const UINT height =
                description.BufferDesc.Height;

            if (width != 0 &&
                height != 0 &&
                (g_width != width ||
                 g_height != height))
            {
                g_width = width;
                g_height = height;
                ++g_surfaceGeneration;
            }
        }

        void TrimTagsLocked() noexcept
        {
            constexpr std::size_t kMaxTags = 64;

            if (g_owned.size() <= kMaxTags)
            {
                return;
            }

            while (g_owned.size() > kMaxTags)
            {
                auto oldest = g_owned.end();

                for (auto it = g_owned.begin();
                     it != g_owned.end();
                     ++it)
                {
                    if (oldest == g_owned.end() ||
                        it->second.sequence <
                            oldest->second.sequence)
                    {
                        oldest = it;
                    }
                }

                if (oldest == g_owned.end())
                {
                    break;
                }

                g_owned.erase(oldest);
            }
        }
    }

    bool AttachPrimary(
        IDXGISwapChain* const swapChain,
        ID3D11Device* const device,
        ID3D11DeviceContext* const immediate) noexcept
    {
        try
        {
            if (device == nullptr ||
                immediate == nullptr)
            {
                return false;
            }

            std::scoped_lock lock(g_mutex);

            const bool deviceChanged =
                g_device == nullptr ||
                Identity(g_device) != Identity(device);

            if (deviceChanged)
            {
                ReleasePrimaryLocked();

                g_device = device;
                g_device->AddRef();

                g_immediate = immediate;
                g_immediate->AddRef();

                immediate->QueryInterface(
                    __uuidof(ID3D11DeviceContext1),
                    reinterpret_cast<void**>(&g_immediate1));

                ++g_deviceGeneration;
                ++g_surfaceGeneration;
            }
            else if (g_immediate == nullptr)
            {
                g_immediate = immediate;
                g_immediate->AddRef();

                immediate->QueryInterface(
                    __uuidof(ID3D11DeviceContext1),
                    reinterpret_cast<void**>(&g_immediate1));
            }

            if (swapChain != nullptr &&
                (g_swapChain == nullptr ||
                 Identity(g_swapChain) !=
                    Identity(swapChain)))
            {
                if (g_swapChain != nullptr)
                {
                    g_swapChain->Release();
                }

                g_swapChain = swapChain;
                g_swapChain->AddRef();

                ++g_surfaceGeneration;
            }

            UpdateSurfaceLocked(g_swapChain);
            return g_device != nullptr &&
                   g_immediate != nullptr;
        }
        catch (...)
        {
            return false;
        }
    }

    bool Acquire(
        ExecutionLease* const outLease) noexcept
    {
        if (outLease == nullptr)
        {
            return false;
        }

        *outLease = {};

        try
        {
            std::scoped_lock lock(g_mutex);

            if (g_device == nullptr ||
                g_immediate == nullptr)
            {
                return false;
            }

            UpdateSurfaceLocked(g_swapChain);

            outLease->device = g_device;
            outLease->device->AddRef();

            outLease->immediate = g_immediate;
            outLease->immediate->AddRef();

            if (g_immediate1 != nullptr)
            {
                outLease->immediate1 = g_immediate1;
                outLease->immediate1->AddRef();
            }

            if (g_swapChain != nullptr)
            {
                outLease->swapChain = g_swapChain;
                outLease->swapChain->AddRef();
            }

            outLease->deviceGeneration =
                g_deviceGeneration;

            outLease->surfaceGeneration =
                g_surfaceGeneration;

            outLease->width = g_width;
            outLease->height = g_height;
            outLease->valid = true;

            return true;
        }
        catch (...)
        {
            Release(*outLease);
            return false;
        }
    }

    void Release(
        ExecutionLease& lease) noexcept
    {
        if (lease.immediate1 != nullptr)
        {
            lease.immediate1->Release();
        }

        if (lease.immediate != nullptr)
        {
            lease.immediate->Release();
        }

        if (lease.swapChain != nullptr)
        {
            lease.swapChain->Release();
        }

        if (lease.device != nullptr)
        {
            lease.device->Release();
        }

        lease = {};
    }

    std::uint64_t DeviceGeneration() noexcept
    {
        try
        {
            std::scoped_lock lock(g_mutex);
            return g_deviceGeneration;
        }
        catch (...)
        {
            return 0;
        }
    }

    std::uint64_t SurfaceGeneration() noexcept
    {
        try
        {
            std::scoped_lock lock(g_mutex);
            return g_surfaceGeneration;
        }
        catch (...)
        {
            return 0;
        }
    }

    void TagCommandList(
        ID3D11CommandList* const commandList,
        const OwnedPass pass,
        const std::uint64_t frame) noexcept
    {
        try
        {
            if (commandList == nullptr)
            {
                return;
            }

            std::scoped_lock lock(g_mutex);

            OwnedTag tag{};
            tag.pass = pass;
            tag.frame = frame;
            tag.sequence = ++g_tagSequence;

            g_owned[
                reinterpret_cast<std::uintptr_t>(
                    commandList)] = tag;

            TrimTagsLocked();
        }
        catch (...)
        {
        }
    }

    bool QueryOwnedCommandList(
        ID3D11CommandList* const commandList,
        OwnedPass* const outPass,
        std::uint64_t* const outFrame) noexcept
    {
        try
        {
            if (commandList == nullptr)
            {
                return false;
            }

            std::scoped_lock lock(g_mutex);

            const auto found =
                g_owned.find(
                    reinterpret_cast<std::uintptr_t>(
                        commandList));

            if (found == g_owned.end())
            {
                return false;
            }

            if (outPass != nullptr)
            {
                *outPass = found->second.pass;
            }

            if (outFrame != nullptr)
            {
                *outFrame = found->second.frame;
            }

            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    void RetireCommandList(
        ID3D11CommandList* const commandList) noexcept
    {
        try
        {
            if (commandList == nullptr)
            {
                return;
            }

            std::scoped_lock lock(g_mutex);

            g_owned.erase(
                reinterpret_cast<std::uintptr_t>(
                    commandList));
        }
        catch (...)
        {
        }
    }

    bool ExecuteOwned(
        const ExecutionLease& lease,
        ID3D11CommandList* const commandList,
        const OwnedPass pass,
        const std::uint64_t frame,
        const bool restoreContextState) noexcept
    {
        try
        {
            if (!lease.valid ||
                commandList == nullptr ||
                lease.immediate == nullptr)
            {
                return false;
            }

            TagCommandList(
                commandList,
                pass,
                frame);

            // Prefer the D3D11.1 interface when available. Renderer forensics
            // replaces the base ID3D11DeviceContext vtable for observation;
            // the separately queried Context1 interface gives GZFX an isolated
            // execution lane so its own deferred work is not mistaken for an
            // opaque Skyrim command list. The tag remains as explicit ownership
            // proof and for any future observer that elects to consume it.
            if (lease.immediate1 != nullptr)
            {
                lease.immediate1->ExecuteCommandList(
                    commandList,
                    restoreContextState ? TRUE : FALSE);
            }
            else
            {
                lease.immediate->ExecuteCommandList(
                    commandList,
                    restoreContextState ? TRUE : FALSE);
            }

            RetireCommandList(commandList);
            return true;
        }
        catch (...)
        {
            RetireCommandList(commandList);
            return false;
        }
    }

    const char* OwnedPassName(
        const OwnedPass pass) noexcept
    {
        switch (pass)
        {
        case OwnedPass::Wave3DepthHierarchy:
            return "wave3_depth_hierarchy";
        case OwnedPass::Wave3TemporalGeometry:
            return "wave3_temporal_geometry";
        case OwnedPass::Wave3Diagnostics:
            return "wave3_diagnostics";
        case OwnedPass::Wave4AmbientVisibility:
            return "wave4_ambient_visibility";
        case OwnedPass::Wave4AmbientInjection:
            return "wave4_ambient_injection";
        case OwnedPass::VisiblePass:
            return "visible_pass";
        case OwnedPass::ResourceDiagnostic:
            return "resource_diagnostic";
        case OwnedPass::SemanticDiagnostic:
            return "semantic_diagnostic";
        case OwnedPass::Unknown:
        case OwnedPass::Count:
        default:
            return "unknown";
        }
    }
}
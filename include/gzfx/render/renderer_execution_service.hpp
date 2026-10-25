#pragma once

#include <d3d11.h>
#include <d3d11_1.h>
#include <dxgi.h>

#include <cstdint>

namespace gzfx::render::execution
{
    enum class OwnedPass : std::uint32_t
    {
        Unknown = 0,
        Wave3DepthHierarchy,
        Wave3TemporalGeometry,
        Wave3Diagnostics,
        Wave4AmbientVisibility,
        Wave4AmbientInjection,
        VisiblePass,
        ResourceDiagnostic,
        SemanticDiagnostic,
        Count
    };

    struct ExecutionLease
    {
        ID3D11Device* device = nullptr;
        ID3D11DeviceContext* immediate = nullptr;
        ID3D11DeviceContext1* immediate1 = nullptr;
        IDXGISwapChain* swapChain = nullptr;

        std::uint64_t deviceGeneration = 0;
        std::uint64_t surfaceGeneration = 0;
        UINT width = 0;
        UINT height = 0;
        bool valid = false;
    };

    bool AttachPrimary(
        IDXGISwapChain* swapChain,
        ID3D11Device* device,
        ID3D11DeviceContext* immediate) noexcept;

    bool Acquire(
        ExecutionLease* outLease) noexcept;

    void Release(
        ExecutionLease& lease) noexcept;

    std::uint64_t DeviceGeneration() noexcept;
    std::uint64_t SurfaceGeneration() noexcept;

    void TagCommandList(
        ID3D11CommandList* commandList,
        OwnedPass pass,
        std::uint64_t frame) noexcept;

    bool QueryOwnedCommandList(
        ID3D11CommandList* commandList,
        OwnedPass* outPass,
        std::uint64_t* outFrame) noexcept;

    void RetireCommandList(
        ID3D11CommandList* commandList) noexcept;

    bool ExecuteOwned(
        const ExecutionLease& lease,
        ID3D11CommandList* commandList,
        OwnedPass pass,
        std::uint64_t frame,
        bool restoreContextState = true) noexcept;

    const char* OwnedPassName(
        OwnedPass pass) noexcept;
}
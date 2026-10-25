#pragma once

#include <d3d11.h>

#include <cstdint>

namespace gzfx::diagnostics::deferred_context_cartography
{
    struct Status
    {
        std::uint64_t contextsAttached = 0;
        std::uint64_t listsFinished = 0;
        std::uint64_t listsExecuted = 0;
        std::uint64_t opaqueExecutions = 0;
        std::uint64_t recordedRuns = 0;
        std::uint64_t recordOverflows = 0;
        std::uint64_t registryEvictions = 0;
    };

    bool AttachContext(
        ID3D11DeviceContext* context) noexcept;

    bool ExecuteObservedCommandList(
        std::uint64_t frame,
        std::uint64_t executeOrdinal,
        ID3D11CommandList* commandList,
        bool restoreContextState) noexcept;

    void Reset() noexcept;

    Status GetStatus() noexcept;
}

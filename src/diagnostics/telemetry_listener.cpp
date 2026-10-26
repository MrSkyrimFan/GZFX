#include "gzfx/diagnostics/telemetry_listener.hpp"

#include "gzfx/diagnostics/pipeline_cartography.hpp"
#include "gzfx/diagnostics/deferred_context_cartography.hpp"
#include "gzfx/diagnostics/engine_epochs.hpp"
#include "gzfx/diagnostics/renderer_forensics.hpp"
#include "gzfx/paths.hpp"
#include "gzfx/render/ambient_visibility_v2.hpp"
#include "gzfx/render/runtime_state.hpp"
#include "gzfx/render/semantic_authority.hpp"
#include "gzfx/render/stage_scheduler.hpp"
#include "gzfx/render/wave3_pipeline.hpp"
#include "gzfx/version.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

namespace gzfx::diagnostics::telemetry_listener
{
    namespace fs = std::filesystem;

    namespace
    {
        enum class EventType : std::uint32_t
        {
            Frame = 0,
            Stage,
            PipelineRun,
            Resource,
            Semantic,
            Transition,
            Ownership,
            Timing,
            DeferredList,
            DeferredRun,
            PresentTimeline,
            EngineEvent
        };

        enum class TransitionKind : std::uint32_t
        {
            ListenerStart = 0,
            ListenerStop,
            ModeChange,
            EnvironmentChange,
            ResolutionChange,
            DeviceGenerationChange,
            SurfaceGenerationChange,
            Wave3HistoryGenerationChange,
            Wave4HistoryRestart,
            FrameGap,
            Resize,
            QueueOverflowBegin,
            QueueOverflowEnd
        };

        struct Event
        {
            EventType type = EventType::Frame;
            std::uint32_t subtype = 0;
            std::uint64_t sequence = 0;
            std::uint64_t qpc = 0;
            std::uint64_t frame = 0;
            std::uint64_t ordinal = 0;
            std::array<std::uint64_t, 96> u{};
            std::array<double, 4> d{};
        };

        struct Slot
        {
            std::atomic<std::size_t> sequence{0};
            Event event{};
        };

        class Ring
        {
        public:
            bool Initialize(const std::size_t capacity)
            {
                if (capacity < 64 ||
                    (capacity & (capacity - 1)) != 0)
                {
                    return false;
                }

                m_slots =
                    std::make_unique<Slot[]>(
                        capacity);

                if (!m_slots)
                {
                    return false;
                }

                m_capacity = capacity;
                m_mask = capacity - 1;

                for (std::size_t i = 0;
                     i < capacity;
                     ++i)
                {
                    m_slots[i].sequence.store(
                        i,
                        std::memory_order_relaxed);
                }

                m_enqueue.store(
                    0,
                    std::memory_order_relaxed);

                m_dequeue.store(
                    0,
                    std::memory_order_relaxed);

                return true;
            }

            bool TryEnqueue(
                const Event& event) noexcept
            {
                std::size_t position =
                    m_enqueue.load(
                        std::memory_order_relaxed);

                for (;;)
                {
                    Slot& slot =
                        m_slots[
                            position &
                            m_mask];

                    const std::size_t sequence =
                        slot.sequence.load(
                            std::memory_order_acquire);

                    const auto difference =
                        static_cast<std::intptr_t>(
                            sequence) -
                        static_cast<std::intptr_t>(
                            position);

                    if (difference == 0)
                    {
                        if (m_enqueue.compare_exchange_weak(
                                position,
                                position + 1,
                                std::memory_order_relaxed))
                        {
                            slot.event = event;

                            slot.sequence.store(
                                position + 1,
                                std::memory_order_release);

                            return true;
                        }
                    }
                    else if (difference < 0)
                    {
                        return false;
                    }
                    else
                    {
                        position =
                            m_enqueue.load(
                                std::memory_order_relaxed);
                    }
                }
            }

            bool TryDequeue(
                Event& event) noexcept
            {
                std::size_t position =
                    m_dequeue.load(
                        std::memory_order_relaxed);

                for (;;)
                {
                    Slot& slot =
                        m_slots[
                            position &
                            m_mask];

                    const std::size_t sequence =
                        slot.sequence.load(
                            std::memory_order_acquire);

                    const auto difference =
                        static_cast<std::intptr_t>(
                            sequence) -
                        static_cast<std::intptr_t>(
                            position + 1);

                    if (difference == 0)
                    {
                        if (m_dequeue.compare_exchange_weak(
                                position,
                                position + 1,
                                std::memory_order_relaxed))
                        {
                            event = slot.event;

                            slot.sequence.store(
                                position +
                                    m_capacity,
                                std::memory_order_release);

                            return true;
                        }
                    }
                    else if (difference < 0)
                    {
                        return false;
                    }
                    else
                    {
                        position =
                            m_dequeue.load(
                                std::memory_order_relaxed);
                    }
                }
            }

            std::size_t ApproxSize() const noexcept
            {
                const auto write =
                    m_enqueue.load(
                        std::memory_order_relaxed);

                const auto read =
                    m_dequeue.load(
                        std::memory_order_relaxed);

                return
                    write >= read
                        ? write - read
                        : 0;
            }

            std::size_t Capacity() const noexcept
            {
                return m_capacity;
            }

        private:
            std::unique_ptr<Slot[]> m_slots{};
            std::size_t m_capacity = 0;
            std::size_t m_mask = 0;
            std::atomic<std::size_t> m_enqueue{0};
            std::atomic<std::size_t> m_dequeue{0};
        };

        struct PendingPassRun
        {
            bool active = false;
            std::uint64_t signature = 0;
            pipeline_cartography::PassTelemetry pass{};
            std::uint64_t firstOrdinal = 0;
            std::uint64_t lastOrdinal = 0;
            std::uint64_t repeatCount = 0;
        };

        struct WriterFiles
        {
            std::ofstream frames;
            std::ofstream seconds;
            std::ofstream stages;
            std::ofstream semantics;
            std::ofstream resources;
            std::ofstream transitions;
            std::ofstream ownership;
            std::ofstream timings;
            std::ofstream deferredLists;
            std::ofstream pipeline;
            std::ofstream deferredRuns;
            std::ofstream presentTimeline;
            std::ofstream engineEvents;
            std::uint32_t pipelineSegment = 0;
            std::uint64_t pipelineBytes = 0;
            std::uint32_t deferredRunSegment = 0;
            std::uint64_t deferredRunBytes = 0;
        };

        struct SecondAccumulator
        {
            bool active = false;
            std::uint64_t startQpc = 0;
            std::uint64_t firstFrame = 0;
            std::uint64_t lastFrame = 0;
            std::uint64_t frameCount = 0;
            double frameMsSum = 0.0;
            double frameMsMin =
                std::numeric_limits<double>::max();
            double frameMsMax = 0.0;
            std::uint64_t mode = 0;
            std::uint64_t environment = 0;
            std::uint64_t pipelineRuns = 0;
            std::uint64_t stageEvents = 0;
            std::uint64_t ownedPasses = 0;
            std::uint64_t externalCommandLists = 0;
        };

        constexpr std::uint64_t kPipelineSegmentBytes =
            64ull * 1024ull * 1024ull;

        Ring g_ring{};
        std::atomic<bool> g_initialized{false};
        std::atomic<bool> g_stop{false};
        std::atomic<bool> g_writerAlive{false};

        std::atomic<std::uint64_t> g_eventSequence{0};
        std::atomic<std::uint64_t> g_enqueued{0};
        std::atomic<std::uint64_t> g_written{0};
        std::atomic<std::uint64_t> g_dropped{0};
        std::atomic<std::uint64_t> g_queueHighWater{0};
        std::atomic<std::uint64_t> g_bytesWritten{0};
        std::atomic<bool> g_overflowActive{false};
        std::atomic<bool> g_detailActive{false};
        bool g_fullPipelineDetail = false;
        std::atomic<std::uint64_t> g_detailedPipelineRows{0};

        HANDLE g_wakeEvent = nullptr;
        std::thread g_writer{};

        fs::path g_sessionRoot{};
        std::string g_lastError{};

        LARGE_INTEGER g_qpcFrequency{};
        std::uint64_t g_lastPresentQpc = 0;
        std::uint64_t g_previousFrame = 0;

        FrameCounters g_previousCounters{};
        bool g_havePreviousCounters = false;

        render::semantic_authority::Snapshot
            g_previousSemanticSnapshot{};

        bool g_havePreviousSemanticSnapshot = false;
        std::uint32_t g_previousMode = 0;
        render::semantic_authority::EnvironmentClass
            g_previousEnvironment =
                render::semantic_authority::
                    EnvironmentClass::UnknownTransition;
        UINT g_previousWidth = 0;
        UINT g_previousHeight = 0;
        std::uint64_t g_previousDeviceGeneration = 0;
        std::uint64_t g_previousSurfaceGeneration = 0;
        std::uint64_t g_previousWave3HistoryGeneration = 0;
        bool g_previousWave4HistoryValid = false;
        std::uint64_t g_lastWave3TimingFrame = 0;
        std::uint64_t g_lastWave4TimingFrame = 0;

        thread_local PendingPassRun g_pendingPass{};

        std::uint64_t QpcNow() noexcept
        {
            LARGE_INTEGER value{};

            QueryPerformanceCounter(
                &value);

            return
                static_cast<std::uint64_t>(
                    value.QuadPart);
        }

        std::uint64_t HashMix(
            std::uint64_t hash,
            const std::uint64_t value) noexcept
        {
            constexpr std::uint64_t prime =
                1099511628211ull;

            for (std::uint32_t i = 0;
                 i < 8;
                 ++i)
            {
                hash ^=
                    static_cast<std::uint8_t>(
                        value >>
                        (i * 8));

                hash *= prime;
            }

            return hash;
        }

        std::uint64_t HashText(
            const char* value) noexcept
        {
            std::uint64_t hash =
                14695981039346656037ull;

            if (value == nullptr)
            {
                return hash;
            }

            while (*value != '\0')
            {
                hash ^=
                    static_cast<std::uint8_t>(
                        *value++);

                hash *=
                    1099511628211ull;
            }

            return hash;
        }

        std::size_t DefaultRingCapacity()
        {
            std::size_t capacity =
                65536;

            wchar_t text[32]{};

            if (GetEnvironmentVariableW(
                    L"GZFX_TEST_TELEMETRY_RING_CAPACITY",
                    text,
                    static_cast<DWORD>(
                        std::size(text))) > 0)
            {
                const auto parsed =
                    std::wcstoull(
                        text,
                        nullptr,
                        10);

                if (parsed >= 64 &&
                    parsed <= 262144 &&
                    (parsed & (parsed - 1)) == 0)
                {
                    capacity =
                        static_cast<std::size_t>(
                            parsed);
                }
            }

            return capacity;
        }

        bool Disabled()
        {
            wchar_t value[8]{};

            if (GetEnvironmentVariableW(
                    L"GZFX_TELEMETRY_DISABLE",
                    value,
                    static_cast<DWORD>(
                        std::size(value))) > 0)
            {
                return true;
            }

            wchar_t outputRoot[8]{};
            wchar_t testActive[8]{};

            const bool testHarness =
                GetEnvironmentVariableW(
                    L"GZFX_TEST_OUTPUT_ROOT",
                    outputRoot,
                    static_cast<DWORD>(
                        std::size(outputRoot))) > 0;

            const bool telemetryTest =
                GetEnvironmentVariableW(
                    L"GZFX_TEST_TELEMETRY_ACTIVE",
                    testActive,
                    static_cast<DWORD>(
                        std::size(testActive))) > 0;

            return
                testHarness &&
                !telemetryTest;
        }

        std::string Timestamp()
        {
            SYSTEMTIME value{};
            GetLocalTime(&value);

            std::ostringstream out;
            out
                << std::setfill('0')
                << std::setw(4)
                << value.wYear
                << std::setw(2)
                << value.wMonth
                << std::setw(2)
                << value.wDay
                << "_"
                << std::setw(2)
                << value.wHour
                << std::setw(2)
                << value.wMinute
                << std::setw(2)
                << value.wSecond;

            return out.str();
        }

        void UpdateHighWater(
            const std::uint64_t depth) noexcept
        {
            auto current =
                g_queueHighWater.load(
                    std::memory_order_relaxed);

            while (depth > current &&
                   !g_queueHighWater.compare_exchange_weak(
                       current,
                       depth,
                       std::memory_order_relaxed))
            {
            }
        }

        bool Enqueue(
            Event event) noexcept
        {
            if (!g_initialized.load(
                    std::memory_order_acquire))
            {
                return false;
            }

            event.sequence =
                g_eventSequence.fetch_add(
                    1,
                    std::memory_order_relaxed) +
                1;

            if (event.qpc == 0)
            {
                event.qpc =
                    QpcNow();
            }

            if (!g_ring.TryEnqueue(event))
            {
                g_dropped.fetch_add(
                    1,
                    std::memory_order_relaxed);

                if (!g_overflowActive.exchange(
                        true,
                        std::memory_order_acq_rel))
                {
                    // The transition itself may not fit while full;
                    // listener_status still exposes the loss.
                }

                return false;
            }

            g_enqueued.fetch_add(
                1,
                std::memory_order_relaxed);

            const auto depth =
                g_ring.ApproxSize();

            UpdateHighWater(
                depth);

            if (g_wakeEvent != nullptr &&
                (
                    depth >= 1024 ||
                    event.type ==
                        EventType::Frame ||
                    event.type ==
                        EventType::Transition
                ))
            {
                SetEvent(
                    g_wakeEvent);
            }

            return true;
        }

        std::uint64_t PassSignature(
            const pipeline_cartography::
                PassTelemetry& pass)
        {
            std::uint64_t hash =
                14695981039346656037ull;

            hash =
                HashMix(
                    hash,
                    static_cast<std::uint64_t>(
                        pass.kind));

            hash =
                HashMix(
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
                    HashMix(
                        hash,
                        stable
                            ? pass.shaderHashes[i]
                            : static_cast<std::uint64_t>(
                                pass.shaderObjectIds[i]));
            }

            for (const auto value :
                 pass.rtvs)
            {
                hash =
                    HashMix(
                        hash,
                        value);
            }

            hash =
                HashMix(
                    hash,
                    pass.dsv);
            hash =
                HashMix(
                    hash,
                    pass.omUavHash);
            hash =
                HashMix(
                    hash,
                    pass.csUavHash);
            hash =
                HashMix(
                    hash,
                    pass.readHash);
            hash =
                HashMix(
                    hash,
                    pass.writeHash);
            hash =
                HashMix(
                    hash,
                    pass.constantBufferHash);
            hash =
                HashMix(
                    hash,
                    pass.samplerHash);
            hash =
                HashMix(
                    hash,
                    pass.blendHash);
            hash =
                HashMix(
                    hash,
                    pass.depthHash);
            hash =
                HashMix(
                    hash,
                    pass.rasterHash);
            hash =
                HashMix(
                    hash,
                    pass.topology);
            hash =
                HashMix(
                    hash,
                    pass.viewportWidth);
            hash =
                HashMix(
                    hash,
                    pass.viewportHeight);

            return hash;
        }

        void EnqueuePendingPass() noexcept
        {
            if (!g_pendingPass.active)
            {
                return;
            }

            const auto& pass =
                g_pendingPass.pass;

            Event event{};
            event.type =
                EventType::PipelineRun;
            event.frame =
                pass.frame;
            event.ordinal =
                g_pendingPass.firstOrdinal;

            event.u[0] =
                static_cast<std::uint64_t>(
                    pass.kind);
            event.u[1] =
                static_cast<std::uint64_t>(
                    pass.drawKind);
            event.u[2] =
                g_pendingPass.firstOrdinal;
            event.u[3] =
                g_pendingPass.lastOrdinal;
            event.u[4] =
                g_pendingPass.repeatCount;

            for (std::size_t i = 0;
                 i < pass.shaderHashes.size();
                 ++i)
            {
                event.u[5 + i] =
                    pass.shaderHashes[i];
            }

            for (std::size_t i = 0;
                 i < pass.rtvs.size();
                 ++i)
            {
                event.u[11 + i] =
                    pass.rtvs[i];
            }

            event.u[15] =
                pass.dsv;
            event.u[16] =
                pass.omUavHash;
            event.u[17] =
                pass.csUavHash;
            event.u[18] =
                pass.readHash;
            event.u[19] =
                pass.writeHash;
            event.u[20] =
                pass.totalReadResourceCount;
            event.u[21] =
                pass.totalWriteResourceCount;
            event.u[22] =
                pass.retainedReadResourceCount;
            event.u[23] =
                pass.retainedWriteResourceCount;
            event.u[24] =
                pass.readOverflow ? 1ull : 0ull;
            event.u[25] =
                pass.writeOverflow ? 1ull : 0ull;

            for (std::size_t i = 0;
                 i < pass.readResources.size();
                 ++i)
            {
                event.u[26 + i] =
                    pass.readResources[i];
            }

            for (std::size_t i = 0;
                 i < pass.writeResources.size();
                 ++i)
            {
                event.u[50 + i] =
                    pass.writeResources[i];
            }

            for (std::size_t i = 0;
                 i < pass.explicitReads.size();
                 ++i)
            {
                event.u[62 + i] =
                    pass.explicitReads[i];
            }

            for (std::size_t i = 0;
                 i < pass.explicitWrites.size();
                 ++i)
            {
                event.u[64 + i] =
                    pass.explicitWrites[i];
            }

            event.u[66] =
                pass.explicitReadCount;
            event.u[67] =
                pass.explicitWriteCount;
            event.u[68] =
                pass.sourceSubresource;
            event.u[69] =
                pass.destinationSubresource;
            event.u[70] =
                pass.explicitFormat;
            event.u[71] =
                pass.explicitFlags;

            event.u[72] =
                pass.constantBufferHash;
            event.u[73] =
                pass.samplerHash;
            event.u[74] =
                pass.blendHash;
            event.u[75] =
                pass.depthHash;
            event.u[76] =
                pass.rasterHash;
            event.u[77] =
                pass.topology;
            event.u[78] =
                pass.viewportWidth;
            event.u[79] =
                pass.viewportHeight;

            for (std::size_t i = 0;
                 i < pass.args.size();
                 ++i)
            {
                event.u[80 + i] =
                    pass.args[i];
            }

            for (std::size_t i = 0;
                 i < pass.shaderObjectIds.size();
                 ++i)
            {
                event.u[84 + i] =
                    pass.shaderObjectIds[i];
            }

            event.u[90] =
                pass.shaderHashKnownMask;

            if (Enqueue(event))
            {
                g_detailedPipelineRows.fetch_add(
                    1,
                    std::memory_order_relaxed);
            }

            g_pendingPass = {};
        }

        void OnPipelinePass(
            const pipeline_cartography::
                PassTelemetry& pass,
            void*) noexcept
        {
            try
            {
                if (!g_fullPipelineDetail &&
                    !g_detailActive.load(
                        std::memory_order_acquire))
                {
                    return;
                }

                const auto signature =
                    PassSignature(
                        pass);

                if (g_pendingPass.active &&
                    g_pendingPass.pass.frame ==
                        pass.frame &&
                    g_pendingPass.signature ==
                        signature)
                {
                    g_pendingPass.lastOrdinal =
                        pass.ordinal;
                    ++g_pendingPass.repeatCount;
                    return;
                }

                EnqueuePendingPass();

                g_pendingPass.active = true;
                g_pendingPass.signature =
                    signature;
                g_pendingPass.pass =
                    pass;
                g_pendingPass.firstOrdinal =
                    pass.ordinal;
                g_pendingPass.lastOrdinal =
                    pass.ordinal;
                g_pendingPass.repeatCount = 1;
            }
            catch (...)
            {
            }
        }

        void OnResource(
            const pipeline_cartography::
                ResourceTelemetry& resource,
            void*) noexcept
        {
            Event event{};
            event.type =
                EventType::Resource;

            event.u[0] =
                resource.identity;
            event.u[1] =
                resource.birth;
            event.u[2] =
                resource.type;
            event.u[3] =
                resource.width;
            event.u[4] =
                resource.height;
            event.u[5] =
                resource.depth;
            event.u[6] =
                resource.mipLevels;
            event.u[7] =
                resource.arraySize;
            event.u[8] =
                resource.format;
            event.u[9] =
                resource.bindFlags;
            event.u[10] =
                resource.miscFlags;
            event.u[11] =
                resource.sampleCount;
            event.u[12] =
                resource.usage;
            event.u[13] =
                resource.cpuAccessFlags;
            event.u[14] =
                resource.structureStride;
            event.u[15] =
                resource.bufferBytes;
            event.u[16] =
                resource.ownerHash;
            event.u[17] =
                resource.debugNameHash;

            Enqueue(event);
        }

        enum class DetailGateAction : std::uint32_t
        {
            None = 0,
            Start = 1,
            Stop = 2
        };

        DetailGateAction DetailGateForStage(
            const render::stage_scheduler::Boundary boundary,
            const render::stage_scheduler::Phase phase) noexcept
        {
            using B = render::stage_scheduler::Boundary;
            using P = render::stage_scheduler::Phase;

            return
                boundary ==
                        B::AfterAmbientScreenSpaceComposite &&
                    phase == P::AfterExecution
                    ? DetailGateAction::Start
                    : DetailGateAction::None;
        }

        DetailGateAction DetailGateForPresent(
            const PresentPhase phase) noexcept
        {
            return
                phase ==
                    PresentPhase::BeforeOriginalPresent
                    ? DetailGateAction::Stop
                    : DetailGateAction::None;
        }

        void OnStage(
            const render::stage_scheduler::
                Event& stage,
            void*) noexcept
        {
            using B =
                render::stage_scheduler::Boundary;
            using P =
                render::stage_scheduler::Phase;

            if (!g_fullPipelineDetail)
            {
                const auto action =
                    DetailGateForStage(
                        stage.boundary,
                        stage.phase);

                if (action ==
                    DetailGateAction::Start)
                {
                    EnqueuePendingPass();
                    g_detailActive.store(
                        true,
                        std::memory_order_release);
                }
            }

            Event event{};
            event.type =
                EventType::Stage;
            event.frame =
                stage.frame;
            event.ordinal =
                stage.ordinal;
            event.u[0] =
                static_cast<std::uint64_t>(
                    stage.boundary);
            event.u[1] =
                static_cast<std::uint64_t>(
                    stage.phase);
            event.u[2] =
                stage.sequence;
            event.u[3] =
                HashText(
                    stage.reason);

            Enqueue(event);
        }

        void EnqueueTransition(
            const std::uint64_t frame,
            const TransitionKind kind,
            const std::uint64_t before = 0,
            const std::uint64_t after = 0,
            const std::uint64_t extra = 0) noexcept
        {
            Event event{};
            event.type =
                EventType::Transition;
            event.frame =
                frame;
            event.subtype =
                static_cast<std::uint32_t>(
                    kind);
            event.u[0] = before;
            event.u[1] = after;
            event.u[2] = extra;

            Enqueue(event);
        }

        std::uint64_t ChangeMask(
            const render::semantic_authority::
                ResourceInfo& a,
            const render::semantic_authority::
                ResourceInfo& b,
            const render::semantic_authority::
                AvailabilityInfo& aa,
            const render::semantic_authority::
                AvailabilityInfo& ba)
        {
            std::uint64_t mask = 0;

            if (a.confidence != b.confidence)
                mask |= 1ull << 0;
            if (a.identity != b.identity)
                mask |= 1ull << 1;
            if (a.generation != b.generation)
                mask |= 1ull << 2;
            if (a.width != b.width ||
                a.height != b.height ||
                a.depth != b.depth ||
                a.mipLevels != b.mipLevels ||
                a.arraySize != b.arraySize)
                mask |= 1ull << 3;
            if (a.format != b.format ||
                a.bindFlags != b.bindFlags)
                mask |= 1ull << 4;
            if (aa.currentFrameAvailable !=
                    ba.currentFrameAvailable ||
                aa.previousFrameAvailable !=
                    ba.previousFrameAvailable)
                mask |= 1ull << 5;
            if (aa.producedThisFrame !=
                    ba.producedThisFrame ||
                aa.consumedThisFrame !=
                    ba.consumedThisFrame ||
                aa.observedThisFrame !=
                    ba.observedThisFrame)
                mask |= 1ull << 6;
            if (aa.contentEpoch !=
                    ba.contentEpoch ||
                aa.epochKind !=
                    ba.epochKind)
                mask |= 1ull << 7;
            if (aa.environmentScope !=
                    ba.environmentScope)
                mask |= 1ull << 8;
            if (aa.validFromStage !=
                    ba.validFromStage ||
                aa.validUntilStage !=
                    ba.validUntilStage)
                mask |= 1ull << 9;

            return mask;
        }

        void EmitSemanticChanges(
            const render::semantic_authority::
                Snapshot& snapshot)
        {
            for (std::size_t i = 0;
                 i < snapshot.resources.size();
                 ++i)
            {
                const auto& resource =
                    snapshot.resources[i];

                const auto& availability =
                    snapshot.availability[i];

                std::uint64_t mask =
                    std::numeric_limits<
                        std::uint64_t>::max();

                if (g_havePreviousSemanticSnapshot)
                {
                    mask =
                        ChangeMask(
                            g_previousSemanticSnapshot.
                                resources[i],
                            resource,
                            g_previousSemanticSnapshot.
                                availability[i],
                            availability);

                    if (mask == 0)
                    {
                        continue;
                    }
                }

                Event event{};
                event.type =
                    EventType::Semantic;
                event.frame =
                    snapshot.frame;
                event.u[0] = i;
                event.u[1] =
                    static_cast<std::uint64_t>(
                        resource.confidence);
                event.u[2] =
                    resource.identity;
                event.u[3] =
                    resource.generation;
                event.u[4] =
                    resource.width;
                event.u[5] =
                    resource.height;
                event.u[6] =
                    resource.depth;
                event.u[7] =
                    resource.mipLevels;
                event.u[8] =
                    resource.arraySize;
                event.u[9] =
                    static_cast<std::uint64_t>(
                        resource.format);
                event.u[10] =
                    resource.bindFlags;
                event.u[11] =
                    availability.observedThisFrame;
                event.u[12] =
                    availability.producedThisFrame;
                event.u[13] =
                    availability.consumedThisFrame;
                event.u[14] =
                    availability.currentFrameAvailable;
                event.u[15] =
                    availability.previousFrameAvailable;
                event.u[16] =
                    availability.contentEpoch;
                event.u[17] =
                    static_cast<std::uint64_t>(
                        availability.epochKind);
                event.u[18] =
                    static_cast<std::uint64_t>(
                        availability.environmentScope);
                event.u[19] =
                    static_cast<std::uint64_t>(
                        availability.validFromStage);
                event.u[20] =
                    static_cast<std::uint64_t>(
                        availability.validUntilStage);
                event.u[21] =
                    mask;

                Enqueue(event);
            }

            g_previousSemanticSnapshot =
                snapshot;
            g_havePreviousSemanticSnapshot =
                true;
        }

        const char* TransitionName(
            const TransitionKind value)
        {
            switch (value)
            {
            case TransitionKind::ListenerStart:
                return "listener_start";
            case TransitionKind::ListenerStop:
                return "listener_stop";
            case TransitionKind::ModeChange:
                return "mode_change";
            case TransitionKind::EnvironmentChange:
                return "environment_change";
            case TransitionKind::ResolutionChange:
                return "resolution_change";
            case TransitionKind::DeviceGenerationChange:
                return "device_generation_change";
            case TransitionKind::SurfaceGenerationChange:
                return "surface_generation_change";
            case TransitionKind::Wave3HistoryGenerationChange:
                return "wave3_history_generation_change";
            case TransitionKind::Wave4HistoryRestart:
                return "wave4_history_restart";
            case TransitionKind::FrameGap:
                return "frame_gap";
            case TransitionKind::Resize:
                return "resize";
            case TransitionKind::QueueOverflowBegin:
                return "queue_overflow_begin";
            case TransitionKind::QueueOverflowEnd:
                return "queue_overflow_end";
            default:
                return "unknown";
            }
        }

        const char* PresentPhaseName(
            const PresentPhase phase)
        {
            switch (phase)
            {
            case PresentPhase::HookEnter: return "hook_enter";
            case PresentPhase::CallbacksBegin: return "callbacks_begin";
            case PresentPhase::CallbacksEnd: return "callbacks_end";
            case PresentPhase::BeforeOriginalPresent: return "before_original_present";
            case PresentPhase::AfterOriginalPresent: return "after_original_present";
            }
            return "unknown";
        }

        const char* EngineAnchorName(
            const EngineAnchor anchor)
        {
            switch (anchor)
            {
            case EngineAnchor::PostProcessing: return "post_processing";
            case EngineAnchor::PreUi: return "pre_ui";
            }
            return "unknown";
        }

        const char* EnginePhaseName(
            const EnginePhase phase)
        {
            switch (phase)
            {
            case EnginePhase::Enter: return "enter";
            case EnginePhase::Exit: return "exit";
            }
            return "unknown";
        }

        const char* EventOwnerName(
            const std::uint64_t value)
        {
            return
                value == 1
                    ? "gzfx"
                    : "external";
        }

        std::string Hex(
            const std::uint64_t value)
        {
            std::ostringstream out;
            out
                << "0x"
                << std::hex
                << std::uppercase
                << value;
            return out.str();
        }

        bool OpenText(
            std::ofstream& stream,
            const fs::path& path,
            const char* header)
        {
            stream.open(
                path,
                std::ios::out |
                    std::ios::trunc);

            if (!stream)
            {
                return false;
            }

            stream << header;

            return
                static_cast<bool>(stream);
        }

        bool OpenPipelineSegment(
            WriterFiles& files)
        {
            if (files.pipeline.is_open())
            {
                files.pipeline.flush();
                files.pipeline.close();
            }

            ++files.pipelineSegment;
            files.pipelineBytes = 0;

            std::wostringstream name;
            name
                << L"pipeline_runs_"
                << std::setw(4)
                << std::setfill(L'0')
                << files.pipelineSegment
                << L".csv";

            return OpenText(
                files.pipeline,
                g_sessionRoot /
                    name.str(),
                "sequence,qpc,frame,first_ordinal,last_ordinal,repeat,kind,draw_kind,vs_hash,hs_hash,ds_hash,gs_hash,ps_hash,cs_hash,rtv0,rtv1,rtv2,rtv3,dsv,om_uav_hash,cs_uav_hash,read_hash,write_hash,total_read_count,total_write_count,retained_read_count,retained_write_count,read_overflow,write_overflow,read0,read1,read2,read3,read4,read5,read6,read7,read8,read9,read10,read11,read12,read13,read14,read15,read16,read17,read18,read19,read20,read21,read22,read23,write0,write1,write2,write3,write4,write5,write6,write7,write8,write9,write10,write11,explicit_read0,explicit_read1,explicit_write0,explicit_write1,explicit_read_count,explicit_write_count,source_subresource,destination_subresource,explicit_format,explicit_flags,cb_hash,sampler_hash,blend_hash,depth_hash,raster_hash,topology,viewport_width,viewport_height,arg0,arg1,arg2,arg3,vs_object,hs_object,ds_object,gs_object,ps_object,cs_object,shader_hash_known_mask\n");
        }

        bool OpenDeferredRunSegment(
            WriterFiles& files)
        {
            if (files.deferredRuns.is_open())
            {
                files.deferredRuns.flush();
                files.deferredRuns.close();
            }

            ++files.deferredRunSegment;
            files.deferredRunBytes = 0;

            std::wostringstream name;
            name
                << L"deferred_runs_"
                << std::setw(4)
                << std::setfill(L'0')
                << files.deferredRunSegment
                << L".csv";

            const std::string readColumns =
                "read0,read1,read2,read3,read4,read5,read6,read7,"
                "read8,read9,read10,read11,read12,read13,read14,read15,"
                "read16,read17,read18,read19,read20,read21,read22,read23";

            const std::string writeColumns =
                "write0,write1,write2,write3,write4,write5,"
                "write6,write7,write8,write9,write10,write11";

            return OpenText(
                files.deferredRuns,
                g_sessionRoot /
                    name.str(),
                (
                    "sequence,qpc,frame,execute_ordinal,list_generation,"
                    "context_generation,recording_generation,run_index,"
                    "first_recording_ordinal,last_recording_ordinal,repeat,"
                    "kind,draw_kind,vs_hash,hs_hash,ds_hash,gs_hash,ps_hash,cs_hash,"
                    "rtv0,rtv1,rtv2,rtv3,dsv,om_uav_hash,cs_uav_hash,"
                    "read_hash,write_hash,total_read_count,total_write_count,"
                    "retained_read_count,retained_write_count,"
                    "read_overflow,write_overflow," +
                    readColumns + "," +
                    writeColumns +
                    ",explicit_read0,explicit_read1,explicit_write0,"
                    "explicit_write1,explicit_read_count,explicit_write_count,"
                    "source_subresource,destination_subresource,explicit_format,"
                    "explicit_flags,cb_hash,sampler_hash,blend_hash,depth_hash,"
                    "raster_hash,topology,viewport_width,viewport_height,"
                    "arg0,arg1,arg2,arg3,vs_object,hs_object,ds_object,gs_object,"
                    "ps_object,cs_object,shader_hash_known_mask\n"
                ).c_str());
        }

        bool OpenWriterFiles(
            WriterFiles& files)
        {
            return
                OpenText(
                    files.frames,
                    g_sessionRoot /
                        L"frames.csv",
                    "sequence,qpc,frame,frame_ms,fps,mode,environment,width,height,device_generation,surface_generation,draw_ordinal,draws,draw_indexed,draw_instanced,draw_indexed_instanced,dispatches,rt_sets,rtv_clears,dsv_clears,copies,resolves,viewport_sets,wave3_executions,wave3_skips,wave3_history_generation,wave3_history_valid,wave3_history_reused,wave3_hierarchy_valid,wave3_temporal_valid,wave3_reset_reason,wave3_gpu_ms,wave4_computes,wave4_applications,wave4_skips,wave4_history_valid,wave4_wave3_generation,wave4_final_frame,wave4_applied_frame,wave4_gpu_ms,queue_depth,dropped\n") &&
                OpenText(
                    files.seconds,
                    g_sessionRoot /
                        L"seconds.csv",
                    "second,first_frame,last_frame,frames,avg_frame_ms,min_frame_ms,max_frame_ms,avg_fps,mode,environment,pipeline_runs,stage_events,gzfx_owned_passes,external_command_lists,dropped,queue_high_water,bytes_written\n") &&
                OpenText(
                    files.stages,
                    g_sessionRoot /
                        L"stages.csv",
                    "sequence,qpc,frame,ordinal,boundary,phase,stage_sequence,reason_hash\n") &&
                OpenText(
                    files.semantics,
                    g_sessionRoot /
                        L"semantics.csv",
                    "sequence,qpc,frame,semantic,confidence,identity,generation,width,height,depth,mips,array_size,format,bind_flags,observed,produced,consumed,current_available,previous_available,content_epoch,epoch_kind,environment_scope,valid_from,valid_until,change_mask\n") &&
                OpenText(
                    files.resources,
                    g_sessionRoot /
                        L"resources.csv",
                    "sequence,qpc,identity,birth,type,width,height,depth,mips,array_size,format,bind_flags,misc_flags,samples,usage,cpu_access,structure_stride,buffer_bytes,owner_hash,debug_name_hash\n") &&
                OpenText(
                    files.transitions,
                    g_sessionRoot /
                        L"transitions.csv",
                    "sequence,qpc,frame,transition,before,after,extra\n") &&
                OpenText(
                    files.ownership,
                    g_sessionRoot /
                        L"ownership.csv",
                    "sequence,qpc,frame,ordinal,owner,owned_pass,command_list,restore_state\n") &&
                OpenText(
                    files.timings,
                    g_sessionRoot /
                        L"timings.csv",
                    "sequence,qpc,frame,source,timing_frame,total_ms\n") &&
                OpenText(
                    files.deferredLists,
                    g_sessionRoot /
                        L"deferred_lists.csv",
                    "sequence,qpc,frame,execute_ordinal,command_list,"
                    "list_generation,context,context_generation,"
                    "recording_generation,original_pass_count,retained_runs,"
                    "finish_restore,execute_restore,complete,overflow\n") &&
                OpenPipelineSegment(
                    files) &&
                OpenDeferredRunSegment(
                    files) &&
                OpenText(
                    files.presentTimeline,
                    g_sessionRoot /
                        L"present_timeline.csv",
                    "sequence,qpc,frame,present_epoch,phase,thread,sync_interval,flags,hresult\n") &&
                OpenText(
                    files.engineEvents,
                    g_sessionRoot /
                        L"engine_events.csv",
                    "sequence,qpc,frame,thread,anchor,phase,runtime_profile,target_rva\n");
        }

        void WriteStatusFile(
            const WriterFiles& files,
            const bool complete)
        {
            std::ofstream out(
                g_sessionRoot /
                    L"listener_status.txt",
                std::ios::out |
                    std::ios::trunc);

            if (!out)
            {
                return;
            }

            out
                << "GZFX Renderer Telemetry Listener\n"
                << "version="
                << gzfx::kVersion
                << "\n"
                << "pid="
                << GetCurrentProcessId()
                << "\n"
                << "last_heartbeat_qpc="
                << QpcNow()
                << "\n"
                << "last_heartbeat_local_time="
                << Timestamp()
                << "\n"
                << "initialized="
                << (g_initialized.load()
                    ? "true"
                    : "false")
                << "\n"
                << "writer_alive="
                << (g_writerAlive.load()
                    ? "true"
                    : "false")
                << "\n"
                << "complete="
                << (complete
                    ? "true"
                    : "false")
                << "\n"
                << "enqueued="
                << g_enqueued.load()
                << "\n"
                << "written="
                << g_written.load()
                << "\n"
                << "dropped="
                << g_dropped.load()
                << "\n"
                << "queue_depth="
                << g_ring.ApproxSize()
                << "\n"
                << "queue_high_water="
                << g_queueHighWater.load()
                << "\n"
                << "ring_capacity="
                << g_ring.Capacity()
                << "\n"
                << "bytes_written="
                << g_bytesWritten.load()
                << "\n"
                << "pipeline_segment="
                << files.pipelineSegment
                << "\n"
                << "detail_gate="
                << (g_fullPipelineDetail
                    ? "full_pipeline"
                    : "late_to_before_original_present")
                << "\n"
                << "detailed_pipeline_rows="
                << g_detailedPipelineRows.load()
                << "\n";

            const auto rendererStatus =
                GetRendererForensicsStatus();

            out
                << "stable_shader_creation_hooks_armed="
                << (rendererStatus.shaderCreationHooksArmed ? "true" : "false")
                << "\n"
                << "resource_creation_hooks_armed="
                << (rendererStatus.resourceCreationHooksArmed ? "true" : "false")
                << "\n"
                << "shader_creations_observed="
                << rendererStatus.shaderCreationsObserved
                << "\n"
                << "resource_creations_observed="
                << rendererStatus.resourceCreationsObserved
                << "\n"
                << "present_epochs="
                << rendererStatus.presentEpochs
                << "\n";

            const auto engineStatus =
                engine_epochs::GetStatus();

            out
                << "engine_profile="
                << Hex(engineStatus.runtimeProfile)
                << "\n"
                << "renderer_core_supported="
                << (engineStatus.rendererCoreSupported ? "true" : "false")
                << "\n"
                << "engine_runtime_known="
                << (engineStatus.runtimeKnown ? "true" : "false")
                << "\n"
                << "engine_profile_available="
                << (engineStatus.engineProfileAvailable ? "true" : "false")
                << "\n"
                << "engine_discovery_supported="
                << (engineStatus.discoverySupported ? "true" : "false")
                << "\n"
                << "engine_discovery_only="
                << (engineStatus.discoveryOnly ? "true" : "false")
                << "\n"
                << "engine_post_processing_discovery_matches="
                << engineStatus.postProcessingDiscoveryMatches
                << "\n"
                << "engine_post_processing_candidate_match_rva="
                << Hex(engineStatus.postProcessingCandidateMatchRva)
                << "\n"
                << "engine_post_processing_candidate_target_rva="
                << Hex(engineStatus.postProcessingCandidateTargetRva)
                << "\n"
                << "engine_pre_ui_discovery_matches="
                << engineStatus.preUiDiscoveryMatches
                << "\n"
                << "engine_pre_ui_candidate_match_rva="
                << Hex(engineStatus.preUiCandidateMatchRva)
                << "\n"
                << "engine_pre_ui_candidate_target_rva="
                << Hex(engineStatus.preUiCandidateTargetRva)
                << "\n"
                << "engine_post_processing_anchor="
                << (engineStatus.postProcessingArmed ? "armed" : "unavailable")
                << "\n"
                << "engine_pre_ui_anchor="
                << (engineStatus.preUiArmed ? "armed" : "unavailable")
                << "\n"
                << "engine_post_processing_events="
                << engineStatus.postProcessingEvents
                << "\n"
                << "engine_pre_ui_events="
                << engineStatus.preUiEvents
                << "\n";

            const auto deferred =
                deferred_context_cartography::
                    GetStatus();

            out
                << "deferred_contexts_attached="
                << deferred.contextsAttached
                << "\n"
                << "deferred_lists_finished="
                << deferred.listsFinished
                << "\n"
                << "deferred_lists_executed="
                << deferred.listsExecuted
                << "\n"
                << "deferred_opaque_executions="
                << deferred.opaqueExecutions
                << "\n"
                << "deferred_recorded_runs="
                << deferred.recordedRuns
                << "\n"
                << "deferred_record_overflows="
                << deferred.recordOverflows
                << "\n"
                << "deferred_registry_evictions="
                << deferred.registryEvictions
                << "\n"
                << "last_error="
                << g_lastError
                << "\n"
                << "status="
                << (
                    g_dropped.load() == 0
                        ? "healthy"
                        : "degraded"
                )
                << "\n";
        }

        void WriteManifest()
        {
            std::ofstream out(
                g_sessionRoot /
                    L"manifest.txt",
                std::ios::out |
                    std::ios::trunc);

            if (!out)
            {
                return;
            }

            out
                << "GZFX telemetry session\n"
                << "version="
                << gzfx::kVersion
                << "\n"
                << "pid="
                << GetCurrentProcessId()
                << "\n"
                << "start="
                << Timestamp()
                << "\n"
                << "qpc_frequency="
                << g_qpcFrequency.QuadPart
                << "\n"
                << "ring_capacity="
                << g_ring.Capacity()
                << "\n"
                << "event_bytes="
                << sizeof(Event)
                << "\n"
                << "session_root="
                << g_sessionRoot.string()
                << "\n"
                << "policy=continuous_async_structured_text\n"
                << "detail_gate="
                << (g_fullPipelineDetail
                    ? "full_pipeline"
                    : "late_to_before_original_present")
                << "\n"
                << "performance_note=forensic_build_not_release_fps_evidence\n"
                << "renderer_identity_policy=creation_time_exact_hash\n"
                << "engine_anchor_policy=versioned_fail_closed\n";
        }

        void WriteEvent(
            WriterFiles& files,
            SecondAccumulator& second,
            const Event& event)
        {
            std::ostringstream row;

            switch (event.type)
            {
            case EventType::Frame:
            {
                const double frameMs =
                    event.d[0];

                const double fps =
                    frameMs > 0.0
                        ? 1000.0 / frameMs
                        : 0.0;

                row
                    << event.sequence << ","
                    << event.qpc << ","
                    << event.frame << ","
                    << frameMs << ","
                    << fps;

                for (std::size_t i = 0;
                     i <= 25;
                     ++i)
                {
                    row
                        << ","
                        << event.u[i];
                }

                row
                    << ","
                    << event.d[1];

                for (std::size_t i = 26;
                     i <= 32;
                     ++i)
                {
                    row
                        << ","
                        << event.u[i];
                }

                row
                    << ","
                    << event.d[2]
                    << ","
                    << event.u[33]
                    << ","
                    << event.u[34]
                    << "\n";

                const auto text =
                    row.str();

                files.frames
                    << text;

                g_bytesWritten.fetch_add(
                    text.size());

                if (!second.active)
                {
                    second.active = true;
                    second.startQpc =
                        event.qpc;
                    second.firstFrame =
                        event.frame;
                    second.frameMsMin =
                        std::numeric_limits<
                            double>::max();
                }

                second.lastFrame =
                    event.frame;
                ++second.frameCount;
                second.frameMsSum +=
                    frameMs;
                second.frameMsMin =
                    std::min(
                        second.frameMsMin,
                        frameMs);
                second.frameMsMax =
                    std::max(
                        second.frameMsMax,
                        frameMs);
                second.mode =
                    event.u[0];
                second.environment =
                    event.u[1];

                break;
            }

            case EventType::Stage:
                row
                    << event.sequence << ","
                    << event.qpc << ","
                    << event.frame << ","
                    << event.ordinal << ","
                    << render::stage_scheduler::
                        BoundaryName(
                            static_cast<
                                render::stage_scheduler::
                                    Boundary>(
                                event.u[0]))
                    << ","
                    << render::stage_scheduler::
                        PhaseName(
                            static_cast<
                                render::stage_scheduler::
                                    Phase>(
                                event.u[1]))
                    << ","
                    << event.u[2]
                    << ","
                    << Hex(event.u[3])
                    << "\n";
                files.stages
                    << row.str();
                ++second.stageEvents;
                g_bytesWritten.fetch_add(
                    row.str().size());
                break;

            case EventType::PipelineRun:
            {
                row
                    << event.sequence << ","
                    << event.qpc << ","
                    << event.frame
                    << ","
                    << event.u[2]
                    << ","
                    << event.u[3]
                    << ","
                    << event.u[4]
                    << ","
                    << event.u[0]
                    << ","
                    << event.u[1];

                for (std::size_t i = 5;
                     i <= 90;
                     ++i)
                {
                    row
                        << ","
                        << event.u[i];
                }

                row
                    << "\n";

                const auto text =
                    row.str();

                if (files.pipelineBytes +
                        text.size() >
                    kPipelineSegmentBytes)
                {
                    if (!OpenPipelineSegment(
                            files))
                    {
                        g_lastError =
                            "pipeline segment rotation failed";
                    }
                }

                files.pipeline
                    << text;
                files.pipelineBytes +=
                    text.size();
                ++second.pipelineRuns;

                g_bytesWritten.fetch_add(
                    text.size());

                break;
            }

            case EventType::Resource:
                row
                    << event.sequence << ","
                    << event.qpc;

                for (std::size_t i = 0;
                     i <= 17;
                     ++i)
                {
                    row
                        << ","
                        << event.u[i];
                }

                row << "\n";
                files.resources
                    << row.str();
                g_bytesWritten.fetch_add(
                    row.str().size());
                break;

            case EventType::Semantic:
                row
                    << event.sequence << ","
                    << event.qpc << ","
                    << event.frame << ","
                    << render::semantic_authority::
                        SemanticName(
                            static_cast<
                                render::semantic_authority::
                                    Semantic>(
                                event.u[0]));

                for (std::size_t i = 1;
                     i <= 21;
                     ++i)
                {
                    if (i == 2)
                    {
                        row
                            << ","
                            << Hex(
                                event.u[i]);
                    }
                    else
                    {
                        row
                            << ","
                            << event.u[i];
                    }
                }

                row << "\n";
                files.semantics
                    << row.str();
                g_bytesWritten.fetch_add(
                    row.str().size());
                break;

            case EventType::Transition:
                row
                    << event.sequence << ","
                    << event.qpc << ","
                    << event.frame << ","
                    << TransitionName(
                        static_cast<
                            TransitionKind>(
                                event.subtype))
                    << ","
                    << event.u[0]
                    << ","
                    << event.u[1]
                    << ","
                    << event.u[2]
                    << "\n";
                files.transitions
                    << row.str();
                g_bytesWritten.fetch_add(
                    row.str().size());
                break;

            case EventType::Ownership:
                row
                    << event.sequence << ","
                    << event.qpc << ","
                    << event.frame << ","
                    << event.ordinal << ","
                    << EventOwnerName(
                        event.u[0])
                    << ","
                    << event.u[1]
                    << ","
                    << Hex(event.u[2])
                    << ","
                    << event.u[3]
                    << "\n";
                files.ownership
                    << row.str();

                if (event.u[0] == 1)
                {
                    ++second.ownedPasses;
                }
                else
                {
                    ++second.externalCommandLists;
                }

                g_bytesWritten.fetch_add(
                    row.str().size());
                break;

            case EventType::Timing:
                row
                    << event.sequence << ","
                    << event.qpc << ","
                    << event.frame << ","
                    << event.subtype << ","
                    << event.u[0] << ","
                    << event.d[0]
                    << "\n";
                files.timings
                    << row.str();
                g_bytesWritten.fetch_add(
                    row.str().size());
                break;

            case EventType::DeferredList:
                row
                    << event.sequence << ","
                    << event.qpc << ","
                    << event.frame;

                for (std::size_t i = 0;
                     i <= 11;
                     ++i)
                {
                    row
                        << ","
                        << event.u[i];
                }

                row << "\n";
                files.deferredLists
                    << row.str();
                g_bytesWritten.fetch_add(
                    row.str().size());
                break;

            case EventType::DeferredRun:
            {
                row
                    << event.sequence << ","
                    << event.qpc << ","
                    << event.frame;

                for (std::size_t i = 0;
                     i <= 95;
                     ++i)
                {
                    row
                        << ","
                        << event.u[i];
                }

                row << "\n";
                const auto text =
                    row.str();

                if (files.deferredRunBytes +
                        text.size() >
                    kPipelineSegmentBytes)
                {
                    if (!OpenDeferredRunSegment(
                            files))
                    {
                        g_lastError =
                            "deferred segment rotation failed";
                    }
                }

                files.deferredRuns
                    << text;
                files.deferredRunBytes +=
                    text.size();
                g_bytesWritten.fetch_add(
                    text.size());
                break;
            }

            case EventType::PresentTimeline:
                row
                    << event.sequence << ","
                    << event.qpc << ","
                    << event.frame << ","
                    << event.u[0] << ","
                    << PresentPhaseName(
                        static_cast<PresentPhase>(event.u[1])) << ","
                    << event.u[2] << ","
                    << event.u[3] << ","
                    << event.u[4] << ","
                    << static_cast<std::int32_t>(event.u[5])
                    << "\n";
                files.presentTimeline
                    << row.str();
                g_bytesWritten.fetch_add(
                    row.str().size());
                break;

            case EventType::EngineEvent:
                row
                    << event.sequence << ","
                    << event.qpc << ","
                    << event.frame << ","
                    << event.u[0] << ","
                    << EngineAnchorName(
                        static_cast<EngineAnchor>(event.u[1])) << ","
                    << EnginePhaseName(
                        static_cast<EnginePhase>(event.u[2])) << ","
                    << event.u[3] << ","
                    << Hex(event.u[4])
                    << "\n";
                files.engineEvents
                    << row.str();
                g_bytesWritten.fetch_add(
                    row.str().size());
                break;
            }

            g_written.fetch_add(
                1,
                std::memory_order_relaxed);
        }

        void FinishSecondIfReady(
            WriterFiles& files,
            SecondAccumulator& second,
            const std::uint64_t currentQpc,
            std::uint64_t& secondIndex)
        {
            if (!second.active ||
                g_qpcFrequency.QuadPart <= 0 ||
                currentQpc -
                    second.startQpc <
                    static_cast<std::uint64_t>(
                        g_qpcFrequency.QuadPart))
            {
                return;
            }

            const double count =
                static_cast<double>(
                    std::max<std::uint64_t>(
                        second.frameCount,
                        1));

            const double averageMs =
                second.frameMsSum /
                count;

            files.seconds
                << secondIndex++
                << ","
                << second.firstFrame
                << ","
                << second.lastFrame
                << ","
                << second.frameCount
                << ","
                << averageMs
                << ","
                << second.frameMsMin
                << ","
                << second.frameMsMax
                << ","
                << (
                    averageMs > 0.0
                        ? 1000.0 /
                            averageMs
                        : 0.0
                )
                << ","
                << second.mode
                << ","
                << second.environment
                << ","
                << second.pipelineRuns
                << ","
                << second.stageEvents
                << ","
                << second.ownedPasses
                << ","
                << second.externalCommandLists
                << ","
                << g_dropped.load()
                << ","
                << g_queueHighWater.load()
                << ","
                << g_bytesWritten.load()
                << "\n";

            second = {};
        }

        void FlushFiles(
            WriterFiles& files)
        {
            files.frames.flush();
            files.seconds.flush();
            files.stages.flush();
            files.semantics.flush();
            files.resources.flush();
            files.transitions.flush();
            files.ownership.flush();
            files.timings.flush();
            files.deferredLists.flush();
            files.pipeline.flush();
            files.deferredRuns.flush();
            files.presentTimeline.flush();
            files.engineEvents.flush();
        }

        void WriterMain()
        {
            g_writerAlive.store(
                true,
                std::memory_order_release);

            WriterFiles files{};

            if (!OpenWriterFiles(
                    files))
            {
                g_lastError =
                    "failed to open telemetry files";
                g_writerAlive.store(
                    false,
                    std::memory_order_release);
                return;
            }

            SecondAccumulator second{};
            std::uint64_t secondIndex = 0;
            std::uint64_t lastFlush =
                QpcNow();

            WriteStatusFile(
                files,
                false);

            for (;;)
            {
                if (g_wakeEvent != nullptr)
                {
                    WaitForSingleObject(
                        g_wakeEvent,
                        250);
                }
                else
                {
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(
                            250));
                }

                Event event{};

                while (g_ring.TryDequeue(
                    event))
                {
                    WriteEvent(
                        files,
                        second,
                        event);
                }

                const auto now =
                    QpcNow();

                FinishSecondIfReady(
                    files,
                    second,
                    now,
                    secondIndex);

                const bool flushDue =
                    g_qpcFrequency.QuadPart > 0 &&
                    now - lastFlush >=
                        static_cast<std::uint64_t>(
                            g_qpcFrequency.QuadPart);

                if (flushDue)
                {
                    FlushFiles(
                        files);

                    WriteStatusFile(
                        files,
                        false);

                    lastFlush =
                        now;
                }

                if (g_stop.load(
                        std::memory_order_acquire) &&
                    g_ring.ApproxSize() == 0)
                {
                    break;
                }
            }

            FlushFiles(files);
            WriteStatusFile(
                files,
                true);

            std::ofstream complete(
                g_sessionRoot /
                    L"telemetry_session_complete.txt",
                std::ios::out |
                    std::ios::trunc);

            if (complete)
            {
                complete
                    << "version="
                    << gzfx::kVersion
                    << "\n"
                    << "status=complete\n"
                    << "written="
                    << g_written.load()
                    << "\n"
                    << "dropped="
                    << g_dropped.load()
                    << "\n";
            }

            g_writerAlive.store(
                false,
                std::memory_order_release);
        }

        void CopyStartupTraceIntoSession()
        {
            if (g_sessionRoot.empty())
            {
                return;
            }

            const fs::path source =
                paths::DiagnosticsRoot() /
                L"renderer_startup.csv";

            if (!fs::exists(source))
            {
                return;
            }

            std::error_code error;

            fs::copy_file(
                source,
                g_sessionRoot /
                    L"renderer_startup.csv",
                fs::copy_options::overwrite_existing,
                error);
        }

        void WriteSessionCloseState(
            const char* const state)
        {
            if (state == nullptr ||
                g_sessionRoot.empty())
            {
                return;
            }

            std::ofstream out(
                g_sessionRoot /
                    L"session_close_state.txt",
                std::ios::out |
                    std::ios::trunc);

            if (!out)
            {
                return;
            }

            out
                << "state=" << state << "\n"
                << "qpc=" << QpcNow() << "\n"
                << "enqueued=" << g_enqueued.load() << "\n"
                << "written=" << g_written.load() << "\n"
                << "dropped=" << g_dropped.load() << "\n"
                << "queue_depth=" << g_ring.ApproxSize() << "\n"
                << "writer_alive="
                << (g_writerAlive.load() ? "true" : "false")
                << "\n";
        }

        void EmitFrameTransitions(
            const std::uint64_t frame,
            const std::uint32_t mode,
            const render::semantic_authority::
                Snapshot& semantic,
            const std::uint64_t deviceGeneration,
            const std::uint64_t surfaceGeneration,
            const render::wave3::
                TelemetryStatus& wave3,
            const render::ambient_visibility_v2::
                TelemetryStatus& wave4)
        {
            if (g_previousFrame != 0 &&
                frame !=
                    g_previousFrame + 1)
            {
                EnqueueTransition(
                    frame,
                    TransitionKind::FrameGap,
                    g_previousFrame,
                    frame);
            }

            if (frame != 0 &&
                mode !=
                    g_previousMode)
            {
                EnqueueTransition(
                    frame,
                    TransitionKind::ModeChange,
                    g_previousMode,
                    mode);
            }

            if (semantic.environment !=
                g_previousEnvironment)
            {
                EnqueueTransition(
                    frame,
                    TransitionKind::
                        EnvironmentChange,
                    static_cast<std::uint64_t>(
                        g_previousEnvironment),
                    static_cast<std::uint64_t>(
                        semantic.environment));
            }

            if (semantic.screenWidth !=
                    g_previousWidth ||
                semantic.screenHeight !=
                    g_previousHeight)
            {
                const std::uint64_t before =
                    (
                        static_cast<std::uint64_t>(
                            g_previousWidth) <<
                        32
                    ) |
                    g_previousHeight;

                const std::uint64_t after =
                    (
                        static_cast<std::uint64_t>(
                            semantic.screenWidth) <<
                        32
                    ) |
                    semantic.screenHeight;

                EnqueueTransition(
                    frame,
                    TransitionKind::
                        ResolutionChange,
                    before,
                    after);
            }

            if (deviceGeneration !=
                g_previousDeviceGeneration)
            {
                EnqueueTransition(
                    frame,
                    TransitionKind::
                        DeviceGenerationChange,
                    g_previousDeviceGeneration,
                    deviceGeneration);
            }

            if (surfaceGeneration !=
                g_previousSurfaceGeneration)
            {
                EnqueueTransition(
                    frame,
                    TransitionKind::
                        SurfaceGenerationChange,
                    g_previousSurfaceGeneration,
                    surfaceGeneration);
            }

            if (wave3.historyGeneration !=
                g_previousWave3HistoryGeneration)
            {
                EnqueueTransition(
                    frame,
                    TransitionKind::
                        Wave3HistoryGenerationChange,
                    g_previousWave3HistoryGeneration,
                    wave3.historyGeneration);
            }

            if (g_previousWave4HistoryValid &&
                !wave4.signalHistoryValid)
            {
                EnqueueTransition(
                    frame,
                    TransitionKind::
                        Wave4HistoryRestart,
                    1,
                    0);
            }

            g_previousFrame = frame;
            g_previousMode = mode;
            g_previousEnvironment =
                semantic.environment;
            g_previousWidth =
                semantic.screenWidth;
            g_previousHeight =
                semantic.screenHeight;
            g_previousDeviceGeneration =
                deviceGeneration;
            g_previousSurfaceGeneration =
                surfaceGeneration;
            g_previousWave3HistoryGeneration =
                wave3.historyGeneration;
            g_previousWave4HistoryValid =
                wave4.signalHistoryValid;
        }
    }

    bool Initialize() noexcept
    {
        try
        {
            if (Disabled())
            {
                return false;
            }

            bool expected = false;

            if (!g_initialized.compare_exchange_strong(
                    expected,
                    true,
                    std::memory_order_acq_rel))
            {
                return true;
            }

            QueryPerformanceFrequency(
                &g_qpcFrequency);

            wchar_t fullPipeline[8]{};
            g_fullPipelineDetail =
                GetEnvironmentVariableW(
                    L"GZFX_TEST_TELEMETRY_FULL_PIPELINE",
                    fullPipeline,
                    static_cast<DWORD>(
                        std::size(fullPipeline))) > 0;

            g_detailActive.store(
                g_fullPipelineDetail,
                std::memory_order_release);

            const auto capacity =
                DefaultRingCapacity();

            if (!g_ring.Initialize(
                    capacity))
            {
                g_initialized.store(
                    false);
                return false;
            }

            const fs::path runtime =
                paths::RuntimeRoot();

            if (runtime.empty())
            {
                g_initialized.store(
                    false);
                return false;
            }

            std::error_code error;

            const auto sessionName =
                std::string("session_") +
                Timestamp() +
                "_" +
                std::to_string(
                    GetCurrentProcessId());

            g_sessionRoot =
                runtime /
                L"Telemetry" /
                fs::path(sessionName);

            fs::create_directories(
                g_sessionRoot,
                error);

            if (error)
            {
                g_initialized.store(
                    false);
                return false;
            }

            g_wakeEvent =
                CreateEventW(
                    nullptr,
                    FALSE,
                    FALSE,
                    nullptr);

            if (g_wakeEvent == nullptr)
            {
                g_initialized.store(
                    false);
                return false;
            }

            WriteManifest();

            pipeline_cartography::
                SetTelemetrySinks(
                    &OnPipelinePass,
                    &OnResource,
                    nullptr);

            for (std::uint32_t value = 1;
                 value <
                    static_cast<std::uint32_t>(
                        render::stage_scheduler::
                            Boundary::Count);
                 ++value)
            {
                render::stage_scheduler::
                    RegisterCallback(
                        static_cast<
                            render::stage_scheduler::
                                Boundary>(
                            value),
                        &OnStage,
                        nullptr);
            }

            g_stop.store(
                false);

            wchar_t forceOverflow[8]{};

            if (GetEnvironmentVariableW(
                    L"GZFX_TEST_TELEMETRY_FORCE_OVERFLOW",
                    forceOverflow,
                    static_cast<DWORD>(
                        std::size(forceOverflow))) > 0)
            {
                for (std::size_t i = 0;
                     i < g_ring.Capacity() * 2;
                     ++i)
                {
                    Event burst{};
                    burst.type =
                        EventType::Resource;
                    burst.u[0] =
                        0xF0000000ull + i;
                    burst.u[1] =
                        i + 1;

                    Enqueue(burst);
                }
            }

            g_writer =
                std::thread(
                    &WriterMain);

            EnqueueTransition(
                0,
                TransitionKind::
                    ListenerStart,
                0,
                capacity);

            return true;
        }
        catch (...)
        {
            g_initialized.store(
                false);
            return false;
        }
    }

    bool IsInitialized() noexcept
    {
        return
            g_initialized.load(
                std::memory_order_acquire);
    }

    void Shutdown() noexcept
    {
        try
        {
            if (!g_initialized.load(
                    std::memory_order_acquire))
            {
                return;
            }

            WriteSessionCloseState(
                "shutdown_requested");

            EnqueuePendingPass();

            g_initialized.store(
                false,
                std::memory_order_release);

            Event stop{};
            stop.type =
                EventType::Transition;
            stop.subtype =
                static_cast<std::uint32_t>(
                    TransitionKind::
                        ListenerStop);
            stop.qpc =
                QpcNow();

            // Direct ring insert because initialized is now false.
            stop.sequence =
                g_eventSequence.fetch_add(
                    1) +
                1;

            if (g_ring.TryEnqueue(
                    stop))
            {
                g_enqueued.fetch_add(1);
            }

            pipeline_cartography::
                ClearTelemetrySinks();

            g_stop.store(
                true,
                std::memory_order_release);

            if (g_wakeEvent != nullptr)
            {
                SetEvent(
                    g_wakeEvent);
            }

            if (g_writer.joinable())
            {
                g_writer.join();
            }

            if (g_wakeEvent != nullptr)
            {
                CloseHandle(
                    g_wakeEvent);
                g_wakeEvent = nullptr;
            }

            CopyStartupTraceIntoSession();

            WriteSessionCloseState(
                "complete");
        }
        catch (...)
        {
        }
    }

    void OnPresent(
        const FrameCounters& counters) noexcept
    {
        try
        {
            if (!IsInitialized())
            {
                return;
            }

            EnqueuePendingPass();

            const auto semantic =
                render::semantic_authority::
                    GetSnapshot();

            const auto wave3 =
                render::wave3::
                    GetTelemetryStatus();

            const auto wave4 =
                render::ambient_visibility_v2::
                    GetTelemetryStatus();

            const auto deviceGeneration =
                render::execution::
                    DeviceGeneration();

            const auto surfaceGeneration =
                render::execution::
                    SurfaceGeneration();

            const std::uint32_t mode =
                render::state::
                    GetDevelopmentMode();

            const auto now =
                QpcNow();

            const double frameMs =
                g_lastPresentQpc != 0 &&
                g_qpcFrequency.QuadPart > 0
                    ? (
                        static_cast<double>(
                            now -
                            g_lastPresentQpc) *
                        1000.0 /
                        static_cast<double>(
                            g_qpcFrequency.QuadPart)
                    )
                    : 0.0;

            g_lastPresentQpc =
                now;

            EmitFrameTransitions(
                counters.frame,
                mode,
                semantic,
                deviceGeneration,
                surfaceGeneration,
                wave3,
                wave4);

            EmitSemanticChanges(
                semantic);

            FrameCounters delta =
                counters;

            if (g_havePreviousCounters)
            {
                delta.draws -=
                    g_previousCounters.draws;
                delta.drawIndexedCalls -=
                    g_previousCounters.drawIndexedCalls;
                delta.drawInstancedCalls -=
                    g_previousCounters.drawInstancedCalls;
                delta.drawIndexedInstancedCalls -=
                    g_previousCounters.drawIndexedInstancedCalls;
                delta.dispatchCalls -=
                    g_previousCounters.dispatchCalls;
                delta.renderTargetSets -=
                    g_previousCounters.renderTargetSets;
                delta.renderTargetClears -=
                    g_previousCounters.renderTargetClears;
                delta.depthClears -=
                    g_previousCounters.depthClears;
                delta.copies -=
                    g_previousCounters.copies;
                delta.resolves -=
                    g_previousCounters.resolves;
                delta.viewportSets -=
                    g_previousCounters.viewportSets;
            }

            g_previousCounters =
                counters;
            g_havePreviousCounters =
                true;

            Event event{};
            event.type =
                EventType::Frame;
            event.qpc =
                now;
            event.frame =
                counters.frame;
            event.d[0] =
                frameMs;

            event.u[0] =
                mode;
            event.u[1] =
                static_cast<std::uint64_t>(
                    semantic.environment);
            event.u[2] =
                semantic.screenWidth;
            event.u[3] =
                semantic.screenHeight;
            event.u[4] =
                deviceGeneration;
            event.u[5] =
                surfaceGeneration;
            event.u[6] =
                counters.drawOrdinal;
            event.u[7] =
                delta.draws;
            event.u[8] =
                delta.drawIndexedCalls;
            event.u[9] =
                delta.drawInstancedCalls;
            event.u[10] =
                delta.drawIndexedInstancedCalls;
            event.u[11] =
                delta.dispatchCalls;
            event.u[12] =
                delta.renderTargetSets;
            event.u[13] =
                delta.renderTargetClears;
            event.u[14] =
                delta.depthClears;
            event.u[15] =
                delta.copies;
            event.u[16] =
                delta.resolves;
            event.u[17] =
                delta.viewportSets;

            event.u[18] =
                wave3.executions;
            event.u[19] =
                wave3.skips;
            event.u[20] =
                wave3.historyGeneration;
            event.u[21] =
                wave3.historyValid;
            event.u[22] =
                wave3.historyReusedCandidate;
            event.u[23] =
                wave3.hierarchyValid;
            event.u[24] =
                wave3.temporalValid;
            event.u[25] =
                wave3.lastResetReason;
            event.d[1] =
                wave3.gpuTimingValid
                    ? wave3.gpuTotalMs
                    : 0.0;

            event.u[26] =
                wave4.computes;
            event.u[27] =
                wave4.applications;
            event.u[28] =
                wave4.skips;
            event.u[29] =
                wave4.signalHistoryValid;
            event.u[30] =
                wave4.wave3HistoryGeneration;
            event.u[31] =
                wave4.finalVisibilityFrame;
            event.u[32] =
                wave4.lastAppliedFrame;
            event.u[33] =
                g_ring.ApproxSize();
            event.u[34] =
                g_dropped.load(
                    std::memory_order_relaxed);

            event.d[2] =
                wave4.gpuTimingValid
                    ? wave4.gpuTotalMs
                    : 0.0;

            // lastAppliedFrame and queue values are written as timing/health
            // events to preserve the fixed payload without hot allocations.
            Enqueue(event);

            if (wave3.gpuTimingValid &&
                wave3.frame !=
                    g_lastWave3TimingFrame)
            {
                Event timing{};
                timing.type =
                    EventType::Timing;
                timing.subtype = 3;
                timing.frame =
                    counters.frame;
                timing.u[0] =
                    wave3.frame;
                timing.d[0] =
                    wave3.gpuTotalMs;

                Enqueue(timing);
                g_lastWave3TimingFrame =
                    wave3.frame;
            }

            if (wave4.gpuTimingValid &&
                wave4.frame !=
                    g_lastWave4TimingFrame)
            {
                Event timing{};
                timing.type =
                    EventType::Timing;
                timing.subtype = 4;
                timing.frame =
                    counters.frame;
                timing.u[0] =
                    wave4.frame;
                timing.d[0] =
                    wave4.gpuTotalMs;

                Enqueue(timing);
                g_lastWave4TimingFrame =
                    wave4.frame;
            }

            if (g_overflowActive.load(
                    std::memory_order_acquire) &&
                g_ring.ApproxSize() <
                    g_ring.Capacity() / 2)
            {
                if (g_overflowActive.exchange(
                        false))
                {
                    EnqueueTransition(
                        counters.frame,
                        TransitionKind::
                            QueueOverflowEnd,
                        g_dropped.load(),
                        g_ring.ApproxSize());
                }
            }
        }
        catch (...)
        {
        }
    }

    void OnDeferredList(
        const DeferredListInfo& info) noexcept
    {
        if (!IsInitialized())
        {
            return;
        }

        Event event{};
        event.type =
            EventType::DeferredList;
        event.frame =
            info.frame;
        event.ordinal =
            info.executeOrdinal;

        event.u[0] =
            info.executeOrdinal;
        event.u[1] =
            info.commandListIdentity;
        event.u[2] =
            info.commandListGeneration;
        event.u[3] =
            info.contextIdentity;
        event.u[4] =
            info.contextGeneration;
        event.u[5] =
            info.recordingGeneration;
        event.u[6] =
            info.originalPassCount;
        event.u[7] =
            info.retainedRunCount;
        event.u[8] =
            info.finishRestoreState ? 1ull : 0ull;
        event.u[9] =
            info.executeRestoreState ? 1ull : 0ull;
        event.u[10] =
            info.complete ? 1ull : 0ull;
        event.u[11] =
            info.overflow ? 1ull : 0ull;

        Enqueue(event);
    }

    void OnDeferredRun(
        const std::uint64_t frame,
        const std::uint64_t executeOrdinal,
        const std::uint64_t commandListGeneration,
        const std::uint64_t contextGeneration,
        const std::uint64_t recordingGeneration,
        const std::uint64_t runIndex,
        const std::uint64_t firstRecordingOrdinal,
        const std::uint64_t lastRecordingOrdinal,
        const std::uint64_t repeatCount,
        const pipeline_cartography::PassTelemetry& pass) noexcept
    {
        if (!IsInitialized())
        {
            return;
        }

        Event event{};
        event.type =
            EventType::DeferredRun;
        event.frame =
            frame;
        event.ordinal =
            executeOrdinal;

        event.u[0] =
            executeOrdinal;
        event.u[1] =
            commandListGeneration;
        event.u[2] =
            contextGeneration;
        event.u[3] =
            recordingGeneration;
        event.u[4] =
            runIndex;
        event.u[5] =
            firstRecordingOrdinal;
        event.u[6] =
            lastRecordingOrdinal;
        event.u[7] =
            repeatCount;
        event.u[8] =
            static_cast<std::uint64_t>(
                pass.kind);
        event.u[9] =
            static_cast<std::uint64_t>(
                pass.drawKind);

        for (std::size_t i = 0;
             i < pass.shaderHashes.size();
             ++i)
        {
            event.u[10 + i] =
                pass.shaderHashes[i];
        }

        for (std::size_t i = 0;
             i < pass.rtvs.size();
             ++i)
        {
            event.u[16 + i] =
                pass.rtvs[i];
        }

        event.u[20] =
            pass.dsv;
        event.u[21] =
            pass.omUavHash;
        event.u[22] =
            pass.csUavHash;
        event.u[23] =
            pass.readHash;
        event.u[24] =
            pass.writeHash;
        event.u[25] =
            pass.totalReadResourceCount;
        event.u[26] =
            pass.totalWriteResourceCount;
        event.u[27] =
            pass.retainedReadResourceCount;
        event.u[28] =
            pass.retainedWriteResourceCount;
        event.u[29] =
            pass.readOverflow ? 1ull : 0ull;
        event.u[30] =
            pass.writeOverflow ? 1ull : 0ull;

        for (std::size_t i = 0;
             i < pass.readResources.size();
             ++i)
        {
            event.u[31 + i] =
                pass.readResources[i];
        }

        for (std::size_t i = 0;
             i < pass.writeResources.size();
             ++i)
        {
            event.u[55 + i] =
                pass.writeResources[i];
        }

        for (std::size_t i = 0;
             i < pass.explicitReads.size();
             ++i)
        {
            event.u[67 + i] =
                pass.explicitReads[i];
        }

        for (std::size_t i = 0;
             i < pass.explicitWrites.size();
             ++i)
        {
            event.u[69 + i] =
                pass.explicitWrites[i];
        }

        event.u[71] =
            pass.explicitReadCount;
        event.u[72] =
            pass.explicitWriteCount;
        event.u[73] =
            pass.sourceSubresource;
        event.u[74] =
            pass.destinationSubresource;
        event.u[75] =
            pass.explicitFormat;
        event.u[76] =
            pass.explicitFlags;
        event.u[77] =
            pass.constantBufferHash;
        event.u[78] =
            pass.samplerHash;
        event.u[79] =
            pass.blendHash;
        event.u[80] =
            pass.depthHash;
        event.u[81] =
            pass.rasterHash;
        event.u[82] =
            pass.topology;
        event.u[83] =
            pass.viewportWidth;
        event.u[84] =
            pass.viewportHeight;

        for (std::size_t i = 0;
             i < pass.args.size();
             ++i)
        {
            event.u[85 + i] =
                pass.args[i];
        }

        for (std::size_t i = 0;
             i < pass.shaderObjectIds.size();
             ++i)
        {
            event.u[89 + i] =
                pass.shaderObjectIds[i];
        }

        event.u[95] =
            pass.shaderHashKnownMask;

        Enqueue(event);
    }

    void OnPresentTimeline(
        const std::uint64_t frame,
        const std::uint64_t presentEpoch,
        const PresentPhase phase,
        const std::uint32_t threadId,
        const std::uint32_t syncInterval,
        const std::uint32_t flags,
        const std::int32_t hresult) noexcept
    {
        if (!g_fullPipelineDetail &&
            DetailGateForPresent(phase) ==
                DetailGateAction::Stop)
        {
            EnqueuePendingPass();
            g_detailActive.store(
                false,
                std::memory_order_release);
        }

        Event event{};
        event.type = EventType::PresentTimeline;
        event.frame = frame;
        event.u[0] = presentEpoch;
        event.u[1] =
            static_cast<std::uint64_t>(phase);
        event.u[2] = threadId;
        event.u[3] = syncInterval;
        event.u[4] = flags;
        event.u[5] =
            static_cast<std::uint32_t>(hresult);
        Enqueue(event);
    }

    void OnEngineEvent(
        const std::uint64_t frame,
        const EngineAnchor anchor,
        const EnginePhase phase,
        const std::uint32_t threadId,
        const std::uint64_t runtimeProfile,
        const std::uint64_t targetRva) noexcept
    {
        Event event{};
        event.type = EventType::EngineEvent;
        event.frame = frame;
        event.u[0] = threadId;
        event.u[1] =
            static_cast<std::uint64_t>(anchor);
        event.u[2] =
            static_cast<std::uint64_t>(phase);
        event.u[3] = runtimeProfile;
        event.u[4] = targetRva;
        Enqueue(event);
    }

    void OnResize(
        const std::uint64_t frame,
        const UINT width,
        const UINT height,
        const DXGI_FORMAT format) noexcept
    {
        EnqueueTransition(
            frame,
            TransitionKind::Resize,
            (
                static_cast<std::uint64_t>(
                    width) <<
                32
            ) |
                height,
            static_cast<std::uint64_t>(
                format));
    }

    void OnOwnedCommandList(
        const std::uint64_t frame,
        const std::uint64_t ordinal,
        const render::execution::OwnedPass pass,
        ID3D11CommandList* const commandList,
        const bool restoreState) noexcept
    {
        Event event{};
        event.type =
            EventType::Ownership;
        event.frame = frame;
        event.ordinal = ordinal;
        event.u[0] = 1;
        event.u[1] =
            static_cast<std::uint64_t>(
                pass);
        event.u[2] =
            reinterpret_cast<std::uint64_t>(
                commandList);
        event.u[3] =
            restoreState ? 1 : 0;

        Enqueue(event);
    }

    void OnExternalCommandList(
        const std::uint64_t frame,
        const std::uint64_t ordinal,
        ID3D11CommandList* const commandList,
        const bool restoreState) noexcept
    {
        Event event{};
        event.type =
            EventType::Ownership;
        event.frame = frame;
        event.ordinal = ordinal;
        event.u[0] = 0;
        event.u[1] = 0;
        event.u[2] =
            reinterpret_cast<std::uint64_t>(
                commandList);
        event.u[3] =
            restoreState ? 1 : 0;

        Enqueue(event);
    }
    namespace testing
    {
        std::uint32_t DetailGateStageAction(
            const std::uint32_t boundary,
            const std::uint32_t phase) noexcept
        {
            return static_cast<std::uint32_t>(
                DetailGateForStage(
                    static_cast<
                        render::stage_scheduler::Boundary>(
                            boundary),
                    static_cast<
                        render::stage_scheduler::Phase>(
                            phase)));
        }

        std::uint32_t DetailGatePresentAction(
            const std::uint32_t phase) noexcept
        {
            return static_cast<std::uint32_t>(
                DetailGateForPresent(
                    static_cast<PresentPhase>(
                        phase)));
        }
    }

}


extern "C" __declspec(dllexport)
std::uint32_t GZFX_TestTelemetryDetailGateStage(
    const std::uint32_t boundary,
    const std::uint32_t phase) noexcept
{
    return static_cast<std::uint32_t>(
        gzfx::diagnostics::telemetry_listener::
            testing::DetailGateStageAction(
                boundary,
                phase));
}

extern "C" __declspec(dllexport)
std::uint32_t GZFX_TestTelemetryDetailGatePresent(
    const std::uint32_t phase) noexcept
{
    return static_cast<std::uint32_t>(
        gzfx::diagnostics::telemetry_listener::
            testing::DetailGatePresentAction(
                phase));
}

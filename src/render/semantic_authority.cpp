#include "gzfx/render/semantic_authority.hpp"

#include "gzfx/paths.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace gzfx::render::semantic_authority
{
    namespace fs = std::filesystem;

    namespace
    {
        constexpr std::size_t kStageCount =
            static_cast<std::size_t>(ShaderStage::Count);

        constexpr std::size_t kSrvSlots =
            D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT;

        constexpr std::size_t kRtvSlots =
            D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;

        constexpr std::size_t kUavSlots =
            D3D11_PS_CS_UAV_REGISTER_COUNT;

        struct ResourceShape
        {
            D3D11_RESOURCE_DIMENSION dimension =
                D3D11_RESOURCE_DIMENSION_UNKNOWN;
            UINT width = 0;
            UINT height = 0;
            UINT depth = 0;
            UINT mipLevels = 0;
            UINT arraySize = 0;
            DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
            UINT bindFlags = 0;
        };

        struct ViewBinding
        {
            std::uintptr_t identity = 0;
            ResourceShape shape{};
            INT firstMip = -1;
            INT mipCount = -1;
            INT firstSlice = -1;
            INT sliceCount = -1;
        };

        struct CurrentState
        {
            std::array<
                std::uintptr_t,
                kStageCount> shaders{};

            std::array<
                std::array<
                    ViewBinding,
                    kSrvSlots>,
                kStageCount> srvs{};

            std::array<
                ViewBinding,
                kRtvSlots> rtvs{};

            ViewBinding dsv{};

            std::array<
                ViewBinding,
                kUavSlots> omUavs{};

            std::array<
                ViewBinding,
                kUavSlots> csUavs{};

            UINT viewportWidth = 0;
            UINT viewportHeight = 0;
        };

        struct SemanticRecord
        {
            ResourceInfo resource{};
            AvailabilityInfo availability{};
        };

        struct FrameState
        {
            std::uint64_t frame = 0;

            std::uint64_t mainMrtDraws = 0;
            bool sawMainMrt = false;
            bool sawCameraZ = false;
            bool sawAO = false;
            bool sawVolumetric = false;

            bool earlyDepthCopySeen = false;
            bool opaqueWorldMrtSignaled = false;

            std::uintptr_t cameraZCandidate = 0;
            std::uint64_t cameraZMipMask = 0;
            bool cameraZCompleteSignaled = false;

            std::uintptr_t aoFilterTemp = 0;
            UINT aoFilterStep = 0;

            bool ambientCompositePending = false;
            std::uint64_t ambientCompositeOrdinal = 0;

            bool volumetricApplyPending = false;
            std::uint64_t volumetricApplyOrdinal = 0;
            std::uintptr_t volumetricPreviousDepthIdentity = 0;
            bool preWaterDepthPublished = false;
        };

        struct Promotion
        {
            std::uint64_t frame = 0;
            Semantic semantic = Semantic::SceneColorHDR;
            Confidence confidence = Confidence::Unknown;
            std::uintptr_t identity = 0;
            std::uint64_t generation = 0;
            std::string reason;
        };

        struct Observation
        {
            std::uint64_t frame = 0;
            Semantic semantic = Semantic::SceneColorHDR;
            std::uintptr_t identity = 0;
            bool produced = false;
            bool consumed = false;
            bool currentAvailable = false;
            bool previousAvailable = false;
            std::uint64_t epoch = 0;
            std::string reason;
        };

        struct EpochTransition
        {
            std::uint64_t frame = 0;
            std::uintptr_t identity = 0;
            Semantic from = Semantic::SceneColorHDR;
            Semantic to = Semantic::SceneColorHDR;
            std::uint64_t fromEpoch = 0;
            std::uint64_t toEpoch = 0;
            std::string event;
        };

        struct PendingStage
        {
            stage_scheduler::Boundary boundary =
                stage_scheduler::Boundary::Unknown;
            stage_scheduler::Phase phase =
                stage_scheduler::Phase::Landmark;
            const char* reason = nullptr;
        };

        std::mutex g_mutex;
        CurrentState g_state{};
        FrameState g_frame{};

        std::array<
            SemanticRecord,
            static_cast<std::size_t>(Semantic::Count)>
                g_semantics{};

        EnvironmentClass g_environment =
            EnvironmentClass::UnknownTransition;

        UINT g_screenWidth = 0;
        UINT g_screenHeight = 0;

        std::uint64_t g_generation = 0;
        std::uint64_t g_epochSequence = 0;
        std::uint64_t g_snapshotFrame = 0;

        std::unordered_map<std::uintptr_t, ViewBinding> g_srvCache;
        std::unordered_map<std::uintptr_t, ViewBinding> g_rtvCache;
        std::unordered_map<std::uintptr_t, ViewBinding> g_dsvCache;
        std::unordered_map<std::uintptr_t, ViewBinding> g_uavCache;

        bool g_captureActive = false;
        std::uint64_t g_captureStart = 0;
        std::uint64_t g_captureEnd = 0;

        std::vector<Promotion> g_capturePromotions;
        std::vector<Observation> g_captureObservations;
        std::vector<EpochTransition> g_captureEpochTransitions;

        std::size_t SemanticIndex(
            const Semantic semantic)
        {
            return static_cast<std::size_t>(semantic);
        }

        std::size_t StageIndex(
            const ShaderStage stage)
        {
            return static_cast<std::size_t>(stage);
        }

        int StageRank(
            const stage_scheduler::Boundary stage)
        {
            using B = stage_scheduler::Boundary;

            switch (stage)
            {
            case B::AfterShadowProduction:
                return 5;
            case B::AfterDepthPrepass:
                return 10;
            case B::AfterOpaqueWorldMRT:
                return 20;
            case B::AfterCameraZ:
                return 30;
            case B::AfterStockAO:
                return 40;
            case B::BeforeAmbientScreenSpaceComposite:
                return 50;
            case B::AtAmbientScreenSpaceComposite:
                return 51;
            case B::AfterAmbientScreenSpaceComposite:
                return 52;
            case B::BeforeVolumetricApply:
                return 60;
            case B::AfterVolumetricApply:
                return 61;
            case B::PublishPreWaterDepth:
                return 62;
            case B::WaterDomain:
                return 70;
            case B::BeforeGlobalTAA:
                return 80;
            case B::AfterGlobalTAA:
                return 81;
            case B::HDRReduction:
                return 90;
            case B::BeforeTonemap:
                return 100;
            case B::PostTonemapPreUi:
                return 110;
            case B::UiPresent:
                return 120;
            case B::Unknown:
            case B::Count:
            default:
                return -1;
            }
        }

        bool StageWithinValidity(
            const AvailabilityInfo& availability,
            const stage_scheduler::Boundary stage)
        {
            if (stage == stage_scheduler::Boundary::Unknown)
            {
                return true;
            }

            const int rank = StageRank(stage);

            if (rank < 0)
            {
                return false;
            }

            const int from =
                StageRank(availability.validFromStage);

            const int until =
                StageRank(availability.validUntilStage);

            if (from >= 0 && rank < from)
            {
                return false;
            }

            if (until >= 0 && rank > until)
            {
                return false;
            }

            return true;
        }

        std::uintptr_t CanonicalId(
            IUnknown* const value)
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

            const auto id =
                reinterpret_cast<std::uintptr_t>(identity);

            identity->Release();
            return id;
        }

        ResourceShape DescribeResource(
            ID3D11Resource* const resource)
        {
            ResourceShape shape{};

            if (resource == nullptr)
            {
                return shape;
            }

            resource->GetType(&shape.dimension);

            if (shape.dimension ==
                D3D11_RESOURCE_DIMENSION_TEXTURE2D)
            {
                ID3D11Texture2D* texture = nullptr;

                if (SUCCEEDED(
                        resource->QueryInterface(
                            __uuidof(ID3D11Texture2D),
                            reinterpret_cast<void**>(&texture))) &&
                    texture != nullptr)
                {
                    D3D11_TEXTURE2D_DESC desc{};
                    texture->GetDesc(&desc);

                    shape.width = desc.Width;
                    shape.height = desc.Height;
                    shape.depth = 1;
                    shape.mipLevels = desc.MipLevels;
                    shape.arraySize = desc.ArraySize;
                    shape.format = desc.Format;
                    shape.bindFlags = desc.BindFlags;

                    texture->Release();
                }
            }
            else if (shape.dimension ==
                D3D11_RESOURCE_DIMENSION_TEXTURE3D)
            {
                ID3D11Texture3D* texture = nullptr;

                if (SUCCEEDED(
                        resource->QueryInterface(
                            __uuidof(ID3D11Texture3D),
                            reinterpret_cast<void**>(&texture))) &&
                    texture != nullptr)
                {
                    D3D11_TEXTURE3D_DESC desc{};
                    texture->GetDesc(&desc);

                    shape.width = desc.Width;
                    shape.height = desc.Height;
                    shape.depth = desc.Depth;
                    shape.mipLevels = desc.MipLevels;
                    shape.arraySize = 1;
                    shape.format = desc.Format;
                    shape.bindFlags = desc.BindFlags;

                    texture->Release();
                }
            }
            else if (shape.dimension ==
                D3D11_RESOURCE_DIMENSION_TEXTURE1D)
            {
                ID3D11Texture1D* texture = nullptr;

                if (SUCCEEDED(
                        resource->QueryInterface(
                            __uuidof(ID3D11Texture1D),
                            reinterpret_cast<void**>(&texture))) &&
                    texture != nullptr)
                {
                    D3D11_TEXTURE1D_DESC desc{};
                    texture->GetDesc(&desc);

                    shape.width = desc.Width;
                    shape.height = 1;
                    shape.depth = 1;
                    shape.mipLevels = desc.MipLevels;
                    shape.arraySize = desc.ArraySize;
                    shape.format = desc.Format;
                    shape.bindFlags = desc.BindFlags;

                    texture->Release();
                }
            }

            return shape;
        }

        ViewBinding BaseViewBinding(
            ID3D11View* const view)
        {
            ViewBinding binding{};

            if (view == nullptr)
            {
                return binding;
            }

            ID3D11Resource* resource = nullptr;
            view->GetResource(&resource);

            if (resource == nullptr)
            {
                return binding;
            }

            binding.identity = CanonicalId(resource);
            binding.shape = DescribeResource(resource);

            resource->Release();
            return binding;
        }

        ViewBinding DescribeSrv(
            ID3D11ShaderResourceView* const view)
        {
            if (view == nullptr)
            {
                return {};
            }

            const auto key =
                reinterpret_cast<std::uintptr_t>(view);

            const auto found = g_srvCache.find(key);

            if (found != g_srvCache.end())
            {
                return found->second;
            }

            ViewBinding binding = BaseViewBinding(view);

            D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
            view->GetDesc(&desc);

            switch (desc.ViewDimension)
            {
            case D3D11_SRV_DIMENSION_TEXTURE1D:
                binding.firstMip =
                    static_cast<INT>(
                        desc.Texture1D.MostDetailedMip);
                binding.mipCount =
                    static_cast<INT>(
                        desc.Texture1D.MipLevels);
                binding.firstSlice = 0;
                binding.sliceCount = 1;
                break;

            case D3D11_SRV_DIMENSION_TEXTURE2D:
                binding.firstMip =
                    static_cast<INT>(
                        desc.Texture2D.MostDetailedMip);
                binding.mipCount =
                    static_cast<INT>(
                        desc.Texture2D.MipLevels);
                binding.firstSlice = 0;
                binding.sliceCount = 1;
                break;

            case D3D11_SRV_DIMENSION_TEXTURE2DARRAY:
                binding.firstMip =
                    static_cast<INT>(
                        desc.Texture2DArray.MostDetailedMip);
                binding.mipCount =
                    static_cast<INT>(
                        desc.Texture2DArray.MipLevels);
                binding.firstSlice =
                    static_cast<INT>(
                        desc.Texture2DArray.FirstArraySlice);
                binding.sliceCount =
                    static_cast<INT>(
                        desc.Texture2DArray.ArraySize);
                break;

            case D3D11_SRV_DIMENSION_TEXTURE3D:
                binding.firstMip =
                    static_cast<INT>(
                        desc.Texture3D.MostDetailedMip);
                binding.mipCount =
                    static_cast<INT>(
                        desc.Texture3D.MipLevels);
                binding.firstSlice = 0;
                binding.sliceCount =
                    static_cast<INT>(
                        binding.shape.depth);
                break;

            default:
                break;
            }

            g_srvCache.emplace(key, binding);
            return binding;
        }

        ViewBinding DescribeRtv(
            ID3D11RenderTargetView* const view)
        {
            if (view == nullptr)
            {
                return {};
            }

            const auto key =
                reinterpret_cast<std::uintptr_t>(view);

            const auto found = g_rtvCache.find(key);

            if (found != g_rtvCache.end())
            {
                return found->second;
            }

            ViewBinding binding = BaseViewBinding(view);

            D3D11_RENDER_TARGET_VIEW_DESC desc{};
            view->GetDesc(&desc);

            switch (desc.ViewDimension)
            {
            case D3D11_RTV_DIMENSION_TEXTURE1D:
                binding.firstMip =
                    static_cast<INT>(
                        desc.Texture1D.MipSlice);
                binding.mipCount = 1;
                binding.firstSlice = 0;
                binding.sliceCount = 1;
                break;

            case D3D11_RTV_DIMENSION_TEXTURE2D:
                binding.firstMip =
                    static_cast<INT>(
                        desc.Texture2D.MipSlice);
                binding.mipCount = 1;
                binding.firstSlice = 0;
                binding.sliceCount = 1;
                break;

            case D3D11_RTV_DIMENSION_TEXTURE2DARRAY:
                binding.firstMip =
                    static_cast<INT>(
                        desc.Texture2DArray.MipSlice);
                binding.mipCount = 1;
                binding.firstSlice =
                    static_cast<INT>(
                        desc.Texture2DArray.FirstArraySlice);
                binding.sliceCount =
                    static_cast<INT>(
                        desc.Texture2DArray.ArraySize);
                break;

            case D3D11_RTV_DIMENSION_TEXTURE3D:
                binding.firstMip =
                    static_cast<INT>(
                        desc.Texture3D.MipSlice);
                binding.mipCount = 1;
                binding.firstSlice =
                    static_cast<INT>(
                        desc.Texture3D.FirstWSlice);
                binding.sliceCount =
                    static_cast<INT>(
                        desc.Texture3D.WSize);
                break;

            default:
                break;
            }

            g_rtvCache.emplace(key, binding);
            return binding;
        }

        ViewBinding DescribeDsv(
            ID3D11DepthStencilView* const view)
        {
            if (view == nullptr)
            {
                return {};
            }

            const auto key =
                reinterpret_cast<std::uintptr_t>(view);

            const auto found = g_dsvCache.find(key);

            if (found != g_dsvCache.end())
            {
                return found->second;
            }

            ViewBinding binding = BaseViewBinding(view);

            D3D11_DEPTH_STENCIL_VIEW_DESC desc{};
            view->GetDesc(&desc);

            switch (desc.ViewDimension)
            {
            case D3D11_DSV_DIMENSION_TEXTURE2D:
                binding.firstMip =
                    static_cast<INT>(
                        desc.Texture2D.MipSlice);
                binding.mipCount = 1;
                binding.firstSlice = 0;
                binding.sliceCount = 1;
                break;

            case D3D11_DSV_DIMENSION_TEXTURE2DARRAY:
                binding.firstMip =
                    static_cast<INT>(
                        desc.Texture2DArray.MipSlice);
                binding.mipCount = 1;
                binding.firstSlice =
                    static_cast<INT>(
                        desc.Texture2DArray.FirstArraySlice);
                binding.sliceCount =
                    static_cast<INT>(
                        desc.Texture2DArray.ArraySize);
                break;

            default:
                break;
            }

            g_dsvCache.emplace(key, binding);
            return binding;
        }

        ViewBinding DescribeUav(
            ID3D11UnorderedAccessView* const view)
        {
            if (view == nullptr)
            {
                return {};
            }

            const auto key =
                reinterpret_cast<std::uintptr_t>(view);

            const auto found = g_uavCache.find(key);

            if (found != g_uavCache.end())
            {
                return found->second;
            }

            ViewBinding binding = BaseViewBinding(view);

            D3D11_UNORDERED_ACCESS_VIEW_DESC desc{};
            view->GetDesc(&desc);

            switch (desc.ViewDimension)
            {
            case D3D11_UAV_DIMENSION_TEXTURE1D:
                binding.firstMip =
                    static_cast<INT>(
                        desc.Texture1D.MipSlice);
                binding.mipCount = 1;
                binding.firstSlice = 0;
                binding.sliceCount = 1;
                break;

            case D3D11_UAV_DIMENSION_TEXTURE2D:
                binding.firstMip =
                    static_cast<INT>(
                        desc.Texture2D.MipSlice);
                binding.mipCount = 1;
                binding.firstSlice = 0;
                binding.sliceCount = 1;
                break;

            case D3D11_UAV_DIMENSION_TEXTURE2DARRAY:
                binding.firstMip =
                    static_cast<INT>(
                        desc.Texture2DArray.MipSlice);
                binding.mipCount = 1;
                binding.firstSlice =
                    static_cast<INT>(
                        desc.Texture2DArray.FirstArraySlice);
                binding.sliceCount =
                    static_cast<INT>(
                        desc.Texture2DArray.ArraySize);
                break;

            case D3D11_UAV_DIMENSION_TEXTURE3D:
                binding.firstMip =
                    static_cast<INT>(
                        desc.Texture3D.MipSlice);
                binding.mipCount = 1;
                binding.firstSlice =
                    static_cast<INT>(
                        desc.Texture3D.FirstWSlice);
                binding.sliceCount =
                    static_cast<INT>(
                        desc.Texture3D.WSize);
                break;

            default:
                break;
            }

            g_uavCache.emplace(key, binding);
            return binding;
        }

        bool SameShape(
            const ResourceShape& a,
            const ResourceShape& b)
        {
            return
                a.dimension == b.dimension &&
                a.width == b.width &&
                a.height == b.height &&
                a.depth == b.depth &&
                a.mipLevels == b.mipLevels &&
                a.arraySize == b.arraySize &&
                a.format == b.format;
        }

        bool IsFullResolution(
            const ViewBinding& binding)
        {
            return
                g_screenWidth != 0 &&
                g_screenHeight != 0 &&
                binding.shape.dimension ==
                    D3D11_RESOURCE_DIMENSION_TEXTURE2D &&
                binding.shape.width == g_screenWidth &&
                binding.shape.height == g_screenHeight;
        }

        bool IsFullResolutionFormat(
            const ViewBinding& binding,
            const DXGI_FORMAT format)
        {
            return
                IsFullResolution(binding) &&
                binding.shape.format == format;
        }

        bool IsDepthLike(
            const ViewBinding& binding)
        {
            return
                IsFullResolution(binding) &&
                (
                    binding.shape.format ==
                        DXGI_FORMAT_R24G8_TYPELESS ||
                    binding.shape.format ==
                        DXGI_FORMAT_R32_TYPELESS ||
                    binding.shape.format ==
                        DXGI_FORMAT_R32_FLOAT
                );
        }

        void ResetFrameAvailabilityLocked(
            const std::uint64_t frame)
        {
            for (auto& record : g_semantics)
            {
                auto& availability = record.availability;

                availability.observedThisFrame = false;
                availability.producedThisFrame = false;
                availability.consumedThisFrame = false;
                availability.currentFrameAvailable = false;
                availability.previousFrameAvailable = false;
                availability.frame = frame;
                availability.environmentScope =
                    EnvironmentClass::UnknownTransition;
                availability.validFromStage =
                    stage_scheduler::Boundary::Unknown;
                availability.validUntilStage =
                    stage_scheduler::Boundary::Unknown;
                availability.epochKind =
                    ContentEpochKind::Unknown;
            }
        }

        void EnsureFrameLocked(
            const std::uint64_t frame)
        {
            if (g_frame.frame == frame)
            {
                return;
            }

            g_frame = FrameState{};
            g_frame.frame = frame;

            ResetFrameAvailabilityLocked(frame);
        }

        ResourceInfo MakeResourceInfo(
            const Semantic semantic,
            const Confidence confidence,
            const ViewBinding& binding,
            const std::uint64_t frame,
            const std::uint64_t generation)
        {
            ResourceInfo info{};

            info.semantic = semantic;
            info.confidence = confidence;
            info.identity = binding.identity;
            info.generation = generation;
            info.firstSeenFrame = frame;
            info.lastSeenFrame = frame;

            info.dimension = binding.shape.dimension;
            info.width = binding.shape.width;
            info.height = binding.shape.height;
            info.depth = binding.shape.depth;
            info.mipLevels = binding.shape.mipLevels;
            info.arraySize = binding.shape.arraySize;
            info.format = binding.shape.format;
            info.bindFlags = binding.shape.bindFlags;

            return info;
        }

        bool InCaptureRange(
            const std::uint64_t frame)
        {
            return
                g_captureActive &&
                frame >= g_captureStart &&
                frame <= g_captureEnd;
        }

        bool PromoteIdentityLocked(
            const Semantic semantic,
            const Confidence confidence,
            const ViewBinding& binding,
            const std::uint64_t frame,
            const char* const reason)
        {
            if (binding.identity == 0)
            {
                return false;
            }

            auto& current =
                g_semantics[SemanticIndex(semantic)].resource;

            const bool changed =
                current.identity != binding.identity ||
                static_cast<std::uint32_t>(confidence) >
                    static_cast<std::uint32_t>(
                        current.confidence);

            if (!changed)
            {
                current.lastSeenFrame = frame;
                return false;
            }

            const std::uint64_t generation =
                ++g_generation;

            const std::uint64_t firstSeen =
                current.identity == binding.identity &&
                current.firstSeenFrame != 0
                    ? current.firstSeenFrame
                    : frame;

            current =
                MakeResourceInfo(
                    semantic,
                    confidence,
                    binding,
                    frame,
                    generation);

            current.firstSeenFrame = firstSeen;

            if (InCaptureRange(frame))
            {
                Promotion promotion{};
                promotion.frame = frame;
                promotion.semantic = semantic;
                promotion.confidence = confidence;
                promotion.identity = binding.identity;
                promotion.generation = generation;
                promotion.reason =
                    reason != nullptr ? reason : "";

                g_capturePromotions.push_back(
                    std::move(promotion));
            }

            return true;
        }

        void RecordObservationLocked(
            const Semantic semantic,
            const std::uintptr_t identity,
            const std::uint64_t frame,
            const bool produced,
            const bool consumed,
            const bool currentAvailable,
            const bool previousAvailable,
            const std::uint64_t epoch,
            const char* const reason)
        {
            if (!InCaptureRange(frame))
            {
                return;
            }

            Observation observation{};
            observation.frame = frame;
            observation.semantic = semantic;
            observation.identity = identity;
            observation.produced = produced;
            observation.consumed = consumed;
            observation.currentAvailable = currentAvailable;
            observation.previousAvailable = previousAvailable;
            observation.epoch = epoch;
            observation.reason =
                reason != nullptr ? reason : "";

            g_captureObservations.push_back(
                std::move(observation));
        }

        void MarkSemanticLocked(
            const Semantic semantic,
            const Confidence confidence,
            const ViewBinding& binding,
            const std::uint64_t frame,
            const bool produced,
            const bool consumed,
            const bool currentAvailable,
            const bool previousAvailable,
            const ContentEpochKind epochKind,
            const EnvironmentClass environmentScope,
            const stage_scheduler::Boundary validFrom,
            const stage_scheduler::Boundary validUntil,
            const char* const reason)
        {
            if (binding.identity == 0)
            {
                return;
            }

            PromoteIdentityLocked(
                semantic,
                confidence,
                binding,
                frame,
                reason);

            auto& record =
                g_semantics[SemanticIndex(semantic)];

            auto& availability =
                record.availability;

            const bool firstObservationThisFrame =
                availability.frame != frame ||
                !availability.observedThisFrame;

            if (availability.frame != frame)
            {
                availability.observedThisFrame = false;
                availability.producedThisFrame = false;
                availability.consumedThisFrame = false;
                availability.currentFrameAvailable = false;
                availability.previousFrameAvailable = false;
                availability.frame = frame;
            }

            if (firstObservationThisFrame ||
                availability.epochKind != epochKind)
            {
                availability.contentEpoch =
                    ++g_epochSequence;
            }

            availability.observedThisFrame = true;
            availability.producedThisFrame =
                availability.producedThisFrame || produced;
            availability.consumedThisFrame =
                availability.consumedThisFrame || consumed;
            availability.currentFrameAvailable =
                availability.currentFrameAvailable ||
                currentAvailable;
            availability.previousFrameAvailable =
                availability.previousFrameAvailable ||
                previousAvailable;

            availability.lastObservedFrame = frame;

            if (produced)
            {
                availability.lastProducedFrame = frame;
            }

            if (consumed)
            {
                availability.lastConsumedFrame = frame;
            }

            availability.epochKind = epochKind;
            availability.environmentScope = environmentScope;
            availability.validFromStage = validFrom;
            availability.validUntilStage = validUntil;

            record.resource.lastSeenFrame = frame;

            if (firstObservationThisFrame)
            {
                RecordObservationLocked(
                    semantic,
                    binding.identity,
                    frame,
                    produced,
                    consumed,
                    currentAvailable,
                    previousAvailable,
                    availability.contentEpoch,
                    reason);
            }
        }

        void MarkConsumedLocked(
            const Semantic semantic,
            const std::uint64_t frame)
        {
            auto& record =
                g_semantics[SemanticIndex(semantic)];

            if (record.resource.identity == 0)
            {
                return;
            }

            record.availability.consumedThisFrame = true;
            record.availability.lastConsumedFrame = frame;
        }

        std::uintptr_t ResourceIdLocked(
            const Semantic semantic)
        {
            return
                g_semantics[SemanticIndex(semantic)]
                    .resource.identity;
        }

        ViewBinding BindingFromRecordLocked(
            const Semantic semantic)
        {
            ViewBinding binding{};

            const auto& resource =
                g_semantics[SemanticIndex(semantic)]
                    .resource;

            binding.identity = resource.identity;
            binding.shape.dimension = resource.dimension;
            binding.shape.width = resource.width;
            binding.shape.height = resource.height;
            binding.shape.depth = resource.depth;
            binding.shape.mipLevels = resource.mipLevels;
            binding.shape.arraySize = resource.arraySize;
            binding.shape.format = resource.format;
            binding.shape.bindFlags = resource.bindFlags;

            return binding;
        }

        bool ConflictsWithOutputsLocked(
            const std::uintptr_t identity)
        {
            if (identity == 0)
            {
                return false;
            }

            for (const auto& binding : g_state.rtvs)
            {
                if (binding.identity == identity)
                {
                    return true;
                }
            }

            if (g_state.dsv.identity == identity)
            {
                return true;
            }

            for (const auto& binding : g_state.omUavs)
            {
                if (binding.identity == identity)
                {
                    return true;
                }
            }

            for (const auto& binding : g_state.csUavs)
            {
                if (binding.identity == identity)
                {
                    return true;
                }
            }

            return false;
        }

        void UnbindReadsForOutputLocked(
            const ViewBinding& output)
        {
            if (output.identity == 0)
            {
                return;
            }

            for (auto& stage : g_state.srvs)
            {
                for (auto& binding : stage)
                {
                    if (binding.identity == output.identity)
                    {
                        binding = {};
                    }
                }
            }
        }

        bool ReadsLocked(
            const std::uintptr_t identity)
        {
            if (identity == 0)
            {
                return false;
            }

            for (const auto& stage : g_state.srvs)
            {
                for (const auto& binding : stage)
                {
                    if (binding.identity == identity)
                    {
                        return true;
                    }
                }
            }

            return false;
        }

        ViewBinding FindFullResolutionReadLocked(
            const DXGI_FORMAT format,
            const std::array<std::uintptr_t, 8>& exclusions)
        {
            for (const auto& stage : g_state.srvs)
            {
                for (const auto& binding : stage)
                {
                    if (!IsFullResolutionFormat(
                            binding,
                            format))
                    {
                        continue;
                    }

                    bool excluded = false;

                    for (const auto identity : exclusions)
                    {
                        if (identity != 0 &&
                            binding.identity == identity)
                        {
                            excluded = true;
                            break;
                        }
                    }

                    if (!excluded)
                    {
                        return binding;
                    }
                }
            }

            return {};
        }

        std::vector<ViewBinding>
        CurrentWritesLocked(
            const bool compute)
        {
            std::vector<ViewBinding> writes;

            if (compute)
            {
                for (const auto& binding : g_state.csUavs)
                {
                    if (binding.identity != 0)
                    {
                        writes.push_back(binding);
                    }
                }

                return writes;
            }

            for (const auto& binding : g_state.rtvs)
            {
                if (binding.identity != 0)
                {
                    writes.push_back(binding);
                }
            }

            for (const auto& binding : g_state.omUavs)
            {
                if (binding.identity != 0)
                {
                    writes.push_back(binding);
                }
            }

            return writes;
        }

        bool MainMrtBoundLocked()
        {
            const auto& a = g_state.rtvs[0];
            const auto& b = g_state.rtvs[1];
            const auto& c = g_state.rtvs[2];
            const auto& d = g_state.rtvs[3];
            const auto& depth = g_state.dsv;

            if (a.identity == 0 ||
                b.identity == 0 ||
                c.identity == 0 ||
                d.identity == 0 ||
                depth.identity == 0)
            {
                return false;
            }

            if (a.shape.dimension !=
                    D3D11_RESOURCE_DIMENSION_TEXTURE2D ||
                b.shape.dimension !=
                    D3D11_RESOURCE_DIMENSION_TEXTURE2D ||
                c.shape.dimension !=
                    D3D11_RESOURCE_DIMENSION_TEXTURE2D ||
                d.shape.dimension !=
                    D3D11_RESOURCE_DIMENSION_TEXTURE2D ||
                depth.shape.dimension !=
                    D3D11_RESOURCE_DIMENSION_TEXTURE2D)
            {
                return false;
            }

            if (a.shape.width == 0 ||
                a.shape.height == 0)
            {
                return false;
            }

            const bool sizes =
                b.shape.width == a.shape.width &&
                b.shape.height == a.shape.height &&
                c.shape.width == a.shape.width &&
                c.shape.height == a.shape.height &&
                d.shape.width == a.shape.width &&
                d.shape.height == a.shape.height &&
                depth.shape.width == a.shape.width &&
                depth.shape.height == a.shape.height;

            return
                sizes &&
                a.shape.format ==
                    DXGI_FORMAT_R16G16B16A16_FLOAT &&
                b.shape.format ==
                    DXGI_FORMAT_R16G16_FLOAT &&
                c.shape.format ==
                    DXGI_FORMAT_R8G8B8A8_UNORM &&
                d.shape.format ==
                    DXGI_FORMAT_R8G8_UNORM &&
                depth.shape.format ==
                    DXGI_FORMAT_R24G8_TYPELESS;
        }

        void ObserveMainMrtLocked(
            const std::uint64_t frame)
        {
            if (!MainMrtBoundLocked())
            {
                return;
            }

            g_screenWidth =
                g_state.rtvs[0].shape.width;

            g_screenHeight =
                g_state.rtvs[0].shape.height;

            using B = stage_scheduler::Boundary;

            MarkSemanticLocked(
                Semantic::SceneColorHDR,
                Confidence::Verified,
                g_state.rtvs[0],
                frame,
                true,
                false,
                true,
                false,
                ContentEpochKind::CurrentFrame,
                EnvironmentClass::UnknownTransition,
                B::AfterOpaqueWorldMRT,
                B::UiPresent,
                "four_target_opaque_mrt_rt0");

            MarkSemanticLocked(
                Semantic::MotionVectors,
                Confidence::Verified,
                g_state.rtvs[1],
                frame,
                true,
                false,
                true,
                false,
                ContentEpochKind::CurrentFrame,
                EnvironmentClass::UnknownTransition,
                B::AfterOpaqueWorldMRT,
                B::AfterGlobalTAA,
                "four_target_opaque_mrt_rt1");

            MarkSemanticLocked(
                Semantic::NativeNormalMask,
                Confidence::Verified,
                g_state.rtvs[2],
                frame,
                true,
                false,
                true,
                false,
                ContentEpochKind::CurrentFrame,
                EnvironmentClass::UnknownTransition,
                B::AfterOpaqueWorldMRT,
                B::AfterAmbientScreenSpaceComposite,
                "four_target_opaque_mrt_rt2");

            MarkSemanticLocked(
                Semantic::AuxiliaryRT3,
                Confidence::Verified,
                g_state.rtvs[3],
                frame,
                true,
                false,
                true,
                false,
                ContentEpochKind::CurrentFrame,
                EnvironmentClass::UnknownTransition,
                B::AfterOpaqueWorldMRT,
                B::AfterAmbientScreenSpaceComposite,
                "four_target_opaque_mrt_rt3");

            MarkSemanticLocked(
                Semantic::MainDepthCurrent,
                Confidence::Verified,
                g_state.dsv,
                frame,
                true,
                false,
                true,
                false,
                ContentEpochKind::CurrentFrame,
                EnvironmentClass::UnknownTransition,
                B::AfterOpaqueWorldMRT,
                B::UiPresent,
                "four_target_opaque_mrt_depth");

            g_frame.sawMainMrt = true;
            ++g_frame.mainMrtDraws;
        }

        std::uint64_t RequiredMipMask(
            const UINT mipLevels)
        {
            const UINT count =
                std::min<UINT>(mipLevels, 63);

            if (count == 0)
            {
                return 0;
            }

            return (1ull << count) - 1ull;
        }

        bool IsCameraZCandidate(
            const ViewBinding& output)
        {
            return
                g_screenWidth != 0 &&
                g_screenHeight != 0 &&
                output.identity != 0 &&
                output.shape.dimension ==
                    D3D11_RESOURCE_DIMENSION_TEXTURE2D &&
                output.shape.format ==
                    DXGI_FORMAT_R32_FLOAT &&
                output.shape.width == g_screenWidth &&
                output.shape.height == g_screenHeight &&
                output.shape.mipLevels >= 8 &&
                output.firstMip >= 0 &&
                output.firstMip < 63;
        }

        bool BeginsCameraZLocked(
            const bool compute)
        {
            const auto writes =
                CurrentWritesLocked(compute);

            for (const auto& output : writes)
            {
                if (IsCameraZCandidate(output) &&
                    output.firstMip == 0)
                {
                    return true;
                }
            }

            return false;
        }

        bool ObserveCameraZWriteLocked(
            const ViewBinding& output,
            const std::uint64_t frame)
        {
            if (!IsCameraZCandidate(output))
            {
                return false;
            }

            if (g_frame.cameraZCandidate !=
                output.identity)
            {
                g_frame.cameraZCandidate =
                    output.identity;
                g_frame.cameraZMipMask = 0;
                g_frame.cameraZCompleteSignaled = false;
            }

            g_frame.cameraZMipMask |=
                1ull <<
                static_cast<UINT>(output.firstMip);

            const std::uint64_t required =
                RequiredMipMask(
                    output.shape.mipLevels);

            if (required == 0 ||
                (g_frame.cameraZMipMask & required) != required)
            {
                return false;
            }

            using B = stage_scheduler::Boundary;

            MarkSemanticLocked(
                Semantic::StockCameraZ,
                Confidence::Verified,
                output,
                frame,
                true,
                false,
                true,
                false,
                ContentEpochKind::CurrentFrame,
                EnvironmentClass::UnknownTransition,
                B::AfterCameraZ,
                B::AfterAmbientScreenSpaceComposite,
                "complete_r32_cameraz_mip_hierarchy");

            g_frame.sawCameraZ = true;

            if (g_frame.cameraZCompleteSignaled)
            {
                return false;
            }

            g_frame.cameraZCompleteSignaled = true;
            return true;
        }

        bool IsVolumetricDensity(
            const ViewBinding& output)
        {
            return
                output.identity != 0 &&
                output.shape.dimension ==
                    D3D11_RESOURCE_DIMENSION_TEXTURE3D &&
                output.shape.format ==
                    DXGI_FORMAT_R16_FLOAT &&
                output.shape.width >= 256 &&
                output.shape.height >= 128 &&
                output.shape.depth >= 64;
        }

        void ObserveVolumetricLocked(
            const ViewBinding& output,
            const std::uint64_t frame)
        {
            if (!IsVolumetricDensity(output))
            {
                return;
            }

            g_frame.sawVolumetric = true;

            const auto first =
                ResourceIdLocked(
                    Semantic::VolumetricDensityA);

            const Semantic semantic =
                first == 0 ||
                first == output.identity
                    ? Semantic::VolumetricDensityA
                    : Semantic::VolumetricDensityB;

            using B = stage_scheduler::Boundary;

            MarkSemanticLocked(
                semantic,
                Confidence::Verified,
                output,
                frame,
                true,
                false,
                true,
                false,
                ContentEpochKind::CurrentFrame,
                EnvironmentClass::Exterior,
                B::AfterOpaqueWorldMRT,
                B::AfterVolumetricApply,
                semantic ==
                        Semantic::VolumetricDensityA
                    ? "exterior_3d_r16_density_volume"
                    : "exterior_3d_r16_density_ping_pong");
        }

        bool ObserveRawAOLocked(
            const ViewBinding& output,
            const std::uint64_t frame)
        {
            if (!IsFullResolutionFormat(
                    output,
                    DXGI_FORMAT_R8G8B8A8_UNORM))
            {
                return false;
            }

            const auto cameraZ =
                ResourceIdLocked(
                    Semantic::StockCameraZ);

            const auto motion =
                ResourceIdLocked(
                    Semantic::MotionVectors);

            const auto normal =
                ResourceIdLocked(
                    Semantic::NativeNormalMask);

            if (cameraZ == 0 ||
                motion == 0 ||
                normal == 0 ||
                !ReadsLocked(cameraZ) ||
                !ReadsLocked(motion) ||
                !ReadsLocked(normal))
            {
                return false;
            }

            const std::array<std::uintptr_t, 8> exclusions =
            {
                output.identity,
                cameraZ,
                motion,
                normal,
                ResourceIdLocked(Semantic::AuxiliaryRT3),
                ResourceIdLocked(Semantic::SceneColorHDR),
                ResourceIdLocked(Semantic::AOFilterTemp),
                ResourceIdLocked(Semantic::AOFiltered)
            };

            const auto previous =
                FindFullResolutionReadLocked(
                    DXGI_FORMAT_R8G8B8A8_UNORM,
                    exclusions);

            if (previous.identity == 0)
            {
                return false;
            }

            using B = stage_scheduler::Boundary;

            MarkSemanticLocked(
                Semantic::RawAOPrevious,
                Confidence::Strong,
                previous,
                frame,
                false,
                true,
                false,
                true,
                ContentEpochKind::PreviousFrameHistory,
                EnvironmentClass::UnknownTransition,
                B::AfterCameraZ,
                B::AfterStockAO,
                "cameraz_normal_motion_previous_ao_read");

            MarkSemanticLocked(
                Semantic::RawAOCurrent,
                Confidence::Strong,
                output,
                frame,
                true,
                false,
                true,
                false,
                ContentEpochKind::CurrentFrame,
                EnvironmentClass::UnknownTransition,
                B::AfterCameraZ,
                B::AfterStockAO,
                "cameraz_normal_motion_temporal_ao_write");

            g_frame.sawAO = true;
            g_frame.aoFilterStep = 0;
            g_frame.aoFilterTemp = 0;

            return true;
        }

        bool ObserveAOFilterLocked(
            const ViewBinding& output,
            const std::uint64_t frame,
            std::vector<PendingStage>& stages)
        {
            if (!IsFullResolutionFormat(
                    output,
                    DXGI_FORMAT_R8G8B8A8_UNORM))
            {
                return false;
            }

            const auto raw =
                ResourceIdLocked(
                    Semantic::RawAOCurrent);

            using B = stage_scheduler::Boundary;

            if (g_frame.aoFilterStep == 0 &&
                raw != 0 &&
                ReadsLocked(raw) &&
                output.identity != raw)
            {
                MarkSemanticLocked(
                    Semantic::AOFilterTemp,
                    Confidence::Strong,
                    output,
                    frame,
                    true,
                    false,
                    true,
                    false,
                    ContentEpochKind::CurrentFrame,
                    EnvironmentClass::UnknownTransition,
                    B::AfterCameraZ,
                    B::AfterStockAO,
                    "first_temporal_ao_filter");

                g_frame.aoFilterTemp =
                    output.identity;
                g_frame.aoFilterStep = 1;

                return true;
            }

            if (g_frame.aoFilterStep == 1 &&
                g_frame.aoFilterTemp != 0 &&
                ReadsLocked(g_frame.aoFilterTemp) &&
                output.identity != g_frame.aoFilterTemp)
            {
                MarkSemanticLocked(
                    Semantic::AOFiltered,
                    Confidence::Strong,
                    output,
                    frame,
                    true,
                    false,
                    true,
                    false,
                    ContentEpochKind::CurrentFrame,
                    EnvironmentClass::UnknownTransition,
                    B::AfterStockAO,
                    B::AfterAmbientScreenSpaceComposite,
                    "second_temporal_ao_filter");

                g_frame.aoFilterStep = 2;
                g_frame.sawAO = true;

                stages.push_back(
                    PendingStage{
                        B::AfterStockAO,
                        stage_scheduler::Phase::AfterExecution,
                        "two_pass_temporal_ao_filter_complete"
                    });

                return true;
            }

            return false;
        }

        bool ObserveRawIndirectLocked(
            const ViewBinding& output,
            const std::uint64_t frame)
        {
            if (!IsFullResolutionFormat(
                    output,
                    DXGI_FORMAT_R11G11B10_FLOAT))
            {
                return false;
            }

            const auto cameraZ =
                ResourceIdLocked(
                    Semantic::StockCameraZ);

            const auto scene =
                ResourceIdLocked(
                    Semantic::SceneColorHDR);

            const auto aux =
                ResourceIdLocked(
                    Semantic::AuxiliaryRT3);

            if (cameraZ == 0 ||
                scene == 0 ||
                aux == 0 ||
                !ReadsLocked(cameraZ) ||
                !ReadsLocked(scene) ||
                !ReadsLocked(aux))
            {
                return false;
            }

            using B = stage_scheduler::Boundary;

            MarkSemanticLocked(
                Semantic::RawIndirect,
                Confidence::Strong,
                output,
                frame,
                true,
                false,
                true,
                false,
                ContentEpochKind::CurrentFrame,
                EnvironmentClass::UnknownTransition,
                B::AfterStockAO,
                B::AfterAmbientScreenSpaceComposite,
                "cameraz_hdr_aux_raw_indirect_write");

            return true;
        }

        bool HasDepthArrayReadLocked(
            const UINT minWidth,
            const UINT minArray)
        {
            for (const auto& stage : g_state.srvs)
            {
                for (const auto& binding : stage)
                {
                    if (binding.identity == 0)
                    {
                        continue;
                    }

                    if (binding.shape.dimension ==
                            D3D11_RESOURCE_DIMENSION_TEXTURE2D &&
                        binding.shape.width >= minWidth &&
                        binding.shape.height >= minWidth &&
                        binding.shape.arraySize >= minArray &&
                        (
                            binding.shape.format ==
                                DXGI_FORMAT_R16_TYPELESS ||
                            binding.shape.format ==
                                DXGI_FORMAT_R16_UNORM ||
                            binding.shape.format ==
                                DXGI_FORMAT_R24G8_TYPELESS
                        ))
                    {
                        return true;
                    }
                }
            }

            return false;
        }

        void ObserveShadowMaskLocked(
            const ViewBinding& output,
            const std::uint64_t frame)
        {
            if (!IsFullResolutionFormat(
                    output,
                    DXGI_FORMAT_R8G8B8A8_UNORM))
            {
                return;
            }

            const auto depth =
                ResourceIdLocked(
                    Semantic::MainDepthCurrent);

            if (depth == 0 ||
                !ReadsLocked(depth) ||
                !HasDepthArrayReadLocked(2048, 2))
            {
                return;
            }

            using B = stage_scheduler::Boundary;

            MarkSemanticLocked(
                Semantic::ShadowMask,
                Confidence::Strong,
                output,
                frame,
                true,
                false,
                true,
                false,
                ContentEpochKind::CurrentFrame,
                EnvironmentClass::UnknownTransition,
                B::AfterShadowProduction,
                B::AfterAmbientScreenSpaceComposite,
                "full_resolution_mask_from_depth_and_shadow_arrays");
        }

        bool IsAmbientScreenSpaceCompositeLocked()
        {
            const auto scene =
                ResourceIdLocked(
                    Semantic::SceneColorHDR);

            const auto rawIndirect =
                ResourceIdLocked(
                    Semantic::RawIndirect);

            const auto ao =
                ResourceIdLocked(
                    Semantic::AOFiltered);

            if (scene == 0 ||
                rawIndirect == 0 ||
                ao == 0)
            {
                return false;
            }

            bool writesScene = false;

            for (const auto& output : g_state.rtvs)
            {
                if (output.identity == scene)
                {
                    writesScene = true;
                    break;
                }
            }

            if (!writesScene)
            {
                for (const auto& output : g_state.omUavs)
                {
                    if (output.identity == scene)
                    {
                        writesScene = true;
                        break;
                    }
                }
            }

            return
                writesScene &&
                ReadsLocked(rawIndirect) &&
                ReadsLocked(ao);
        }

        bool IsFullResolutionR16OutputLocked()
        {
            for (const auto& output : g_state.rtvs)
            {
                if (IsFullResolutionFormat(
                        output,
                        DXGI_FORMAT_R16_FLOAT))
                {
                    return true;
                }
            }

            return false;
        }

        bool DetectVolumetricApplyLocked(
            const std::uint64_t frame)
        {
            const auto pixel =
                StageIndex(ShaderStage::Pixel);

            const auto& slots =
                g_state.srvs[pixel];

            const auto mainDepth =
                ResourceIdLocked(
                    Semantic::MainDepthCurrent);

            const auto motion =
                ResourceIdLocked(
                    Semantic::MotionVectors);

            if (mainDepth == 0 ||
                motion == 0 ||
                !IsFullResolutionR16OutputLocked())
            {
                return false;
            }

            const auto& depthCurrent = slots[0];
            const auto& volume = slots[2];
            const auto& repartition = slots[3];
            const auto& motionRead = slots[5];
            const auto& previousDepth = slots[6];

            const bool matches =
                depthCurrent.identity == mainDepth &&
                volume.identity != 0 &&
                IsVolumetricDensity(volume) &&
                repartition.identity != 0 &&
                repartition.shape.dimension ==
                    D3D11_RESOURCE_DIMENSION_TEXTURE1D &&
                repartition.shape.format ==
                    DXGI_FORMAT_R32_FLOAT &&
                motionRead.identity == motion &&
                previousDepth.identity != 0 &&
                previousDepth.identity != mainDepth &&
                IsDepthLike(previousDepth);

            if (!matches)
            {
                return false;
            }

            using B = stage_scheduler::Boundary;

            MarkSemanticLocked(
                Semantic::VolumetricPreviousDepth,
                Confidence::Verified,
                previousDepth,
                frame,
                false,
                true,
                false,
                true,
                ContentEpochKind::PreviousFrameHistory,
                EnvironmentClass::Exterior,
                B::BeforeVolumetricApply,
                B::PublishPreWaterDepth,
                "volumetric_apply_previous_depth_t6");

            g_frame.volumetricApplyPending = true;
            g_frame.volumetricPreviousDepthIdentity =
                previousDepth.identity;
            g_frame.sawVolumetric = true;

            return true;
        }

        void RecordEpochTransitionLocked(
            const std::uint64_t frame,
            const std::uintptr_t identity,
            const Semantic from,
            const Semantic to,
            const std::uint64_t fromEpoch,
            const std::uint64_t toEpoch,
            const char* const event)
        {
            if (!InCaptureRange(frame))
            {
                return;
            }

            EpochTransition transition{};
            transition.frame = frame;
            transition.identity = identity;
            transition.from = from;
            transition.to = to;
            transition.fromEpoch = fromEpoch;
            transition.toEpoch = toEpoch;
            transition.event =
                event != nullptr ? event : "";

            g_captureEpochTransitions.push_back(
                std::move(transition));
        }

        void SignalStages(
            const std::vector<PendingStage>& stages,
            const std::uint64_t frame,
            const std::uint64_t ordinal)
        {
            for (const auto& stage : stages)
            {
                stage_scheduler::Signal(
                    stage.boundary,
                    stage.phase,
                    frame,
                    ordinal,
                    stage.reason);
            }
        }

        std::string PointerText(
            const std::uintptr_t value)
        {
            std::ostringstream stream;
            stream
                << "0x"
                << std::hex
                << std::uppercase
                << value;

            return stream.str();
        }

        const char* BoolText(const bool value)
        {
            return value ? "1" : "0";
        }

        void WriteSnapshotLocked(
            const std::wstring& suffix)
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

            std::ofstream out(
                root /
                    (
                        std::wstring(L"semantic_authority") +
                        suffix +
                        L".txt"
                    ),
                std::ios::out |
                    std::ios::trunc);

            if (!out)
            {
                return;
            }

            out
                << "frame="
                << g_snapshotFrame
                << "\n"
                << "environment="
                << EnvironmentName(g_environment)
                << "\n"
                << "screen="
                << g_screenWidth
                << "x"
                << g_screenHeight
                << "\n"
                << "capture_start="
                << g_captureStart
                << "\n"
                << "capture_end="
                << g_captureEnd
                << "\n\n";

            out
                << "[session_identity_registry]\n"
                << "semantic,confidence,identity,generation,"
                   "first_seen_frame,last_seen_frame,"
                   "dimension,width,height,depth,mips,array_size,"
                   "format,bind_flags\n";

            for (std::size_t i = 0;
                 i < g_semantics.size();
                 ++i)
            {
                const auto& resource =
                    g_semantics[i].resource;

                if (resource.identity == 0)
                {
                    continue;
                }

                out
                    << SemanticName(
                        static_cast<Semantic>(i))
                    << ","
                    << ConfidenceName(resource.confidence)
                    << ","
                    << PointerText(resource.identity)
                    << ","
                    << resource.generation
                    << ","
                    << resource.firstSeenFrame
                    << ","
                    << resource.lastSeenFrame
                    << ","
                    << static_cast<std::uint32_t>(
                        resource.dimension)
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
                    << static_cast<std::uint32_t>(
                        resource.format)
                    << ","
                    << resource.bindFlags
                    << "\n";
            }

            out
                << "\n[current_frame_semantic_views]\n"
                << "semantic,identity,frame,current_available,"
                   "previous_available,observed,produced,consumed,"
                   "last_observed,last_produced,last_consumed,"
                   "epoch,epoch_kind,environment_scope,"
                   "valid_from,valid_until\n";

            for (std::size_t i = 0;
                 i < g_semantics.size();
                 ++i)
            {
                const auto semantic =
                    static_cast<Semantic>(i);

                const auto& record =
                    g_semantics[i];

                if (record.resource.identity == 0)
                {
                    continue;
                }

                const auto& availability =
                    record.availability;

                out
                    << SemanticName(semantic)
                    << ","
                    << PointerText(
                        record.resource.identity)
                    << ","
                    << availability.frame
                    << ","
                    << BoolText(
                        availability.currentFrameAvailable)
                    << ","
                    << BoolText(
                        availability.previousFrameAvailable)
                    << ","
                    << BoolText(
                        availability.observedThisFrame)
                    << ","
                    << BoolText(
                        availability.producedThisFrame)
                    << ","
                    << BoolText(
                        availability.consumedThisFrame)
                    << ","
                    << availability.lastObservedFrame
                    << ","
                    << availability.lastProducedFrame
                    << ","
                    << availability.lastConsumedFrame
                    << ","
                    << availability.contentEpoch
                    << ","
                    << ContentEpochName(
                        availability.epochKind)
                    << ","
                    << EnvironmentName(
                        availability.environmentScope)
                    << ","
                    << stage_scheduler::BoundaryName(
                        availability.validFromStage)
                    << ","
                    << stage_scheduler::BoundaryName(
                        availability.validUntilStage)
                    << "\n";
            }

            out
                << "\n[capture_local_observations]\n"
                << "frame,semantic,identity,produced,consumed,"
                   "current_available,previous_available,epoch,reason\n";

            for (const auto& observation :
                 g_captureObservations)
            {
                out
                    << observation.frame
                    << ","
                    << SemanticName(observation.semantic)
                    << ","
                    << PointerText(observation.identity)
                    << ","
                    << BoolText(observation.produced)
                    << ","
                    << BoolText(observation.consumed)
                    << ","
                    << BoolText(
                        observation.currentAvailable)
                    << ","
                    << BoolText(
                        observation.previousAvailable)
                    << ","
                    << observation.epoch
                    << ","
                    << observation.reason
                    << "\n";
            }

            out
                << "\n[capture_local_promotions]\n"
                << "frame,semantic,confidence,identity,"
                   "generation,reason\n";

            for (const auto& promotion :
                 g_capturePromotions)
            {
                out
                    << promotion.frame
                    << ","
                    << SemanticName(promotion.semantic)
                    << ","
                    << ConfidenceName(
                        promotion.confidence)
                    << ","
                    << PointerText(promotion.identity)
                    << ","
                    << promotion.generation
                    << ","
                    << promotion.reason
                    << "\n";
            }

            out
                << "\n[content_epoch_transitions]\n"
                << "frame,identity,from,to,from_epoch,to_epoch,event\n";

            for (const auto& transition :
                 g_captureEpochTransitions)
            {
                out
                    << transition.frame
                    << ","
                    << PointerText(transition.identity)
                    << ","
                    << SemanticName(transition.from)
                    << ","
                    << SemanticName(transition.to)
                    << ","
                    << transition.fromEpoch
                    << ","
                    << transition.toEpoch
                    << ","
                    << transition.event
                    << "\n";
            }
        }
    }

    const char* SemanticName(
        const Semantic semantic) noexcept
    {
        switch (semantic)
        {
        case Semantic::SceneColorHDR:
            return "MAIN_HDR";
        case Semantic::MainDepthCurrent:
            return "MAIN_DEPTH_CURRENT";
        case Semantic::MotionVectors:
            return "MOTION";
        case Semantic::NativeNormalMask:
            return "NORMAL_MASK_CURRENT";
        case Semantic::AuxiliaryRT3:
            return "AUXILIARY_RT3";
        case Semantic::DepthEarlyCopy:
            return "DEPTH_EARLY_COPY";
        case Semantic::StockCameraZ:
            return "STOCK_CAMERAZ";
        case Semantic::RawAOCurrent:
            return "SAO_RAWAO_CURRENT";
        case Semantic::RawAOPrevious:
            return "SAO_RAWAO_PREVIOUS";
        case Semantic::AOFilterTemp:
            return "SAO_FILTER_TEMP";
        case Semantic::AOFiltered:
            return "SAO_FILTERED";
        case Semantic::RawIndirect:
            return "RAW_INDIRECT";
        case Semantic::ShadowMask:
            return "SHADOW_MASK";
        case Semantic::VolumetricDensityA:
            return "VOLUMETRIC_DENSITY_A";
        case Semantic::VolumetricDensityB:
            return "VOLUMETRIC_DENSITY_B";
        case Semantic::VolumetricPreviousDepth:
            return "VOLUMETRIC_PREVIOUS_DEPTH";
        case Semantic::PreWaterDepthCurrent:
            return "PRE_WATER_DEPTH_CURRENT";
        case Semantic::ReducedReflectionLighting:
            return "REDUCED_REFLECTION_LIGHTING";
        case Semantic::ReducedTemporalLightingHistory:
            return "REDUCED_TEMPORAL_LIGHTING_HISTORY";
        case Semantic::Count:
        default:
            return "UNKNOWN";
        }
    }

    const char* ConfidenceName(
        const Confidence confidence) noexcept
    {
        switch (confidence)
        {
        case Confidence::Verified:
            return "verified";
        case Confidence::Strong:
            return "strong";
        case Confidence::Provisional:
            return "provisional";
        case Confidence::Unknown:
        default:
            return "unknown";
        }
    }

    const char* EnvironmentName(
        const EnvironmentClass environment) noexcept
    {
        switch (environment)
        {
        case EnvironmentClass::Exterior:
            return "exterior";
        case EnvironmentClass::Interior:
            return "interior";
        case EnvironmentClass::UnknownTransition:
        default:
            return "unknown_transition";
        }
    }

    const char* QueryModeName(
        const QueryMode mode) noexcept
    {
        switch (mode)
        {
        case QueryMode::CurrentFrame:
            return "current_frame";
        case QueryMode::PreviousFrameHistory:
            return "previous_frame_history";
        case QueryMode::PersistentIdentity:
            return "persistent_identity";
        case QueryMode::DiagnosticAnyKnown:
            return "diagnostic_any_known";
        default:
            return "unknown";
        }
    }

    const char* ContentEpochName(
        const ContentEpochKind epoch) noexcept
    {
        switch (epoch)
        {
        case ContentEpochKind::CurrentFrame:
            return "current_frame";
        case ContentEpochKind::PreviousFrameHistory:
            return "previous_frame_history";
        case ContentEpochKind::PreWaterCurrent:
            return "pre_water_current";
        case ContentEpochKind::Unknown:
        default:
            return "unknown";
        }
    }

    void SetShader(
        const ShaderStage stage,
        ID3D11DeviceChild* const shader) noexcept
    {
        try
        {
            const auto index = StageIndex(stage);

            if (index >= g_state.shaders.size())
            {
                return;
            }

            std::scoped_lock lock(g_mutex);
            g_state.shaders[index] =
                CanonicalId(shader);
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
            const auto index = StageIndex(stage);

            if (index >= g_state.srvs.size())
            {
                return;
            }

            std::scoped_lock lock(g_mutex);
            auto& slots = g_state.srvs[index];

            for (UINT i = 0;
                 i < count &&
                 startSlot + i < slots.size();
                 ++i)
            {
                const auto binding =
                    DescribeSrv(
                        resources != nullptr
                            ? resources[i]
                            : nullptr);

                slots[startSlot + i] =
                    ConflictsWithOutputsLocked(
                        binding.identity)
                        ? ViewBinding{}
                        : binding;
            }
        }
        catch (...)
        {
        }
    }

    void SetRenderTargets(
        const UINT count,
        ID3D11RenderTargetView* const* renderTargets,
        ID3D11DepthStencilView* const depthTarget) noexcept
    {
        try
        {
            std::scoped_lock lock(g_mutex);

            g_state.rtvs.fill({});
            g_state.omUavs.fill({});

            for (UINT i = 0;
                 i < count &&
                 i < g_state.rtvs.size();
                 ++i)
            {
                g_state.rtvs[i] =
                    DescribeRtv(
                        renderTargets != nullptr
                            ? renderTargets[i]
                            : nullptr);
            }

            g_state.dsv =
                DescribeDsv(depthTarget);

            for (const auto& output : g_state.rtvs)
            {
                UnbindReadsForOutputLocked(output);
            }

            UnbindReadsForOutputLocked(
                g_state.dsv);
        }
        catch (...)
        {
        }
    }

    void SetRenderTargetsAndUavs(
        const UINT renderTargetCount,
        ID3D11RenderTargetView* const* renderTargets,
        ID3D11DepthStencilView* const depthTarget,
        const UINT uavStartSlot,
        const UINT uavCount,
        ID3D11UnorderedAccessView* const* uavs) noexcept
    {
        try
        {
            std::scoped_lock lock(g_mutex);

            if (renderTargetCount !=
                D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL)
            {
                g_state.rtvs.fill({});

                for (UINT i = 0;
                     i < renderTargetCount &&
                     i < g_state.rtvs.size();
                     ++i)
                {
                    g_state.rtvs[i] =
                        DescribeRtv(
                            renderTargets != nullptr
                                ? renderTargets[i]
                                : nullptr);
                }

                g_state.dsv =
                    DescribeDsv(depthTarget);

                for (const auto& output :
                     g_state.rtvs)
                {
                    UnbindReadsForOutputLocked(
                        output);
                }

                UnbindReadsForOutputLocked(
                    g_state.dsv);
            }

            if (uavStartSlot !=
                D3D11_KEEP_UNORDERED_ACCESS_VIEWS)
            {
                for (UINT i = 0;
                     i < uavCount &&
                     uavStartSlot + i <
                        g_state.omUavs.size();
                     ++i)
                {
                    g_state.omUavs[
                        uavStartSlot + i] =
                        DescribeUav(
                            uavs != nullptr
                                ? uavs[i]
                                : nullptr);

                    UnbindReadsForOutputLocked(
                        g_state.omUavs[
                            uavStartSlot + i]);
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
            std::scoped_lock lock(g_mutex);

            for (UINT i = 0;
                 i < count &&
                 startSlot + i <
                    g_state.csUavs.size();
                 ++i)
            {
                g_state.csUavs[
                    startSlot + i] =
                    DescribeUav(
                        uavs != nullptr
                            ? uavs[i]
                            : nullptr);

                UnbindReadsForOutputLocked(
                    g_state.csUavs[
                        startSlot + i]);
            }
        }
        catch (...)
        {
        }
    }

    void SetViewports(
        const UINT count,
        const D3D11_VIEWPORT* const viewports) noexcept
    {
        try
        {
            std::scoped_lock lock(g_mutex);

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
            std::scoped_lock lock(g_mutex);
            g_state = CurrentState{};
        }
        catch (...)
        {
        }
    }

    void BeginDraw(
        const std::uint64_t frame,
        const std::uint64_t ordinal) noexcept
    {
        try
        {
            std::vector<PendingStage> stages;

            {
                std::scoped_lock lock(g_mutex);
                EnsureFrameLocked(frame);

                if (g_frame.sawMainMrt &&
                    !g_frame.opaqueWorldMrtSignaled &&
                    BeginsCameraZLocked(false))
                {
                    g_frame.opaqueWorldMrtSignaled = true;

                    stages.push_back(
                        PendingStage{
                            stage_scheduler::Boundary::
                                AfterOpaqueWorldMRT,
                            stage_scheduler::Phase::
                                Landmark,
                            "main_opaque_mrt_complete_before_cameraz"
                        });
                }

                if (IsAmbientScreenSpaceCompositeLocked())
                {
                    g_frame.ambientCompositePending =
                        true;
                    g_frame.ambientCompositeOrdinal =
                        ordinal;

                    MarkConsumedLocked(
                        Semantic::RawIndirect,
                        frame);

                    MarkConsumedLocked(
                        Semantic::AOFiltered,
                        frame);

                    stages.push_back(
                        PendingStage{
                            stage_scheduler::Boundary::
                                BeforeAmbientScreenSpaceComposite,
                            stage_scheduler::Phase::
                                BeforeExecution,
                            "raw_indirect_and_filtered_ao_to_main_hdr"
                        });

                    stages.push_back(
                        PendingStage{
                            stage_scheduler::Boundary::
                                AtAmbientScreenSpaceComposite,
                            stage_scheduler::Phase::
                                BeforeExecution,
                            "ambient_screen_space_composite_execution"
                        });
                }

                if (DetectVolumetricApplyLocked(frame))
                {
                    g_frame.volumetricApplyOrdinal =
                        ordinal;

                    stages.push_back(
                        PendingStage{
                            stage_scheduler::Boundary::
                                BeforeVolumetricApply,
                            stage_scheduler::Phase::
                                BeforeExecution,
                            "volumetric_temporal_apply_with_previous_depth"
                        });
                }
            }

            SignalStages(stages, frame, ordinal);
        }
        catch (...)
        {
        }
    }

    void EndDraw(
        const std::uint64_t frame,
        const std::uint64_t ordinal) noexcept
    {
        try
        {
            std::vector<PendingStage> stages;

            {
                std::scoped_lock lock(g_mutex);
                EnsureFrameLocked(frame);

                ObserveMainMrtLocked(frame);

                const auto writes =
                    CurrentWritesLocked(false);

                bool cameraZCompleted = false;

                for (const auto& output : writes)
                {
                    if (ObserveCameraZWriteLocked(
                            output,
                            frame))
                    {
                        cameraZCompleted = true;
                    }
                }

                if (cameraZCompleted)
                {
                    stages.push_back(
                        PendingStage{
                            stage_scheduler::Boundary::
                                AfterCameraZ,
                            stage_scheduler::Phase::
                                AfterExecution,
                            "cameraz_hierarchy_complete_this_frame"
                        });
                }

                bool rawAo = false;

                for (const auto& output : writes)
                {
                    if (ObserveRawAOLocked(
                            output,
                            frame))
                    {
                        rawAo = true;
                        break;
                    }
                }

                if (!rawAo)
                {
                    for (const auto& output : writes)
                    {
                        if (ObserveAOFilterLocked(
                                output,
                                frame,
                                stages))
                        {
                            break;
                        }
                    }
                }

                for (const auto& output : writes)
                {
                    ObserveRawIndirectLocked(
                        output,
                        frame);

                    ObserveShadowMaskLocked(
                        output,
                        frame);
                }

                if (g_frame.ambientCompositePending &&
                    g_frame.ambientCompositeOrdinal ==
                        ordinal)
                {
                    const auto sceneBinding =
                        BindingFromRecordLocked(
                            Semantic::SceneColorHDR);

                    if (sceneBinding.identity != 0)
                    {
                        MarkSemanticLocked(
                            Semantic::SceneColorHDR,
                            Confidence::Verified,
                            sceneBinding,
                            frame,
                            true,
                            false,
                            true,
                            false,
                            ContentEpochKind::CurrentFrame,
                            EnvironmentClass::UnknownTransition,
                            stage_scheduler::Boundary::
                                AfterAmbientScreenSpaceComposite,
                            stage_scheduler::Boundary::
                                UiPresent,
                            "ambient_screen_space_composite_writeback");
                    }

                    stages.push_back(
                        PendingStage{
                            stage_scheduler::Boundary::
                                AfterAmbientScreenSpaceComposite,
                            stage_scheduler::Phase::
                                AfterExecution,
                            "ambient_screen_space_composite_complete"
                        });

                    g_frame.ambientCompositePending =
                        false;
                }

                if (g_frame.volumetricApplyPending &&
                    g_frame.volumetricApplyOrdinal ==
                        ordinal)
                {
                    stages.push_back(
                        PendingStage{
                            stage_scheduler::Boundary::
                                AfterVolumetricApply,
                            stage_scheduler::Phase::
                                AfterExecution,
                            "volumetric_temporal_apply_complete"
                        });

                    g_frame.volumetricApplyPending =
                        false;
                }
            }

            SignalStages(stages, frame, ordinal);
        }
        catch (...)
        {
        }
    }

    void BeginDispatch(
        const std::uint64_t frame,
        const std::uint64_t ordinal) noexcept
    {
        try
        {
            std::vector<PendingStage> stages;

            {
                std::scoped_lock lock(g_mutex);
                EnsureFrameLocked(frame);

                if (g_frame.sawMainMrt &&
                    !g_frame.opaqueWorldMrtSignaled &&
                    BeginsCameraZLocked(true))
                {
                    g_frame.opaqueWorldMrtSignaled = true;

                    stages.push_back(
                        PendingStage{
                            stage_scheduler::Boundary::
                                AfterOpaqueWorldMRT,
                            stage_scheduler::Phase::
                                Landmark,
                            "main_opaque_mrt_complete_before_compute_cameraz"
                        });
                }
            }

            SignalStages(stages, frame, ordinal);
        }
        catch (...)
        {
        }
    }

    void EndDispatch(
        const std::uint64_t frame,
        const std::uint64_t ordinal) noexcept
    {
        try
        {
            std::vector<PendingStage> stages;

            {
                std::scoped_lock lock(g_mutex);
                EnsureFrameLocked(frame);

                const auto writes =
                    CurrentWritesLocked(true);

                bool cameraZCompleted = false;

                for (const auto& output : writes)
                {
                    if (ObserveCameraZWriteLocked(
                            output,
                            frame))
                    {
                        cameraZCompleted = true;
                    }

                    ObserveVolumetricLocked(
                        output,
                        frame);

                    ObserveRawIndirectLocked(
                        output,
                        frame);
                }

                if (cameraZCompleted)
                {
                    stages.push_back(
                        PendingStage{
                            stage_scheduler::Boundary::
                                AfterCameraZ,
                            stage_scheduler::Phase::
                                AfterExecution,
                            "compute_cameraz_hierarchy_complete_this_frame"
                        });
                }

                bool rawAo = false;

                for (const auto& output : writes)
                {
                    if (ObserveRawAOLocked(
                            output,
                            frame))
                    {
                        rawAo = true;
                        break;
                    }
                }

                if (!rawAo)
                {
                    for (const auto& output : writes)
                    {
                        if (ObserveAOFilterLocked(
                                output,
                                frame,
                                stages))
                        {
                            break;
                        }
                    }
                }
            }

            SignalStages(stages, frame, ordinal);
        }
        catch (...)
        {
        }
    }

    void AfterCopy(
        const std::uint64_t frame,
        const std::uint64_t ordinal,
        ID3D11Resource* const destination,
        ID3D11Resource* const source) noexcept
    {
        try
        {
            if (destination == nullptr ||
                source == nullptr)
            {
                return;
            }

            std::vector<PendingStage> stages;

            {
                std::scoped_lock lock(g_mutex);
                EnsureFrameLocked(frame);

                const auto sourceId =
                    CanonicalId(source);

                const auto mainDepth =
                    ResourceIdLocked(
                        Semantic::MainDepthCurrent);

                if (mainDepth == 0 ||
                    sourceId != mainDepth)
                {
                    return;
                }

                ViewBinding destinationBinding{};
                destinationBinding.identity =
                    CanonicalId(destination);
                destinationBinding.shape =
                    DescribeResource(destination);

                const auto mainBinding =
                    BindingFromRecordLocked(
                        Semantic::MainDepthCurrent);

                if (!SameShape(
                        destinationBinding.shape,
                        mainBinding.shape))
                {
                    return;
                }

                using B = stage_scheduler::Boundary;

                if (g_frame.mainMrtDraws == 0 &&
                    !g_frame.earlyDepthCopySeen)
                {
                    MarkSemanticLocked(
                        Semantic::DepthEarlyCopy,
                        Confidence::Strong,
                        destinationBinding,
                        frame,
                        true,
                        false,
                        true,
                        false,
                        ContentEpochKind::CurrentFrame,
                        EnvironmentClass::UnknownTransition,
                        B::AfterDepthPrepass,
                        B::AfterCameraZ,
                        "main_depth_copy_before_opaque_mrt");

                    g_frame.earlyDepthCopySeen = true;

                    stages.push_back(
                        PendingStage{
                            B::AfterDepthPrepass,
                            stage_scheduler::Phase::
                                AfterExecution,
                            "early_main_depth_copy_complete"
                        });
                }
                else if (
                    g_frame.volumetricPreviousDepthIdentity != 0 &&
                    destinationBinding.identity ==
                        g_frame.volumetricPreviousDepthIdentity &&
                    !g_frame.preWaterDepthPublished)
                {
                    auto& previous =
                        g_semantics[
                            SemanticIndex(
                                Semantic::VolumetricPreviousDepth)];

                    const std::uint64_t fromEpoch =
                        previous.availability.contentEpoch;

                    previous.availability.previousFrameAvailable =
                        false;
                    previous.availability.currentFrameAvailable =
                        false;
                    previous.availability.validUntilStage =
                        B::AfterVolumetricApply;

                    MarkSemanticLocked(
                        Semantic::PreWaterDepthCurrent,
                        Confidence::Verified,
                        destinationBinding,
                        frame,
                        true,
                        false,
                        true,
                        false,
                        ContentEpochKind::PreWaterCurrent,
                        EnvironmentClass::Exterior,
                        B::PublishPreWaterDepth,
                        B::UiPresent,
                        "main_depth_publish_to_pre_water_depth");

                    const auto& current =
                        g_semantics[
                            SemanticIndex(
                                Semantic::PreWaterDepthCurrent)];

                    RecordEpochTransitionLocked(
                        frame,
                        destinationBinding.identity,
                        Semantic::VolumetricPreviousDepth,
                        Semantic::PreWaterDepthCurrent,
                        fromEpoch,
                        current.availability.contentEpoch,
                        "MAIN_DEPTH_COPY");

                    g_frame.preWaterDepthPublished = true;

                    stages.push_back(
                        PendingStage{
                            B::PublishPreWaterDepth,
                            stage_scheduler::Phase::
                                AfterExecution,
                            "volumetric_previous_depth_to_pre_water_depth"
                        });
                }
            }

            SignalStages(stages, frame, ordinal);
        }
        catch (...)
        {
        }
    }

    void OnPresent(
        const std::uint64_t frame,
        const std::uint64_t drawIndexedActivity) noexcept
    {
        try
        {
            EnvironmentClass environment =
                EnvironmentClass::UnknownTransition;

            {
                std::scoped_lock lock(g_mutex);
                EnsureFrameLocked(frame);

                if (g_frame.sawVolumetric)
                {
                    environment =
                        EnvironmentClass::Exterior;
                }
                else if (
                    g_frame.sawMainMrt &&
                    g_frame.sawCameraZ &&
                    g_frame.sawAO &&
                    drawIndexedActivity != 0)
                {
                    environment =
                        EnvironmentClass::Interior;
                }

                g_environment = environment;
                g_snapshotFrame = frame;
            }

            stage_scheduler::Signal(
                stage_scheduler::Boundary::UiPresent,
                stage_scheduler::Phase::Landmark,
                frame,
                0,
                "frame_present");
        }
        catch (...)
        {
        }
    }

    void OnResize() noexcept
    {
        try
        {
            std::scoped_lock lock(g_mutex);

            g_state = CurrentState{};
            g_frame = FrameState{};
            g_semantics = {};
            g_environment =
                EnvironmentClass::UnknownTransition;

            g_screenWidth = 0;
            g_screenHeight = 0;
            g_generation = 0;
            g_epochSequence = 0;
            g_snapshotFrame = 0;

            g_srvCache.clear();
            g_rtvCache.clear();
            g_dsvCache.clear();
            g_uavCache.clear();
        }
        catch (...)
        {
        }

        stage_scheduler::Reset();
    }

    void BeginEvidenceCapture(
        const std::uint64_t startFrame,
        const std::uint64_t endFrame) noexcept
    {
        try
        {
            {
                std::scoped_lock lock(g_mutex);

                g_captureStart = startFrame;
                g_captureEnd = endFrame;
                g_capturePromotions.clear();
                g_captureObservations.clear();
                g_captureEpochTransitions.clear();
                g_captureActive = true;
            }

            stage_scheduler::BeginEvidenceCapture(
                startFrame,
                endFrame);
        }
        catch (...)
        {
        }
    }

    void FinishEvidenceCapture(
        const std::wstring& suffix) noexcept
    {
        try
        {
            {
                std::scoped_lock lock(g_mutex);

                if (!g_captureActive)
                {
                    return;
                }

                WriteSnapshotLocked(suffix);

                g_captureActive = false;
                g_capturePromotions.clear();
                g_captureObservations.clear();
                g_captureEpochTransitions.clear();
            }

            stage_scheduler::FinishEvidenceCapture(
                suffix);
        }
        catch (...)
        {
        }
    }

    bool QuerySemantic(
        const Semantic semantic,
        const QueryMode mode,
        const std::uint64_t frame,
        const stage_scheduler::Boundary atStage,
        SemanticView* const outView) noexcept
    {
        if (outView == nullptr ||
            semantic == Semantic::Count)
        {
            return false;
        }

        try
        {
            std::scoped_lock lock(g_mutex);

            const auto& record =
                g_semantics[SemanticIndex(semantic)];

            SemanticView view{};
            view.semantic = semantic;
            view.queryMode = mode;
            view.resource = record.resource;
            view.availability = record.availability;

            bool available =
                record.resource.identity != 0;

            switch (mode)
            {
            case QueryMode::CurrentFrame:
                available =
                    available &&
                    record.availability.frame == frame &&
                    record.availability.currentFrameAvailable &&
                    StageWithinValidity(
                        record.availability,
                        atStage);
                break;

            case QueryMode::PreviousFrameHistory:
                available =
                    available &&
                    record.availability.frame == frame &&
                    record.availability.previousFrameAvailable &&
                    StageWithinValidity(
                        record.availability,
                        atStage);
                break;

            case QueryMode::PersistentIdentity:
            case QueryMode::DiagnosticAnyKnown:
                break;

            default:
                available = false;
                break;
            }

            if (available &&
                record.availability.environmentScope !=
                    EnvironmentClass::UnknownTransition &&
                g_snapshotFrame == frame &&
                g_environment !=
                    EnvironmentClass::UnknownTransition &&
                g_environment !=
                    record.availability.environmentScope)
            {
                available = false;
            }

            view.available = available;
            *outView = view;

            return available;
        }
        catch (...)
        {
            *outView = {};
            return false;
        }
    }

    Snapshot GetSnapshot() noexcept
    {
        Snapshot snapshot{};

        try
        {
            std::scoped_lock lock(g_mutex);

            snapshot.frame = g_snapshotFrame;
            snapshot.environment = g_environment;
            snapshot.screenWidth = g_screenWidth;
            snapshot.screenHeight = g_screenHeight;

            for (std::size_t i = 0;
                 i < g_semantics.size();
                 ++i)
            {
                snapshot.resources[i] =
                    g_semantics[i].resource;

                snapshot.availability[i] =
                    g_semantics[i].availability;
            }
        }
        catch (...)
        {
        }

        return snapshot;
    }
}

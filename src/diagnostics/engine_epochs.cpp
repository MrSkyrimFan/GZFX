#include "gzfx/diagnostics/engine_epochs.hpp"

#include "gzfx/diagnostics/renderer_forensics.hpp"
#include "gzfx/diagnostics/telemetry_listener.hpp"
#include "gzfx/paths.hpp"

#include <Windows.h>
#include <MinHook.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace gzfx::diagnostics::engine_epochs
{
    namespace fs = std::filesystem;

    namespace
    {
        constexpr std::uint64_t kAe1170Profile = 0x0001000604920000ull;

        constexpr std::uint32_t kRuntimeKnown = 1u << 0;
        constexpr std::uint32_t kEngineProfile = 1u << 1;
        constexpr std::uint64_t kPostProcessingExpectedRva = 0x1481B80ull;
        constexpr std::uint64_t kPreUiExpectedRva = 0xE44850ull;

        constexpr std::array<std::uint8_t, 24> kPostProcessingSignature =
        {
            0x00, 0x48, 0x8B, 0x0D, 0x29, 0xBD, 0xE5, 0x01,
            0xE8, 0x04, 0x32, 0xFB, 0xFF, 0x48, 0x8B, 0x05,
            0x65, 0xDF, 0xEB, 0x01, 0x48, 0x8B, 0x88, 0x28
        };

        constexpr std::array<std::uint8_t, 24> kPreUiSignature =
        {
            0xD2, 0x48, 0x8D, 0x0D, 0x46, 0x37, 0x2E, 0x02,
            0xE8, 0xD1, 0xF7, 0xE9, 0xFF, 0x48, 0x8B, 0x05,
            0x42, 0xC1, 0x64, 0x02, 0x48, 0x8B, 0x48, 0x10
        };

        using PostProcessingFn =
            void(*)(void*, std::uint32_t, std::uint32_t, void*, bool);
        using PreUiFn =
            void(*)(std::int64_t, int);

        PostProcessingFn g_postProcessing = nullptr;
        PreUiFn g_preUi = nullptr;

        std::once_flag g_once;
        bool g_armResult = false;
        Status g_status{};

        std::atomic<std::uint64_t> g_postEvents{0};
        std::atomic<std::uint64_t> g_preUiEvents{0};

        struct ImageInfo
        {
            std::uint8_t* base = nullptr;
            std::size_t imageSize = 0;
            std::uint8_t* text = nullptr;
            std::size_t textSize = 0;
        };

        std::uint32_t ClassifyRuntimeImpl(
            const std::uint16_t major,
            const std::uint16_t minor,
            const std::uint16_t patch,
            const std::uint16_t build) noexcept
        {
            if (major != 1 ||
                build != 0)
            {
                return 0;
            }

            bool known = false;

            if (minor == 5)
            {
                known =
                    patch == 97;
            }
            else if (minor == 6)
            {
                switch (patch)
                {
                case 318:
                case 323:
                case 342:
                case 353:
                case 629:
                case 640:
                case 659:
                case 1130:
                case 1170:
                case 1179:
                    known = true;
                    break;
                default:
                    break;
                }
            }
            else if (minor == 7)
            {
                known =
                    patch == 99 ||
                    patch == 104;
            }

            if (!known)
            {
                return 0;
            }

            const bool engineProfile =
                minor == 6 &&
                patch == 1170;

            return
                kRuntimeKnown |
                (
                    engineProfile
                        ? kEngineProfile
                        : 0u
                );
        }

        std::string Hex(const std::uint64_t value)
        {
            std::ostringstream out;
            out << "0x" << std::hex << std::uppercase << value;
            return out.str();
        }

        void WriteStatus(const std::string& detail)
        {
            const fs::path root = paths::DiagnosticsRoot();
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
                root / L"engine_epochs.txt",
                std::ios::out | std::ios::trunc);

            if (!out)
            {
                return;
            }

            out
                << "GZFX Skyrim engine epoch resolver\n"
                << "runtime_profile=" << Hex(g_status.runtimeProfile) << "\n"
                << "renderer_core_supported="
                << (g_status.rendererCoreSupported ? "true" : "false") << "\n"
                << "runtime_known="
                << (g_status.runtimeKnown ? "true" : "false") << "\n"
                << "engine_profile_available="
                << (g_status.engineProfileAvailable ? "true" : "false") << "\n"
                << "post_processing_armed=" << (g_status.postProcessingArmed ? "true" : "false") << "\n"
                << "post_processing_target_rva=" << Hex(g_status.postProcessingTargetRva) << "\n"
                << "pre_ui_armed=" << (g_status.preUiArmed ? "true" : "false") << "\n"
                << "pre_ui_target_rva=" << Hex(g_status.preUiTargetRva) << "\n"
                << "detail=" << detail << "\n";
        }

        bool ExecutableAddress(const void* address)
        {
            MEMORY_BASIC_INFORMATION info{};
            if (VirtualQuery(
                    address,
                    &info,
                    sizeof(info)) == 0)
            {
                return false;
            }

            const DWORD protection =
                info.Protect & 0xFFu;

            return
                protection == PAGE_EXECUTE ||
                protection == PAGE_EXECUTE_READ ||
                protection == PAGE_EXECUTE_READWRITE ||
                protection == PAGE_EXECUTE_WRITECOPY;
        }

        bool UnsupportedEntryRedirect(const std::uint8_t* target)
        {
            if (target == nullptr)
            {
                return true;
            }

            if (target[0] == 0xE9 ||
                target[0] == 0xEB)
            {
                return true;
            }

            return
                target[0] == 0xFF &&
                target[1] == 0x25;
        }

        ImageInfo MainImage()
        {
            ImageInfo result{};
            auto* base =
                reinterpret_cast<std::uint8_t*>(
                    GetModuleHandleW(nullptr));

            if (base == nullptr)
            {
                return result;
            }

            auto* dos =
                reinterpret_cast<IMAGE_DOS_HEADER*>(base);

            if (dos->e_magic != IMAGE_DOS_SIGNATURE)
            {
                return result;
            }

            auto* nt =
                reinterpret_cast<IMAGE_NT_HEADERS64*>(
                    base + dos->e_lfanew);

            if (nt->Signature != IMAGE_NT_SIGNATURE ||
                nt->OptionalHeader.Magic !=
                    IMAGE_NT_OPTIONAL_HDR64_MAGIC)
            {
                return result;
            }

            result.base = base;
            result.imageSize =
                nt->OptionalHeader.SizeOfImage;

            auto* section =
                IMAGE_FIRST_SECTION(nt);

            for (WORD i = 0;
                 i < nt->FileHeader.NumberOfSections;
                 ++i, ++section)
            {
                char name[9]{};
                std::memcpy(
                    name,
                    section->Name,
                    8);

                if (std::strcmp(name, ".text") == 0)
                {
                    result.text =
                        base +
                        section->VirtualAddress;

                    result.textSize =
                        static_cast<std::size_t>(
                            section->Misc.VirtualSize);

                    break;
                }
            }

            return result;
        }

        bool FileVersion(
            std::uint16_t& major,
            std::uint16_t& minor,
            std::uint16_t& patch,
            std::uint16_t& build)
        {
            wchar_t path[MAX_PATH]{};
            if (GetModuleFileNameW(
                    nullptr,
                    path,
                    static_cast<DWORD>(std::size(path))) == 0)
            {
                return false;
            }

            DWORD ignored = 0;
            const DWORD bytes =
                GetFileVersionInfoSizeW(
                    path,
                    &ignored);

            if (bytes == 0)
            {
                return false;
            }

            std::vector<std::uint8_t> buffer(bytes);

            if (!GetFileVersionInfoW(
                    path,
                    0,
                    bytes,
                    buffer.data()))
            {
                return false;
            }

            VS_FIXEDFILEINFO* info = nullptr;
            UINT infoBytes = 0;

            if (!VerQueryValueW(
                    buffer.data(),
                    L"\\",
                    reinterpret_cast<void**>(&info),
                    &infoBytes) ||
                info == nullptr ||
                infoBytes < sizeof(VS_FIXEDFILEINFO))
            {
                return false;
            }

            major =
                HIWORD(info->dwFileVersionMS);
            minor =
                LOWORD(info->dwFileVersionMS);
            patch =
                HIWORD(info->dwFileVersionLS);
            build =
                LOWORD(info->dwFileVersionLS);

            return true;
        }

        std::uint64_t EncodeVersion(
            const std::uint16_t major,
            const std::uint16_t minor,
            const std::uint16_t patch,
            const std::uint16_t build)
        {
            return
                (static_cast<std::uint64_t>(major) << 48) |
                (static_cast<std::uint64_t>(minor) << 32) |
                (static_cast<std::uint64_t>(patch) << 16) |
                build;
        }

        template <std::size_t N>
        std::vector<std::uint8_t*> FindExact(
            const ImageInfo& image,
            const std::array<std::uint8_t, N>& signature)
        {
            std::vector<std::uint8_t*> matches;

            if (image.text == nullptr ||
                image.textSize < N)
            {
                return matches;
            }

            for (std::size_t i = 0;
                 i + N <= image.textSize;
                 ++i)
            {
                if (std::memcmp(
                        image.text + i,
                        signature.data(),
                        N) == 0)
                {
                    matches.push_back(
                        image.text + i);
                }
            }

            return matches;
        }

        std::uint8_t* DecodeCallTarget(
            std::uint8_t* const call)
        {
            if (call == nullptr ||
                call[0] != 0xE8)
            {
                return nullptr;
            }

            std::int32_t displacement = 0;
            std::memcpy(
                &displacement,
                call + 1,
                sizeof(displacement));

            return
                call + 5 +
                displacement;
        }

        template <std::size_t N>
        bool ResolveAnchor(
            const ImageInfo& image,
            const std::array<std::uint8_t, N>& signature,
            const std::uint64_t expectedTargetRva,
            void** outTarget,
            std::uint64_t* outRva)
        {
            if (outTarget == nullptr ||
                outRva == nullptr)
            {
                return false;
            }

            *outTarget = nullptr;
            *outRva = 0;

            const auto matches =
                FindExact(
                    image,
                    signature);

            if (matches.size() != 1)
            {
                return false;
            }

            auto* const call =
                matches.front() + 8;

            auto* const target =
                DecodeCallTarget(call);

            if (target == nullptr ||
                target < image.base ||
                target >= image.base + image.imageSize ||
                !ExecutableAddress(target))
            {
                return false;
            }

            const auto rva =
                static_cast<std::uint64_t>(
                    target - image.base);

            if (rva != expectedTargetRva ||
                UnsupportedEntryRedirect(target))
            {
                return false;
            }

            *outTarget = target;
            *outRva = rva;
            return true;
        }

        void PostProcessingThunk(
            void* manager,
            const std::uint32_t unknown,
            const std::uint32_t target,
            void* data,
            const bool flag)
        {
            g_postEvents.fetch_add(
                1,
                std::memory_order_relaxed);

            telemetry_listener::OnEngineEvent(
                CurrentRendererFrame(),
                telemetry_listener::EngineAnchor::PostProcessing,
                telemetry_listener::EnginePhase::Enter,
                GetCurrentThreadId(),
                kAe1170Profile,
                kPostProcessingExpectedRva);

            g_postProcessing(
                manager,
                unknown,
                target,
                data,
                flag);

            telemetry_listener::OnEngineEvent(
                CurrentRendererFrame(),
                telemetry_listener::EngineAnchor::PostProcessing,
                telemetry_listener::EnginePhase::Exit,
                GetCurrentThreadId(),
                kAe1170Profile,
                kPostProcessingExpectedRva);
        }

        void PreUiThunk(
            const std::int64_t renderer,
            const int unknown)
        {
            g_preUiEvents.fetch_add(
                1,
                std::memory_order_relaxed);

            telemetry_listener::OnEngineEvent(
                CurrentRendererFrame(),
                telemetry_listener::EngineAnchor::PreUi,
                telemetry_listener::EnginePhase::Enter,
                GetCurrentThreadId(),
                kAe1170Profile,
                kPreUiExpectedRva);

            g_preUi(
                renderer,
                unknown);

            telemetry_listener::OnEngineEvent(
                CurrentRendererFrame(),
                telemetry_listener::EngineAnchor::PreUi,
                telemetry_listener::EnginePhase::Exit,
                GetCurrentThreadId(),
                kAe1170Profile,
                kPreUiExpectedRva);
        }

        void ArmImpl()
        {
            std::uint16_t major = 0;
            std::uint16_t minor = 0;
            std::uint16_t patch = 0;
            std::uint16_t build = 0;

            if (!FileVersion(
                    major,
                    minor,
                    patch,
                    build))
            {
                WriteStatus("file_version_unavailable");
                return;
            }

            g_status.runtimeProfile =
                EncodeVersion(
                    major,
                    minor,
                    patch,
                    build);

            const std::uint32_t runtimeClass =
                ClassifyRuntimeImpl(
                    major,
                    minor,
                    patch,
                    build);

            g_status.runtimeKnown =
                (runtimeClass &
                 kRuntimeKnown) != 0;

            g_status.engineProfileAvailable =
                (runtimeClass &
                 kEngineProfile) != 0;

            if (!g_status.engineProfileAvailable)
            {
                WriteStatus(
                    g_status.runtimeKnown
                        ? "known_runtime_engine_profile_unavailable"
                        : "unknown_runtime_engine_profile_unavailable");
                return;
            }

            const ImageInfo image =
                MainImage();

            if (image.base == nullptr ||
                image.text == nullptr)
            {
                WriteStatus("image_or_text_unavailable");
                return;
            }

            void* postTarget = nullptr;
            void* preUiTarget = nullptr;
            std::uint64_t postRva = 0;
            std::uint64_t preUiRva = 0;

            const bool postResolved =
                ResolveAnchor(
                    image,
                    kPostProcessingSignature,
                    kPostProcessingExpectedRva,
                    &postTarget,
                    &postRva);

            const bool preUiResolved =
                ResolveAnchor(
                    image,
                    kPreUiSignature,
                    kPreUiExpectedRva,
                    &preUiTarget,
                    &preUiRva);

            const MH_STATUS init =
                MH_Initialize();

            if (init != MH_OK &&
                init != MH_ERROR_ALREADY_INITIALIZED)
            {
                WriteStatus("minhook_init_failed");
                return;
            }

            if (postResolved)
            {
                void* original = nullptr;
                if (MH_CreateHook(
                        postTarget,
                        reinterpret_cast<void*>(
                            &PostProcessingThunk),
                        &original) == MH_OK &&
                    MH_EnableHook(
                        postTarget) == MH_OK)
                {
                    g_postProcessing =
                        reinterpret_cast<PostProcessingFn>(
                            original);

                    g_status.postProcessingArmed =
                        true;
                    g_status.postProcessingTargetRva =
                        postRva;
                }
            }

            if (preUiResolved)
            {
                void* original = nullptr;
                if (MH_CreateHook(
                        preUiTarget,
                        reinterpret_cast<void*>(
                            &PreUiThunk),
                        &original) == MH_OK &&
                    MH_EnableHook(
                        preUiTarget) == MH_OK)
                {
                    g_preUi =
                        reinterpret_cast<PreUiFn>(
                            original);

                    g_status.preUiArmed = true;
                    g_status.preUiTargetRva =
                        preUiRva;
                }
            }

            g_armResult =
                g_status.postProcessingArmed ||
                g_status.preUiArmed;

            WriteStatus(
                g_armResult
                    ? "armed"
                    : "verified_runtime_no_anchor_armed");
        }

        template <std::size_t N>
        testing::CallFixtureResult ValidateFixture(
            const std::uint8_t* bytes,
            const std::array<std::uint8_t, N>& signature,
            const std::uint64_t matchRva,
            const std::uint64_t expectedTargetRva)
        {
            testing::CallFixtureResult result{};

            if (bytes == nullptr ||
                std::memcmp(
                    bytes,
                    signature.data(),
                    N) != 0 ||
                bytes[8] != 0xE8)
            {
                return result;
            }

            std::int32_t displacement = 0;
            std::memcpy(
                &displacement,
                bytes + 9,
                sizeof(displacement));

            result.targetRva =
                matchRva +
                8 +
                5 +
                displacement;

            result.matched =
                result.targetRva ==
                expectedTargetRva;

            return result;
        }
    }

    bool Arm() noexcept
    {
        try
        {
            std::call_once(
                g_once,
                ArmImpl);
            return g_armResult;
        }
        catch (...)
        {
            return false;
        }
    }

    Status GetStatus() noexcept
    {
        Status result = g_status;
        result.postProcessingEvents =
            g_postEvents.load(
                std::memory_order_relaxed);
        result.preUiEvents =
            g_preUiEvents.load(
                std::memory_order_relaxed);
        return result;
    }

    namespace testing
    {
        CallFixtureResult ValidateAe1170PostProcessingFixture(
            const std::uint8_t* bytes,
            const std::uint64_t matchRva,
            const std::uint64_t) noexcept
        {
            return ValidateFixture(
                bytes,
                kPostProcessingSignature,
                matchRva,
                kPostProcessingExpectedRva);
        }

        CallFixtureResult ValidateAe1170PreUiFixture(
            const std::uint8_t* bytes,
            const std::uint64_t matchRva,
            const std::uint64_t) noexcept
        {
            return ValidateFixture(
                bytes,
                kPreUiSignature,
                matchRva,
                kPreUiExpectedRva);
        }

        std::uint32_t ClassifyRuntime(
            const std::uint16_t major,
            const std::uint16_t minor,
            const std::uint16_t patch,
            const std::uint16_t build) noexcept
        {
            return ClassifyRuntimeImpl(
                major,
                minor,
                patch,
                build);
        }
    }
}


extern "C" __declspec(dllexport)
bool GZFX_TestValidateAe1170PostProcessing(
    const std::uint8_t* bytes,
    const std::uint64_t matchRva,
    std::uint64_t* outTargetRva) noexcept
{
    const auto result =
        gzfx::diagnostics::engine_epochs::testing::
            ValidateAe1170PostProcessingFixture(
                bytes,
                matchRva);

    if (outTargetRva != nullptr)
    {
        *outTargetRva = result.targetRva;
    }

    return result.matched;
}

extern "C" __declspec(dllexport)
bool GZFX_TestValidateAe1170PreUi(
    const std::uint8_t* bytes,
    const std::uint64_t matchRva,
    std::uint64_t* outTargetRva) noexcept
{
    const auto result =
        gzfx::diagnostics::engine_epochs::testing::
            ValidateAe1170PreUiFixture(
                bytes,
                matchRva);

    if (outTargetRva != nullptr)
    {
        *outTargetRva = result.targetRva;
    }

    return result.matched;
}


extern "C" __declspec(dllexport)
std::uint32_t GZFX_TestClassifyRuntime(
    const std::uint16_t major,
    const std::uint16_t minor,
    const std::uint16_t patch,
    const std::uint16_t build) noexcept
{
    return
        gzfx::diagnostics::engine_epochs::testing::
            ClassifyRuntime(
                major,
                minor,
                patch,
                build);
}

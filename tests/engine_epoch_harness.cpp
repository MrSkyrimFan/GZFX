#include <Windows.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>

using ValidateFn = bool (*)(
    const std::uint8_t*,
    std::uint64_t,
    std::uint64_t*);

using ClassifyRuntimeFn = std::uint32_t (*)(
    std::uint16_t,
    std::uint16_t,
    std::uint16_t,
    std::uint16_t);

namespace
{
    std::uint64_t MatchRvaFor(
        const std::array<std::uint8_t, 24>& bytes,
        const std::uint64_t expectedTarget)
    {
        std::int32_t displacement = 0;
        std::memcpy(
            &displacement,
            bytes.data() + 9,
            sizeof(displacement));

        const std::int64_t match =
            static_cast<std::int64_t>(
                expectedTarget) -
            13 -
            static_cast<std::int64_t>(
                displacement);

        return static_cast<std::uint64_t>(
            match);
    }
}

int wmain(int argc, wchar_t** argv)
{
    if (argc != 2)
    {
        return 2;
    }

    const HMODULE module =
        LoadLibraryW(argv[1]);

    if (module == nullptr)
    {
        return 3;
    }

    const auto validatePost =
        reinterpret_cast<ValidateFn>(
            GetProcAddress(
                module,
                "GZFX_TestValidateAe1170PostProcessing"));

    const auto validatePreUi =
        reinterpret_cast<ValidateFn>(
            GetProcAddress(
                module,
                "GZFX_TestValidateAe1170PreUi"));

    const auto classifyRuntime =
        reinterpret_cast<ClassifyRuntimeFn>(
            GetProcAddress(
                module,
                "GZFX_TestClassifyRuntime"));

    if (validatePost == nullptr ||
        validatePreUi == nullptr ||
        classifyRuntime == nullptr)
    {
        FreeLibrary(module);
        return 4;
    }

    constexpr std::uint32_t knownRuntime =
        1u << 0;

    constexpr std::uint32_t engineProfile =
        1u << 1;

    if (classifyRuntime(1, 5, 97, 0) != knownRuntime ||
        classifyRuntime(1, 6, 1170, 0) !=
            (knownRuntime | engineProfile) ||
        classifyRuntime(1, 7, 104, 0) != knownRuntime ||
        classifyRuntime(1, 9, 999, 0) != 0)
    {
        FreeLibrary(module);
        return 8;
    }

    constexpr std::array<std::uint8_t, 24> post =
    {
        0x00, 0x48, 0x8B, 0x0D, 0x29, 0xBD, 0xE5, 0x01,
        0xE8, 0x04, 0x32, 0xFB, 0xFF, 0x48, 0x8B, 0x05,
        0x65, 0xDF, 0xEB, 0x01, 0x48, 0x8B, 0x88, 0x28
    };

    constexpr std::array<std::uint8_t, 24> preUi =
    {
        0xD2, 0x48, 0x8D, 0x0D, 0x46, 0x37, 0x2E, 0x02,
        0xE8, 0xD1, 0xF7, 0xE9, 0xFF, 0x48, 0x8B, 0x05,
        0x42, 0xC1, 0x64, 0x02, 0x48, 0x8B, 0x48, 0x10
    };

    constexpr std::uint64_t postTarget =
        0x1481B80ull;
    constexpr std::uint64_t preUiTarget =
        0xE44850ull;

    const std::uint64_t postMatch =
        MatchRvaFor(
            post,
            postTarget);

    const std::uint64_t preUiMatch =
        MatchRvaFor(
            preUi,
            preUiTarget);

    std::uint64_t resolved = 0;

    if (!validatePost(
            post.data(),
            postMatch,
            &resolved) ||
        resolved != postTarget)
    {
        FreeLibrary(module);
        return 5;
    }

    resolved = 0;

    if (!validatePreUi(
            preUi.data(),
            preUiMatch,
            &resolved) ||
        resolved != preUiTarget)
    {
        FreeLibrary(module);
        return 6;
    }

    auto corrupted = post;
    corrupted[3] ^= 0x01;

    if (validatePost(
            corrupted.data(),
            postMatch,
            &resolved))
    {
        FreeLibrary(module);
        return 7;
    }

    FreeLibrary(module);

    std::wcout
        << L"Engine epoch signature validation passed.\n";

    return 0;
}

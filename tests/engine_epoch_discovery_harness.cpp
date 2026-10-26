#include <Windows.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>

using ValidateFn = bool (*)(
    const std::uint8_t*,
    std::uint64_t,
    std::uint64_t*);

namespace
{
    void SetCallTarget(
        std::array<std::uint8_t, 24>& bytes,
        const std::uint64_t matchRva,
        const std::uint64_t targetRva)
    {
        const auto displacement =
            static_cast<std::int64_t>(targetRva) -
            static_cast<std::int64_t>(matchRva + 13);

        const auto rel =
            static_cast<std::int32_t>(
                displacement);

        std::memcpy(
            bytes.data() + 9,
            &rel,
            sizeof(rel));
    }

    bool CheckDiscovery(
        ValidateFn validate,
        std::array<std::uint8_t, 24> bytes,
        const std::uint64_t matchRva,
        const std::uint64_t targetRva)
    {
        // Wildcard every displacement field and the leading byte while
        // preserving the opcode skeleton.
        bytes[0] ^= 0x5A;
        bytes[4] = 0x11;
        bytes[5] = 0x22;
        bytes[6] = 0x33;
        bytes[7] = 0x44;
        bytes[16] = 0x55;
        bytes[17] = 0x66;
        bytes[18] = 0x77;
        bytes[19] = 0x88;

        SetCallTarget(
            bytes,
            matchRva,
            targetRva);

        std::uint64_t resolved = 0;

        if (!validate(
                bytes.data(),
                matchRva,
                &resolved) ||
            resolved != targetRva)
        {
            return false;
        }

        auto fixedOpcodeCorrupt = bytes;
        fixedOpcodeCorrupt[2] ^= 0x01;

        if (validate(
                fixedOpcodeCorrupt.data(),
                matchRva,
                &resolved))
        {
            return false;
        }

        auto callOpcodeCorrupt = bytes;
        callOpcodeCorrupt[8] = 0x90;

        if (validate(
                callOpcodeCorrupt.data(),
                matchRva,
                &resolved))
        {
            return false;
        }

        return true;
    }
}

int wmain(
    int argc,
    wchar_t** argv)
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
                "GZFX_TestValidateAe17104PostProcessingDiscovery"));

    const auto validatePreUi =
        reinterpret_cast<ValidateFn>(
            GetProcAddress(
                module,
                "GZFX_TestValidateAe17104PreUiDiscovery"));

    if (validatePost == nullptr ||
        validatePreUi == nullptr)
    {
        FreeLibrary(module);
        return 4;
    }

    std::array<std::uint8_t, 24> post =
    {
        0x00, 0x48, 0x8B, 0x0D, 0x29, 0xBD, 0xE5, 0x01,
        0xE8, 0x04, 0x32, 0xFB, 0xFF, 0x48, 0x8B, 0x05,
        0x65, 0xDF, 0xEB, 0x01, 0x48, 0x8B, 0x88, 0x28
    };

    std::array<std::uint8_t, 24> preUi =
    {
        0xD2, 0x48, 0x8D, 0x0D, 0x46, 0x37, 0x2E, 0x02,
        0xE8, 0xD1, 0xF7, 0xE9, 0xFF, 0x48, 0x8B, 0x05,
        0x42, 0xC1, 0x64, 0x02, 0x48, 0x8B, 0x48, 0x10
    };

    constexpr std::uint64_t postMatch =
        0x00100000ull;
    constexpr std::uint64_t postTarget =
        0x00123456ull;

    constexpr std::uint64_t preUiMatch =
        0x00200000ull;
    constexpr std::uint64_t preUiTarget =
        0x001F1020ull;

    const bool postOk =
        CheckDiscovery(
            validatePost,
            post,
            postMatch,
            postTarget);

    const bool preUiOk =
        CheckDiscovery(
            validatePreUi,
            preUi,
            preUiMatch,
            preUiTarget);

    FreeLibrary(module);

    if (!postOk ||
        !preUiOk)
    {
        return 5;
    }

    std::wcout
        << L"1.7.104 discovery-only engine anchor contract passed.\n";

    return 0;
}

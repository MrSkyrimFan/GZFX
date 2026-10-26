#include <Windows.h>

#include <array>
#include <cstdint>
#include <iostream>

using ClassifyFn = std::uint32_t (*)(
    std::uint16_t,
    std::uint16_t,
    std::uint16_t,
    std::uint16_t);

int wmain(
    int argc,
    wchar_t** argv)
{
    if (argc != 2)
    {
        return 2;
    }

    const HMODULE module =
        LoadLibraryW(
            argv[1]);

    if (module == nullptr)
    {
        return 3;
    }

    const auto classify =
        reinterpret_cast<ClassifyFn>(
            GetProcAddress(
                module,
                "GZFX_TestClassifyRuntime"));

    if (classify == nullptr)
    {
        FreeLibrary(module);
        return 4;
    }

    constexpr std::uint32_t known =
        1u << 0;

    constexpr std::uint32_t profile =
        1u << 1;

    constexpr std::uint32_t discovery =
        1u << 2;

    struct Runtime
    {
        std::uint16_t minor;
        std::uint16_t patch;
        std::uint32_t expected;
    };

    constexpr std::array<Runtime, 13> cases =
    {{
        {5, 97, known},
        {6, 318, known},
        {6, 323, known},
        {6, 342, known},
        {6, 353, known},
        {6, 629, known},
        {6, 640, known},
        {6, 659, known},
        {6, 1130, known},
        {6, 1170, known | profile},
        {6, 1179, known},
        {7, 99, known},
        {7, 104, known | discovery}
    }};

    for (const auto& runtime :
         cases)
    {
        if (classify(
                1,
                runtime.minor,
                runtime.patch,
                0) !=
            runtime.expected)
        {
            FreeLibrary(module);
            return 5;
        }
    }

    if (classify(
            1,
            8,
            1,
            0) != 0)
    {
        FreeLibrary(module);
        return 6;
    }

    FreeLibrary(module);

    std::wcout
        << L"Skyrim runtime catalog passed.\n";

    return 0;
}

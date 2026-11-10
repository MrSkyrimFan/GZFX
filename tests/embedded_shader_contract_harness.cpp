#include <Windows.h>
#include <d3dcompiler.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

int wmain(int argc, wchar_t** argv)
{
    if (argc != 2)
    {
        return 2;
    }

    std::ifstream file(std::filesystem::path(argv[1]), std::ios::binary);
    if (!file)
    {
        return 3;
    }

    const std::string source(
        std::istreambuf_iterator<char>{file},
        std::istreambuf_iterator<char>{});
    const std::string begin = "R\"GZFX_HLSL(";
    const std::string end = ")GZFX_HLSL\"";

    std::string hlsl;
    std::size_t offset = 0;
    int parts = 0;

    while (true)
    {
        const std::size_t open = source.find(begin, offset);
        if (open == std::string::npos)
        {
            break;
        }

        const std::size_t contentStart = open + begin.size();
        const std::size_t close = source.find(end, contentStart);
        if (close == std::string::npos)
        {
            return 4;
        }

        hlsl += source.substr(contentStart, close - contentStart);
        offset = close + end.size();
        ++parts;
    }

    if (parts != 3 ||
        hlsl.find("float3 ApplyShadowHighlightRecovery") == std::string::npos ||
        hlsl.find("float3 ApplyLocalClarity") == std::string::npos ||
        hlsl.find("float3 ApplySplitToning") == std::string::npos)
    {
        return 5;
    }

    const char* const entries[] =
    {
        "VSMain", "PSLuminanceBloom", "PSExposure", "PSComposite"
    };

    for (const auto* entry : entries)
    {
        ID3DBlob* compiled = nullptr;
        ID3DBlob* diagnostics = nullptr;

        const char* const profile =
            std::string(entry) == "VSMain" ? "vs_5_0" : "ps_5_0";

        const HRESULT hr = D3DCompile(
            hlsl.c_str(),
            hlsl.size(),
            "GZFX_0.0.38_embedded_shader",
            nullptr,
            nullptr,
            entry,
            profile,
            D3DCOMPILE_OPTIMIZATION_LEVEL3 |
                D3DCOMPILE_WARNINGS_ARE_ERRORS,
            0,
            &compiled,
            &diagnostics);

        if (diagnostics != nullptr)
        {
            std::cout << entry << ": " <<
                static_cast<const char*>(diagnostics->GetBufferPointer())
                << "\n";
            diagnostics->Release();
        }

        if (compiled != nullptr)
        {
            compiled->Release();
        }

        if (FAILED(hr))
        {
            return 6;
        }
    }

    std::cout << "Four runtime HLSL entrypoints compile cleanly.\n";
    return 0;
}

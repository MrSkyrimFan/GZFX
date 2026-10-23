#include "gzfx/ui/settings_menu.hpp"

#include "gzfx/paths.hpp"
#include "gzfx/settings.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace gzfx::ui
{
    namespace fs = std::filesystem;

    namespace
    {
        constexpr UINT kMenuWidth = 640;
        constexpr UINT kMenuHeight = 320;

        struct Runtime
        {
            ID3D11Device* device = nullptr;
            ID3D11Texture2D* texture = nullptr;
            ID3D11ShaderResourceView* srv = nullptr;
        };

        struct KeyEdge
        {
            bool previous = false;

            bool Pressed(const int virtualKey)
            {
                const bool down =
                    (GetAsyncKeyState(virtualKey) & 0x8000) != 0;

                const bool pressed =
                    down && !previous;

                previous = down;
                return pressed;
            }
        };

        std::mutex g_mutex;
        Runtime g_runtime{};

        bool g_visible = false;
        bool g_dirty = true;
        bool g_cycleModeRequested = false;
        bool g_captureRequested = false;
        std::uint32_t g_selected = 0;
        bool g_testAdjustmentApplied = false;

        KeyEdge g_numpad1{};
        KeyEdge g_numpad2{};
        KeyEdge g_numpad3{};
        KeyEdge g_numpad4{};
        KeyEdge g_numpad5{};
        KeyEdge g_numpad6{};
        KeyEdge g_numpad8{};

        bool TestFlag(const wchar_t* name)
        {
            wchar_t value[8]{};
            return GetEnvironmentVariableW(
                name,
                value,
                static_cast<DWORD>(std::size(value))) > 0;
        }

        const wchar_t* ModeName(const std::uint32_t mode)
        {
            switch (mode)
            {
            case 0: return L"OFF";
            case 1: return L"SPLIT CONTACT";
            case 2: return L"FULL CONTACT";
            case 3: return L"DEPTH";
            case 4: return L"PRE-UI COLOR";
            default: return L"UNKNOWN";
            }
        }

        const wchar_t* ItemName(
            const settings::Editable item)
        {
            switch (item)
            {
            case settings::Editable::AmbientContactStrength:
                return L"AMBIENT CONTACT STRENGTH";

            case settings::Editable::AmbientContactRadiusPixels:
                return L"AMBIENT CONTACT RADIUS";

            case settings::Editable::DefaultMode:
                return L"DEFAULT DEVELOPMENT VIEW";

            case settings::Editable::ActivationDrawIndexedThreshold:
                return L"WORLD DRAW THRESHOLD";

            case settings::Editable::AccessibleDiagnostics:
                return L"ACCESSIBLE DIAGNOSTICS";

            case settings::Editable::CaptureOnModeChange:
                return L"CAPTURE ON MODE CHANGE";

            case settings::Editable::Count:
                return L"";
            }

            return L"";
        }

        std::wstring ItemValue(
            const settings::Editable item)
        {
            const auto& values = settings::Get();
            std::wostringstream stream;

            switch (item)
            {
            case settings::Editable::AmbientContactStrength:
                stream
                    << std::fixed
                    << std::setprecision(2)
                    << values.ambientContactStrength;
                return stream.str();

            case settings::Editable::AmbientContactRadiusPixels:
                stream
                    << std::fixed
                    << std::setprecision(1)
                    << values.ambientContactRadiusPixels
                    << L" PX";
                return stream.str();

            case settings::Editable::DefaultMode:
                return ModeName(values.defaultMode);

            case settings::Editable::ActivationDrawIndexedThreshold:
                return std::to_wstring(
                    values.activationDrawIndexedThreshold);

            case settings::Editable::AccessibleDiagnostics:
                return values.accessibleDiagnostics
                    ? L"ON"
                    : L"OFF";

            case settings::Editable::CaptureOnModeChange:
                return values.captureOnModeChange
                    ? L"ON"
                    : L"OFF";

            case settings::Editable::Count:
                return L"";
            }

            return L"";
        }

        void WriteState(
            const std::uint64_t frame,
            const char* status)
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
                root / L"settings_menu.txt",
                std::ios::out | std::ios::trunc);

            if (!out)
            {
                return;
            }

            out
                << "frame=" << frame << "\n"
                << "status=" << status << "\n"
                << "visible="
                << (g_visible ? "true" : "false")
                << "\n"
                << "selected=" << g_selected << "\n"
                << "controls="
                   "Shift+Numpad1 menu; "
                   "Shift+Numpad2 reload; "
                   "Shift+Numpad3 cycle view; "
                   "Numpad8/2 select; "
                   "Numpad4/6 change; "
                   "Numpad5 toggle\n";
        }

        void ReleaseRuntime()
        {
            if (g_runtime.srv != nullptr)
            {
                g_runtime.srv->Release();
            }

            if (g_runtime.texture != nullptr)
            {
                g_runtime.texture->Release();
            }

            if (g_runtime.device != nullptr)
            {
                g_runtime.device->Release();
            }

            g_runtime = {};
        }

        bool EnsureTexture(ID3D11Device* device)
        {
            if (device == nullptr)
            {
                return false;
            }

            if (g_runtime.device == device &&
                g_runtime.texture != nullptr &&
                g_runtime.srv != nullptr)
            {
                return true;
            }

            ReleaseRuntime();

            D3D11_TEXTURE2D_DESC description{};
            description.Width = kMenuWidth;
            description.Height = kMenuHeight;
            description.MipLevels = 1;
            description.ArraySize = 1;
            description.Format =
                DXGI_FORMAT_B8G8R8A8_UNORM;
            description.SampleDesc.Count = 1;
            description.Usage =
                D3D11_USAGE_DYNAMIC;
            description.BindFlags =
                D3D11_BIND_SHADER_RESOURCE;
            description.CPUAccessFlags =
                D3D11_CPU_ACCESS_WRITE;

            HRESULT result =
                device->CreateTexture2D(
                    &description,
                    nullptr,
                    &g_runtime.texture);

            if (SUCCEEDED(result))
            {
                result =
                    device->CreateShaderResourceView(
                        g_runtime.texture,
                        nullptr,
                        &g_runtime.srv);
            }

            if (FAILED(result))
            {
                ReleaseRuntime();
                return false;
            }

            g_runtime.device = device;
            device->AddRef();
            g_dirty = true;
            return true;
        }

        bool BuildMenuPixels(
            std::vector<std::uint8_t>& pixels)
        {
            pixels.assign(
                static_cast<std::size_t>(
                    kMenuWidth) *
                kMenuHeight *
                4,
                0);

            BITMAPINFO info{};
            info.bmiHeader.biSize =
                sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth =
                static_cast<LONG>(kMenuWidth);
            info.bmiHeader.biHeight =
                -static_cast<LONG>(kMenuHeight);
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;

            void* dibPixels = nullptr;

            HDC dc = CreateCompatibleDC(nullptr);
            if (dc == nullptr)
            {
                return false;
            }

            HBITMAP bitmap =
                CreateDIBSection(
                    dc,
                    &info,
                    DIB_RGB_COLORS,
                    &dibPixels,
                    nullptr,
                    0);

            if (bitmap == nullptr ||
                dibPixels == nullptr)
            {
                if (bitmap != nullptr)
                {
                    DeleteObject(bitmap);
                }

                DeleteDC(dc);
                return false;
            }

            const HGDIOBJ oldBitmap =
                SelectObject(dc, bitmap);

            HFONT titleFont =
                CreateFontW(
                    24,
                    0,
                    0,
                    0,
                    FW_BOLD,
                    FALSE,
                    FALSE,
                    FALSE,
                    DEFAULT_CHARSET,
                    OUT_DEFAULT_PRECIS,
                    CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY,
                    DEFAULT_PITCH |
                        FF_DONTCARE,
                    L"Segoe UI");

            HFONT bodyFont =
                CreateFontW(
                    18,
                    0,
                    0,
                    0,
                    FW_NORMAL,
                    FALSE,
                    FALSE,
                    FALSE,
                    DEFAULT_CHARSET,
                    OUT_DEFAULT_PRECIS,
                    CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY,
                    DEFAULT_PITCH |
                        FF_DONTCARE,
                    L"Segoe UI");

            const HBRUSH background =
                CreateSolidBrush(RGB(18, 18, 18));

            const HBRUSH selectedBrush =
                CreateSolidBrush(RGB(245, 245, 245));

            const HBRUSH borderBrush =
                CreateSolidBrush(RGB(255, 255, 255));

            RECT full{
                0,
                0,
                static_cast<LONG>(kMenuWidth),
                static_cast<LONG>(kMenuHeight)
            };

            FillRect(
                dc,
                &full,
                background);

            FrameRect(
                dc,
                &full,
                borderBrush);

            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(255, 255, 255));

            HGDIOBJ oldFont =
                SelectObject(dc, titleFont);

            RECT title{
                18,
                12,
                static_cast<LONG>(kMenuWidth - 18),
                44
            };

            DrawTextW(
                dc,
                L"GZFX SETTINGS",
                -1,
                &title,
                DT_LEFT |
                    DT_SINGLELINE |
                    DT_VCENTER);

            SelectObject(dc, bodyFont);

            constexpr int firstRowY = 58;
            constexpr int rowHeight = 34;

            const auto count =
                static_cast<std::uint32_t>(
                    settings::Editable::Count);

            for (std::uint32_t index = 0;
                 index < count;
                 ++index)
            {
                const auto item =
                    static_cast<settings::Editable>(
                        index);

                RECT row{
                    14,
                    firstRowY +
                        static_cast<int>(index) *
                            rowHeight,
                    static_cast<LONG>(
                        kMenuWidth - 14),
                    firstRowY +
                        static_cast<int>(index + 1) *
                            rowHeight - 4
                };

                const bool selected =
                    index == g_selected;

                if (selected)
                {
                    FillRect(
                        dc,
                        &row,
                        selectedBrush);

                    SetTextColor(
                        dc,
                        RGB(0, 0, 0));
                }
                else
                {
                    SetTextColor(
                        dc,
                        RGB(255, 255, 255));
                }

                RECT label = row;
                label.left += 10;
                label.right = 430;

                DrawTextW(
                    dc,
                    ItemName(item),
                    -1,
                    &label,
                    DT_LEFT |
                        DT_SINGLELINE |
                        DT_VCENTER);

                std::wstring value =
                    ItemValue(item);

                RECT valueRect = row;
                valueRect.left = 430;
                valueRect.right -= 10;

                DrawTextW(
                    dc,
                    value.c_str(),
                    -1,
                    &valueRect,
                    DT_RIGHT |
                        DT_SINGLELINE |
                        DT_VCENTER);
            }

            SetTextColor(
                dc,
                RGB(255, 255, 255));

            RECT footer{
                18,
                270,
                static_cast<LONG>(kMenuWidth - 18),
                310
            };

            DrawTextW(
                dc,
                L"NUM8/2 SELECT   NUM4/6 CHANGE   NUM5 TOGGLE   SHIFT+NUM1 CLOSE",
                -1,
                &footer,
                DT_LEFT |
                    DT_WORDBREAK);

            SelectObject(dc, oldFont);

            const auto* source =
                static_cast<const std::uint8_t*>(
                    dibPixels);

            const std::size_t pixelCount =
                static_cast<std::size_t>(
                    kMenuWidth) *
                kMenuHeight;

            for (std::size_t i = 0;
                 i < pixelCount;
                 ++i)
            {
                const std::size_t offset =
                    i * 4;

                pixels[offset + 0] =
                    source[offset + 0];

                pixels[offset + 1] =
                    source[offset + 1];

                pixels[offset + 2] =
                    source[offset + 2];

                pixels[offset + 3] = 235;
            }

            DeleteObject(borderBrush);
            DeleteObject(selectedBrush);
            DeleteObject(background);
            DeleteObject(bodyFont);
            DeleteObject(titleFont);

            SelectObject(dc, oldBitmap);
            DeleteObject(bitmap);
            DeleteDC(dc);

            return true;
        }

        bool UploadMenu(
            ID3D11DeviceContext* context)
        {
            if (context == nullptr ||
                g_runtime.texture == nullptr)
            {
                return false;
            }

            std::vector<std::uint8_t> pixels;

            if (!BuildMenuPixels(pixels))
            {
                return false;
            }

            D3D11_MAPPED_SUBRESOURCE mapped{};

            const HRESULT result =
                context->Map(
                    g_runtime.texture,
                    0,
                    D3D11_MAP_WRITE_DISCARD,
                    0,
                    &mapped);

            if (FAILED(result))
            {
                return false;
            }

            const std::size_t sourcePitch =
                static_cast<std::size_t>(
                    kMenuWidth) *
                4;

            for (UINT y = 0;
                 y < kMenuHeight;
                 ++y)
            {
                std::memcpy(
                    static_cast<std::uint8_t*>(
                        mapped.pData) +
                        static_cast<std::size_t>(y) *
                            mapped.RowPitch,
                    pixels.data() +
                        static_cast<std::size_t>(y) *
                            sourcePitch,
                    sourcePitch);
            }

            context->Unmap(
                g_runtime.texture,
                0);

            g_dirty = false;
            return true;
        }

        bool BooleanItem(
            const settings::Editable item)
        {
            return
                item ==
                    settings::Editable::
                        AccessibleDiagnostics ||
                item ==
                    settings::Editable::
                        CaptureOnModeChange;
        }

        void ApplyAdjustment(
            const int direction)
        {
            const auto item =
                static_cast<settings::Editable>(
                    g_selected);

            if (BooleanItem(item))
            {
                settings::Adjust(
                    item,
                    direction);
            }
            else
            {
                settings::Adjust(
                    item,
                    direction);
            }

            g_dirty = true;
        }
    }

    void ProcessInput(
        const std::uint64_t frame) noexcept
    {
        try
        {
            std::scoped_lock lock(g_mutex);

            if (TestFlag(L"GZFX_TEST_MENU_VISIBLE"))
            {
                g_visible = true;
            }

            if (TestFlag(L"GZFX_TEST_MENU_ADJUST") &&
                !g_testAdjustmentApplied)
            {
                settings::Adjust(
                    settings::Editable::
                        AmbientContactStrength,
                    1);

                g_testAdjustmentApplied = true;
                g_dirty = true;
            }

            const bool shiftDown =
                (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

            const bool num1 =
                g_numpad1.Pressed(VK_NUMPAD1);

            const bool num2 =
                g_numpad2.Pressed(VK_NUMPAD2);

            const bool num3 =
                g_numpad3.Pressed(VK_NUMPAD3);

            const bool num4 =
                g_numpad4.Pressed(VK_NUMPAD4);

            const bool num5 =
                g_numpad5.Pressed(VK_NUMPAD5);

            const bool num6 =
                g_numpad6.Pressed(VK_NUMPAD6);

            const bool num8 =
                g_numpad8.Pressed(VK_NUMPAD8);

            if (shiftDown && num1)
            {
                g_visible = !g_visible;
                g_dirty = true;
                g_captureRequested = true;

                WriteState(
                    frame,
                    g_visible
                        ? "opened"
                        : "closed");
            }

            if (shiftDown && num2)
            {
                settings::Reload();
                g_dirty = true;
                g_captureRequested = true;

                WriteState(
                    frame,
                    "settings_reloaded");
            }

            if (shiftDown && num3)
            {
                g_cycleModeRequested = true;

                WriteState(
                    frame,
                    "cycle_view_requested");
            }

            if (!g_visible || shiftDown)
            {
                return;
            }

            const auto count =
                static_cast<std::uint32_t>(
                    settings::Editable::Count);

            if (num8)
            {
                g_selected =
                    g_selected == 0
                        ? count - 1
                        : g_selected - 1;

                g_dirty = true;
                g_captureRequested = true;
            }

            if (num2)
            {
                g_selected =
                    (g_selected + 1) %
                    count;

                g_dirty = true;
                g_captureRequested = true;
            }

            if (num4)
            {
                ApplyAdjustment(-1);
                g_captureRequested = true;
            }

            if (num6)
            {
                ApplyAdjustment(1);
                g_captureRequested = true;
            }

            if (num5)
            {
                const auto item =
                    static_cast<settings::Editable>(
                        g_selected);

                if (BooleanItem(item))
                {
                    settings::Toggle(item);
                    g_dirty = true;
                    g_captureRequested = true;
                }
            }

            if (num8 ||
                num2 ||
                num4 ||
                num5 ||
                num6)
            {
                WriteState(
                    frame,
                    "menu_changed");
            }
        }
        catch (...)
        {
        }
    }

    bool ConsumeCycleModeRequest() noexcept
    {
        try
        {
            std::scoped_lock lock(g_mutex);

            const bool requested =
                g_cycleModeRequested;

            g_cycleModeRequested = false;
            return requested;
        }
        catch (...)
        {
            return false;
        }
    }

    bool ConsumeCaptureRequest() noexcept
    {
        try
        {
            std::scoped_lock lock(g_mutex);

            const bool requested =
                g_captureRequested;

            g_captureRequested = false;
            return requested;
        }
        catch (...)
        {
            return false;
        }
    }

    bool GetMenuOverlay(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        MenuOverlay& overlay) noexcept
    {
        try
        {
            std::scoped_lock lock(g_mutex);

            overlay = {};
            overlay.visible = g_visible;

            if (!g_visible)
            {
                return true;
            }

            if (!EnsureTexture(device))
            {
                return false;
            }

            if (g_dirty &&
                !UploadMenu(context))
            {
                return false;
            }

            overlay.srv = g_runtime.srv;
            overlay.width = kMenuWidth;
            overlay.height = kMenuHeight;
            return overlay.srv != nullptr;
        }
        catch (...)
        {
            return false;
        }
    }
}

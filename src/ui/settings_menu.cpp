#include "gzfx/ui/settings_menu.hpp"

#include "gzfx/diagnostics/semantic_probe.hpp"
#include "gzfx/paths.hpp"
#include "gzfx/settings.hpp"
#include "gzfx/render/wave3_pipeline.hpp"
#include "gzfx/render/ambient_visibility_v2.hpp"
#include "gzfx/render/unified_product_renderer.hpp"

#include <Windows.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
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
        constexpr UINT kMenuHeight = 768;

        constexpr int kFirstRowY = 58;
        constexpr int kRowHeight = 34;
        constexpr int kSliderLeft = 430;
        constexpr int kSliderRight = 610;
        constexpr std::uint32_t kVisibleRows = 19;

        struct Runtime
        {
            ID3D11Device* device = nullptr;
            ID3D11Texture2D* texture = nullptr;
            ID3D11ShaderResourceView* srv = nullptr;
        };

        struct KeyEdge
        {
            bool previous = false;

            bool Pressed(
                const int primaryVirtualKey,
                const int aliasVirtualKey = 0)
            {
                const bool primaryDown =
                    (GetAsyncKeyState(
                        primaryVirtualKey) & 0x8000) != 0;

                const bool aliasDown =
                    aliasVirtualKey != 0 &&
                    (GetAsyncKeyState(
                        aliasVirtualKey) & 0x8000) != 0;

                const bool down =
                    primaryDown || aliasDown;

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
        bool g_manualCaptureRequested = false;
        std::uint32_t g_selected = 0;
        std::uint32_t g_scrollOffset = 0;
        bool g_testAdjustmentApplied = false;
        bool g_testManualCaptureApplied = false;

        HWND g_window = nullptr;
        WNDPROC g_originalWindowProc = nullptr;

        POINT g_cursor{
            static_cast<LONG>(kMenuWidth / 2),
            static_cast<LONG>(kMenuHeight / 2)
        };

        bool g_leftDown = false;
        bool g_directInputSeen = false;

        std::uint64_t g_directInputPackets = 0;
        std::int64_t g_directInputDeltaX = 0;
        std::int64_t g_directInputDeltaY = 0;
        std::uint64_t g_inputFrame = 0;

        KeyEdge g_numpad1{};
        KeyEdge g_numpad2{};
        KeyEdge g_numpad3{};
        KeyEdge g_numpad4{};
        KeyEdge g_numpad5{};

        bool TestFlag(const wchar_t* name)
        {
            wchar_t value[8]{};

            return GetEnvironmentVariableW(
                name,
                value,
                static_cast<DWORD>(
                    std::size(value))) > 0;
        }

        void ClampCursor()
        {
            g_cursor.x =
                std::clamp<LONG>(
                    g_cursor.x,
                    0,
                    static_cast<LONG>(
                        kMenuWidth - 1));

            g_cursor.y =
                std::clamp<LONG>(
                    g_cursor.y,
                    0,
                    static_cast<LONG>(
                        kMenuHeight - 1));
        }

        const wchar_t* ItemName(
            const settings::Editable item)
        {
            return settings::EditableName(item);
        }

        std::wstring ItemValue(
            const settings::Editable item)
        {
            return settings::EditableValue(item);
        }

        bool BooleanItem(
            const settings::Editable item)
        {
            return settings::IsToggle(item);
        }

        bool SliderItem(
            const settings::Editable item)
        {
            return settings::IsSlider(item);
        }

        bool ActionItem(
            const settings::Editable item)
        {
            return settings::IsAction(item);
        }

        void ClampScroll()
        {
            const std::uint32_t count =
                static_cast<std::uint32_t>(
                    settings::Editable::Count);

            const std::uint32_t maximum =
                count > kVisibleRows
                ? count - kVisibleRows
                : 0;

            g_scrollOffset =
                std::min(
                    g_scrollOffset,
                    maximum);
        }

        void EnsureSelectionVisible()
        {
            const std::uint32_t count =
                static_cast<std::uint32_t>(
                    settings::Editable::Count);

            if (count == 0)
            {
                g_selected = 0;
                g_scrollOffset = 0;
                return;
            }

            g_selected =
                std::min(
                    g_selected,
                    count - 1);

            if (g_selected < g_scrollOffset)
            {
                g_scrollOffset = g_selected;
            }
            else if (g_selected >=
                     g_scrollOffset +
                        kVisibleRows)
            {
                g_scrollOffset =
                    g_selected -
                    kVisibleRows + 1;
            }

            ClampScroll();
        }

        void ScrollBy(
            const int rows)
        {
            const int count =
                static_cast<int>(
                    settings::Editable::Count);

            const int maximum =
                std::max(
                    0,
                    count -
                        static_cast<int>(
                            kVisibleRows));

            const int next =
                std::clamp(
                    static_cast<int>(
                        g_scrollOffset) + rows,
                    0,
                    maximum);

            g_scrollOffset =
                static_cast<std::uint32_t>(next);

            if (g_selected < g_scrollOffset)
            {
                g_selected = g_scrollOffset;
            }
            else if (g_selected >=
                     g_scrollOffset +
                        kVisibleRows)
            {
                g_selected =
                    g_scrollOffset +
                    kVisibleRows - 1;
            }

            g_dirty = true;
        }

        int RowFromCursor()
        {
            if (g_cursor.y < kFirstRowY)
            {
                return -1;
            }

            const int relative =
                static_cast<int>(g_cursor.y) -
                kFirstRowY;

            const int visibleRow =
                relative / kRowHeight;

            if (visibleRow < 0 ||
                visibleRow >=
                    static_cast<int>(
                        kVisibleRows))
            {
                return -1;
            }

            const int rowTop =
                kFirstRowY +
                visibleRow *
                    kRowHeight;

            if (g_cursor.y >=
                rowTop + kRowHeight - 4)
            {
                return -1;
            }

            const int logicalRow =
                static_cast<int>(
                    g_scrollOffset) +
                visibleRow;

            const int count =
                static_cast<int>(
                    settings::Editable::Count);

            return logicalRow >= 0 &&
                logicalRow < count
                ? logicalRow
                : -1;
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
            fs::create_directories(
                root,
                error);

            if (error)
            {
                return;
            }

            std::ofstream out(
                root / L"settings_menu.txt",
                std::ios::out |
                    std::ios::trunc);

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
                << "selected="
                << g_selected << "\n"
                << "cursor="
                << g_cursor.x << ","
                << g_cursor.y << "\n"
                << "directinput_seen="
                << (g_directInputSeen ? "true" : "false")
                << "\n"
                << "directinput_packets="
                << g_directInputPackets
                << "\n"
                << "directinput_delta_total="
                << g_directInputDeltaX
                << ","
                << g_directInputDeltaY
                << "\n"
                << "controls="
                   "Shift+Numpad1 menu; "
                   "Shift+Numpad2 reload; "
                   "Shift+Numpad3 cycle view; "
                   "Shift+Numpad4 capture frame+settings; "
                   "Shift+Numpad5 arm semantic + Wave3 capture; "
                   "mouse click/drag sliders; "
                   "mouse click toggles\n";
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

        bool EnsureTexture(
            ID3D11Device* device)
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
                static_cast<LONG>(
                    kMenuWidth);
            info.bmiHeader.biHeight =
                -static_cast<LONG>(
                    kMenuHeight);
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression =
                BI_RGB;

            void* dibPixels = nullptr;

            HDC dc =
                CreateCompatibleDC(nullptr);

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
                CreateSolidBrush(
                    RGB(18, 18, 18));

            const HBRUSH selectedBrush =
                CreateSolidBrush(
                    RGB(245, 245, 245));

            const HBRUSH borderBrush =
                CreateSolidBrush(
                    RGB(255, 255, 255));

            RECT full{
                0,
                0,
                static_cast<LONG>(
                    kMenuWidth),
                static_cast<LONG>(
                    kMenuHeight)
            };

            FillRect(
                dc,
                &full,
                background);

            FrameRect(
                dc,
                &full,
                borderBrush);

            SetBkMode(
                dc,
                TRANSPARENT);

            SetTextColor(
                dc,
                RGB(255, 255, 255));

            HGDIOBJ oldFont =
                SelectObject(
                    dc,
                    titleFont);

            RECT title{
                18,
                12,
                static_cast<LONG>(
                    kMenuWidth - 18),
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

            SelectObject(
                dc,
                bodyFont);

            const auto count =
                static_cast<std::uint32_t>(
                    settings::Editable::Count);

            ClampScroll();

            const std::uint32_t endIndex =
                std::min(
                    count,
                    g_scrollOffset +
                        kVisibleRows);

            for (std::uint32_t index =
                    g_scrollOffset;
                 index < endIndex;
                 ++index)
            {
                const auto item =
                    static_cast<
                        settings::Editable>(
                            index);

                const std::uint32_t visibleIndex =
                    index - g_scrollOffset;

                RECT row{
                    14,
                    kFirstRowY +
                        static_cast<int>(
                            visibleIndex) *
                            kRowHeight,
                    static_cast<LONG>(
                        kMenuWidth - 14),
                    kFirstRowY +
                        static_cast<int>(
                            visibleIndex + 1) *
                            kRowHeight - 4
                };

                const bool selected =
                    index == g_selected;

                const COLORREF foreground =
                    selected
                        ? RGB(0, 0, 0)
                        : RGB(255, 255, 255);

                const COLORREF backgroundColor =
                    selected
                        ? RGB(245, 245, 245)
                        : RGB(18, 18, 18);

                if (selected)
                {
                    FillRect(
                        dc,
                        &row,
                        selectedBrush);
                }

                SetTextColor(
                    dc,
                    foreground);

                RECT label = row;
                label.left += 10;
                label.right = 360;

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
                valueRect.left = 360;
                valueRect.right = 425;

                DrawTextW(
                    dc,
                    value.c_str(),
                    -1,
                    &valueRect,
                    DT_RIGHT |
                        DT_SINGLELINE |
                        DT_VCENTER);

                if (SliderItem(item))
                {
                    const int centerY =
                        (row.top + row.bottom) / 2;

                    HPEN trackPen =
                        CreatePen(
                            PS_SOLID,
                            2,
                            foreground);

                    const HGDIOBJ oldPen =
                        SelectObject(
                            dc,
                            trackPen);

                    MoveToEx(
                        dc,
                        kSliderLeft,
                        centerY,
                        nullptr);

                    LineTo(
                        dc,
                        kSliderRight,
                        centerY);

                    const float normalized =
                        settings::GetNormalized(
                            item);

                    const int knobX =
                        kSliderLeft +
                        static_cast<int>(
                            normalized *
                            static_cast<float>(
                                kSliderRight -
                                kSliderLeft));

                    const HBRUSH knobBrush =
                        CreateSolidBrush(
                            foreground);

                    RECT knob{
                        knobX - 5,
                        centerY - 7,
                        knobX + 6,
                        centerY + 8
                    };

                    FillRect(
                        dc,
                        &knob,
                        knobBrush);

                    DeleteObject(
                        knobBrush);

                    SelectObject(
                        dc,
                        oldPen);

                    DeleteObject(
                        trackPen);
                }
                else
                {
                    RECT toggle{
                        kSliderLeft,
                        row.top + 5,
                        kSliderRight,
                        row.bottom - 5
                    };

                    const HBRUSH toggleBrush =
                        CreateSolidBrush(
                            backgroundColor);

                    FillRect(
                        dc,
                        &toggle,
                        toggleBrush);

                    FrameRect(
                        dc,
                        &toggle,
                        borderBrush);

                    DeleteObject(
                        toggleBrush);
                }
            }

            SetTextColor(
                dc,
                RGB(255, 255, 255));

            RECT footer{
                18,
                static_cast<LONG>(
                    kMenuHeight - 54),
                static_cast<LONG>(
                    kMenuWidth - 18),
                static_cast<LONG>(
                    kMenuHeight - 10)
            };

            std::wostringstream footerText;
            footerText
                << L"MOUSE: CLICK / DRAG / WHEEL   "
                << L"ROWS "
                << (count == 0
                    ? 0
                    : g_scrollOffset + 1)
                << L"-"
                << endIndex
                << L" OF "
                << count
                << L"   SHIFT+NUM1 CLOSE";

            const std::wstring footerValue =
                footerText.str();

            DrawTextW(
                dc,
                footerValue.c_str(),
                -1,
                &footer,
                DT_LEFT |
                    DT_WORDBREAK);

            // Custom monochrome cursor. Black outline + white core.
            HPEN blackPen =
                CreatePen(
                    PS_SOLID,
                    5,
                    RGB(0, 0, 0));

            HPEN whitePen =
                CreatePen(
                    PS_SOLID,
                    2,
                    RGB(255, 255, 255));

            HGDIOBJ oldPen =
                SelectObject(
                    dc,
                    blackPen);

            MoveToEx(
                dc,
                g_cursor.x - 8,
                g_cursor.y,
                nullptr);

            LineTo(
                dc,
                g_cursor.x + 9,
                g_cursor.y);

            MoveToEx(
                dc,
                g_cursor.x,
                g_cursor.y - 8,
                nullptr);

            LineTo(
                dc,
                g_cursor.x,
                g_cursor.y + 9);

            SelectObject(
                dc,
                whitePen);

            MoveToEx(
                dc,
                g_cursor.x - 7,
                g_cursor.y,
                nullptr);

            LineTo(
                dc,
                g_cursor.x + 8,
                g_cursor.y);

            MoveToEx(
                dc,
                g_cursor.x,
                g_cursor.y - 7,
                nullptr);

            LineTo(
                dc,
                g_cursor.x,
                g_cursor.y + 8);

            SelectObject(
                dc,
                oldPen);

            DeleteObject(
                whitePen);

            DeleteObject(
                blackPen);

            SelectObject(
                dc,
                oldFont);

            DeleteObject(
                borderBrush);

            DeleteObject(
                selectedBrush);

            DeleteObject(
                background);

            DeleteObject(
                bodyFont);

            DeleteObject(
                titleFont);

            SelectObject(
                dc,
                oldBitmap);

            const auto* source =
                static_cast<
                    const std::uint8_t*>(
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

            std::vector<std::uint8_t>
                pixels;

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

        void MarkChanged()
        {
            g_dirty = true;
            g_captureRequested = true;
        }

        void ApplyMouse(
            const bool click)
        {
            ClampCursor();

            const int row =
                RowFromCursor();

            if (row < 0)
            {
                return;
            }

            g_selected =
                static_cast<std::uint32_t>(
                    row);

            const auto item =
                static_cast<settings::Editable>(
                    g_selected);

            if (SliderItem(item))
            {
                const float normalized =
                    std::clamp(
                        (
                            static_cast<float>(
                                g_cursor.x) -
                            static_cast<float>(
                                kSliderLeft)
                        ) /
                        static_cast<float>(
                            kSliderRight -
                            kSliderLeft),
                        0.0f,
                        1.0f);

                if (g_cursor.x >=
                        kSliderLeft - 12 &&
                    g_cursor.x <=
                        kSliderRight + 12)
                {
                    settings::SetNormalized(
                        item,
                        normalized);

                    MarkChanged();
                }
            }
            else if (click &&
                     BooleanItem(item))
            {
                settings::Toggle(item);
                MarkChanged();
            }
            else if (click &&
                     ActionItem(item))
            {
                settings::Activate(item);
                MarkChanged();
            }
            else
            {
                g_dirty = true;
            }
        }

        void UpdateCursorFromClient(
            const LPARAM lParam)
        {
            g_cursor.x =
                GET_X_LPARAM(lParam);

            g_cursor.y =
                GET_Y_LPARAM(lParam);

            ClampCursor();

            const int row =
                RowFromCursor();

            if (row >= 0)
            {
                g_selected =
                    static_cast<
                        std::uint32_t>(row);
            }

            g_dirty = true;
        }

        LRESULT CALLBACK WindowProc(
            HWND window,
            UINT message,
            WPARAM wParam,
            LPARAM lParam)
        {
            {
                std::scoped_lock lock(
                    g_mutex);

                if (g_visible)
                {
                    switch (message)
                    {
                    case WM_MOUSEMOVE:
                        if (!g_directInputSeen)
                        {
                            UpdateCursorFromClient(
                                lParam);

                            if (g_leftDown)
                            {
                                ApplyMouse(false);
                            }
                        }

                        return 0;

                    case WM_LBUTTONDOWN:
                        if (!g_directInputSeen)
                        {
                            UpdateCursorFromClient(
                                lParam);

                            g_leftDown = true;
                            ApplyMouse(true);
                            SetCapture(window);
                        }

                        return 0;

                    case WM_LBUTTONUP:
                        if (!g_directInputSeen)
                        {
                            UpdateCursorFromClient(
                                lParam);

                            g_leftDown = false;
                            ReleaseCapture();
                        }

                        return 0;

                    case WM_INPUT:
                    {
                        UINT size = 0;

                        GetRawInputData(
                            reinterpret_cast<HRAWINPUT>(
                                lParam),
                            RID_INPUT,
                            nullptr,
                            &size,
                            sizeof(RAWINPUTHEADER));

                        if (size > 0 &&
                            size <= 4096)
                        {
                            std::vector<std::uint8_t>
                                bytes(size);

                            if (GetRawInputData(
                                    reinterpret_cast<
                                        HRAWINPUT>(lParam),
                                    RID_INPUT,
                                    bytes.data(),
                                    &size,
                                    sizeof(RAWINPUTHEADER)) ==
                                size)
                            {
                                const RAWINPUT* input =
                                    reinterpret_cast<
                                        const RAWINPUT*>(
                                            bytes.data());

                                if (input->header.dwType ==
                                        RIM_TYPEMOUSE &&
                                    !g_directInputSeen)
                                {
                                    if ((input->data.mouse.usButtonFlags &
                                         RI_MOUSE_WHEEL) != 0)
                                    {
                                        const SHORT wheel =
                                            static_cast<SHORT>(
                                                input->data.mouse.
                                                    usButtonData);

                                        ScrollBy(
                                            wheel > 0
                                            ? -3
                                            : 3);
                                    }

                                    g_cursor.x +=
                                        input->data.mouse
                                            .lLastX;

                                    g_cursor.y +=
                                        input->data.mouse
                                            .lLastY;

                                    ClampCursor();

                                    const int row =
                                        RowFromCursor();

                                    if (row >= 0)
                                    {
                                        g_selected =
                                            static_cast<
                                                std::uint32_t>(
                                                    row);
                                    }

                                    if (g_leftDown)
                                    {
                                        ApplyMouse(false);
                                    }

                                    g_dirty = true;
                                }
                            }
                        }

                        DefWindowProcW(
                            window,
                            message,
                            wParam,
                            lParam);

                        return 0;
                    }

                    case WM_SETCURSOR:
                        SetCursor(nullptr);
                        return TRUE;

                    case WM_MOUSEWHEEL:
                    {
                        const SHORT delta =
                            GET_WHEEL_DELTA_WPARAM(wParam);

                        if (delta != 0)
                        {
                            ScrollBy(
                                delta > 0
                                ? -3
                                : 3);
                        }

                        return 0;
                    }

                    case WM_RBUTTONDOWN:
                    case WM_RBUTTONUP:
                    case WM_MBUTTONDOWN:
                    case WM_MBUTTONUP:
                        return 0;

                    default:
                        break;
                    }
                }
            }

            return g_originalWindowProc != nullptr
                ? CallWindowProcW(
                    g_originalWindowProc,
                    window,
                    message,
                    wParam,
                    lParam)
                : DefWindowProcW(
                    window,
                    message,
                    wParam,
                    lParam);
        }

        bool EnsureWindowHook(HWND window)
        {
            if (window == nullptr)
            {
                return false;
            }

            if (g_window == window &&
                g_originalWindowProc != nullptr)
            {
                return true;
            }

            SetLastError(ERROR_SUCCESS);

            const LONG_PTR previous =
                SetWindowLongPtrW(
                    window,
                    GWLP_WNDPROC,
                    reinterpret_cast<LONG_PTR>(
                        &WindowProc));

            if (previous == 0 &&
                GetLastError() != ERROR_SUCCESS)
            {
                return false;
            }

            g_window = window;

            g_originalWindowProc =
                reinterpret_cast<WNDPROC>(
                    previous);

            return g_originalWindowProc != nullptr;
        }
    }

    void ProcessInput(
        const std::uint64_t frame,
        HWND gameWindow) noexcept
    {
        try
        {
            std::scoped_lock lock(g_mutex);

            g_inputFrame = frame;

            EnsureWindowHook(gameWindow);

            if (TestFlag(
                    L"GZFX_TEST_MENU_VISIBLE"))
            {
                g_visible = true;
            }

            if (TestFlag(
                    L"GZFX_TEST_MENU_ADJUST") &&
                !g_testAdjustmentApplied)
            {
                settings::SetNormalized(
                    settings::Editable::
                        AmbientContactStrength,
                    0.335f);

                g_testAdjustmentApplied = true;
                MarkChanged();
            }

            if (TestFlag(
                    L"GZFX_TEST_MANUAL_CAPTURE") &&
                !g_testManualCaptureApplied)
            {
                g_manualCaptureRequested = true;
                g_captureRequested = true;
                g_testManualCaptureApplied = true;

                WriteState(
                    frame,
                    "manual_capture_requested");
            }

            const bool shiftDown =
                (GetAsyncKeyState(
                    VK_SHIFT) & 0x8000) != 0;

            const bool num1 =
                g_numpad1.Pressed(
                    VK_NUMPAD1,
                    VK_END);

            const bool num2 =
                g_numpad2.Pressed(
                    VK_NUMPAD2,
                    VK_DOWN);

            const bool num3 =
                g_numpad3.Pressed(
                    VK_NUMPAD3,
                    VK_NEXT);

            const bool num4 =
                g_numpad4.Pressed(
                    VK_NUMPAD4,
                    VK_LEFT);

            const bool num5 =
                g_numpad5.Pressed(
                    VK_NUMPAD5,
                    VK_CLEAR);

            if (shiftDown && num1)
            {
                g_visible = !g_visible;

                if (g_visible)
                {
                    g_cursor.x =
                        static_cast<LONG>(
                            kMenuWidth / 2);

                    g_cursor.y =
                        static_cast<LONG>(
                            kMenuHeight / 2);

                    g_scrollOffset = 0;
                    EnsureSelectionVisible();
                }
                else
                {
                    g_leftDown = false;
                    ReleaseCapture();
                }

                MarkChanged();

                WriteState(
                    frame,
                    g_visible
                        ? "opened"
                        : "closed");
            }

            if (shiftDown && num2)
            {
                settings::Reload();
                MarkChanged();

                WriteState(
                    frame,
                    "settings_reloaded");
            }

            if (shiftDown && num3)
            {
                g_cycleModeRequested = true;
                g_captureRequested = true;

                WriteState(
                    frame,
                    "cycle_view_requested");
            }

            if (shiftDown && num4)
            {
                g_manualCaptureRequested = true;
                g_captureRequested = true;

                WriteState(
                    frame,
                    "manual_capture_requested");
            }

            if (shiftDown && num5)
            {
                gzfx::diagnostics::
                    semantic_probe::
                    RequestManualCapture();

                gzfx::render::wave3::
                    RequestManualCapture();

                gzfx::render::ambient_visibility_v2::
                    RequestManualCapture();

                gzfx::render::unified_product_renderer::
                    RequestManualCapture();

                WriteState(
                    frame,
                    "semantic_wave3_wave4_capture_requested");
            }
        }
        catch (...)
        {
        }
    }

    bool IsMenuVisible() noexcept
    {
        try
        {
            std::scoped_lock lock(g_mutex);
            return g_visible;
        }
        catch (...)
        {
            return false;
        }
    }

    void SubmitDirectInputMouse(
        const LONG deltaX,
        const LONG deltaY,
        const LONG wheel,
        const std::uint32_t buttonMask) noexcept
    {
        try
        {
            std::scoped_lock lock(g_mutex);

            if (!g_visible)
            {
                return;
            }

            g_directInputSeen = true;
            const std::uint64_t packet =
                ++g_directInputPackets;

            g_directInputDeltaX += deltaX;
            g_directInputDeltaY += deltaY;

            if (wheel != 0)
            {
                ScrollBy(
                    wheel > 0
                    ? -3
                    : 3);
            }

            g_cursor.x += deltaX;
            g_cursor.y += deltaY;
            ClampCursor();

            const int row =
                RowFromCursor();

            if (row >= 0)
            {
                g_selected =
                    static_cast<std::uint32_t>(row);
            }

            const bool leftDown =
                (buttonMask & 0x1u) != 0u;

            const bool leftPressed =
                leftDown && !g_leftDown;

            if (leftDown)
            {
                ApplyMouse(leftPressed);
            }
            else
            {
                g_dirty = true;
            }

            g_leftDown = leftDown;
            g_dirty = true;

            if (packet == 1 ||
                (packet % 120) == 0)
            {
                WriteState(
                    g_inputFrame,
                    "directinput_mouse");
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

    bool ConsumeManualCaptureRequest() noexcept
    {
        try
        {
            std::scoped_lock lock(g_mutex);

            const bool requested =
                g_manualCaptureRequested;

            g_manualCaptureRequested = false;
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

            overlay.srv =
                g_runtime.srv;

            overlay.width =
                kMenuWidth;

            overlay.height =
                kMenuHeight;

            return overlay.srv != nullptr;
        }
        catch (...)
        {
            return false;
        }
    }
}

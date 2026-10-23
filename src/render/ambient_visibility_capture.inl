        float HalfToFloat(const std::uint16_t value)
        {
            const std::uint32_t sign =
                static_cast<std::uint32_t>(value & 0x8000u) << 16;
            const std::uint32_t exponent = (value >> 10) & 0x1fu;
            std::uint32_t mantissa = value & 0x03ffu;
            std::uint32_t bits = 0;

            if (exponent == 0)
            {
                if (mantissa == 0)
                {
                    bits = sign;
                }
                else
                {
                    int unbiasedExponent = -14;
                    while ((mantissa & 0x0400u) == 0)
                    {
                        mantissa <<= 1;
                        --unbiasedExponent;
                    }

                    mantissa &= 0x03ffu;
                    bits =
                        sign |
                        (static_cast<std::uint32_t>(unbiasedExponent + 127) << 23) |
                        (mantissa << 13);
                }
            }
            else if (exponent == 31)
            {
                bits = sign | 0x7f800000u | (mantissa << 13);
            }
            else
            {
                const std::uint32_t adjusted = exponent + (127 - 15);
                bits = sign | (adjusted << 23) | (mantissa << 13);
            }

            return std::bit_cast<float>(bits);
        }

        bool WriteFieldBmp(
            const fs::path& path,
            const std::vector<float>& values,
            const UINT width,
            const UINT height,
            const bool color)
        {
            constexpr UINT outputWidth = 640;
            constexpr UINT outputHeight = 360;
            const UINT channelCount = color ? 3u : 1u;

            BITMAPFILEHEADER fileHeader{};
            BITMAPINFOHEADER infoHeader{};
            const DWORD imageBytes = outputWidth * outputHeight * 4;

            fileHeader.bfType = 0x4D42;
            fileHeader.bfOffBits =
                sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
            fileHeader.bfSize = fileHeader.bfOffBits + imageBytes;

            infoHeader.biSize = sizeof(BITMAPINFOHEADER);
            infoHeader.biWidth = static_cast<LONG>(outputWidth);
            infoHeader.biHeight = static_cast<LONG>(outputHeight);
            infoHeader.biPlanes = 1;
            infoHeader.biBitCount = 32;
            infoHeader.biCompression = BI_RGB;
            infoHeader.biSizeImage = imageBytes;

            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            if (!out)
            {
                return false;
            }

            out.write(
                reinterpret_cast<const char*>(&fileHeader),
                sizeof(fileHeader));
            out.write(
                reinterpret_cast<const char*>(&infoHeader),
                sizeof(infoHeader));

            std::vector<std::uint8_t> row(outputWidth * 4);

            for (UINT y = 0; y < outputHeight; ++y)
            {
                const UINT sourceY = std::min(
                    height - 1,
                    (outputHeight - 1 - y) * height / outputHeight);

                for (UINT x = 0; x < outputWidth; ++x)
                {
                    const UINT sourceX = std::min(
                        width - 1,
                        x * width / outputWidth);
                    const std::size_t sourceOffset =
                        (static_cast<std::size_t>(sourceY) * width + sourceX) *
                        channelCount;

                    float red = values[sourceOffset + 0];
                    float green = color ? values[sourceOffset + 1] : red;
                    float blue = color ? values[sourceOffset + 2] : red;

                    if (color)
                    {
                        red = std::pow(std::clamp(red, 0.0f, 1.0f), 1.0f / 2.2f);
                        green = std::pow(std::clamp(green, 0.0f, 1.0f), 1.0f / 2.2f);
                        blue = std::pow(std::clamp(blue, 0.0f, 1.0f), 1.0f / 2.2f);
                    }

                    row[x * 4 + 0] = static_cast<std::uint8_t>(
                        std::clamp(blue, 0.0f, 1.0f) * 255.0f + 0.5f);
                    row[x * 4 + 1] = static_cast<std::uint8_t>(
                        std::clamp(green, 0.0f, 1.0f) * 255.0f + 0.5f);
                    row[x * 4 + 2] = static_cast<std::uint8_t>(
                        std::clamp(red, 0.0f, 1.0f) * 255.0f + 0.5f);
                    row[x * 4 + 3] = 255;
                }

                out.write(
                    reinterpret_cast<const char*>(row.data()),
                    static_cast<std::streamsize>(row.size()));
            }

            return static_cast<bool>(out);
        }

        void CaptureFields(
            const std::uint64_t frame,
            ID3D11Device* device,
            ID3D11DeviceContext* context)
        {
            if (g_fieldCaptureDone ||
                frame < FieldCaptureFrame() ||
                !device ||
                !context ||
                !g_runtime.first.ao ||
                !g_runtime.first.indirect)
            {
                return;
            }

            D3D11_TEXTURE2D_DESC aoDesc{};
            g_runtime.first.ao->GetDesc(&aoDesc);
            D3D11_TEXTURE2D_DESC indirectDesc{};
            g_runtime.first.indirect->GetDesc(&indirectDesc);

            aoDesc.Usage = D3D11_USAGE_STAGING;
            aoDesc.BindFlags = 0;
            aoDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            aoDesc.MiscFlags = 0;

            indirectDesc.Usage = D3D11_USAGE_STAGING;
            indirectDesc.BindFlags = 0;
            indirectDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            indirectDesc.MiscFlags = 0;

            ID3D11Texture2D* aoStaging = nullptr;
            ID3D11Texture2D* indirectStaging = nullptr;

            if (FAILED(device->CreateTexture2D(&aoDesc, nullptr, &aoStaging)) ||
                FAILED(device->CreateTexture2D(
                    &indirectDesc,
                    nullptr,
                    &indirectStaging)))
            {
                if (aoStaging) aoStaging->Release();
                if (indirectStaging) indirectStaging->Release();
                return;
            }

            context->CopyResource(aoStaging, g_runtime.first.ao);
            context->CopyResource(indirectStaging, g_runtime.first.indirect);

            D3D11_MAPPED_SUBRESOURCE aoMapped{};
            D3D11_MAPPED_SUBRESOURCE indirectMapped{};

            if (FAILED(context->Map(
                    aoStaging,
                    0,
                    D3D11_MAP_READ,
                    0,
                    &aoMapped)) ||
                FAILED(context->Map(
                    indirectStaging,
                    0,
                    D3D11_MAP_READ,
                    0,
                    &indirectMapped)))
            {
                if (aoMapped.pData) context->Unmap(aoStaging, 0);
                if (indirectMapped.pData) context->Unmap(indirectStaging, 0);
                aoStaging->Release();
                indirectStaging->Release();
                return;
            }

            const std::size_t pixelCount =
                static_cast<std::size_t>(aoDesc.Width) * aoDesc.Height;
            std::vector<float> aoValues(pixelCount);
            std::vector<float> indirectValues(pixelCount * 3);

            double aoSum = 0.0;
            double indirectLumaSum = 0.0;
            float aoMinimum = std::numeric_limits<float>::max();
            float aoMaximum = 0.0f;
            float indirectMinimum = std::numeric_limits<float>::max();
            float indirectMaximum = 0.0f;
            std::uint64_t aoNonzero = 0;
            std::uint64_t indirectNonzero = 0;

            for (UINT y = 0; y < aoDesc.Height; ++y)
            {
                const auto* aoRow = reinterpret_cast<const std::uint16_t*>(
                    static_cast<const std::uint8_t*>(aoMapped.pData) +
                    static_cast<std::size_t>(y) * aoMapped.RowPitch);

                const auto* indirectRow = reinterpret_cast<const std::uint16_t*>(
                    static_cast<const std::uint8_t*>(indirectMapped.pData) +
                    static_cast<std::size_t>(y) * indirectMapped.RowPitch);

                for (UINT x = 0; x < aoDesc.Width; ++x)
                {
                    const std::size_t index =
                        static_cast<std::size_t>(y) * aoDesc.Width + x;
                    const float ao = std::max(HalfToFloat(aoRow[x]), 0.0f);
                    const float red = std::max(HalfToFloat(indirectRow[x * 4 + 0]), 0.0f);
                    const float green = std::max(HalfToFloat(indirectRow[x * 4 + 1]), 0.0f);
                    const float blue = std::max(HalfToFloat(indirectRow[x * 4 + 2]), 0.0f);
                    const float luma = red * 0.2126f + green * 0.7152f + blue * 0.0722f;

                    aoValues[index] = ao;
                    indirectValues[index * 3 + 0] = red;
                    indirectValues[index * 3 + 1] = green;
                    indirectValues[index * 3 + 2] = blue;

                    aoMinimum = std::min(aoMinimum, ao);
                    aoMaximum = std::max(aoMaximum, ao);
                    indirectMinimum = std::min(indirectMinimum, luma);
                    indirectMaximum = std::max(indirectMaximum, luma);
                    aoSum += ao;
                    indirectLumaSum += luma;
                    aoNonzero += ao > 0.001f ? 1 : 0;
                    indirectNonzero += luma > 0.001f ? 1 : 0;
                }
            }

            context->Unmap(aoStaging, 0);
            context->Unmap(indirectStaging, 0);
            aoStaging->Release();
            indirectStaging->Release();

            const fs::path root = gzfx::paths::DiagnosticsRoot();
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

            const double denominator = static_cast<double>(pixelCount);
            std::ofstream report(
                root / L"ambient_depth_fields.txt",
                std::ios::out | std::ios::trunc);

            if (report)
            {
                report
                    << "GZFX ambient visibility fields\n"
                    << "version=" << gzfx::kVersion << "\n"
                    << "frame=" << frame << "\n"
                    << "resolution=" << aoDesc.Width << "x" << aoDesc.Height << "\n"
                    << std::fixed << std::setprecision(8)
                    << "ao_min=" << aoMinimum << "\n"
                    << "ao_max=" << aoMaximum << "\n"
                    << "ao_mean=" << (aoSum / denominator) << "\n"
                    << "ao_nonzero_fraction="
                    << (static_cast<double>(aoNonzero) / denominator) << "\n"
                    << "ssil_luma_min=" << indirectMinimum << "\n"
                    << "ssil_luma_max=" << indirectMaximum << "\n"
                    << "ssil_luma_mean="
                    << (indirectLumaSum / denominator) << "\n"
                    << "ssil_nonzero_fraction="
                    << (static_cast<double>(indirectNonzero) / denominator) << "\n";
            }

            const bool aoWritten = WriteFieldBmp(
                root / L"ambient_depth_ao.bmp",
                aoValues,
                aoDesc.Width,
                aoDesc.Height,
                false);

            const bool indirectWritten = WriteFieldBmp(
                root / L"ambient_depth_ssil.bmp",
                indirectValues,
                aoDesc.Width,
                aoDesc.Height,
                true);

            g_fieldCaptureDone = aoWritten && indirectWritten;
        }
    }


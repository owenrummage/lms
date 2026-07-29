/*
 * Copyright (C) 2024 Emeric Poupon
 *
 * This file is part of LMS.
 *
 * LMS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * LMS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with LMS.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "image/Image.hpp"

#include <array>
#include <fstream>

#include <webp/decode.h>

#include "Exception.hpp"
#include "StbImage.hpp"
#include "StbImageWrite.hpp"

#include "core/ITraceLogger.hpp"
#include "image/Exception.hpp"

#include "EncodedImage.hpp"
#include "RawImage.hpp"

namespace lms::image
{
    void init(const std::filesystem::path& /*unused*/)
    {
    }

    std::span<const std::filesystem::path> getSupportedFileExtensions()
    {
        static const std::array<std::filesystem::path, 5> fileExtensions{ ".jpg", ".jpeg", ".png", ".bmp", ".webp" };
        return fileExtensions;
    }

    ImageProperties probeImage(const std::filesystem::path& path)
    {
        LMS_SCOPED_TRACE_DETAILED("Image", "ProbeFile");

        std::ifstream file{ path, std::ios::binary };
        if (!file) throw StbiException{ "Probe failed" };
        const std::string data{ std::istreambuf_iterator<char>{ file }, std::istreambuf_iterator<char>{} };
        return probeImage(std::as_bytes(std::span{ data.data(), data.size() }));
    }

    ImageProperties probeImage(std::span<const std::byte> encodedData)
    {
        LMS_SCOPED_TRACE_DETAILED("Image", "ProbeBuffer");

        int webpWidth{};
        int webpHeight{};
        if (::WebPGetInfo(reinterpret_cast<const uint8_t*>(encodedData.data()), encodedData.size(), &webpWidth, &webpHeight))
            return ImageProperties{ .width = static_cast<ImageSize>(webpWidth), .height = static_cast<ImageSize>(webpHeight) };

        int x{};
        int y{};
        int comp{};

        if (::stbi_info_from_memory(reinterpret_cast<const stbi_uc*>(encodedData.data()), static_cast<int>(encodedData.size()), &x, &y, &comp) == 0)
            throw StbiException{ "Probe failed" };

        ImageProperties properties;
        properties.width = x;
        properties.height = y;

        return properties;
    }

    std::unique_ptr<IRawImage> decodeImage(std::span<const std::byte> encodedData)
    {
        LMS_SCOPED_TRACE_DETAILED("Image", "DecodeBuffer");
        return std::make_unique<STB::RawImage>(encodedData);
    }

    std::unique_ptr<IRawImage> decodeImage(const std::filesystem::path& path)
    {
        LMS_SCOPED_TRACE_DETAILED("Image", "DecodeFile");
        return std::make_unique<STB::RawImage>(path);
    }

    std::unique_ptr<IEncodedImage> encodeToJPEG(const IRawImage& rawImage, unsigned quality)
    {
        LMS_SCOPED_TRACE_DETAILED("Image", "WriteJPEG");

        std::vector<std::byte> encodedData;

        auto writeCb{ [](void* ctx, void* writeData, int writeSize) {
            auto& output{ *reinterpret_cast<std::vector<std::byte>*>(ctx) };
            const std::size_t currentOutputSize{ output.size() };
            output.resize(currentOutputSize + writeSize);
            std::copy(reinterpret_cast<const std::byte*>(writeData), reinterpret_cast<const std::byte*>(writeData) + writeSize, output.data() + currentOutputSize);
        } };

        if (::stbi_write_jpg_to_func(writeCb, &encodedData, rawImage.getWidth(), rawImage.getHeight(), 3, static_cast<const STB::RawImage&>(rawImage).getData(), quality) == 0)
            throw Exception{ "Failed to export in jpeg format!" };

        return std::make_unique<EncodedImage>(std::move(encodedData), "image/jpeg");
    }
} // namespace lms::image

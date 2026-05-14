#include "TextureCooker.h"

// === Engine includes ===
#include <Asset/TextureAssetFormat.h>
#include <Engine/Source/Runtime/PAL/Win/ConvertUTF.h>

// === C++ includes ===
#include <cctype>
#include <cstring>
#include <span>
#include <string>
#include <vector>

// === ThirdParty includes ===
#include <DirectXTex.h>

namespace Cue::Editor
{
    namespace
    {
        [[nodiscard]] Result to_wide_path(
            const Core::IO::Path& a_path,
            std::wstring& outWidePath)
        {
            return PAL::Win::utf8_to_wide(a_path.normalize().utf8(), &outWidePath);
        }

        [[nodiscard]] Result to_color_format(
            DXGI_FORMAT a_dxgiFormat,
            RHI::ColorFormat& outFormat)
        {
            switch (a_dxgiFormat)
            {
            case DXGI_FORMAT_R8G8B8A8_UNORM:
                outFormat = RHI::ColorFormat::R8G8B8A8_UNORM;
                return Result::ok();
            case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
                outFormat = RHI::ColorFormat::R8G8B8A8_UNORM_SRGB;
                return Result::ok();
            case DXGI_FORMAT_BC6H_UF16:
                outFormat = RHI::ColorFormat::BC6H_UF16;
                return Result::ok();
            case DXGI_FORMAT_BC7_UNORM:
                outFormat = RHI::ColorFormat::BC7_UNORM;
                return Result::ok();
            case DXGI_FORMAT_BC7_UNORM_SRGB:
                outFormat = RHI::ColorFormat::BC7_UNORM_SRGB;
                return Result::ok();
            default:
                return Result::fail(
                    Code::Unsupported,
                    Severity::Error,
                    "Texture format is not supported for cuetexture.");
            }
        }

        [[nodiscard]] std::string to_lower_ascii(std::string a_text) noexcept
        {
            for (char& character : a_text)
            {
                character = static_cast<char>(std::tolower(
                    static_cast<unsigned char>(character)));
            }
            return a_text;
        }

        [[nodiscard]] Result save_cuetexture_from_scratch_image(
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_filePath,
            const DirectX::ScratchImage& a_image)
        {
            const Core::IO::Path normalizedPath = a_filePath.normalize();
            if (normalizedPath.extension() != ".cuetexture")
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Texture asset file extension must be .cuetexture.");
            }

            const DirectX::TexMetadata metadata = a_image.GetMetadata();
            const bool isCubeMap = metadata.IsCubemap();
            if (metadata.dimension != DirectX::TEX_DIMENSION_TEXTURE2D ||
                (!isCubeMap && metadata.arraySize != 1) ||
                (isCubeMap && metadata.arraySize != 6) ||
                metadata.depth != 1 ||
                metadata.mipLevels == 0)
            {
                return Result::fail(
                    Code::Unsupported,
                    Severity::Error,
                    "Only 2D textures and single CubeMap textures are supported for cuetexture.");
            }

            RHI::ColorFormat colorFormat{};
            Result result = to_color_format(metadata.format, colorFormat);
            if (!result)
            {
                return result;
            }

            uint64_t totalDataSize = 0;
            std::vector<CueTextureMipInfo> mipInfos{};
            mipInfos.reserve(
                static_cast<size_t>(metadata.mipLevels * metadata.arraySize));
            for (size_t itemIndex = 0; itemIndex < metadata.arraySize; ++itemIndex)
            {
                for (size_t mipIndex = 0; mipIndex < metadata.mipLevels; ++mipIndex)
                {
                    const DirectX::Image* image =
                        a_image.GetImage(mipIndex, itemIndex, 0);
                    if (image == nullptr || image->pixels == nullptr)
                    {
                        return Result::fail(
                            Code::GetFailed,
                            Severity::Error,
                            "Texture mip image could not be resolved.");
                    }

                    CueTextureMipInfo mipInfo{};
                    mipInfo.width = static_cast<uint32_t>(image->width);
                    mipInfo.height = static_cast<uint32_t>(image->height);
                    mipInfo.rowPitch = static_cast<uint32_t>(image->rowPitch);
                    mipInfo.slicePitch = static_cast<uint32_t>(image->slicePitch);
                    mipInfo.offset = totalDataSize;
                    mipInfo.size = image->slicePitch;
                    mipInfos.push_back(mipInfo);
                    totalDataSize += mipInfo.size;
                }
            }

            CueTextureHeader header{};
            header.magic = k_cueTextureMagic;
            header.version = k_cueTextureVersion;
            header.width = static_cast<uint32_t>(metadata.width);
            header.height = static_cast<uint32_t>(metadata.height);
            header.mipCount = static_cast<uint32_t>(metadata.mipLevels);
            header.arraySize = static_cast<uint32_t>(metadata.arraySize);
            header.format = static_cast<uint32_t>(colorFormat);
            header.flags =
                (DirectX::IsSRGB(metadata.format) ? k_cueTextureFlagSrgb : 0) |
                (isCubeMap ? k_cueTextureFlagCubeMap : 0);
            header.dataSize = totalDataSize;

            const size_t headerSize =
                sizeof(CueTextureHeader) +
                sizeof(CueTextureMipInfo) * mipInfos.size();
            std::vector<std::byte> fileData(headerSize + static_cast<size_t>(totalDataSize));
            std::memcpy(fileData.data(), &header, sizeof(CueTextureHeader));
            std::memcpy(
                fileData.data() + sizeof(CueTextureHeader),
                mipInfos.data(),
                sizeof(CueTextureMipInfo) * mipInfos.size());

            size_t writeOffset = headerSize;
            for (size_t itemIndex = 0; itemIndex < metadata.arraySize; ++itemIndex)
            {
                for (size_t mipIndex = 0; mipIndex < metadata.mipLevels; ++mipIndex)
                {
                    const DirectX::Image* image =
                        a_image.GetImage(mipIndex, itemIndex, 0);
                    std::memcpy(
                        fileData.data() + writeOffset,
                        image->pixels,
                        image->slicePitch);
                    writeOffset += image->slicePitch;
                }
            }

            return a_fileSystem.write_all(
                normalizedPath,
                std::span<const std::byte>(fileData.data(), fileData.size()),
                true);
        }
    }

    Result TextureCooker::cook_source_to_cuetexture(
        Core::IO::IFileSystem& a_fileSystem,
        const Core::IO::Path& a_sourcePath,
        const Core::IO::Path& a_destinationPath) noexcept
    {
        std::wstring wideSourcePath{};
        Result result = to_wide_path(a_sourcePath, wideSourcePath);
        if (!result)
        {
            return result;
        }

        DirectX::TexMetadata metadata{};
        DirectX::ScratchImage sourceImage{};
        HRESULT hr = S_OK;
        const std::string extension = to_lower_ascii(a_sourcePath.extension());
        if (extension == ".dds")
        {
            hr = DirectX::LoadFromDDSFile(
                wideSourcePath.c_str(),
                DirectX::DDS_FLAGS_NONE,
                &metadata,
                sourceImage);
        }
        else
        {
            hr = DirectX::LoadFromWICFile(
                wideSourcePath.c_str(),
                DirectX::WIC_FLAGS_FORCE_RGB,
                &metadata,
                sourceImage);
        }
        if (FAILED(hr))
        {
            return Result::fail(
                PAL::Win::convert_hresult_code(hr),
                Severity::Error,
                "Failed to load source texture with DirectXTex.");
        }

        const DirectX::TexMetadata sourceMetadata = sourceImage.GetMetadata();
        if (sourceMetadata.IsCubemap())
        {
            return save_cuetexture_from_scratch_image(
                a_fileSystem,
                a_destinationPath,
                sourceImage);
        }

        DirectX::ScratchImage mipChain{};
        hr = DirectX::GenerateMipMaps(
            sourceImage.GetImages(),
            sourceImage.GetImageCount(),
            sourceMetadata,
            DirectX::TEX_FILTER_DEFAULT,
            0,
            mipChain);

        const DirectX::ScratchImage* outputImage = &sourceImage;
        if (SUCCEEDED(hr) && mipChain.GetImageCount() > 0)
        {
            outputImage = &mipChain;
        }

        return save_cuetexture_from_scratch_image(
            a_fileSystem,
            a_destinationPath,
            *outputImage);
    }

    Result TextureCooker::ensure_cuetexture_is_up_to_date(
        Core::IO::IFileSystem& a_fileSystem,
        const Core::IO::Path& a_sourcePath,
        const Core::IO::Path& a_destinationPath) noexcept
    {
        bool cookedTextureExists = false;
        Result result = a_fileSystem.exists(
            a_destinationPath, &cookedTextureExists);
        if (!result)
        {
            return result;
        }

        bool shouldRecook = !cookedTextureExists;
        if (!shouldRecook)
        {
            Core::IO::FileStat sourceStat{};
            result = a_fileSystem.stat(a_sourcePath, &sourceStat);
            if (!result)
            {
                return result;
            }

            Core::IO::FileStat cookedStat{};
            result = a_fileSystem.stat(a_destinationPath, &cookedStat);
            if (!result)
            {
                return result;
            }

            shouldRecook = sourceStat.mtime_ns > cookedStat.mtime_ns;
        }

        if (!shouldRecook)
        {
            return Result::ok();
        }

        return cook_source_to_cuetexture(
            a_fileSystem,
            a_sourcePath,
            a_destinationPath);
    }
}

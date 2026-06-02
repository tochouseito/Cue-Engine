#include "AssetManager.h"

// === Engine includes ===
#include <GpuData/Particle.h>

// === C++ includes ===
#include <algorithm>
#include <cctype>
#include <cstring>
#include <limits>
#include <string_view>
#include <span>
#include <type_traits>

// === ThirdParty includes ===
#include <nlohmann/json.hpp>

namespace Cue
{
    namespace
    {
        using Json = nlohmann::json;

        struct LoadedTextureMipData final
        {
            std::vector<std::byte> pixels{};
            uint32_t width = 0;
            uint32_t height = 0;
            uint32_t rowPitch = 0;
            uint32_t slicePitch = 0;
        };

        struct LoadedTextureData final
        {
            std::vector<LoadedTextureMipData> mipData{};
            RHI::ColorFormat format = RHI::ColorFormat::R8G8B8A8_UNORM;
            uint32_t width = 0;
            uint32_t height = 0;
            uint32_t mipLevels = 1;
            uint32_t arraySize = 1;
            uint32_t flags = 0;
        };

        struct PreparedTextureUpload final
        {
            RHI::TextureDesc desc{};
            std::vector<RHI::TextureSubresourceData> subresources{};
        };

        static_assert(std::is_trivially_copyable_v<CueTextureHeader>);
        static_assert(std::is_trivially_copyable_v<CueTextureMipInfo>);
        static_assert(std::is_trivially_copyable_v<CueModelHeader>);
        static_assert(std::is_trivially_copyable_v<CueModelHeaderV3>);
        static_assert(std::is_trivially_copyable_v<CueModelLegacyHeader>);
        static_assert(std::is_trivially_copyable_v<CueModelMeshInfoV2>);
        static_assert(std::is_trivially_copyable_v<CueModelMeshInfo>);
        static_assert(std::is_trivially_copyable_v<CueModelMaterialInfo>);
        static_assert(std::is_trivially_copyable_v<CueModelRenderPartInfo>);
        static_assert(std::is_trivially_copyable_v<CueModelRenderPartInfoV3>);
        static_assert(std::is_trivially_copyable_v<CueModelSkeletonJointInfo>);
        static_assert(std::is_trivially_copyable_v<CueModelAnimationClipInfo>);
        static_assert(
            std::is_trivially_copyable_v<CueModelAnimationChannelInfo>);

        [[nodiscard]] std::string to_lower_ascii(std::string a_text) noexcept
        {
            for (char& character : a_text)
            {
                character = static_cast<char>(std::tolower(
                    static_cast<unsigned char>(character)));
            }
            return a_text;
        }

        [[nodiscard]] Json serialize_float4(const Math::float4& value)
        {
            return Json{
                { "x", value.x },
                { "y", value.y },
                { "z", value.z },
                { "w", value.w },
            };
        }

        void deserialize_float4(const Json& json, Math::float4& outValue)
        {
            outValue.x = json.at("x").get<float>();
            outValue.y = json.at("y").get<float>();
            outValue.z = json.at("z").get<float>();
            outValue.w = json.at("w").get<float>();
        }

        void deserialize_float3_or_keep(const Json& json, Math::float3& outValue)
        {
            if (json.is_array() && json.size() >= 3)
            {
                outValue.x = json.at(0).get<float>();
                outValue.y = json.at(1).get<float>();
                outValue.z = json.at(2).get<float>();
                return;
            }

            outValue.x = json.value("x", outValue.x);
            outValue.y = json.value("y", outValue.y);
            outValue.z = json.value("z", outValue.z);
        }

        void deserialize_float2_or_keep(const Json& json, Math::float2& outValue)
        {
            if (json.is_array() && json.size() >= 2)
            {
                outValue.x = json.at(0).get<float>();
                outValue.y = json.at(1).get<float>();
                return;
            }

            outValue.x = json.value("x", outValue.x);
            outValue.y = json.value("y", outValue.y);
        }

        void deserialize_float4_or_keep(const Json& json, Math::float4& outValue)
        {
            if (json.is_array() && json.size() >= 4)
            {
                outValue.x = json.at(0).get<float>();
                outValue.y = json.at(1).get<float>();
                outValue.z = json.at(2).get<float>();
                outValue.w = json.at(3).get<float>();
                return;
            }

            outValue.x = json.value("x", outValue.x);
            outValue.y = json.value("y", outValue.y);
            outValue.z = json.value("z", outValue.z);
            outValue.w = json.value("w", outValue.w);
        }

        [[nodiscard]] EffectSystem::EffectBillboardMode
            parse_effect_billboard_mode(std::string_view a_text) noexcept
        {
            const std::string mode = to_lower_ascii(std::string(a_text));
            if (mode == "view")
            {
                return EffectSystem::EffectBillboardMode::View;
            }

            return EffectSystem::EffectBillboardMode::View;
        }

        [[nodiscard]] EffectSystem::EffectRendererType
            parse_effect_renderer_type(std::string_view a_text) noexcept
        {
            const std::string type = to_lower_ascii(std::string(a_text));
            if (type == "trail")
            {
                return EffectSystem::EffectRendererType::Trail;
            }
            if (type == "ribbon")
            {
                return EffectSystem::EffectRendererType::Ribbon;
            }
            if (type == "mesh")
            {
                return EffectSystem::EffectRendererType::Mesh;
            }

            return EffectSystem::EffectRendererType::Billboard;
        }

        [[nodiscard]] EffectSystem::EffectEmitterShape parse_effect_shape(
            std::string_view a_text) noexcept
        {
            const std::string shape = to_lower_ascii(std::string(a_text));
            if (shape == "sphere")
            {
                return EffectSystem::EffectEmitterShape::Sphere;
            }
            if (shape == "box")
            {
                return EffectSystem::EffectEmitterShape::Box;
            }
            if (shape == "cone")
            {
                return EffectSystem::EffectEmitterShape::Cone;
            }

            return EffectSystem::EffectEmitterShape::Point;
        }

        void clamp_effect_emitter(EffectSystem::EffectEmitterDesc& a_emitter)
        {
            a_emitter.startSize = (std::max)(a_emitter.startSize, 0.0f);
            a_emitter.midSize = (std::max)(a_emitter.midSize, 0.0f);
            a_emitter.endSize = (std::max)(a_emitter.endSize, 0.0f);
            a_emitter.curveMidTime =
                (std::clamp)(a_emitter.curveMidTime, 0.001f, 0.999f);
            a_emitter.startDelay = (std::max)(a_emitter.startDelay, 0.0f);
            a_emitter.duration = (std::max)(a_emitter.duration, 0.0f);
            a_emitter.shapeRadius = (std::max)(a_emitter.shapeRadius, 0.0f);
            a_emitter.shapeAngleDegrees =
                (std::clamp)(a_emitter.shapeAngleDegrees, 0.0f, 89.0f);
            a_emitter.trailWidth = (std::max)(a_emitter.trailWidth, 0.0f);
            a_emitter.trailLength = (std::max)(a_emitter.trailLength, 0.0f);
            a_emitter.meshScale = (std::max)(a_emitter.meshScale, 0.0f);
            a_emitter.drag = (std::max)(a_emitter.drag, 0.0f);
            a_emitter.noiseStrength =
                (std::max)(a_emitter.noiseStrength, 0.0f);
            a_emitter.noiseFrequency =
                (std::max)(a_emitter.noiseFrequency, 0.001f);
            a_emitter.trailSegmentCount = (std::clamp)(
                a_emitter.trailSegmentCount,
                1u,
                64u);
            a_emitter.shapeBoxExtents.x =
                (std::max)(a_emitter.shapeBoxExtents.x, 0.0f);
            a_emitter.shapeBoxExtents.y =
                (std::max)(a_emitter.shapeBoxExtents.y, 0.0f);
            a_emitter.shapeBoxExtents.z =
                (std::max)(a_emitter.shapeBoxExtents.z, 0.0f);
            a_emitter.minLifetime = (std::max)(a_emitter.minLifetime, 0.001f);
            a_emitter.maxLifetime =
                (std::max)(a_emitter.maxLifetime, a_emitter.minLifetime);
            a_emitter.emitRate = (std::max)(a_emitter.emitRate, 0.0f);
            a_emitter.maxParticleCount = (std::clamp)(
                a_emitter.maxParticleCount,
                1u,
                GpuData::k_maxParticleCount);
        }

        [[nodiscard]] Result validate_loaded_texture_data(
            const LoadedTextureData& a_loadedTextureData)
        {
            if (a_loadedTextureData.width == 0 ||
                a_loadedTextureData.height == 0 ||
                a_loadedTextureData.mipLevels == 0 ||
                a_loadedTextureData.arraySize == 0 ||
                a_loadedTextureData.mipData.empty())
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Loaded texture data is invalid.");
            }

            const uint64_t expectedSubresourceCount =
                static_cast<uint64_t>(a_loadedTextureData.mipLevels) *
                static_cast<uint64_t>(a_loadedTextureData.arraySize);
            if (a_loadedTextureData.mipData.size() != expectedSubresourceCount)
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Loaded texture subresource count is invalid.");
            }

            const bool isCubeMap =
                (a_loadedTextureData.flags & k_cueTextureFlagCubeMap) != 0;
            if (isCubeMap && a_loadedTextureData.arraySize != 6)
            {
                return Result::fail(
                    Code::Unsupported,
                    Severity::Error,
                    "CubeMap texture must contain 6 array slices.");
            }
            if (!isCubeMap && a_loadedTextureData.arraySize != 1)
            {
                return Result::fail(
                    Code::Unsupported,
                    Severity::Error,
                    "Only single 2D textures and CubeMap textures are supported.");
            }

            for (const LoadedTextureMipData& mipData : a_loadedTextureData.mipData)
            {
                if (mipData.width == 0 ||
                    mipData.height == 0 ||
                    mipData.rowPitch == 0 ||
                    mipData.slicePitch == 0 ||
                    mipData.pixels.empty() ||
                    mipData.pixels.size() != mipData.slicePitch)
                {
                    return Result::fail(
                        Code::InvalidArgument,
                        Severity::Error,
                        "Loaded texture mip data is invalid.");
                }
            }

            return Result::ok();
        }

        [[nodiscard]] Result build_texture_upload(
            const LoadedTextureData& a_loadedTextureData,
            PreparedTextureUpload& outUpload)
        {
            Result result = validate_loaded_texture_data(a_loadedTextureData);
            if (!result)
            {
                return result;
            }

            outUpload = {};
            outUpload.desc.width = a_loadedTextureData.width;
            outUpload.desc.height = a_loadedTextureData.height;
            outUpload.desc.mipLevels =
                static_cast<uint16_t>(a_loadedTextureData.mipLevels);
            outUpload.desc.arraySize =
                static_cast<uint16_t>(a_loadedTextureData.arraySize);
            outUpload.desc.type =
                (a_loadedTextureData.flags & k_cueTextureFlagCubeMap) != 0
                ? RHI::TextureType::CubeMap
                : RHI::TextureType::Texture2D;
            outUpload.desc.format = a_loadedTextureData.format;
            outUpload.subresources.reserve(a_loadedTextureData.mipData.size());

            for (const LoadedTextureMipData& mipData : a_loadedTextureData.mipData)
            {
                RHI::TextureSubresourceData subresource{};
                subresource.data = mipData.pixels.data();
                subresource.dataSize = mipData.pixels.size();
                subresource.rowPitch = mipData.rowPitch;
                subresource.slicePitch = mipData.slicePitch;
                outUpload.subresources.push_back(subresource);
            }

            return Result::ok();
        }

        [[nodiscard]] Result upload_texture(
            RHI::ITextureManager& a_textureManager,
            std::string_view a_name,
            const LoadedTextureData& a_loadedTextureData,
            RHI::TextureHandle& outTextureHandle)
        {
            PreparedTextureUpload upload{};
            Result result = build_texture_upload(a_loadedTextureData, upload);
            if (!result)
            {
                return result;
            }

            upload.desc.name = std::string(a_name);
            return a_textureManager.create_texture(
                upload.desc,
                std::span<const RHI::TextureSubresourceData>(
                    upload.subresources.data(),
                    upload.subresources.size()),
                outTextureHandle);
        }

        [[nodiscard]] Result save_cuetexture(
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_filePath,
            const LoadedTextureData& a_loadedTextureData)
        {
            const Core::IO::Path normalizedPath = a_filePath.normalize();
            if (normalizedPath.extension() != ".cuetexture")
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Texture asset file extension must be .cuetexture.");
            }

            Result result = validate_loaded_texture_data(a_loadedTextureData);
            if (!result)
            {
                return result;
            }

            uint64_t totalDataSize = 0;
            std::vector<CueTextureMipInfo> mipInfos{};
            mipInfos.reserve(a_loadedTextureData.mipData.size());
            for (const LoadedTextureMipData& mipData : a_loadedTextureData.mipData)
            {
                CueTextureMipInfo mipInfo{};
                mipInfo.width = mipData.width;
                mipInfo.height = mipData.height;
                mipInfo.rowPitch = mipData.rowPitch;
                mipInfo.slicePitch = mipData.slicePitch;
                mipInfo.offset = totalDataSize;
                mipInfo.size = mipData.pixels.size();
                mipInfos.push_back(mipInfo);
                totalDataSize += mipInfo.size;
            }

            CueTextureHeader header{};
            header.magic = k_cueTextureMagic;
            header.version = k_cueTextureVersion;
            header.width = a_loadedTextureData.width;
            header.height = a_loadedTextureData.height;
            header.mipCount = a_loadedTextureData.mipLevels;
            header.arraySize = a_loadedTextureData.arraySize;
            header.format = static_cast<uint32_t>(a_loadedTextureData.format);
            header.flags = a_loadedTextureData.flags;
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
            for (const LoadedTextureMipData& mipData : a_loadedTextureData.mipData)
            {
                std::memcpy(
                    fileData.data() + writeOffset,
                    mipData.pixels.data(),
                    mipData.pixels.size());
                writeOffset += mipData.pixels.size();
            }

            return a_fileSystem.write_all(
                normalizedPath,
                std::span<const std::byte>(fileData.data(), fileData.size()),
                true);
        }

        [[nodiscard]] Result load_cuetexture(
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_filePath,
            LoadedTextureData& outTextureData)
        {
            const Core::IO::Path normalizedPath = a_filePath.normalize();
            if (normalizedPath.extension() != ".cuetexture")
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Texture asset file extension must be .cuetexture.");
            }

            std::vector<std::byte> fileData{};
            Result result = a_fileSystem.read_all(normalizedPath, &fileData);
            if (!result)
            {
                return result;
            }

            if (fileData.size() < sizeof(CueTextureHeader) + sizeof(CueTextureMipInfo))
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Cooked texture file is too small.");
            }

            CueTextureHeader header{};
            std::memcpy(&header, fileData.data(), sizeof(CueTextureHeader));
            if (header.magic != k_cueTextureMagic ||
                header.version != k_cueTextureVersion ||
                header.mipCount == 0 ||
                header.arraySize == 0)
            {
                return Result::fail(
                    Code::Unsupported,
                    Severity::Error,
                    "Cooked texture format is not supported.");
            }

            const uint64_t loadedSubresourceCount =
                static_cast<uint64_t>(header.mipCount) *
                static_cast<uint64_t>(header.arraySize);
            if (loadedSubresourceCount > std::numeric_limits<uint32_t>::max())
            {
                return Result::fail(
                    Code::Unsupported,
                    Severity::Error,
                    "Cooked texture subresource count is too large.");
            }

            const size_t mipInfoTableSize =
                sizeof(CueTextureMipInfo) *
                static_cast<size_t>(loadedSubresourceCount);
            const size_t pixelDataBegin = sizeof(CueTextureHeader) + mipInfoTableSize;
            if (fileData.size() < pixelDataBegin + static_cast<size_t>(header.dataSize))
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Cooked texture payload is invalid.");
            }

            outTextureData = {};
            outTextureData.width = header.width;
            outTextureData.height = header.height;
            outTextureData.mipLevels = header.mipCount;
            outTextureData.arraySize = header.arraySize;
            outTextureData.format = static_cast<RHI::ColorFormat>(header.format);
            outTextureData.flags = header.flags;
            outTextureData.mipData.reserve(
                static_cast<size_t>(loadedSubresourceCount));

            for (uint32_t subresourceIndex = 0;
                 subresourceIndex < static_cast<uint32_t>(loadedSubresourceCount);
                 ++subresourceIndex)
            {
                CueTextureMipInfo mipInfo{};
                std::memcpy(
                    &mipInfo,
                    fileData.data() + sizeof(CueTextureHeader) +
                        sizeof(CueTextureMipInfo) *
                            static_cast<size_t>(subresourceIndex),
                    sizeof(CueTextureMipInfo));

                const uint64_t pixelDataOffset = pixelDataBegin + mipInfo.offset;
                if (pixelDataOffset + mipInfo.size > fileData.size() ||
                    mipInfo.slicePitch != mipInfo.size)
                {
                    return Result::fail(
                        Code::InvalidArgument,
                        Severity::Error,
                        "Cooked texture payload is invalid.");
                }

                LoadedTextureMipData mipData{};
                mipData.width = mipInfo.width;
                mipData.height = mipInfo.height;
                mipData.rowPitch = mipInfo.rowPitch;
                mipData.slicePitch = mipInfo.slicePitch;
                mipData.pixels.resize(static_cast<size_t>(mipInfo.size));
                std::memcpy(
                    mipData.pixels.data(),
                    fileData.data() + static_cast<size_t>(pixelDataOffset),
                    static_cast<size_t>(mipInfo.size));
                outTextureData.mipData.push_back(std::move(mipData));
            }

            return validate_loaded_texture_data(outTextureData);
        }

        template <typename T>
        [[nodiscard]] Result copy_payload_bytes(
            const std::vector<std::byte>& a_fileData,
            size_t a_payloadBegin,
            uint64_t a_offset,
            T* a_destination,
            size_t a_count)
        {
            if (a_count == 0)
            {
                return Result::ok();
            }

            if (a_destination == nullptr)
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Destination buffer is null.");
            }

            const uint64_t byteCount = sizeof(T) * static_cast<uint64_t>(a_count);
            const uint64_t readBegin = static_cast<uint64_t>(a_payloadBegin) + a_offset;
            const uint64_t readEnd = readBegin + byteCount;
            if (readEnd > a_fileData.size())
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Cooked model payload is invalid.");
            }

            std::memcpy(
                a_destination,
                a_fileData.data() + static_cast<size_t>(readBegin),
                static_cast<size_t>(byteCount));
            return Result::ok();
        }

        [[nodiscard]] Result load_cuemodel(
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_filePath,
            Core::Native::ModelData& outModelData)
        {
            const Core::IO::Path normalizedPath = a_filePath.normalize();
            if (normalizedPath.extension() != ".cuemodel")
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Model asset file extension must be .cuemodel.");
            }

            std::vector<std::byte> fileData{};
            Result result = a_fileSystem.read_all(normalizedPath, &fileData);
            if (!result)
            {
                return result;
            }

            if (fileData.size() <
                sizeof(CueModelLegacyHeader) + sizeof(CueModelMeshInfoV2))
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Cooked model file is too small.");
            }

            CueModelLegacyHeader legacyHeader{};
            std::memcpy(
                &legacyHeader,
                fileData.data(),
                sizeof(CueModelLegacyHeader));
            if (legacyHeader.magic != k_cueModelMagic ||
                legacyHeader.meshCount == 0)
            {
                return Result::fail(
                    Code::Unsupported,
                    Severity::Error,
                    "Cooked model format is not supported.");
            }

            size_t meshInfoBegin = 0;
            size_t materialInfoBegin = 0;
            size_t renderPartInfoBegin = 0;
            size_t jointInfoBegin = 0;
            size_t animationClipInfoBegin = 0;
            size_t payloadBegin = 0;
            uint32_t materialCount = 0;
            uint32_t renderPartCount = 0;
            uint32_t jointCount = 0;
            uint32_t animationClipCount = 0;
            uint64_t dataSize = 0;
            bool isVersion3 = false;
            bool isVersion4 = false;

            if (legacyHeader.version == k_cueModelLegacyVersion)
            {
                const size_t meshInfoTableSize =
                    sizeof(CueModelMeshInfoV2) *
                    static_cast<size_t>(legacyHeader.meshCount);
                meshInfoBegin = sizeof(CueModelLegacyHeader);
                payloadBegin = meshInfoBegin + meshInfoTableSize;
                dataSize = legacyHeader.dataSize;
            }
            else if (legacyHeader.version == k_cueModelVersion2)
            {
                if (fileData.size() < sizeof(CueModelHeader))
                {
                    return Result::fail(
                        Code::InvalidArgument,
                        Severity::Error,
                        "Cooked model file is too small.");
                }

                CueModelHeader header{};
                std::memcpy(&header, fileData.data(), sizeof(CueModelHeader));
                if (header.magic != k_cueModelMagic ||
                    header.meshCount == 0 ||
                    header.renderPartCount == 0)
                {
                    return Result::fail(
                        Code::Unsupported,
                        Severity::Error,
                        "Cooked model format is not supported.");
                }

                const size_t meshInfoTableSize =
                    sizeof(CueModelMeshInfoV2) *
                    static_cast<size_t>(header.meshCount);
                const size_t materialInfoTableSize =
                    sizeof(CueModelMaterialInfo) *
                    static_cast<size_t>(header.materialCount);
                const size_t renderPartInfoTableSize =
                    sizeof(CueModelRenderPartInfoV3) *
                    static_cast<size_t>(header.renderPartCount);
                meshInfoBegin = sizeof(CueModelHeader);
                materialInfoBegin = meshInfoBegin + meshInfoTableSize;
                renderPartInfoBegin = materialInfoBegin + materialInfoTableSize;
                payloadBegin = renderPartInfoBegin + renderPartInfoTableSize;
                materialCount = header.materialCount;
                renderPartCount = header.renderPartCount;
                dataSize = header.dataSize;
            }
            else if (legacyHeader.version == k_cueModelVersion3 ||
                     legacyHeader.version == k_cueModelVersion)
            {
                if (fileData.size() < sizeof(CueModelHeaderV3))
                {
                    return Result::fail(
                        Code::InvalidArgument,
                        Severity::Error,
                        "Cooked model file is too small.");
                }

                CueModelHeaderV3 header{};
                std::memcpy(&header, fileData.data(), sizeof(CueModelHeaderV3));
                if (header.magic != k_cueModelMagic ||
                    header.meshCount == 0 ||
                    header.renderPartCount == 0)
                {
                    return Result::fail(
                        Code::Unsupported,
                        Severity::Error,
                        "Cooked model format is not supported.");
                }

                const size_t meshInfoTableSize =
                    sizeof(CueModelMeshInfo) *
                    static_cast<size_t>(header.meshCount);
                const size_t materialInfoTableSize =
                    sizeof(CueModelMaterialInfo) *
                    static_cast<size_t>(header.materialCount);
                const size_t renderPartInfoTableSize =
                    (legacyHeader.version == k_cueModelVersion
                            ? sizeof(CueModelRenderPartInfo)
                            : sizeof(CueModelRenderPartInfoV3)) *
                    static_cast<size_t>(header.renderPartCount);
                const size_t jointInfoTableSize =
                    sizeof(CueModelSkeletonJointInfo) *
                    static_cast<size_t>(header.jointCount);
                const size_t animationClipInfoTableSize =
                    sizeof(CueModelAnimationClipInfo) *
                    static_cast<size_t>(header.animationClipCount);
                meshInfoBegin = sizeof(CueModelHeaderV3);
                materialInfoBegin = meshInfoBegin + meshInfoTableSize;
                renderPartInfoBegin = materialInfoBegin + materialInfoTableSize;
                jointInfoBegin = renderPartInfoBegin + renderPartInfoTableSize;
                animationClipInfoBegin = jointInfoBegin + jointInfoTableSize;
                payloadBegin =
                    animationClipInfoBegin + animationClipInfoTableSize;
                materialCount = header.materialCount;
                renderPartCount = header.renderPartCount;
                jointCount = header.jointCount;
                animationClipCount = header.animationClipCount;
                dataSize = header.dataSize;
                isVersion3 = true;
                isVersion4 = legacyHeader.version == k_cueModelVersion;
            }
            else
            {
                return Result::fail(
                    Code::Unsupported,
                    Severity::Error,
                    "Cooked model format is not supported.");
            }

            if (fileData.size() < payloadBegin + static_cast<size_t>(dataSize))
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Cooked model payload is invalid.");
            }

            outModelData = {};
            outModelData.meshes.reserve(legacyHeader.meshCount);
            for (uint32_t meshIndex = 0;
                 meshIndex < legacyHeader.meshCount;
                 ++meshIndex)
            {
                CueModelMeshInfo meshInfo{};
                if (isVersion3)
                {
                    std::memcpy(
                        &meshInfo,
                        fileData.data() + meshInfoBegin +
                            sizeof(CueModelMeshInfo) *
                                static_cast<size_t>(meshIndex),
                        sizeof(CueModelMeshInfo));
                }
                else
                {
                    CueModelMeshInfoV2 meshInfoV2{};
                    std::memcpy(
                        &meshInfoV2,
                        fileData.data() + meshInfoBegin +
                            sizeof(CueModelMeshInfoV2) *
                                static_cast<size_t>(meshIndex),
                        sizeof(CueModelMeshInfoV2));
                    meshInfo.nameSize = meshInfoV2.nameSize;
                    meshInfo.vertexCount = meshInfoV2.vertexCount;
                    meshInfo.indexCount = meshInfoV2.indexCount;
                    meshInfo.flags = meshInfoV2.flags;
                    meshInfo.nameOffset = meshInfoV2.nameOffset;
                    meshInfo.positionsOffset = meshInfoV2.positionsOffset;
                    meshInfo.uvsOffset = meshInfoV2.uvsOffset;
                    meshInfo.normalsOffset = meshInfoV2.normalsOffset;
                    meshInfo.indicesOffset = meshInfoV2.indicesOffset;
                }

                if (meshInfo.vertexCount == 0 || meshInfo.indexCount == 0)
                {
                    return Result::fail(
                        Code::InvalidArgument,
                        Severity::Error,
                        "Cooked model mesh is empty.");
                }

                Core::Native::MeshData meshData{};
                meshData.name.resize(meshInfo.nameSize);
                result = copy_payload_bytes(fileData, payloadBegin,
                    meshInfo.nameOffset, meshData.name.data(),
                    meshData.name.size());
                if (!result)
                {
                    return result;
                }

                meshData.positions.resize(meshInfo.vertexCount);
                result = copy_payload_bytes(fileData, payloadBegin,
                    meshInfo.positionsOffset, meshData.positions.data(),
                    meshData.positions.size());
                if (!result)
                {
                    return result;
                }

                if ((meshInfo.flags & k_cueModelMeshFlagHasUvs) != 0)
                {
                    meshData.uvs.resize(meshInfo.vertexCount);
                    result = copy_payload_bytes(fileData, payloadBegin,
                        meshInfo.uvsOffset, meshData.uvs.data(),
                        meshData.uvs.size());
                    if (!result)
                    {
                        return result;
                    }
                }

                if ((meshInfo.flags & k_cueModelMeshFlagHasNormals) != 0)
                {
                    meshData.normals.resize(meshInfo.vertexCount);
                    result = copy_payload_bytes(fileData, payloadBegin,
                        meshInfo.normalsOffset, meshData.normals.data(),
                        meshData.normals.size());
                    if (!result)
                    {
                        return result;
                    }
                }

                meshData.indices.resize(meshInfo.indexCount);
                result = copy_payload_bytes(fileData, payloadBegin,
                    meshInfo.indicesOffset, meshData.indices.data(),
                    meshData.indices.size());
                if (!result)
                {
                    return result;
                }

                if ((meshInfo.flags & k_cueModelMeshFlagHasSkinInfluences) !=
                    0)
                {
                    meshData.skinInfluences.resize(meshInfo.vertexCount);
                    result = copy_payload_bytes(fileData, payloadBegin,
                        meshInfo.skinInfluencesOffset,
                        meshData.skinInfluences.data(),
                        meshData.skinInfluences.size());
                    if (!result)
                    {
                        return result;
                    }
                }

                outModelData.meshes.push_back(std::move(meshData));
            }

            outModelData.materials.reserve(materialCount);
            for (uint32_t materialIndex = 0;
                 materialIndex < materialCount;
                 ++materialIndex)
            {
                CueModelMaterialInfo materialInfo{};
                std::memcpy(
                    &materialInfo,
                    fileData.data() + materialInfoBegin +
                        sizeof(CueModelMaterialInfo) *
                            static_cast<size_t>(materialIndex),
                    sizeof(CueModelMaterialInfo));

                Core::Native::ImportedMaterialData materialData{};
                materialData.name.resize(materialInfo.nameSize);
                result = copy_payload_bytes(fileData, payloadBegin,
                    materialInfo.nameOffset, materialData.name.data(),
                    materialData.name.size());
                if (!result)
                {
                    return result;
                }

                materialData.textureName.resize(materialInfo.textureNameSize);
                result = copy_payload_bytes(fileData, payloadBegin,
                    materialInfo.textureNameOffset,
                    materialData.textureName.data(),
                    materialData.textureName.size());
                if (!result)
                {
                    return result;
                }

                materialData.color = materialInfo.color;
                materialData.shininess = materialInfo.shininess;
                materialData.isTextureUsed =
                    (materialInfo.flags & k_cueModelMaterialFlagHasTexture) !=
                    0;
                materialData.usesReflectionSkybox =
                    (materialInfo.flags &
                        k_cueModelMaterialFlagUsesReflectionSkybox) != 0;
                outModelData.materials.push_back(std::move(materialData));
            }

            if (renderPartCount == 0)
            {
                outModelData.renderParts.reserve(outModelData.meshes.size());
                for (uint32_t meshIndex = 0;
                     meshIndex < outModelData.meshes.size();
                     ++meshIndex)
                {
                    Core::Native::ModelRenderPartData renderPart{};
                    renderPart.name = outModelData.meshes[meshIndex].name;
                    renderPart.meshIndex = meshIndex;
                    outModelData.renderParts.push_back(std::move(renderPart));
                }
            }
            else
            {
                outModelData.renderParts.reserve(renderPartCount);
                for (uint32_t renderPartIndex = 0;
                     renderPartIndex < renderPartCount;
                     ++renderPartIndex)
                {
                    CueModelRenderPartInfo renderPartInfo{};
                    const size_t renderPartInfoSize =
                        isVersion4
                        ? sizeof(CueModelRenderPartInfo)
                        : sizeof(CueModelRenderPartInfoV3);
                    if (isVersion4)
                    {
                        std::memcpy(
                            &renderPartInfo,
                            fileData.data() + renderPartInfoBegin +
                                renderPartInfoSize *
                                    static_cast<size_t>(renderPartIndex),
                            sizeof(CueModelRenderPartInfo));
                    }
                    else
                    {
                        CueModelRenderPartInfoV3 renderPartInfoV3{};
                        std::memcpy(
                            &renderPartInfoV3,
                            fileData.data() + renderPartInfoBegin +
                                renderPartInfoSize *
                                    static_cast<size_t>(renderPartIndex),
                            sizeof(CueModelRenderPartInfoV3));
                        renderPartInfo.nameSize = renderPartInfoV3.nameSize;
                        renderPartInfo.meshIndex = renderPartInfoV3.meshIndex;
                        renderPartInfo.materialIndex =
                            renderPartInfoV3.materialIndex;
                        renderPartInfo.jointIndex =
                            Core::Native::k_invalidAnimationIndex;
                        renderPartInfo.nameOffset = renderPartInfoV3.nameOffset;
                        renderPartInfo.localTransform =
                            renderPartInfoV3.localTransform;
                    }

                    if (renderPartInfo.meshIndex >= outModelData.meshes.size())
                    {
                        return Result::fail(
                            Code::InvalidArgument,
                            Severity::Error,
                            "Cooked model render part mesh index is out of range.");
                    }
                    if (renderPartInfo.materialIndex !=
                            Core::Native::k_invalidModelMaterialIndex &&
                        renderPartInfo.materialIndex >=
                            outModelData.materials.size())
                    {
                        return Result::fail(
                            Code::InvalidArgument,
                            Severity::Error,
                            "Cooked model render part material index is out of range.");
                    }

                    Core::Native::ModelRenderPartData renderPart{};
                    renderPart.name.resize(renderPartInfo.nameSize);
                    result = copy_payload_bytes(fileData, payloadBegin,
                        renderPartInfo.nameOffset, renderPart.name.data(),
                        renderPart.name.size());
                    if (!result)
                    {
                        return result;
                    }

                    renderPart.meshIndex = renderPartInfo.meshIndex;
                    renderPart.materialIndex = renderPartInfo.materialIndex;
                    renderPart.jointIndex = renderPartInfo.jointIndex;
                    renderPart.localTransform =
                        renderPartInfo.localTransform;
                    outModelData.renderParts.push_back(std::move(renderPart));
                }
            }

            outModelData.skeletonJoints.reserve(jointCount);
            for (uint32_t jointIndex = 0; jointIndex < jointCount; ++jointIndex)
            {
                CueModelSkeletonJointInfo jointInfo{};
                std::memcpy(
                    &jointInfo,
                    fileData.data() + jointInfoBegin +
                        sizeof(CueModelSkeletonJointInfo) *
                            static_cast<size_t>(jointIndex),
                    sizeof(CueModelSkeletonJointInfo));

                Core::Native::SkeletonJointData jointData{};
                jointData.name.resize(jointInfo.nameSize);
                result = copy_payload_bytes(fileData, payloadBegin,
                    jointInfo.nameOffset, jointData.name.data(),
                    jointData.name.size());
                if (!result)
                {
                    return result;
                }

                jointData.parentIndex = jointInfo.parentIndex;
                jointData.inverseBindMatrix = jointInfo.inverseBindMatrix;
                jointData.localBindMatrix = jointInfo.localBindMatrix;
                outModelData.skeletonJoints.push_back(std::move(jointData));
            }

            outModelData.animationClips.reserve(animationClipCount);
            for (uint32_t clipIndex = 0; clipIndex < animationClipCount;
                 ++clipIndex)
            {
                CueModelAnimationClipInfo clipInfo{};
                std::memcpy(
                    &clipInfo,
                    fileData.data() + animationClipInfoBegin +
                        sizeof(CueModelAnimationClipInfo) *
                            static_cast<size_t>(clipIndex),
                    sizeof(CueModelAnimationClipInfo));

                Core::Native::AnimationClipData clipData{};
                clipData.name.resize(clipInfo.nameSize);
                result = copy_payload_bytes(fileData, payloadBegin,
                    clipInfo.nameOffset, clipData.name.data(),
                    clipData.name.size());
                if (!result)
                {
                    return result;
                }
                clipData.duration = clipInfo.duration;
                clipData.ticksPerSecond = clipInfo.ticksPerSecond;

                std::vector<CueModelAnimationChannelInfo> channelInfos(
                    clipInfo.channelCount);
                result = copy_payload_bytes(fileData, payloadBegin,
                    clipInfo.channelsOffset, channelInfos.data(),
                    channelInfos.size());
                if (!result)
                {
                    return result;
                }

                clipData.channels.reserve(channelInfos.size());
                for (const CueModelAnimationChannelInfo& channelInfo :
                     channelInfos)
                {
                    Core::Native::AnimationChannelData channelData{};
                    channelData.targetName.resize(channelInfo.targetNameSize);
                    result = copy_payload_bytes(fileData, payloadBegin,
                        channelInfo.targetNameOffset,
                        channelData.targetName.data(),
                        channelData.targetName.size());
                    if (!result)
                    {
                        return result;
                    }
                    channelData.jointIndex = channelInfo.jointIndex;
                    channelData.translations.resize(
                        channelInfo.translationCount);
                    result = copy_payload_bytes(fileData, payloadBegin,
                        channelInfo.translationsOffset,
                        channelData.translations.data(),
                        channelData.translations.size());
                    if (!result)
                    {
                        return result;
                    }
                    channelData.rotations.resize(channelInfo.rotationCount);
                    result = copy_payload_bytes(fileData, payloadBegin,
                        channelInfo.rotationsOffset,
                        channelData.rotations.data(),
                        channelData.rotations.size());
                    if (!result)
                    {
                        return result;
                    }
                    channelData.scales.resize(channelInfo.scaleCount);
                    result = copy_payload_bytes(fileData, payloadBegin,
                        channelInfo.scalesOffset,
                        channelData.scales.data(),
                        channelData.scales.size());
                    if (!result)
                    {
                        return result;
                    }
                    clipData.channels.push_back(std::move(channelData));
                }

                outModelData.animationClips.push_back(std::move(clipData));
            }

            return Result::ok();
        }
    }

    Result AssetManager::create_material(std::string_view name,
        const MaterialDesc& desc, MaterialHandle& outHandle)
    {
        return add_material(name, desc, outHandle);
    }

    Result AssetManager::create_color_material(std::string_view name,
        const Math::float4& color, MaterialHandle& outHandle)
    {
        MaterialDesc desc{};
        desc.color = color;
        return add_material(name, desc, outHandle);
    }

    Result AssetManager::save_material(MaterialHandle handle,
        Core::IO::IFileSystem& fileSystem,
        const Core::IO::Path& filePath) const
    {
        MaterialAssetRecord record{};
        if (!m_materialRegistry.try_copy_get(handle, record))
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "Material not found for the given handle.");
        }

        const Core::IO::Path normalizedPath = filePath.normalize();
        if (normalizedPath.extension() != ".cuematerial")
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Material asset file extension must be .cuematerial.");
        }

        try
        {
            Json root = {
                { "version", k_materialAssetVersion },
                { "name", record.name },
                { "color", serialize_float4(record.desc.color) },
                { "texture", record.desc.textureName },
                { "useTexture", record.desc.isTextureUsed },
                { "reflectionSkybox", record.desc.usesReflectionSkybox },
                { "shininess", record.desc.shininess },
            };

            std::string text = root.dump(4);
            text.push_back('\n');

            const std::span<const char> charSpan(text.data(), text.size());
            const std::span<const std::byte> byteSpan = std::as_bytes(charSpan);
            return fileSystem.write_all(normalizedPath, byteSpan, true);
        }
        catch (...)
        {
            return Result::fail(
                Code::InternalError,
                Severity::Error,
                "Material asset could not be serialized.");
        }
    }

    Result AssetManager::load_material(Core::IO::IFileSystem& fileSystem,
        const Core::IO::Path& filePath, MaterialHandle& outHandle)
    {
        outHandle = {};

        const Core::IO::Path normalizedPath = filePath.normalize();
        if (normalizedPath.extension() != ".cuematerial")
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Material asset file extension must be .cuematerial.");
        }

        std::vector<std::byte> fileData{};
        Result result = fileSystem.read_all(normalizedPath, &fileData);
        if (!result)
        {
            return result;
        }

        try
        {
            const std::string text(
                reinterpret_cast<const char*>(fileData.data()),
                fileData.size());
            const Json root = Json::parse(text);

            const uint32_t version = root.at("version").get<uint32_t>();
            if (version != 1 && version != 2 && version != 3 &&
                version != k_materialAssetVersion)
            {
                return Result::fail(
                    Code::Unsupported,
                    Severity::Error,
                    "Material asset version is not supported.");
            }

            MaterialDesc desc{};
            deserialize_float4(root.at("color"), desc.color);
            if (version >= 2)
            {
                desc.textureName = root.value("texture", std::string{});
                Result textureResult =
                    get_texture_id(desc.textureName, desc.textureId);
                if (!textureResult)
                {
                    desc.textureId = k_errorTextureId;
                }
                desc.isTextureUsed = version >= 3
                    ? root.value("useTexture", !desc.textureName.empty())
                    : !desc.textureName.empty();
                if (version >= 4)
                {
                    desc.usesReflectionSkybox =
                        root.value("reflectionSkybox", false);
                    desc.shininess = root.value("shininess", 32.0f);
                }
            }
            else
            {
                desc.textureId = root.value("textureId", k_errorTextureId);
                desc.isTextureUsed = desc.textureId != k_errorTextureId;
            }

            const std::string materialName =
                root.value("name", normalizedPath.stem());
            Result existingResult = get_material(materialName, outHandle);
            if (existingResult)
            {
                MaterialAssetRecord* record =
                    m_materialRegistry.ref_get(outHandle);
                if (record == nullptr)
                {
                    return Result::fail(
                        Code::InternalError,
                        Severity::Error,
                        "Loaded material handle could not be resolved.");
                }

                // 同名マテリアルの再読込ではハンドルを維持しつつ内容だけ更新し、
                // Scene 側が保持している参照を壊さずに色変更を反映する
                record->name = materialName;
                record->desc = desc;
                return Result::ok();
            }
            return add_material(materialName, desc, outHandle);
        }
        catch (...)
        {
            return Result::fail(
                Code::GetFailed,
                Severity::Error,
                "Material asset could not be parsed.");
        }
    }

    Result AssetManager::create_effect(std::string_view name,
        const EffectSystem::EffectAsset& asset,
        EffectHandle& outHandle)
    {
        return add_effect(name, asset, outHandle);
    }

    Result AssetManager::load_effect(Core::IO::IFileSystem& fileSystem,
        const Core::IO::Path& filePath,
        EffectHandle& outHandle)
    {
        outHandle = {};

        const Core::IO::Path normalizedPath = filePath.normalize();
        if (normalizedPath.extension() != ".cuefx")
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Effect asset file extension must be .cuefx.");
        }

        std::vector<std::byte> fileData{};
        Result result = fileSystem.read_all(normalizedPath, &fileData);
        if (!result)
        {
            return result;
        }

        try
        {
            const std::string text(
                reinterpret_cast<const char*>(fileData.data()),
                fileData.size());
            const Json root = Json::parse(text);
            const uint32_t version = root.value("version", 1u);
            if (version > k_effectAssetVersion)
            {
                return Result::fail(
                    Code::Unsupported,
                    Severity::Error,
                    "Effect asset version is not supported.");
            }

            EffectSystem::EffectAsset asset{};
            asset.name = root.value("name", normalizedPath.stem());
            const Json* emittersJson = root.contains("emitters")
                ? &root.at("emitters")
                : nullptr;
            if (emittersJson != nullptr && emittersJson->is_array())
            {
                for (const Json& emitterJson : *emittersJson)
                {
                    EffectSystem::EffectEmitterDesc emitter{};
                    emitter.name = emitterJson.value("name", emitter.name);
                    emitter.materialName = emitterJson.value(
                        "materialName",
                        emitterJson.value("material", std::string{}));
                    emitter.meshName =
                        emitterJson.value("meshName", emitter.meshName);
                    if (emitterJson.contains("positionOffset"))
                    {
                        deserialize_float3_or_keep(
                            emitterJson.at("positionOffset"),
                            emitter.positionOffset);
                    }
                    if (emitterJson.contains("linearForce"))
                    {
                        deserialize_float3_or_keep(
                            emitterJson.at("linearForce"),
                            emitter.linearForce);
                    }
                    if (emitterJson.contains("attractorPosition"))
                    {
                        deserialize_float3_or_keep(
                            emitterJson.at("attractorPosition"),
                            emitter.attractorPosition);
                    }
                    if (emitterJson.contains("shapeBoxExtents"))
                    {
                        deserialize_float3_or_keep(
                            emitterJson.at("shapeBoxExtents"),
                            emitter.shapeBoxExtents);
                    }
                    if (emitterJson.contains("velocityMin"))
                    {
                        deserialize_float3_or_keep(
                            emitterJson.at("velocityMin"),
                            emitter.velocityMin);
                    }
                    if (emitterJson.contains("velocityMax"))
                    {
                        deserialize_float3_or_keep(
                            emitterJson.at("velocityMax"),
                            emitter.velocityMax);
                    }
                    if (emitterJson.contains("acceleration"))
                    {
                        deserialize_float3_or_keep(
                            emitterJson.at("acceleration"),
                            emitter.acceleration);
                    }
                    if (emitterJson.contains("startColor"))
                    {
                        deserialize_float4_or_keep(
                            emitterJson.at("startColor"),
                            emitter.startColor);
                    }
                    if (emitterJson.contains("midColor"))
                    {
                        deserialize_float4_or_keep(
                            emitterJson.at("midColor"),
                            emitter.midColor);
                    }
                    if (emitterJson.contains("endColor"))
                    {
                        deserialize_float4_or_keep(
                            emitterJson.at("endColor"),
                            emitter.endColor);
                    }
                    emitter.startSize =
                        emitterJson.value("startSize", emitter.startSize);
                    emitter.midSize =
                        emitterJson.value("midSize", emitter.midSize);
                    emitter.endSize =
                        emitterJson.value("endSize", emitter.endSize);
                    emitter.curveMidTime = emitterJson.value(
                        "curveMidTime",
                        emitter.curveMidTime);
                    emitter.startDelay =
                        emitterJson.value("startDelay", emitter.startDelay);
                    emitter.duration =
                        emitterJson.value("duration", emitter.duration);
                    emitter.shapeRadius =
                        emitterJson.value("shapeRadius", emitter.shapeRadius);
                    emitter.shapeAngleDegrees = emitterJson.value(
                        "shapeAngleDegrees",
                        emitter.shapeAngleDegrees);
                    emitter.trailWidth =
                        emitterJson.value("trailWidth", emitter.trailWidth);
                    emitter.trailLength =
                        emitterJson.value("trailLength", emitter.trailLength);
                    emitter.meshScale =
                        emitterJson.value("meshScale", emitter.meshScale);
                    emitter.drag = emitterJson.value("drag", emitter.drag);
                    emitter.noiseStrength = emitterJson.value(
                        "noiseStrength",
                        emitter.noiseStrength);
                    emitter.noiseFrequency = emitterJson.value(
                        "noiseFrequency",
                        emitter.noiseFrequency);
                    emitter.attractorStrength = emitterJson.value(
                        "attractorStrength",
                        emitter.attractorStrength);
                    emitter.vortexStrength = emitterJson.value(
                        "vortexStrength",
                        emitter.vortexStrength);
                    emitter.minLifetime =
                        emitterJson.value("minLifetime", emitter.minLifetime);
                    emitter.maxLifetime =
                        emitterJson.value("maxLifetime", emitter.maxLifetime);
                    emitter.emitRate =
                        emitterJson.value("emitRate", emitter.emitRate);
                    emitter.burstCount =
                        emitterJson.value("burstCount", emitter.burstCount);
                    emitter.trailSegmentCount = emitterJson.value(
                        "trailSegmentCount",
                        emitter.trailSegmentCount);
                    emitter.maxParticleCount = emitterJson.value(
                        "maxParticleCount",
                        emitter.maxParticleCount);
                    emitter.randomSeed =
                        emitterJson.value("randomSeed", emitter.randomSeed);
                    emitter.rendererType = parse_effect_renderer_type(
                        emitterJson.value(
                            "rendererType",
                            std::string("Billboard")));
                    emitter.shape = parse_effect_shape(
                        emitterJson.value("shape", std::string("Point")));
                    emitter.billboardMode = parse_effect_billboard_mode(
                        emitterJson.value("billboardMode", std::string("View")));
                    emitter.isLooping =
                        emitterJson.value("loop", emitter.isLooping);
                    emitter.isVisible =
                        emitterJson.value("visible", emitter.isVisible);
                    clamp_effect_emitter(emitter);
                    asset.emitters.push_back(std::move(emitter));
                }
            }
            if (asset.emitters.empty())
            {
                EffectSystem::EffectEmitterDesc emitter{};
                clamp_effect_emitter(emitter);
                asset.emitters.push_back(std::move(emitter));
            }

            const Json* graphNodesJson = root.contains("graphNodes")
                ? &root.at("graphNodes")
                : nullptr;
            if (graphNodesJson != nullptr && graphNodesJson->is_array())
            {
                for (const Json& nodeJson : *graphNodesJson)
                {
                    EffectSystem::EffectGraphNodeDesc node{};
                    node.name = nodeJson.value("name", node.name);
                    node.emitterIndex =
                        nodeJson.value("emitterIndex", node.emitterIndex);
                    if (nodeJson.contains("position"))
                    {
                        deserialize_float2_or_keep(
                            nodeJson.at("position"),
                            node.position);
                    }
                    if (node.emitterIndex < asset.emitters.size())
                    {
                        asset.graphNodes.push_back(std::move(node));
                    }
                }
            }

            if (asset.graphNodes.empty())
            {
                asset.graphNodes.reserve(asset.emitters.size());
                for (uint32_t emitterIndex = 0;
                     emitterIndex < static_cast<uint32_t>(asset.emitters.size());
                     ++emitterIndex)
                {
                    EffectSystem::EffectGraphNodeDesc node{};
                    node.name = asset.emitters[emitterIndex].name;
                    node.emitterIndex = emitterIndex;
                    node.position = Math::float2(
                        24.0f + static_cast<float>(emitterIndex) * 180.0f,
                        32.0f);
                    asset.graphNodes.push_back(std::move(node));
                }
            }

            Result existingResult = get_effect(asset.name, outHandle);
            if (existingResult)
            {
                EffectAssetRecord* record = m_effectRegistry.ref_get(outHandle);
                if (record == nullptr)
                {
                    return Result::fail(
                        Code::InternalError,
                        Severity::Error,
                        "Loaded effect handle could not be resolved.");
                }

                record->name = asset.name;
                record->asset = std::move(asset);
                return Result::ok();
            }

            return add_effect(asset.name, asset, outHandle);
        }
        catch (...)
        {
            return Result::fail(
                Code::GetFailed,
                Severity::Error,
                "Effect asset could not be parsed.");
        }
    }

    Result AssetManager::register_texture_from_cuetexture(
        Core::IO::IFileSystem& fileSystem,
        std::string_view name,
        const Core::IO::Path& filePath,
        uint32_t& outTextureId)
    {
        outTextureId = k_errorTextureId;

        const Core::ResourceNameId nameId = Core::fnv1a64(name);
        if (m_textureNameMap.contains(nameId))
        {
            outTextureId = m_textureNameMap.at(nameId);
            return Result::ok();
        }

        if (m_textureManager == nullptr)
        {
            return Result::fail(
                Code::InvalidState,
                Severity::Error,
                "Texture manager is not initialized in AssetManager.");
        }

        LoadedTextureData loadedTextureData{};
        Result result = load_cuetexture(fileSystem, filePath, loadedTextureData);
        if (!result)
        {
            return result;
        }

        RHI::TextureHandle textureHandle{};
        result = upload_texture(
            *m_textureManager, name, loadedTextureData, textureHandle);
        if (!result)
        {
            return result;
        }

        result = m_textureManager->get_texture_descriptor_index(
            textureHandle,
            outTextureId);
        if (!result)
        {
            return result;
        }

        if (outTextureId >= m_textures.size())
        {
            m_textures.resize(static_cast<size_t>(outTextureId) + 1);
        }

        m_textures[outTextureId] = TextureAssetRecord{
            std::string(name),
            textureHandle
        };
        m_textureNameMap.emplace(nameId, outTextureId);
        return Result::ok();
    }

    Result AssetManager::register_error_texture_from_cuetexture(
        Core::IO::IFileSystem& fileSystem,
        const Core::IO::Path& filePath)
    {
        if (!m_textures.empty())
        {
            return Result::fail(
                Code::InvalidState,
                Severity::Error,
                "Error texture must be registered before any other texture.");
        }

        uint32_t textureId = k_errorTextureId;
        Result result = register_texture_from_cuetexture(
            fileSystem, "CueDummy", filePath, textureId);
        if (!result)
        {
            return result;
        }

        if (textureId != k_errorTextureId)
        {
            return Result::fail(
                Code::InternalError,
                Severity::Error,
                "Error texture id must be zero.");
        }

        return Result::ok();
    }

    Result AssetManager::register_texture_from_file(
        Core::IO::IFileSystem& fileSystem,
        std::string_view name,
        const Core::IO::Path& filePath,
        uint32_t& outTextureId)
    {
        outTextureId = k_errorTextureId;

        const std::string extension =
            to_lower_ascii(filePath.normalize().extension());
        if (extension == ".cuetexture")
        {
            return register_texture_from_cuetexture(
                fileSystem,
                name,
                filePath,
                outTextureId);
        }

        if (extension != ".dds")
        {
            return Result::fail(
                Code::Unsupported,
                Severity::Error,
                "Texture asset file extension is not supported.");
        }

        const Core::ResourceNameId nameId = Core::fnv1a64(name);
        if (m_textureNameMap.contains(nameId))
        {
            outTextureId = m_textureNameMap.at(nameId);
            return Result::ok();
        }

        if (m_textureManager == nullptr)
        {
            return Result::fail(
                Code::InvalidState,
                Severity::Error,
                "Texture manager is not initialized in AssetManager.");
        }

        RHI::TextureHandle textureHandle{};
        Result result = m_textureManager->create_texture_from_file(
            name,
            filePath.normalize().utf8(),
            textureHandle);
        if (!result)
        {
            return result;
        }

        result = m_textureManager->get_texture_descriptor_index(
            textureHandle,
            outTextureId);
        if (!result)
        {
            return result;
        }

        if (outTextureId >= m_textures.size())
        {
            m_textures.resize(static_cast<size_t>(outTextureId) + 1);
        }

        m_textures[outTextureId] = TextureAssetRecord{
            std::string(name),
            textureHandle
        };
        m_textureNameMap.emplace(nameId, outTextureId);
        return Result::ok();
    }

    Result AssetManager::register_texture_from_rgba8(
        std::string_view name,
        uint32_t width,
        uint32_t height,
        std::span<const std::byte> pixels,
        uint32_t& outTextureId)
    {
        outTextureId = k_errorTextureId;
        if (name.empty() || width == 0 || height == 0)
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "RGBA texture name and size must be valid.");
        }

        const uint64_t expectedSize =
            static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 4u;
        if (pixels.size() != expectedSize)
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "RGBA texture pixel data size is invalid.");
        }

        if (m_textureManager == nullptr)
        {
            return Result::fail(
                Code::InvalidState,
                Severity::Error,
                "Texture manager is not initialized in AssetManager.");
        }

        RHI::TextureDesc textureDesc{};
        textureDesc.name = std::string(name);
        textureDesc.kind = RHI::TextureKind::Default;
        textureDesc.type = RHI::TextureType::Texture2D;
        textureDesc.width = width;
        textureDesc.height = height;
        textureDesc.format = RHI::ColorFormat::R8G8B8A8_UNORM;

        RHI::TextureSubresourceData subresource{};
        subresource.data = pixels.data();
        subresource.dataSize = pixels.size();
        subresource.rowPitch = width * 4u;
        subresource.slicePitch = subresource.rowPitch * height;

        RHI::TextureHandle textureHandle{};
        Result result = m_textureManager->create_texture(
            textureDesc,
            std::span<const RHI::TextureSubresourceData>(&subresource, 1),
            textureHandle);
        if (!result)
        {
            return result;
        }

        result =
            m_textureManager->get_texture_descriptor_index(textureHandle, outTextureId);
        if (!result)
        {
            (void)m_textureManager->destroy_texture(textureHandle);
            return result;
        }

        const Core::ResourceNameId nameId = Core::fnv1a64(name);
        auto existingIt = m_textureNameMap.find(nameId);
        if (existingIt != m_textureNameMap.end())
        {
            // フォントアトラスのように描画収集中に再生成されるテクスチャは、
            // 同一フレーム内で古い descriptor id を参照する draw item が残り得る
            // 即破棄せず、新しい名前解決だけを最新 descriptor id に更新する
            existingIt->second = outTextureId;
        }
        else
        {
            m_textureNameMap.emplace(nameId, outTextureId);
        }

        if (outTextureId >= m_textures.size())
        {
            m_textures.resize(static_cast<size_t>(outTextureId) + 1);
        }

        m_textures[outTextureId] = TextureAssetRecord{
            std::string(name),
            textureHandle
        };
        return Result::ok();
    }

    Result AssetManager::register_error_texture_from_file(
        Core::IO::IFileSystem& fileSystem,
        const Core::IO::Path& filePath)
    {
        if (!m_textures.empty())
        {
            return Result::fail(
                Code::InvalidState,
                Severity::Error,
                "Error texture must be registered before any other texture.");
        }

        uint32_t textureId = k_errorTextureId;
        Result result = register_texture_from_file(
            fileSystem, "CueDummy", filePath, textureId);
        if (!result)
        {
            return result;
        }

        if (textureId != k_errorTextureId)
        {
            return Result::fail(
                Code::InternalError,
                Severity::Error,
                "Error texture id must be zero.");
        }

        return Result::ok();
    }

    Result AssetManager::register_model_from_cuemodel(
        Core::IO::IFileSystem& fileSystem,
        std::string_view name,
        const Core::IO::Path& filePath,
        ModelHandle& outHandle)
    {
        Core::Native::ModelData modelData{};
        Result result = load_cuemodel(fileSystem, filePath, modelData);
        if (!result)
        {
            return result;
        }

        return add_model(name, modelData, outHandle);
    }

    Result AssetManager::get_model_name_from_mesh_id(
        uint32_t meshId,
        std::string& outName) const
    {
        if (m_staticMeshPool == nullptr)
        {
            return Result::fail(
                Code::InvalidState,
                Severity::Error,
                "Static mesh pool is not initialized in AssetManager.");
        }

        for (ModelHandle handle : m_modelHandles)
        {
            const ModelAssetRecord* record = m_modelRegistry.ref_get(handle);
            if (record == nullptr)
            {
                continue;
            }

            for (RHI::StaticMeshHandle staticMeshHandle : record->staticMeshHandles)
            {
                uint32_t registeredMeshId = 0;
                Result result = m_staticMeshPool->get_mesh_id(
                    staticMeshHandle, registeredMeshId);
                if (!result)
                {
                    continue;
                }

                if (registeredMeshId == meshId)
                {
                    outName = record->name;
                    return Result::ok();
                }
            }
        }

        return Result::fail(
            Code::NotFound,
            Severity::Error,
            "Model not found for the given mesh id.");
    }

    Result AssetManager::resolve_model_mesh_id(
        std::string_view name,
        uint32_t& outMeshId) const
    {
        if (name.empty())
        {
            outMeshId = std::numeric_limits<uint32_t>::max();
            return Result::ok();
        }

        if (m_staticMeshPool == nullptr)
        {
            return Result::fail(
                Code::InvalidState,
                Severity::Error,
                "Static mesh pool is not initialized in AssetManager.");
        }

        ModelHandle handle{};
        Result result = get_model(name, handle);
        if (!result)
        {
            return result;
        }

        RHI::StaticMeshHandle staticMeshHandle{};
        result = get_static_mesh_handle(handle, 0, staticMeshHandle);
        if (!result)
        {
            return result;
        }

        return m_staticMeshPool->get_mesh_id(staticMeshHandle, outMeshId);
    }

    void AssetManager::collect_model_names(std::vector<std::string>& outNames) const
    {
        outNames.clear();
        outNames.reserve(m_modelHandles.size());
        for (ModelHandle handle : m_modelHandles)
        {
            const ModelAssetRecord* record = m_modelRegistry.ref_get(handle);
            if (record == nullptr || record->name.empty())
            {
                continue;
            }

            outNames.push_back(record->name);
        }

        std::sort(outNames.begin(), outNames.end());
    }

    Result AssetManager::create_cube_model(ModelHandle& outHandle)
    {
        // 構造体の用意
        Core::Native::ModelData modelData{};
        Core::Native::MeshData meshData{};

        // 頂点データとインデックスデータの用意
        std::string name = "Cube";
        uint32_t verticesCount = 24;
        uint32_t indicesCount = 36;
        meshData.name = name;
        meshData.positions.resize(verticesCount);
        meshData.uvs.resize(verticesCount);
        meshData.normals.resize(verticesCount);
        meshData.indices.resize(indicesCount);

        auto set_vertex = [&meshData](uint32_t index, const Math::float4& position, const Math::float2& uv, const Math::float3& normal)
            {
                meshData.positions[index] = position;
                meshData.uvs[index] = uv;
                meshData.normals[index] = normal;
            };

        // 頂点データの設定（位置、UV、法線など）
        // 右面
        set_vertex(0, { 0.5f,  0.5f,  0.5f, 1.0f }, { 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }); // 右上
        set_vertex(1, { 0.5f,  0.5f, -0.5f, 1.0f }, { 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }); // 左上
        set_vertex(2, { 0.5f, -0.5f,  0.5f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 0.0f, 0.0f }); // 右下
        set_vertex(3, { 0.5f, -0.5f, -0.5f, 1.0f }, { 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f }); // 左下

        // 左面
        set_vertex(4, { -0.5f,  0.5f, -0.5f, 1.0f }, { 1.0f, 0.0f }, { -1.0f, 0.0f, 0.0f }); // 左上
        set_vertex(5, { -0.5f,  0.5f,  0.5f, 1.0f }, { 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f }); // 右上
        set_vertex(6, { -0.5f, -0.5f, -0.5f, 1.0f }, { 1.0f, 1.0f }, { -1.0f, 0.0f, 0.0f }); // 左下
        set_vertex(7, { -0.5f, -0.5f,  0.5f, 1.0f }, { 0.0f, 1.0f }, { -1.0f, 0.0f, 0.0f }); // 右下

        // 前面
        set_vertex(8, { -0.5f,  0.5f,  0.5f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }); // 左上
        set_vertex(9, { 0.5f,  0.5f,  0.5f, 1.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }); // 右上
        set_vertex(10, { -0.5f, -0.5f,  0.5f, 1.0f }, { 1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f }); // 左下
        set_vertex(11, { 0.5f, -0.5f,  0.5f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f }); // 右下

        // 後面
        set_vertex(12, { 0.5f,  0.5f, -0.5f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }); // 右上
        set_vertex(13, { -0.5f,  0.5f, -0.5f, 1.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }); // 左上
        set_vertex(14, { 0.5f, -0.5f, -0.5f, 1.0f }, { 1.0f, 1.0f }, { 0.0f, 0.0f, -1.0f }); // 右下
        set_vertex(15, { -0.5f, -0.5f, -0.5f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f }); // 左下

        // 上面
        set_vertex(16, { -0.5f,  0.5f, -0.5f, 1.0f }, { 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }); // 左奥
        set_vertex(17, { 0.5f,  0.5f, -0.5f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }); // 右奥
        set_vertex(18, { -0.5f,  0.5f,  0.5f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f }); // 左前
        set_vertex(19, { 0.5f,  0.5f,  0.5f, 1.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f, 0.0f }); // 右前

        // 下面
        set_vertex(20, { -0.5f, -0.5f,  0.5f, 1.0f }, { 0.0f, 0.0f }, { 0.0f, -1.0f, 0.0f }); // 左前
        set_vertex(21, { 0.5f, -0.5f,  0.5f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, -1.0f, 0.0f }); // 右前
        set_vertex(22, { -0.5f, -0.5f, -0.5f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, -1.0f, 0.0f }); // 左奥
        set_vertex(23, { 0.5f, -0.5f, -0.5f, 1.0f }, { 1.0f, 1.0f }, { 0.0f, -1.0f, 0.0f }); // 右奥

        // インデックスデータの設定（トライアングルリスト）
        // 右面インデックス
        meshData.indices[0] = 0; meshData.indices[1] = 2; meshData.indices[2] = 1;
        meshData.indices[3] = 2; meshData.indices[4] = 3; meshData.indices[5] = 1;

        // 左面インデックス
        meshData.indices[6] = 4; meshData.indices[7] = 6; meshData.indices[8] = 5;
        meshData.indices[9] = 6; meshData.indices[10] = 7; meshData.indices[11] = 5;

        // 前面インデックス
        meshData.indices[12] = 8; meshData.indices[13] = 10; meshData.indices[14] = 9;
        meshData.indices[15] = 10; meshData.indices[16] = 11; meshData.indices[17] = 9;

        // 後面インデックス
        meshData.indices[18] = 12; meshData.indices[19] = 14; meshData.indices[20] = 13;
        meshData.indices[21] = 14; meshData.indices[22] = 15; meshData.indices[23] = 13;

        // 上面インデックス
        meshData.indices[24] = 16; meshData.indices[25] = 18; meshData.indices[26] = 17;
        meshData.indices[27] = 18; meshData.indices[28] = 19; meshData.indices[29] = 17;

        // 下面インデックス
        meshData.indices[30] = 20; meshData.indices[31] = 22; meshData.indices[32] = 21;
        meshData.indices[33] = 22; meshData.indices[34] = 23; meshData.indices[35] = 21;

        // メッシュデータをモデルデータに追加
        modelData.meshes.push_back(std::move(meshData));

        return add_model("Cube", modelData, outHandle);
    }
}

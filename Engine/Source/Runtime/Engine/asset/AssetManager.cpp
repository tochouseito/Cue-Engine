#include "AssetManager.h"

// === C++ includes ===
#include <algorithm>
#include <charconv>
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

        struct ObjIndexKey final
        {
            int32_t positionIndex = 0;
            int32_t texcoordIndex = 0;
            int32_t normalIndex = 0;

            [[nodiscard]] bool operator==(const ObjIndexKey& other) const noexcept
            {
                return positionIndex == other.positionIndex &&
                    texcoordIndex == other.texcoordIndex &&
                    normalIndex == other.normalIndex;
            }
        };

        struct ObjIndexKeyHash final
        {
            [[nodiscard]] size_t operator()(const ObjIndexKey& key) const noexcept
            {
                size_t hash = static_cast<size_t>(key.positionIndex);
                hash ^= static_cast<size_t>(key.texcoordIndex) + 0x9e3779b9u + (hash << 6) + (hash >> 2);
                hash ^= static_cast<size_t>(key.normalIndex) + 0x9e3779b9u + (hash << 6) + (hash >> 2);
                return hash;
            }
        };

        static_assert(std::is_trivially_copyable_v<CueTextureHeader>);
        static_assert(std::is_trivially_copyable_v<CueTextureMipInfo>);
        static_assert(std::is_trivially_copyable_v<CueModelHeader>);
        static_assert(std::is_trivially_copyable_v<CueModelMeshInfo>);

        [[nodiscard]] std::string_view trim_ascii(std::string_view text) noexcept
        {
            while (!text.empty() &&
                (text.front() == ' ' || text.front() == '\t' ||
                    text.front() == '\r' || text.front() == '\n'))
            {
                text.remove_prefix(1);
            }

            while (!text.empty() &&
                (text.back() == ' ' || text.back() == '\t' ||
                    text.back() == '\r' || text.back() == '\n'))
            {
                text.remove_suffix(1);
            }

            return text;
        }

        [[nodiscard]] bool take_token(
            std::string_view& text,
            std::string_view& outToken) noexcept
        {
            text = trim_ascii(text);
            if (text.empty())
            {
                outToken = {};
                return false;
            }

            size_t tokenEnd = 0;
            while (tokenEnd < text.size() &&
                text[tokenEnd] != ' ' &&
                text[tokenEnd] != '\t' &&
                text[tokenEnd] != '\r' &&
                text[tokenEnd] != '\n')
            {
                ++tokenEnd;
            }

            outToken = text.substr(0, tokenEnd);
            text.remove_prefix(tokenEnd);
            return true;
        }

        template <typename T>
        [[nodiscard]] bool parse_number(
            std::string_view token,
            T& outValue) noexcept
        {
            const char* begin = token.data();
            const char* end = token.data() + token.size();
            const std::from_chars_result result =
                std::from_chars(begin, end, outValue);
            return result.ec == std::errc{} && result.ptr == end;
        }

        [[nodiscard]] bool parse_face_index_token(
            std::string_view token,
            ObjIndexKey& outKey) noexcept
        {
            outKey = {};

            size_t firstSlash = token.find('/');
            if (firstSlash == std::string_view::npos)
            {
                return parse_number(token, outKey.positionIndex);
            }

            if (!parse_number(token.substr(0, firstSlash), outKey.positionIndex))
            {
                return false;
            }

            size_t secondSlash = token.find('/', firstSlash + 1);
            if (secondSlash == std::string_view::npos)
            {
                return parse_number(token.substr(firstSlash + 1), outKey.texcoordIndex);
            }

            const std::string_view texcoordToken =
                token.substr(firstSlash + 1, secondSlash - firstSlash - 1);
            if (!texcoordToken.empty() &&
                !parse_number(texcoordToken, outKey.texcoordIndex))
            {
                return false;
            }

            const std::string_view normalToken = token.substr(secondSlash + 1);
            if (!normalToken.empty() &&
                !parse_number(normalToken, outKey.normalIndex))
            {
                return false;
            }

            return true;
        }

        template <typename T>
        [[nodiscard]] const T* get_obj_element(
            const std::vector<T>& values,
            int32_t index) noexcept
        {
            if (index <= 0)
            {
                return nullptr;
            }

            const size_t resolvedIndex = static_cast<size_t>(index - 1);
            if (resolvedIndex >= values.size())
            {
                return nullptr;
            }

            return &values[resolvedIndex];
        }

        [[nodiscard]] Result load_obj_model_data(
            Core::IO::IFileSystem& fileSystem,
            const Core::IO::Path& filePath,
            std::string_view modelName,
            Core::Native::ModelData& outModelData)
        {
            std::vector<std::byte> fileData{};
            Result result = fileSystem.read_all(filePath, &fileData);
            if (!result)
            {
                return result;
            }

            const std::string text(
                reinterpret_cast<const char*>(fileData.data()),
                fileData.size());

            std::vector<Math::float4> sourcePositions{};
            std::vector<Math::float2> sourceUvs{};
            std::vector<Math::float3> sourceNormals{};
            std::unordered_map<ObjIndexKey, uint32_t, ObjIndexKeyHash> vertexMap{};

            Core::Native::ModelData modelData{};
            Core::Native::MeshData meshData{};
            meshData.name = std::string(modelName);

            size_t lineBegin = 0;
            while (lineBegin < text.size())
            {
                size_t lineEnd = text.find('\n', lineBegin);
                if (lineEnd == std::string::npos)
                {
                    lineEnd = text.size();
                }

                std::string_view line = trim_ascii(
                    std::string_view(text).substr(lineBegin, lineEnd - lineBegin));
                lineBegin = lineEnd + 1;

                if (line.empty() || line.front() == '#')
                {
                    continue;
                }

                std::string_view keyword{};
                if (!take_token(line, keyword))
                {
                    continue;
                }

                if (keyword == "v")
                {
                    std::string_view xToken{};
                    std::string_view yToken{};
                    std::string_view zToken{};
                    float x = 0.0f;
                    float y = 0.0f;
                    float z = 0.0f;
                    if (!take_token(line, xToken) ||
                        !take_token(line, yToken) ||
                        !take_token(line, zToken) ||
                        !parse_number(xToken, x) ||
                        !parse_number(yToken, y) ||
                        !parse_number(zToken, z))
                    {
                        return Result::fail(
                            Code::InvalidArgument,
                            Severity::Error,
                            "OBJ vertex position could not be parsed.");
                    }

                    sourcePositions.emplace_back(x, y, z, 1.0f);
                    continue;
                }

                if (keyword == "vt")
                {
                    std::string_view uToken{};
                    std::string_view vToken{};
                    float u = 0.0f;
                    float v = 0.0f;
                    if (!take_token(line, uToken) ||
                        !take_token(line, vToken) ||
                        !parse_number(uToken, u) ||
                        !parse_number(vToken, v))
                    {
                        return Result::fail(
                            Code::InvalidArgument,
                            Severity::Error,
                            "OBJ uv could not be parsed.");
                    }

                    sourceUvs.emplace_back(u, v);
                    continue;
                }

                if (keyword == "vn")
                {
                    std::string_view xToken{};
                    std::string_view yToken{};
                    std::string_view zToken{};
                    float x = 0.0f;
                    float y = 0.0f;
                    float z = 0.0f;
                    if (!take_token(line, xToken) ||
                        !take_token(line, yToken) ||
                        !take_token(line, zToken) ||
                        !parse_number(xToken, x) ||
                        !parse_number(yToken, y) ||
                        !parse_number(zToken, z))
                    {
                        return Result::fail(
                            Code::InvalidArgument,
                            Severity::Error,
                            "OBJ normal could not be parsed.");
                    }

                    sourceNormals.emplace_back(x, y, z);
                    continue;
                }

                if (keyword != "f")
                {
                    continue;
                }

                std::vector<uint32_t> faceVertexIndices{};
                std::string_view faceToken{};
                while (take_token(line, faceToken))
                {
                    ObjIndexKey key{};
                    if (!parse_face_index_token(faceToken, key))
                    {
                        return Result::fail(
                            Code::InvalidArgument,
                            Severity::Error,
                            "OBJ face index could not be parsed.");
                    }

                    if (key.positionIndex <= 0)
                    {
                        return Result::fail(
                            Code::InvalidArgument,
                            Severity::Error,
                            "OBJ face position index is invalid.");
                    }

                    const auto it = vertexMap.find(key);
                    if (it != vertexMap.end())
                    {
                        faceVertexIndices.push_back(it->second);
                        continue;
                    }

                    const Math::float4* position =
                        get_obj_element(sourcePositions, key.positionIndex);
                    if (position == nullptr)
                    {
                        return Result::fail(
                            Code::InvalidArgument,
                            Severity::Error,
                            "OBJ face references an unknown position.");
                    }

                    const Math::float2* uv = get_obj_element(sourceUvs, key.texcoordIndex);
                    const Math::float3* normal = get_obj_element(sourceNormals, key.normalIndex);

                    const uint32_t vertexIndex = static_cast<uint32_t>(
                        meshData.positions.size());
                    meshData.positions.push_back(*position);
                    meshData.uvs.push_back(uv != nullptr ? *uv : Math::float2::zero());
                    meshData.normals.push_back(
                        normal != nullptr ? *normal : Math::float3::zero());
                    vertexMap.emplace(key, vertexIndex);
                    faceVertexIndices.push_back(vertexIndex);
                }

                if (faceVertexIndices.size() < 3)
                {
                    return Result::fail(
                        Code::InvalidArgument,
                        Severity::Error,
                        "OBJ face requires at least three vertices.");
                }

                for (size_t vertexIndex = 1;
                    vertexIndex + 1 < faceVertexIndices.size();
                    ++vertexIndex)
                {
                    meshData.indices.push_back(faceVertexIndices[0]);
                    meshData.indices.push_back(faceVertexIndices[vertexIndex]);
                    meshData.indices.push_back(faceVertexIndices[vertexIndex + 1]);
                }
            }

            if (meshData.positions.empty() || meshData.indices.empty())
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "OBJ does not contain any renderable mesh data.");
            }

            modelData.meshes.push_back(std::move(meshData));
            outModelData = std::move(modelData);
            return Result::ok();
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

            if (fileData.size() < sizeof(CueModelHeader) + sizeof(CueModelMeshInfo))
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Cooked model file is too small.");
            }

            CueModelHeader header{};
            std::memcpy(&header, fileData.data(), sizeof(CueModelHeader));
            if (header.magic != k_cueModelMagic ||
                header.version != k_cueModelVersion ||
                header.meshCount == 0)
            {
                return Result::fail(
                    Code::Unsupported,
                    Severity::Error,
                    "Cooked model format is not supported.");
            }

            const size_t meshInfoTableSize =
                sizeof(CueModelMeshInfo) * static_cast<size_t>(header.meshCount);
            const size_t payloadBegin = sizeof(CueModelHeader) + meshInfoTableSize;
            if (fileData.size() < payloadBegin + static_cast<size_t>(header.dataSize))
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Cooked model payload is invalid.");
            }

            outModelData = {};
            outModelData.meshes.reserve(header.meshCount);
            for (uint32_t meshIndex = 0; meshIndex < header.meshCount; ++meshIndex)
            {
                CueModelMeshInfo meshInfo{};
                std::memcpy(
                    &meshInfo,
                    fileData.data() + sizeof(CueModelHeader) +
                        sizeof(CueModelMeshInfo) * static_cast<size_t>(meshIndex),
                    sizeof(CueModelMeshInfo));

                if (meshInfo.vertexCount == 0 || meshInfo.indexCount == 0)
                {
                    return Result::fail(
                        Code::InvalidArgument,
                        Severity::Error,
                        "Cooked model mesh is empty.");
                }

                Core::Native::MeshData meshData{};
                meshData.name.resize(meshInfo.nameSize);
                result = copy_payload_bytes(
                    fileData,
                    payloadBegin,
                    meshInfo.nameOffset,
                    meshData.name.data(),
                    meshData.name.size());
                if (!result)
                {
                    return result;
                }

                meshData.positions.resize(meshInfo.vertexCount);
                result = copy_payload_bytes(
                    fileData,
                    payloadBegin,
                    meshInfo.positionsOffset,
                    meshData.positions.data(),
                    meshData.positions.size());
                if (!result)
                {
                    return result;
                }

                if ((meshInfo.flags & k_cueModelMeshFlagHasUvs) != 0)
                {
                    meshData.uvs.resize(meshInfo.vertexCount);
                    result = copy_payload_bytes(
                        fileData,
                        payloadBegin,
                        meshInfo.uvsOffset,
                        meshData.uvs.data(),
                        meshData.uvs.size());
                    if (!result)
                    {
                        return result;
                    }
                }

                if ((meshInfo.flags & k_cueModelMeshFlagHasNormals) != 0)
                {
                    meshData.normals.resize(meshInfo.vertexCount);
                    result = copy_payload_bytes(
                        fileData,
                        payloadBegin,
                        meshInfo.normalsOffset,
                        meshData.normals.data(),
                        meshData.normals.size());
                    if (!result)
                    {
                        return result;
                    }
                }

                meshData.indices.resize(meshInfo.indexCount);
                result = copy_payload_bytes(
                    fileData,
                    payloadBegin,
                    meshInfo.indicesOffset,
                    meshData.indices.data(),
                    meshData.indices.size());
                if (!result)
                {
                    return result;
                }

                outModelData.meshes.push_back(std::move(meshData));
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
                // Scene 側が保持している参照を壊さずに色変更を反映する。
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

    Result AssetManager::load_model_from_obj(
        Core::IO::IFileSystem& fileSystem,
        std::string_view name,
        const Core::IO::Path& filePath,
        ModelHandle& outHandle)
    {
        Core::Native::ModelData modelData{};
        Result result = load_obj_model_data(fileSystem, filePath, name, modelData);
        if (!result)
        {
            return result;
        }

        return add_model(name, modelData, outHandle);
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

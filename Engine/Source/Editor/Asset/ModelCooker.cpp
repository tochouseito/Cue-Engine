#include "ModelCooker.h"

// === Engine Includes ===
#include <asset/ModelAssetFormat.h>
#include <ModelImporter.h>
#include <TextureCooker.h>

// === Core Includes ===
#include <Native/EngineNativeStruct.h>

// === C++ Includes ===
#include <algorithm>
#include <cctype>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

// === ThirdParty Includes ===
#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/scene.h>
#include <nlohmann/json.hpp>

namespace Cue::Editor
{
    namespace
    {
        using Json = nlohmann::json;

        static_assert(std::is_trivially_copyable_v<CueModelHeader>);
        static_assert(std::is_trivially_copyable_v<CueModelHeaderV3>);
        static_assert(std::is_trivially_copyable_v<CueModelLegacyHeader>);
        static_assert(std::is_trivially_copyable_v<CueModelMeshInfoV2>);
        static_assert(std::is_trivially_copyable_v<CueModelMeshInfo>);
        static_assert(std::is_trivially_copyable_v<CueModelMaterialInfo>);
        static_assert(std::is_trivially_copyable_v<CueModelRenderPartInfo>);
        static_assert(std::is_trivially_copyable_v<CueModelSkeletonJointInfo>);
        static_assert(std::is_trivially_copyable_v<CueModelAnimationClipInfo>);
        static_assert(
            std::is_trivially_copyable_v<CueModelAnimationChannelInfo>);
        static_assert(std::is_trivially_copyable_v<CueModelVectorKeyframe>);
        static_assert(
            std::is_trivially_copyable_v<CueModelQuaternionKeyframe>);

        template <typename T>
        void append_bytes(
            std::vector<std::byte>& a_buffer,
            const T* a_data,
            size_t a_count)
        {
            if (a_data == nullptr || a_count == 0)
            {
                return;
            }

            const size_t byteCount = sizeof(T) * a_count;
            const size_t writeOffset = a_buffer.size();
            a_buffer.resize(writeOffset + byteCount);
            std::memcpy(a_buffer.data() + writeOffset, a_data, byteCount);
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

        [[nodiscard]] bool is_supported_texture_source(
            const Core::IO::Path& a_path) noexcept
        {
            const std::string extension =
                to_lower_ascii(a_path.extension());
            return extension == ".png" || extension == ".dds" ||
                extension == ".jpg" || extension == ".jpeg" ||
                extension == ".tga" || extension == ".bmp";
        }

        [[nodiscard]] bool is_gltf_source(
            const Core::IO::Path& a_path) noexcept
        {
            return to_lower_ascii(a_path.extension()) == ".gltf";
        }

        [[nodiscard]] bool is_embedded_texture_path(
            std::string_view a_path) noexcept
        {
            return !a_path.empty() && a_path.front() == '*';
        }

        [[nodiscard]] bool is_data_uri(std::string_view a_uri) noexcept
        {
            constexpr std::string_view k_dataPrefix = "data:";
            return a_uri.size() >= k_dataPrefix.size() &&
                a_uri.substr(0, k_dataPrefix.size()) == k_dataPrefix;
        }

        [[nodiscard]] std::string trim_ascii(std::string_view a_text)
        {
            size_t begin = 0;
            while (begin < a_text.size() &&
                std::isspace(static_cast<unsigned char>(a_text[begin])) != 0)
            {
                ++begin;
            }

            size_t end = a_text.size();
            while (end > begin &&
                std::isspace(static_cast<unsigned char>(a_text[end - 1])) != 0)
            {
                --end;
            }

            return std::string(a_text.substr(begin, end - begin));
        }

        [[nodiscard]] bool extract_mtl_diffuse_texture_path(
            std::string_view a_line,
            std::string& outTexturePath)
        {
            outTexturePath.clear();

            const size_t commentBegin = a_line.find('#');
            if (commentBegin != std::string_view::npos)
            {
                a_line = a_line.substr(0, commentBegin);
            }

            const std::string line = trim_ascii(a_line);
            constexpr std::string_view k_mapKd = "map_Kd";
            if (line.size() <= k_mapKd.size() ||
                line.compare(0, k_mapKd.size(), k_mapKd) != 0 ||
                std::isspace(
                    static_cast<unsigned char>(line[k_mapKd.size()])) == 0)
            {
                return false;
            }

            const std::string rest = trim_ascii(
                std::string_view(line).substr(k_mapKd.size()));
            if (rest.empty())
            {
                return false;
            }

            const size_t pathBegin = rest.find_last_of(" \t\r\n");
            outTexturePath = pathBegin == std::string::npos
                ? rest
                : trim_ascii(std::string_view(rest).substr(pathBegin + 1));
            return !outTexturePath.empty();
        }

        void push_unique_path(
            std::vector<Core::IO::Path>& a_paths,
            const Core::IO::Path& a_path)
        {
            const std::string normalized = a_path.normalize().utf8();
            const auto it = std::find_if(
                a_paths.begin(),
                a_paths.end(),
                [&normalized](const Core::IO::Path& a_existing)
                {
                    return a_existing.normalize().utf8() == normalized;
                });
            if (it == a_paths.end())
            {
                a_paths.push_back(a_path.normalize());
            }
        }

        [[nodiscard]] Result try_resolve_existing_path(
            Core::IO::IFileSystem& a_fileSystem,
            std::span<const Core::IO::Path> a_candidates,
            Core::IO::Path& outPath)
        {
            outPath = {};
            for (const Core::IO::Path& candidate : a_candidates)
            {
                if (candidate.is_empty())
                {
                    continue;
                }

                bool exists = false;
                Result result = a_fileSystem.exists(
                    candidate.normalize(),
                    &exists);
                if (!result)
                {
                    return result;
                }
                if (exists)
                {
                    outPath = candidate.normalize();
                    return Result::ok();
                }
            }

            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "Model material texture file was not found.");
        }

        void collect_assimp_material_texture_paths(
            const aiMaterial& a_material,
            std::vector<Core::IO::Path>& outTexturePaths)
        {
            constexpr aiTextureType k_textureTypes[] = {
                aiTextureType_DIFFUSE,
                aiTextureType_BASE_COLOR,
            };

            for (const aiTextureType textureType : k_textureTypes)
            {
                const uint32_t textureCount =
                    a_material.GetTextureCount(textureType);
                for (uint32_t textureIndex = 0;
                     textureIndex < textureCount;
                     ++textureIndex)
                {
                    aiString texturePath{};
                    if (a_material.GetTexture(
                            textureType,
                            textureIndex,
                            &texturePath) != AI_SUCCESS ||
                        texturePath.length == 0 ||
                        texturePath.C_Str() == nullptr)
                    {
                        continue;
                    }

                    const std::string pathText(
                        texturePath.C_Str(),
                        texturePath.length);
                    if (is_embedded_texture_path(pathText))
                    {
                        continue;
                    }

                    push_unique_path(
                        outTexturePaths,
                        Core::IO::Path(pathText));
                }
            }
        }

        [[nodiscard]] Result collect_assimp_material_texture_dependencies(
            const Core::IO::Path& a_sourcePath,
            std::vector<Core::IO::Path>& outTexturePaths)
        {
            outTexturePaths.clear();

            Assimp::Importer importer{};
            const aiScene* scene = importer.ReadFile(a_sourcePath.utf8(), 0);
            if (scene == nullptr ||
                scene->mNumMaterials == 0 ||
                scene->mMaterials == nullptr)
            {
                return Result::ok();
            }

            for (uint32_t materialIndex = 0;
                 materialIndex < scene->mNumMaterials;
                 ++materialIndex)
            {
                if (scene->mMaterials[materialIndex] == nullptr)
                {
                    continue;
                }

                collect_assimp_material_texture_paths(
                    *scene->mMaterials[materialIndex],
                    outTexturePaths);
            }

            return Result::ok();
        }

        [[nodiscard]] Result resolve_model_texture_path(
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_sourcePath,
            const Core::IO::Path& a_destinationPath,
            const Core::IO::Path& a_texturePath,
            Core::IO::Path& outResolvedTexturePath)
        {
            outResolvedTexturePath = {};

            const Core::IO::Path modelRoot = a_sourcePath.parent();
            const Core::IO::Path assetRoot = modelRoot.parent();
            const Core::IO::Path textureRoot = Core::IO::Path::join(
                assetRoot,
                Core::IO::Path("Textures"));

            std::vector<Core::IO::Path> candidates{};
            if (a_texturePath.is_absolute())
            {
                push_unique_path(candidates, a_texturePath);
            }
            else
            {
                push_unique_path(
                    candidates,
                    Core::IO::Path::join(modelRoot, a_texturePath));
                push_unique_path(
                    candidates,
                    Core::IO::Path::join(assetRoot, a_texturePath));
                push_unique_path(
                    candidates,
                    Core::IO::Path::join(
                        textureRoot,
                        Core::IO::Path(a_texturePath.filename())));
                push_unique_path(
                    candidates,
                    Core::IO::Path::join(
                        modelRoot,
                        Core::IO::Path(a_texturePath.filename())));
                push_unique_path(
                    candidates,
                    Core::IO::Path::join(
                        a_destinationPath.parent(),
                        a_texturePath));
            }

            return try_resolve_existing_path(
                a_fileSystem,
                std::span<const Core::IO::Path>(
                    candidates.data(),
                    candidates.size()),
                outResolvedTexturePath);
        }

        [[nodiscard]] Result load_gltf_json(
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_sourcePath,
            Json& outJson)
        {
            outJson = Json::object();
            if (!is_gltf_source(a_sourcePath))
            {
                return Result::ok();
            }

            std::vector<std::byte> fileData{};
            Result result = a_fileSystem.read_all(a_sourcePath, &fileData);
            if (!result)
            {
                return result;
            }

            std::string text{};
            if (!fileData.empty())
            {
                text.assign(
                    reinterpret_cast<const char*>(fileData.data()),
                    fileData.size());
            }

            outJson = Json::parse(text, nullptr, false);
            if (outJson.is_discarded())
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "glTF json parse failed.");
            }

            return Result::ok();
        }

        void collect_gltf_uri_paths(const Json& a_json,
            std::string_view a_collectionName,
            const Core::IO::Path& a_modelRoot,
            std::vector<Core::IO::Path>& outPaths)
        {
            const std::string collectionName(a_collectionName);
            if (!a_json.contains(collectionName) ||
                !a_json.at(collectionName).is_array())
            {
                return;
            }

            for (const Json& entry : a_json.at(collectionName))
            {
                if (!entry.is_object() ||
                    !entry.contains("uri") ||
                    !entry.at("uri").is_string())
                {
                    continue;
                }

                const std::string uri = entry.at("uri").get<std::string>();
                if (uri.empty() || is_data_uri(uri))
                {
                    continue;
                }

                const Core::IO::Path uriPath(uri);
                push_unique_path(
                    outPaths,
                    uriPath.is_absolute()
                        ? uriPath
                        : Core::IO::Path::join(a_modelRoot, uriPath));
            }
        }

        [[nodiscard]] Result collect_gltf_dependencies(
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_sourcePath,
            std::vector<Core::IO::Path>& outBufferPaths,
            std::vector<Core::IO::Path>& outTexturePaths)
        {
            outBufferPaths.clear();
            outTexturePaths.clear();
            if (!is_gltf_source(a_sourcePath))
            {
                return Result::ok();
            }

            Json gltfJson{};
            Result result = load_gltf_json(
                a_fileSystem,
                a_sourcePath,
                gltfJson);
            if (!result)
            {
                return result;
            }

            const Core::IO::Path modelRoot = a_sourcePath.parent();
            collect_gltf_uri_paths(
                gltfJson,
                "buffers",
                modelRoot,
                outBufferPaths);
            collect_gltf_uri_paths(
                gltfJson,
                "images",
                modelRoot,
                outTexturePaths);
            return Result::ok();
        }

        [[nodiscard]] Result is_any_dependency_newer(
            Core::IO::IFileSystem& a_fileSystem,
            std::span<const Core::IO::Path> a_paths,
            int64_t a_compareMtimeNs,
            bool& outIsNewer)
        {
            outIsNewer = false;
            for (const Core::IO::Path& path : a_paths)
            {
                bool exists = false;
                Result result = a_fileSystem.exists(path, &exists);
                if (!result)
                {
                    return result;
                }
                if (!exists)
                {
                    return Result::fail(
                        Code::NotFound,
                        Severity::Error,
                        "glTF dependency file was not found.");
                }

                Core::IO::FileStat stat{};
                result = a_fileSystem.stat(path, &stat);
                if (!result)
                {
                    return result;
                }

                if (stat.mtime_ns > a_compareMtimeNs)
                {
                    outIsNewer = true;
                    return Result::ok();
                }
            }

            return Result::ok();
        }

        [[nodiscard]] Result resolve_material_texture(
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_sourcePath,
            const Core::IO::Path& a_destinationPath,
            Core::Native::ImportedMaterialData& a_materialData)
        {
            if (a_materialData.sourceTexturePath.empty())
            {
                return Result::ok();
            }

            const Core::IO::Path texturePath(
                a_materialData.sourceTexturePath);
            if (!is_supported_texture_source(texturePath))
            {
                return Result::fail(
                    Code::Unsupported,
                    Severity::Error,
                    "Model material texture extension is not supported.");
            }

            const Core::IO::Path assetRoot = a_sourcePath.parent().parent();
            const Core::IO::Path textureRoot = Core::IO::Path::join(
                assetRoot,
                Core::IO::Path("Textures"));

            Core::IO::Path resolvedTexturePath{};
            Result result = resolve_model_texture_path(
                a_fileSystem,
                a_sourcePath,
                a_destinationPath,
                texturePath,
                resolvedTexturePath);
            if (!result)
            {
                return result;
            }

            result = a_fileSystem.create_directories(textureRoot);
            if (!result)
            {
                return result;
            }

            const Core::IO::Path cookedTexturePath = Core::IO::Path::join(
                textureRoot,
                Core::IO::Path(resolvedTexturePath.stem() + ".dds"));
            result = TextureCooker::ensure_dds_is_up_to_date(
                a_fileSystem,
                resolvedTexturePath,
                cookedTexturePath);
            if (!result)
            {
                return result;
            }

            a_materialData.textureName = Core::IO::Path::join(
                Core::IO::Path("Textures"),
                Core::IO::Path(cookedTexturePath.filename())).utf8();
            a_materialData.isTextureUsed = true;
            return Result::ok();
        }

        [[nodiscard]] Result resolve_material_textures(
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_sourcePath,
            const Core::IO::Path& a_destinationPath,
            Core::Native::ModelData& a_modelData)
        {
            for (Core::Native::ImportedMaterialData& materialData :
                a_modelData.materials)
            {
                Result result = resolve_material_texture(
                    a_fileSystem,
                    a_sourcePath,
                    a_destinationPath,
                    materialData);
                if (!result)
                {
                    return result;
                }
            }

            return Result::ok();
        }

        [[nodiscard]] Result ensure_sidecar_material_textures_are_up_to_date(
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_sourcePath)
        {
            const Core::IO::Path sidecarMaterialPath = Core::IO::Path::join(
                a_sourcePath.parent(),
                Core::IO::Path(a_sourcePath.stem() + ".mtl"));
            bool sidecarMaterialExists = false;
            Result result = a_fileSystem.exists(
                sidecarMaterialPath,
                &sidecarMaterialExists);
            if (!result || !sidecarMaterialExists)
            {
                return result;
            }

            std::vector<std::byte> fileData{};
            result = a_fileSystem.read_all(sidecarMaterialPath, &fileData);
            if (!result)
            {
                return result;
            }

            const Core::IO::Path modelRoot = a_sourcePath.parent();
            const Core::IO::Path assetRoot = modelRoot.parent();
            const Core::IO::Path textureRoot = Core::IO::Path::join(
                assetRoot,
                Core::IO::Path("Textures"));

            std::string text{};
            if (!fileData.empty())
            {
                text.assign(
                    reinterpret_cast<const char*>(fileData.data()),
                    fileData.size());
            }
            size_t lineBegin = 0;
            while (lineBegin < text.size())
            {
                const size_t lineEnd = text.find('\n', lineBegin);
                const size_t lineSize = lineEnd == std::string::npos
                    ? text.size() - lineBegin
                    : lineEnd - lineBegin;

                std::string texturePathText{};
                if (extract_mtl_diffuse_texture_path(
                        std::string_view(text).substr(lineBegin, lineSize),
                        texturePathText))
                {
                    const Core::IO::Path texturePath(texturePathText);
                    if (!is_supported_texture_source(texturePath))
                    {
                        return Result::fail(
                            Code::Unsupported,
                            Severity::Error,
                            "Model material texture extension is not supported.");
                    }

                    std::vector<Core::IO::Path> candidates{};
                    if (texturePath.is_absolute())
                    {
                        push_unique_path(candidates, texturePath);
                    }
                    else
                    {
                        push_unique_path(
                            candidates,
                            Core::IO::Path::join(modelRoot, texturePath));
                        push_unique_path(
                            candidates,
                            Core::IO::Path::join(assetRoot, texturePath));
                        push_unique_path(
                            candidates,
                            Core::IO::Path::join(
                                textureRoot,
                                Core::IO::Path(texturePath.filename())));
                        push_unique_path(
                            candidates,
                            Core::IO::Path::join(
                                modelRoot,
                                Core::IO::Path(texturePath.filename())));
                    }

                    Core::IO::Path resolvedTexturePath{};
                    result = try_resolve_existing_path(
                        a_fileSystem,
                        std::span<const Core::IO::Path>(
                            candidates.data(),
                            candidates.size()),
                        resolvedTexturePath);
                    if (!result)
                    {
                        return result;
                    }

                    result = a_fileSystem.create_directories(textureRoot);
                    if (!result)
                    {
                        return result;
                    }

                    const Core::IO::Path cookedTexturePath =
                        Core::IO::Path::join(
                            textureRoot,
                            Core::IO::Path(
                                resolvedTexturePath.stem() + ".dds"));
                    result = TextureCooker::ensure_dds_is_up_to_date(
                        a_fileSystem,
                        resolvedTexturePath,
                        cookedTexturePath);
                    if (!result)
                    {
                        return result;
                    }
                }

                if (lineEnd == std::string::npos)
                {
                    break;
                }
                lineBegin = lineEnd + 1;
            }

            return Result::ok();
        }

        [[nodiscard]] Result ensure_gltf_textures_are_up_to_date(
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_sourcePath)
        {
            std::vector<Core::IO::Path> bufferPaths{};
            std::vector<Core::IO::Path> texturePaths{};
            Result result = collect_gltf_dependencies(
                a_fileSystem,
                a_sourcePath,
                bufferPaths,
                texturePaths);
            if (!result)
            {
                return result;
            }

            if (texturePaths.empty())
            {
                return Result::ok();
            }

            const Core::IO::Path assetRoot = a_sourcePath.parent().parent();
            const Core::IO::Path textureRoot = Core::IO::Path::join(
                assetRoot,
                Core::IO::Path("Textures"));
            result = a_fileSystem.create_directories(textureRoot);
            if (!result)
            {
                return result;
            }

            for (const Core::IO::Path& texturePath : texturePaths)
            {
                if (!is_supported_texture_source(texturePath))
                {
                    return Result::fail(
                        Code::Unsupported,
                        Severity::Error,
                        "glTF texture extension is not supported.");
                }

                const Core::IO::Path cookedTexturePath = Core::IO::Path::join(
                    textureRoot,
                    Core::IO::Path(texturePath.stem() + ".dds"));
                result = TextureCooker::ensure_dds_is_up_to_date(
                    a_fileSystem,
                    texturePath,
                    cookedTexturePath);
                if (!result)
                {
                    return result;
                }
            }

            return Result::ok();
        }

        [[nodiscard]] Result ensure_assimp_material_textures_are_up_to_date(
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_sourcePath,
            const Core::IO::Path& a_destinationPath)
        {
            std::vector<Core::IO::Path> texturePaths{};
            Result result = collect_assimp_material_texture_dependencies(
                a_sourcePath,
                texturePaths);
            if (!result)
            {
                return result;
            }

            if (texturePaths.empty())
            {
                return Result::ok();
            }

            const Core::IO::Path assetRoot = a_sourcePath.parent().parent();
            const Core::IO::Path textureRoot = Core::IO::Path::join(
                assetRoot,
                Core::IO::Path("Textures"));
            result = a_fileSystem.create_directories(textureRoot);
            if (!result)
            {
                return result;
            }

            for (const Core::IO::Path& texturePath : texturePaths)
            {
                if (!is_supported_texture_source(texturePath))
                {
                    return Result::fail(
                        Code::Unsupported,
                        Severity::Error,
                        "Model material texture extension is not supported.");
                }

                Core::IO::Path resolvedTexturePath{};
                result = resolve_model_texture_path(
                    a_fileSystem,
                    a_sourcePath,
                    a_destinationPath,
                    texturePath,
                    resolvedTexturePath);
                if (!result)
                {
                    return result;
                }

                const Core::IO::Path cookedTexturePath = Core::IO::Path::join(
                    textureRoot,
                    Core::IO::Path(resolvedTexturePath.stem() + ".dds"));
                result = TextureCooker::ensure_dds_is_up_to_date(
                    a_fileSystem,
                    resolvedTexturePath,
                    cookedTexturePath);
                if (!result)
                {
                    return result;
                }
            }

            return Result::ok();
        }

        [[nodiscard]] Result save_cuemodel(
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_filePath,
            const Core::Native::ModelData& a_modelData)
        {
            const Core::IO::Path normalizedPath = a_filePath.normalize();
            if (normalizedPath.extension() != ".cuemodel")
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Model asset file extension must be .cuemodel.");
            }

            if (a_modelData.meshes.empty())
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Model asset must contain at least one mesh.");
            }

            std::vector<CueModelMeshInfo> meshInfos{};
            meshInfos.reserve(a_modelData.meshes.size());
            std::vector<CueModelMaterialInfo> materialInfos{};
            materialInfos.reserve(a_modelData.materials.size());
            std::vector<CueModelRenderPartInfo> renderPartInfos{};
            std::vector<CueModelSkeletonJointInfo> jointInfos{};
            jointInfos.reserve(a_modelData.skeletonJoints.size());
            std::vector<CueModelAnimationClipInfo> animationClipInfos{};
            animationClipInfos.reserve(a_modelData.animationClips.size());
            std::vector<std::byte> payload{};

            for (const Core::Native::MeshData& meshData : a_modelData.meshes)
            {
                if (meshData.positions.empty() || meshData.indices.empty())
                {
                    return Result::fail(
                        Code::InvalidArgument,
                        Severity::Error,
                        "Model mesh must contain positions and indices.");
                }

                const uint32_t vertexCount = static_cast<uint32_t>(meshData.positions.size());
                const bool hasUvs = !meshData.uvs.empty();
                const bool hasNormals = !meshData.normals.empty();
                const bool hasSkinInfluences =
                    !meshData.skinInfluences.empty();
                if (hasUvs && meshData.uvs.size() != meshData.positions.size())
                {
                    return Result::fail(
                        Code::InvalidArgument,
                        Severity::Error,
                        "Model mesh uv count must match position count.");
                }
                if (hasNormals && meshData.normals.size() != meshData.positions.size())
                {
                    return Result::fail(
                        Code::InvalidArgument,
                        Severity::Error,
                        "Model mesh normal count must match position count.");
                }
                if (hasSkinInfluences &&
                    meshData.skinInfluences.size() != meshData.positions.size())
                {
                    return Result::fail(
                        Code::InvalidArgument,
                        Severity::Error,
                        "Model mesh skin influence count must match position count.");
                }

                CueModelMeshInfo meshInfo{};
                meshInfo.nameSize = static_cast<uint32_t>(meshData.name.size());
                meshInfo.vertexCount = vertexCount;
                meshInfo.indexCount = static_cast<uint32_t>(meshData.indices.size());
                meshInfo.flags =
                    (hasUvs ? k_cueModelMeshFlagHasUvs : 0u) |
                    (hasNormals ? k_cueModelMeshFlagHasNormals : 0u) |
                    (hasSkinInfluences
                            ? k_cueModelMeshFlagHasSkinInfluences
                            : 0u);

                meshInfo.nameOffset = payload.size();
                append_bytes(payload, meshData.name.data(), meshData.name.size());

                meshInfo.positionsOffset = payload.size();
                append_bytes(
                    payload,
                    meshData.positions.data(),
                    meshData.positions.size());

                if (hasUvs)
                {
                    meshInfo.uvsOffset = payload.size();
                    append_bytes(
                        payload,
                        meshData.uvs.data(),
                        meshData.uvs.size());
                }

                if (hasNormals)
                {
                    meshInfo.normalsOffset = payload.size();
                    append_bytes(
                        payload,
                        meshData.normals.data(),
                        meshData.normals.size());
                }

                meshInfo.indicesOffset = payload.size();
                append_bytes(
                    payload,
                    meshData.indices.data(),
                    meshData.indices.size());

                if (hasSkinInfluences)
                {
                    meshInfo.skinInfluencesOffset = payload.size();
                    append_bytes(
                        payload,
                        meshData.skinInfluences.data(),
                        meshData.skinInfluences.size());
                }

                meshInfos.push_back(meshInfo);
            }

            for (const Core::Native::ImportedMaterialData& materialData :
                a_modelData.materials)
            {
                CueModelMaterialInfo materialInfo{};
                materialInfo.nameSize =
                    static_cast<uint32_t>(materialData.name.size());
                materialInfo.textureNameSize =
                    static_cast<uint32_t>(materialData.textureName.size());
                materialInfo.flags =
                    (materialData.isTextureUsed
                            ? k_cueModelMaterialFlagHasTexture
                            : 0u) |
                    (materialData.usesReflectionSkybox
                            ? k_cueModelMaterialFlagUsesReflectionSkybox
                            : 0u);
                materialInfo.color = materialData.color;
                materialInfo.shininess = materialData.shininess;

                materialInfo.nameOffset = payload.size();
                append_bytes(
                    payload,
                    materialData.name.data(),
                    materialData.name.size());

                materialInfo.textureNameOffset = payload.size();
                append_bytes(
                    payload,
                    materialData.textureName.data(),
                    materialData.textureName.size());

                materialInfos.push_back(materialInfo);
            }

            const std::vector<Core::Native::ModelRenderPartData> fallbackParts =
                [&a_modelData]()
            {
                std::vector<Core::Native::ModelRenderPartData> parts{};
                if (!a_modelData.renderParts.empty())
                {
                    return parts;
                }

                parts.reserve(a_modelData.meshes.size());
                for (uint32_t meshIndex = 0;
                     meshIndex < a_modelData.meshes.size();
                     ++meshIndex)
                {
                    Core::Native::ModelRenderPartData renderPart{};
                    renderPart.name = a_modelData.meshes[meshIndex].name;
                    renderPart.meshIndex = meshIndex;
                    parts.push_back(std::move(renderPart));
                }
                return parts;
            }();
            const std::vector<Core::Native::ModelRenderPartData>& renderParts =
                a_modelData.renderParts.empty()
                ? fallbackParts
                : a_modelData.renderParts;
            renderPartInfos.reserve(renderParts.size());
            for (const Core::Native::ModelRenderPartData& renderPart :
                renderParts)
            {
                if (renderPart.meshIndex >= a_modelData.meshes.size())
                {
                    return Result::fail(
                        Code::InvalidArgument,
                        Severity::Error,
                        "Model render part mesh index is out of range.");
                }
                if (renderPart.materialIndex !=
                        Core::Native::k_invalidModelMaterialIndex &&
                    renderPart.materialIndex >= a_modelData.materials.size())
                {
                    return Result::fail(
                        Code::InvalidArgument,
                        Severity::Error,
                        "Model render part material index is out of range.");
                }

                CueModelRenderPartInfo renderPartInfo{};
                renderPartInfo.nameSize =
                    static_cast<uint32_t>(renderPart.name.size());
                renderPartInfo.meshIndex = renderPart.meshIndex;
                renderPartInfo.materialIndex = renderPart.materialIndex;
                renderPartInfo.localTransform = renderPart.localTransform;
                renderPartInfo.nameOffset = payload.size();
                append_bytes(
                    payload,
                    renderPart.name.data(),
                    renderPart.name.size());
                renderPartInfos.push_back(renderPartInfo);
            }

            for (const Core::Native::SkeletonJointData& jointData :
                 a_modelData.skeletonJoints)
            {
                CueModelSkeletonJointInfo jointInfo{};
                jointInfo.nameSize = static_cast<uint32_t>(jointData.name.size());
                jointInfo.parentIndex = jointData.parentIndex;
                jointInfo.inverseBindMatrix = jointData.inverseBindMatrix;
                jointInfo.localBindMatrix = jointData.localBindMatrix;
                jointInfo.nameOffset = payload.size();
                append_bytes(payload, jointData.name.data(), jointData.name.size());
                jointInfos.push_back(jointInfo);
            }

            for (const Core::Native::AnimationClipData& clipData :
                 a_modelData.animationClips)
            {
                CueModelAnimationClipInfo clipInfo{};
                clipInfo.nameSize = static_cast<uint32_t>(clipData.name.size());
                clipInfo.channelCount =
                    static_cast<uint32_t>(clipData.channels.size());
                clipInfo.duration = clipData.duration;
                clipInfo.ticksPerSecond = clipData.ticksPerSecond;
                clipInfo.nameOffset = payload.size();
                append_bytes(payload, clipData.name.data(), clipData.name.size());

                std::vector<CueModelAnimationChannelInfo> channelInfos{};
                channelInfos.reserve(clipData.channels.size());
                for (const Core::Native::AnimationChannelData& channelData :
                     clipData.channels)
                {
                    CueModelAnimationChannelInfo channelInfo{};
                    channelInfo.targetNameSize =
                        static_cast<uint32_t>(channelData.targetName.size());
                    channelInfo.jointIndex = channelData.jointIndex;
                    channelInfo.translationCount =
                        static_cast<uint32_t>(channelData.translations.size());
                    channelInfo.rotationCount =
                        static_cast<uint32_t>(channelData.rotations.size());
                    channelInfo.scaleCount =
                        static_cast<uint32_t>(channelData.scales.size());
                    channelInfo.targetNameOffset = payload.size();
                    append_bytes(payload,
                        channelData.targetName.data(),
                        channelData.targetName.size());
                    channelInfo.translationsOffset = payload.size();
                    append_bytes(payload,
                        channelData.translations.data(),
                        channelData.translations.size());
                    channelInfo.rotationsOffset = payload.size();
                    append_bytes(payload,
                        channelData.rotations.data(),
                        channelData.rotations.size());
                    channelInfo.scalesOffset = payload.size();
                    append_bytes(
                        payload, channelData.scales.data(), channelData.scales.size());
                    channelInfos.push_back(channelInfo);
                }
                clipInfo.channelsOffset = payload.size();
                append_bytes(payload, channelInfos.data(), channelInfos.size());
                animationClipInfos.push_back(clipInfo);
            }

            CueModelHeaderV3 header{};
            header.magic = k_cueModelMagic;
            header.version = k_cueModelVersion;
            header.meshCount = static_cast<uint32_t>(a_modelData.meshes.size());
            header.materialCount =
                static_cast<uint32_t>(a_modelData.materials.size());
            header.renderPartCount = static_cast<uint32_t>(renderParts.size());
            header.jointCount =
                static_cast<uint32_t>(a_modelData.skeletonJoints.size());
            header.animationClipCount =
                static_cast<uint32_t>(a_modelData.animationClips.size());
            header.dataSize = payload.size();

            const size_t headerSize =
                sizeof(CueModelHeaderV3) +
                sizeof(CueModelMeshInfo) * meshInfos.size() +
                sizeof(CueModelMaterialInfo) * materialInfos.size() +
                sizeof(CueModelRenderPartInfo) * renderPartInfos.size() +
                sizeof(CueModelSkeletonJointInfo) * jointInfos.size() +
                sizeof(CueModelAnimationClipInfo) * animationClipInfos.size();
            std::vector<std::byte> fileData(headerSize + payload.size());
            size_t writeOffset = 0;
            std::memcpy(
                fileData.data() + writeOffset,
                &header,
                sizeof(CueModelHeaderV3));
            writeOffset += sizeof(CueModelHeaderV3);
            std::memcpy(
                fileData.data() + writeOffset,
                meshInfos.data(),
                sizeof(CueModelMeshInfo) * meshInfos.size());
            writeOffset += sizeof(CueModelMeshInfo) * meshInfos.size();
            std::memcpy(
                fileData.data() + writeOffset,
                materialInfos.data(),
                sizeof(CueModelMaterialInfo) * materialInfos.size());
            writeOffset += sizeof(CueModelMaterialInfo) * materialInfos.size();
            std::memcpy(
                fileData.data() + writeOffset,
                renderPartInfos.data(),
                sizeof(CueModelRenderPartInfo) * renderPartInfos.size());
            writeOffset +=
                sizeof(CueModelRenderPartInfo) * renderPartInfos.size();
            std::memcpy(
                fileData.data() + writeOffset,
                jointInfos.data(),
                sizeof(CueModelSkeletonJointInfo) * jointInfos.size());
            writeOffset += sizeof(CueModelSkeletonJointInfo) * jointInfos.size();
            std::memcpy(
                fileData.data() + writeOffset,
                animationClipInfos.data(),
                sizeof(CueModelAnimationClipInfo) *
                    animationClipInfos.size());
            std::memcpy(
                fileData.data() + headerSize,
                payload.data(),
                payload.size());

            return a_fileSystem.write_all(
                normalizedPath,
                std::span<const std::byte>(fileData.data(), fileData.size()),
                true);
        }

        [[nodiscard]] Result is_cuemodel_version_current(
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_filePath,
            bool& outIsCurrent)
        {
            outIsCurrent = false;

            std::vector<std::byte> fileData{};
            Result result = a_fileSystem.read_all(a_filePath, &fileData);
            if (!result)
            {
                return result;
            }

            if (fileData.size() < sizeof(CueModelLegacyHeader))
            {
                return Result::ok();
            }

            CueModelLegacyHeader header{};
            std::memcpy(&header, fileData.data(), sizeof(header));
            outIsCurrent =
                header.magic == k_cueModelMagic &&
                header.version == k_cueModelVersion;
            return Result::ok();
        }
    }

    Result ModelCooker::cook_model_to_cuemodel(
        Core::IO::IFileSystem& a_fileSystem,
        const Core::IO::Path& a_sourcePath,
        const Core::IO::Path& a_destinationPath) noexcept
    {
        Core::Native::ModelData modelData{};
        Result result = ModelImporter::import_model(
            a_sourcePath,
            a_sourcePath.stem(),
            modelData);
        if (!result)
        {
            return result;
        }

        result = resolve_material_textures(
            a_fileSystem,
            a_sourcePath,
            a_destinationPath,
            modelData);
        if (!result)
        {
            return result;
        }

        return save_cuemodel(a_fileSystem, a_destinationPath, modelData);
    }

    Result ModelCooker::ensure_cuemodel_is_up_to_date(
        Core::IO::IFileSystem& a_fileSystem,
        const Core::IO::Path& a_sourcePath,
        const Core::IO::Path& a_destinationPath) noexcept
    {
        bool cookedModelExists = false;
        Result result = a_fileSystem.exists(
            a_destinationPath,
            &cookedModelExists);
        if (!result)
        {
            return result;
        }

        bool shouldRecook = !cookedModelExists;
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
            if (!shouldRecook)
            {
                const Core::IO::Path sidecarMaterialPath = Core::IO::Path::join(
                    a_sourcePath.parent(),
                    Core::IO::Path(a_sourcePath.stem() + ".mtl"));
                bool sidecarMaterialExists = false;
                result = a_fileSystem.exists(
                    sidecarMaterialPath,
                    &sidecarMaterialExists);
                if (!result)
                {
                    return result;
                }
                if (sidecarMaterialExists)
                {
                    Core::IO::FileStat sidecarMaterialStat{};
                    result = a_fileSystem.stat(
                        sidecarMaterialPath,
                        &sidecarMaterialStat);
                    if (!result)
                    {
                        return result;
                    }

                    shouldRecook =
                        sidecarMaterialStat.mtime_ns > cookedStat.mtime_ns;
                }
            }
            if (!shouldRecook)
            {
                std::vector<Core::IO::Path> gltfBufferPaths{};
                std::vector<Core::IO::Path> gltfTexturePaths{};
                result = collect_gltf_dependencies(
                    a_fileSystem,
                    a_sourcePath,
                    gltfBufferPaths,
                    gltfTexturePaths);
                if (!result)
                {
                    return result;
                }

                bool isDependencyNewer = false;
                result = is_any_dependency_newer(
                    a_fileSystem,
                    std::span<const Core::IO::Path>(
                        gltfBufferPaths.data(),
                        gltfBufferPaths.size()),
                    cookedStat.mtime_ns,
                    isDependencyNewer);
                if (!result)
                {
                    return result;
                }
                shouldRecook = isDependencyNewer;
            }
            if (!shouldRecook)
            {
                bool isCurrentVersion = false;
                result = is_cuemodel_version_current(
                    a_fileSystem,
                    a_destinationPath,
                    isCurrentVersion);
                if (!result)
                {
                    return result;
                }

                shouldRecook = !isCurrentVersion;
            }
        }

        if (!shouldRecook)
        {
            result = ensure_sidecar_material_textures_are_up_to_date(
                a_fileSystem,
                a_sourcePath);
            if (!result)
            {
                return result;
            }

            result = ensure_gltf_textures_are_up_to_date(
                a_fileSystem,
                a_sourcePath);
            if (!result)
            {
                return result;
            }

            return ensure_assimp_material_textures_are_up_to_date(
                a_fileSystem,
                a_sourcePath,
                a_destinationPath);
        }

        return cook_model_to_cuemodel(
            a_fileSystem,
            a_sourcePath,
            a_destinationPath);
    }
}

#include "ModelCooker.h"

// === Engine Includes ===
#include <asset/ModelAssetFormat.h>
#include <ModelImporter.h>

// === Core Includes ===
#include <Native/EngineNativeStruct.h>

// === C++ Includes ===
#include <cstring>
#include <span>
#include <type_traits>
#include <vector>

namespace Cue::Editor
{
    namespace
    {
        static_assert(std::is_trivially_copyable_v<CueModelHeader>);
        static_assert(std::is_trivially_copyable_v<CueModelMeshInfo>);

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

                CueModelMeshInfo meshInfo{};
                meshInfo.nameSize = static_cast<uint32_t>(meshData.name.size());
                meshInfo.vertexCount = vertexCount;
                meshInfo.indexCount = static_cast<uint32_t>(meshData.indices.size());
                meshInfo.flags =
                    (hasUvs ? k_cueModelMeshFlagHasUvs : 0u) |
                    (hasNormals ? k_cueModelMeshFlagHasNormals : 0u);

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

                meshInfos.push_back(meshInfo);
            }

            CueModelHeader header{};
            header.magic = k_cueModelMagic;
            header.version = k_cueModelVersion;
            header.meshCount = static_cast<uint32_t>(a_modelData.meshes.size());
            header.dataSize = payload.size();

            const size_t headerSize =
                sizeof(CueModelHeader) +
                sizeof(CueModelMeshInfo) * meshInfos.size();
            std::vector<std::byte> fileData(headerSize + payload.size());
            std::memcpy(fileData.data(), &header, sizeof(CueModelHeader));
            std::memcpy(
                fileData.data() + sizeof(CueModelHeader),
                meshInfos.data(),
                sizeof(CueModelMeshInfo) * meshInfos.size());
            std::memcpy(
                fileData.data() + headerSize,
                payload.data(),
                payload.size());

            return a_fileSystem.write_all(
                normalizedPath,
                std::span<const std::byte>(fileData.data(), fileData.size()),
                true);
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
        }

        if (!shouldRecook)
        {
            return Result::ok();
        }

        return cook_model_to_cuemodel(
            a_fileSystem,
            a_sourcePath,
            a_destinationPath);
    }
}

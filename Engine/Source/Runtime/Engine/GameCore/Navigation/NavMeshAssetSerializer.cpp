#include "NavMeshAssetSerializer.h"

// === C++ includes ===
#include <cstring>
#include <span>

namespace Cue::GameCore
{
    namespace
    {
        [[nodiscard]] bool has_valid_extension(
            const Core::IO::Path& a_filePath) noexcept
        {
            return a_filePath.normalize().extension() == ".cuenavmesh";
        }

        [[nodiscard]] CueNavMeshHeader make_header(
            const NavMeshAssetData& a_asset) noexcept
        {
            CueNavMeshHeader header{};
            header.magic = k_cueNavMeshMagic;
            header.version = k_cueNavMeshVersion;
            header.flags = a_asset.isTiled ? k_cueNavMeshFlagTiled : 0u;
            header.tileCount = a_asset.isTiled ? 1u : 0u;
            header.dataSize = static_cast<uint64_t>(a_asset.navData.size());
            header.sourceGeometryHash = a_asset.sourceGeometryHash;
            header.buildHash = a_asset.buildHash;
            header.cellSize = a_asset.bakeSettings.cellSize;
            header.cellHeight = a_asset.bakeSettings.cellHeight;
            header.agentHeight = a_asset.bakeSettings.agentHeight;
            header.agentRadius = a_asset.bakeSettings.agentRadius;
            header.agentMaxClimb = a_asset.bakeSettings.agentMaxClimb;
            header.agentMaxSlope = a_asset.bakeSettings.agentMaxSlope;
            header.regionMinSize = a_asset.bakeSettings.regionMinSize;
            header.regionMergeSize = a_asset.bakeSettings.regionMergeSize;
            header.edgeMaxLen = a_asset.bakeSettings.edgeMaxLen;
            header.edgeMaxError = a_asset.bakeSettings.edgeMaxError;
            header.detailSampleDist = a_asset.bakeSettings.detailSampleDist;
            header.detailSampleMaxError =
                a_asset.bakeSettings.detailSampleMaxError;
            header.vertsPerPoly = a_asset.bakeSettings.vertsPerPoly;
            return header;
        }

        void apply_header(
            const CueNavMeshHeader& a_header,
            NavMeshAssetData& a_outAsset) noexcept
        {
            a_outAsset.bakeSettings.cellSize = a_header.cellSize;
            a_outAsset.bakeSettings.cellHeight = a_header.cellHeight;
            a_outAsset.bakeSettings.agentHeight = a_header.agentHeight;
            a_outAsset.bakeSettings.agentRadius = a_header.agentRadius;
            a_outAsset.bakeSettings.agentMaxClimb = a_header.agentMaxClimb;
            a_outAsset.bakeSettings.agentMaxSlope = a_header.agentMaxSlope;
            a_outAsset.bakeSettings.regionMinSize = a_header.regionMinSize;
            a_outAsset.bakeSettings.regionMergeSize = a_header.regionMergeSize;
            a_outAsset.bakeSettings.edgeMaxLen = a_header.edgeMaxLen;
            a_outAsset.bakeSettings.edgeMaxError = a_header.edgeMaxError;
            a_outAsset.bakeSettings.detailSampleDist = a_header.detailSampleDist;
            a_outAsset.bakeSettings.detailSampleMaxError =
                a_header.detailSampleMaxError;
            a_outAsset.bakeSettings.vertsPerPoly = a_header.vertsPerPoly;
            a_outAsset.sourceGeometryHash = a_header.sourceGeometryHash;
            a_outAsset.buildHash = a_header.buildHash;
            a_outAsset.isTiled =
                (a_header.flags & k_cueNavMeshFlagTiled) != 0u;
        }

        [[nodiscard]] Result validate_header(
            const CueNavMeshHeader& a_header,
            size_t a_fileSize) noexcept
        {
            if (a_header.magic != k_cueNavMeshMagic)
            {
                return Result::fail(Code::InvalidArgument, Severity::Error,
                    "NavMesh asset magic is invalid.");
            }
            if (a_header.version != k_cueNavMeshVersion)
            {
                return Result::fail(Code::Unsupported, Severity::Error,
                    "NavMesh asset version is not supported.");
            }
            if ((a_header.flags & ~k_cueNavMeshFlagTiled) != 0u)
            {
                return Result::fail(Code::Unsupported, Severity::Error,
                    "NavMesh asset flags are not supported.");
            }
            if ((a_header.flags & k_cueNavMeshFlagTiled) != 0u)
            {
                return Result::fail(Code::Unsupported, Severity::Error,
                    "Tiled NavMesh assets are not supported yet.");
            }
            if (a_header.tileCount != 0u)
            {
                return Result::fail(Code::Unsupported, Severity::Error,
                    "NavMesh asset tile table is not supported yet.");
            }
            if (a_header.dataSize == 0)
            {
                return Result::fail(Code::InvalidArgument, Severity::Error,
                    "NavMesh asset payload is empty.");
            }

            const uint64_t expectedSize =
                sizeof(CueNavMeshHeader) + a_header.dataSize;
            if (expectedSize != static_cast<uint64_t>(a_fileSize))
            {
                return Result::fail(Code::InvalidArgument, Severity::Error,
                    "NavMesh asset payload size is invalid.");
            }

            return Result::ok();
        }
    }

    Result NavMeshAssetSerializer::serialize(
        const NavMeshAssetData& a_asset,
        std::vector<std::byte>& a_outData) noexcept
    {
        a_outData.clear();
        if (a_asset.navData.empty())
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "NavMesh asset payload must not be empty.");
        }
        if (a_asset.isTiled)
        {
            return Result::fail(Code::Unsupported, Severity::Error,
                "Tiled NavMesh asset serialization is not supported yet.");
        }

        const CueNavMeshHeader header = make_header(a_asset);
        const size_t headerSize = sizeof(CueNavMeshHeader);
        a_outData.resize(headerSize + a_asset.navData.size());
        std::memcpy(a_outData.data(), &header, headerSize);
        std::memcpy(a_outData.data() + headerSize, a_asset.navData.data(),
            a_asset.navData.size());
        return Result::ok();
    }

    Result NavMeshAssetSerializer::deserialize(
        std::span<const std::byte> a_data,
        NavMeshAssetData& a_outAsset) noexcept
    {
        a_outAsset = {};
        if (a_data.size() < sizeof(CueNavMeshHeader))
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "NavMesh asset file is too small.");
        }

        CueNavMeshHeader header{};
        std::memcpy(&header, a_data.data(), sizeof(CueNavMeshHeader));

        Result result = validate_header(header, a_data.size());
        if (!result)
        {
            return result;
        }

        apply_header(header, a_outAsset);
        const size_t payloadBegin = sizeof(CueNavMeshHeader);
        a_outAsset.navData.resize(static_cast<size_t>(header.dataSize));
        std::memcpy(a_outAsset.navData.data(), a_data.data() + payloadBegin,
            a_outAsset.navData.size());
        return Result::ok();
    }

    Result NavMeshAssetSerializer::save(
        const NavMeshAssetData& a_asset,
        Core::IO::IFileSystem& a_fileSystem,
        const Core::IO::Path& a_filePath) noexcept
    {
        const Core::IO::Path normalizedPath = a_filePath.normalize();
        if (!has_valid_extension(normalizedPath))
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "NavMesh asset file extension must be .cuenavmesh.");
        }

        std::vector<std::byte> fileData{};
        Result result = serialize(a_asset, fileData);
        if (!result)
        {
            return result;
        }

        return a_fileSystem.write_all(normalizedPath,
            std::span<const std::byte>(fileData.data(), fileData.size()), true);
    }

    Result NavMeshAssetSerializer::load(
        Core::IO::IFileSystem& a_fileSystem,
        const Core::IO::Path& a_filePath,
        NavMeshAssetData& a_outAsset) noexcept
    {
        a_outAsset = {};
        const Core::IO::Path normalizedPath = a_filePath.normalize();
        if (!has_valid_extension(normalizedPath))
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "NavMesh asset file extension must be .cuenavmesh.");
        }

        std::vector<std::byte> fileData{};
        Result result = a_fileSystem.read_all(normalizedPath, &fileData);
        if (!result)
        {
            return result;
        }

        return deserialize(
            std::span<const std::byte>(fileData.data(), fileData.size()),
            a_outAsset);
    }
}

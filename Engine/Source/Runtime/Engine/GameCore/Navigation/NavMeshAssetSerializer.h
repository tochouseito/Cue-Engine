// NavMeshAssetSerializer の役割と公開要素を定義する

#pragma once

// === Base includes ===
#include <Result.h>

// === Core includes ===
#include <IO/IFileSystem.h>

// === Engine includes ===
#include "NavMeshAssetFormat.h"
#include "NavTypes.h"

// === C++ includes ===
#include <cstddef>
#include <span>
#include <vector>

namespace Cue::GameCore
{
    class NavMeshAssetSerializer final
    {
    public:
        NavMeshAssetSerializer() = delete;

        [[nodiscard]] static Result serialize(
            const NavMeshAssetData& a_asset,
            std::vector<std::byte>& a_outData) noexcept;

        [[nodiscard]] static Result deserialize(
            std::span<const std::byte> a_data,
            NavMeshAssetData& a_outAsset) noexcept;

        [[nodiscard]] static Result save(
            const NavMeshAssetData& a_asset,
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_filePath) noexcept;

        [[nodiscard]] static Result load(
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_filePath,
            NavMeshAssetData& a_outAsset) noexcept;
    };
}

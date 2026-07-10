#pragma once

/// **********************************************************************
/// Asset Browser と Inspector が共有する選択中 Asset の情報を定義する
/// **********************************************************************

// === Runtime includes ===
#include <IO/Path.h>

// === C++ includes ===
#include <cstdint>

namespace Cue::Editor
{
    /// @brief Editor が扱う Asset の種別
    enum class AssetKind : uint8_t
    {
        unknown = 0,
        scene,
        model,
        texture,
        material,
    };

    /// @brief Inspector 表示に必要な Asset の選択時点の情報
    struct AssetSelection final
    {
        Core::IO::Path path{};
        uint64_t sizeBytes = 0;
        AssetKind kind = AssetKind::unknown;
    };

    /// @brief Asset path の拡張子から Editor 表示用の種別を解決する
    [[nodiscard]] AssetKind
    classify_asset_kind(const Core::IO::Path& a_path) noexcept;

    /// @brief Asset Browser と Inspector で共有する種別名を返す
    [[nodiscard]] const char* asset_kind_name(AssetKind a_kind) noexcept;
} // namespace Cue::Editor

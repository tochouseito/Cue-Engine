#pragma once

/// **********************************************************************
/// GameProject の Script DLL 用 CMake 構成を生成する
/// **********************************************************************

// === Base includes ===
#include <CueResult.h>

// === C++ includes ===
#include <string_view>

namespace Cue::Core::IO
{
    class IFileSystem;
    class Path;
}

namespace Cue::Editor
{
    /// @brief GameProject に Script 編集・ビルド用の CMake 構成を配置する
    class GameProjectGenerator final
    {
    public:
        explicit GameProjectGenerator(Core::IO::IFileSystem& a_fileSystem) noexcept;

        /// @brief Project root へ GameScript DLL 用の CMake ファイル群を作成する
        ///
        /// 既存の Project 固有 CMake 編集を失わないよう、未作成のファイルだけを配置する
        [[nodiscard]] Result generate(const Core::IO::Path& a_projectRoot) const;

    private:
        [[nodiscard]] Result ensure_text_file(
            const Core::IO::Path& a_path, std::string_view a_text) const;

        Core::IO::IFileSystem* m_fileSystem = nullptr; // Project 作成と同じ FileSystem 経由で出力する非所有サービス
    };
} // namespace Cue::Editor

#pragma once

/// **********************************************************************
/// GameScript DLL の CMake configure と build を実行する
/// **********************************************************************

// === Base includes ===
#include <CueResult.h>

// === Core includes ===
#include <IO/Path.h>

// === C++ includes ===
#include <cstdint>
#include <string>
#include <string_view>

namespace Cue::Core::IO
{
    class IFileSystem;
}

namespace Cue::Editor
{
    /// @brief GameScript の configure または build の実行結果
    struct ScriptBuildReport final
    {
        std::string summary{};
        std::string output{};
        Core::IO::Path logPath{};
        uint32_t exitCode = 0u;
        bool didConfigure = false;
        bool succeeded = false;
    };

    /// @brief Project root の CMake preset から GameScript DLL を構成・ビルドする
    class GameScriptBuildRunner final
    {
    public:
        explicit GameScriptBuildRunner(Core::IO::IFileSystem& a_fileSystem) noexcept;

        /// @brief CMake configure を実行して Visual Studio project を生成する
        [[nodiscard]] Result configure(
            const Core::IO::Path& a_scriptRoot,
            ScriptBuildReport& a_outReport) const noexcept;

        /// @brief GameScript target をビルドし、未構成なら先に configure する
        [[nodiscard]] Result build(
            const Core::IO::Path& a_scriptRoot,
            std::string_view a_configuration,
            ScriptBuildReport& a_outReport) const noexcept;

    private:
        [[nodiscard]] Result run_cmake(
            const Core::IO::Path& a_scriptRoot,
            std::wstring_view a_commandLine,
            std::string_view a_logFileName,
            bool a_didConfigure,
            ScriptBuildReport& a_outReport) const noexcept;

        Core::IO::IFileSystem* m_fileSystem = nullptr; // Project root と同じ FileSystem で configure cache と log を扱う
    };
} // namespace Cue::Editor

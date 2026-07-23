#pragma once

/// **********************************************************************
/// GameScript DLL の hot reload 用 shadow copy を作成する
/// **********************************************************************

// === Base includes ===
#include <CueResult.h>

// === Core includes ===
#include <IO/IFileSystem.h>

// === C++ includes ===
#include <cstdint>

namespace Cue::Script
{
    /// @brief ビルド出力をロックせずにロードできる世代別 DLL を作成する
    class ScriptShadowCopyService final
    {
    public:
        explicit ScriptShadowCopyService(Core::IO::IFileSystem& a_fileSystem) noexcept;

        /// @brief DLL と任意の PDB を Project の中間領域へ複製する
        [[nodiscard]] Result create_shadow_copy(const Core::IO::Path& a_scriptRoot, const Core::IO::Path& a_modulePath,
                                                uint64_t a_copyId, Core::IO::Path& a_outModulePath) noexcept;

    private:
        Core::IO::IFileSystem* m_fileSystem = nullptr;
    };
} // namespace Cue::Script

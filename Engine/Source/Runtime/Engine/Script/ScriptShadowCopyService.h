// ScriptShadowCopyService の役割と公開要素を定義する

#pragma once

// === Base includes ===
#include <Result.h>

// === Core includes ===
#include <IO/IFileSystem.h>

namespace Cue
{
    class ScriptShadowCopyService final
    {
    public:
        explicit ScriptShadowCopyService(
            Core::IO::IFileSystem& a_fileSystem) noexcept
            : m_fileSystem(a_fileSystem)
        {
        }

        [[nodiscard]] Result create_shadow_copy(
            const Core::IO::Path& a_scriptRoot,
            const Core::IO::Path& a_modulePath,
            uint64_t a_shadowCopyId,
            Core::IO::Path& a_outShadowModulePath) noexcept;

    private:
        Core::IO::IFileSystem& m_fileSystem;
    };
}

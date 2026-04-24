#include "ScriptShadowCopyService.h"

namespace Cue
{
    namespace
    {
        [[nodiscard]] Result copy_file_if_exists(
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_sourcePath,
            const Core::IO::Path& a_destinationPath) noexcept
        {
            bool exists = false;
            Result result = a_fileSystem.exists(a_sourcePath, &exists);
            if (!result)
            {
                return result;
            }
            if (!exists)
            {
                return Result::ok();
            }

            return a_fileSystem.copy_file(a_sourcePath, a_destinationPath, true);
        }
    }

    Result ScriptShadowCopyService::create_shadow_copy(
        const Core::IO::Path& a_scriptRoot,
        const Core::IO::Path& a_modulePath,
        uint64_t a_shadowCopyId,
        Core::IO::Path& a_outShadowModulePath) noexcept
    {
        const Core::IO::Path shadowDirectory = Core::IO::Path::join(
            a_scriptRoot,
            Core::IO::Path("Intermediate/ScriptRuntime"));
        Result result = m_fileSystem.create_directories(shadowDirectory);
        if (!result)
        {
            return Result::fail(Code::CreateFailed, Severity::Error,
                "Script shadow copy directory の作成に失敗しました。");
        }

        const std::string shadowBaseName =
            a_modulePath.stem() + "_" + std::to_string(a_shadowCopyId);
        a_outShadowModulePath = Core::IO::Path::join(
            shadowDirectory,
            Core::IO::Path(shadowBaseName + a_modulePath.extension()));

        result = m_fileSystem.copy_file(
            a_modulePath, a_outShadowModulePath, true);
        if (!result)
        {
            return Result::fail(Code::CreateFailed, Severity::Error,
                "Script module shadow copy の作成に失敗しました。");
        }

        const Core::IO::Path sourcePdbPath = Core::IO::Path::join(
            a_modulePath.parent(),
            Core::IO::Path(a_modulePath.stem() + ".pdb"));
        const Core::IO::Path shadowPdbPath = Core::IO::Path::join(
            shadowDirectory,
            Core::IO::Path(shadowBaseName + ".pdb"));
        result = copy_file_if_exists(
            m_fileSystem, sourcePdbPath, shadowPdbPath);
        if (!result)
        {
            return Result::fail(Code::CreateFailed, Severity::Error,
                "Script module PDB shadow copy の作成に失敗しました。");
        }

        return Result::ok();
    }
}

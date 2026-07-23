#include "ScriptShadowCopyService.h"

// === C++ includes ===
#include <string>

namespace Cue::Script
{
    namespace
    {
        [[nodiscard]] Result copy_file_if_exists(Core::IO::IFileSystem& a_fileSystem,
                                                 const Core::IO::Path& a_sourcePath,
                                                 const Core::IO::Path& a_destinationPath) noexcept
        {
            bool exists = false;
            Result result = a_fileSystem.exists(a_sourcePath, &exists);
            if (!result || !exists)
            {
                return result;
            }

            return a_fileSystem.copy_file(a_sourcePath, a_destinationPath, true);
        }
    } // namespace

    ScriptShadowCopyService::ScriptShadowCopyService(Core::IO::IFileSystem& a_fileSystem) noexcept
        : m_fileSystem(&a_fileSystem)
    {
    }

    Result ScriptShadowCopyService::create_shadow_copy(const Core::IO::Path& a_scriptRoot,
                                                       const Core::IO::Path& a_modulePath, uint64_t a_copyId,
                                                       Core::IO::Path& a_outModulePath) noexcept
    {
        a_outModulePath = {};
        if (m_fileSystem == nullptr || a_scriptRoot.is_empty() || a_modulePath.is_empty())
        {
            return Result::fail(Code::InvalidArgument, Severity::Error, "Script shadow copy path is invalid.");
        }

        // ビルド対象外の中間領域へ複製し、元 DLL を CMake が更新できる状態に保つ
        const Core::IO::Path shadowDirectory =
            Core::IO::Path::join(a_scriptRoot, Core::IO::Path("Intermediate/ScriptRuntime"));
        Result result = m_fileSystem->create_directories(shadowDirectory);
        if (!result)
        {
            return result;
        }

        const std::string shadowBaseName = a_modulePath.stem() + "_" + std::to_string(a_copyId);
        a_outModulePath =
            Core::IO::Path::join(shadowDirectory, Core::IO::Path(shadowBaseName + a_modulePath.extension()));
        result = m_fileSystem->copy_file(a_modulePath, a_outModulePath, true);
        if (!result)
        {
            a_outModulePath = {};
            return result;
        }

        const Core::IO::Path sourcePdbPath =
            Core::IO::Path::join(a_modulePath.parent(), Core::IO::Path(a_modulePath.stem() + ".pdb"));
        const Core::IO::Path shadowPdbPath =
            Core::IO::Path::join(shadowDirectory, Core::IO::Path(shadowBaseName + ".pdb"));
        result = copy_file_if_exists(*m_fileSystem, sourcePdbPath, shadowPdbPath);
        if (!result)
        {
            bool wasRemoved = false;
            (void)m_fileSystem->remove(a_outModulePath, &wasRemoved);
            a_outModulePath = {};
            return result;
        }

        return Result::ok();
    }
} // namespace Cue::Script

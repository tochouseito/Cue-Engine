#include "TextureCooker.h"

// === Engine includes ===
#include <Engine/Source/Runtime/PAL/Win/ConvertUTF.h>

// === C++ includes ===
#include <cctype>
#include <cstdlib>
#include <iterator>
#include <process.h>
#include <string>
#include <vector>

// === Windows includes ===
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace Cue::Editor
{
    namespace
    {
        [[nodiscard]] std::string to_lower_ascii(std::string a_text) noexcept
        {
            for (char& character : a_text)
            {
                character = static_cast<char>(std::tolower(
                    static_cast<unsigned char>(character)));
            }
            return a_text;
        }

        [[nodiscard]] Result to_wide_path(
            const Core::IO::Path& a_path,
            std::wstring& outWidePath)
        {
            return PAL::Win::utf8_to_wide(a_path.normalize().utf8(), &outWidePath);
        }

        [[nodiscard]] bool file_exists(const std::wstring& a_path) noexcept
        {
            const DWORD attributes = ::GetFileAttributesW(a_path.c_str());
            return attributes != INVALID_FILE_ATTRIBUTES &&
                (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
        }

        [[nodiscard]] bool environment_path(
            const wchar_t* a_name,
            std::wstring& outPath)
        {
            wchar_t buffer[MAX_PATH]{};
            const DWORD length = ::GetEnvironmentVariableW(
                a_name,
                buffer,
                static_cast<DWORD>(std::size(buffer)));
            if (length == 0 || length >= std::size(buffer))
            {
                return false;
            }

            outPath = buffer;
            return !outPath.empty();
        }

        [[nodiscard]] std::wstring join_wide_path(
            std::wstring a_left,
            const wchar_t* a_right)
        {
            if (!a_left.empty() && a_left.back() != L'\\' && a_left.back() != L'/')
            {
                a_left.push_back(L'\\');
            }
            a_left += a_right;
            return a_left;
        }

        [[nodiscard]] std::wstring directxtex_tool_path_from_project_root(
            const wchar_t* a_toolName)
        {
#ifdef CUE_PROJECT_ROOT_PATH
            std::wstring projectRoot{};
            if (!PAL::Win::utf8_to_wide(
                    std::string(CUE_PROJECT_ROOT_PATH),
                    &projectRoot))
            {
                return {};
            }

            return join_wide_path(
                join_wide_path(
                    join_wide_path(
                    join_wide_path(
                        join_wide_path(projectRoot, L"out"),
                        L"build"),
                    L"win-x64"),
                    L"vcpkg_installed\\x64-windows-static-md\\tools\\directxtex"),
                a_toolName);
#else
            return {};
#endif
        }

        [[nodiscard]] std::wstring resolve_directxtex_tool_path(
            const wchar_t* a_environmentName,
            const wchar_t* a_toolName)
        {
            std::wstring toolPath{};
            if (environment_path(a_environmentName, toolPath) &&
                file_exists(toolPath))
            {
                return toolPath;
            }

            toolPath = directxtex_tool_path_from_project_root(a_toolName);
            if (!toolPath.empty() && file_exists(toolPath))
            {
                return toolPath;
            }

            return a_toolName;
        }

        [[nodiscard]] Result run_texconv_to_dds(
            const Core::IO::Path& a_sourcePath,
            const Core::IO::Path& a_destinationPath)
        {
            std::wstring sourcePath{};
            Result result = to_wide_path(a_sourcePath, sourcePath);
            if (!result)
            {
                return result;
            }

            std::wstring destinationDirectory{};
            result = to_wide_path(a_destinationPath.parent(), destinationDirectory);
            if (!result)
            {
                return result;
            }

            std::wstring texconvPath = resolve_directxtex_tool_path(
                L"CUE_TEXCONV_PATH",
                L"texconv.exe");
            std::vector<std::wstring> arguments{
                texconvPath,
                L"-nologo",
                L"-y",
                L"-ft",
                L"dds",
                L"-m",
                L"0",
                L"-o",
                destinationDirectory
            };

            const std::string extension =
                to_lower_ascii(a_sourcePath.extension());
            if (extension != ".dds")
            {
                arguments.push_back(L"-f");
                arguments.push_back(L"R8G8B8A8_UNORM");
            }
            arguments.push_back(sourcePath);

            std::vector<wchar_t*> argv{};
            argv.reserve(arguments.size() + 1);
            for (std::wstring& argument : arguments)
            {
                argv.push_back(argument.data());
            }
            argv.push_back(nullptr);

            const intptr_t exitCode =
                _wspawnvp(_P_WAIT, texconvPath.c_str(), argv.data());
            if (exitCode != 0)
            {
                return Result::fail(
                    Code::CreateFailed,
                    Severity::Error,
                    "texconv failed to convert texture to DDS.");
            }

            return Result::ok();
        }

        [[nodiscard]] Result run_texassemble_cube(
            const std::array<Core::IO::Path, 6>& a_facePaths,
            const Core::IO::Path& a_destinationPath)
        {
            std::wstring destinationPath{};
            Result result = to_wide_path(a_destinationPath, destinationPath);
            if (!result)
            {
                return result;
            }

            std::array<std::wstring, 6> facePaths{};
            for (size_t faceIndex = 0; faceIndex < a_facePaths.size(); ++faceIndex)
            {
                result = to_wide_path(a_facePaths[faceIndex], facePaths[faceIndex]);
                if (!result)
                {
                    return result;
                }
            }

            std::wstring texassemblePath = resolve_directxtex_tool_path(
                L"CUE_TEXASSEMBLE_PATH",
                L"texassemble.exe");
            std::vector<std::wstring> arguments{
                texassemblePath,
                L"cube",
                L"-nologo",
                L"-y",
                L"-f",
                L"R8G8B8A8_UNORM",
                L"-o",
                destinationPath,
                L"--"
            };
            for (const std::wstring& facePath : facePaths)
            {
                arguments.push_back(facePath);
            }

            std::vector<wchar_t*> argv{};
            argv.reserve(arguments.size() + 1);
            for (std::wstring& argument : arguments)
            {
                argv.push_back(argument.data());
            }
            argv.push_back(nullptr);

            const intptr_t exitCode =
                _wspawnvp(_P_WAIT, texassemblePath.c_str(), argv.data());
            if (exitCode != 0)
            {
                return Result::fail(
                    Code::CreateFailed,
                    Severity::Error,
                    "texassemble failed to create DDS CubeMap.");
            }

            return Result::ok();
        }
    }

    Result TextureCooker::cook_source_to_dds(
        Core::IO::IFileSystem& a_fileSystem,
        const Core::IO::Path& a_sourcePath,
        const Core::IO::Path& a_destinationPath) noexcept
    {
        const Core::IO::Path normalizedDestination =
            a_destinationPath.normalize();
        if (to_lower_ascii(normalizedDestination.extension()) != ".dds")
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Texture asset file extension must be .dds.");
        }

        const std::string sourceExtension =
            to_lower_ascii(a_sourcePath.extension());
        if (sourceExtension == ".dds")
        {
            if (a_sourcePath.normalize().utf8() == normalizedDestination.utf8())
            {
                return Result::ok();
            }

            return a_fileSystem.copy_file(
                a_sourcePath.normalize(),
                normalizedDestination,
                true);
        }

        return run_texconv_to_dds(a_sourcePath, normalizedDestination);
    }

    Result TextureCooker::ensure_dds_is_up_to_date(
        Core::IO::IFileSystem& a_fileSystem,
        const Core::IO::Path& a_sourcePath,
        const Core::IO::Path& a_destinationPath) noexcept
    {
        bool cookedTextureExists = false;
        Result result = a_fileSystem.exists(
            a_destinationPath, &cookedTextureExists);
        if (!result)
        {
            return result;
        }

        bool shouldRecook = !cookedTextureExists;
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

        return cook_source_to_dds(
            a_fileSystem,
            a_sourcePath,
            a_destinationPath);
    }

    Result TextureCooker::make_cube_dds_from_faces(
        Core::IO::IFileSystem& a_fileSystem,
        const std::array<Core::IO::Path, 6>& a_facePaths,
        const Core::IO::Path& a_destinationPath) noexcept
    {
        const Core::IO::Path normalizedDestination =
            a_destinationPath.normalize();
        if (to_lower_ascii(normalizedDestination.extension()) != ".dds")
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Cube texture output extension must be .dds.");
        }

        for (const Core::IO::Path& facePath : a_facePaths)
        {
            if (facePath.is_empty())
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Cube texture face path is empty.");
            }

            bool exists = false;
            Result result = a_fileSystem.exists(facePath.normalize(), &exists);
            if (!result)
            {
                return result;
            }
            if (!exists)
            {
                return Result::fail(
                    Code::NotFound,
                    Severity::Error,
                    "Cube texture face image was not found.");
            }
        }

        return run_texassemble_cube(a_facePaths, normalizedDestination);
    }
}

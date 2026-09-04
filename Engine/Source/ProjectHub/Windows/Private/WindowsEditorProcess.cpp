#include <Cue/ProjectHub/Windows/WindowsProjectHubPlatform.h>

#include <Cue/Foundation/Assert.h>
#include <Cue/Foundation/Windows/UtfConversion.h>
#include <Cue/IO/Error.h>
#include <Cue/ProjectHub/Error.h>

#include <cstdint>
#include <exception>
#include <string>
#include <string_view>
#include <utility>

#include <Windows.h>

namespace
{
constexpr std::size_t k_maxCommandLineLength = 32767;

/// @brief Allocation失敗をEditor Process起動境界からFatal終端する
[[noreturn]] void terminate_allocation(const cue::AssertContext &a_context) noexcept
{
    a_context.fatal_handler().terminate("Windows Editor process command construction failed");
    std::terminate();
}

/// @brief UTF-8引数をNULなしのStrict UTF-16へ変換する
[[nodiscard]] cue::Result<std::wstring> to_utf16(std::string_view a_text, const cue::AssertContext &a_context) noexcept
{
    std::wstring converted;
    const cue::WindowsUtfConversionResult result =
        cue::convert_utf8_to_windows_utf16(a_text, converted, a_context.fatal_handler());
    if (result.status != cue::WindowsUtfConversionStatus::Success || converted.find(L'\0') != std::wstring::npos)
    {
        return cue::Result<std::wstring>::failure(
            cue::project_hub::make_project_hub_error(a_context, cue::project_hub::ProjectHubError::EditorLaunchFailed,
                                                     "Editor process argument is not valid UTF-8"));
    }
    return cue::Result<std::wstring>::success(std::move(converted));
}

/// @brief Windows C Runtime規則で一つのCommand Line引数をQuoteする
void append_argument(std::wstring &a_commandLine, std::wstring_view a_argument)
{
    if (!a_commandLine.empty())
    {
        a_commandLine.push_back(L' ');
    }
    a_commandLine.push_back(L'"');
    std::size_t slashCount = 0;
    for (wchar_t character : a_argument)
    {
        if (character == L'\\')
        {
            ++slashCount;
            continue;
        }
        if (character == L'"')
        {
            a_commandLine.append(slashCount * 2 + 1, L'\\');
            a_commandLine.push_back(L'"');
            slashCount = 0;
            continue;
        }
        a_commandLine.append(slashCount, L'\\');
        slashCount = 0;
        a_commandLine.push_back(character);
    }
    a_commandLine.append(slashCount * 2, L'\\');
    a_commandLine.push_back(L'"');
}

/// @brief UTF-8値を変換してCommand Lineへ一引数として追加する
[[nodiscard]] cue::Result<void> append_utf8_argument(std::wstring &a_commandLine, std::string_view a_argument,
                                                     const cue::AssertContext &a_context) noexcept
{
    cue::Result<std::wstring> converted = to_utf16(a_argument, a_context);
    if (!converted)
    {
        return cue::Result<void>::failure(std::move(*converted.try_error()));
    }
    try
    {
        append_argument(a_commandLine, *converted.try_value());
    }
    catch (...)
    {
        terminate_allocation(a_context);
    }
    return cue::Result<void>::success();
}

/// @brief Editor Process起動のWin32失敗をProject Hub分類へ変換する
[[nodiscard]] cue::Error make_launch_error(const cue::AssertContext &a_context, DWORD a_nativeCode) noexcept
{
    cue::Error cause = cue::make_io_error(a_context, cue::IoError::IoFailure, "Editor process creation failed",
                                          static_cast<std::int64_t>(a_nativeCode));
    return cue::project_hub::reclassify_project_hub_error(a_context,
                                                          cue::project_hub::ProjectHubError::EditorLaunchFailed,
                                                          "Editor process could not be launched", std::move(cause));
}
} // namespace

namespace cue::project_hub
{
Result<void> launch_windows_editor_process(std::string_view a_editorExecutableLocator,
                                           const EditorLaunchRequest &a_request,
                                           const AssertContext &a_assertContext) noexcept
{
    Result<std::wstring> executable = to_utf16(a_editorExecutableLocator, a_assertContext);
    if (!executable || executable.try_value()->empty())
    {
        return executable
                   ? Result<void>::failure(make_project_hub_error(a_assertContext, ProjectHubError::EditorLaunchFailed,
                                                                  "Editor executable locator is empty"))
                   : Result<void>::failure(std::move(*executable.try_error()));
    }

    std::wstring commandLine;
    try
    {
        commandLine.reserve(1024);
        append_argument(commandLine, *executable.try_value());
        append_argument(commandLine, L"--protocol-version");
        append_argument(commandLine, std::to_wstring(a_request.protocol_version()));
        append_argument(commandLine, L"--project-descriptor");
    }
    catch (...)
    {
        terminate_allocation(a_assertContext);
    }
    Result<void> descriptor =
        append_utf8_argument(commandLine, a_request.project_descriptor_locator(), a_assertContext);
    if (!descriptor)
    {
        return descriptor;
    }
    try
    {
        append_argument(commandLine, L"--expected-project-id");
    }
    catch (...)
    {
        terminate_allocation(a_assertContext);
    }
    Result<void> projectId = append_utf8_argument(commandLine, a_request.expected_project_id(), a_assertContext);
    if (!projectId)
    {
        return projectId;
    }
    try
    {
        append_argument(commandLine, L"--engine-compatibility-id");
    }
    catch (...)
    {
        terminate_allocation(a_assertContext);
    }
    Result<void> compatibility =
        append_utf8_argument(commandLine, a_request.engine_compatibility_id(), a_assertContext);
    if (!compatibility)
    {
        return compatibility;
    }
    if (a_request.initial_scene_locator().has_value())
    {
        try
        {
            append_argument(commandLine, L"--initial-scene");
        }
        catch (...)
        {
            terminate_allocation(a_assertContext);
        }
        Result<void> scene = append_utf8_argument(commandLine, *a_request.initial_scene_locator(), a_assertContext);
        if (!scene)
        {
            return scene;
        }
    }
    if (commandLine.size() >= k_maxCommandLineLength)
    {
        return Result<void>::failure(make_project_hub_error(a_assertContext, ProjectHubError::EditorLaunchFailed,
                                                            "Editor process command line exceeds the Windows limit"));
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const BOOL created = CreateProcessW(executable.try_value()->c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0,
                                        nullptr, nullptr, &startup, &process);
    if (!created)
    {
        return Result<void>::failure(make_launch_error(a_assertContext, GetLastError()));
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return Result<void>::success();
}
} // namespace cue::project_hub

#include <Cue/Foundation/Assert.h>
#include <Cue/Foundation/Fatal.h>
#include <Cue/Foundation/Log.h>
#include <Cue/IO/Error.h>
#include <Cue/IO/Windows/WindowsWorkspaceFilesystem.h>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
/// @brief Test中の回復不能失敗を終了Codeへ変換する
class TestFatalHandler final : public cue::FatalHandler
{
  public:
    /// @brief MessageなしFatalをTest終了Codeへ変換する
    [[noreturn]] void terminate() noexcept override
    {
        std::_Exit(90);
    }

    /// @brief Message付きFatalをTest終了Codeへ変換する
    [[noreturn]] void terminate(std::string_view) noexcept override
    {
        std::_Exit(91);
    }
};

/// @brief Windows Workspace Test専用Directoryを作成して限定Cleanupする
class TestDirectory final
{
  public:
    /// @brief ProcessとTickから一意なRootおよびRoot外Directoryを作成する
    TestDirectory()
    {
        std::array<wchar_t, MAX_PATH> temporary{};
        const DWORD length = GetTempPathW(static_cast<DWORD>(temporary.size()), temporary.data());
        if (length == 0U || length >= temporary.size())
        {
            return;
        }
        m_path = temporary.data();
        m_path +=
            L"CueWorkspaceTests-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
        m_outsidePath = m_path + L"-Outside";
        m_movedPath = m_path + L"-Moved";
        m_created = CreateDirectoryW(m_path.c_str(), nullptr) != FALSE &&
                    CreateDirectoryW(m_outsidePath.c_str(), nullptr) != FALSE;
    }
    /// @brief Test Directoryの一意Cleanup責務を保つためCopy構築を禁止する
    TestDirectory(const TestDirectory &) = delete;
    /// @brief Test Directoryの一意Cleanup責務を保つためCopy代入を禁止する
    TestDirectory &operator=(const TestDirectory &) = delete;
    /// @brief Test DirectoryのPathを固定するためMove構築を禁止する
    TestDirectory(TestDirectory &&) = delete;
    /// @brief Test DirectoryのPathを固定するためMove代入を禁止する
    TestDirectory &operator=(TestDirectory &&) = delete;
    /// @brief Test専用DirectoryだけをBest-effortで再帰削除する
    ~TestDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
        std::filesystem::remove_all(m_movedPath, error);
        std::filesystem::remove_all(m_outsidePath, error);
    }

    /// @brief Test Directory作成に成功したか返す
    [[nodiscard]] bool is_created() const noexcept
    {
        return m_created;
    }

    /// @brief Factoryへ渡すRoot UTF-8 Pathを返す
    [[nodiscard]] std::string utf8_path() const
    {
        const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, m_path.c_str(),
                                              static_cast<int>(m_path.size()), nullptr, 0, nullptr, nullptr);
        std::string result(static_cast<std::size_t>(count), '\0');
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, m_path.c_str(), static_cast<int>(m_path.size()),
                            result.data(), count, nullptr, nullptr);
        return result;
    }

    /// @brief Root配下のNative Pathを返す
    [[nodiscard]] std::wstring child(std::wstring_view a_relative) const
    {
        return m_path + L"\\" + std::wstring(a_relative);
    }

    /// @brief Root外Test Directory Pathを返す
    [[nodiscard]] const std::wstring &outside_path() const noexcept
    {
        return m_outsidePath;
    }

    /// @brief Root置換試行先Pathを返す
    [[nodiscard]] const std::wstring &moved_path() const noexcept
    {
        return m_movedPath;
    }

    /// @brief Root Native Pathを返す
    [[nodiscard]] const std::wstring &path() const noexcept
    {
        return m_path;
    }

  private:
    std::wstring m_path;
    std::wstring m_outsidePath;
    std::wstring m_movedPath;
    bool m_created = false;
};

/// @brief Resultが指定Portable IO分類を保持するか判定する
template <typename T> [[nodiscard]] bool has_io_error(cue::Result<T> &a_result, cue::IoError a_code) noexcept
{
    const cue::Error *error = a_result.try_error();
    return error != nullptr && error->code().domain() == "Cue.IO" &&
           error->code().value() == static_cast<std::int64_t>(a_code);
}

/// @brief Native Test Fileへ全Byteを書き込む
[[nodiscard]] bool write_file(std::wstring_view a_path, std::string_view a_bytes)
{
    const std::wstring path(a_path);
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return false;
    }
    DWORD written = 0U;
    const BOOL succeeded = WriteFile(file, a_bytes.data(), static_cast<DWORD>(a_bytes.size()), &written, nullptr);
    CloseHandle(file);
    return succeeded != FALSE && written == static_cast<DWORD>(a_bytes.size());
}

/// @brief Snapshotから表示名が一致するEntryを返す
[[nodiscard]] const cue::WorkspaceEntry *find_entry(const cue::DirectorySnapshot &a_snapshot,
                                                    std::string_view a_name) noexcept
{
    for (const cue::WorkspaceEntry &entry : a_snapshot.entries)
    {
        if (entry.displayName == a_name)
        {
            return &entry;
        }
    }
    return nullptr;
}

/// @brief Directory、File、Unsupported Entryの決定的列挙を検証する
[[nodiscard]] bool test_listing(cue::WorkspaceFilesystem &a_filesystem, const cue::AssertContext &a_assertContext,
                                bool a_hasReparse)
{
    const cue::TraversalLimits limits{8U, 64U, 64U, 16U * 1024U};
    auto first = a_filesystem.list_directory(cue::WorkspaceDirectory::root(), limits);
    auto second = a_filesystem.list_directory(cue::WorkspaceDirectory::root(), limits);
    if (!first || !second || first.try_value()->entries.size() != second.try_value()->entries.size() ||
        first.try_value()->generation == second.try_value()->generation)
    {
        return false;
    }
    for (std::size_t index = 0U; index < first.try_value()->entries.size(); ++index)
    {
        if (first.try_value()->entries[index].displayName != second.try_value()->entries[index].displayName ||
            first.try_value()->entries[index].parentGeneration != first.try_value()->generation)
        {
            return false;
        }
    }

    const cue::WorkspaceEntry *folder = find_entry(*first.try_value(), "Folder");
    const cue::WorkspaceEntry *alpha = find_entry(*first.try_value(), "alpha.bin");
    const cue::WorkspaceEntry *zeta = find_entry(*first.try_value(), "Zeta.bin");
    const cue::WorkspaceEntry *hidden = find_entry(*first.try_value(), ".Hidden");
    const cue::WorkspaceEntry *reparse = find_entry(*first.try_value(), "ReparseLink");
    if (folder == nullptr || alpha == nullptr || zeta == nullptr || hidden == nullptr || !folder->is_operable() ||
        folder->type != cue::WorkspaceEntryType::Directory || alpha->byteSize != 5U || zeta->byteSize != 4U ||
        hidden->is_operable() || hidden->rejection != cue::WorkspaceDiagnosticCode::UnsupportedName ||
        (a_hasReparse && (reparse == nullptr || reparse->is_operable() ||
                          reparse->rejection != cue::WorkspaceDiagnosticCode::ReparsePoint)))
    {
        return false;
    }

    std::vector<std::string> names;
    for (const cue::WorkspaceEntry &entry : first.try_value()->entries)
    {
        names.push_back(entry.displayName);
    }
    const auto folderPosition = std::find(names.begin(), names.end(), "Folder");
    const auto alphaPosition = std::find(names.begin(), names.end(), "alpha.bin");
    const auto zetaPosition = std::find(names.begin(), names.end(), "Zeta.bin");
    const auto hiddenPosition = std::find(names.begin(), names.end(), ".Hidden");
    if (folderPosition == names.end() || alphaPosition == names.end() || zetaPosition == names.end() ||
        hiddenPosition == names.end() ||
        !(folderPosition < alphaPosition && alphaPosition < zetaPosition && zetaPosition < hiddenPosition))
    {
        return false;
    }

    auto folderLocator = cue::RelativePath::parse("Folder", a_assertContext);
    if (!folderLocator)
    {
        return false;
    }
    auto nested = a_filesystem.list_directory(
        cue::WorkspaceDirectory::from_locator(std::move(*folderLocator.try_value())), limits);
    return nested && nested.try_value()->entries.size() == 1U &&
           nested.try_value()->entries[0].displayName == "Needle.txt";
}

/// @brief Bounded SearchがReparse PointをTraversalせず各上限を拒否するか検証する
[[nodiscard]] bool test_search(cue::WorkspaceFilesystem &a_filesystem, const cue::AssertContext &a_assertContext,
                               bool a_hasReparse)
{
    auto found = cue::search_workspace(a_filesystem, cue::WorkspaceDirectory::root(), "needle",
                                       cue::TraversalLimits{8U, 64U, 64U, 16U * 1024U}, a_assertContext);
    if (!found || found.try_value()->entries.size() != 1U || found.try_value()->entries[0].displayName != "Needle.txt")
    {
        return false;
    }
    auto depth = cue::search_workspace(a_filesystem, cue::WorkspaceDirectory::root(), {},
                                       cue::TraversalLimits{1U, 64U, 64U, 16U * 1024U}, a_assertContext);
    auto visited = cue::search_workspace(a_filesystem, cue::WorkspaceDirectory::root(), {},
                                         cue::TraversalLimits{8U, 1U, 64U, 16U * 1024U}, a_assertContext);
    auto results = cue::search_workspace(a_filesystem, cue::WorkspaceDirectory::root(), {},
                                         cue::TraversalLimits{8U, 64U, 1U, 16U * 1024U}, a_assertContext);
    auto metadata = cue::search_workspace(a_filesystem, cue::WorkspaceDirectory::root(), {},
                                          cue::TraversalLimits{8U, 64U, 64U, 1U}, a_assertContext);
    if (!has_io_error(depth, cue::IoError::CapacityExceeded) ||
        !has_io_error(visited, cue::IoError::CapacityExceeded) ||
        !has_io_error(results, cue::IoError::CapacityExceeded) ||
        !has_io_error(metadata, cue::IoError::CapacityExceeded))
    {
        return false;
    }
    if (a_hasReparse)
    {
        auto outside = cue::search_workspace(a_filesystem, cue::WorkspaceDirectory::root(), "OutsideSecret",
                                             cue::TraversalLimits{8U, 64U, 64U, 16U * 1024U}, a_assertContext);
        if (!outside || !outside.try_value()->entries.empty())
        {
            return false;
        }
        auto linkLocator = cue::RelativePath::parse("ReparseLink", a_assertContext);
        auto escaped =
            a_filesystem.list_directory(cue::WorkspaceDirectory::from_locator(std::move(*linkLocator.try_value())),
                                        cue::TraversalLimits{2U, 4U, 4U, 1024U});
        if (!has_io_error(escaped, cue::IoError::UnsupportedEntry))
        {
            return false;
        }
    }
    return true;
}

/// @brief Share ViolationをEntry単位Permission診断とRescan要求へ変換するか検証する
[[nodiscard]] bool test_access_denied(cue::WorkspaceFilesystem &a_filesystem, const TestDirectory &a_directory)
{
    const std::wstring lockedPath = a_directory.child(L"Locked.bin");
    HANDLE locked = CreateFileW(lockedPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0U, nullptr, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (locked == INVALID_HANDLE_VALUE)
    {
        return false;
    }
    auto snapshot =
        a_filesystem.list_directory(cue::WorkspaceDirectory::root(), cue::TraversalLimits{4U, 64U, 64U, 16U * 1024U});
    CloseHandle(locked);
    if (!snapshot || snapshot.try_value()->state != cue::WorkspaceSnapshotState::RescanRequired)
    {
        return false;
    }
    const cue::WorkspaceEntry *entry = find_entry(*snapshot.try_value(), "Locked.bin");
    if (entry == nullptr || entry->rejection != cue::WorkspaceDiagnosticCode::PermissionDenied)
    {
        return false;
    }
    for (const cue::WorkspaceDiagnostic &diagnostic : snapshot.try_value()->diagnostics)
    {
        if (diagnostic.displayName == "Locked.bin" && diagnostic.code == cue::WorkspaceDiagnosticCode::PermissionDenied)
        {
            return true;
        }
    }
    return false;
}

/// @brief Workspace Factoryが絶対DirectoryだけをRootとして受理するか検証する
[[nodiscard]] bool test_factory_validation(const TestDirectory &a_directory, const cue::AssertContext &a_assertContext)
{
    auto relative = cue::create_windows_workspace_filesystem("RelativeRoot", a_assertContext);
    const std::string filePath = a_directory.utf8_path() + "/alpha.bin";
    auto file = cue::create_windows_workspace_filesystem(filePath, a_assertContext);
    std::string embeddedNul = a_directory.utf8_path();
    embeddedNul.push_back('\0');
    embeddedNul.append("Ignored");
    auto nul = cue::create_windows_workspace_filesystem(embeddedNul, a_assertContext);
    return has_io_error(relative, cue::IoError::InvalidPath) && has_io_error(file, cue::IoError::TypeMismatch) &&
           has_io_error(nul, cue::IoError::InvalidPath);
}
} // namespace

/// @brief Windows Workspace列挙、Reparse拒否、競合診断、Root Pinを統合検証する
int main()
{
    TestFatalHandler fatalHandler;
    std::vector<std::unique_ptr<cue::LogSink>> sinks;
    cue::Logger logger(fatalHandler, std::move(sinks));
    cue::AssertContext assertContext(logger, fatalHandler);
    TestDirectory directory;
    if (!directory.is_created() || CreateDirectoryW(directory.child(L"Folder").c_str(), nullptr) == FALSE ||
        !write_file(directory.child(L"Folder\\Needle.txt"), "needle") ||
        !write_file(directory.child(L"alpha.bin"), "alpha") || !write_file(directory.child(L"Zeta.bin"), "zeta") ||
        !write_file(directory.child(L".Hidden"), "hidden") ||
        !write_file(directory.outside_path() + L"\\OutsideSecret.bin", "secret"))
    {
        return 1;
    }

    const std::wstring linkPath = directory.child(L"ReparseLink");
    const bool hasReparse =
        CreateSymbolicLinkW(linkPath.c_str(), directory.outside_path().c_str(),
                            SYMBOLIC_LINK_FLAG_DIRECTORY | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) != FALSE;
    if (!hasReparse)
    {
        const DWORD code = GetLastError();
        if (code != ERROR_PRIVILEGE_NOT_HELD && code != ERROR_INVALID_PARAMETER && code != ERROR_NOT_SUPPORTED)
        {
            return 2;
        }
    }

    if (!test_factory_validation(directory, assertContext))
    {
        return 3;
    }
    auto filesystem = cue::create_windows_workspace_filesystem(directory.utf8_path(), assertContext);
    if (!filesystem)
    {
        return 4;
    }
    if (!test_listing(**filesystem.try_value(), assertContext, hasReparse))
    {
        return 5;
    }
    if (!test_search(**filesystem.try_value(), assertContext, hasReparse))
    {
        return 6;
    }
    if (!test_access_denied(**filesystem.try_value(), directory))
    {
        return 7;
    }

    auto limited = (**filesystem.try_value())
                       .list_directory(cue::WorkspaceDirectory::root(), cue::TraversalLimits{2U, 64U, 1U, 16U * 1024U});
    if (!has_io_error(limited, cue::IoError::CapacityExceeded))
    {
        return 8;
    }

    if (MoveFileExW(directory.path().c_str(), directory.moved_path().c_str(), 0U) != FALSE)
    {
        MoveFileExW(directory.moved_path().c_str(), directory.path().c_str(), 0U);
        return 9;
    }
    const DWORD replacementCode = GetLastError();
    return replacementCode == ERROR_SHARING_VIOLATION || replacementCode == ERROR_ACCESS_DENIED ? 0 : 10;
}

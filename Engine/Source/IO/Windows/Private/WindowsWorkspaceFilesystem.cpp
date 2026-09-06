#include <Cue/IO/Windows/WindowsWorkspaceFilesystem.h>

#include <Cue/Foundation/Assert.h>
#include <Cue/Foundation/Windows/UtfConversion.h>
#include <Cue/IO/Error.h>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cwctype>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
constexpr std::size_t k_maxWindowsPathLength = 32767U;
constexpr cue::TraversalLimits k_windowsHardLimits{64U, 1'000'000U, 1'000'000U, 64U * 1024U * 1024U};

/// @brief Win32 Handleを一意所有して全終了経路でCloseする
class UniqueHandle final
{
  public:
    /// @brief 無効Handleを所有しない状態で生成する
    UniqueHandle() noexcept = default;
    /// @brief Native HandleのClose責務を受け取る
    explicit UniqueHandle(HANDLE a_handle) noexcept : m_handle(a_handle)
    {
    }
    /// @brief Handleの二重Closeを防ぐためCopy構築を禁止する
    UniqueHandle(const UniqueHandle &) = delete;
    /// @brief Handleの二重Closeを防ぐためCopy代入を禁止する
    UniqueHandle &operator=(const UniqueHandle &) = delete;
    /// @brief Native Handleの所有権を移動する
    UniqueHandle(UniqueHandle &&a_other) noexcept : m_handle(a_other.release())
    {
    }
    /// @brief 現在のHandleを閉じて新しい所有権を移動する
    UniqueHandle &operator=(UniqueHandle &&a_other) noexcept
    {
        if (this != &a_other)
        {
            reset(a_other.release());
        }
        return *this;
    }
    /// @brief 所有Handleを閉じる
    ~UniqueHandle()
    {
        reset();
    }

    /// @brief Native APIへ渡すHandleを返す
    [[nodiscard]] HANDLE get() const noexcept
    {
        return m_handle;
    }

    /// @brief HandleがNative API呼出しに使用可能か判定する
    [[nodiscard]] bool is_valid() const noexcept
    {
        return m_handle != nullptr && m_handle != INVALID_HANDLE_VALUE;
    }

    /// @brief CloseせずHandle所有権を返す
    [[nodiscard]] HANDLE release() noexcept
    {
        HANDLE handle = m_handle;
        m_handle = INVALID_HANDLE_VALUE;
        return handle;
    }

    /// @brief 現在のHandleを閉じて指定Handleを所有する
    void reset(HANDLE a_handle = INVALID_HANDLE_VALUE) noexcept
    {
        if (is_valid())
        {
            CloseHandle(m_handle);
        }
        m_handle = a_handle;
    }

  private:
    HANDLE m_handle = INVALID_HANDLE_VALUE;
};

/// @brief noexcept処理中のAllocation失敗をProject Fatal Policyへ接続する
[[noreturn]] void terminate_allocation(const cue::AssertContext &a_assertContext) noexcept
{
    a_assertContext.fatal_handler().terminate("Windows workspace filesystem allocation failed");
    std::abort();
}

/// @brief Win32 ErrorをPortable IO分類へ変換する
[[nodiscard]] cue::IoError classify_windows_error(DWORD a_code) noexcept
{
    switch (a_code)
    {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
        return cue::IoError::NotFound;
    case ERROR_ACCESS_DENIED:
    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION:
        return cue::IoError::PermissionDenied;
    case ERROR_FILENAME_EXCED_RANGE:
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY:
        return cue::IoError::CapacityExceeded;
    default:
        return cue::IoError::IoFailure;
    }
}

/// @brief Win32 ErrorをNative Code付きErrorへ変換する
[[nodiscard]] cue::Error make_windows_error(const cue::AssertContext &a_assertContext, DWORD a_code,
                                            std::string_view a_summary) noexcept
{
    return cue::make_io_error(a_assertContext, classify_windows_error(a_code), a_summary,
                              static_cast<std::int64_t>(a_code));
}

/// @brief UTF-8を厳密なUTF-16へ変換する
[[nodiscard]] cue::Result<std::wstring> to_utf16(std::string_view a_text,
                                                 const cue::AssertContext &a_assertContext) noexcept
{
    std::wstring converted;
    const cue::WindowsUtfConversionResult conversion =
        cue::convert_utf8_to_windows_utf16(a_text, converted, a_assertContext.fatal_handler());
    if (conversion.status != cue::WindowsUtfConversionStatus::Success)
    {
        const cue::IoError code = conversion.status == cue::WindowsUtfConversionStatus::InputTooLong
                                      ? cue::IoError::CapacityExceeded
                                      : cue::IoError::InvalidPath;
        return cue::Result<std::wstring>::failure(
            cue::make_io_error(a_assertContext, code, "Workspace root UTF-8 conversion failed", conversion.nativeCode));
    }
    return cue::Result<std::wstring>::success(std::move(converted));
}

/// @brief UTF-16 Entry Nameを厳密なUTF-8へ変換する
[[nodiscard]] cue::Result<std::string> to_utf8(std::wstring_view a_text,
                                               const cue::AssertContext &a_assertContext) noexcept
{
    std::string converted;
    const cue::WindowsUtfConversionResult conversion =
        cue::convert_windows_utf16_to_utf8(a_text, converted, a_assertContext.fatal_handler());
    if (conversion.status != cue::WindowsUtfConversionStatus::Success)
    {
        const cue::IoError code = conversion.status == cue::WindowsUtfConversionStatus::InputTooLong
                                      ? cue::IoError::CapacityExceeded
                                      : cue::IoError::InvalidPath;
        return cue::Result<std::string>::failure(cue::make_io_error(
            a_assertContext, code, "Workspace entry UTF-16 conversion failed", conversion.nativeCode));
    }
    return cue::Result<std::string>::success(std::move(converted));
}

/// @brief Absolute Windows PathをExtended Path表現へ変換する
[[nodiscard]] cue::Result<std::wstring> make_extended_path(std::wstring a_path,
                                                           const cue::AssertContext &a_assertContext) noexcept
{
    try
    {
        for (wchar_t &character : a_path)
        {
            if (character == L'/')
            {
                character = L'\\';
            }
        }
        if (a_path.starts_with(L"\\\\?\\"))
        {
            return cue::Result<std::wstring>::success(std::move(a_path));
        }
        if (a_path.starts_with(L"\\\\"))
        {
            a_path = L"\\\\?\\UNC\\" + a_path.substr(2U);
        }
        else
        {
            a_path = L"\\\\?\\" + a_path;
        }
    }
    catch (...)
    {
        terminate_allocation(a_assertContext);
    }
    if (a_path.size() >= k_maxWindowsPathLength)
    {
        return cue::Result<std::wstring>::failure(cue::make_io_error(a_assertContext, cue::IoError::CapacityExceeded,
                                                                     "Workspace native path limit was exceeded"));
    }
    return cue::Result<std::wstring>::success(std::move(a_path));
}

/// @brief Windows Path PrefixをOrdinalな大小文字非依存で比較する
[[nodiscard]] bool starts_with_ordinal_ignore_case(std::wstring_view a_text, std::wstring_view a_prefix) noexcept
{
    if (a_text.size() < a_prefix.size())
    {
        return false;
    }
    return CompareStringOrdinal(a_text.data(), static_cast<int>(a_prefix.size()), a_prefix.data(),
                                static_cast<int>(a_prefix.size()), TRUE) == CSTR_EQUAL;
}

/// @brief 通常またはExtended形式のUNC Pathか判定する
[[nodiscard]] bool is_unc_path(std::wstring_view a_path) noexcept
{
    const bool extendedPrefix = a_path.starts_with(L"\\\\?\\");
    return (a_path.starts_with(L"\\\\") && !extendedPrefix) || starts_with_ordinal_ignore_case(a_path, L"\\\\?\\UNC\\");
}

/// @brief Local Drive Rootで保持すべき末尾Separator込み長を返す
[[nodiscard]] std::size_t local_drive_root_length(std::wstring_view a_path) noexcept
{
    if (a_path.size() >= 7U && a_path.starts_with(L"\\\\?\\") && std::iswalpha(a_path[4U]) != 0 && a_path[5U] == L':' &&
        (a_path[6U] == L'\\' || a_path[6U] == L'/'))
    {
        return 7U;
    }
    return 3U;
}

/// @brief Invalid UTF-16 Nameを安定した表示・Sort用16進表現へ変換する
[[nodiscard]] std::string make_native_name_fallback(std::wstring_view a_name,
                                                    const cue::AssertContext &a_assertContext) noexcept
{
    constexpr std::array<char, 16> k_hex{'0', '1', '2', '3', '4', '5', '6', '7',
                                         '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
    std::string result;
    try
    {
        result = "<invalid-utf16:";
        result.reserve(result.size() + a_name.size() * 4U + 1U);
        for (wchar_t character : a_name)
        {
            const std::uint16_t value = static_cast<std::uint16_t>(character);
            result.push_back(k_hex[(value >> 12U) & 0x0fU]);
            result.push_back(k_hex[(value >> 8U) & 0x0fU]);
            result.push_back(k_hex[(value >> 4U) & 0x0fU]);
            result.push_back(k_hex[value & 0x0fU]);
        }
        result.push_back('>');
    }
    catch (...)
    {
        terminate_allocation(a_assertContext);
    }
    return result;
}

/// @brief Entry MetadataのBounded Byte数を返す
[[nodiscard]] std::size_t metadata_bytes(const cue::WorkspaceEntry &a_entry) noexcept
{
    const std::size_t locatorBytes = a_entry.locator.has_value() ? a_entry.locator->text().size() : 0U;
    constexpr std::size_t k_fixedBytes = sizeof(cue::WorkspaceEntry);
    if (a_entry.displayName.size() > std::numeric_limits<std::size_t>::max() - k_fixedBytes ||
        k_fixedBytes + a_entry.displayName.size() > std::numeric_limits<std::size_t>::max() - a_entry.sortKey.size() ||
        k_fixedBytes + a_entry.displayName.size() + a_entry.sortKey.size() >
            std::numeric_limits<std::size_t>::max() - locatorBytes)
    {
        return std::numeric_limits<std::size_t>::max();
    }
    return k_fixedBytes + a_entry.displayName.size() + a_entry.sortKey.size() + locatorBytes;
}

/// @brief Entry診断が使用するBounded Metadata Byte数を返す
[[nodiscard]] std::size_t metadata_bytes(const cue::WorkspaceDiagnostic &a_diagnostic) noexcept
{
    if (a_diagnostic.displayName.size() > std::numeric_limits<std::size_t>::max() - sizeof(cue::WorkspaceDiagnostic))
    {
        return std::numeric_limits<std::size_t>::max();
    }
    return sizeof(cue::WorkspaceDiagnostic) + a_diagnostic.displayName.size();
}

/// @brief Unsupported Entryの表示名をSort KeyへProject Fatal Policy付きで複製する
void copy_display_sort_key(cue::WorkspaceEntry &a_entry, const cue::AssertContext &a_assertContext) noexcept
{
    try
    {
        a_entry.sortKey = a_entry.displayName;
    }
    catch (...)
    {
        terminate_allocation(a_assertContext);
    }
}

/// @brief Entry単位診断をProject Fatal Policy付きでSnapshotへ追加する
void append_diagnostic(cue::DirectorySnapshot &a_snapshot, cue::WorkspaceDiagnosticCode a_code,
                       std::string_view a_displayName, std::int64_t a_nativeCode,
                       const cue::AssertContext &a_assertContext) noexcept
{
    try
    {
        a_snapshot.diagnostics.push_back(
            cue::WorkspaceDiagnostic{a_snapshot.generation, a_code, std::string(a_displayName), a_nativeCode});
    }
    catch (...)
    {
        terminate_allocation(a_assertContext);
    }
}

/// @brief Root IdentityをProcess寿命中に比較する値
struct RootIdentity final
{
    DWORD volumeSerial = 0U;
    DWORD fileIndexHigh = 0U;
    DWORD fileIndexLow = 0U;
};

/// @brief Pin済みDirectory Handleから取得したEntry MetadataとFile Identity
struct NativeDirectoryEntry final
{
    std::wstring name;
    DWORD attributes = 0U;
    LARGE_INTEGER fileId{};
};

/// @brief Directory Handle列挙の完了状態と取得済みNative Entryを所有する
struct NativeDirectoryEnumeration final
{
    std::vector<NativeDirectoryEntry> entries;
    std::optional<DWORD> interruptedCode;
};

/// @brief Pin済みDirectory HandleをFile ID付きで列挙し、途中I/O失敗では取得済みEntryを保持する
[[nodiscard]] cue::Result<NativeDirectoryEnumeration> enumerate_directory_handle(
    HANDLE a_directoryHandle, std::size_t a_maximumEntries, std::size_t a_maximumMetadataBytes,
    const cue::AssertContext &a_assertContext) noexcept
{
    constexpr std::size_t k_directoryBufferBytes = 64U * 1024U;
    alignas(FILE_ID_BOTH_DIR_INFO) std::array<std::byte, k_directoryBufferBytes> buffer{};
    NativeDirectoryEnumeration enumeration;
    bool restart = true;
    bool receivedBatch = false;
    std::size_t metadataBytes = 0U;

    try
    {
        while (true)
        {
            const FILE_INFO_BY_HANDLE_CLASS informationClass =
                restart ? FileIdBothDirectoryRestartInfo : FileIdBothDirectoryInfo;
            if (GetFileInformationByHandleEx(a_directoryHandle, informationClass, buffer.data(),
                                             static_cast<DWORD>(buffer.size())) == FALSE)
            {
                const DWORD code = GetLastError();
                if (code == ERROR_NO_MORE_FILES)
                {
                    break;
                }
                if (!receivedBatch)
                {
                    return cue::Result<NativeDirectoryEnumeration>::failure(
                        make_windows_error(a_assertContext, code, "Workspace directory enumeration failed"));
                }
                enumeration.interruptedCode = code;
                break;
            }
            receivedBatch = true;
            restart = false;

            std::size_t offset = 0U;
            while (true)
            {
                constexpr std::size_t k_minimumEntryBytes = offsetof(FILE_ID_BOTH_DIR_INFO, FileName);
                if (offset > buffer.size() - k_minimumEntryBytes)
                {
                    return cue::Result<NativeDirectoryEnumeration>::failure(cue::make_io_error(
                        a_assertContext, cue::IoError::IoFailure, "Workspace directory metadata is malformed"));
                }
                const auto *data = reinterpret_cast<const FILE_ID_BOTH_DIR_INFO *>(buffer.data() + offset);
                if ((data->FileNameLength % sizeof(wchar_t)) != 0U ||
                    data->FileNameLength > buffer.size() - offset - k_minimumEntryBytes)
                {
                    return cue::Result<NativeDirectoryEnumeration>::failure(cue::make_io_error(
                        a_assertContext, cue::IoError::IoFailure, "Workspace directory name metadata is malformed"));
                }

                const std::wstring_view name(data->FileName, data->FileNameLength / sizeof(wchar_t));
                if (name != L"." && name != L"..")
                {
                    if (enumeration.entries.size() == a_maximumEntries)
                    {
                        return cue::Result<NativeDirectoryEnumeration>::failure(
                            cue::make_io_error(a_assertContext, cue::IoError::CapacityExceeded,
                                               "Workspace listing entry limit was exceeded"));
                    }
                    if (name.size() >
                        (std::numeric_limits<std::size_t>::max() - sizeof(NativeDirectoryEntry)) / sizeof(wchar_t))
                    {
                        return cue::Result<NativeDirectoryEnumeration>::failure(
                            cue::make_io_error(a_assertContext, cue::IoError::CapacityExceeded,
                                               "Workspace native listing metadata size overflowed"));
                    }
                    const std::size_t entryBytes = sizeof(NativeDirectoryEntry) + name.size() * sizeof(wchar_t);
                    if (entryBytes > a_maximumMetadataBytes - metadataBytes)
                    {
                        return cue::Result<NativeDirectoryEnumeration>::failure(
                            cue::make_io_error(a_assertContext, cue::IoError::CapacityExceeded,
                                               "Workspace native listing metadata limit was exceeded"));
                    }
                    enumeration.entries.push_back(
                        NativeDirectoryEntry{std::wstring(name), data->FileAttributes, data->FileId});
                    metadataBytes += entryBytes;
                }

                if (data->NextEntryOffset == 0U)
                {
                    break;
                }
                if (data->NextEntryOffset < k_minimumEntryBytes || data->NextEntryOffset > buffer.size() - offset)
                {
                    return cue::Result<NativeDirectoryEnumeration>::failure(cue::make_io_error(
                        a_assertContext, cue::IoError::IoFailure, "Workspace directory entry chain is malformed"));
                }
                offset += data->NextEntryOffset;
            }
        }
    }
    catch (...)
    {
        terminate_allocation(a_assertContext);
    }
    return cue::Result<NativeDirectoryEnumeration>::success(std::move(enumeration));
}

/// @brief File IDが同じDirectory Entry Objectを示すか判定する
[[nodiscard]] bool same_file_id(const LARGE_INTEGER &a_left, const LARGE_INTEGER &a_right) noexcept
{
    return a_left.QuadPart == a_right.QuadPart;
}

/// @brief Entryを操作不能へ変更し、古いLocatorとMetadataを公開しない
void reject_stale_entry(cue::WorkspaceEntry &a_entry, cue::WorkspaceDiagnosticCode a_code,
                        const cue::AssertContext &a_assertContext) noexcept
{
    copy_display_sort_key(a_entry, a_assertContext);
    a_entry.locator.reset();
    a_entry.type = cue::WorkspaceEntryType::UnsupportedEntry;
    a_entry.byteSize = 0U;
    a_entry.rejection = a_code;
}

/// @brief Windows Root境界内だけを列挙するWorkspace Adapter
class WindowsWorkspaceFilesystem final : public cue::WorkspaceFilesystem
{
  public:
    /// @brief 検証済みRoot Path、Handle、Identityを所有する
    WindowsWorkspaceFilesystem(const cue::AssertContext &a_assertContext, std::wstring a_rootPath,
                               UniqueHandle a_rootHandle, RootIdentity a_identity,
                               std::size_t a_maxBoundPathCharacters) noexcept
        : cue::WorkspaceFilesystem(a_maxBoundPathCharacters), m_assertContext(a_assertContext),
          m_rootPath(std::move(a_rootPath)), m_rootHandle(std::move(a_rootHandle)), m_identity(a_identity)
    {
    }
    /// @brief Native Rootの一意所有を保つためCopy構築を禁止する
    WindowsWorkspaceFilesystem(const WindowsWorkspaceFilesystem &) = delete;
    /// @brief Native Rootの一意所有を保つためCopy代入を禁止する
    WindowsWorkspaceFilesystem &operator=(const WindowsWorkspaceFilesystem &) = delete;
    /// @brief Native Root所有権を移動する
    WindowsWorkspaceFilesystem(WindowsWorkspaceFilesystem &&) noexcept = default;
    /// @brief Native Root所有権を移動代入する
    WindowsWorkspaceFilesystem &operator=(WindowsWorkspaceFilesystem &&) noexcept = default;
    /// @brief Root HandleとAdapter状態を解放する
    ~WindowsWorkspaceFilesystem() override = default;

    /// @brief Windows Adapterの固定Hard Limitを返す
    [[nodiscard]] cue::TraversalLimits hard_limits() const noexcept override
    {
        return k_windowsHardLimits;
    }

    /// @brief Directory直下をRoot Pin保持中に列挙する
    [[nodiscard]] cue::Result<cue::DirectorySnapshot> list_directory(const cue::WorkspaceDirectory &a_directory,
                                                                     cue::TraversalLimits a_limits) noexcept override;

  private:
    /// @brief Root PathがBinding時と同じNative Directory Objectか再検証する
    [[nodiscard]] cue::Result<void> verify_root_identity() const noexcept;

    /// @brief 対象Directoryまでの全Componentを非Reparse DirectoryとしてPinする
    [[nodiscard]] cue::Result<std::vector<UniqueHandle>> pin_directory_chain(
        const cue::WorkspaceDirectory &a_directory) const noexcept;

    /// @brief Find Data一件を検証済みPortable Entryへ変換する
    [[nodiscard]] cue::Result<cue::WorkspaceEntry> make_entry(const cue::WorkspaceDirectory &a_directory,
                                                              HANDLE a_directoryHandle,
                                                              const NativeDirectoryEntry &a_data,
                                                              std::uint64_t a_generation,
                                                              cue::DirectorySnapshot &a_snapshot) const noexcept;

    cue::AssertContext m_assertContext;
    std::wstring m_rootPath;
    UniqueHandle m_rootHandle;
    RootIdentity m_identity;
    std::uint64_t m_nextGeneration = 1U;
};

cue::Result<void> WindowsWorkspaceFilesystem::verify_root_identity() const noexcept
{
    UniqueHandle current(CreateFileW(m_rootPath.c_str(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                     FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!current.is_valid())
    {
        return cue::Result<void>::failure(cue::make_io_error(m_assertContext, cue::IoError::OutsideRoot,
                                                             "Workspace root no longer resolves to the bound root",
                                                             static_cast<std::int64_t>(GetLastError())));
    }
    BY_HANDLE_FILE_INFORMATION information{};
    if (GetFileInformationByHandle(current.get(), &information) == FALSE ||
        (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
        (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
        information.dwVolumeSerialNumber != m_identity.volumeSerial ||
        information.nFileIndexHigh != m_identity.fileIndexHigh || information.nFileIndexLow != m_identity.fileIndexLow)
    {
        return cue::Result<void>::failure(
            cue::make_io_error(m_assertContext, cue::IoError::OutsideRoot, "Workspace root identity changed"));
    }
    return cue::Result<void>::success();
}

cue::Result<std::vector<UniqueHandle>> WindowsWorkspaceFilesystem::pin_directory_chain(
    const cue::WorkspaceDirectory &a_directory) const noexcept
{
    cue::Result<void> rootIdentity = verify_root_identity();
    if (!rootIdentity)
    {
        return cue::Result<std::vector<UniqueHandle>>::failure(std::move(*rootIdentity.try_error()));
    }

    std::vector<UniqueHandle> pinned;
    try
    {
        const cue::BoundWorkspacePath *locator = a_directory.locator();
        if (locator == nullptr)
        {
            return cue::Result<std::vector<UniqueHandle>>::success(std::move(pinned));
        }

        std::string_view remaining = locator->text();
        while (!remaining.empty())
        {
            const std::size_t separator = remaining.find('/');
            const std::string_view component = remaining.substr(0U, separator);
            cue::Result<std::wstring> nativeComponent = to_utf16(component, m_assertContext);
            if (!nativeComponent)
            {
                return cue::Result<std::vector<UniqueHandle>>::failure(std::move(*nativeComponent.try_error()));
            }

            const HANDLE parentHandle = pinned.empty() ? m_rootHandle.get() : pinned.back().get();
            cue::Result<NativeDirectoryEnumeration> before =
                enumerate_directory_handle(parentHandle, k_windowsHardLimits.maxVisitedEntries,
                                           k_windowsHardLimits.maxMetadataBytes, m_assertContext);
            if (!before)
            {
                return cue::Result<std::vector<UniqueHandle>>::failure(std::move(*before.try_error()));
            }
            if (before.try_value()->interruptedCode.has_value())
            {
                return cue::Result<std::vector<UniqueHandle>>::failure(
                    make_windows_error(m_assertContext, *before.try_value()->interruptedCode,
                                       "Workspace directory chain enumeration was interrupted"));
            }

            const auto findComponent =
                [&](const NativeDirectoryEnumeration &a_enumeration) noexcept -> const NativeDirectoryEntry *
            {
                for (const NativeDirectoryEntry &entry : a_enumeration.entries)
                {
                    if (entry.name == *nativeComponent.try_value())
                    {
                        return &entry;
                    }
                }

                const NativeDirectoryEntry *caseInsensitive = nullptr;
                for (const NativeDirectoryEntry &entry : a_enumeration.entries)
                {
                    if (CompareStringOrdinal(entry.name.data(), static_cast<int>(entry.name.size()),
                                             nativeComponent.try_value()->data(),
                                             static_cast<int>(nativeComponent.try_value()->size()), TRUE) == CSTR_EQUAL)
                    {
                        if (caseInsensitive != nullptr)
                        {
                            return nullptr;
                        }
                        caseInsensitive = &entry;
                    }
                }
                return caseInsensitive;
            };

            const NativeDirectoryEntry *entry = findComponent(*before.try_value());
            if (entry == nullptr)
            {
                return cue::Result<std::vector<UniqueHandle>>::failure(cue::make_io_error(
                    m_assertContext, cue::IoError::NotFound, "Workspace directory component was not found"));
            }
            if ((entry->attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U)
            {
                return cue::Result<std::vector<UniqueHandle>>::failure(
                    cue::make_io_error(m_assertContext, cue::IoError::UnsupportedEntry,
                                       "Workspace directory chain contains a reparse point"));
            }
            if ((entry->attributes & FILE_ATTRIBUTE_DIRECTORY) == 0U)
            {
                return cue::Result<std::vector<UniqueHandle>>::failure(cue::make_io_error(
                    m_assertContext, cue::IoError::TypeMismatch, "Workspace directory chain contains a file"));
            }

            const LARGE_INTEGER expectedId = entry->fileId;
            const DWORD expectedAttributes = entry->attributes;
            FILE_ID_DESCRIPTOR descriptor{};
            descriptor.dwSize = sizeof(descriptor);
            descriptor.Type = FileIdType;
            descriptor.FileId = expectedId;
            UniqueHandle handle(OpenFileById(parentHandle, &descriptor, FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
                                             FILE_SHARE_READ, nullptr,
                                             FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT));
            if (!handle.is_valid())
            {
                return cue::Result<std::vector<UniqueHandle>>::failure(
                    make_windows_error(m_assertContext, GetLastError(), "Workspace directory open failed"));
            }
            BY_HANDLE_FILE_INFORMATION information{};
            if (GetFileInformationByHandle(handle.get(), &information) == FALSE)
            {
                return cue::Result<std::vector<UniqueHandle>>::failure(
                    make_windows_error(m_assertContext, GetLastError(), "Workspace directory inspection failed"));
            }
            LARGE_INTEGER actualId{};
            actualId.HighPart = static_cast<LONG>(information.nFileIndexHigh);
            actualId.LowPart = information.nFileIndexLow;
            const DWORD typeMask = FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT;
            if (!same_file_id(actualId, expectedId) ||
                (information.dwFileAttributes & typeMask) != (expectedAttributes & typeMask))
            {
                return cue::Result<std::vector<UniqueHandle>>::failure(cue::make_io_error(
                    m_assertContext, cue::IoError::OutsideRoot, "Workspace directory component identity changed"));
            }
            if ((information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U)
            {
                return cue::Result<std::vector<UniqueHandle>>::failure(
                    cue::make_io_error(m_assertContext, cue::IoError::UnsupportedEntry,
                                       "Workspace directory chain contains a reparse point"));
            }
            if ((information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0U)
            {
                return cue::Result<std::vector<UniqueHandle>>::failure(cue::make_io_error(
                    m_assertContext, cue::IoError::TypeMismatch, "Workspace directory chain contains a file"));
            }

            cue::Result<NativeDirectoryEnumeration> after =
                enumerate_directory_handle(parentHandle, k_windowsHardLimits.maxVisitedEntries,
                                           k_windowsHardLimits.maxMetadataBytes, m_assertContext);
            if (!after)
            {
                return cue::Result<std::vector<UniqueHandle>>::failure(std::move(*after.try_error()));
            }
            if (after.try_value()->interruptedCode.has_value())
            {
                return cue::Result<std::vector<UniqueHandle>>::failure(
                    make_windows_error(m_assertContext, *after.try_value()->interruptedCode,
                                       "Workspace directory chain verification was interrupted"));
            }
            const NativeDirectoryEntry *verified = findComponent(*after.try_value());
            if (verified == nullptr || !same_file_id(verified->fileId, expectedId) ||
                (verified->attributes & typeMask) != (expectedAttributes & typeMask))
            {
                return cue::Result<std::vector<UniqueHandle>>::failure(cue::make_io_error(
                    m_assertContext, cue::IoError::OutsideRoot, "Workspace directory component identity changed"));
            }
            pinned.push_back(std::move(handle));
            if (separator == std::string_view::npos)
            {
                break;
            }
            remaining.remove_prefix(separator + 1U);
        }
    }
    catch (...)
    {
        terminate_allocation(m_assertContext);
    }
    return cue::Result<std::vector<UniqueHandle>>::success(std::move(pinned));
}

cue::Result<cue::WorkspaceEntry> WindowsWorkspaceFilesystem::make_entry(
    const cue::WorkspaceDirectory &a_directory, HANDLE a_directoryHandle, const NativeDirectoryEntry &a_data,
    std::uint64_t a_generation, cue::DirectorySnapshot &a_snapshot) const noexcept
{
    const std::wstring_view nativeName(a_data.name);
    cue::WorkspaceEntry entry;
    entry.parentGeneration = a_generation;

    cue::Result<std::string> convertedName = to_utf8(nativeName, m_assertContext);
    if (!convertedName)
    {
        entry.displayName = make_native_name_fallback(nativeName, m_assertContext);
        copy_display_sort_key(entry, m_assertContext);
        entry.type = cue::WorkspaceEntryType::UnsupportedEntry;
        entry.rejection = cue::WorkspaceDiagnosticCode::UnsupportedName;
        return cue::Result<cue::WorkspaceEntry>::success(std::move(entry));
    }
    entry.displayName = std::move(*convertedName.try_value());

    cue::Result<cue::RelativePath> childLocator = cue::RelativePath::parse(entry.displayName, m_assertContext);
    if (!childLocator)
    {
        copy_display_sort_key(entry, m_assertContext);
        entry.type = cue::WorkspaceEntryType::UnsupportedEntry;
        entry.rejection = cue::WorkspaceDiagnosticCode::UnsupportedName;
        return cue::Result<cue::WorkspaceEntry>::success(std::move(entry));
    }

    FILE_ID_DESCRIPTOR descriptor{};
    descriptor.dwSize = sizeof(descriptor);
    descriptor.Type = FileIdType;
    descriptor.FileId = a_data.fileId;
    UniqueHandle child(OpenFileById(a_directoryHandle, &descriptor, FILE_READ_ATTRIBUTES,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                    FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS));
    if (!child.is_valid())
    {
        const DWORD code = GetLastError();
        cue::WorkspaceDiagnosticCode diagnosticCode = cue::WorkspaceDiagnosticCode::EnumerationFailed;
        if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND)
        {
            diagnosticCode = cue::WorkspaceDiagnosticCode::EntryDisappeared;
        }
        else if (classify_windows_error(code) == cue::IoError::PermissionDenied)
        {
            diagnosticCode = cue::WorkspaceDiagnosticCode::PermissionDenied;
        }
        copy_display_sort_key(entry, m_assertContext);
        entry.type = cue::WorkspaceEntryType::UnsupportedEntry;
        entry.rejection = diagnosticCode;
        a_snapshot.state = cue::WorkspaceSnapshotState::RescanRequired;
        append_diagnostic(a_snapshot, diagnosticCode, entry.displayName, static_cast<std::int64_t>(code),
                          m_assertContext);
        return cue::Result<cue::WorkspaceEntry>::success(std::move(entry));
    }

    BY_HANDLE_FILE_INFORMATION information{};
    if (GetFileInformationByHandle(child.get(), &information) == FALSE)
    {
        const DWORD code = GetLastError();
        copy_display_sort_key(entry, m_assertContext);
        entry.type = cue::WorkspaceEntryType::UnsupportedEntry;
        entry.rejection = cue::WorkspaceDiagnosticCode::EnumerationFailed;
        a_snapshot.state = cue::WorkspaceSnapshotState::RescanRequired;
        append_diagnostic(a_snapshot, cue::WorkspaceDiagnosticCode::EnumerationFailed, entry.displayName,
                          static_cast<std::int64_t>(code), m_assertContext);
        return cue::Result<cue::WorkspaceEntry>::success(std::move(entry));
    }
    const bool findDirectory = (a_data.attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    const bool findReparse = (a_data.attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    const bool actualDirectory = (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    const bool actualReparse = (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    if (findDirectory != actualDirectory || findReparse != actualReparse)
    {
        const cue::WorkspaceDiagnosticCode diagnosticCode =
            actualReparse ? cue::WorkspaceDiagnosticCode::ReparsePoint : cue::WorkspaceDiagnosticCode::TypeChanged;
        copy_display_sort_key(entry, m_assertContext);
        entry.type = cue::WorkspaceEntryType::UnsupportedEntry;
        entry.rejection = diagnosticCode;
        a_snapshot.state = cue::WorkspaceSnapshotState::RescanRequired;
        append_diagnostic(a_snapshot, diagnosticCode, entry.displayName, 0, m_assertContext);
        return cue::Result<cue::WorkspaceEntry>::success(std::move(entry));
    }
    if (actualReparse)
    {
        copy_display_sort_key(entry, m_assertContext);
        entry.type = cue::WorkspaceEntryType::UnsupportedEntry;
        entry.rejection = cue::WorkspaceDiagnosticCode::ReparsePoint;
        return cue::Result<cue::WorkspaceEntry>::success(std::move(entry));
    }

    entry.sortKey = childLocator.try_value()->comparison_key(m_assertContext);
    cue::Result<cue::BoundWorkspacePath> locator =
        append_path(a_directory, std::move(*childLocator.try_value()), m_assertContext);
    if (!locator)
    {
        return cue::Result<cue::WorkspaceEntry>::failure(std::move(*locator.try_error()));
    }
    entry.locator = std::move(*locator.try_value());
    entry.type = actualDirectory ? cue::WorkspaceEntryType::Directory : cue::WorkspaceEntryType::RegularFile;
    if (!actualDirectory)
    {
        ULARGE_INTEGER size{};
        size.HighPart = information.nFileSizeHigh;
        size.LowPart = information.nFileSizeLow;
        entry.byteSize = size.QuadPart;
    }
    return cue::Result<cue::WorkspaceEntry>::success(std::move(entry));
}

cue::Result<cue::DirectorySnapshot> WindowsWorkspaceFilesystem::list_directory(
    const cue::WorkspaceDirectory &a_directory, cue::TraversalLimits a_limits) noexcept
{
    if (!owns_directory(a_directory))
    {
        return cue::Result<cue::DirectorySnapshot>::failure(cue::make_io_error(
            m_assertContext, cue::IoError::OutsideRoot, "Workspace directory belongs to another root binding"));
    }
    if (!a_limits.is_valid())
    {
        return cue::Result<cue::DirectorySnapshot>::failure(cue::make_io_error(
            m_assertContext, cue::IoError::CapacityExceeded, "Workspace listing limits must all be non-zero"));
    }
    if (m_nextGeneration == std::numeric_limits<std::uint64_t>::max())
    {
        return cue::Result<cue::DirectorySnapshot>::failure(cue::make_io_error(
            m_assertContext, cue::IoError::CapacityExceeded, "Workspace snapshot generation was exhausted"));
    }

    cue::DirectorySnapshot snapshot;
    snapshot.generation = m_nextGeneration++;
    cue::Result<std::vector<UniqueHandle>> pinned = pin_directory_chain(a_directory);
    if (!pinned)
    {
        return cue::Result<cue::DirectorySnapshot>::failure(std::move(*pinned.try_error()));
    }

    const HANDLE directoryHandle = pinned.try_value()->empty() ? m_rootHandle.get() : pinned.try_value()->back().get();
    std::size_t metadataBytes = 0U;
    const std::size_t maximumEntries =
        std::min({a_limits.maxVisitedEntries, a_limits.maxResults, k_windowsHardLimits.maxVisitedEntries,
                  k_windowsHardLimits.maxResults});
    const std::size_t maximumMetadata = std::min(a_limits.maxMetadataBytes, k_windowsHardLimits.maxMetadataBytes);

    cue::Result<NativeDirectoryEnumeration> initial =
        enumerate_directory_handle(directoryHandle, maximumEntries, maximumMetadata, m_assertContext);
    if (!initial)
    {
        return cue::Result<cue::DirectorySnapshot>::failure(std::move(*initial.try_error()));
    }

    const auto append_bounded_diagnostic = [&](cue::WorkspaceDiagnosticCode a_code, std::string_view a_displayName,
                                               std::int64_t a_nativeCode) noexcept -> cue::Result<void>
    {
        if (a_displayName.size() > std::numeric_limits<std::size_t>::max() - sizeof(cue::WorkspaceDiagnostic))
        {
            return cue::Result<void>::failure(cue::make_io_error(m_assertContext, cue::IoError::CapacityExceeded,
                                                                 "Workspace listing metadata size overflowed"));
        }
        const std::size_t bytes = sizeof(cue::WorkspaceDiagnostic) + a_displayName.size();
        if (bytes > maximumMetadata - metadataBytes)
        {
            return cue::Result<void>::failure(cue::make_io_error(m_assertContext, cue::IoError::CapacityExceeded,
                                                                 "Workspace listing metadata limit was exceeded"));
        }
        append_diagnostic(snapshot, a_code, a_displayName, a_nativeCode, m_assertContext);
        metadataBytes += bytes;
        return cue::Result<void>::success();
    };

    const auto reject_unverified_entries = [&]() noexcept
    {
        for (cue::WorkspaceEntry &entry : snapshot.entries)
        {
            if (entry.is_operable())
            {
                reject_stale_entry(entry, cue::WorkspaceDiagnosticCode::EnumerationFailed, m_assertContext);
            }
        }
    };

    for (const NativeDirectoryEntry &nativeEntry : initial.try_value()->entries)
    {
        const std::size_t diagnosticCount = snapshot.diagnostics.size();
        cue::Result<cue::WorkspaceEntry> converted =
            make_entry(a_directory, directoryHandle, nativeEntry, snapshot.generation, snapshot);
        if (!converted)
        {
            return cue::Result<cue::DirectorySnapshot>::failure(std::move(*converted.try_error()));
        }
        for (std::size_t index = diagnosticCount; index < snapshot.diagnostics.size(); ++index)
        {
            const std::size_t diagnosticBytes = metadata_bytes(snapshot.diagnostics[index]);
            if (diagnosticBytes > maximumMetadata - metadataBytes)
            {
                return cue::Result<cue::DirectorySnapshot>::failure(cue::make_io_error(
                    m_assertContext, cue::IoError::CapacityExceeded, "Workspace listing metadata limit was exceeded"));
            }
            metadataBytes += diagnosticBytes;
        }
        const std::size_t entryBytes = metadata_bytes(*converted.try_value());
        if (entryBytes > maximumMetadata - metadataBytes)
        {
            return cue::Result<cue::DirectorySnapshot>::failure(cue::make_io_error(
                m_assertContext, cue::IoError::CapacityExceeded, "Workspace listing metadata limit was exceeded"));
        }
        metadataBytes += entryBytes;
        snapshot.entries.push_back(std::move(*converted.try_value()));
    }

    if (initial.try_value()->interruptedCode.has_value())
    {
        snapshot.state = cue::WorkspaceSnapshotState::RescanRequired;
        reject_unverified_entries();
        cue::Result<void> recorded =
            append_bounded_diagnostic(cue::WorkspaceDiagnosticCode::EnumerationFailed, {},
                                      static_cast<std::int64_t>(*initial.try_value()->interruptedCode));
        if (!recorded)
        {
            return cue::Result<cue::DirectorySnapshot>::failure(std::move(*recorded.try_error()));
        }
    }
    else
    {
        cue::Result<NativeDirectoryEnumeration> verification =
            enumerate_directory_handle(directoryHandle, maximumEntries, maximumMetadata, m_assertContext);
        if (!verification)
        {
            const cue::ErrorCode &rootCode = verification.try_error()->root_code();
            const bool isCapacityFailure =
                rootCode.domain() == "Cue.IO" &&
                rootCode.value() == static_cast<std::int64_t>(cue::IoError::CapacityExceeded);
            const bool isPortableFailure = verification.try_error()->try_native_error() == nullptr;
            if (isCapacityFailure || isPortableFailure)
            {
                return cue::Result<cue::DirectorySnapshot>::failure(std::move(*verification.try_error()));
            }
            std::int64_t nativeCode = 0;
            if (const cue::NativeError *native = verification.try_error()->try_native_error(); native != nullptr)
            {
                nativeCode = native->value();
            }
            snapshot.state = cue::WorkspaceSnapshotState::RescanRequired;
            reject_unverified_entries();
            cue::Result<void> recorded =
                append_bounded_diagnostic(cue::WorkspaceDiagnosticCode::EnumerationFailed, {}, nativeCode);
            if (!recorded)
            {
                return cue::Result<cue::DirectorySnapshot>::failure(std::move(*recorded.try_error()));
            }
        }
        else if (verification.try_value()->interruptedCode.has_value())
        {
            snapshot.state = cue::WorkspaceSnapshotState::RescanRequired;
            reject_unverified_entries();
            cue::Result<void> recorded =
                append_bounded_diagnostic(cue::WorkspaceDiagnosticCode::EnumerationFailed, {},
                                          static_cast<std::int64_t>(*verification.try_value()->interruptedCode));
            if (!recorded)
            {
                return cue::Result<cue::DirectorySnapshot>::failure(std::move(*recorded.try_error()));
            }
        }
        else
        {
            try
            {
                auto &currentEntries = verification.try_value()->entries;
                std::sort(currentEntries.begin(), currentEntries.end(),
                          [](const NativeDirectoryEntry &a_left, const NativeDirectoryEntry &a_right) noexcept
                          { return a_left.name < a_right.name; });

                std::vector<std::wstring_view> initialNames;
                initialNames.reserve(initial.try_value()->entries.size());
                for (const NativeDirectoryEntry &entry : initial.try_value()->entries)
                {
                    initialNames.push_back(entry.name);
                }
                std::sort(initialNames.begin(), initialNames.end());

                for (std::size_t index = 0U; index < initial.try_value()->entries.size(); ++index)
                {
                    const NativeDirectoryEntry &before = initial.try_value()->entries[index];
                    const auto current =
                        std::lower_bound(currentEntries.begin(), currentEntries.end(), before.name,
                                         [](const NativeDirectoryEntry &a_entry, const std::wstring &a_name) noexcept
                                         { return a_entry.name < a_name; });

                    std::optional<cue::WorkspaceDiagnosticCode> diagnostic;
                    if (current == currentEntries.end() || current->name != before.name)
                    {
                        diagnostic = cue::WorkspaceDiagnosticCode::EntryDisappeared;
                    }
                    else
                    {
                        const DWORD typeMask = FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT;
                        if (!same_file_id(current->fileId, before.fileId))
                        {
                            diagnostic = (current->attributes & typeMask) != (before.attributes & typeMask)
                                             ? cue::WorkspaceDiagnosticCode::TypeChanged
                                             : cue::WorkspaceDiagnosticCode::EntryDisappeared;
                        }
                        else if ((current->attributes & typeMask) != (before.attributes & typeMask))
                        {
                            diagnostic = (current->attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U
                                             ? cue::WorkspaceDiagnosticCode::ReparsePoint
                                             : cue::WorkspaceDiagnosticCode::TypeChanged;
                        }
                    }

                    if (diagnostic.has_value())
                    {
                        snapshot.state = cue::WorkspaceSnapshotState::RescanRequired;
                        reject_stale_entry(snapshot.entries[index], *diagnostic, m_assertContext);
                        cue::Result<void> recorded =
                            append_bounded_diagnostic(*diagnostic, snapshot.entries[index].displayName, 0);
                        if (!recorded)
                        {
                            return cue::Result<cue::DirectorySnapshot>::failure(std::move(*recorded.try_error()));
                        }
                    }
                }

                for (const NativeDirectoryEntry &current : currentEntries)
                {
                    if (!std::binary_search(initialNames.begin(), initialNames.end(), std::wstring_view(current.name)))
                    {
                        cue::Result<std::string> displayName = to_utf8(current.name, m_assertContext);
                        std::string fallback;
                        std::string_view diagnosticName;
                        if (displayName)
                        {
                            diagnosticName = *displayName.try_value();
                        }
                        else
                        {
                            fallback = make_native_name_fallback(current.name, m_assertContext);
                            diagnosticName = fallback;
                        }
                        snapshot.state = cue::WorkspaceSnapshotState::RescanRequired;
                        cue::Result<void> recorded = append_bounded_diagnostic(
                            cue::WorkspaceDiagnosticCode::EnumerationFailed, diagnosticName, 0);
                        if (!recorded)
                        {
                            return cue::Result<cue::DirectorySnapshot>::failure(std::move(*recorded.try_error()));
                        }
                    }
                }
            }
            catch (...)
            {
                terminate_allocation(m_assertContext);
            }
        }
    }

    cue::sort_workspace_entries(snapshot.entries);
    return cue::Result<cue::DirectorySnapshot>::success(std::move(snapshot));
}
} // namespace

namespace cue
{
Result<std::unique_ptr<WorkspaceFilesystem>> create_windows_workspace_filesystem(
    std::string_view a_rootPath, const AssertContext &a_assertContext) noexcept
{
    Result<std::wstring> converted = to_utf16(a_rootPath, a_assertContext);
    if (!converted)
    {
        return Result<std::unique_ptr<WorkspaceFilesystem>>::failure(std::move(*converted.try_error()));
    }
    const std::wstring_view input(*converted.try_value());
    if (input.find(L'\0') != std::wstring_view::npos)
    {
        return Result<std::unique_ptr<WorkspaceFilesystem>>::failure(
            make_io_error(a_assertContext, IoError::InvalidPath, "Workspace root path contains NUL"));
    }
    const bool extendedPrefix = input.starts_with(L"\\\\?\\");
    if (is_unc_path(input))
    {
        return Result<std::unique_ptr<WorkspaceFilesystem>>::failure(
            make_io_error(a_assertContext, IoError::UnsupportedEntry, "UNC workspace roots are not supported"));
    }
    const std::size_t driveOffset = extendedPrefix ? 4U : 0U;
    const bool driveAbsolute = input.size() >= driveOffset + 3U && std::iswalpha(input[driveOffset]) != 0 &&
                               input[driveOffset + 1U] == L':' &&
                               (input[driveOffset + 2U] == L'\\' || input[driveOffset + 2U] == L'/');
    if (!driveAbsolute)
    {
        return Result<std::unique_ptr<WorkspaceFilesystem>>::failure(
            make_io_error(a_assertContext, IoError::InvalidPath, "Workspace root path must be local-drive absolute"));
    }
    const std::array<wchar_t, 4U> driveRoot{input[driveOffset], L':', L'\\', L'\0'};
    if (GetDriveTypeW(driveRoot.data()) == DRIVE_REMOTE)
    {
        return Result<std::unique_ptr<WorkspaceFilesystem>>::failure(make_io_error(
            a_assertContext, IoError::UnsupportedEntry, "Remote drive workspace roots are not supported"));
    }

    const DWORD required = GetFullPathNameW(converted.try_value()->c_str(), 0U, nullptr, nullptr);
    if (required == 0U)
    {
        return Result<std::unique_ptr<WorkspaceFilesystem>>::failure(
            make_windows_error(a_assertContext, GetLastError(), "Workspace root absolute path resolution failed"));
    }
    std::wstring absolute;
    try
    {
        absolute.resize(required);
    }
    catch (...)
    {
        terminate_allocation(a_assertContext);
    }
    const DWORD written = GetFullPathNameW(converted.try_value()->c_str(), required, absolute.data(), nullptr);
    if (written == 0U || written >= required)
    {
        return Result<std::unique_ptr<WorkspaceFilesystem>>::failure(
            make_windows_error(a_assertContext, GetLastError(), "Workspace root absolute path resolution failed"));
    }
    absolute.resize(written);
    const std::size_t rootLength = local_drive_root_length(absolute);
    while (absolute.size() > rootLength && (absolute.back() == L'\\' || absolute.back() == L'/'))
    {
        absolute.pop_back();
    }

    Result<std::wstring> extended = make_extended_path(std::move(absolute), a_assertContext);
    if (!extended)
    {
        return Result<std::unique_ptr<WorkspaceFilesystem>>::failure(std::move(*extended.try_error()));
    }
    UniqueHandle root(CreateFileW(extended.try_value()->c_str(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                  FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!root.is_valid())
    {
        return Result<std::unique_ptr<WorkspaceFilesystem>>::failure(
            make_windows_error(a_assertContext, GetLastError(), "Workspace root open failed"));
    }
    BY_HANDLE_FILE_INFORMATION information{};
    if (GetFileInformationByHandle(root.get(), &information) == FALSE)
    {
        return Result<std::unique_ptr<WorkspaceFilesystem>>::failure(
            make_windows_error(a_assertContext, GetLastError(), "Workspace root inspection failed"));
    }
    if ((information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
    {
        return Result<std::unique_ptr<WorkspaceFilesystem>>::failure(
            make_io_error(a_assertContext, IoError::TypeMismatch, "Workspace root is not a directory"));
    }
    if ((information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
    {
        return Result<std::unique_ptr<WorkspaceFilesystem>>::failure(
            make_io_error(a_assertContext, IoError::UnsupportedEntry, "Workspace root is a reparse point"));
    }

    DWORD fileSystemFlags = 0U;
    if (GetVolumeInformationByHandleW(root.get(), nullptr, 0U, nullptr, nullptr, &fileSystemFlags, nullptr, 0U) ==
        FALSE)
    {
        return Result<std::unique_ptr<WorkspaceFilesystem>>::failure(
            make_windows_error(a_assertContext, GetLastError(), "Workspace volume capability query failed"));
    }
    if ((fileSystemFlags & FILE_SUPPORTS_OPEN_BY_FILE_ID) == 0U)
    {
        return Result<std::unique_ptr<WorkspaceFilesystem>>::failure(
            make_io_error(a_assertContext, IoError::UnsupportedEntry,
                          "Workspace volume does not support opening entries by file id"));
    }

    const DWORD finalRequired =
        GetFinalPathNameByHandleW(root.get(), nullptr, 0U, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (finalRequired == 0U)
    {
        return Result<std::unique_ptr<WorkspaceFilesystem>>::failure(
            make_windows_error(a_assertContext, GetLastError(), "Workspace root final path query failed"));
    }
    std::wstring finalPath;
    try
    {
        finalPath.resize(finalRequired);
    }
    catch (...)
    {
        terminate_allocation(a_assertContext);
    }
    const DWORD finalWritten =
        GetFinalPathNameByHandleW(root.get(), finalPath.data(), finalRequired, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (finalWritten == 0U || finalWritten >= finalRequired)
    {
        return Result<std::unique_ptr<WorkspaceFilesystem>>::failure(
            make_windows_error(a_assertContext, GetLastError(), "Workspace root final path query failed"));
    }
    finalPath.resize(finalWritten);
    if (is_unc_path(finalPath))
    {
        return Result<std::unique_ptr<WorkspaceFilesystem>>::failure(make_io_error(
            a_assertContext, IoError::UnsupportedEntry, "Resolved workspace root is on a remote filesystem"));
    }
    const std::size_t finalDriveOffset = finalPath.starts_with(L"\\\\?\\") ? 4U : 0U;
    if (finalPath.size() < finalDriveOffset + 3U || std::iswalpha(finalPath[finalDriveOffset]) == 0 ||
        finalPath[finalDriveOffset + 1U] != L':')
    {
        return Result<std::unique_ptr<WorkspaceFilesystem>>::failure(make_io_error(
            a_assertContext, IoError::UnsupportedEntry, "Resolved workspace root is not on a local drive"));
    }
    const std::array<wchar_t, 4U> finalDriveRoot{finalPath[finalDriveOffset], L':', L'\\', L'\0'};
    if (GetDriveTypeW(finalDriveRoot.data()) == DRIVE_REMOTE)
    {
        return Result<std::unique_ptr<WorkspaceFilesystem>>::failure(
            make_io_error(a_assertContext, IoError::UnsupportedEntry, "Resolved workspace root is on a remote drive"));
    }

    constexpr std::size_t k_directoryPatternCharacters = 4U;
    if (finalPath.size() > k_maxWindowsPathLength - k_directoryPatternCharacters)
    {
        return Result<std::unique_ptr<WorkspaceFilesystem>>::failure(make_io_error(
            a_assertContext, IoError::CapacityExceeded, "Workspace root leaves no capacity for directory paths"));
    }
    const std::size_t maxBoundPathCharacters = k_maxWindowsPathLength - finalPath.size() - k_directoryPatternCharacters;

    const RootIdentity identity{information.dwVolumeSerialNumber, information.nFileIndexHigh,
                                information.nFileIndexLow};
    try
    {
        std::unique_ptr<WorkspaceFilesystem> filesystem = std::make_unique<WindowsWorkspaceFilesystem>(
            a_assertContext, std::move(finalPath), std::move(root), identity, maxBoundPathCharacters);
        return Result<std::unique_ptr<WorkspaceFilesystem>>::success(std::move(filesystem));
    }
    catch (...)
    {
        terminate_allocation(a_assertContext);
    }
}
} // namespace cue

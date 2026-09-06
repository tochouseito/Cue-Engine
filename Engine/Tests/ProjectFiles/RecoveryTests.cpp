#include <Cue/Foundation/Assert.h>
#include <Cue/Foundation/Fatal.h>
#include <Cue/Foundation/Log.h>
#include <Cue/IO/Windows/WindowsWorkspaceFilesystem.h>
#include <Cue/Project/Descriptor.h>
#include <Cue/ProjectFiles/Error.h>
#include <Cue/ProjectFiles/Service.h>

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
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

class TestDirectory final
{
  public:
    /// @brief Recovery Integration Test用Project Directory Treeを作成する
    TestDirectory()
    {
        std::array<wchar_t, MAX_PATH> temporary{};
        const DWORD length = GetTempPathW(static_cast<DWORD>(temporary.size()), temporary.data());
        if (length == 0U || length >= temporary.size())
        {
            return;
        }
        static std::atomic_uint64_t sequence{0U};
        m_path = temporary.data();
        m_path += L"CueRecoveryTests-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                  std::to_wstring(GetTickCount64()) + L"-" +
                  std::to_wstring(sequence.fetch_add(1U, std::memory_order_relaxed));
        m_created = CreateDirectoryW(m_path.c_str(), nullptr) != FALSE &&
                    CreateDirectoryW(child(L"Assets").c_str(), nullptr) != FALSE &&
                    CreateDirectoryW(child(L"Assets\\Source").c_str(), nullptr) != FALSE &&
                    CreateDirectoryW(child(L"Assets\\Runtime").c_str(), nullptr) != FALSE &&
                    CreateDirectoryW(child(L"Generated").c_str(), nullptr) != FALSE &&
                    CreateDirectoryW(child(L"Saved").c_str(), nullptr) != FALSE;
    }
    /// @brief Test Directory所有権の重複を防ぐためCopy構築を禁止する
    TestDirectory(const TestDirectory &) = delete;
    /// @brief Test Directory所有権の重複を防ぐためCopy代入を禁止する
    TestDirectory &operator=(const TestDirectory &) = delete;
    /// @brief Test終了時に作成済みDirectory Treeを削除する
    ~TestDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
    }

    /// @brief Project Directory Treeが完全に作成されたか返す
    [[nodiscard]] bool is_created() const noexcept
    {
        return m_created;
    }

    /// @brief Test Root配下のNative Child Pathを返す
    [[nodiscard]] std::wstring child(std::wstring_view a_relative) const
    {
        return m_path + L"\\" + std::wstring(a_relative);
    }

    /// @brief Workspace Factoryへ渡すTest RootのUTF-8 Pathを返す
    [[nodiscard]] std::string utf8_path() const
    {
        const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, m_path.c_str(),
                                              static_cast<int>(m_path.size()), nullptr, 0, nullptr, nullptr);
        std::string result(static_cast<std::size_t>(count), '\0');
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, m_path.c_str(), static_cast<int>(m_path.size()),
                            result.data(), count, nullptr, nullptr);
        return result;
    }

  private:
    std::wstring m_path;
    bool m_created = false;
};

class SequenceOperationIdSource final : public cue::project_files::ProjectFileOperationIdSource
{
  public:
    /// @brief 決定的Operation ID列を所有するTest Sourceを構築する
    SequenceOperationIdSource(const cue::AssertContext &a_assertContext, std::vector<std::string> a_ids) noexcept
        : m_assertContext(&a_assertContext), m_ids(std::move(a_ids))
    {
    }
    /// @brief Test Sourceが所有するID列を解放する
    ~SequenceOperationIdSource() override = default;

    /// @brief 次の決定的Operation IDまたは枯渇Errorを返す
    [[nodiscard]] cue::Result<std::string> next_operation_id() noexcept override
    {
        if (m_next == m_ids.size())
        {
            return cue::Result<std::string>::failure(cue::project_files::make_project_file_error(
                *m_assertContext, cue::project_files::ProjectFileError::InvalidRequest,
                "Recovery test operation id source is exhausted"));
        }
        return cue::Result<std::string>::success(std::move(m_ids[m_next++]));
    }

  private:
    const cue::AssertContext *m_assertContext;
    std::vector<std::string> m_ids;
    std::size_t m_next = 0U;
};

/// @brief Test用Native Fileへ指定Byte列を書き込む
[[nodiscard]] bool write_file(std::wstring_view a_path, std::span<const std::byte> a_bytes) noexcept
{
    HANDLE file = CreateFileW(a_path.data(), GENERIC_WRITE, 0U, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return false;
    }
    DWORD written = 0U;
    const bool succeeded =
        WriteFile(file, a_bytes.data(), static_cast<DWORD>(a_bytes.size()), &written, nullptr) != FALSE &&
        written == a_bytes.size();
    CloseHandle(file);
    return succeeded;
}

/// @brief Test用Native Fileを小容量Byte列として読み込む
[[nodiscard]] std::vector<std::byte> read_file(std::wstring_view a_path)
{
    std::vector<std::byte> bytes;
    HANDLE file = CreateFileW(a_path.data(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return bytes;
    }
    LARGE_INTEGER size{};
    if (GetFileSizeEx(file, &size) == FALSE || size.QuadPart < 0 || size.QuadPart > 4096)
    {
        CloseHandle(file);
        return bytes;
    }
    bytes.resize(static_cast<std::size_t>(size.QuadPart));
    DWORD read = 0U;
    if (!bytes.empty() && (ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) == FALSE ||
                           read != bytes.size()))
    {
        bytes.clear();
    }
    CloseHandle(file);
    return bytes;
}

/// @brief Recovery Test用Descriptorを構築する
[[nodiscard]] cue::Result<cue::ProjectDescriptor> make_descriptor(const cue::AssertContext &a_assertContext) noexcept
{
    auto id = cue::ProjectId::parse("12345678-1234-4234-8234-123456789abc", a_assertContext);
    if (!id)
    {
        return cue::Result<cue::ProjectDescriptor>::failure(std::move(*id.try_error()));
    }
    return cue::create_blank_project_descriptor(
        *id.try_value(), "Recovery Test",
        cue::EngineCompatibility{cue::EngineVersion{1U, 0U, 0U}, cue::EngineVersion{2U, 0U, 0U}}, a_assertContext);
}

/// @brief Test RootへDescriptorを保存する
[[nodiscard]] bool write_descriptor(const TestDirectory &a_directory, const cue::ProjectDescriptor &a_descriptor,
                                    const cue::AssertContext &a_assertContext) noexcept
{
    auto serialized = cue::serialize_project_descriptor(a_descriptor, a_assertContext);
    return serialized &&
           write_file(a_directory.child(L"CueProject.json"),
                      std::as_bytes(std::span(serialized.try_value()->data(), serialized.try_value()->size())));
}

/// @brief 決定的ID列をPolymorphic Sourceへ変換する
[[nodiscard]] std::unique_ptr<cue::project_files::ProjectFileOperationIdSource> make_ids(
    const cue::AssertContext &a_assertContext, std::vector<std::string> a_ids)
{
    return std::make_unique<SequenceOperationIdSource>(a_assertContext, std::move(a_ids));
}

/// @brief 新しいWorkspaceとID SourceからProject File Serviceを構築する
[[nodiscard]] cue::Result<cue::project_files::ProjectFileService> make_service(
    const TestDirectory &a_directory, const cue::ProjectDescriptor &a_descriptor,
    const cue::AssertContext &a_assertContext, std::vector<std::string> a_ids)
{
    auto workspace = cue::create_windows_workspace_filesystem(a_directory.utf8_path(), a_assertContext);
    if (!workspace)
    {
        return cue::Result<cue::project_files::ProjectFileService>::failure(std::move(*workspace.try_error()));
    }
    return cue::project_files::ProjectFileService::create(a_descriptor, std::move(*workspace.try_value()),
                                                          make_ids(a_assertContext, std::move(a_ids)), a_assertContext);
}

/// @brief Errorが指定Project File分類を保持するか判定する
[[nodiscard]] bool has_error(const cue::Error *a_error, cue::project_files::ProjectFileError a_code) noexcept
{
    return a_error != nullptr && a_error->code().domain() == "Cue.ProjectFiles" &&
           a_error->code().value() == static_cast<std::int64_t>(a_code);
}

/// @brief File Delete、再起動Catalog、衝突、RestoreをEnd-to-Endで検証する
[[nodiscard]] bool test_file_recovery(const TestDirectory &a_directory, const cue::ProjectDescriptor &a_descriptor,
                                      const cue::AssertContext &a_assertContext)
{
    constexpr std::string_view k_operationId = "11111111-1111-4111-8111-111111111111";
    constexpr cue::TraversalLimits k_traversal{16U, 1024U, 1024U, 1024U * 1024U};
    constexpr cue::ContentVerificationLimits k_content{1024U * 1024U, 4U * 1024U * 1024U};
    const std::array<std::byte, 5U> originalBytes{std::byte{'c'}, std::byte{'u'}, std::byte{'e'}, std::byte{'!'},
                                                  std::byte{'!'}};
    const std::array<std::byte, 4U> collisionBytes{std::byte{'k'}, std::byte{'e'}, std::byte{'e'}, std::byte{'p'}};
    if (!write_file(a_directory.child(L"Assets\\Source\\Recover.bin"), originalBytes))
    {
        std::fprintf(stderr, "file recovery: source setup failed\n");
        return false;
    }
    auto service = make_service(a_directory, a_descriptor, a_assertContext, {std::string(k_operationId)});
    auto source = cue::RelativePath::parse("Recover.bin", a_assertContext);
    if (!service || !source)
    {
        std::fprintf(stderr, "file recovery: service setup failed\n");
        return false;
    }
    auto deleted = service.try_value()->delete_entry(cue::project_files::ProjectFileArea::SourceAssets,
                                                     std::move(*source.try_value()), k_traversal, k_content);
    if (!deleted ||
        deleted.try_value()->outcome() !=
            cue::project_files::ProjectFileOperationOutcome::CommittedButDurabilityUnknown ||
        GetFileAttributesW(a_directory.child(L"Assets\\Source\\Recover.bin").c_str()) != INVALID_FILE_ATTRIBUTES ||
        read_file(a_directory.child(L"Saved\\Editor\\Trash\\11111111-1111-4111-8111-111111111111\\Payload")) !=
            std::vector<std::byte>(originalBytes.begin(), originalBytes.end()) ||
        service.try_value()->recovery_entries().size() != 1U)
    {
        std::fprintf(stderr,
                     "file recovery: delete state failed; result=%d outcome=%d stage=%d code=%lld root=%.*s:%lld "
                     "native=%lld entries=%zu\n",
                     deleted ? 1 : 0, deleted ? static_cast<int>(deleted.try_value()->outcome()) : -1,
                     deleted ? static_cast<int>(deleted.try_value()->stage()) : -1,
                     deleted && deleted.try_value()->try_primary_error() != nullptr
                         ? static_cast<long long>(deleted.try_value()->try_primary_error()->code().value())
                         : -1LL,
                     deleted && deleted.try_value()->try_primary_error() != nullptr
                         ? static_cast<int>(deleted.try_value()->try_primary_error()->root_code().domain().size())
                         : 0,
                     deleted && deleted.try_value()->try_primary_error() != nullptr
                         ? deleted.try_value()->try_primary_error()->root_code().domain().data()
                         : "",
                     deleted && deleted.try_value()->try_primary_error() != nullptr
                         ? static_cast<long long>(deleted.try_value()->try_primary_error()->root_code().value())
                         : -1LL,
                     deleted && deleted.try_value()->try_primary_error() != nullptr &&
                             deleted.try_value()->try_primary_error()->try_native_error() != nullptr
                         ? static_cast<long long>(deleted.try_value()->try_primary_error()->try_native_error()->value())
                         : -1LL,
                     service.try_value()->recovery_entries().size());
        if (deleted && deleted.try_value()->try_primary_error() != nullptr)
        {
            const cue::Error &error = *deleted.try_value()->try_primary_error();
            std::fprintf(stderr, "summary=%.*s causes=%zu\n", static_cast<int>(error.summary().size()),
                         error.summary().data(), error.causes().size());
            for (const cue::ErrorCause &cause : error.causes())
            {
                std::fprintf(
                    stderr, "cause=%.*s code=%.*s:%lld native=%lld\n", static_cast<int>(cause.summary().size()),
                    cause.summary().data(), static_cast<int>(cause.code().domain().size()),
                    cause.code().domain().data(), static_cast<long long>(cause.code().value()),
                    cause.try_native_error() != nullptr ? static_cast<long long>(cause.try_native_error()->value())
                                                        : -1LL);
            }
        }
        return false;
    }

    auto reopened = make_service(a_directory, a_descriptor, a_assertContext, {});
    if (!reopened || !reopened.try_value()->refresh_recovery_catalog(k_traversal, k_content) ||
        reopened.try_value()->recovery_entries().size() != 1U ||
        reopened.try_value()->recovery_entries()[0U].operationId != k_operationId ||
        reopened.try_value()->recovery_entries()[0U].originalPath != "Recover.bin" ||
        reopened.try_value()->recovery_entries()[0U].state != cue::project_files::RecoveryEntryState::Recoverable)
    {
        std::fprintf(stderr, "file recovery: catalog refresh failed; service=%d entries=%zu diagnostics=%zu\n",
                     reopened ? 1 : 0, reopened ? reopened.try_value()->recovery_entries().size() : 0U,
                     reopened ? reopened.try_value()->recovery_diagnostics().size() : 0U);
        return false;
    }
    if (!write_file(a_directory.child(L"Assets\\Source\\Recover.bin"), collisionBytes))
    {
        std::fprintf(stderr, "file recovery: collision setup failed\n");
        return false;
    }
    auto conflict = reopened.try_value()->restore(k_operationId, k_traversal, k_content);
    if (!conflict || conflict.try_value()->outcome() != cue::project_files::ProjectFileOperationOutcome::NotCommitted ||
        !has_error(conflict.try_value()->try_primary_error(), cue::project_files::ProjectFileError::Conflict) ||
        read_file(a_directory.child(L"Assets\\Source\\Recover.bin")) !=
            std::vector<std::byte>(collisionBytes.begin(), collisionBytes.end()) ||
        read_file(a_directory.child(L"Saved\\Editor\\Trash\\11111111-1111-4111-8111-111111111111\\Payload")) !=
            std::vector<std::byte>(originalBytes.begin(), originalBytes.end()) ||
        DeleteFileW(a_directory.child(L"Assets\\Source\\Recover.bin").c_str()) == FALSE)
    {
        std::fprintf(stderr, "file recovery: restore conflict failed; result=%d outcome=%d code=%lld\n",
                     conflict ? 1 : 0, conflict ? static_cast<int>(conflict.try_value()->outcome()) : -1,
                     conflict && conflict.try_value()->try_primary_error() != nullptr
                         ? static_cast<long long>(conflict.try_value()->try_primary_error()->code().value())
                         : -1LL);
        return false;
    }
    auto restored = reopened.try_value()->restore(k_operationId, k_traversal, k_content);
    const bool succeeded =
        restored &&
        restored.try_value()->outcome() ==
            cue::project_files::ProjectFileOperationOutcome::CommittedButDurabilityUnknown &&
        read_file(a_directory.child(L"Assets\\Source\\Recover.bin")) ==
            std::vector<std::byte>(originalBytes.begin(), originalBytes.end()) &&
        GetFileAttributesW(a_directory.child(L"Saved\\Editor\\Trash\\11111111-1111-4111-8111-111111111111").c_str()) ==
            INVALID_FILE_ATTRIBUTES;
    if (!succeeded)
    {
        const std::vector<std::byte> observed = read_file(a_directory.child(L"Assets\\Source\\Recover.bin"));
        const DWORD containerAttributes = GetFileAttributesW(
            a_directory.child(L"Saved\\Editor\\Trash\\11111111-1111-4111-8111-111111111111").c_str());
        const DWORD recordAttributes = GetFileAttributesW(
            a_directory.child(L"Saved\\Editor\\Trash\\11111111-1111-4111-8111-111111111111\\Record.cuetrash").c_str());
        std::fprintf(stderr,
                     "file recovery: final restore failed; result=%d outcome=%d code=%lld content=%d container=%lu "
                     "record=%lu secondary=%zu\n",
                     restored ? 1 : 0, restored ? static_cast<int>(restored.try_value()->outcome()) : -1,
                     restored && restored.try_value()->try_primary_error() != nullptr
                         ? static_cast<long long>(restored.try_value()->try_primary_error()->code().value())
                         : -1LL,
                     observed == std::vector<std::byte>(originalBytes.begin(), originalBytes.end()) ? 1 : 0,
                     static_cast<unsigned long>(containerAttributes), static_cast<unsigned long>(recordAttributes),
                     restored ? restored.try_value()->secondary_diagnostics().size() : 0U);
        if (restored)
        {
            for (const cue::Error &diagnostic : restored.try_value()->secondary_diagnostics())
            {
                std::fprintf(
                    stderr, "cleanup=%.*s root=%.*s:%lld native=%lld\n", static_cast<int>(diagnostic.summary().size()),
                    diagnostic.summary().data(), static_cast<int>(diagnostic.root_code().domain().size()),
                    diagnostic.root_code().domain().data(), static_cast<long long>(diagnostic.root_code().value()),
                    diagnostic.try_native_error() != nullptr
                        ? static_cast<long long>(diagnostic.try_native_error()->value())
                        : -1LL);
            }
        }
    }
    return succeeded;
}

/// @brief Directory Manifestと保護Area Delete拒否を検証する
[[nodiscard]] bool test_directory_recovery(const TestDirectory &a_directory, const cue::ProjectDescriptor &a_descriptor,
                                           const cue::AssertContext &a_assertContext)
{
    constexpr std::string_view k_operationId = "22222222-2222-4222-8222-222222222222";
    constexpr cue::TraversalLimits k_traversal{16U, 1024U, 1024U, 1024U * 1024U};
    constexpr cue::ContentVerificationLimits k_content{1024U * 1024U, 4U * 1024U * 1024U};
    const std::array<std::byte, 3U> content{std::byte{'d'}, std::byte{'i'}, std::byte{'r'}};
    if (CreateDirectoryW(a_directory.child(L"Assets\\Source\\Folder").c_str(), nullptr) == FALSE ||
        CreateDirectoryW(a_directory.child(L"Assets\\Source\\Folder\\Nested").c_str(), nullptr) == FALSE ||
        !write_file(a_directory.child(L"Assets\\Source\\Folder\\Nested\\Data.bin"), content))
    {
        return false;
    }
    auto service = make_service(a_directory, a_descriptor, a_assertContext,
                                {std::string(k_operationId), "33333333-3333-4333-8333-333333333333"});
    auto folder = cue::RelativePath::parse("Folder", a_assertContext);
    if (!service || !folder)
    {
        return false;
    }
    auto deleted = service.try_value()->delete_entry(cue::project_files::ProjectFileArea::SourceAssets,
                                                     std::move(*folder.try_value()), k_traversal, k_content);
    if (!deleted || service.try_value()->recovery_entries().size() != 1U ||
        service.try_value()->recovery_entries()[0U].descendantCount != 2U)
    {
        return false;
    }
    auto restored = service.try_value()->restore(k_operationId, k_traversal, k_content);
    if (!restored || read_file(a_directory.child(L"Assets\\Source\\Folder\\Nested\\Data.bin")) !=
                         std::vector<std::byte>(content.begin(), content.end()))
    {
        return false;
    }
    auto protectedPath = cue::RelativePath::parse("Protected.bin", a_assertContext);
    if (!protectedPath)
    {
        return false;
    }
    auto protectedResult =
        service.try_value()->delete_entry(cue::project_files::ProjectFileArea::RuntimeAssets,
                                          std::move(*protectedPath.try_value()), k_traversal, k_content);
    return protectedResult &&
           protectedResult.try_value()->outcome() == cue::project_files::ProjectFileOperationOutcome::NotCommitted &&
           has_error(protectedResult.try_value()->try_primary_error(),
                     cue::project_files::ProjectFileError::ProtectedEntry);
}

/// @brief Root表現と複数Hard LinkをRecoverable Delete対象から拒否する
[[nodiscard]] bool test_rejected_delete_targets(const TestDirectory &a_directory,
                                                const cue::ProjectDescriptor &a_descriptor,
                                                const cue::AssertContext &a_assertContext)
{
    constexpr std::string_view k_operationId = "44444444-4444-4444-8444-444444444444";
    constexpr cue::TraversalLimits k_traversal{16U, 1024U, 1024U, 1024U * 1024U};
    constexpr cue::ContentVerificationLimits k_content{1024U * 1024U, 4U * 1024U * 1024U};
    const std::array<std::byte, 4U> content{std::byte{'l'}, std::byte{'i'}, std::byte{'n'}, std::byte{'k'}};
    if (cue::RelativePath::parse({}, a_assertContext) || cue::RelativePath::parse("../Outside.bin", a_assertContext) ||
        !write_file(a_directory.child(L"Assets\\Source\\Linked.bin"), content) ||
        CreateHardLinkW(a_directory.child(L"Assets\\Source\\Alias.bin").c_str(),
                        a_directory.child(L"Assets\\Source\\Linked.bin").c_str(), nullptr) == FALSE)
    {
        return false;
    }

    auto service = make_service(a_directory, a_descriptor, a_assertContext, {std::string(k_operationId)});
    auto source = cue::RelativePath::parse("Linked.bin", a_assertContext);
    if (!service || !source)
    {
        return false;
    }
    auto result = service.try_value()->delete_entry(cue::project_files::ProjectFileArea::SourceAssets,
                                                    std::move(*source.try_value()), k_traversal, k_content);
    const bool passed =
        result && result.try_value()->outcome() == cue::project_files::ProjectFileOperationOutcome::NotCommitted &&
        has_error(result.try_value()->try_primary_error(), cue::project_files::ProjectFileError::InvalidRequest) &&
        read_file(a_directory.child(L"Assets\\Source\\Linked.bin")) ==
            std::vector<std::byte>(content.begin(), content.end()) &&
        read_file(a_directory.child(L"Assets\\Source\\Alias.bin")) ==
            std::vector<std::byte>(content.begin(), content.end()) &&
        GetFileAttributesW(a_directory.child(L"Saved\\Editor").c_str()) == INVALID_FILE_ATTRIBUTES;
    if (!passed)
    {
        std::fprintf(stderr,
                     "rejected delete: result=%d outcome=%d code=%lld root=%.*s:%lld native=%lld linked=%zu alias=%zu "
                     "editor=%lu\n",
                     result ? 1 : 0, result ? static_cast<int>(result.try_value()->outcome()) : -1,
                     result && result.try_value()->try_primary_error() != nullptr
                         ? static_cast<long long>(result.try_value()->try_primary_error()->code().value())
                         : -1LL,
                     result && result.try_value()->try_primary_error() != nullptr
                         ? static_cast<int>(result.try_value()->try_primary_error()->root_code().domain().size())
                         : 0,
                     result && result.try_value()->try_primary_error() != nullptr
                         ? result.try_value()->try_primary_error()->root_code().domain().data()
                         : "",
                     result && result.try_value()->try_primary_error() != nullptr
                         ? static_cast<long long>(result.try_value()->try_primary_error()->root_code().value())
                         : -1LL,
                     result && result.try_value()->try_primary_error() != nullptr &&
                             result.try_value()->try_primary_error()->try_native_error() != nullptr
                         ? static_cast<long long>(result.try_value()->try_primary_error()->try_native_error()->value())
                         : -1LL,
                     read_file(a_directory.child(L"Assets\\Source\\Linked.bin")).size(),
                     read_file(a_directory.child(L"Assets\\Source\\Alias.bin")).size(),
                     GetFileAttributesW(a_directory.child(L"Saved\\Editor").c_str()));
    }
    return passed;
}

/// @brief Recordと一致しないPayloadを自動CleanupせずReconciliation Entryとして保持する
[[nodiscard]] bool test_payload_mismatch_reconciliation(const TestDirectory &a_directory,
                                                        const cue::ProjectDescriptor &a_descriptor,
                                                        const cue::AssertContext &a_assertContext)
{
    constexpr std::string_view k_operationId = "55555555-5555-4555-8555-555555555555";
    constexpr cue::TraversalLimits k_traversal{16U, 1024U, 1024U, 1024U * 1024U};
    constexpr cue::ContentVerificationLimits k_content{1024U * 1024U, 4U * 1024U * 1024U};
    const std::array<std::byte, 4U> original{std::byte{'s'}, std::byte{'a'}, std::byte{'f'}, std::byte{'e'}};
    const std::array<std::byte, 4U> changed{std::byte{'e'}, std::byte{'d'}, std::byte{'i'}, std::byte{'t'}};
    if (!write_file(a_directory.child(L"Assets\\Source\\Mismatch.bin"), original))
    {
        return false;
    }
    auto service = make_service(a_directory, a_descriptor, a_assertContext, {std::string(k_operationId)});
    auto source = cue::RelativePath::parse("Mismatch.bin", a_assertContext);
    if (!service || !source)
    {
        return false;
    }
    auto deleted = service.try_value()->delete_entry(cue::project_files::ProjectFileArea::SourceAssets,
                                                     std::move(*source.try_value()), k_traversal, k_content);
    const std::wstring payload =
        a_directory.child(L"Saved\\Editor\\Trash\\55555555-5555-4555-8555-555555555555\\Payload");
    if (!deleted || !write_file(payload, changed))
    {
        return false;
    }

    auto reopened = make_service(a_directory, a_descriptor, a_assertContext, {});
    return reopened && reopened.try_value()->refresh_recovery_catalog(k_traversal, k_content) &&
           reopened.try_value()->recovery_entries().size() == 1U &&
           reopened.try_value()->recovery_entries()[0U].state ==
               cue::project_files::RecoveryEntryState::ReconciliationRequired &&
           !reopened.try_value()->recovery_diagnostics().empty() &&
           read_file(payload) == std::vector<std::byte>(changed.begin(), changed.end()) &&
           GetFileAttributesW(a_directory.child(L"Assets\\Source\\Mismatch.bin").c_str()) == INVALID_FILE_ATTRIBUTES;
}

/// @brief Crashで残った空のTrash Staging DirectoryをCatalog更新時にCleanupする
[[nodiscard]] bool test_empty_staging_cleanup(const TestDirectory &a_directory,
                                              const cue::ProjectDescriptor &a_descriptor,
                                              const cue::AssertContext &a_assertContext)
{
    constexpr cue::TraversalLimits k_traversal{16U, 1024U, 1024U, 1024U * 1024U};
    constexpr cue::ContentVerificationLimits k_content{1024U * 1024U, 4U * 1024U * 1024U};
    const std::wstring editorDirectory = a_directory.child(L"Saved\\Editor");
    const std::wstring trashDirectory = a_directory.child(L"Saved\\Editor\\Trash");
    const std::wstring stagingDirectory =
        a_directory.child(L"Saved\\Editor\\Trash\\.66666666-6666-4666-8666-666666666666.cuetrash-staging");
    if (CreateDirectoryW(editorDirectory.c_str(), nullptr) == FALSE ||
        CreateDirectoryW(trashDirectory.c_str(), nullptr) == FALSE ||
        CreateDirectoryW(stagingDirectory.c_str(), nullptr) == FALSE)
    {
        return false;
    }

    auto service = make_service(a_directory, a_descriptor, a_assertContext, {});
    return service && service.try_value()->refresh_recovery_catalog(k_traversal, k_content) &&
           GetFileAttributesW(stagingDirectory.c_str()) == INVALID_FILE_ATTRIBUTES &&
           service.try_value()->recovery_entries().empty();
}
} // namespace

/// @brief Project-local TrashとRecoveryのIntegration Caseを実行する
int main()
{
    TestFatalHandler fatalHandler;
    std::vector<std::unique_ptr<cue::LogSink>> sinks;
    cue::Logger logger(fatalHandler, std::move(sinks));
    cue::AssertContext assertContext(logger, fatalHandler);
    auto descriptor = make_descriptor(assertContext);
    TestDirectory fileDirectory;
    TestDirectory directoryDirectory;
    TestDirectory rejectedDirectory;
    TestDirectory reconciliationDirectory;
    TestDirectory stagingDirectory;
    if (!descriptor || !fileDirectory.is_created() || !directoryDirectory.is_created() ||
        !rejectedDirectory.is_created() || !reconciliationDirectory.is_created() || !stagingDirectory.is_created() ||
        !write_descriptor(fileDirectory, *descriptor.try_value(), assertContext) ||
        !write_descriptor(directoryDirectory, *descriptor.try_value(), assertContext) ||
        !write_descriptor(rejectedDirectory, *descriptor.try_value(), assertContext) ||
        !write_descriptor(reconciliationDirectory, *descriptor.try_value(), assertContext) ||
        !write_descriptor(stagingDirectory, *descriptor.try_value(), assertContext))
    {
        return 1;
    }
    if (!test_file_recovery(fileDirectory, *descriptor.try_value(), assertContext))
    {
        return 2;
    }
    if (!test_directory_recovery(directoryDirectory, *descriptor.try_value(), assertContext))
    {
        return 3;
    }
    if (!test_rejected_delete_targets(rejectedDirectory, *descriptor.try_value(), assertContext))
    {
        return 4;
    }
    if (!test_payload_mismatch_reconciliation(reconciliationDirectory, *descriptor.try_value(), assertContext))
    {
        return 5;
    }
    return test_empty_staging_cleanup(stagingDirectory, *descriptor.try_value(), assertContext) ? 0 : 6;
}

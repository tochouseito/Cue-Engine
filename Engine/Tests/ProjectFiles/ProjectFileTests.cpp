#include <Cue/Foundation/Assert.h>
#include <Cue/Foundation/Fatal.h>
#include <Cue/Foundation/Log.h>
#include <Cue/IO/Error.h>
#include <Cue/IO/Windows/WindowsWorkspaceFilesystem.h>
#include <Cue/Project/Descriptor.h>
#include <Cue/ProjectFiles/Error.h>
#include <Cue/ProjectFiles/Service.h>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <barrier>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace
{
class TestFatalHandler final : public cue::FatalHandler
{
  public:
    [[noreturn]] void terminate() noexcept override
    {
        std::_Exit(90);
    }

    [[noreturn]] void terminate(std::string_view) noexcept override
    {
        std::_Exit(91);
    }
};

class TestDirectory final
{
  public:
    TestDirectory()
    {
        std::array<wchar_t, MAX_PATH> temporary{};
        const DWORD length = GetTempPathW(static_cast<DWORD>(temporary.size()), temporary.data());
        if (length == 0U || length >= temporary.size())
        {
            return;
        }
        m_path = temporary.data();
        m_path += L"CueProjectFilesTests-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                  std::to_wstring(GetTickCount64());
        m_created = CreateDirectoryW(m_path.c_str(), nullptr) != FALSE &&
                    CreateDirectoryW(child(L"Assets").c_str(), nullptr) != FALSE &&
                    CreateDirectoryW(child(L"Assets\\Source").c_str(), nullptr) != FALSE &&
                    CreateDirectoryW(child(L"Assets\\Runtime").c_str(), nullptr) != FALSE &&
                    CreateDirectoryW(child(L"Generated").c_str(), nullptr) != FALSE &&
                    CreateDirectoryW(child(L"Saved").c_str(), nullptr) != FALSE;
    }
    TestDirectory(const TestDirectory &) = delete;
    TestDirectory &operator=(const TestDirectory &) = delete;
    ~TestDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
    }

    [[nodiscard]] bool is_created() const noexcept
    {
        return m_created;
    }

    [[nodiscard]] std::wstring child(std::wstring_view a_relative) const
    {
        return m_path + L"\\" + std::wstring(a_relative);
    }

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
    SequenceOperationIdSource(const cue::AssertContext &a_assertContext, std::vector<std::string> a_ids) noexcept
        : m_assertContext(&a_assertContext), m_ids(std::move(a_ids))
    {
    }
    ~SequenceOperationIdSource() override = default;

    [[nodiscard]] cue::Result<std::string> next_operation_id() noexcept override
    {
        if (m_next == m_ids.size())
        {
            return cue::Result<std::string>::failure(cue::project_files::make_project_file_error(
                *m_assertContext, cue::project_files::ProjectFileError::InvalidRequest,
                "Test operation id source is exhausted"));
        }
        return cue::Result<std::string>::success(std::move(m_ids[m_next++]));
    }

  private:
    const cue::AssertContext *m_assertContext;
    std::vector<std::string> m_ids;
    std::size_t m_next = 0U;
};

[[nodiscard]] bool has_project_file_error(const cue::Error *a_error,
                                          cue::project_files::ProjectFileError a_code) noexcept
{
    return a_error != nullptr && a_error->code().domain() == "Cue.ProjectFiles" &&
           a_error->code().value() == static_cast<std::int64_t>(a_code);
}

[[nodiscard]] bool write_file(std::wstring_view a_path, std::span<const std::byte> a_bytes) noexcept
{
    HANDLE file = CreateFileW(a_path.data(), GENERIC_WRITE, 0U, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return false;
    }
    DWORD written = 0U;
    const bool success =
        WriteFile(file, a_bytes.data(), static_cast<DWORD>(a_bytes.size()), &written, nullptr) != FALSE &&
        written == a_bytes.size();
    CloseHandle(file);
    return success;
}

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
    if (GetFileSizeEx(file, &size) == FALSE || size.QuadPart < 0 || size.QuadPart > 1024)
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

[[nodiscard]] cue::Result<cue::ProjectDescriptor> make_descriptor(const cue::AssertContext &a_assertContext) noexcept
{
    auto id = cue::ProjectId::parse("12345678-1234-4234-8234-123456789abc", a_assertContext);
    if (!id)
    {
        return cue::Result<cue::ProjectDescriptor>::failure(std::move(*id.try_error()));
    }
    return cue::create_blank_project_descriptor(
        *id.try_value(), "Project Files Test",
        cue::EngineCompatibility{cue::EngineVersion{1U, 0U, 0U}, cue::EngineVersion{2U, 0U, 0U}}, a_assertContext);
}

[[nodiscard]] std::unique_ptr<cue::project_files::ProjectFileOperationIdSource> make_id_source(
    const cue::AssertContext &a_assertContext, std::vector<std::string> a_ids)
{
    return std::make_unique<SequenceOperationIdSource>(a_assertContext, std::move(a_ids));
}

[[nodiscard]] bool has_no_staging_entries(const TestDirectory &a_directory)
{
    std::error_code error;
    for (const std::filesystem::directory_entry &entry :
         std::filesystem::directory_iterator(a_directory.child(L"Assets\\Source"), error))
    {
        const std::wstring name = entry.path().filename().wstring();
        if (name.find(L".cuefile-staging") != std::wstring::npos || name.find(L".cuedir-staging") != std::wstring::npos)
        {
            return false;
        }
    }
    return !error;
}

[[nodiscard]] bool test_create_and_policy(const TestDirectory &a_directory, const cue::ProjectDescriptor &a_descriptor,
                                          const cue::AssertContext &a_assertContext)
{
    auto workspace = cue::create_windows_workspace_filesystem(a_directory.utf8_path(), a_assertContext);
    if (!workspace)
    {
        return false;
    }
    std::vector<std::string> ids{"11111111-1111-4111-8111-111111111111", "22222222-2222-4222-8222-222222222222",
                                 "33333333-3333-4333-8333-333333333333", "44444444-4444-4444-8444-444444444444",
                                 "55555555-5555-4555-8555-555555555555", "88888888-8888-4888-8888-888888888888"};
    auto serviceResult = cue::project_files::ProjectFileService::create(a_descriptor, std::move(*workspace.try_value()),
                                                                        make_id_source(a_assertContext, std::move(ids)),
                                                                        a_assertContext);
    if (!serviceResult)
    {
        return false;
    }
    cue::project_files::ProjectFileService service = std::move(*serviceResult.try_value());

    auto folderPath = cue::RelativePath::parse("CreatedFolder", a_assertContext);
    if (!folderPath)
    {
        return false;
    }
    auto folder =
        service.create_directory(cue::project_files::ProjectFileArea::SourceAssets, std::move(*folderPath.try_value()));
    if (!folder || folder.try_value()->outcome() != cue::project_files::ProjectFileOperationOutcome::Committed ||
        folder.try_value()->operation_id() != "11111111-1111-4111-8111-111111111111" ||
        folder.try_value()->kind() != cue::project_files::ProjectFileOperationKind::DirectoryCreation ||
        folder.try_value()->area() != cue::project_files::ProjectFileArea::SourceAssets ||
        folder.try_value()->destination() != "CreatedFolder" ||
        folder.try_value()->stage() != cue::project_files::ProjectFileOperationStage::Complete ||
        folder.try_value()->try_primary_error() != nullptr || !folder.try_value()->secondary_diagnostics().empty() ||
        folder.try_value()->rescan_directories().size() != 1U ||
        !folder.try_value()->rescan_directories()[0U].empty() ||
        GetFileAttributesW(a_directory.child(L"Assets\\Source\\CreatedFolder").c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        return false;
    }

    folderPath = cue::RelativePath::parse("CreatedFolder", a_assertContext);
    auto folderConflict =
        service.create_directory(cue::project_files::ProjectFileArea::SourceAssets, std::move(*folderPath.try_value()));
    if (!folderConflict ||
        folderConflict.try_value()->outcome() != cue::project_files::ProjectFileOperationOutcome::NotCommitted ||
        !has_project_file_error(folderConflict.try_value()->try_primary_error(),
                                cue::project_files::ProjectFileError::Conflict))
    {
        return false;
    }

    const std::array<std::byte, 4U> newBytes{std::byte{'n'}, std::byte{'e'}, std::byte{'w'}, std::byte{'!'}};
    auto filePath = cue::RelativePath::parse("Created.bin", a_assertContext);
    auto file = service.create_file(cue::project_files::ProjectFileArea::SourceAssets, std::move(*filePath.try_value()),
                                    newBytes);
    if (!file || file.try_value()->outcome() != cue::project_files::ProjectFileOperationOutcome::Committed ||
        read_file(a_directory.child(L"Assets\\Source\\Created.bin")) !=
            std::vector<std::byte>(newBytes.begin(), newBytes.end()))
    {
        return false;
    }

    const std::array<std::byte, 3U> oldBytes{std::byte{'o'}, std::byte{'l'}, std::byte{'d'}};
    if (!write_file(a_directory.child(L"Assets\\Source\\Keep.bin"), oldBytes))
    {
        return false;
    }
    filePath = cue::RelativePath::parse("Keep.bin", a_assertContext);
    auto fileConflict = service.create_file(cue::project_files::ProjectFileArea::SourceAssets,
                                            std::move(*filePath.try_value()), newBytes);
    if (!fileConflict ||
        fileConflict.try_value()->outcome() != cue::project_files::ProjectFileOperationOutcome::NotCommitted ||
        !has_project_file_error(fileConflict.try_value()->try_primary_error(),
                                cue::project_files::ProjectFileError::Conflict) ||
        read_file(a_directory.child(L"Assets\\Source\\Keep.bin")) !=
            std::vector<std::byte>(oldBytes.begin(), oldBytes.end()))
    {
        return false;
    }

    void *protectedMemory = VirtualAlloc(nullptr, 4096U, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (protectedMemory == nullptr)
    {
        return false;
    }
    auto *unreadableByte = std::construct_at(static_cast<std::byte *>(protectedMemory), std::byte{0x7f});
    DWORD oldProtection = 0U;
    if (VirtualProtect(protectedMemory, 4096U, PAGE_NOACCESS, &oldProtection) == FALSE)
    {
        VirtualFree(protectedMemory, 0U, MEM_RELEASE);
        return false;
    }
    filePath = cue::RelativePath::parse("WriteFailure.bin", a_assertContext);
    auto writeFailure =
        service.create_file(cue::project_files::ProjectFileArea::SourceAssets, std::move(*filePath.try_value()),
                            std::span<const std::byte>(unreadableByte, 1U));
    DWORD ignoredProtection = 0U;
    VirtualProtect(protectedMemory, 4096U, PAGE_READWRITE, &ignoredProtection);
    std::destroy_at(unreadableByte);
    VirtualFree(protectedMemory, 0U, MEM_RELEASE);
    if (!writeFailure ||
        writeFailure.try_value()->outcome() != cue::project_files::ProjectFileOperationOutcome::NotCommitted ||
        !has_project_file_error(writeFailure.try_value()->try_primary_error(),
                                cue::project_files::ProjectFileError::StorageFailure) ||
        GetFileAttributesW(a_directory.child(L"Assets\\Source\\WriteFailure.bin").c_str()) != INVALID_FILE_ATTRIBUTES ||
        !has_no_staging_entries(a_directory))
    {
        return false;
    }

    auto protectedPath = cue::RelativePath::parse("Blocked.bin", a_assertContext);
    auto protectedResult = service.create_file(cue::project_files::ProjectFileArea::RuntimeAssets,
                                               std::move(*protectedPath.try_value()), newBytes);
    bool wrongThreadRejected = false;
    std::thread wrongThread(
        [&]()
        {
            auto wrongThreadPath = cue::RelativePath::parse("WrongThread.bin", a_assertContext);
            auto wrongThreadResult = service.create_file(cue::project_files::ProjectFileArea::SourceAssets,
                                                         std::move(*wrongThreadPath.try_value()), newBytes);
            wrongThreadRejected =
                !wrongThreadResult && has_project_file_error(wrongThreadResult.try_error(),
                                                             cue::project_files::ProjectFileError::InvalidRequest);
        });
    wrongThread.join();

    return protectedResult && wrongThreadRejected &&
           protectedResult.try_value()->outcome() == cue::project_files::ProjectFileOperationOutcome::NotCommitted &&
           has_project_file_error(protectedResult.try_value()->try_primary_error(),
                                  cue::project_files::ProjectFileError::ProtectedEntry) &&
           GetFileAttributesW(a_directory.child(L"Assets\\Runtime\\Blocked.bin").c_str()) == INVALID_FILE_ATTRIBUTES &&
           has_no_staging_entries(a_directory);
}

[[nodiscard]] bool test_concurrent_conflict(const TestDirectory &a_directory,
                                            const cue::ProjectDescriptor &a_descriptor,
                                            const cue::AssertContext &a_assertContext)
{
    std::barrier start(2);
    std::array<int, 2U> outcomes{0, 0};
    const std::array<std::string_view, 2U> ids{"66666666-6666-4666-8666-666666666666",
                                               "77777777-7777-4777-8777-777777777777"};
    std::array<std::thread, 2U> workers;
    for (std::size_t index = 0U; index < workers.size(); ++index)
    {
        workers[index] = std::thread(
            [&, index]()
            {
                auto workspace = cue::create_windows_workspace_filesystem(a_directory.utf8_path(), a_assertContext);
                std::vector<std::string> operationIds{std::string(ids[index])};
                if (!workspace)
                {
                    start.arrive_and_drop();
                    return;
                }
                auto service = cue::project_files::ProjectFileService::create(
                    a_descriptor, std::move(*workspace.try_value()),
                    make_id_source(a_assertContext, std::move(operationIds)), a_assertContext);
                auto locator = cue::RelativePath::parse("Concurrent.bin", a_assertContext);
                if (!service || !locator)
                {
                    start.arrive_and_drop();
                    return;
                }
                start.arrive_and_wait();
                const std::array<std::byte, 1U> content{std::byte{static_cast<unsigned char>('A' + index)}};
                auto result = service.try_value()->create_file(cue::project_files::ProjectFileArea::SourceAssets,
                                                               std::move(*locator.try_value()), content);
                if (!result)
                {
                    return;
                }
                if (result.try_value()->outcome() == cue::project_files::ProjectFileOperationOutcome::Committed)
                {
                    outcomes[index] = 1;
                }
                else if (result.try_value()->outcome() ==
                             cue::project_files::ProjectFileOperationOutcome::NotCommitted &&
                         has_project_file_error(result.try_value()->try_primary_error(),
                                                cue::project_files::ProjectFileError::Conflict))
                {
                    outcomes[index] = 2;
                }
            });
    }
    for (std::thread &worker : workers)
    {
        worker.join();
    }
    return std::count(outcomes.begin(), outcomes.end(), 1) == 1 &&
           std::count(outcomes.begin(), outcomes.end(), 2) == 1 && has_no_staging_entries(a_directory);
}

[[nodiscard]] bool test_missing_source_root(const cue::ProjectDescriptor &a_descriptor,
                                            const cue::AssertContext &a_assertContext)
{
    TestDirectory directory;
    if (!directory.is_created() || RemoveDirectoryW(directory.child(L"Assets\\Source").c_str()) == FALSE)
    {
        return false;
    }
    auto workspace = cue::create_windows_workspace_filesystem(directory.utf8_path(), a_assertContext);
    if (!workspace)
    {
        return false;
    }
    std::vector<std::string> ids{"99999999-9999-4999-8999-999999999999"};
    auto service = cue::project_files::ProjectFileService::create(a_descriptor, std::move(*workspace.try_value()),
                                                                  make_id_source(a_assertContext, std::move(ids)),
                                                                  a_assertContext);
    return !service &&
           has_project_file_error(service.try_error(), cue::project_files::ProjectFileError::InvalidRequest);
}

[[nodiscard]] bool test_invalid_operation_id(const TestDirectory &a_directory,
                                             const cue::ProjectDescriptor &a_descriptor,
                                             const cue::AssertContext &a_assertContext)
{
    auto workspace = cue::create_windows_workspace_filesystem(a_directory.utf8_path(), a_assertContext);
    if (!workspace)
    {
        return false;
    }
    std::vector<std::string> ids{"not-an-operation-id"};
    auto service = cue::project_files::ProjectFileService::create(a_descriptor, std::move(*workspace.try_value()),
                                                                  make_id_source(a_assertContext, std::move(ids)),
                                                                  a_assertContext);
    auto destination = cue::RelativePath::parse("InvalidOperation.bin", a_assertContext);
    if (!service || !destination)
    {
        return false;
    }

    const std::array<std::byte, 1U> content{std::byte{0x01}};
    auto result = service.try_value()->create_file(cue::project_files::ProjectFileArea::SourceAssets,
                                                   std::move(*destination.try_value()), content);
    return !result &&
           has_project_file_error(result.try_error(), cue::project_files::ProjectFileError::InvalidRequest) &&
           GetFileAttributesW(a_directory.child(L"Assets\\Source\\InvalidOperation.bin").c_str()) ==
               INVALID_FILE_ATTRIBUTES &&
           has_no_staging_entries(a_directory);
}
} // namespace

int main()
{
    TestFatalHandler fatalHandler;
    std::vector<std::unique_ptr<cue::LogSink>> sinks;
    cue::Logger logger(fatalHandler, std::move(sinks));
    cue::AssertContext assertContext(logger, fatalHandler);
    TestDirectory directory;
    auto descriptor = make_descriptor(assertContext);
    if (!directory.is_created() || !descriptor)
    {
        return 1;
    }
    if (!test_create_and_policy(directory, *descriptor.try_value(), assertContext))
    {
        return 2;
    }

    auto reserved = cue::RelativePath::parse("CON.txt", assertContext);
    auto escaped = cue::RelativePath::parse("../Escape.bin", assertContext);
    if (reserved || escaped)
    {
        return 3;
    }
    if (!test_concurrent_conflict(directory, *descriptor.try_value(), assertContext))
    {
        return 4;
    }
    if (!test_invalid_operation_id(directory, *descriptor.try_value(), assertContext))
    {
        return 5;
    }
    if (!test_missing_source_root(*descriptor.try_value(), assertContext))
    {
        return 6;
    }
    return 0;
}

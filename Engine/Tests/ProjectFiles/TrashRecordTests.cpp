#include "TrashRecord.h"

#include <Cue/Foundation/Assert.h>
#include <Cue/Foundation/Fatal.h>
#include <Cue/Foundation/Log.h>
#include <Cue/ProjectFiles/Error.h>

#include <cstdlib>
#include <memory>
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

/// @brief Writer Hard Limit Testの基準Recordを作成する
[[nodiscard]] cue::project_files_private::TrashRecord make_record()
{
    cue::project_files_private::TrashRecord record;
    record.projectId = "11111111-1111-4111-8111-111111111111";
    record.operationId = "22222222-2222-4222-8222-222222222222";
    record.state = cue::project_files_private::TrashRecordState::Allocating;
    record.originalPath = "Entry.bin";
    record.fingerprint.type = cue::WorkspaceEntryType::RegularFile;
    record.fingerprint.file = cue::WorkspaceFileFingerprint{3U, 0x123456789abcdef0ULL};
    return record;
}

/// @brief ErrorがTrash RecoveryRequiredへ分類されたか判定する
[[nodiscard]] bool is_recovery_error(const cue::Error &a_error) noexcept
{
    return a_error.code().domain() == "Cue.ProjectFiles" &&
           a_error.code().value() == static_cast<std::int64_t>(cue::project_files::ProjectFileError::RecoveryRequired);
}
} // namespace

/// @brief Trash Record WriterのRound-tripとHard Limit拒否を検証する
int main()
{
    TestFatalHandler fatalHandler;
    std::vector<std::unique_ptr<cue::LogSink>> sinks;
    cue::Logger logger(fatalHandler, std::move(sinks));
    cue::AssertContext assertContext(logger, fatalHandler);

    cue::project_files_private::TrashRecord record = make_record();
    cue::Result<std::vector<std::byte>> serialized =
        cue::project_files_private::serialize_trash_record(record, assertContext);
    cue::Result<cue::project_files_private::TrashRecord> parsed =
        serialized ? cue::project_files_private::parse_trash_record(
                         *serialized.try_value(), cue::project_files_private::trash_record_hard_limits(), assertContext)
                   : cue::Result<cue::project_files_private::TrashRecord>::failure(std::move(*serialized.try_error()));
    if (!parsed || parsed.try_value()->projectId != record.projectId ||
        parsed.try_value()->operationId != record.operationId || parsed.try_value()->fingerprint != record.fingerprint)
    {
        return 1;
    }

    record.fingerprint.type = cue::WorkspaceEntryType::Directory;
    record.fingerprint.file.reset();
    record.fingerprint.manifest.resize(cue::project_files_private::trash_record_hard_limits().maxArrayElements + 1U);
    cue::Result<std::vector<std::byte>> oversized =
        cue::project_files_private::serialize_trash_record(record, assertContext);
    return !oversized && is_recovery_error(*oversized.try_error()) ? 0 : 2;
}

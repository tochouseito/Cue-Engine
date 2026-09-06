#include <Cue/ProjectFiles/Error.h>

#include <Cue/Foundation/Assert.h>

namespace cue::project_files
{
Error make_project_file_error(const AssertContext &a_assertContext, ProjectFileError a_code,
                              std::string_view a_summary) noexcept
{
    auto code =
        ErrorCode::create(a_assertContext.fatal_handler(), "Cue.ProjectFiles", static_cast<std::int64_t>(a_code));
    return Error::create(a_assertContext.fatal_handler(), std::move(code), a_summary);
}

Error reclassify_project_file_error(const AssertContext &a_assertContext, ProjectFileError a_code,
                                    std::string_view a_summary, Error &&a_cause) noexcept
{
    auto code =
        ErrorCode::create(a_assertContext.fatal_handler(), "Cue.ProjectFiles", static_cast<std::int64_t>(a_code));
    return Error::reclassify(a_assertContext.fatal_handler(), std::move(code), a_summary, std::move(a_cause));
}
} // namespace cue::project_files

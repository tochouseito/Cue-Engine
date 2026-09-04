#include <Cue/ProjectHub/Error.h>

#include <Cue/Foundation/Assert.h>

namespace cue::project_hub
{
Error make_project_hub_error(const AssertContext &a_assertContext, ProjectHubError a_code,
                             std::string_view a_summary) noexcept
{
    auto code = ErrorCode::create(a_assertContext.fatal_handler(), "Cue.ProjectHub", static_cast<std::int64_t>(a_code));
    return Error::create(a_assertContext.fatal_handler(), std::move(code), a_summary);
}

Error reclassify_project_hub_error(const AssertContext &a_assertContext, ProjectHubError a_code,
                                   std::string_view a_summary, Error &&a_cause) noexcept
{
    auto code = ErrorCode::create(a_assertContext.fatal_handler(), "Cue.ProjectHub", static_cast<std::int64_t>(a_code));
    return Error::reclassify(a_assertContext.fatal_handler(), std::move(code), a_summary, std::move(a_cause));
}
} // namespace cue::project_hub

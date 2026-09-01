#include <Cue/Project/Error.h>

#include <Cue/Foundation/Assert.h>

namespace
{
constexpr std::string_view k_projectDomain = "Cue.Project";
} // namespace

namespace cue
{
Error make_project_error(const AssertContext &a_assertContext, ProjectError a_code, std::string_view a_summary) noexcept
{
    ErrorCode code =
        ErrorCode::create(a_assertContext.fatal_handler(), k_projectDomain, static_cast<std::int64_t>(a_code));
    return Error::create(a_assertContext.fatal_handler(), std::move(code), a_summary);
}
} // namespace cue

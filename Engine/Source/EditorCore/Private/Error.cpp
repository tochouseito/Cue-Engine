#include <Cue/EditorCore/Error.h>

#include <Cue/Foundation/Assert.h>

namespace cue::editor_core
{
Error make_editor_core_error(const AssertContext &a_assertContext, EditorCoreError a_code,
                             std::string_view a_summary) noexcept
{
    auto code = ErrorCode::create(a_assertContext.fatal_handler(), "Cue.EditorCore", static_cast<std::int64_t>(a_code));
    return Error::create(a_assertContext.fatal_handler(), std::move(code), a_summary);
}
} // namespace cue::editor_core

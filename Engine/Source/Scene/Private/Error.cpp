#include <Cue/Scene/Error.h>

#include <Cue/Foundation/Assert.h>

namespace cue::scene
{
Error make_scene_error(const AssertContext &a_assertContext,
                       SceneError a_code,
                       std::string_view a_summary) noexcept
{
    auto code = ErrorCode::create(a_assertContext.fatal_handler(), "Cue.Scene",
                                  static_cast<std::int64_t>(a_code));
    return Error::create(a_assertContext.fatal_handler(), std::move(code),
                         a_summary);
}
} // namespace cue::scene

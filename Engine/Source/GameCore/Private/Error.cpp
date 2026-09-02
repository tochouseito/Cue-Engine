#include <Cue/GameCore/Error.h>

#include <Cue/Foundation/Assert.h>

namespace cue::game_core
{
Error make_game_core_error(const AssertContext &a_assertContext,
                           GameCoreError a_code,
                           std::string_view a_summary) noexcept
{
    auto code = ErrorCode::create(a_assertContext.fatal_handler(),
                                  "Cue.GameCore",
                                  static_cast<std::int64_t>(a_code));
    return Error::create(a_assertContext.fatal_handler(), std::move(code),
                         a_summary);
}
} // namespace cue::game_core

#include <Cue/IO/Error.h>

#include <Cue/Foundation/Assert.h>

namespace
{
constexpr std::string_view k_ioDomain = "Cue.IO";
constexpr std::string_view k_windowsDomain = "Win32";
} // namespace

namespace cue
{
Error make_io_error(const AssertContext &a_assertContext, IoError a_code, std::string_view a_summary) noexcept
{
    ErrorCode code = ErrorCode::create(a_assertContext.fatal_handler(), k_ioDomain, static_cast<std::int64_t>(a_code));
    return Error::create(a_assertContext.fatal_handler(), std::move(code), a_summary);
}

Error make_io_error(const AssertContext &a_assertContext, IoError a_code, std::string_view a_summary,
                    std::int64_t a_nativeCode) noexcept
{
    ErrorCode code = ErrorCode::create(a_assertContext.fatal_handler(), k_ioDomain, static_cast<std::int64_t>(a_code));
    NativeError native = NativeError::create(a_assertContext.fatal_handler(), k_windowsDomain, a_nativeCode);
    return Error::create(a_assertContext.fatal_handler(), std::move(code), a_summary, std::move(native));
}
} // namespace cue

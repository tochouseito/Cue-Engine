#include <Cue/Schema/Error.h>

#include <Cue/Foundation/Assert.h>

namespace
{
constexpr std::string_view k_schemaDomain = "Cue.Schema";
} // namespace

namespace cue::schema
{
Error make_schema_error(const AssertContext &a_assertContext, SchemaError a_code,
                        std::string_view a_summary) noexcept
{
    auto code = ErrorCode::create(a_assertContext.fatal_handler(), k_schemaDomain,
                                  static_cast<std::int64_t>(a_code));
    return Error::create(a_assertContext.fatal_handler(), std::move(code), a_summary);
}
} // namespace cue::schema

#include <Cue/EditorCore/Error.h>

#include <Cue/Foundation/Assert.h>

#include <algorithm>
#include <array>
#include <charconv>

namespace cue::editor_core
{
Error make_editor_core_error(const AssertContext &a_assertContext, EditorCoreError a_code,
                             std::string_view a_summary) noexcept
{
    auto code = ErrorCode::create(a_assertContext.fatal_handler(), "Cue.EditorCore", static_cast<std::int64_t>(a_code));
    return Error::create(a_assertContext.fatal_handler(), std::move(code), a_summary);
}

Error make_editor_document_error(const AssertContext &a_assertContext, EditorCoreError a_code,
                                 std::string_view a_summary, std::uint64_t a_documentId) noexcept
{
    Error error = make_editor_core_error(a_assertContext, a_code, a_summary);
    constexpr std::string_view prefix = "EditorDocumentId=";
    std::array<char, prefix.size() + 20U> context{};
    std::copy(prefix.begin(), prefix.end(), context.begin());
    const auto converted = std::to_chars(context.data() + prefix.size(), context.data() + context.size(), a_documentId);
    if (converted.ec != std::errc{})
    {
        a_assertContext.fatal_handler().terminate("Cue.EditorCore document identity formatting failed");
    }
    error.add_context(a_assertContext.fatal_handler(),
                      std::string_view(context.data(), static_cast<std::size_t>(converted.ptr - context.data())));
    return error;
}
} // namespace cue::editor_core

#pragma once

#include <Cue/Foundation/Result.h>

#include <string>
#include <string_view>

namespace cue
{
class AssertContext;

[[nodiscard]] Result<std::wstring> utf8_to_utf16(std::string_view a_text,
                                                 const AssertContext &a_assertContext) noexcept;
} // namespace cue

#pragma once

#include <Cue/Foundation/Error.h>

#include <cstdint>
#include <string_view>

namespace cue
{
class AssertContext;

/// @brief Platform 固有値へ依存せず IO 失敗を呼び出し側が分類するための Code
enum class IoError : std::int64_t
{
    InvalidPath = 1,
    OutsideRoot = 2,
    NotFound = 3,
    AlreadyExists = 4,
    TypeMismatch = 5,
    UnsupportedEntry = 6,
    PermissionDenied = 7,
    CapacityExceeded = 8,
    IoFailure = 9,
    DurabilityUnknown = 10
};

/// @brief Portable IO Error を Native 診断なしで生成する
[[nodiscard]] Error make_io_error(const AssertContext &a_assertContext, IoError a_code,
                                  std::string_view a_summary) noexcept;

/// @brief Portable IO Error を Native 診断付きで生成する
[[nodiscard]] Error make_io_error(const AssertContext &a_assertContext, IoError a_code, std::string_view a_summary,
                                  std::int64_t a_nativeCode) noexcept;
} // namespace cue

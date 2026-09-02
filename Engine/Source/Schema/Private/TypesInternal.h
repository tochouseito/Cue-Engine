#pragma once

#include <Cue/Schema/Types.h>

namespace cue::schema
{
/// @brief TypeId が non-nil RFC 4122 UUID Version 4 か判定する
[[nodiscard]] bool is_valid_type_id(TypeId a_id) noexcept;
} // namespace cue::schema

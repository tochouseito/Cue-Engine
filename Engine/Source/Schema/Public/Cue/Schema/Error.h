#pragma once

#include <Cue/Foundation/Error.h>

#include <cstdint>
#include <string_view>

namespace cue
{
class AssertContext;
}

namespace cue::schema
{
/// @brief Schema値とRegistry構築の失敗を分類するCode
enum class SchemaError : std::int64_t
{
    InvalidTypeId = 1,
    InvalidFieldId = 2,
    InvalidSchemaVersion = 3,
    InvalidName = 4,
    DuplicateTypeId = 5,
    DuplicateTypeName = 6,
    DuplicateFieldId = 7,
    DuplicateFieldName = 8,
    ReservedFieldId = 9,
    TombstonedTypeId = 10,
    DuplicateTombstone = 11,
    CapacityExceeded = 12
};

/// @brief Schema Errorを診断Summaryと共に生成する
[[nodiscard]] Error make_schema_error(const AssertContext &a_assertContext,
                                      SchemaError a_code, std::string_view a_summary) noexcept;
} // namespace cue::schema

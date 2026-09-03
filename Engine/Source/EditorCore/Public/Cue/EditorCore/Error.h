#pragma once

#include <Cue/Foundation/Error.h>

#include <cstdint>
#include <string_view>

namespace cue
{
class AssertContext;
}

namespace cue::editor_core
{
/// @brief Editor Coreの回復可能な失敗を分類するCode
enum class EditorCoreError : std::int64_t
{
    DocumentNotFound = 1,
    DuplicateScene = 2,
    DuplicateLocator = 3,
    DocumentIdExhausted = 4,
    RevisionExhausted = 5,
    InvalidSavedState = 6,
    InvalidDocumentState = 7,
    InvalidCloseTransition = 8
};

/// @brief Editor Core Errorを診断Summaryと共に生成する
[[nodiscard]] Error make_editor_core_error(const AssertContext &a_assertContext, EditorCoreError a_code,
                                           std::string_view a_summary) noexcept;
} // namespace cue::editor_core

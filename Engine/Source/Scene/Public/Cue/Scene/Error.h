#pragma once

#include <Cue/Foundation/Error.h>

#include <cstdint>
#include <string_view>

namespace cue
{
class AssertContext;
}

namespace cue::scene
{
/// @brief Scene Authoring Modelの回復可能な失敗を分類するCode
enum class SceneError : std::int64_t
{
    InvalidIdentity = 1,
    DuplicateObjectId = 2,
    ObjectNotFound = 3,
    DanglingParent = 4,
    HierarchyCycle = 5,
    ChildObjectsExist = 6,
    InvalidName = 7,
    HierarchyDepthExceeded = 8,
    DuplicateComponentId = 9,
    ComponentNotFound = 10,
    InvalidComponentData = 11,
    DuplicateFieldId = 12,
    UnknownSchemaType = 13,
    UnknownSchemaField = 14,
    FieldTypeMismatch = 15,
    InvalidOpaqueData = 16,
    InvalidFormat = 17,
    UnsupportedFormatVersion = 18,
    MissingMigrationStep = 19,
    MigrationFailed = 20,
    StorageNotPublished = 21,
    StorageDurabilityUnknown = 22,
    ParseBackMismatch = 23,
    PublishedVerificationFailed = 24,
    UnsupportedRuntimeComponent = 25,
    RuntimeInstantiationFailed = 26,
    RuntimeWorldMismatch = 27,
    StructuralCapacityExceeded = 28
};

/// @brief Scene Errorを診断Summaryと共に生成する
[[nodiscard]] Error make_scene_error(const AssertContext &a_assertContext, SceneError a_code,
                                     std::string_view a_summary) noexcept;
} // namespace cue::scene

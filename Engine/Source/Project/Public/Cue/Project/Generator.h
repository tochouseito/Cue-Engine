#pragma once

#include <Cue/Project/Descriptor.h>

#include <string_view>

namespace cue
{
class AssertContext;
class FilesystemRoot;

/// @brief Blank Project が要求する Engine 互換範囲だけを保持する最小 Template
struct BlankProjectTemplate final
{
    EngineCompatibility engineCompatibility;
};

/// @brief Project を Operation 所有 Staging で完成させ、既存 Directory を上書きせず Atomic に公開する
///
/// Project 名は Portable な単一 Directory Segment だけを受理する
/// 成功時は検証済み Descriptor を返し、Publish 前失敗時は最終 Directory を作らず Staging を Rollback する
[[nodiscard]] Result<ProjectDescriptor> generate_blank_project(
    FilesystemRoot &a_parentFilesystem, std::string_view a_projectName, std::string_view a_displayName,
    const ProjectId &a_projectId, BlankProjectTemplate a_template, const AssertContext &a_assertContext) noexcept;
} // namespace cue

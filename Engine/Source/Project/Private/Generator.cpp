#include <Cue/Project/Generator.h>

#include <Cue/Foundation/Assert.h>
#include <Cue/IO/Error.h>
#include <Cue/IO/Filesystem.h>
#include <Cue/IO/RelativePath.h>
#include <Cue/Project/Error.h>

#include <array>
#include <cstddef>
#include <cstdlib>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
constexpr std::size_t k_maximumDescriptorBytes = 1024U * 1024U;

/// @brief Generator 処理中の予期しない例外を追加 Allocation なしで Fatal 境界へ渡す
[[noreturn]] void terminate_generator_exception(const cue::AssertContext &a_assertContext) noexcept
{
    a_assertContext.fatal_handler().terminate("Project generator operation failed unexpectedly");
    std::abort();
}

/// @brief 下位 IO 失敗を原因として保持しながら Project 生成失敗へ再分類する
[[nodiscard]] cue::Error reclassify_io_error(const cue::AssertContext &a_assertContext, std::string_view a_summary,
                                             cue::Error &&a_cause) noexcept
{
    cue::ErrorCode code = cue::ErrorCode::create(a_assertContext.fatal_handler(), "Cue.Project",
                                                 static_cast<std::int64_t>(cue::ProjectError::IoFailure));
    return cue::Error::reclassify(a_assertContext.fatal_handler(), std::move(code), a_summary, std::move(a_cause));
}

/// @brief 無効な Project 名の下位 Path 診断を保持して Project 分類へ再分類する
[[nodiscard]] cue::Error reclassify_project_name_error(const cue::AssertContext &a_assertContext,
                                                       cue::Error &&a_cause) noexcept
{
    cue::ErrorCode code = cue::ErrorCode::create(a_assertContext.fatal_handler(), "Cue.Project",
                                                 static_cast<std::int64_t>(cue::ProjectError::InvalidProjectName));
    return cue::Error::reclassify(a_assertContext.fatal_handler(), std::move(code), "Project name is invalid",
                                  std::move(a_cause));
}

/// @brief Staging 基点と Template 相対 Path を結合して再検証済み Path を返す
[[nodiscard]] cue::Result<cue::RelativePath> make_staging_path(const cue::StagingArea &a_staging,
                                                              std::string_view a_suffix,
                                                              const cue::AssertContext &a_assertContext) noexcept
{
    try
    {
        std::string path(a_staging.path().text());
        path.push_back('/');
        path.append(a_suffix);
        return cue::RelativePath::parse(path, a_assertContext);
    }
    catch (...)
    {
        terminate_generator_exception(a_assertContext);
    }
}

/// @brief Primary 失敗を維持したまま Operation 所有 Staging の Rollback 失敗を Secondary 診断へ追加する
void rollback_staging(cue::FilesystemRoot &a_filesystem, cue::StagingArea &a_staging, cue::Error &a_primary,
                      const cue::AssertContext &a_assertContext) noexcept
{
    auto rollback = a_filesystem.rollback_staging_area(std::move(a_staging));
    if (!rollback)
    {
        a_primary.append_secondary_diagnostics(a_assertContext, *rollback.try_error(),
                                               "Project staging rollback failed", "Rollback");
    }
}

/// @brief IO 失敗が Publish 済みで Rollback 不能な DurabilityUnknown か判定する
[[nodiscard]] bool is_durability_unknown(const cue::Error &a_error) noexcept
{
    return a_error.code().domain() == "Cue.IO" &&
           a_error.code().value() == static_cast<std::int64_t>(cue::IoError::DurabilityUnknown);
}

/// @brief Byte 列を Copy せず Descriptor Parser へ渡せる UTF-8 View へ変換する
[[nodiscard]] std::string_view bytes_as_string(std::span<const std::byte> a_bytes) noexcept
{
    return std::string_view(reinterpret_cast<const char *>(a_bytes.data()), a_bytes.size());
}
} // namespace

namespace cue
{
Result<ProjectDescriptor> generate_blank_project(FilesystemRoot &a_parentFilesystem,
                                                  std::string_view a_projectName, std::string_view a_displayName,
                                                  const ProjectId &a_projectId, BlankProjectTemplate a_template,
                                                  const AssertContext &a_assertContext) noexcept
{
    try
    {
        auto destination = RelativePath::parse(a_projectName, a_assertContext);
        if (!destination)
        {
            return Result<ProjectDescriptor>::failure(
                reclassify_project_name_error(a_assertContext, std::move(*destination.try_error())));
        }
        if (a_projectName.find('/') != std::string_view::npos)
        {
            return Result<ProjectDescriptor>::failure(make_project_error(
                a_assertContext, ProjectError::InvalidProjectName, "Project name must be one directory segment"));
        }

        auto descriptor = create_blank_project_descriptor(a_projectId, a_displayName, a_template.engineCompatibility,
                                                          a_assertContext);
        if (!descriptor)
        {
            return Result<ProjectDescriptor>::failure(std::move(*descriptor.try_error()));
        }
        auto serialized = serialize_project_descriptor(*descriptor.try_value(), a_assertContext);
        if (!serialized)
        {
            return Result<ProjectDescriptor>::failure(std::move(*serialized.try_error()));
        }

        auto staging = a_parentFilesystem.create_staging_area(*destination.try_value());
        if (!staging)
        {
            return Result<ProjectDescriptor>::failure(reclassify_io_error(
                a_assertContext, "Project staging directory creation failed", std::move(*staging.try_error())));
        }

        constexpr std::array<std::string_view, 4U> directories = {"Assets/Source", "Assets/Runtime", "Generated",
                                                                  "Saved"};
        for (const std::string_view directory : directories)
        {
            auto path = make_staging_path(*staging.try_value(), directory, a_assertContext);
            if (!path)
            {
                Error primary = reclassify_io_error(a_assertContext, "Project directory path creation failed",
                                                    std::move(*path.try_error()));
                rollback_staging(a_parentFilesystem, *staging.try_value(), primary, a_assertContext);
                return Result<ProjectDescriptor>::failure(std::move(primary));
            }
            auto created = a_parentFilesystem.create_directories(*path.try_value());
            if (!created)
            {
                Error primary = reclassify_io_error(a_assertContext, "Project directory creation failed",
                                                    std::move(*created.try_error()));
                rollback_staging(a_parentFilesystem, *staging.try_value(), primary, a_assertContext);
                return Result<ProjectDescriptor>::failure(std::move(primary));
            }
        }

        auto descriptorPath = make_staging_path(*staging.try_value(), "CueProject.json", a_assertContext);
        if (!descriptorPath)
        {
            Error primary = reclassify_io_error(a_assertContext, "Project descriptor path creation failed",
                                                std::move(*descriptorPath.try_error()));
            rollback_staging(a_parentFilesystem, *staging.try_value(), primary, a_assertContext);
            return Result<ProjectDescriptor>::failure(std::move(primary));
        }

        const std::span<const char> characters(serialized.try_value()->data(), serialized.try_value()->size());
        auto written = a_parentFilesystem.write_file_atomic(*descriptorPath.try_value(), std::as_bytes(characters));
        if (!written)
        {
            Error primary = reclassify_io_error(a_assertContext, "Project descriptor write failed",
                                                std::move(*written.try_error()));
            rollback_staging(a_parentFilesystem, *staging.try_value(), primary, a_assertContext);
            return Result<ProjectDescriptor>::failure(std::move(primary));
        }

        auto stagedBytes = a_parentFilesystem.read_file(*descriptorPath.try_value(), k_maximumDescriptorBytes);
        if (!stagedBytes)
        {
            Error primary = reclassify_io_error(a_assertContext, "Project descriptor verification read failed",
                                                std::move(*stagedBytes.try_error()));
            rollback_staging(a_parentFilesystem, *staging.try_value(), primary, a_assertContext);
            return Result<ProjectDescriptor>::failure(std::move(primary));
        }
        auto reparsed = parse_project_descriptor(bytes_as_string(*stagedBytes.try_value()), a_assertContext);
        if (!reparsed || !descriptor.try_value()->equivalent_to(*reparsed.try_value()))
        {
            Error primary = reparsed
                                ? make_project_error(a_assertContext, ProjectError::InvalidFormat,
                                                     "Staged project descriptor changed during verification")
                                : std::move(*reparsed.try_error());
            rollback_staging(a_parentFilesystem, *staging.try_value(), primary, a_assertContext);
            return Result<ProjectDescriptor>::failure(std::move(primary));
        }

        auto published = a_parentFilesystem.publish_staging_area(std::move(*staging.try_value()),
                                                                 *destination.try_value());
        if (!published)
        {
            const bool wasPublished = is_durability_unknown(*published.try_error());
            Error primary = reclassify_io_error(a_assertContext, "Project directory publish failed",
                                                std::move(*published.try_error()));
            if (!wasPublished)
            {
                rollback_staging(a_parentFilesystem, *staging.try_value(), primary, a_assertContext);
            }
            return Result<ProjectDescriptor>::failure(std::move(primary));
        }

        return Result<ProjectDescriptor>::success(std::move(*descriptor.try_value()));
    }
    catch (...)
    {
        terminate_generator_exception(a_assertContext);
    }
}
} // namespace cue

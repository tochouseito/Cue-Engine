#include <Cue/Project/Registry.h>

#include "Json.h"

#include <Cue/Foundation/Assert.h>
#include <Cue/IO/Error.h>
#include <Cue/IO/Filesystem.h>
#include <Cue/IO/RelativePath.h>
#include <Cue/Project/Error.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
constexpr std::size_t k_maximumWorkspaceBytes = 1024U * 1024U;
constexpr std::size_t k_maximumLocatorBytes = 32U * 1024U;
constexpr std::uint64_t k_workspaceSchemaVersion = 1U;
constexpr std::size_t k_missingIndex = std::numeric_limits<std::size_t>::max();

using cue::project_private::JsonType;
using cue::project_private::JsonValue;

/// @brief Workspace 処理中の予期しない例外を追加 Allocation なしで Fatal 境界へ渡す
[[noreturn]] void terminate_registry_exception(const cue::AssertContext &a_assertContext) noexcept
{
    a_assertContext.fatal_handler().terminate("Project registry operation failed unexpectedly");
    std::abort();
}

/// @brief 下位 IO 失敗を Cause として保持しながら Project Workspace 失敗へ再分類する
[[nodiscard]] cue::Error reclassify_io_error(const cue::AssertContext &a_assertContext, std::string_view a_summary,
                                             cue::Error &&a_cause) noexcept
{
    cue::ErrorCode code = cue::ErrorCode::create(a_assertContext.fatal_handler(), "Cue.Project",
                                                 static_cast<std::int64_t>(cue::ProjectError::IoFailure));
    return cue::Error::reclassify(a_assertContext.fatal_handler(), std::move(code), a_summary, std::move(a_cause));
}

/// @brief canonical unsigned 64-bit Decimal を上限検査して数値化する
[[nodiscard]] bool parse_canonical_u64(std::string_view a_text, std::uint64_t &a_value) noexcept
{
    if (a_text.empty() || (a_text.size() > 1U && a_text.front() == '0'))
    {
        return false;
    }
    const auto conversion = std::from_chars(a_text.data(), a_text.data() + a_text.size(), a_value);
    return conversion.ec == std::errc{} && conversion.ptr == a_text.data() + a_text.size();
}

/// @brief Workspace JSON の State 名を公開 Enum へ変換する
[[nodiscard]] bool parse_locator_state(std::string_view a_text, cue::ProjectLocatorState &a_state) noexcept
{
    if (a_text == "available")
    {
        a_state = cue::ProjectLocatorState::Available;
        return true;
    }
    if (a_text == "missing")
    {
        a_state = cue::ProjectLocatorState::Missing;
        return true;
    }
    if (a_text == "moved")
    {
        a_state = cue::ProjectLocatorState::Moved;
        return true;
    }
    return false;
}

/// @brief 公開 Enum を Workspace JSON の canonical State 名へ変換する
[[nodiscard]] std::string_view locator_state_name(cue::ProjectLocatorState a_state) noexcept
{
    switch (a_state)
    {
    case cue::ProjectLocatorState::Available:
        return "available";
    case cue::ProjectLocatorState::Missing:
        return "missing";
    case cue::ProjectLocatorState::Moved:
        return "moved";
    }
    return "";
}

/// @brief Byte 列を Copy せず Workspace Parser へ渡す UTF-8 View へ変換する
[[nodiscard]] std::string_view bytes_as_string(std::span<const std::byte> a_bytes) noexcept
{
    return std::string_view(reinterpret_cast<const char *>(a_bytes.data()), a_bytes.size());
}
} // namespace

namespace cue
{
RecentProject::RecentProject(ProjectId &&a_projectId, std::string &&a_locator,
                             std::uint64_t a_lastOpenedMilliseconds, bool a_isPinned, std::uint64_t a_pinOrder,
                             std::uint64_t a_registrationOrder, ProjectLocatorState a_locatorState) noexcept
    : m_projectId(std::move(a_projectId)), m_locator(std::move(a_locator)),
      m_lastOpenedMilliseconds(a_lastOpenedMilliseconds), m_pinOrder(a_pinOrder),
      m_registrationOrder(a_registrationOrder), m_locatorState(a_locatorState), m_isPinned(a_isPinned)
{
}

const ProjectId &RecentProject::project_id() const noexcept
{
    return m_projectId;
}

std::string_view RecentProject::locator() const noexcept
{
    return m_locator;
}

std::uint64_t RecentProject::last_opened_milliseconds() const noexcept
{
    return m_lastOpenedMilliseconds;
}

bool RecentProject::is_pinned() const noexcept
{
    return m_isPinned;
}

std::uint64_t RecentProject::pin_order() const noexcept
{
    return m_pinOrder;
}

std::uint64_t RecentProject::registration_order() const noexcept
{
    return m_registrationOrder;
}

ProjectLocatorState RecentProject::locator_state() const noexcept
{
    return m_locatorState;
}

std::span<const RecentProject> RecentProjectRegistry::entries() const noexcept
{
    return m_entries;
}

Result<void> RecentProjectRegistry::register_project(const ProjectDescriptor &a_descriptor, std::string_view a_locator,
                                                     std::uint64_t a_lastOpenedMilliseconds,
                                                     const AssertContext &a_assertContext) noexcept
{
    try
    {
        if (!project_private::is_valid_json_text(a_locator, k_maximumLocatorBytes))
        {
            return Result<void>::failure(make_project_error(a_assertContext, ProjectError::InvalidProjectLocator,
                                                            "Project locator is invalid"));
        }
        const std::size_t existing = find_project(a_descriptor.project_id());
        if (existing != k_missingIndex)
        {
            if (m_entries[existing].locator() != a_locator)
            {
                return Result<void>::failure(make_project_error(
                    a_assertContext, ProjectError::DuplicateProjectId,
                    "ProjectId is already registered at a different locator"));
            }
            m_entries[existing].m_lastOpenedMilliseconds = a_lastOpenedMilliseconds;
            m_entries[existing].m_locatorState = ProjectLocatorState::Available;
            sort_entries();
            return Result<void>::success();
        }
        if (find_locator(a_locator) != k_missingIndex)
        {
            return Result<void>::failure(make_project_error(a_assertContext, ProjectError::ProjectLocatorConflict,
                                                            "Project locator belongs to another ProjectId"));
        }
        if (m_entries.size() >= project_private::k_maximumJsonContainerElements)
        {
            return Result<void>::failure(make_project_error(a_assertContext, ProjectError::InvalidWorkspaceFormat,
                                                            "Workspace entry limit is exceeded"));
        }
        if (m_nextRegistrationOrder == std::numeric_limits<std::uint64_t>::max())
        {
            return Result<void>::failure(make_project_error(
                a_assertContext, ProjectError::InvalidWorkspaceFormat, "Workspace registration order is exhausted"));
        }

        auto projectId = ProjectId::parse(a_descriptor.project_id().text(), a_assertContext);
        if (!projectId)
        {
            return Result<void>::failure(std::move(*projectId.try_error()));
        }
        std::string locator(a_locator);
        RecentProject entry(std::move(*projectId.try_value()), std::move(locator), a_lastOpenedMilliseconds, false,
                            0U, m_nextRegistrationOrder, ProjectLocatorState::Available);
        m_entries.push_back(std::move(entry));
        ++m_nextRegistrationOrder;
        sort_entries();
        return Result<void>::success();
    }
    catch (...)
    {
        terminate_registry_exception(a_assertContext);
    }
}

Result<void> RecentProjectRegistry::reassociate_project(const ProjectDescriptor &a_descriptor,
                                                        std::string_view a_locator,
                                                        std::uint64_t a_lastOpenedMilliseconds,
                                                        const AssertContext &a_assertContext) noexcept
{
    try
    {
        if (!project_private::is_valid_json_text(a_locator, k_maximumLocatorBytes))
        {
            return Result<void>::failure(make_project_error(a_assertContext, ProjectError::InvalidProjectLocator,
                                                            "Project locator is invalid"));
        }
        const std::size_t existing = find_project(a_descriptor.project_id());
        if (existing == k_missingIndex)
        {
            return Result<void>::failure(make_project_error(a_assertContext, ProjectError::ProjectNotRegistered,
                                                            "ProjectId is not registered"));
        }
        const std::size_t locatorOwner = find_locator(a_locator);
        if (locatorOwner != k_missingIndex && locatorOwner != existing)
        {
            return Result<void>::failure(make_project_error(a_assertContext, ProjectError::ProjectLocatorConflict,
                                                            "Project locator belongs to another ProjectId"));
        }

        const bool hasMoved = m_entries[existing].locator() != a_locator;
        std::string locator(a_locator);
        m_entries[existing].m_locator = std::move(locator);
        m_entries[existing].m_lastOpenedMilliseconds = a_lastOpenedMilliseconds;
        m_entries[existing].m_locatorState =
            hasMoved ? ProjectLocatorState::Moved : ProjectLocatorState::Available;
        sort_entries();
        return Result<void>::success();
    }
    catch (...)
    {
        terminate_registry_exception(a_assertContext);
    }
}

Result<void> RecentProjectRegistry::mark_project_missing(const ProjectId &a_projectId,
                                                         const AssertContext &a_assertContext) noexcept
{
    const std::size_t existing = find_project(a_projectId);
    if (existing == k_missingIndex)
    {
        return Result<void>::failure(make_project_error(a_assertContext, ProjectError::ProjectNotRegistered,
                                                        "ProjectId is not registered"));
    }
    m_entries[existing].m_locatorState = ProjectLocatorState::Missing;
    return Result<void>::success();
}

Result<void> RecentProjectRegistry::mark_project_available(const ProjectId &a_projectId,
                                                           const AssertContext &a_assertContext) noexcept
{
    const std::size_t existing = find_project(a_projectId);
    if (existing == k_missingIndex)
    {
        return Result<void>::failure(make_project_error(a_assertContext, ProjectError::ProjectNotRegistered,
                                                        "ProjectId is not registered"));
    }
    m_entries[existing].m_locatorState = ProjectLocatorState::Available;
    return Result<void>::success();
}

Result<void> RecentProjectRegistry::set_project_pinned(const ProjectId &a_projectId, bool a_isPinned,
                                                       const AssertContext &a_assertContext) noexcept
{
    const std::size_t existing = find_project(a_projectId);
    if (existing == k_missingIndex)
    {
        return Result<void>::failure(make_project_error(a_assertContext, ProjectError::ProjectNotRegistered,
                                                        "ProjectId is not registered"));
    }
    if (m_entries[existing].m_isPinned == a_isPinned)
    {
        return Result<void>::success();
    }
    if (a_isPinned && m_nextPinOrder == std::numeric_limits<std::uint64_t>::max())
    {
        return Result<void>::failure(make_project_error(a_assertContext, ProjectError::InvalidWorkspaceFormat,
                                                        "Workspace pin order is exhausted"));
    }
    m_entries[existing].m_isPinned = a_isPinned;
    m_entries[existing].m_pinOrder = a_isPinned ? m_nextPinOrder++ : 0U;
    sort_entries();
    return Result<void>::success();
}

Result<void> RecentProjectRegistry::move_pinned_project(const ProjectId &a_projectId, std::size_t a_targetIndex,
                                                        const AssertContext &a_assertContext) noexcept
{
    const std::size_t existing = find_project(a_projectId);
    const std::size_t pinnedCount = static_cast<std::size_t>(std::count_if(
        m_entries.begin(), m_entries.end(), [](const RecentProject &a_entry) { return a_entry.is_pinned(); }));
    if (existing == k_missingIndex)
    {
        return Result<void>::failure(make_project_error(a_assertContext, ProjectError::ProjectNotRegistered,
                                                        "ProjectId is not registered"));
    }
    if (!m_entries[existing].m_isPinned || a_targetIndex >= pinnedCount)
    {
        return Result<void>::failure(make_project_error(a_assertContext, ProjectError::InvalidPinOrder,
                                                        "Pinned project move target is invalid"));
    }
    if (existing < a_targetIndex)
    {
        std::rotate(m_entries.begin() + static_cast<std::ptrdiff_t>(existing),
                    m_entries.begin() + static_cast<std::ptrdiff_t>(existing + 1U),
                    m_entries.begin() + static_cast<std::ptrdiff_t>(a_targetIndex + 1U));
    }
    else if (existing > a_targetIndex)
    {
        std::rotate(m_entries.begin() + static_cast<std::ptrdiff_t>(a_targetIndex),
                    m_entries.begin() + static_cast<std::ptrdiff_t>(existing),
                    m_entries.begin() + static_cast<std::ptrdiff_t>(existing + 1U));
    }
    for (std::size_t index = 0U; index < pinnedCount; ++index)
    {
        m_entries[index].m_pinOrder = static_cast<std::uint64_t>(index + 1U);
    }
    m_nextPinOrder = static_cast<std::uint64_t>(pinnedCount + 1U);
    return Result<void>::success();
}

Result<void> RecentProjectRegistry::remove_project(const ProjectId &a_projectId,
                                                   const AssertContext &a_assertContext) noexcept
{
    const std::size_t existing = find_project(a_projectId);
    if (existing == k_missingIndex)
    {
        return Result<void>::failure(make_project_error(a_assertContext, ProjectError::ProjectNotRegistered,
                                                        "ProjectId is not registered"));
    }
    m_entries.erase(m_entries.begin() + static_cast<std::ptrdiff_t>(existing));
    return Result<void>::success();
}

std::size_t RecentProjectRegistry::find_project(const ProjectId &a_projectId) const noexcept
{
    for (std::size_t index = 0U; index < m_entries.size(); ++index)
    {
        if (m_entries[index].project_id() == a_projectId)
        {
            return index;
        }
    }
    return k_missingIndex;
}

std::size_t RecentProjectRegistry::find_locator(std::string_view a_locator) const noexcept
{
    for (std::size_t index = 0U; index < m_entries.size(); ++index)
    {
        if (m_entries[index].locator() == a_locator)
        {
            return index;
        }
    }
    return k_missingIndex;
}

void RecentProjectRegistry::sort_entries() noexcept
{
    std::sort(m_entries.begin(), m_entries.end(), comes_before);
}

bool RecentProjectRegistry::comes_before(const RecentProject &a_left, const RecentProject &a_right) noexcept
{
    if (a_left.m_isPinned != a_right.m_isPinned)
    {
        return a_left.m_isPinned;
    }
    if (a_left.m_isPinned && a_left.m_pinOrder != a_right.m_pinOrder)
    {
        return a_left.m_pinOrder < a_right.m_pinOrder;
    }
    if (!a_left.m_isPinned && a_left.m_lastOpenedMilliseconds != a_right.m_lastOpenedMilliseconds)
    {
        return a_left.m_lastOpenedMilliseconds > a_right.m_lastOpenedMilliseconds;
    }
    return a_left.m_registrationOrder < a_right.m_registrationOrder;
}

bool RecentProjectRegistry::is_valid(const AssertContext &a_assertContext) const noexcept
{
    try
    {
        std::uint64_t maximumRegistrationOrder = 0U;
        std::uint64_t maximumPinOrder = 0U;
        for (std::size_t index = 0U; index < m_entries.size(); ++index)
        {
            const RecentProject &entry = m_entries[index];
            auto projectId = ProjectId::parse(entry.project_id().text(), a_assertContext);
            if (!projectId || !project_private::is_valid_json_text(entry.locator(), k_maximumLocatorBytes) ||
                (entry.is_pinned() && entry.pin_order() == 0U) || (!entry.is_pinned() && entry.pin_order() != 0U))
            {
                return false;
            }
            maximumPinOrder = std::max(maximumPinOrder, entry.pin_order());
            for (std::size_t other = index + 1U; other < m_entries.size(); ++other)
            {
                if (entry.project_id() == m_entries[other].project_id() ||
                    entry.locator() == m_entries[other].locator() ||
                    (entry.is_pinned() && m_entries[other].is_pinned() &&
                     entry.pin_order() == m_entries[other].pin_order()) ||
                    entry.m_registrationOrder == m_entries[other].m_registrationOrder)
                {
                    return false;
                }
            }
            maximumRegistrationOrder = std::max(maximumRegistrationOrder, entry.m_registrationOrder);
        }
        return m_nextRegistrationOrder > maximumRegistrationOrder && m_nextPinOrder > maximumPinOrder;
    }
    catch (...)
    {
        terminate_registry_exception(a_assertContext);
    }
}

Result<RecentProjectRegistry> parse_recent_project_registry(std::string_view a_json,
                                                            const AssertContext &a_assertContext) noexcept
{
    try
    {
        if (a_json.size() > k_maximumWorkspaceBytes || a_json.starts_with("\xEF\xBB\xBF"))
        {
            return Result<RecentProjectRegistry>::failure(make_project_error(
                a_assertContext, ProjectError::InvalidWorkspaceFormat, "Workspace file size or encoding is invalid"));
        }
        project_private::JsonValue root;
        std::string_view parseError;
        if (!project_private::parse_json_document(a_json, root, parseError))
        {
            return Result<RecentProjectRegistry>::failure(
                make_project_error(a_assertContext, ProjectError::InvalidWorkspaceFormat, parseError));
        }
        if (root.type != JsonType::Object)
        {
            return Result<RecentProjectRegistry>::failure(make_project_error(
                a_assertContext, ProjectError::InvalidWorkspaceFormat, "Workspace root must be a JSON object"));
        }

        const JsonValue *schema = project_private::find_json_member(root, "schemaVersion");
        std::uint64_t schemaVersion = 0U;
        if (schema == nullptr || schema->type != JsonType::Number ||
            !parse_canonical_u64(schema->text, schemaVersion))
        {
            return Result<RecentProjectRegistry>::failure(make_project_error(
                a_assertContext, ProjectError::InvalidWorkspaceFormat, "Workspace schemaVersion is invalid"));
        }
        if (schemaVersion != k_workspaceSchemaVersion)
        {
            return Result<RecentProjectRegistry>::failure(
                make_project_error(a_assertContext, ProjectError::UnsupportedWorkspaceVersion,
                                   "Workspace schemaVersion is unsupported"));
        }
        constexpr std::array rootNames = {std::string_view("schemaVersion"),
                                          std::string_view("nextRegistrationOrder"),
                                          std::string_view("nextPinOrder"), std::string_view("entries")};
        if (!project_private::has_exact_json_members(root, rootNames))
        {
            return Result<RecentProjectRegistry>::failure(make_project_error(
                a_assertContext, ProjectError::InvalidWorkspaceFormat, "Workspace root members are invalid"));
        }

        const JsonValue &nextRegistration = *project_private::find_json_member(root, "nextRegistrationOrder");
        const JsonValue &nextPin = *project_private::find_json_member(root, "nextPinOrder");
        const JsonValue &entries = *project_private::find_json_member(root, "entries");
        RecentProjectRegistry registry;
        if (nextRegistration.type != JsonType::Number || nextPin.type != JsonType::Number ||
            !parse_canonical_u64(nextRegistration.text, registry.m_nextRegistrationOrder) ||
            !parse_canonical_u64(nextPin.text, registry.m_nextPinOrder) || registry.m_nextRegistrationOrder == 0U ||
            registry.m_nextPinOrder == 0U || entries.type != JsonType::Array)
        {
            return Result<RecentProjectRegistry>::failure(make_project_error(
                a_assertContext, ProjectError::InvalidWorkspaceFormat, "Workspace counters or entries are invalid"));
        }

        constexpr std::array entryNames = {
            std::string_view("projectId"),         std::string_view("locator"),
            std::string_view("lastOpenedMillis"), std::string_view("isPinned"),
            std::string_view("pinOrder"),         std::string_view("registrationOrder"),
            std::string_view("locatorState")};
        for (const JsonValue &value : entries.elements)
        {
            if (!project_private::has_exact_json_members(value, entryNames))
            {
                return Result<RecentProjectRegistry>::failure(make_project_error(
                    a_assertContext, ProjectError::InvalidWorkspaceFormat, "Workspace entry members are invalid"));
            }
            const JsonValue &projectIdValue = *project_private::find_json_member(value, "projectId");
            const JsonValue &locatorValue = *project_private::find_json_member(value, "locator");
            const JsonValue &lastOpenedValue = *project_private::find_json_member(value, "lastOpenedMillis");
            const JsonValue &isPinnedValue = *project_private::find_json_member(value, "isPinned");
            const JsonValue &pinOrderValue = *project_private::find_json_member(value, "pinOrder");
            const JsonValue &registrationValue = *project_private::find_json_member(value, "registrationOrder");
            const JsonValue &stateValue = *project_private::find_json_member(value, "locatorState");
            std::uint64_t lastOpened = 0U;
            std::uint64_t pinOrder = 0U;
            std::uint64_t registrationOrder = 0U;
            ProjectLocatorState state = ProjectLocatorState::Available;
            if (projectIdValue.type != JsonType::String || locatorValue.type != JsonType::String ||
                !project_private::is_valid_json_text(locatorValue.text, k_maximumLocatorBytes) ||
                lastOpenedValue.type != JsonType::Number || !parse_canonical_u64(lastOpenedValue.text, lastOpened) ||
                isPinnedValue.type != JsonType::Boolean || pinOrderValue.type != JsonType::Number ||
                !parse_canonical_u64(pinOrderValue.text, pinOrder) || registrationValue.type != JsonType::Number ||
                !parse_canonical_u64(registrationValue.text, registrationOrder) || registrationOrder == 0U ||
                stateValue.type != JsonType::String || !parse_locator_state(stateValue.text, state) ||
                (isPinnedValue.boolean && pinOrder == 0U) || (!isPinnedValue.boolean && pinOrder != 0U))
            {
                return Result<RecentProjectRegistry>::failure(make_project_error(
                    a_assertContext, ProjectError::InvalidWorkspaceFormat, "Workspace entry value is invalid"));
            }
            auto projectId = ProjectId::parse(projectIdValue.text, a_assertContext);
            if (!projectId)
            {
                return Result<RecentProjectRegistry>::failure(std::move(*projectId.try_error()));
            }
            RecentProject entry(std::move(*projectId.try_value()), std::string(locatorValue.text), lastOpened,
                                isPinnedValue.boolean, pinOrder, registrationOrder, state);
            registry.m_entries.push_back(std::move(entry));
        }
        registry.sort_entries();
        if (!registry.is_valid(a_assertContext))
        {
            return Result<RecentProjectRegistry>::failure(make_project_error(
                a_assertContext, ProjectError::InvalidWorkspaceFormat, "Workspace registry invariants are invalid"));
        }
        return Result<RecentProjectRegistry>::success(std::move(registry));
    }
    catch (...)
    {
        terminate_registry_exception(a_assertContext);
    }
}

Result<std::string> serialize_recent_project_registry(const RecentProjectRegistry &a_registry,
                                                      const AssertContext &a_assertContext) noexcept
{
    try
    {
        if (a_registry.m_entries.size() > project_private::k_maximumJsonContainerElements)
        {
            return Result<std::string>::failure(make_project_error(
                a_assertContext, ProjectError::InvalidWorkspaceFormat, "Workspace entry limit is exceeded"));
        }
        if (!a_registry.is_valid(a_assertContext))
        {
            return Result<std::string>::failure(make_project_error(
                a_assertContext, ProjectError::InvalidWorkspaceFormat, "Workspace registry invariants are invalid"));
        }
        std::string output;
        output.reserve(128U + a_registry.m_entries.size() * 192U);
        output.append("{\"schemaVersion\":1,\"nextRegistrationOrder\":");
        output.append(std::to_string(a_registry.m_nextRegistrationOrder));
        output.append(",\"nextPinOrder\":");
        output.append(std::to_string(a_registry.m_nextPinOrder));
        output.append(",\"entries\":[");
        for (std::size_t index = 0U; index < a_registry.m_entries.size(); ++index)
        {
            if (index != 0U)
            {
                output.push_back(',');
            }
            const RecentProject &entry = a_registry.m_entries[index];
            output.append("{\"projectId\":");
            project_private::write_json_string(output, entry.project_id().text());
            output.append(",\"locator\":");
            project_private::write_json_string(output, entry.locator());
            output.append(",\"lastOpenedMillis\":");
            output.append(std::to_string(entry.last_opened_milliseconds()));
            output.append(",\"isPinned\":");
            output.append(entry.is_pinned() ? "true" : "false");
            output.append(",\"pinOrder\":");
            output.append(std::to_string(entry.pin_order()));
            output.append(",\"registrationOrder\":");
            output.append(std::to_string(entry.registration_order()));
            output.append(",\"locatorState\":");
            project_private::write_json_string(output, locator_state_name(entry.locator_state()));
            output.push_back('}');
        }
        output.append("]}");
        if (output.size() > k_maximumWorkspaceBytes)
        {
            return Result<std::string>::failure(make_project_error(
                a_assertContext, ProjectError::InvalidWorkspaceFormat, "Workspace serialization exceeds 1 MiB"));
        }
        return Result<std::string>::success(std::move(output));
    }
    catch (...)
    {
        terminate_registry_exception(a_assertContext);
    }
}

Result<RecentProjectRegistry> load_recent_project_registry(FilesystemRoot &a_workspaceFilesystem,
                                                           const AssertContext &a_assertContext) noexcept
{
    auto path = RelativePath::parse("CueWorkspace.json", a_assertContext);
    if (!path)
    {
        return Result<RecentProjectRegistry>::failure(std::move(*path.try_error()));
    }
    auto type = a_workspaceFilesystem.query_entry(*path.try_value());
    if (!type)
    {
        return Result<RecentProjectRegistry>::failure(reclassify_io_error(
            a_assertContext, "Failed to query CueWorkspace.json", std::move(*type.try_error())));
    }
    if (*type.try_value() == EntryType::Missing)
    {
        return Result<RecentProjectRegistry>::success(RecentProjectRegistry{});
    }
    if (*type.try_value() != EntryType::RegularFile)
    {
        return Result<RecentProjectRegistry>::failure(make_project_error(
            a_assertContext, ProjectError::InvalidWorkspaceFormat, "CueWorkspace.json is not a regular file"));
    }
    auto bytes = a_workspaceFilesystem.read_file(*path.try_value(), k_maximumWorkspaceBytes);
    if (!bytes)
    {
        return Result<RecentProjectRegistry>::failure(reclassify_io_error(
            a_assertContext, "Failed to read CueWorkspace.json", std::move(*bytes.try_error())));
    }
    return parse_recent_project_registry(bytes_as_string(*bytes.try_value()), a_assertContext);
}

Result<void> save_recent_project_registry(FilesystemRoot &a_workspaceFilesystem,
                                          const RecentProjectRegistry &a_registry,
                                          const AssertContext &a_assertContext) noexcept
{
    auto path = RelativePath::parse("CueWorkspace.json", a_assertContext);
    if (!path)
    {
        return Result<void>::failure(std::move(*path.try_error()));
    }
    auto serialized = serialize_recent_project_registry(a_registry, a_assertContext);
    if (!serialized)
    {
        return Result<void>::failure(std::move(*serialized.try_error()));
    }
    const std::span<const char> characters(serialized.try_value()->data(), serialized.try_value()->size());
    auto written = a_workspaceFilesystem.write_file_atomic(*path.try_value(), std::as_bytes(characters));
    if (!written)
    {
        return Result<void>::failure(reclassify_io_error(
            a_assertContext, "Failed to atomically save CueWorkspace.json", std::move(*written.try_error())));
    }
    return Result<void>::success();
}
} // namespace cue

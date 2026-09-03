#include <Cue/EditorCore/EditorController.h>

#include <Cue/EditorCore/Error.h>
#include <Cue/Foundation/Assert.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <new>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace cue::editor_core
{
namespace
{
/// @brief Command Error へ対象 Editor Document Identity を付加する
void add_document_context(Error &a_error, const AssertContext &a_assertContext, EditorDocumentId a_documentId) noexcept
{
    constexpr std::string_view prefix = "EditorDocumentId=";
    std::array<char, prefix.size() + 20U> context{};
    std::copy(prefix.begin(), prefix.end(), context.begin());
    const auto converted =
        std::to_chars(context.data() + prefix.size(), context.data() + context.size(), a_documentId.value());
    if (converted.ec != std::errc{})
    {
        a_assertContext.fatal_handler().terminate("Cue.EditorCore command document identity formatting failed");
    }
    a_error.add_context(a_assertContext.fatal_handler(),
                        std::string_view(context.data(), static_cast<std::size_t>(converted.ptr - context.data())));
}

/// @brief Command Error へ Canonical Text を持つ Stable Identity を付加する
template <typename Identity>
void add_identity_context(Error &a_error, const AssertContext &a_assertContext, std::string_view a_prefix,
                          const Identity &a_identity) noexcept
{
    const scene::IdentityText identity = a_identity.canonical_text();
    std::array<char, 64U> context{};
    if (a_prefix.size() + identity.size() > context.size())
    {
        a_assertContext.fatal_handler().terminate("Cue.EditorCore command scene identity context is too large");
    }
    std::copy(a_prefix.begin(), a_prefix.end(), context.begin());
    std::copy(identity.begin(), identity.end(), context.begin() + static_cast<std::ptrdiff_t>(a_prefix.size()));
    a_error.add_context(a_assertContext.fatal_handler(),
                        std::string_view(context.data(), a_prefix.size() + identity.size()));
}

/// @brief Command Error へ具体 Command の主要 Object／Component Identity を付加する
void add_command_target_context(Error &a_error, const AssertContext &a_assertContext,
                                const SceneEditCommand &a_command) noexcept
{
    std::visit(
        /// @brief 具体 Command が直接対象とする Stable Identity を診断へ追加する
        [&a_error, &a_assertContext](const auto &a_typedCommand) noexcept
        {
            using Command = std::remove_cvref_t<decltype(a_typedCommand)>;
            if constexpr (std::is_same_v<Command, DuplicateObjectCommand>)
            {
                add_identity_context(a_error, a_assertContext, "ObjectId=", a_typedCommand.sourceRootId);
            }
            else
            {
                add_identity_context(a_error, a_assertContext, "ObjectId=", a_typedCommand.objectId);
                if constexpr (std::is_same_v<Command, AddComponentCommand>)
                {
                    add_identity_context(a_error, a_assertContext,
                                         "ComponentInstanceId=", a_typedCommand.component.instance_id());
                }
                else if constexpr (std::is_same_v<Command, RemoveComponentCommand> ||
                                   std::is_same_v<Command, EditFieldCommand>)
                {
                    add_identity_context(a_error, a_assertContext, "ComponentInstanceId=", a_typedCommand.componentId);
                }
            }
        },
        a_command);
}

/// @brief Command Error へ公開 Request の Document と Scene Identity を付加する
Error add_request_context(Error a_error, const AssertContext &a_assertContext,
                          const SceneCommandRequest &a_request) noexcept
{
    add_document_context(a_error, a_assertContext, a_request.documentId);
    add_identity_context(a_error, a_assertContext, "SceneAssetId=", a_request.sceneAssetId);
    add_command_target_context(a_error, a_assertContext, a_request.command);
    return a_error;
}

/// @brief Candidate Object が指定 Root 自身またはその Descendant か判定する
[[nodiscard]] bool belongs_to_subtree(const scene::SceneDocument &a_document, const scene::ObjectId &a_candidateId,
                                      const scene::ObjectId &a_rootId) noexcept
{
    const scene::SceneObject *current = a_document.find_object(a_candidateId);
    for (std::size_t depth = 0U; current != nullptr && depth <= scene::SceneDocument::maximum_hierarchy_depth();
         ++depth)
    {
        if (current->id() == a_rootId)
        {
            return true;
        }
        const scene::ObjectId *parentId = current->try_parent_id();
        current = parentId != nullptr ? a_document.find_object(*parentId) : nullptr;
    }
    return false;
}

/// @brief 指定 Root を含む Subtree の Stable Object Identity を収集する
[[nodiscard]] Result<std::vector<scene::ObjectId>> collect_subtree(const scene::SceneDocument &a_document,
                                                                   const scene::ObjectId &a_rootId,
                                                                   const AssertContext &a_assertContext) noexcept
{
    if (a_document.find_object(a_rootId) == nullptr)
    {
        return Result<std::vector<scene::ObjectId>>::failure(make_editor_core_error(
            a_assertContext, EditorCoreError::InvalidCommand, "Scene command root object was not found"));
    }

    std::vector<scene::ObjectId> result;
    result.reserve(a_document.object_count());
    for (const scene::SceneObject &object : a_document.objects())
    {
        if (belongs_to_subtree(a_document, object.id(), a_rootId))
        {
            result.push_back(object.id());
        }
    }
    return Result<std::vector<scene::ObjectId>>::success(std::move(result));
}

/// @brief Child を先に除去して一つの Object Subtree を削除する
[[nodiscard]] Result<void> delete_subtree(scene::SceneDocument &a_document, const DeleteObjectCommand &a_command,
                                          const AssertContext &a_assertContext) noexcept
{
    auto collected = collect_subtree(a_document, a_command.objectId, a_assertContext);
    if (!collected)
    {
        return Result<void>::failure(std::move(*collected.try_error()));
    }
    std::vector<scene::ObjectId> remaining = std::move(*collected.try_value());
    while (!remaining.empty())
    {
        bool removed = false;
        for (auto candidate = remaining.begin(); candidate != remaining.end(); ++candidate)
        {
            const bool hasRemainingChild =
                std::any_of(remaining.begin(), remaining.end(),
                            /// @brief Remaining Object が Candidate を Parent として参照するか判定する
                            [&a_document, &candidate](const scene::ObjectId &a_otherId) noexcept
                            {
                                const scene::SceneObject *other = a_document.find_object(a_otherId);
                                const scene::ObjectId *parentId = other != nullptr ? other->try_parent_id() : nullptr;
                                return parentId != nullptr && *parentId == *candidate;
                            });
            if (hasRemainingChild)
            {
                continue;
            }

            auto result = a_document.remove_object(*candidate);
            if (!result)
            {
                return result;
            }
            remaining.erase(candidate);
            removed = true;
            break;
        }
        if (!removed)
        {
            return Result<void>::failure(
                make_editor_core_error(a_assertContext, EditorCoreError::InvalidCommand,
                                       "Scene command subtree could not produce a leaf removal order"));
        }
    }
    return Result<void>::success();
}

/// @brief Duplicate Request が Source Subtree を過不足なく一意に対応付けるか検証する
[[nodiscard]] Result<std::vector<std::size_t>> validate_duplicate_targets(const scene::SceneDocument &a_document,
                                                                          const DuplicateObjectCommand &a_command,
                                                                          std::span<const scene::ObjectId> a_sourceIds,
                                                                          const AssertContext &a_assertContext) noexcept
{
    if (a_command.targets.size() != a_sourceIds.size())
    {
        return Result<std::vector<std::size_t>>::failure(
            make_editor_core_error(a_assertContext, EditorCoreError::InvalidCommand,
                                   "Duplicate command must map every source subtree object exactly once"));
    }

    constexpr std::size_t unmapped = static_cast<std::size_t>(-1);
    std::vector<std::size_t> sourceTargets(a_sourceIds.size(), unmapped);
    std::vector<scene::ObjectId> duplicateIds;
    std::vector<scene::ComponentInstanceId> componentIds;
    duplicateIds.reserve(a_command.targets.size());
    for (std::size_t index = 0U; index < a_command.targets.size(); ++index)
    {
        const DuplicateObjectTarget &target = a_command.targets[index];
        const auto sourcePosition = std::find(a_sourceIds.begin(), a_sourceIds.end(), target.sourceObjectId);
        if (sourcePosition == a_sourceIds.end() ||
            sourceTargets[static_cast<std::size_t>(sourcePosition - a_sourceIds.begin())] != unmapped ||
            a_document.find_object(target.duplicateObjectId) != nullptr ||
            std::find(duplicateIds.begin(), duplicateIds.end(), target.duplicateObjectId) != duplicateIds.end())
        {
            return Result<std::vector<std::size_t>>::failure(
                make_editor_core_error(a_assertContext, EditorCoreError::InvalidCommand,
                                       "Duplicate command contains an invalid or repeated object identity mapping"));
        }
        sourceTargets[static_cast<std::size_t>(sourcePosition - a_sourceIds.begin())] = index;
        duplicateIds.push_back(target.duplicateObjectId);

        const scene::SceneObject *source = a_document.find_object(target.sourceObjectId);
        if (source == nullptr || source->components().size() != target.duplicateComponentIds.size())
        {
            return Result<std::vector<std::size_t>>::failure(
                make_editor_core_error(a_assertContext, EditorCoreError::InvalidCommand,
                                       "Duplicate command must provide one new identity for every source component"));
        }
        for (const scene::ComponentInstanceId &componentId : target.duplicateComponentIds)
        {
            if (std::find(componentIds.begin(), componentIds.end(), componentId) != componentIds.end())
            {
                return Result<std::vector<std::size_t>>::failure(
                    make_editor_core_error(a_assertContext, EditorCoreError::InvalidCommand,
                                           "Duplicate command contains a repeated component identity"));
            }
            componentIds.push_back(componentId);
        }
    }
    return Result<std::vector<std::size_t>>::success(std::move(sourceTargets));
}

/// @brief Source Hierarchy 順に Object と既知 Component 値を複製する
[[nodiscard]] Result<void> duplicate_subtree(scene::SceneDocument &a_document, const DuplicateObjectCommand &a_command,
                                             const AssertContext &a_assertContext) noexcept
{
    auto collected = collect_subtree(a_document, a_command.sourceRootId, a_assertContext);
    if (!collected)
    {
        return Result<void>::failure(std::move(*collected.try_error()));
    }
    std::vector<scene::ObjectId> sourceIds = std::move(*collected.try_value());
    auto validated = validate_duplicate_targets(a_document, a_command, sourceIds, a_assertContext);
    if (!validated)
    {
        return Result<void>::failure(std::move(*validated.try_error()));
    }
    const std::vector<std::size_t> &sourceTargets = *validated.try_value();

    std::vector<bool> created(a_command.targets.size(), false);
    std::size_t createdCount = 0U;
    while (createdCount < a_command.targets.size())
    {
        bool progressed = false;
        for (std::size_t index = 0U; index < a_command.targets.size(); ++index)
        {
            if (created[index])
            {
                continue;
            }
            const DuplicateObjectTarget &target = a_command.targets[index];
            const scene::SceneObject *source = a_document.find_object(target.sourceObjectId);
            if (source == nullptr)
            {
                return Result<void>::failure(
                    make_editor_core_error(a_assertContext, EditorCoreError::InvalidCommand,
                                           "Duplicate command source object disappeared during execution"));
            }

            std::optional<scene::ObjectId> parentId;
            if (const scene::ObjectId *sourceParentId = source->try_parent_id(); sourceParentId != nullptr)
            {
                const auto sourceParent = std::find(sourceIds.begin(), sourceIds.end(), *sourceParentId);
                if (sourceParent != sourceIds.end())
                {
                    const std::size_t sourceIndex = static_cast<std::size_t>(sourceParent - sourceIds.begin());
                    const std::size_t targetIndex = sourceTargets[sourceIndex];
                    if (!created[targetIndex])
                    {
                        continue;
                    }
                    parentId = a_command.targets[targetIndex].duplicateObjectId;
                }
                else
                {
                    parentId = *sourceParentId;
                }
            }

            const bool isActive = source->is_active();
            const math::Transform transform = source->transform();
            std::vector<scene::SceneComponent> components(source->components().begin(), source->components().end());
            auto added =
                a_document.add_object(target.duplicateObjectId, target.name, isActive, std::move(parentId), transform);
            if (!added)
            {
                return added;
            }
            for (std::size_t componentIndex = 0U; componentIndex < components.size(); ++componentIndex)
            {
                auto duplicate = components[componentIndex].duplicate_with_identity(
                    target.duplicateComponentIds[componentIndex], a_assertContext);
                if (!duplicate)
                {
                    return Result<void>::failure(std::move(*duplicate.try_error()));
                }
                auto componentAdded =
                    a_document.add_component(target.duplicateObjectId, std::move(*duplicate.try_value()));
                if (!componentAdded)
                {
                    return componentAdded;
                }
            }

            created[index] = true;
            ++createdCount;
            progressed = true;
        }
        if (!progressed)
        {
            return Result<void>::failure(
                make_editor_core_error(a_assertContext, EditorCoreError::InvalidCommand,
                                       "Duplicate command source hierarchy could not produce a parent-first order"));
        }
    }
    return Result<void>::success();
}

/// @brief 一つの型付き Scene Edit Command を Authoring Scene へ適用する
[[nodiscard]] Result<void> apply_command(scene::SceneDocument &a_document, SceneEditCommand &a_command,
                                         const AssertContext &a_assertContext) noexcept
{
    return std::visit(
        /// @brief Variant の具体 Command を対応する SceneDocument Mutation へ変換する
        [&a_document, &a_assertContext](auto &a_typedCommand) -> Result<void>
        {
            using Command = std::remove_cvref_t<decltype(a_typedCommand)>;
            if constexpr (std::is_same_v<Command, AddObjectCommand>)
            {
                return a_document.add_object(a_typedCommand.objectId, a_typedCommand.name, a_typedCommand.isActive,
                                             a_typedCommand.parentId, a_typedCommand.transform);
            }
            else if constexpr (std::is_same_v<Command, DeleteObjectCommand>)
            {
                return delete_subtree(a_document, a_typedCommand, a_assertContext);
            }
            else if constexpr (std::is_same_v<Command, DuplicateObjectCommand>)
            {
                return duplicate_subtree(a_document, a_typedCommand, a_assertContext);
            }
            else if constexpr (std::is_same_v<Command, RenameObjectCommand>)
            {
                return a_document.rename_object(a_typedCommand.objectId, a_typedCommand.name);
            }
            else if constexpr (std::is_same_v<Command, ReparentObjectCommand>)
            {
                return a_document.set_parent(a_typedCommand.objectId, a_typedCommand.parentId);
            }
            else if constexpr (std::is_same_v<Command, AddComponentCommand>)
            {
                return a_document.add_component(a_typedCommand.objectId, a_typedCommand.component);
            }
            else if constexpr (std::is_same_v<Command, RemoveComponentCommand>)
            {
                return a_document.remove_component(a_typedCommand.objectId, a_typedCommand.componentId);
            }
            else if constexpr (std::is_same_v<Command, EditFieldCommand>)
            {
                return a_document.set_component_field(a_typedCommand.objectId, a_typedCommand.componentId,
                                                      a_typedCommand.fieldId, std::move(a_typedCommand.value));
            }
            else
            {
                return a_document.set_transform(a_typedCommand.objectId, a_typedCommand.transform);
            }
        },
        a_command);
}

/// @brief 二つの Core Transform が同じ永続値を表すか判定する
[[nodiscard]] bool is_same_transform(const math::Transform &a_left, const math::Transform &a_right) noexcept
{
    return a_left.translation() == a_right.translation() && a_left.rotation() == a_right.rotation() &&
           a_left.scale() == a_right.scale();
}

/// @brief Command が現在の Authoring Data と同値で Revision を必要としないか判定する
[[nodiscard]] bool is_noop_command(const scene::SceneDocument &a_document, const SceneEditCommand &a_command) noexcept
{
    return std::visit(
        /// @brief 同値判定可能な更新 Command だけを現在の Authoring Data と比較する
        [&a_document](const auto &a_typedCommand) noexcept
        {
            using Command = std::remove_cvref_t<decltype(a_typedCommand)>;
            if constexpr (std::is_same_v<Command, RenameObjectCommand>)
            {
                const scene::SceneObject *object = a_document.find_object(a_typedCommand.objectId);
                return object != nullptr && object->name() == a_typedCommand.name;
            }
            else if constexpr (std::is_same_v<Command, ReparentObjectCommand>)
            {
                const scene::SceneObject *object = a_document.find_object(a_typedCommand.objectId);
                if (object == nullptr)
                {
                    return false;
                }
                const scene::ObjectId *parentId = object->try_parent_id();
                return parentId != nullptr ? a_typedCommand.parentId && *parentId == *a_typedCommand.parentId
                                           : !a_typedCommand.parentId;
            }
            else if constexpr (std::is_same_v<Command, EditFieldCommand>)
            {
                const scene::SceneObject *object = a_document.find_object(a_typedCommand.objectId);
                if (object == nullptr)
                {
                    return false;
                }
                for (const scene::SceneComponent &component : object->components())
                {
                    if (component.instance_id() != a_typedCommand.componentId)
                    {
                        continue;
                    }
                    const scene::KnownComponentData *known = component.try_known();
                    if (known == nullptr)
                    {
                        return false;
                    }
                    for (const scene::KnownFieldData &field : known->known_fields())
                    {
                        if (field.id() == a_typedCommand.fieldId)
                        {
                            return field.value() == a_typedCommand.value;
                        }
                    }
                    return false;
                }
                return false;
            }
            else if constexpr (std::is_same_v<Command, EditTransformCommand>)
            {
                const scene::SceneObject *object = a_document.find_object(a_typedCommand.objectId);
                return object != nullptr && is_same_transform(object->transform(), a_typedCommand.transform);
            }
            else
            {
                return false;
            }
        },
        a_command);
}
} // namespace

Result<DocumentStateId> EditorController::execute_command(SceneCommandRequest a_request) noexcept
{
    assert_owner_thread();
    EditorDocument *document = find_document(a_request.documentId);
    if (document == nullptr)
    {
        return Result<DocumentStateId>::failure(
            make_editor_document_error(*m_assertContext, EditorCoreError::DocumentNotFound,
                                       "Editor document was not found", a_request.documentId.value()));
    }
    if (document->m_closeState != DocumentCloseState::Open)
    {
        return Result<DocumentStateId>::failure(
            make_editor_document_error(*m_assertContext, EditorCoreError::InvalidDocumentState,
                                       "Scene commands require an open editor document", a_request.documentId.value()));
    }
    if (document->m_document.scene_asset_id() != a_request.sceneAssetId)
    {
        Error error = make_editor_document_error(*m_assertContext, EditorCoreError::SceneMismatch,
                                                 "Scene command target does not match the editor document",
                                                 a_request.documentId.value());
        add_identity_context(error, *m_assertContext, "RequestedSceneAssetId=", a_request.sceneAssetId);
        add_identity_context(error, *m_assertContext, "DocumentSceneAssetId=", document->m_document.scene_asset_id());
        return Result<DocumentStateId>::failure(std::move(error));
    }
    if (is_noop_command(document->m_document, a_request.command))
    {
        DocumentStateId state = document->m_currentStateId;
        return Result<DocumentStateId>::success(std::move(state));
    }

    scene::SceneDocumentCheckpoint checkpoint = document->m_document.create_checkpoint();
    try
    {
        auto applied = apply_command(document->m_document, a_request.command, *m_assertContext);
        if (!applied)
        {
            Error error = add_request_context(std::move(*applied.try_error()), *m_assertContext, a_request);
            auto restored = document->m_document.restore_checkpoint(std::move(checkpoint));
            if (!restored)
            {
                m_assertContext->fatal_handler().terminate("Cue.EditorCore command rollback failed");
            }
            return Result<DocumentStateId>::failure(std::move(error));
        }

        auto validated = document->m_document.validate();
        if (!validated)
        {
            Error error = add_request_context(std::move(*validated.try_error()), *m_assertContext, a_request);
            auto restored = document->m_document.restore_checkpoint(std::move(checkpoint));
            if (!restored)
            {
                m_assertContext->fatal_handler().terminate("Cue.EditorCore command validation rollback failed");
            }
            return Result<DocumentStateId>::failure(std::move(error));
        }

        auto state = record_persistent_change(a_request.documentId);
        if (!state)
        {
            Error error = std::move(*state.try_error());
            auto restored = document->m_document.restore_checkpoint(std::move(checkpoint));
            if (!restored)
            {
                m_assertContext->fatal_handler().terminate("Cue.EditorCore command revision rollback failed");
            }
            return Result<DocumentStateId>::failure(std::move(error));
        }
        return state;
    }
    catch (const std::bad_alloc &)
    {
        terminate_allocation();
    }
    catch (...)
    {
        terminate_exception();
    }
}
} // namespace cue::editor_core

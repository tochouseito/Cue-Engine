#include <Cue/Scene/SceneDocument.h>

#include "Json.h"

#include <Cue/Foundation/Assert.h>
#include <Cue/Scene/Error.h>

#include <algorithm>
#include <utility>

namespace cue::scene
{
SceneObject::SceneObject(ObjectId a_id, std::string a_name, bool a_isActive, std::optional<ObjectId> a_parentId,
                         math::Transform a_transform, std::vector<SceneComponent> a_components) noexcept
    : m_id(std::move(a_id)), m_name(std::move(a_name)), m_isActive(a_isActive), m_parentId(std::move(a_parentId)),
      m_transform(std::move(a_transform)), m_components(std::move(a_components))
{
}

const ObjectId &SceneObject::id() const noexcept
{
    return m_id;
}

std::string_view SceneObject::name() const noexcept
{
    return m_name;
}

bool SceneObject::is_active() const noexcept
{
    return m_isActive;
}

const ObjectId *SceneObject::try_parent_id() const noexcept
{
    return m_parentId ? &m_parentId.value() : nullptr;
}

const math::Transform &SceneObject::transform() const noexcept
{
    return m_transform;
}

std::span<const SceneComponent> SceneObject::components() const noexcept
{
    return m_components;
}

SceneDocumentCheckpoint::SceneDocumentCheckpoint(
    SceneAssetId a_sceneAssetId, std::vector<SceneObject> a_objects,
    std::string a_extensionsJson) noexcept
    : m_sceneAssetId(std::move(a_sceneAssetId)),
      m_objects(std::move(a_objects)),
      m_extensionsJson(std::move(a_extensionsJson))
{
}

SceneDocumentCheckpoint::SceneDocumentCheckpoint(SceneDocumentCheckpoint &&a_other) noexcept
    : m_sceneAssetId(std::move(a_other.m_sceneAssetId)),
      m_objects(std::move(a_other.m_objects)),
      m_extensionsJson(std::move(a_other.m_extensionsJson)),
      m_isValid(a_other.m_isValid)
{
    a_other.m_isValid = false;
}

SceneDocumentCheckpoint &SceneDocumentCheckpoint::operator=(SceneDocumentCheckpoint &&a_other) noexcept
{
    if (this != &a_other)
    {
        m_sceneAssetId = std::move(a_other.m_sceneAssetId);
        m_objects = std::move(a_other.m_objects);
        m_extensionsJson = std::move(a_other.m_extensionsJson);
        m_isValid = a_other.m_isValid;
    }
    a_other.m_isValid = false;
    return *this;
}

const SceneAssetId &SceneDocumentCheckpoint::scene_asset_id() const noexcept
{
    return m_sceneAssetId;
}

SceneDocument::SceneDocument(SceneAssetId a_sceneAssetId, const AssertContext &a_assertContext) noexcept
    : m_sceneAssetId(std::move(a_sceneAssetId)), m_assertContext(&a_assertContext)
{
}

SceneDocument::~SceneDocument() noexcept = default;

SceneDocument SceneDocument::create(SceneAssetId a_sceneAssetId, const AssertContext &a_assertContext) noexcept
{
    return SceneDocument(std::move(a_sceneAssetId), a_assertContext);
}

const SceneAssetId &SceneDocument::scene_asset_id() const noexcept
{
    return m_sceneAssetId;
}

std::size_t SceneDocument::object_count() const noexcept
{
    return m_objects.size();
}

std::string_view SceneDocument::extensions_json() const noexcept
{
    return m_extensionsJson;
}

std::span<const SceneObject> SceneDocument::objects() const noexcept
{
    return m_objects;
}

const SceneObject *SceneDocument::find_object(const ObjectId &a_id) const noexcept
{
    const auto found = m_objectIndex.find(a_id);
    if (found == m_objectIndex.end())
    {
        return nullptr;
    }
    return &m_objects[found->second];
}

SceneDocumentCheckpoint SceneDocument::create_checkpoint() const noexcept
{
    try
    {
        return SceneDocumentCheckpoint(m_sceneAssetId, m_objects,
                                       m_extensionsJson);
    }
    catch (...)
    {
        terminate_exception();
    }
}

Result<void> SceneDocument::restore_checkpoint(
    SceneDocumentCheckpoint a_checkpoint) noexcept
{
    if (!a_checkpoint.m_isValid)
    {
        return Result<void>::failure(make_scene_error(
            *m_assertContext, SceneError::InvalidCheckpoint,
            "Scene checkpoint has already been consumed"));
    }
    if (a_checkpoint.m_sceneAssetId != m_sceneAssetId)
    {
        return Result<void>::failure(make_scene_error(
            *m_assertContext, SceneError::InvalidIdentity,
            "Scene checkpoint identity must match the target document"));
    }

    m_objects = std::move(a_checkpoint.m_objects);
    m_extensionsJson = std::move(a_checkpoint.m_extensionsJson);
    rebuild_index();
    return Result<void>::success();
}

Result<void> SceneDocument::add_object(ObjectId a_id, std::string_view a_name, bool a_isActive,
                                       std::optional<ObjectId> a_parentId, math::Transform a_transform) noexcept
{
    if (a_name.empty())
    {
        return Result<void>::failure(
            make_scene_error(*m_assertContext, SceneError::InvalidName, "Scene object name must not be empty"));
    }
    if (a_name.size() > k_maximumSceneStringBytes)
    {
        return Result<void>::failure(make_scene_error(
            *m_assertContext, SceneError::ResourceLimitExceeded,
            "Scene object name exceeds the 256 KiB string limit"));
    }
    if (!scene_private::is_valid_json_string_text(a_name))
    {
        return Result<void>::failure(make_scene_error(
            *m_assertContext, SceneError::InvalidName,
            "Scene object name must be valid UTF-8"));
    }
    if (find_object(a_id) != nullptr)
    {
        return Result<void>::failure(make_scene_error(*m_assertContext, SceneError::DuplicateObjectId,
                                                      "Scene object identity must be unique within a document"));
    }
    if (a_parentId && find_object(*a_parentId) == nullptr)
    {
        return Result<void>::failure(make_scene_error(*m_assertContext, SceneError::DanglingParent,
                                                      "Scene object parent must already exist in the document"));
    }
    if (a_parentId && a_id == *a_parentId)
    {
        return Result<void>::failure(
            make_scene_error(*m_assertContext, SceneError::HierarchyCycle, "Scene object cannot be its own parent"));
    }
    if (a_parentId && child_depth(*a_parentId) > maximum_hierarchy_depth())
    {
        return Result<void>::failure(make_scene_error(*m_assertContext, SceneError::HierarchyDepthExceeded,
                                                      "Scene object hierarchy exceeds the supported depth"));
    }
    if (m_objects.size() >= k_maximumSceneObjectCount)
    {
        return Result<void>::failure(make_scene_error(
            *m_assertContext, SceneError::ResourceLimitExceeded,
            "Scene object count exceeds the 4096 element limit"));
    }

    try
    {
        const auto index = m_objects.size();
        m_objects.push_back(SceneObject(std::move(a_id), std::string(a_name), a_isActive, std::move(a_parentId),
                                        std::move(a_transform), {}));
        m_objectIndex.emplace(m_objects.back().id(), index);
    }
    catch (...)
    {
        terminate_exception();
    }
    return Result<void>::success();
}

Result<void> SceneDocument::remove_object(const ObjectId &a_id) noexcept
{
    const auto found = m_objectIndex.find(a_id);
    if (found == m_objectIndex.end())
    {
        return Result<void>::failure(
            make_scene_error(*m_assertContext, SceneError::ObjectNotFound, "Scene object to remove was not found"));
    }
    const bool hasChild = std::any_of(m_objects.begin(), m_objects.end(),
                                      /// @brief Objectが削除対象をParentとして参照するか判定する
                                      [&a_id](const SceneObject &a_object) noexcept
                                      {
                                          const auto *parentId = a_object.try_parent_id();
                                          return parentId != nullptr && *parentId == a_id;
                                      });
    if (hasChild)
    {
        return Result<void>::failure(make_scene_error(*m_assertContext, SceneError::ChildObjectsExist,
                                                      "Scene object with children cannot be removed implicitly"));
    }

    m_objects.erase(m_objects.begin() + static_cast<std::ptrdiff_t>(found->second));
    rebuild_index();
    return Result<void>::success();
}

Result<void> SceneDocument::rename_object(const ObjectId &a_id, std::string_view a_name) noexcept
{
    if (a_name.empty())
    {
        return Result<void>::failure(
            make_scene_error(*m_assertContext, SceneError::InvalidName, "Scene object name must not be empty"));
    }
    if (a_name.size() > k_maximumSceneStringBytes)
    {
        return Result<void>::failure(make_scene_error(
            *m_assertContext, SceneError::ResourceLimitExceeded,
            "Scene object name exceeds the 256 KiB string limit"));
    }
    if (!scene_private::is_valid_json_string_text(a_name))
    {
        return Result<void>::failure(make_scene_error(
            *m_assertContext, SceneError::InvalidName,
            "Scene object name must be valid UTF-8"));
    }
    auto *object = find_mutable_object(a_id);
    if (object == nullptr)
    {
        return Result<void>::failure(
            make_scene_error(*m_assertContext, SceneError::ObjectNotFound, "Scene object to rename was not found"));
    }
    try
    {
        object->m_name = a_name;
    }
    catch (...)
    {
        terminate_exception();
    }
    return Result<void>::success();
}

Result<void> SceneDocument::set_parent(const ObjectId &a_id, std::optional<ObjectId> a_parentId) noexcept
{
    auto *object = find_mutable_object(a_id);
    if (object == nullptr)
    {
        return Result<void>::failure(
            make_scene_error(*m_assertContext, SceneError::ObjectNotFound, "Scene object to reparent was not found"));
    }
    if (a_parentId && find_object(*a_parentId) == nullptr)
    {
        return Result<void>::failure(make_scene_error(*m_assertContext, SceneError::DanglingParent,
                                                      "Scene object parent must exist in the document"));
    }
    if (a_parentId && (a_id == *a_parentId || would_create_cycle(a_id, *a_parentId)))
    {
        return Result<void>::failure(make_scene_error(*m_assertContext, SceneError::HierarchyCycle,
                                                      "Scene object reparenting would create a hierarchy cycle"));
    }
    if (a_parentId)
    {
        const auto targetDepth = child_depth(*a_parentId);
        const auto subtreeHeight = subtree_height(a_id);
        if (targetDepth > maximum_hierarchy_depth() || subtreeHeight > maximum_hierarchy_depth() - targetDepth + 1U)
        {
            return Result<void>::failure(
                make_scene_error(*m_assertContext, SceneError::HierarchyDepthExceeded,
                                 "Scene object subtree exceeds the supported hierarchy depth"));
        }
    }
    object->m_parentId = std::move(a_parentId);
    return Result<void>::success();
}

Result<void> SceneDocument::set_active(const ObjectId &a_id, bool a_isActive) noexcept
{
    auto *object = find_mutable_object(a_id);
    if (object == nullptr)
    {
        return Result<void>::failure(
            make_scene_error(*m_assertContext, SceneError::ObjectNotFound, "Scene object to activate was not found"));
    }
    object->m_isActive = a_isActive;
    return Result<void>::success();
}

Result<void> SceneDocument::set_transform(const ObjectId &a_id, math::Transform a_transform) noexcept
{
    auto *object = find_mutable_object(a_id);
    if (object == nullptr)
    {
        return Result<void>::failure(make_scene_error(*m_assertContext, SceneError::ObjectNotFound,
                                                      "Scene object transform target was not found"));
    }
    object->m_transform = std::move(a_transform);
    return Result<void>::success();
}

Result<void> SceneDocument::add_component(const ObjectId &a_objectId, SceneComponent a_component) noexcept
{
    if (!a_component.is_valid())
    {
        return Result<void>::failure(make_scene_error(*m_assertContext, SceneError::InvalidComponentData,
                                                      "Moved-from component data cannot be added to a document"));
    }
    auto *object = find_mutable_object(a_objectId);
    if (object == nullptr)
    {
        return Result<void>::failure(make_scene_error(*m_assertContext, SceneError::ObjectNotFound,
                                                      "Scene component owner object was not found"));
    }
    const bool isDuplicate =
        std::any_of(m_objects.begin(), m_objects.end(),
                    /// @brief Document内Objectが追加対象Component Identityを既に所有するか判定する
                    [&a_component](const SceneObject &a_existingObject) noexcept
                    {
                        const auto found = std::lower_bound(
                            a_existingObject.m_components.begin(),
                            a_existingObject.m_components.end(),
                            a_component.instance_id(),
                            /// @brief Componentを検索Identityより前へ並べるか判定する
                            [](const SceneComponent &a_existing,
                               const ComponentInstanceId &a_id) noexcept
                            {
                                return a_existing.instance_id() < a_id;
                            });
                        return found != a_existingObject.m_components.end() &&
                               found->instance_id() == a_component.instance_id();
                    });
    if (isDuplicate)
    {
        return Result<void>::failure(make_scene_error(*m_assertContext, SceneError::DuplicateComponentId,
                                                      "Component instance identity must be unique within a document"));
    }
    if (object->m_components.size() >= k_maximumSceneComponentsPerObject)
    {
        return Result<void>::failure(make_scene_error(
            *m_assertContext, SceneError::ResourceLimitExceeded,
            "Scene component count exceeds the 4096 element limit"));
    }
    try
    {
        const auto insertion = std::lower_bound(
            object->m_components.begin(), object->m_components.end(),
            a_component.instance_id(),
            /// @brief Componentを挿入Identityより前へ並べるか判定する
            [](const SceneComponent &a_existing,
               const ComponentInstanceId &a_id) noexcept
            {
                return a_existing.instance_id() < a_id;
            });
        object->m_components.insert(insertion, std::move(a_component));
    }
    catch (...)
    {
        terminate_exception();
    }
    return Result<void>::success();
}

Result<void> SceneDocument::remove_component(const ObjectId &a_objectId,
                                             const ComponentInstanceId &a_componentId) noexcept
{
    auto *object = find_mutable_object(a_objectId);
    if (object == nullptr)
    {
        return Result<void>::failure(make_scene_error(*m_assertContext, SceneError::ObjectNotFound,
                                                      "Scene component owner object was not found"));
    }
    const auto found = std::find_if(object->m_components.begin(), object->m_components.end(),
                                    /// @brief Component Instance Identityが削除対象と一致するか判定する
                                    [&a_componentId](const SceneComponent &a_component) noexcept
                                    { return a_component.instance_id() == a_componentId; });
    if (found == object->m_components.end())
    {
        return Result<void>::failure(make_scene_error(*m_assertContext, SceneError::ComponentNotFound,
                                                      "Scene component to remove was not found"));
    }
    object->m_components.erase(found);
    return Result<void>::success();
}

Result<void> SceneDocument::set_component_field(
    const ObjectId &a_objectId, const ComponentInstanceId &a_componentId,
    schema::FieldId a_fieldId, FieldValue a_value) noexcept
{
    auto *object = find_mutable_object(a_objectId);
    if (object == nullptr)
    {
        return Result<void>::failure(make_scene_error(
            *m_assertContext, SceneError::ObjectNotFound,
            "Scene component owner object was not found"));
    }
    const auto component = std::find_if(
        object->m_components.begin(), object->m_components.end(),
        /// @brief Component Instance Identity が編集対象と一致するか判定する
        [&a_componentId](const SceneComponent &a_existing) noexcept
        { return a_existing.instance_id() == a_componentId; });
    if (component == object->m_components.end())
    {
        return Result<void>::failure(make_scene_error(
            *m_assertContext, SceneError::ComponentNotFound,
            "Scene component field target was not found"));
    }

    auto *knownData = std::get_if<KnownComponentData>(&component->m_storage);
    if (knownData == nullptr)
    {
        return Result<void>::failure(make_scene_error(
            *m_assertContext, SceneError::UnsupportedComponentOperation,
            "Opaque component fields cannot be edited semantically"));
    }
    const auto field = std::lower_bound(
        knownData->m_knownFields.begin(), knownData->m_knownFields.end(),
        a_fieldId,
        /// @brief Known Field を検索 Identity より前へ並べるか判定する
        [](const KnownFieldData &a_existing, schema::FieldId a_id) noexcept
        { return a_existing.id() < a_id; });
    if (field == knownData->m_knownFields.end() || field->id() != a_fieldId)
    {
        return Result<void>::failure(make_scene_error(
            *m_assertContext, SceneError::UnknownSchemaField,
            "Scene component field identity was not found"));
    }
    if (!is_valid_field_value(a_value) || field->value().kind() != a_value.kind())
    {
        return Result<void>::failure(make_scene_error(
            *m_assertContext, SceneError::FieldTypeMismatch,
            "Scene component field value kind cannot change"));
    }

    field->m_value = std::move(a_value);
    return Result<void>::success();
}

Result<void> SceneDocument::validate() const noexcept
{
    if (m_objects.size() > k_maximumSceneObjectCount)
    {
        return Result<void>::failure(make_scene_error(
            *m_assertContext, SceneError::ResourceLimitExceeded,
            "Scene object count exceeds the 4096 element limit"));
    }
    if (m_objectIndex.size() != m_objects.size())
    {
        return Result<void>::failure(make_scene_error(*m_assertContext, SceneError::DuplicateObjectId,
                                                      "Scene object index size does not match the object collection"));
    }
    std::vector<ComponentInstanceId> componentIds;
    try
    {
        for (const auto &object : m_objects)
        {
            if (object.m_components.size() > k_maximumSceneComponentsPerObject)
            {
                return Result<void>::failure(make_scene_error(
                    *m_assertContext, SceneError::ResourceLimitExceeded,
                    "Scene component count exceeds the 4096 element limit"));
            }
            for (const auto &component : object.m_components)
            {
                if (!component.is_valid())
                {
                    return Result<void>::failure(make_scene_error(*m_assertContext, SceneError::InvalidComponentData,
                                                                  "Scene document contains moved-from component data"));
                }
                componentIds.push_back(component.instance_id());
            }
        }
        std::sort(componentIds.begin(), componentIds.end());
    }
    catch (...)
    {
        terminate_exception();
    }
    if (std::adjacent_find(componentIds.begin(), componentIds.end()) != componentIds.end())
    {
        return Result<void>::failure(make_scene_error(*m_assertContext, SceneError::DuplicateComponentId,
                                                      "Component instance identity must be unique within a document"));
    }

    for (std::size_t index = 0U; index < m_objects.size(); ++index)
    {
        const auto &object = m_objects[index];
        const auto found = m_objectIndex.find(object.id());
        if (found == m_objectIndex.end() || found->second != index)
        {
            return Result<void>::failure(make_scene_error(*m_assertContext, SceneError::DuplicateObjectId,
                                                          "Scene object index does not identify its stable object"));
        }
        const auto *parentId = object.try_parent_id();
        if (parentId != nullptr && find_object(*parentId) == nullptr)
        {
            return Result<void>::failure(make_scene_error(*m_assertContext, SceneError::DanglingParent,
                                                          "Scene object has a dangling parent identity"));
        }
        if (parentId != nullptr && (object.id() == *parentId || would_create_cycle(object.id(), *parentId)))
        {
            return Result<void>::failure(make_scene_error(*m_assertContext, SceneError::HierarchyCycle,
                                                          "Scene object hierarchy contains a cycle"));
        }
        if (parentId != nullptr && child_depth(*parentId) > maximum_hierarchy_depth())
        {
            return Result<void>::failure(make_scene_error(*m_assertContext, SceneError::HierarchyDepthExceeded,
                                                          "Scene object hierarchy exceeds the supported depth"));
        }
        for (std::size_t componentIndex = 1U; componentIndex < object.m_components.size(); ++componentIndex)
        {
            if (!(object.m_components[componentIndex - 1U].instance_id() <
                  object.m_components[componentIndex].instance_id()))
            {
                return Result<void>::failure(
                    make_scene_error(*m_assertContext, SceneError::DuplicateComponentId,
                                     "Scene component identities must be unique and stable ordered"));
            }
        }
    }
    return Result<void>::success();
}

SceneObject *SceneDocument::find_mutable_object(const ObjectId &a_id) noexcept
{
    const auto found = m_objectIndex.find(a_id);
    return found == m_objectIndex.end() ? nullptr : &m_objects[found->second];
}

bool SceneDocument::would_create_cycle(const ObjectId &a_id, const ObjectId &a_parentId) const noexcept
{
    const ObjectId *currentId = &a_parentId;
    while (currentId != nullptr)
    {
        if (*currentId == a_id)
        {
            return true;
        }
        const auto *current = find_object(*currentId);
        currentId = current == nullptr ? nullptr : current->try_parent_id();
    }
    return false;
}

std::size_t SceneDocument::child_depth(const ObjectId &a_parentId) const noexcept
{
    std::size_t depth = 1U;
    const auto *current = find_object(a_parentId);
    while (current != nullptr)
    {
        ++depth;
        if (depth > maximum_hierarchy_depth())
        {
            return depth;
        }
        const auto *parentId = current->try_parent_id();
        current = parentId == nullptr ? nullptr : find_object(*parentId);
    }
    return depth;
}

std::size_t SceneDocument::subtree_height(const ObjectId &a_id) const noexcept
{
    std::size_t maximumHeight = 1U;
    for (const auto &candidate : m_objects)
    {
        std::size_t height = 1U;
        const auto *parentId = candidate.try_parent_id();
        while (parentId != nullptr && height <= maximum_hierarchy_depth())
        {
            if (*parentId == a_id)
            {
                maximumHeight = std::max(maximumHeight, height + 1U);
                break;
            }
            const auto *parent = find_object(*parentId);
            parentId = parent == nullptr ? nullptr : parent->try_parent_id();
            ++height;
        }
    }
    return maximumHeight;
}

void SceneDocument::rebuild_index() noexcept
{
    try
    {
        m_objectIndex.clear();
        for (std::size_t index = 0U; index < m_objects.size(); ++index)
        {
            m_objectIndex.emplace(m_objects[index].id(), index);
        }
    }
    catch (...)
    {
        terminate_exception();
    }
}

[[noreturn]] void SceneDocument::terminate_exception() const noexcept
{
    m_assertContext->fatal_handler().terminate("Cue.Scene operation encountered an unexpected exception");
}
} // namespace cue::scene

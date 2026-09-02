#include <Cue/Scene/SceneDocument.h>

#include <Cue/Foundation/Assert.h>
#include <Cue/Scene/Error.h>

#include <algorithm>
#include <utility>

namespace cue::scene
{
SceneObject::SceneObject(ObjectId a_id, std::string a_name, bool a_isActive,
                         std::optional<ObjectId> a_parentId,
                         math::Transform a_transform) noexcept
    : m_id(std::move(a_id)), m_name(std::move(a_name)),
      m_isActive(a_isActive), m_parentId(std::move(a_parentId)),
      m_transform(std::move(a_transform))
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

SceneDocument::SceneDocument(SceneAssetId a_sceneAssetId,
                             const AssertContext &a_assertContext) noexcept
    : m_sceneAssetId(std::move(a_sceneAssetId)),
      m_assertContext(&a_assertContext)
{
}

SceneDocument::~SceneDocument() noexcept = default;

SceneDocument SceneDocument::create(
    SceneAssetId a_sceneAssetId,
    const AssertContext &a_assertContext) noexcept
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

Result<void> SceneDocument::add_object(
    ObjectId a_id, std::string_view a_name, bool a_isActive,
    std::optional<ObjectId> a_parentId,
    math::Transform a_transform) noexcept
{
    if (a_name.empty())
    {
        return Result<void>::failure(make_scene_error(
            *m_assertContext, SceneError::InvalidName,
            "Scene object name must not be empty"));
    }
    if (find_object(a_id) != nullptr)
    {
        return Result<void>::failure(make_scene_error(
            *m_assertContext, SceneError::DuplicateObjectId,
            "Scene object identity must be unique within a document"));
    }
    if (a_parentId && find_object(*a_parentId) == nullptr)
    {
        return Result<void>::failure(make_scene_error(
            *m_assertContext, SceneError::DanglingParent,
            "Scene object parent must already exist in the document"));
    }
    if (a_parentId && a_id == *a_parentId)
    {
        return Result<void>::failure(make_scene_error(
            *m_assertContext, SceneError::HierarchyCycle,
            "Scene object cannot be its own parent"));
    }

    try
    {
        const auto index = m_objects.size();
        m_objects.push_back(SceneObject(std::move(a_id), std::string(a_name),
                                        a_isActive, std::move(a_parentId),
                                        std::move(a_transform)));
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
        return Result<void>::failure(make_scene_error(
            *m_assertContext, SceneError::ObjectNotFound,
            "Scene object to remove was not found"));
    }
    const bool hasChild = std::any_of(
        m_objects.begin(), m_objects.end(),
        /// @brief Objectが削除対象をParentとして参照するか判定する
        [&a_id](const SceneObject &a_object) noexcept
        {
            const auto *parentId = a_object.try_parent_id();
            return parentId != nullptr && *parentId == a_id;
        });
    if (hasChild)
    {
        return Result<void>::failure(make_scene_error(
            *m_assertContext, SceneError::ChildObjectsExist,
            "Scene object with children cannot be removed implicitly"));
    }

    m_objects.erase(m_objects.begin() + static_cast<std::ptrdiff_t>(found->second));
    rebuild_index();
    return Result<void>::success();
}

Result<void> SceneDocument::rename_object(const ObjectId &a_id,
                                          std::string_view a_name) noexcept
{
    if (a_name.empty())
    {
        return Result<void>::failure(make_scene_error(
            *m_assertContext, SceneError::InvalidName,
            "Scene object name must not be empty"));
    }
    auto *object = find_mutable_object(a_id);
    if (object == nullptr)
    {
        return Result<void>::failure(make_scene_error(
            *m_assertContext, SceneError::ObjectNotFound,
            "Scene object to rename was not found"));
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

Result<void> SceneDocument::set_parent(
    const ObjectId &a_id,
    std::optional<ObjectId> a_parentId) noexcept
{
    auto *object = find_mutable_object(a_id);
    if (object == nullptr)
    {
        return Result<void>::failure(make_scene_error(
            *m_assertContext, SceneError::ObjectNotFound,
            "Scene object to reparent was not found"));
    }
    if (a_parentId && find_object(*a_parentId) == nullptr)
    {
        return Result<void>::failure(make_scene_error(
            *m_assertContext, SceneError::DanglingParent,
            "Scene object parent must exist in the document"));
    }
    if (a_parentId && (a_id == *a_parentId ||
                       would_create_cycle(a_id, *a_parentId)))
    {
        return Result<void>::failure(make_scene_error(
            *m_assertContext, SceneError::HierarchyCycle,
            "Scene object reparenting would create a hierarchy cycle"));
    }
    object->m_parentId = std::move(a_parentId);
    return Result<void>::success();
}

Result<void> SceneDocument::set_active(const ObjectId &a_id,
                                       bool a_isActive) noexcept
{
    auto *object = find_mutable_object(a_id);
    if (object == nullptr)
    {
        return Result<void>::failure(make_scene_error(
            *m_assertContext, SceneError::ObjectNotFound,
            "Scene object to activate was not found"));
    }
    object->m_isActive = a_isActive;
    return Result<void>::success();
}

Result<void> SceneDocument::set_transform(
    const ObjectId &a_id,
    math::Transform a_transform) noexcept
{
    auto *object = find_mutable_object(a_id);
    if (object == nullptr)
    {
        return Result<void>::failure(make_scene_error(
            *m_assertContext, SceneError::ObjectNotFound,
            "Scene object transform target was not found"));
    }
    object->m_transform = std::move(a_transform);
    return Result<void>::success();
}

Result<void> SceneDocument::validate() const noexcept
{
    if (m_objectIndex.size() != m_objects.size())
    {
        return Result<void>::failure(make_scene_error(
            *m_assertContext, SceneError::DuplicateObjectId,
            "Scene object index size does not match the object collection"));
    }
    for (std::size_t index = 0U; index < m_objects.size(); ++index)
    {
        const auto &object = m_objects[index];
        const auto found = m_objectIndex.find(object.id());
        if (found == m_objectIndex.end() || found->second != index)
        {
            return Result<void>::failure(make_scene_error(
                *m_assertContext, SceneError::DuplicateObjectId,
                "Scene object index does not identify its stable object"));
        }
        const auto *parentId = object.try_parent_id();
        if (parentId != nullptr && find_object(*parentId) == nullptr)
        {
            return Result<void>::failure(make_scene_error(
                *m_assertContext, SceneError::DanglingParent,
                "Scene object has a dangling parent identity"));
        }
        if (parentId != nullptr &&
            (object.id() == *parentId ||
             would_create_cycle(object.id(), *parentId)))
        {
            return Result<void>::failure(make_scene_error(
                *m_assertContext, SceneError::HierarchyCycle,
                "Scene object hierarchy contains a cycle"));
        }
    }
    return Result<void>::success();
}

SceneObject *SceneDocument::find_mutable_object(const ObjectId &a_id) noexcept
{
    const auto found = m_objectIndex.find(a_id);
    return found == m_objectIndex.end() ? nullptr : &m_objects[found->second];
}

bool SceneDocument::would_create_cycle(const ObjectId &a_id,
                                       const ObjectId &a_parentId) const noexcept
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
    m_assertContext->fatal_handler().terminate(
        "Cue.Scene operation encountered an unexpected exception");
}
} // namespace cue::scene

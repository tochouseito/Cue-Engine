#include "GameWorld.h"

// === C++ includes ===
#include <cmath>
#include <cstdint>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace Cue::GameCore
{
    GameWorld::GameWorld() noexcept = default;

    GameWorld::~GameWorld() = default;

    Result GameWorld::ecs(ECS::ECSManager*& a_outEcs) noexcept
    {
        a_outEcs = &m_ecsManager;
        return Result::ok();
    }

    Result GameWorld::create_object(std::string_view a_name, GameObject& a_outObject)
    {
        return create_object(a_name, "Default", a_outObject);
    }

    Result GameWorld::create_object(std::string_view a_name, std::string_view a_tag, GameObject& a_outObject)
    {
        a_outObject = {};
        return capture_result(
            [&]()
            {
                const EntityId entity = create_entity_record(k_invalidSceneId, k_invalidLocalObjectId);

                initialize_required_components(
                    entity,
                    a_name,
                    a_tag,
                    k_invalidSceneId,
                    k_invalidEntityId,
                    true,
                    false);

                a_outObject = make_handle(entity);
            });
    }

    Result GameWorld::create_static_mesh_object(uint32_t a_meshId, uint32_t a_materialId, GameObject& a_outObject)
    {
        return create_static_mesh_object("StaticMeshObject", a_meshId, a_materialId, Math::float3::zero(), a_outObject);
    }

    Result GameWorld::create_static_mesh_object(std::string_view a_name, uint32_t a_meshId, uint32_t a_materialId,
                                                const Math::float3& a_position, GameObject& a_outObject)
    {
        a_outObject = {};
        return capture_result(
            [&]()
            {
                const EntityId entity = create_entity_record(k_invalidSceneId, k_invalidLocalObjectId);

                initialize_required_components(
                    entity,
                    a_name,
                    "Default",
                    k_invalidSceneId,
                    k_invalidEntityId,
                    true,
                    false);

                // DrawSystem へ渡す最小 component 群を追加する
                ECS::TransformComponent* transform = m_ecsManager.get_component<ECS::TransformComponent>(entity);
                ECS::WorldTransformComponent* worldTransform =
                    m_ecsManager.get_component<ECS::WorldTransformComponent>(entity);
                ECS::MeshFilterComponent* meshFilter = m_ecsManager.add_component<ECS::MeshFilterComponent>(entity);
                ECS::StaticMeshRendererComponent* renderer =
                    m_ecsManager.add_component<ECS::StaticMeshRendererComponent>(entity);

                if (transform == nullptr || worldTransform == nullptr || meshFilter == nullptr || renderer == nullptr)
                {
                    throw std::runtime_error("GameWorld failed to add static mesh components.");
                }

                transform->position = a_position;
                worldTransform->position = a_position;

                // IndirectCommand batching の入力になる mesh/material ID を保持する
                meshFilter->meshId = a_meshId;
                renderer->materialId = a_materialId;

                a_outObject = make_handle(entity);
            });
    }

    Result GameWorld::instantiate_object(const GameObjectProto& a_proto, GameObject& a_outObject)
    {
        a_outObject = {};
        return capture_result(
            [&]()
            {
                const EntityId entity = create_entity_record(k_invalidSceneId, k_invalidLocalObjectId);

                a_proto.restore_components_into(entity, m_ecsManager);

                // 復元済み Entity に GameWorld 共通 component を補う
                initialize_required_components(
                    entity,
                    a_proto.name(),
                    a_proto.tag(),
                    k_invalidSceneId,
                    k_invalidEntityId,
                    true,
                    false);

                a_outObject = make_handle(entity);
            });
    }

    Result GameWorld::destroy_object(EntityId a_entityId) noexcept
    {
        EntityRecord* record = try_get_entity_record(a_entityId);
        if (record == nullptr || !record->isAlive)
        {
            return Result::fail(Code::InvalidState, Severity::Warning, "GameWorld object is not alive.");
        }

        // 同じ Entity が削除キューへ複数回入らないようにする
        if (!record->isPendingDestroy)
        {
            record->isPendingDestroy = true;
            m_pendingDestroyedEntities.push_back(a_entityId);
        }

        return Result::ok();
    }

    void GameWorld::execute_deferred_deletions() noexcept
    {
        // 破棄中に新しい削除要求が積まれても混ざらないよう、現在分だけ退避する
        const std::vector<EntityId> pending = std::move(m_pendingDestroyedEntities);
        m_pendingDestroyedEntities.clear();

        for (const EntityId entity : pending)
        {
            destroy_object_immediately(entity);
        }
    }

    Result GameWorld::clear() noexcept
    {
        for (EntityId entity = 0; entity < static_cast<EntityId>(m_entityRecords.size()); ++entity)
        {
            destroy_object_immediately(entity);
        }

        m_pendingDestroyedEntities.clear();
        clear_render_camera();
        return Result::ok();
    }

    Result GameWorld::object_count(size_t& a_outCount) const noexcept
    {
        a_outCount = m_liveObjectCount;
        return Result::ok();
    }

    Result GameWorld::is_alive(EntityId a_entityId, Generation a_generation, bool& a_outIsAlive) const noexcept
    {
        // EntityRecord と世代番号の一致で GameObject ハンドルの有効性を判定する
        const EntityRecord* record = try_get_entity_record(a_entityId);
        a_outIsAlive =
            record != nullptr && record->isAlive && !record->isPendingDestroy && record->generation == a_generation;
        return Result::ok();
    }

    Result GameWorld::get_object_name(EntityId a_entityId, std::string& a_outName) const
    {
        a_outName.clear();

        const BaseComponent* base = const_cast<GameWorld*>(this)->m_ecsManager.get_component<BaseComponent>(a_entityId);
        if (base == nullptr || !contains_object(a_entityId))
        {
            return Result::fail(Code::InvalidState, Severity::Warning, "GameWorld object is not alive.");
        }

        a_outName = base->name;
        return Result::ok();
    }

    Result GameWorld::set_object_name(EntityId a_entityId, std::string_view a_name)
    {
        BaseComponent* base = m_ecsManager.get_component<BaseComponent>(a_entityId);
        if (base == nullptr || !contains_object(a_entityId))
        {
            return Result::fail(Code::InvalidState, Severity::Warning, "GameWorld object is not alive.");
        }

        const std::string oldName = base->name;

        // 空名と重複名を GameWorld 内で扱いやすい名前へ正規化する
        base->name = make_unique_object_name(a_name, a_entityId);
        if (oldName != base->name)
        {
            remove_object_from_name_index(a_entityId, oldName);
            add_object_to_name_index(a_entityId, base->name);
        }

        return Result::ok();
    }

    Result GameWorld::get_object_tag(EntityId a_entityId, std::string& a_outTag) const
    {
        a_outTag.clear();

        const BaseComponent* base = const_cast<GameWorld*>(this)->m_ecsManager.get_component<BaseComponent>(a_entityId);
        if (base == nullptr || !contains_object(a_entityId))
        {
            return Result::fail(Code::InvalidState, Severity::Warning, "GameWorld object is not alive.");
        }

        a_outTag = base->tag;
        return Result::ok();
    }

    Result GameWorld::set_object_tag(EntityId a_entityId, std::string_view a_tag)
    {
        BaseComponent* base = m_ecsManager.get_component<BaseComponent>(a_entityId);
        if (base == nullptr || !contains_object(a_entityId))
        {
            return Result::fail(Code::InvalidState, Severity::Warning, "GameWorld object is not alive.");
        }

        const std::string oldTag = base->tag;

        // 空タグは既定タグに正規化する
        base->tag = a_tag.empty() ? "Default" : std::string(a_tag);
        if (oldTag != base->tag)
        {
            remove_object_from_tag_index(a_entityId, oldTag);
            add_object_to_tag_index(a_entityId, base->tag);
        }

        return Result::ok();
    }

    Result GameWorld::is_object_active(EntityId a_entityId, bool& a_outIsActive) const
    {
        a_outIsActive = false;

        const BaseComponent* base = const_cast<GameWorld*>(this)->m_ecsManager.get_component<BaseComponent>(a_entityId);
        if (base == nullptr || !contains_object(a_entityId))
        {
            return Result::fail(Code::InvalidState, Severity::Warning, "GameWorld object is not alive.");
        }

        a_outIsActive = base->isActiveSelf;
        return Result::ok();
    }

    Result GameWorld::set_object_active(EntityId a_entityId, bool a_isActive)
    {
        // アクティブ状態は BaseComponent と ECS の両方へ反映する
        BaseComponent* base = m_ecsManager.get_component<BaseComponent>(a_entityId);
        if (base == nullptr || !contains_object(a_entityId))
        {
            return Result::fail(Code::InvalidState, Severity::Warning, "GameWorld object is not alive.");
        }

        base->isActiveSelf = a_isActive;

        m_ecsManager.set_entity_active(a_entityId, a_isActive);
        return Result::ok();
    }

    Result GameWorld::is_object_persistent(EntityId a_entityId, bool& a_outIsPersistent) const
    {
        a_outIsPersistent = false;

        const BaseComponent* base = const_cast<GameWorld*>(this)->m_ecsManager.get_component<BaseComponent>(a_entityId);
        if (base == nullptr || !contains_object(a_entityId))
        {
            return Result::fail(Code::InvalidState, Severity::Warning, "GameWorld object is not alive.");
        }

        a_outIsPersistent = base->isPersistent;
        return Result::ok();
    }

    Result GameWorld::set_object_persistent(EntityId a_entityId, bool a_isPersistent)
    {
        BaseComponent* base = m_ecsManager.get_component<BaseComponent>(a_entityId);
        if (base == nullptr || !contains_object(a_entityId))
        {
            return Result::fail(Code::InvalidState, Severity::Warning, "GameWorld object is not alive.");
        }

        // Scene 所属を導入した時に永続 Object を Scene から切り離せるよう値を整える
        base->isPersistent = a_isPersistent;
        base->owningSceneId = a_isPersistent ? k_invalidSceneId : base->owningSceneId;
        return Result::ok();
    }

    Result GameWorld::get_parent(EntityId a_entityId, EntityId& a_outParent) const noexcept
    {
        a_outParent = k_invalidEntityId;
        if (!contains_object(a_entityId))
        {
            return Result::fail(Code::InvalidState, Severity::Warning, "GameWorld object is not alive.");
        }

        const BaseComponent* base = const_cast<GameWorld*>(this)->m_ecsManager.get_component<BaseComponent>(a_entityId);
        if (base == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error, "GameWorld BaseComponent is missing.");
        }

        a_outParent = base->parent;
        return Result::ok();
    }

    Result GameWorld::set_parent(EntityId a_childEntityId, EntityId a_parentEntityId,
                                 bool a_keepsWorldTransform) noexcept
    {
        if (!contains_object(a_childEntityId) || !contains_object(a_parentEntityId))
        {
            return Result::fail(Code::InvalidState, Severity::Warning, "GameWorld object is not alive.");
        }
        if (a_childEntityId == a_parentEntityId)
        {
            return Result::fail(Code::InvalidArgument, Severity::Error, "GameWorld parent cannot be the child itself.");
        }
        if (is_descendant_of(a_parentEntityId, a_childEntityId))
        {
            return Result::fail(Code::InvalidArgument, Severity::Error, "GameWorld parent cycle was rejected.");
        }

        ECS::TransformComponent* childTransform = m_ecsManager.get_component<ECS::TransformComponent>(a_childEntityId);
        ECS::TransformComponent* parentTransform = m_ecsManager.get_component<ECS::TransformComponent>(a_parentEntityId);
        if (childTransform == nullptr || parentTransform == nullptr)
        {
            return Result::fail(
                Code::InvalidState,
                Severity::Warning,
                "GameWorld parent update requires TransformComponent.");
        }

        ECS::WorldTransformComponent childWorld{};
        ECS::WorldTransformComponent parentWorld{};
        if (a_keepsWorldTransform)
        {
            std::vector<uint8_t> state(m_entityRecords.size(), 0u);
            if (!resolve_world_transform(a_childEntityId, state, childWorld) ||
                !resolve_world_transform(a_parentEntityId, state, parentWorld))
            {
                return Result::fail(
                    Code::InvalidState,
                    Severity::Error,
                    "GameWorld world transform could not be resolved.");
            }
        }

        BaseComponent* childBase = m_ecsManager.get_component<BaseComponent>(a_childEntityId);
        if (childBase == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error, "GameWorld BaseComponent is missing.");
        }

        childBase->parent = a_parentEntityId;
        if (a_keepsWorldTransform)
        {
            *childTransform = make_local_transform(parentWorld, childWorld);
        }

        sync_world_transforms();
        return Result::ok();
    }

    Result GameWorld::detach_parent(EntityId a_childEntityId, bool a_keepsWorldTransform) noexcept
    {
        if (!contains_object(a_childEntityId))
        {
            return Result::fail(Code::InvalidState, Severity::Warning, "GameWorld object is not alive.");
        }

        BaseComponent* childBase = m_ecsManager.get_component<BaseComponent>(a_childEntityId);
        ECS::TransformComponent* childTransform = m_ecsManager.get_component<ECS::TransformComponent>(a_childEntityId);
        if (childBase == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error, "GameWorld BaseComponent is missing.");
        }
        if (childTransform == nullptr)
        {
            return Result::fail(
                Code::InvalidState,
                Severity::Warning,
                "GameWorld parent detach requires TransformComponent.");
        }

        ECS::WorldTransformComponent childWorld{};
        if (a_keepsWorldTransform)
        {
            std::vector<uint8_t> state(m_entityRecords.size(), 0u);
            if (!resolve_world_transform(a_childEntityId, state, childWorld))
            {
                return Result::fail(
                    Code::InvalidState,
                    Severity::Error,
                    "GameWorld world transform could not be resolved.");
            }
        }

        childBase->parent = k_invalidEntityId;
        if (a_keepsWorldTransform)
        {
            childTransform->position = childWorld.position;
            childTransform->rotation = childWorld.rotation;
            childTransform->scale = childWorld.scale;
        }

        sync_world_transforms();
        return Result::ok();
    }

    Result GameWorld::find_objects_by_tag(std::string_view a_tag, std::vector<GameObject>& a_outObjects)
    {
        a_outObjects.clear();

        const auto it = m_tagIndex.find(std::string(a_tag));
        if (it == m_tagIndex.end())
        {
            return Result::ok();
        }

        // index は補助構造なので、世代付きハンドルで最終的な生存判定を行う。
        a_outObjects.reserve(it->second.size());
        for (const EntityId entity : it->second)
        {
            GameObject object = make_handle(entity);
            if (object.is_valid())
            {
                a_outObjects.push_back(object);
            }
        }

        return Result::ok();
    }

    Result GameWorld::find_objects_by_name(std::string_view a_name, std::vector<GameObject>& a_outObjects)
    {
        a_outObjects.clear();

        const auto it = m_nameIndex.find(std::string(a_name));
        if (it == m_nameIndex.end())
        {
            return Result::ok();
        }

        // index は補助構造なので、世代付きハンドルで最終的な生存判定を行う。
        a_outObjects.reserve(it->second.size());
        for (const EntityId entity : it->second)
        {
            GameObject object = make_handle(entity);
            if (object.is_valid())
            {
                a_outObjects.push_back(object);
            }
        }

        return Result::ok();
    }

    Result GameWorld::find_object_by_name(std::string_view a_name, GameObject& a_outObject)
    {
        a_outObject = {};

        std::vector<GameObject> objects{};
        Result result = find_objects_by_name(a_name, objects);
        if (!result)
        {
            return result;
        }

        if (objects.empty())
        {
            return Result::fail(Code::NotFound, Severity::Warning, "GameWorld object was not found.");
        }

        a_outObject = objects.front();
        return Result::ok();
    }

    Result GameWorld::for_each_object(const std::function<void(EntityId, GameObject)>& a_func)
    {
        if (!a_func)
        {
            return Result::fail(Code::InvalidArgument, Severity::Warning, "GameWorld object visitor is empty.");
        }

        for (EntityId entity = 0; entity < static_cast<EntityId>(m_entityRecords.size()); ++entity)
        {
            GameObject object = make_handle(entity);
            if (!object.is_valid())
            {
                continue;
            }

            a_func(entity, object);
        }

        return Result::ok();
    }

    Result GameWorld::capture_result(const std::function<void()>& a_func)
    {
        try
        {
            a_func();
            return Result::ok();
        }
        catch (const std::bad_alloc&)
        {
            // メモリ確保失敗は OutOfMemory として明示する
            return Result::fail(Code::OutOfMemory, Severity::Error, "GameWorld out of memory.");
        }
        catch (const std::exception&)
        {
            // 低レイヤー Result の message は静的文字列前提なので例外文字列は保持しない
            return Result::fail(Code::InternalError, Severity::Error, "GameWorld internal error.");
        }
    }

    template <typename T> Result GameWorld::get_component(EntityId a_entityId, T*& a_outComponent) noexcept
    {
        a_outComponent = nullptr;
        if (!contains_object(a_entityId))
        {
            return Result::fail(Code::InvalidState, Severity::Warning, "GameWorld object is not alive.");
        }

        a_outComponent = m_ecsManager.get_component<T>(a_entityId);
        if (a_outComponent == nullptr)
        {
            return Result::fail(Code::NotFound, Severity::Warning, "GameWorld component was not found.");
        }

        return Result::ok();
    }

    template <typename T, typename... Args>
    Result GameWorld::add_component(EntityId a_entityId, T*& a_outComponent, Args&&... a_args)
    {
        a_outComponent = nullptr;
        if (!contains_object(a_entityId))
        {
            return Result::fail(Code::InvalidState, Severity::Warning, "GameWorld object is not alive.");
        }

        return capture_result(
            [&]()
            {
                T* component = m_ecsManager.add_component<T>(a_entityId);
                if (component == nullptr)
                {
                    throw std::runtime_error("GameWorld failed to add component.");
                }

                *component = T{std::forward<Args>(a_args)...};
                a_outComponent = component;
            });
    }

    Result GameWorld::set_render_camera(EntityId a_entityId) noexcept
    {
        if (!contains_object(a_entityId))
        {
            return Result::fail(Code::InvalidState, Severity::Warning, "GameWorld render camera is not alive.");
        }

        if (m_ecsManager.get_component<ECS::TransformComponent>(a_entityId) == nullptr ||
            m_ecsManager.get_component<ECS::CameraComponent>(a_entityId) == nullptr)
        {
            return Result::fail(
                Code::InvalidState,
                Severity::Warning,
                "GameWorld render camera requires TransformComponent and CameraComponent.");
        }

        m_renderCameraEntity = a_entityId;
        return Result::ok();
    }

    void GameWorld::clear_render_camera() noexcept
    {
        m_renderCameraEntity = k_invalidEntityId;
    }

    EntityId GameWorld::render_camera_entity() const noexcept
    {
        return m_renderCameraEntity;
    }

    template <typename T> Result GameWorld::has_component(EntityId a_entityId, bool& a_outHasComponent) const noexcept
    {
        a_outHasComponent = false;
        if (!contains_object(a_entityId))
        {
            return Result::fail(Code::InvalidState, Severity::Warning, "GameWorld object is not alive.");
        }

        // const 関数だが ECSManager の取得 API は non-const なので限定的に const
        // を外す
        a_outHasComponent = const_cast<GameWorld*>(this)->m_ecsManager.get_component<T>(a_entityId) != nullptr;
        return Result::ok();
    }

    template <typename T> Result GameWorld::remove_component(EntityId a_entityId) noexcept
    {
        if (!contains_object(a_entityId))
        {
            return Result::fail(Code::InvalidState, Severity::Warning, "GameWorld object is not alive.");
        }
        if constexpr (std::is_same_v<T, ECS::TransformComponent>)
        {
            return Result::fail(
                Code::InvalidState,
                Severity::Warning,
                "GameWorld required TransformComponent cannot be removed.");
        }
        else
        {
            m_ecsManager.remove_component<T>(a_entityId);
            return Result::ok();
        }
    }

    EntityId GameWorld::create_entity_record(SceneId a_sourceSceneId, LocalObjectId a_localObjectId)
    {
        const EntityId entity = m_ecsManager.generate_entity();

        if (m_entityRecords.size() <= entity)
        {
            m_entityRecords.resize(entity + 1);
        }

        // 再利用 slot がまだ生存中なら内部状態破損として扱う
        EntityRecord& record = m_entityRecords[entity];
        if (record.isAlive)
        {
            throw std::runtime_error("GameWorld entity slot is already alive.");
        }

        // generation 0 は無効値に寄せるため、最初の世代は 1 にする
        if (record.generation == 0)
        {
            record.generation = 1;
        }

        record.isAlive = true;
        record.isPendingDestroy = false;
        record.sourceSceneId = a_sourceSceneId;
        record.sourceLocalObjectId = a_localObjectId;

        ++m_liveObjectCount;

        return entity;
    }

    void GameWorld::initialize_required_components(EntityId a_entityId, std::string_view a_name, std::string_view a_tag,
                                                   SceneId a_owningSceneId, EntityId a_parent, bool a_isActive,
                                                   bool a_isPersistent)
    {
        BaseComponent* base = m_ecsManager.get_component<BaseComponent>(a_entityId);
        ECS::RenderableInfoComponent* renderableInfo =
            m_ecsManager.get_component<ECS::RenderableInfoComponent>(a_entityId);
        ECS::TransformComponent* transform = m_ecsManager.get_component<ECS::TransformComponent>(a_entityId);
        ECS::WorldTransformComponent* worldTransform =
            m_ecsManager.get_component<ECS::WorldTransformComponent>(a_entityId);
        bool addedWorldTransform = false;

        if (base == nullptr)
        {
            base = m_ecsManager.add_component<BaseComponent>(a_entityId);
        }

        if (renderableInfo == nullptr)
        {
            renderableInfo = m_ecsManager.add_component<ECS::RenderableInfoComponent>(a_entityId);
        }

        if (transform == nullptr)
        {
            transform = m_ecsManager.add_component<ECS::TransformComponent>(a_entityId);
        }

        if (worldTransform == nullptr)
        {
            worldTransform = m_ecsManager.add_component<ECS::WorldTransformComponent>(a_entityId);
            addedWorldTransform = true;
        }

        if (base == nullptr || renderableInfo == nullptr || transform == nullptr || worldTransform == nullptr)
        {
            throw std::runtime_error("GameWorld failed to initialize required components.");
        }

        base->name = make_unique_object_name(a_name, a_entityId);
        base->tag = a_tag.empty() ? "Default" : std::string(a_tag);
        base->owningSceneId = a_owningSceneId;
        base->parent = a_parent;
        base->isActiveSelf = a_isActive;
        base->isPersistent = a_isPersistent;

        // DrawSystem 側の登録 ID はまだ未割り当てにしておく
        renderableInfo->objectId = ECS::k_invalidRenderableId;
        renderableInfo->transformId = ECS::k_invalidRenderableId;

        if (addedWorldTransform)
        {
            worldTransform->position = transform->position;
            worldTransform->rotation = transform->rotation;
            worldTransform->scale = transform->scale;
        }

        add_object_to_name_index(a_entityId, base->name);
        add_object_to_tag_index(a_entityId, base->tag);

        m_ecsManager.set_entity_active(a_entityId, a_isActive);
    }

    void GameWorld::destroy_object_immediately(EntityId a_entityId) noexcept
    {
        EntityRecord* record = try_get_entity_record(a_entityId);
        if (record == nullptr || !record->isAlive)
        {
            return;
        }

        if (m_renderCameraEntity == a_entityId)
        {
            clear_render_camera();
        }

        std::string name{};
        std::string tag{};
        (void)get_object_name(a_entityId, name);
        (void)get_object_tag(a_entityId, tag);
        remove_object_from_name_index(a_entityId, name);
        remove_object_from_tag_index(a_entityId, tag);

        // GameWorld 側のレコードを無効化し、古いハンドルを世代で弾けるようにする
        record->isAlive = false;
        record->isPendingDestroy = false;
        record->sourceSceneId = k_invalidSceneId;
        record->sourceLocalObjectId = k_invalidLocalObjectId;
        ++record->generation;

        // generation 0 は無効扱いにするため wrap 時は 1 へ戻す
        if (record->generation == 0)
        {
            record->generation = 1;
        }

        if (m_liveObjectCount > 0)
        {
            --m_liveObjectCount;
        }

        m_ecsManager.remove_entity(a_entityId);
    }

    GameObject GameWorld::make_handle(EntityId a_entityId) noexcept
    {
        EntityRecord* record = try_get_entity_record(a_entityId);
        if (record == nullptr || !record->isAlive)
        {
            return {};
        }

        return GameObject(this, a_entityId, record->generation);
    }

    bool GameWorld::contains_object(EntityId a_entityId) const noexcept
    {
        // 生存中かつ遅延削除中でない Entity だけを有効 Object として扱う
        const EntityRecord* record = try_get_entity_record(a_entityId);
        return record != nullptr && record->isAlive && !record->isPendingDestroy;
    }

    EntityRecord* GameWorld::try_get_entity_record(EntityId a_entityId) noexcept
    {
        if (a_entityId >= m_entityRecords.size())
        {
            return nullptr;
        }

        return &m_entityRecords[a_entityId];
    }

    const EntityRecord* GameWorld::try_get_entity_record(EntityId a_entityId) const noexcept
    {
        if (a_entityId >= m_entityRecords.size())
        {
            return nullptr;
        }

        return &m_entityRecords[a_entityId];
    }

    std::string GameWorld::normalize_object_name(std::string_view a_name) const
    {
        if (a_name.empty())
        {
            return "GameObject";
        }

        return std::string(a_name);
    }

    bool GameWorld::is_name_taken(std::string_view a_name, EntityId a_ignoredEntityId) const
    {
        const auto it = m_nameIndex.find(std::string(a_name));
        if (it == m_nameIndex.end())
        {
            return false;
        }

        // 同名 Entity のうち、除外対象以外に生存 Object があれば使用中
        for (const EntityId entity : it->second)
        {
            if (entity == a_ignoredEntityId)
            {
                continue;
            }
            if (contains_object(entity))
            {
                return true;
            }
        }

        return false;
    }

    std::string GameWorld::make_unique_object_name(std::string_view a_requestedName, EntityId a_ignoredEntityId) const
    {
        const std::string baseName = normalize_object_name(a_requestedName);
        if (!is_name_taken(baseName, a_ignoredEntityId))
        {
            return baseName;
        }

        // 重複している場合は Name(1), Name(2) の形式で空きを探す
        uint32_t suffix = 1;
        while (true)
        {
            const std::string candidate = baseName + "(" + std::to_string(suffix) + ")";
            if (!is_name_taken(candidate, a_ignoredEntityId))
            {
                return candidate;
            }
            ++suffix;
        }
    }

    void GameWorld::add_object_to_tag_index(EntityId a_entityId, const std::string& a_tag)
    {
        m_tagIndex[a_tag].insert(a_entityId);
    }

    void GameWorld::add_object_to_name_index(EntityId a_entityId, const std::string& a_name)
    {
        m_nameIndex[a_name].insert(a_entityId);
    }

    void GameWorld::remove_object_from_tag_index(EntityId a_entityId, const std::string& a_tag)
    {
        const auto it = m_tagIndex.find(a_tag);
        if (it == m_tagIndex.end())
        {
            return;
        }

        it->second.erase(a_entityId);
        if (it->second.empty())
        {
            m_tagIndex.erase(it);
        }
    }

    void GameWorld::remove_object_from_name_index(EntityId a_entityId, const std::string& a_name)
    {
        const auto it = m_nameIndex.find(a_name);
        if (it == m_nameIndex.end())
        {
            return;
        }

        it->second.erase(a_entityId);
        if (it->second.empty())
        {
            m_nameIndex.erase(it);
        }
    }

    ECS::WorldTransformComponent GameWorld::compose_world_transform(
        const ECS::WorldTransformComponent& a_parent,
        const ECS::TransformComponent& a_local) noexcept
    {
        ECS::WorldTransformComponent world{};
        world.scale = Math::float3(
            a_parent.scale.x * a_local.scale.x,
            a_parent.scale.y * a_local.scale.y,
            a_parent.scale.z * a_local.scale.z);
        world.rotation = Math::Quaternion::normalize(a_parent.rotation * a_local.rotation);

        // 親 scale を適用した local position を親 rotation で回して world position に合成する。
        const Math::float3 scaledLocalPosition(
            a_local.position.x * a_parent.scale.x,
            a_local.position.y * a_parent.scale.y,
            a_local.position.z * a_parent.scale.z);
        world.position = a_parent.position + rotate_vector(a_parent.rotation, scaledLocalPosition);
        return world;
    }

    ECS::TransformComponent GameWorld::make_local_transform(
        const ECS::WorldTransformComponent& a_parent,
        const ECS::WorldTransformComponent& a_world) noexcept
    {
        auto divide_safe = [](float a_value, float a_divisor) noexcept
        {
            return std::abs(a_divisor) > std::numeric_limits<float>::epsilon()
                ? a_value / a_divisor
                : 0.0f;
        };

        ECS::TransformComponent local{};
        const Math::Quaternion inverseParentRotation = Math::Quaternion::inverse(a_parent.rotation);
        const Math::float3 unrotatedPosition =
            rotate_vector(inverseParentRotation, a_world.position - a_parent.position);

        local.position = Math::float3(
            divide_safe(unrotatedPosition.x, a_parent.scale.x),
            divide_safe(unrotatedPosition.y, a_parent.scale.y),
            divide_safe(unrotatedPosition.z, a_parent.scale.z));
        local.rotation = Math::Quaternion::normalize(inverseParentRotation * a_world.rotation);
        local.scale = Math::float3(
            divide_safe(a_world.scale.x, a_parent.scale.x),
            divide_safe(a_world.scale.y, a_parent.scale.y),
            divide_safe(a_world.scale.z, a_parent.scale.z));
        return local;
    }

    bool GameWorld::is_descendant_of(EntityId a_entityId, EntityId a_possibleAncestor) const noexcept
    {
        EntityId current = a_entityId;
        size_t visitedCount = 0;
        while (current != k_invalidEntityId && visitedCount <= m_entityRecords.size())
        {
            if (current == a_possibleAncestor)
            {
                return true;
            }

            const BaseComponent* base = const_cast<GameWorld*>(this)->m_ecsManager.get_component<BaseComponent>(current);
            current = base != nullptr ? base->parent : k_invalidEntityId;
            ++visitedCount;
        }

        return false;
    }

    bool GameWorld::resolve_world_transform(EntityId a_entityId,
                                            std::vector<uint8_t>& a_state,
                                            ECS::WorldTransformComponent& a_outWorld) noexcept
    {
        if (!contains_object(a_entityId) || a_entityId >= a_state.size())
        {
            return false;
        }

        uint8_t& state = a_state[a_entityId];
        if (state == 2u)
        {
            ECS::WorldTransformComponent* resolved =
                m_ecsManager.get_component<ECS::WorldTransformComponent>(a_entityId);
            if (resolved == nullptr)
            {
                return false;
            }

            a_outWorld = *resolved;
            return true;
        }
        if (state == 1u)
        {
            return false;
        }

        ECS::TransformComponent* local = m_ecsManager.get_component<ECS::TransformComponent>(a_entityId);
        if (local == nullptr)
        {
            return false;
        }

        ECS::WorldTransformComponent* world =
            m_ecsManager.get_component<ECS::WorldTransformComponent>(a_entityId);
        if (world == nullptr)
        {
            world = m_ecsManager.add_component<ECS::WorldTransformComponent>(a_entityId);
            if (world == nullptr)
            {
                return false;
            }
        }

        state = 1u;

        const BaseComponent* base = m_ecsManager.get_component<BaseComponent>(a_entityId);
        const EntityId parent = base != nullptr ? base->parent : k_invalidEntityId;
        if (parent != k_invalidEntityId && contains_object(parent) &&
            m_ecsManager.get_component<ECS::TransformComponent>(parent) != nullptr)
        {
            ECS::WorldTransformComponent parentWorld{};
            if (resolve_world_transform(parent, a_state, parentWorld))
            {
                *world = compose_world_transform(parentWorld, *local);
            }
            else
            {
                world->position = local->position;
                world->rotation = local->rotation;
                world->scale = local->scale;
            }
        }
        else
        {
            world->position = local->position;
            world->rotation = local->rotation;
            world->scale = local->scale;
        }

        state = 2u;
        a_outWorld = *world;
        return true;
    }

    void GameWorld::sync_world_transforms() noexcept
    {
        // TransformComponent を持つ Entity だけを対象に、親から子へ WorldTransform を確定する。
        std::vector<uint8_t> state(m_entityRecords.size(), 0u);
        for (EntityId entity = 0; entity < static_cast<EntityId>(m_entityRecords.size()); ++entity)
        {
            if (!contains_object(entity) || m_ecsManager.get_component<ECS::TransformComponent>(entity) == nullptr)
            {
                continue;
            }

            ECS::WorldTransformComponent world{};
            (void)resolve_world_transform(entity, state, world);
        }
    }

    void GameWorld::animate_static_mesh_objects(float a_deltaTime)
    {
        const std::vector<EntityId> entities = collect_active_static_mesh_entities();
        for (size_t entityIndex = 0; entityIndex < entities.size(); ++entityIndex)
        {
            ECS::TransformComponent* transform =
                m_ecsManager.get_component<ECS::TransformComponent>(entities[entityIndex]);
            if (transform == nullptr)
            {
                continue;
            }

            Math::float3 axis(0.0f, 1.0f, 0.0f);
            float angularVelocity = 0.5f;
            switch (entityIndex)
            {
            case 0:
                axis = Math::float3(0.0f, 1.0f, 0.0f);
                angularVelocity = 1.25f;
                break;
            case 1:
                axis = Math::float3(1.0f, 0.0f, 0.0f);
                angularVelocity = 0.75f;
                break;
            case 2:
                axis = Math::float3(0.0f, 1.0f, 0.0f);
                angularVelocity = -1.0f;
                break;
            default:
                axis = Math::float3(0.0f, 1.0f, 0.0f);
                angularVelocity = 0.5f;
                break;
            }

            const Math::Quaternion deltaRotation =
                make_axis_angle_quaternion(axis, a_deltaTime * angularVelocity);
            transform->rotation =
                Math::Quaternion::normalize(transform->rotation * deltaRotation);
        }
    }

    std::vector<EntityId> GameWorld::collect_active_static_mesh_entities() const
    {
        std::vector<EntityId> entities{};
        entities.reserve(m_liveObjectCount);

        ECS::ECSManager& ecs = const_cast<ECS::ECSManager&>(m_ecsManager);
        for (EntityId entity = 0; entity < static_cast<EntityId>(m_entityRecords.size()); ++entity)
        {
            if (!contains_object(entity) || !m_ecsManager.is_entity_active(entity))
            {
                continue;
            }

            const BaseComponent* base = ecs.get_component<BaseComponent>(entity);
            const ECS::TransformComponent* transform = ecs.get_component<ECS::TransformComponent>(entity);
            const ECS::MeshFilterComponent* meshFilter = ecs.get_component<ECS::MeshFilterComponent>(entity);
            const ECS::StaticMeshRendererComponent* renderer =
                ecs.get_component<ECS::StaticMeshRendererComponent>(entity);
            if (base == nullptr || transform == nullptr || meshFilter == nullptr || renderer == nullptr)
            {
                continue;
            }
            if (!base->isActiveSelf || !renderer->visible || meshFilter->meshId == ECS::k_invalidMeshId)
            {
                continue;
            }

            entities.push_back(entity);
        }

        return entities;
    }

    template Result GameWorld::get_component<BaseComponent>(EntityId, BaseComponent*&) noexcept;
    template Result GameWorld::get_component<ECS::RenderableInfoComponent>(EntityId,
                                                                           ECS::RenderableInfoComponent*&) noexcept;
    template Result GameWorld::get_component<ECS::TransformComponent>(EntityId, ECS::TransformComponent*&) noexcept;
    template Result GameWorld::get_component<ECS::WorldTransformComponent>(EntityId,
                                                                           ECS::WorldTransformComponent*&) noexcept;
    template Result GameWorld::get_component<ECS::CameraComponent>(EntityId, ECS::CameraComponent*&) noexcept;
    template Result GameWorld::get_component<ECS::MeshFilterComponent>(EntityId, ECS::MeshFilterComponent*&) noexcept;
    template Result GameWorld::get_component<ECS::StaticMeshRendererComponent>(
        EntityId, ECS::StaticMeshRendererComponent*&) noexcept;

    template Result GameWorld::add_component<BaseComponent>(EntityId, BaseComponent*&);
    template Result GameWorld::add_component<ECS::RenderableInfoComponent>(EntityId, ECS::RenderableInfoComponent*&);
    template Result GameWorld::add_component<ECS::TransformComponent>(EntityId, ECS::TransformComponent*&);
    template Result GameWorld::add_component<ECS::WorldTransformComponent>(EntityId, ECS::WorldTransformComponent*&);
    template Result GameWorld::add_component<ECS::CameraComponent>(EntityId, ECS::CameraComponent*&);
    template Result GameWorld::add_component<ECS::MeshFilterComponent>(EntityId, ECS::MeshFilterComponent*&);
    template Result GameWorld::add_component<ECS::StaticMeshRendererComponent>(EntityId,
                                                                               ECS::StaticMeshRendererComponent*&);

    template Result GameWorld::has_component<BaseComponent>(EntityId, bool&) const noexcept;
    template Result GameWorld::has_component<ECS::RenderableInfoComponent>(EntityId, bool&) const noexcept;
    template Result GameWorld::has_component<ECS::TransformComponent>(EntityId, bool&) const noexcept;
    template Result GameWorld::has_component<ECS::WorldTransformComponent>(EntityId, bool&) const noexcept;
    template Result GameWorld::has_component<ECS::CameraComponent>(EntityId, bool&) const noexcept;
    template Result GameWorld::has_component<ECS::MeshFilterComponent>(EntityId, bool&) const noexcept;
    template Result GameWorld::has_component<ECS::StaticMeshRendererComponent>(EntityId, bool&) const noexcept;

    template Result GameWorld::remove_component<BaseComponent>(EntityId) noexcept;
    template Result GameWorld::remove_component<ECS::RenderableInfoComponent>(EntityId) noexcept;
    template Result GameWorld::remove_component<ECS::TransformComponent>(EntityId) noexcept;
    template Result GameWorld::remove_component<ECS::WorldTransformComponent>(EntityId) noexcept;
    template Result GameWorld::remove_component<ECS::CameraComponent>(EntityId) noexcept;
    template Result GameWorld::remove_component<ECS::MeshFilterComponent>(EntityId) noexcept;
    template Result GameWorld::remove_component<ECS::StaticMeshRendererComponent>(EntityId) noexcept;
} // namespace Cue::GameCore

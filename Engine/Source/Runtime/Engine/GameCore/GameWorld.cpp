#include "GameWorld.h"

// === C++ includes ===
#include <new>
#include <stdexcept>
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

                initialize_base_component(entity, a_name, a_tag, k_invalidSceneId, k_invalidEntityId, true, false);

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

                initialize_base_component(entity, a_name, "Default", k_invalidSceneId, k_invalidEntityId, true, false);

                // DrawSystem へ渡す最小 component 群を追加する
                ECS::TransformComponent* transform = m_ecsManager.add_component<ECS::TransformComponent>(entity);
                ECS::WorldTransformComponent* worldTransform =
                    m_ecsManager.add_component<ECS::WorldTransformComponent>(entity);
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
                initialize_base_component(entity, a_proto.name(), a_proto.tag(), k_invalidSceneId, k_invalidEntityId,
                                          true, false);

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

        m_ecsManager.remove_component<T>(a_entityId);
        return Result::ok();
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

    void GameWorld::initialize_base_component(EntityId a_entityId, std::string_view a_name, std::string_view a_tag,
                                              SceneId a_owningSceneId, EntityId a_parent, bool a_isActive,
                                              bool a_isPersistent)
    {
        BaseComponent* base = m_ecsManager.get_component<BaseComponent>(a_entityId);
        ECS::RenderableInfoComponent* renderableInfo =
            m_ecsManager.get_component<ECS::RenderableInfoComponent>(a_entityId);

        if (base == nullptr)
        {
            base = m_ecsManager.add_component<BaseComponent>(a_entityId);
        }

        if (renderableInfo == nullptr)
        {
            renderableInfo = m_ecsManager.add_component<ECS::RenderableInfoComponent>(a_entityId);
        }

        if (base == nullptr || renderableInfo == nullptr)
        {
            throw std::runtime_error("GameWorld failed to initialize base components.");
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

    template Result GameWorld::get_component<BaseComponent>(EntityId, BaseComponent*&) noexcept;
    template Result GameWorld::get_component<ECS::RenderableInfoComponent>(EntityId,
                                                                           ECS::RenderableInfoComponent*&) noexcept;
    template Result GameWorld::get_component<ECS::TransformComponent>(EntityId, ECS::TransformComponent*&) noexcept;
    template Result GameWorld::get_component<ECS::WorldTransformComponent>(EntityId,
                                                                           ECS::WorldTransformComponent*&) noexcept;
    template Result GameWorld::get_component<ECS::MeshFilterComponent>(EntityId, ECS::MeshFilterComponent*&) noexcept;
    template Result GameWorld::get_component<ECS::StaticMeshRendererComponent>(
        EntityId, ECS::StaticMeshRendererComponent*&) noexcept;

    template Result GameWorld::add_component<BaseComponent>(EntityId, BaseComponent*&);
    template Result GameWorld::add_component<ECS::RenderableInfoComponent>(EntityId, ECS::RenderableInfoComponent*&);
    template Result GameWorld::add_component<ECS::TransformComponent>(EntityId, ECS::TransformComponent*&);
    template Result GameWorld::add_component<ECS::WorldTransformComponent>(EntityId, ECS::WorldTransformComponent*&);
    template Result GameWorld::add_component<ECS::MeshFilterComponent>(EntityId, ECS::MeshFilterComponent*&);
    template Result GameWorld::add_component<ECS::StaticMeshRendererComponent>(EntityId,
                                                                               ECS::StaticMeshRendererComponent*&);

    template Result GameWorld::has_component<BaseComponent>(EntityId, bool&) const noexcept;
    template Result GameWorld::has_component<ECS::RenderableInfoComponent>(EntityId, bool&) const noexcept;
    template Result GameWorld::has_component<ECS::TransformComponent>(EntityId, bool&) const noexcept;
    template Result GameWorld::has_component<ECS::WorldTransformComponent>(EntityId, bool&) const noexcept;
    template Result GameWorld::has_component<ECS::MeshFilterComponent>(EntityId, bool&) const noexcept;
    template Result GameWorld::has_component<ECS::StaticMeshRendererComponent>(EntityId, bool&) const noexcept;

    template Result GameWorld::remove_component<BaseComponent>(EntityId) noexcept;
    template Result GameWorld::remove_component<ECS::RenderableInfoComponent>(EntityId) noexcept;
    template Result GameWorld::remove_component<ECS::TransformComponent>(EntityId) noexcept;
    template Result GameWorld::remove_component<ECS::WorldTransformComponent>(EntityId) noexcept;
    template Result GameWorld::remove_component<ECS::MeshFilterComponent>(EntityId) noexcept;
    template Result GameWorld::remove_component<ECS::StaticMeshRendererComponent>(EntityId) noexcept;
} // namespace Cue::GameCore

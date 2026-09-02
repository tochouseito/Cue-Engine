#pragma once

#include <Cue/Foundation/Assert.h>
#include <Cue/Foundation/Result.h>
#include <Cue/GameCore/Entity.h>
#include <Cue/GameCore/Error.h>
#include <Cue/Schema/Registry.h>
#include <Cue/Schema/Types.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace cue::game_core
{
class World;

/// @brief Process 全体で World Incarnation ID を一意発行する明示所有 Source
///
/// RuntimeHost、Editor、Test を含む全 World 所有者は同じ Source を共有し、Source を全 World より長く生存させる
class WorldIdentitySource final
{
  public:
    /// @brief 未発行の World ID 1 から Process-local Source を開始する
    WorldIdentitySource() noexcept = default;
    /// @brief 発行状態を一意所有するため Copy 構築を禁止する
    WorldIdentitySource(const WorldIdentitySource &) = delete;
    /// @brief 発行状態を一意所有するため Copy 代入を禁止する
    WorldIdentitySource &operator=(const WorldIdentitySource &) = delete;
    /// @brief 発行元 Address を安定させるため Move 構築を禁止する
    WorldIdentitySource(WorldIdentitySource &&) = delete;
    /// @brief 発行元 Address を安定させるため Move 代入を禁止する
    WorldIdentitySource &operator=(WorldIdentitySource &&) = delete;
    /// @brief 全 World と Entity Handle の破棄後に発行状態を破棄する
    ~WorldIdentitySource() = default;

  private:
    friend class World;

    /// @brief 0 へ Wrap せず次の Process-local World ID を予約する
    [[nodiscard]] std::optional<std::uint64_t> acquire_id() noexcept;

    std::atomic_uint64_t m_nextId = 1U;
};

/// @brief 一つの World と Schema Type に C++ Component 型を結び付ける Runtime Token
/// @tparam T 共通基底や RTTI を要求しない Component 型
template <typename T> class ComponentType final
{
  public:
    /// @brief 未登録の Component Type を作らせないため既定構築を禁止する
    ComponentType() = delete;
    /// @brief 同じ World 内で使用する Component Type Token を複製する
    ComponentType(const ComponentType &) noexcept = default;
    /// @brief Component Type Token を複製代入する
    ComponentType &operator=(const ComponentType &) noexcept = default;
    /// @brief Component Type Token を移動する
    ComponentType(ComponentType &&) noexcept = default;
    /// @brief Component Type Token を移動代入する
    ComponentType &operator=(ComponentType &&) noexcept = default;
    /// @brief Component Type Token の値を破棄する
    ~ComponentType() = default;

    /// @brief Schema Registry が割り当てた Runtime Dense Index を返す
    [[nodiscard]] schema::DenseTypeIndex dense_index() const noexcept
    {
        return m_denseIndex;
    }

  private:
    friend class World;

    /// @brief World が検証済み Schema Index と Runtime Binding を結び付ける
    ComponentType(const World &a_world, schema::DenseTypeIndex a_denseIndex,
                  const void *a_binding, std::uint64_t a_worldId,
                  const void *a_identitySource) noexcept
        : m_world(&a_world), m_denseIndex(a_denseIndex), m_binding(a_binding),
          m_worldId(a_worldId), m_identitySource(a_identitySource)
    {
    }

    const World *m_world;
    schema::DenseTypeIndex m_denseIndex;
    const void *m_binding;
    std::uint64_t m_worldId;
    const void *m_identitySource;
};

/// @brief 世代付き Entity と型付き Component Storage を一意所有する Runtime World
class World final
{
  public:
    /// @brief Factory だけが World Constructor へ渡せる生成権限
    class ConstructionKey final
    {
      public:
        /// @brief 生成権限を値として複製する
        ConstructionKey(const ConstructionKey &) noexcept = default;
        /// @brief 生成権限を値として複製代入する
        ConstructionKey &operator=(const ConstructionKey &) noexcept = default;
        /// @brief 生成権限を値として移動する
        ConstructionKey(ConstructionKey &&) noexcept = default;
        /// @brief 生成権限を値として移動代入する
        ConstructionKey &operator=(ConstructionKey &&) noexcept = default;
        /// @brief bit_cast による権限生成を防ぐ non-trivial な破棄を行う
        ~ConstructionKey() noexcept
        {
        }

      private:
        friend class World;

        /// @brief World Factory だけに生成権限を発行する
        ConstructionKey() noexcept = default;
    };

    /// @brief Identity Source と Schema Registry から空の World を生成する
    /// @param a_identitySource Process 全体で共有し全 World と Handle より長く生存する発行元
    /// @param a_schemaRegistry World より長く生存する Seal 済み Schema Registry
    /// @param a_assertContext World より長く生存する非所有診断 Context
    [[nodiscard]] static Result<std::unique_ptr<World>> create(
        WorldIdentitySource &a_identitySource,
        const schema::SchemaRegistry &a_schemaRegistry,
        const AssertContext &a_assertContext) noexcept;

    /// @brief Factory を通らない World 生成を禁止する
    World() = delete;
    /// @brief World 所有状態の複製を禁止する
    World(const World &) = delete;
    /// @brief World 所有状態の複製代入を禁止する
    World &operator=(const World &) = delete;
    /// @brief 発行済み Handle と Component Token を保護するため Move 構築を禁止する
    World(World &&) = delete;
    /// @brief 発行済み Handle と Component Token を保護するため Move 代入を禁止する
    World &operator=(World &&) = delete;
    /// @brief 残る Component と Entity Slot を所有権順に破棄する
    ~World() noexcept;

    /// @brief Factory が検証済み World ID と固定 Schema Binding Table を構築する
    World(ConstructionKey, std::uint64_t a_worldId,
          WorldIdentitySource &a_identitySource,
          const schema::SchemaRegistry &a_schemaRegistry,
          const AssertContext &a_assertContext);

    /// @brief 新しい Entity Slot を作成または Free List から再利用する
    [[nodiscard]] Result<EntityHandle> create_entity() noexcept;
    /// @brief Entity の全 Component を破棄して Slot 世代を進める
    [[nodiscard]] Result<void> destroy_entity(EntityHandle a_entity) noexcept;
    /// @brief Handle が現在この World の生存 Entity を指すか返す
    [[nodiscard]] bool is_alive(EntityHandle a_entity) const noexcept;
    /// @brief 現在生存している Entity 数を返す
    [[nodiscard]] std::size_t entity_count() const noexcept;
    /// @brief World Incarnation を識別する non-zero ID を返す
    [[nodiscard]] std::uint64_t id() const noexcept;

    /// @brief Schema Type と C++ Component 型をこの World へ一意登録する
    template <typename T>
    [[nodiscard]] Result<ComponentType<T>> register_component(
        schema::TypeId a_typeId) noexcept
    {
        static_assert(std::is_object_v<T> && !std::is_const_v<T>);
        static_assert(std::is_nothrow_destructible_v<T>);
        static_assert(std::is_nothrow_move_constructible_v<T>);

        assert_owner_thread();
        auto denseResult = m_schemaRegistry->dense_index(a_typeId, *m_assertContext);
        auto *denseIndex = denseResult.try_value();

        if (denseIndex == nullptr)
        {
            return Result<ComponentType<T>>::failure(
                std::move(*denseResult.try_error()));
        }

        const auto slot = static_cast<std::size_t>(denseIndex->value());
        auto &binding = m_componentBindings[slot];

        if (binding.isRegistered)
        {
            return Result<ComponentType<T>>::failure(make_game_core_error(
                *m_assertContext, GameCoreError::ComponentTypeConflict,
                "Schema type already has a C++ component capability"));
        }

        binding.isRegistered = true;
        return Result<ComponentType<T>>::success(ComponentType<T>(
            *this, *denseIndex, &binding, m_worldId, m_identitySource));
    }

    /// @brief Entity へ Component を構築して World 所有 Storage に追加する
    /// @tparam T nothrow Move 構築と nothrow 破棄が可能な Component 型
    /// @tparam Args Component の nothrow Constructor へ渡す引数型
    /// @return 同じ Component Storage の次の Structural Mutation まで有効な非所有 Pointer
    template <typename T, typename... Args>
    [[nodiscard]] Result<T *> add_component(
        ComponentType<T> a_type, EntityHandle a_entity,
        Args &&...a_arguments) noexcept
    {
        static_assert(std::is_nothrow_constructible_v<T, Args...>);

        assert_owner_thread();

        if (!validate_entity(a_entity))
        {
            return Result<T *>::failure(make_game_core_error(
                *m_assertContext, GameCoreError::InvalidEntity,
                "Component add requires a live entity from this world"));
        }

        if (!validate_component_type(a_type))
        {
            return Result<T *>::failure(make_game_core_error(
                *m_assertContext, GameCoreError::UnregisteredComponent,
                "Component type is not registered in this world"));
        }

        const auto storageIndex =
            static_cast<std::size_t>(a_type.m_denseIndex.value());
        auto *baseStorage = m_componentStorages[storageIndex].get();

        if (baseStorage != nullptr && baseStorage->has(a_entity.index()))
        {
            return Result<T *>::failure(make_game_core_error(
                *m_assertContext, GameCoreError::ComponentAlreadyExists,
                "Entity already owns this component type"));
        }

        T pendingComponent(std::forward<Args>(a_arguments)...);

        try
        {
            if (baseStorage == nullptr)
            {
                m_storageCreationOrder.reserve(
                    m_storageCreationOrder.size() + 1U);
                auto storage = std::make_unique<ComponentStorage<T>>();
                T *component = storage->add(a_entity.index(),
                                            std::move(pendingComponent));
                m_componentStorages[storageIndex] = std::move(storage);
                m_storageCreationOrder.push_back(
                    static_cast<std::uint32_t>(storageIndex));
                return Result<T *>::success(std::move(component));
            }

            auto *storage = static_cast<ComponentStorage<T> *>(baseStorage);
            T *component = storage->add(a_entity.index(),
                                        std::move(pendingComponent));
            return Result<T *>::success(std::move(component));
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

    /// @brief Entity が指定 Component を所有するか検証付きで返す
    template <typename T>
    [[nodiscard]] Result<bool> has_component(
        ComponentType<T> a_type, EntityHandle a_entity) const noexcept
    {
        assert_owner_thread();

        if (!validate_entity(a_entity))
        {
            return Result<bool>::failure(make_game_core_error(
                *m_assertContext, GameCoreError::InvalidEntity,
                "Component lookup requires a live entity from this world"));
        }

        if (!validate_component_type(a_type))
        {
            return Result<bool>::failure(make_game_core_error(
                *m_assertContext, GameCoreError::UnregisteredComponent,
                "Component type is not registered in this world"));
        }

        const auto storageIndex =
            static_cast<std::size_t>(a_type.m_denseIndex.value());
        const auto *storage = m_componentStorages[storageIndex].get();
        bool hasComponent =
            storage != nullptr && storage->has(a_entity.index());
        return Result<bool>::success(std::move(hasComponent));
    }

    /// @brief Entity の Component へ Lifetime 制限付きの Mutable Pointer を返す
    template <typename T>
    [[nodiscard]] Result<T *> get_component(
        ComponentType<T> a_type, EntityHandle a_entity) noexcept
    {
        assert_owner_thread();

        if (!validate_entity(a_entity))
        {
            return Result<T *>::failure(make_game_core_error(
                *m_assertContext, GameCoreError::InvalidEntity,
                "Component lookup requires a live entity from this world"));
        }

        if (!validate_component_type(a_type))
        {
            return Result<T *>::failure(make_game_core_error(
                *m_assertContext, GameCoreError::UnregisteredComponent,
                "Component type is not registered in this world"));
        }

        const auto storageIndex =
            static_cast<std::size_t>(a_type.m_denseIndex.value());
        auto *baseStorage = m_componentStorages[storageIndex].get();

        if (baseStorage == nullptr || !baseStorage->has(a_entity.index()))
        {
            return Result<T *>::failure(make_game_core_error(
                *m_assertContext, GameCoreError::ComponentNotFound,
                "Entity does not own this component type"));
        }

        auto *storage = static_cast<ComponentStorage<T> *>(baseStorage);
        T *component = storage->get(a_entity.index());
        return Result<T *>::success(std::move(component));
    }

    /// @brief Entity の Component を Storage から一度だけ破棄する
    template <typename T>
    [[nodiscard]] Result<void> remove_component(
        ComponentType<T> a_type, EntityHandle a_entity) noexcept
    {
        assert_owner_thread();

        if (!validate_entity(a_entity))
        {
            return Result<void>::failure(make_game_core_error(
                *m_assertContext, GameCoreError::InvalidEntity,
                "Component removal requires a live entity from this world"));
        }

        if (!validate_component_type(a_type))
        {
            return Result<void>::failure(make_game_core_error(
                *m_assertContext, GameCoreError::UnregisteredComponent,
                "Component type is not registered in this world"));
        }

        const auto storageIndex =
            static_cast<std::size_t>(a_type.m_denseIndex.value());
        auto *baseStorage = m_componentStorages[storageIndex].get();

        if (baseStorage == nullptr || !baseStorage->has(a_entity.index()))
        {
            return Result<void>::failure(make_game_core_error(
                *m_assertContext, GameCoreError::ComponentNotFound,
                "Entity does not own this component type"));
        }

        auto *storage = static_cast<ComponentStorage<T> *>(baseStorage);
        storage->remove(a_entity.index());
        return Result<void>::success();
    }

  private:
    struct EntitySlot final
    {
        std::uint32_t generation = 1U;
        bool isAlive = false;
        bool isRetired = false;
    };

    struct ComponentBinding final
    {
        bool isRegistered = false;
    };

    class ComponentStorageBase
    {
      public:
        /// @brief 型付き Storage を基底 Pointer から正しく破棄する
        virtual ~ComponentStorageBase() = default;
        /// @brief Entity がこの Storage の Component を持つか返す
        [[nodiscard]] virtual bool has(std::uint32_t a_entityIndex) const noexcept = 0;
        /// @brief Entity が持つ Component があれば一度だけ破棄する
        virtual void remove_entity(std::uint32_t a_entityIndex) noexcept = 0;
    };

    template <typename T> class PackedArray final
    {
      public:
        /// @brief Allocation を持たない空の Packed Array を開始する
        PackedArray() noexcept = default;
        /// @brief Component 所有権の複製を禁止する
        PackedArray(const PackedArray &) = delete;
        /// @brief Component 所有権の複製代入を禁止する
        PackedArray &operator=(const PackedArray &) = delete;
        /// @brief Storage Address を固定するため Move 構築を禁止する
        PackedArray(PackedArray &&) = delete;
        /// @brief Storage Address を固定するため Move 代入を禁止する
        PackedArray &operator=(PackedArray &&) = delete;

        /// @brief 残る Component を逆順に破棄して Allocation を解放する
        ~PackedArray() noexcept
        {
            while (m_size > 0U)
            {
                --m_size;
                std::destroy_at(m_data + m_size);
            }

            if (m_data != nullptr)
            {
                m_allocator.deallocate(m_data, m_capacity);
            }
        }

        /// @brief 追加時に既存 Component を nothrow Move して連続容量を確保する
        void reserve_for_one()
        {
            if (m_size < m_capacity)
            {
                return;
            }

            const std::size_t nextCapacity = m_capacity == 0U
                                                 ? 4U
                                                 : m_capacity * 2U;
            T *nextData = m_allocator.allocate(nextCapacity);

            for (std::size_t index = 0U; index < m_size; ++index)
            {
                std::construct_at(nextData + index, std::move(m_data[index]));
            }

            for (std::size_t index = m_size; index > 0U; --index)
            {
                std::destroy_at(m_data + index - 1U);
            }

            if (m_data != nullptr)
            {
                m_allocator.deallocate(m_data, m_capacity);
            }

            m_data = nextData;
            m_capacity = nextCapacity;
        }

        /// @brief 予約済み末尾へ Component を直接構築する
        template <typename... Args>
        [[nodiscard]] T *emplace_back(Args &&...a_arguments) noexcept
        {
            T *component = m_data + m_size;
            std::construct_at(component, std::forward<Args>(a_arguments)...);
            ++m_size;
            return component;
        }

        /// @brief 指定要素を破棄し末尾 Component の nothrow Move で穴を埋める
        void remove_swap(std::size_t a_index) noexcept
        {
            const std::size_t lastIndex = m_size - 1U;

            if (a_index != lastIndex)
            {
                std::destroy_at(m_data + a_index);
                std::construct_at(m_data + a_index,
                                  std::move(m_data[lastIndex]));
                std::destroy_at(m_data + lastIndex);
            }
            else
            {
                std::destroy_at(m_data + lastIndex);
            }

            --m_size;
        }

        /// @brief 指定位置の Component へ非所有 Pointer を返す
        [[nodiscard]] T *at(std::size_t a_index) noexcept
        {
            return m_data + a_index;
        }

      private:
        std::allocator<T> m_allocator;
        T *m_data = nullptr;
        std::size_t m_size = 0U;
        std::size_t m_capacity = 0U;
    };

    template <typename T> class ComponentStorage final : public ComponentStorageBase
    {
      public:
        /// @brief Allocation を持たない空の型付き Sparse Set を開始する
        ComponentStorage() noexcept = default;

        /// @brief 所有 Component を Packed Array の規則で破棄する
        ~ComponentStorage() override = default;

        /// @brief Sparse Entry から Entity の Component 所有有無を返す
        [[nodiscard]] bool has(std::uint32_t a_entityIndex) const noexcept override
        {
            return static_cast<std::size_t>(a_entityIndex) < m_sparse.size() &&
                   m_sparse[a_entityIndex] != 0U;
        }

        /// @brief Entity の Component があれば Swap-remove で破棄する
        void remove_entity(std::uint32_t a_entityIndex) noexcept override
        {
            if (has(a_entityIndex))
            {
                remove(a_entityIndex);
            }
        }

        /// @brief Entity と Component を Dense 末尾へ整合性を保って追加する
        [[nodiscard]] T *add(std::uint32_t a_entityIndex,
                             T &&a_component)
        {
            const auto sparseSize = static_cast<std::size_t>(a_entityIndex) + 1U;

            if (m_sparse.size() < sparseSize)
            {
                m_sparse.resize(sparseSize, 0U);
            }

            m_denseEntities.reserve(m_denseEntities.size() + 1U);
            m_components.reserve_for_one();
            m_denseEntities.push_back(a_entityIndex);
            T *component = m_components.emplace_back(std::move(a_component));
            m_sparse[a_entityIndex] =
                static_cast<std::uint32_t>(m_denseEntities.size());
            return component;
        }

        /// @brief Entity に対応する Dense Component Pointer を返す
        [[nodiscard]] T *get(std::uint32_t a_entityIndex) noexcept
        {
            const auto densePosition =
                static_cast<std::size_t>(m_sparse[a_entityIndex] - 1U);
            return m_components.at(densePosition);
        }

        /// @brief Entity の Component を破棄し Dense 穴を末尾要素で埋める
        void remove(std::uint32_t a_entityIndex) noexcept
        {
            const auto densePosition =
                static_cast<std::size_t>(m_sparse[a_entityIndex] - 1U);
            const std::size_t lastPosition = m_denseEntities.size() - 1U;
            const std::uint32_t movedEntity = m_denseEntities[lastPosition];
            m_components.remove_swap(densePosition);

            if (densePosition != lastPosition)
            {
                m_denseEntities[densePosition] = movedEntity;
                m_sparse[movedEntity] =
                    static_cast<std::uint32_t>(densePosition + 1U);
            }

            m_denseEntities.pop_back();
            m_sparse[a_entityIndex] = 0U;
        }

      private:
        std::vector<std::uint32_t> m_denseEntities;
        PackedArray<T> m_components;
        std::vector<std::uint32_t> m_sparse;
    };

    /// @brief Component Type Token がこの World の Binding と一致するか検証する
    template <typename T>
    [[nodiscard]] bool validate_component_type(
        const ComponentType<T> &a_type) const noexcept
    {
        if (a_type.m_world != this || a_type.m_worldId != m_worldId ||
            a_type.m_identitySource != m_identitySource)
        {
            return false;
        }

        const auto index = static_cast<std::size_t>(a_type.m_denseIndex.value());
        return index > 0U && index < m_componentBindings.size() &&
               m_schemaRegistry->find(a_type.m_denseIndex) != nullptr &&
               a_type.m_binding == &m_componentBindings[index] &&
               m_componentBindings[index].isRegistered;
    }

    /// @brief 現在 Thread が World Owner Thread であることを検証する
    void assert_owner_thread() const noexcept;
    /// @brief Handle の World、Slot、Generation、Token が現在状態と一致するか検証する
    [[nodiscard]] bool validate_entity(EntityHandle a_entity) const noexcept;
    /// @brief World と Slot 状態から Handle 改変検出 Token を生成する
    [[nodiscard]] std::uint64_t make_validation_token(
        std::uint32_t a_index, std::uint32_t a_generation) const noexcept;
    /// @brief GameCore Allocation 失敗を Emergency 終了へ変換する
    [[noreturn]] void terminate_allocation() const noexcept;
    /// @brief GameCore 境界の予期しない例外を Emergency 終了へ変換する
    [[noreturn]] void terminate_exception() const noexcept;

    std::uint64_t m_worldId;
    WorldIdentitySource *m_identitySource;
    const schema::SchemaRegistry *m_schemaRegistry;
    const AssertContext *m_assertContext;
    std::thread::id m_ownerThread;
    std::vector<EntitySlot> m_slots;
    std::vector<std::uint32_t> m_freeIndices;
    std::vector<ComponentBinding> m_componentBindings;
    std::vector<std::unique_ptr<ComponentStorageBase>> m_componentStorages;
    std::vector<std::uint32_t> m_storageCreationOrder;
    std::size_t m_entityCount = 0U;
};
} // namespace cue::game_core

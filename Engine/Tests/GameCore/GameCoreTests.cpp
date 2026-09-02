#include <Cue/Foundation/Assert.h>
#include <Cue/Foundation/Fatal.h>
#include <Cue/Foundation/Log.h>
#include <Cue/GameCore/Error.h>
#include <Cue/GameCore/World.h>
#include <Cue/Schema/Descriptor.h>
#include <Cue/Schema/Registry.h>
#include <Cue/Schema/Types.h>

#include <cstdlib>
#include <memory>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
class TestFatalHandler final : public cue::FatalHandler
{
  public:
    /// @brief Test 中の通常 Fatal を Process 失敗へ変換する
    [[noreturn]] void terminate() noexcept override
    {
        std::abort();
    }

    /// @brief Test 中の Emergency Fatal を Process 失敗へ変換する
    [[noreturn]] void terminate(std::string_view) noexcept override
    {
        std::abort();
    }
};

struct Position final
{
    int x;
    int y;

    /// @brief Test 用の座標値を構築する
    Position(int a_x, int a_y) noexcept : x(a_x), y(a_y)
    {
    }
};

struct LifetimeState final
{
    int activeOwners = 0;
    int destroyedOwners = 0;
};

class LifetimeComponent final
{
  public:
    /// @brief Lifetime Counter の所有対象を一つ開始する
    explicit LifetimeComponent(LifetimeState &a_state) noexcept
        : m_state(&a_state), m_ownsCounter(true)
    {
        ++m_state->activeOwners;
    }

    /// @brief Logical Component 所有権の複製を禁止する
    LifetimeComponent(const LifetimeComponent &) = delete;
    /// @brief Logical Component 所有権の複製代入を禁止する
    LifetimeComponent &operator=(const LifetimeComponent &) = delete;

    /// @brief Packed Storage 再配置時に Counter 所有権を一度だけ移す
    LifetimeComponent(LifetimeComponent &&a_other) noexcept
        : m_state(a_other.m_state), m_ownsCounter(a_other.m_ownsCounter)
    {
        a_other.m_ownsCounter = false;
    }

    /// @brief Component が Move 代入を要求されないことを Compile 時に固定する
    LifetimeComponent &operator=(LifetimeComponent &&) = delete;

    /// @brief Counter 所有権を持つ Logical Component だけを一度破棄する
    ~LifetimeComponent() noexcept
    {
        if (m_ownsCounter)
        {
            --m_state->activeOwners;
            ++m_state->destroyedOwners;
        }
    }

  private:
    LifetimeState *m_state;
    bool m_ownsCounter;
};

struct DestructionOrderState final
{
    int values[2] = {0, 0};
    std::size_t count = 0U;
};

template <int Identifier> class OrderedComponent final
{
  public:
    /// @brief Storage 破棄順を記録する Component 所有権を開始する
    explicit OrderedComponent(DestructionOrderState &a_state) noexcept
        : m_state(&a_state), m_isOwner(true)
    {
    }

    /// @brief 破棄順記録の複製を禁止する
    OrderedComponent(const OrderedComponent &) = delete;
    /// @brief 破棄順記録の複製代入を禁止する
    OrderedComponent &operator=(const OrderedComponent &) = delete;

    /// @brief Packed Storage 再配置時に記録責任を一度だけ移す
    OrderedComponent(OrderedComponent &&a_other) noexcept
        : m_state(a_other.m_state), m_isOwner(a_other.m_isOwner)
    {
        a_other.m_isOwner = false;
    }

    /// @brief Component が Move 代入を要求されないことを固定する
    OrderedComponent &operator=(OrderedComponent &&) = delete;

    /// @brief Storage が Logical Component を破棄した順序を記録する
    ~OrderedComponent() noexcept
    {
        if (m_isOwner)
        {
            m_state->values[m_state->count] = Identifier;
            ++m_state->count;
        }
    }

  private:
    DestructionOrderState *m_state;
    bool m_isOwner;
};

static_assert(std::is_nothrow_move_constructible_v<LifetimeComponent>);
static_assert(!std::is_move_assignable_v<LifetimeComponent>);
static_assert(!std::is_default_constructible_v<cue::game_core::EntityHandle>);
static_assert(!std::is_move_constructible_v<cue::game_core::World>);
static_assert(!std::is_default_constructible_v<
              cue::game_core::World::ConstructionKey>);
static_assert(!std::is_trivially_copyable_v<
              cue::game_core::World::ConstructionKey>);

/// @brief Result が指定した Cue.GameCore Error を保持するか判定する
template <typename T>
[[nodiscard]] bool has_game_core_error(
    const cue::Result<T> &a_result,
    cue::game_core::GameCoreError a_expected) noexcept
{
    const cue::Error *error = a_result.try_error();
    return error != nullptr && error->code().domain() == "Cue.GameCore" &&
           error->code().value() == static_cast<std::int64_t>(a_expected);
}

/// @brief Test Fixture 用の検証済み TypeId を生成する
[[nodiscard]] cue::schema::TypeId make_type_id(
    std::string_view a_text, const cue::AssertContext &a_assertContext)
{
    auto result = cue::schema::TypeId::parse(a_text, a_assertContext);

    if (!result)
    {
        std::abort();
    }

    return std::move(*result.try_value());
}

/// @brief Test Fixture 用の Field を持たない Type Descriptor を生成する
[[nodiscard]] cue::schema::TypeDescriptor make_type(
    std::string_view a_typeId, std::string_view a_name,
    const cue::AssertContext &a_assertContext)
{
    auto versionResult = cue::schema::SchemaVersion::create(1U, a_assertContext);

    if (!versionResult)
    {
        std::abort();
    }

    std::vector<cue::schema::FieldDescriptor> fields;
    std::vector<cue::schema::FieldId> reservedFieldIds;
    auto result = cue::schema::create_type_descriptor(
        make_type_id(a_typeId, a_assertContext), a_name,
        std::move(*versionResult.try_value()), std::move(fields),
        std::move(reservedFieldIds), a_assertContext);

    if (!result)
    {
        std::abort();
    }

    return std::move(*result.try_value());
}

/// @brief Position と Lifetime Component を登録した Schema Registry を生成する
[[nodiscard]] std::unique_ptr<cue::schema::SchemaRegistry> make_registry(
    cue::schema::SchemaRegistryIdentitySource &a_identitySource,
    const cue::AssertContext &a_assertContext)
{
    cue::schema::SchemaRegistryBuilder builder(a_identitySource,
                                               a_assertContext);
    auto position = builder.add_type(make_type(
        "10000000-0000-4000-8000-000000000001", "Cue.Test.Position",
        a_assertContext));
    auto lifetime = builder.add_type(make_type(
        "20000000-0000-4000-8000-000000000002", "Cue.Test.Lifetime",
        a_assertContext));
    auto registryResult = builder.seal();

    if (!position || !lifetime || !registryResult)
    {
        std::abort();
    }

    return std::move(*registryResult.try_value());
}

/// @brief World Factory の成功 Value を Test 用 Owner へ移す
[[nodiscard]] std::unique_ptr<cue::game_core::World> make_world(
    cue::game_core::WorldIdentitySource &a_identitySource,
    const cue::schema::SchemaRegistry &a_registry,
    const cue::AssertContext &a_assertContext)
{
    auto result = cue::game_core::World::create(
        a_identitySource, a_registry, a_assertContext);

    if (!result)
    {
        std::abort();
    }

    return std::move(*result.try_value());
}

/// @brief Entity の破棄、Slot 再利用、別 World で古い Handle を拒否することを検証する
[[nodiscard]] bool test_entity_generation_and_world_identity(
    const cue::schema::SchemaRegistry &a_registry,
    const cue::AssertContext &a_assertContext)
{
    cue::game_core::WorldIdentitySource identitySource;
    auto firstWorld = make_world(identitySource, a_registry, a_assertContext);
    auto secondWorld = make_world(identitySource, a_registry, a_assertContext);
    auto firstResult = firstWorld->create_entity();
    const auto *first = firstResult.try_value();

    if (first == nullptr)
    {
        return false;
    }

    const auto stale = *first;
    auto destroy = firstWorld->destroy_entity(stale);
    auto reusedResult = firstWorld->create_entity();
    const auto *reused = reusedResult.try_value();

    if (!destroy || reused == nullptr)
    {
        return false;
    }

    auto staleDestroy = firstWorld->destroy_entity(stale);
    return firstWorld->id() != secondWorld->id() &&
           stale.index() == reused->index() &&
           stale.generation() != reused->generation() &&
           !firstWorld->is_alive(stale) && firstWorld->is_alive(*reused) &&
           !secondWorld->is_alive(*reused) &&
           has_game_core_error(staleDestroy,
                               cue::game_core::GameCoreError::InvalidEntity);
}

/// @brief Component 登録、追加、取得、Swap-remove、寿命契約を検証する
[[nodiscard]] bool test_component_storage_and_lifetime(
    const cue::schema::SchemaRegistry &a_registry,
    const cue::AssertContext &a_assertContext)
{
    cue::game_core::WorldIdentitySource identitySource;
    auto world = make_world(identitySource, a_registry, a_assertContext);
    const auto positionId = make_type_id(
        "10000000-0000-4000-8000-000000000001", a_assertContext);
    const auto lifetimeId = make_type_id(
        "20000000-0000-4000-8000-000000000002", a_assertContext);
    auto positionTypeResult = world->register_component<Position>(positionId);
    auto lifetimeTypeResult =
        world->register_component<LifetimeComponent>(lifetimeId);
    auto conflict = world->register_component<LifetimeComponent>(positionId);
    const auto *positionType = positionTypeResult.try_value();
    const auto *lifetimeType = lifetimeTypeResult.try_value();

    if (positionType == nullptr || lifetimeType == nullptr)
    {
        return false;
    }

    auto otherWorld = make_world(identitySource, a_registry, a_assertContext);
    auto otherEntityResult = otherWorld->create_entity();
    const auto *otherEntity = otherEntityResult.try_value();

    if (otherEntity == nullptr)
    {
        return false;
    }

    auto foreignTypeAdd = otherWorld->add_component(
        *positionType, *otherEntity, 9, 9);

    auto firstResult = world->create_entity();
    auto secondResult = world->create_entity();
    const auto *first = firstResult.try_value();
    const auto *second = secondResult.try_value();

    if (first == nullptr || second == nullptr)
    {
        return false;
    }

    auto firstPosition = world->add_component(*positionType, *first, 1, 2);
    auto secondPosition = world->add_component(*positionType, *second, 3, 4);
    auto duplicatePosition = world->add_component(*positionType, *second, 5, 6);
    auto removeFirstPosition = world->remove_component(*positionType, *first);
    auto movedPosition = world->get_component(*positionType, *second);
    auto missingPosition = world->remove_component(*positionType, *first);
    auto hasSecondPosition = world->has_component(*positionType, *second);
    const auto *movedPositionPointer = movedPosition.try_value();
    const auto *hasSecond = hasSecondPosition.try_value();
    const bool movedPositionMatches =
        movedPositionPointer != nullptr && (*movedPositionPointer)->x == 3 &&
        (*movedPositionPointer)->y == 4;

    LifetimeState lifetimeState;
    auto firstLifetime = world->add_component(
        *lifetimeType, *first, lifetimeState);
    auto secondLifetime = world->add_component(
        *lifetimeType, *second, lifetimeState);
    auto removeFirstLifetime = world->remove_component(*lifetimeType, *first);
    auto destroySecond = world->destroy_entity(*second);

    return has_game_core_error(
               conflict, cue::game_core::GameCoreError::ComponentTypeConflict) &&
           has_game_core_error(
               foreignTypeAdd,
               cue::game_core::GameCoreError::UnregisteredComponent) &&
           firstPosition.has_value() && secondPosition.has_value() &&
           has_game_core_error(
               duplicatePosition,
               cue::game_core::GameCoreError::ComponentAlreadyExists) &&
           removeFirstPosition.has_value() && movedPositionMatches &&
           has_game_core_error(
               missingPosition,
               cue::game_core::GameCoreError::ComponentNotFound) &&
           hasSecond != nullptr && *hasSecond && firstLifetime.has_value() &&
           secondLifetime.has_value() && removeFirstLifetime.has_value() &&
           destroySecond.has_value() && lifetimeState.activeOwners == 0 &&
           lifetimeState.destroyedOwners == 2 && world->entity_count() == 1U;
}

/// @brief 10,000 Entity の追加、破棄、再利用で Sparse Set 整合性を検証する
[[nodiscard]] bool test_storage_stress(
    const cue::schema::SchemaRegistry &a_registry,
    const cue::AssertContext &a_assertContext)
{
    constexpr std::size_t entityCount = 10000U;
    cue::game_core::WorldIdentitySource identitySource;
    auto world = make_world(identitySource, a_registry, a_assertContext);
    auto positionTypeResult = world->register_component<Position>(make_type_id(
        "10000000-0000-4000-8000-000000000001", a_assertContext));
    const auto *positionType = positionTypeResult.try_value();

    if (positionType == nullptr)
    {
        return false;
    }

    std::vector<cue::game_core::EntityHandle> entities;
    entities.reserve(entityCount);

    for (std::size_t index = 0U; index < entityCount; ++index)
    {
        auto entityResult = world->create_entity();
        const auto *entity = entityResult.try_value();

        if (entity == nullptr)
        {
            return false;
        }

        entities.push_back(*entity);
        auto component = world->add_component(
            *positionType, *entity, static_cast<int>(index),
            static_cast<int>(index + 1U));

        if (!component)
        {
            return false;
        }
    }

    for (std::size_t index = 0U; index < entityCount; index += 2U)
    {
        if (!world->destroy_entity(entities[index]))
        {
            return false;
        }
    }

    for (std::size_t index = 1U; index < entityCount; index += 2U)
    {
        auto component = world->get_component(*positionType, entities[index]);
        const auto *pointer = component.try_value();

        if (pointer == nullptr || (*pointer)->x != static_cast<int>(index))
        {
            return false;
        }
    }

    std::vector<cue::game_core::EntityHandle> reusedEntities;
    reusedEntities.reserve(entityCount / 2U);

    for (std::size_t index = 0U; index < entityCount / 2U; ++index)
    {
        auto entityResult = world->create_entity();
        const auto *entity = entityResult.try_value();

        if (entity == nullptr || entities[entity->index()].generation() ==
                                     entity->generation())
        {
            return false;
        }

        reusedEntities.push_back(*entity);
        auto component = world->add_component(
            *positionType, *entity, -static_cast<int>(index),
            -static_cast<int>(index + 1U));

        if (!component || world->is_alive(entities[entity->index()]))
        {
            return false;
        }
    }

    for (const auto entity : reusedEntities)
    {
        auto component = world->get_component(*positionType, entity);

        if (!component)
        {
            return false;
        }
    }

    return world->entity_count() == entityCount;
}

/// @brief World 破棄時に Component Storage が逆作成順で解放されることを検証する
[[nodiscard]] bool test_reverse_storage_destruction(
    const cue::schema::SchemaRegistry &a_registry,
    const cue::AssertContext &a_assertContext)
{
    DestructionOrderState state;
    cue::game_core::WorldIdentitySource identitySource;

    {
        auto world = make_world(identitySource, a_registry, a_assertContext);
        auto firstTypeResult = world->register_component<OrderedComponent<1>>(
            make_type_id("10000000-0000-4000-8000-000000000001",
                         a_assertContext));
        auto secondTypeResult = world->register_component<OrderedComponent<2>>(
            make_type_id("20000000-0000-4000-8000-000000000002",
                         a_assertContext));
        auto entityResult = world->create_entity();
        const auto *firstType = firstTypeResult.try_value();
        const auto *secondType = secondTypeResult.try_value();
        const auto *entity = entityResult.try_value();

        if (firstType == nullptr || secondType == nullptr || entity == nullptr)
        {
            return false;
        }

        auto first = world->add_component(*firstType, *entity, state);
        auto second = world->add_component(*secondType, *entity, state);

        if (!first || !second)
        {
            return false;
        }
    }

    return state.count == 2U && state.values[0] == 2 && state.values[1] == 1;
}
} // namespace

/// @brief Cue.GameCore の Entity と Component Storage 契約を実行時に検証する
int main()
{
    TestFatalHandler fatalHandler;
    std::vector<std::unique_ptr<cue::LogSink>> sinks;
    cue::Logger logger(fatalHandler, std::move(sinks));
    cue::AssertContext assertContext(logger, fatalHandler);
    cue::schema::SchemaRegistryIdentitySource schemaIdentitySource;
    auto registry = make_registry(schemaIdentitySource, assertContext);

    if (!test_entity_generation_and_world_identity(*registry, assertContext))
    {
        return 1;
    }

    if (!test_component_storage_and_lifetime(*registry, assertContext))
    {
        return 2;
    }

    if (!test_storage_stress(*registry, assertContext))
    {
        return 3;
    }

    if (!test_reverse_storage_destruction(*registry, assertContext))
    {
        return 4;
    }

    return 0;
}

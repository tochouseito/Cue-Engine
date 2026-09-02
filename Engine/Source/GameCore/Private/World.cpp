#include <Cue/GameCore/World.h>

#include <Cue/Foundation/Fatal.h>

#include <exception>
#include <functional>

namespace cue::game_core
{
EntityHandle::EntityHandle(std::uint64_t a_worldId, std::uint32_t a_index,
                           std::uint32_t a_generation,
                           std::uint64_t a_validationToken,
                           const void *a_identitySource) noexcept
    : m_worldId(a_worldId), m_index(a_index), m_generation(a_generation),
      m_validationToken(a_validationToken), m_identitySource(a_identitySource)
{
}

EntityHandle::~EntityHandle() noexcept = default;

std::uint64_t EntityHandle::world_id() const noexcept
{
    return m_worldId;
}

std::uint32_t EntityHandle::index() const noexcept
{
    return m_index;
}

std::uint32_t EntityHandle::generation() const noexcept
{
    return m_generation;
}

std::optional<std::uint64_t> WorldIdentitySource::acquire_id() noexcept
{
    std::uint64_t candidate = m_nextId.load(std::memory_order_relaxed);

    while (candidate != 0U)
    {
        const std::uint64_t next =
            candidate == std::numeric_limits<std::uint64_t>::max()
                ? 0U
                : candidate + 1U;

        if (m_nextId.compare_exchange_weak(candidate, next,
                                           std::memory_order_relaxed,
                                           std::memory_order_relaxed))
        {
            return candidate;
        }
    }

    return std::nullopt;
}

Result<std::unique_ptr<World>> World::create(
    WorldIdentitySource &a_identitySource,
    const schema::SchemaRegistry &a_schemaRegistry,
    const AssertContext &a_assertContext) noexcept
{
    const auto worldId = a_identitySource.acquire_id();

    if (!worldId.has_value())
    {
        return Result<std::unique_ptr<World>>::failure(make_game_core_error(
            a_assertContext, GameCoreError::CapacityExceeded,
            "Process-local World ID space is exhausted"));
    }

    try
    {
        return Result<std::unique_ptr<World>>::success(std::make_unique<World>(
            ConstructionKey{}, worldId.value(), a_identitySource,
            a_schemaRegistry, a_assertContext));
    }
    catch (const std::bad_alloc &)
    {
        a_assertContext.fatal_handler().terminate(
            "Cue.GameCore world allocation failed");
    }
    catch (...)
    {
        a_assertContext.fatal_handler().terminate(
            "Cue.GameCore world construction raised an unexpected exception");
    }

    std::terminate();
}

World::World(ConstructionKey, std::uint64_t a_worldId,
             WorldIdentitySource &a_identitySource,
             const schema::SchemaRegistry &a_schemaRegistry,
             const AssertContext &a_assertContext)
    : m_worldId(a_worldId), m_identitySource(&a_identitySource),
      m_schemaRegistry(&a_schemaRegistry), m_assertContext(&a_assertContext),
      m_ownerThread(std::this_thread::get_id()),
      m_componentBindings(a_schemaRegistry.size() + 1U),
      m_componentStorages(a_schemaRegistry.size() + 1U)
{
}

World::~World() noexcept = default;

Result<EntityHandle> World::create_entity() noexcept
{
    assert_owner_thread();
    std::uint32_t index = 0U;

    if (!m_freeIndices.empty())
    {
        index = m_freeIndices.back();
        m_freeIndices.pop_back();
        m_slots[index].isAlive = true;
    }
    else
    {
        if (m_slots.size() >=
            static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
        {
            return Result<EntityHandle>::failure(make_game_core_error(
                *m_assertContext, GameCoreError::CapacityExceeded,
                "Entity Slot index space is exhausted"));
        }

        try
        {
            m_slots.emplace_back();
        }
        catch (const std::bad_alloc &)
        {
            terminate_allocation();
        }
        catch (...)
        {
            terminate_exception();
        }

        index = static_cast<std::uint32_t>(m_slots.size() - 1U);
        m_slots[index].isAlive = true;
    }

    ++m_entityCount;
    const auto generation = m_slots[index].generation;
    return Result<EntityHandle>::success(EntityHandle(
        m_worldId, index, generation,
        make_validation_token(index, generation), m_identitySource));
}

Result<void> World::destroy_entity(EntityHandle a_entity) noexcept
{
    assert_owner_thread();

    if (!validate_entity(a_entity))
    {
        return Result<void>::failure(make_game_core_error(
            *m_assertContext, GameCoreError::InvalidEntity,
            "Entity destruction requires a live handle from this world"));
    }

    auto &slot = m_slots[a_entity.index()];
    const bool retiresSlot =
        slot.generation == std::numeric_limits<std::uint32_t>::max();

    if (!retiresSlot)
    {
        try
        {
            m_freeIndices.reserve(m_freeIndices.size() + 1U);
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

    for (auto &storage : m_componentStorages)
    {
        if (storage != nullptr)
        {
            storage->remove_entity(a_entity.index());
        }
    }

    slot.isAlive = false;
    --m_entityCount;

    if (retiresSlot)
    {
        slot.isRetired = true;
    }
    else
    {
        ++slot.generation;
        m_freeIndices.push_back(a_entity.index());
    }

    return Result<void>::success();
}

bool World::is_alive(EntityHandle a_entity) const noexcept
{
    assert_owner_thread();
    return validate_entity(a_entity);
}

std::size_t World::entity_count() const noexcept
{
    assert_owner_thread();
    return m_entityCount;
}

std::uint64_t World::id() const noexcept
{
    return m_worldId;
}

void World::assert_owner_thread() const noexcept
{
    CUE_ASSERT(*m_assertContext, std::this_thread::get_id() == m_ownerThread,
               "Cue.GameCore World API requires its owner thread");
}

bool World::validate_entity(EntityHandle a_entity) const noexcept
{
    if (a_entity.m_identitySource != m_identitySource ||
        a_entity.world_id() != m_worldId ||
        a_entity.index() >= m_slots.size())
    {
        return false;
    }

    const auto &slot = m_slots[a_entity.index()];
    return slot.isAlive && !slot.isRetired &&
           slot.generation == a_entity.generation() &&
           a_entity.m_validationToken == make_validation_token(
               a_entity.index(), a_entity.generation());
}

std::uint64_t World::make_validation_token(
    std::uint32_t a_index, std::uint32_t a_generation) const noexcept
{
    std::uint64_t value = m_worldId;
    value ^= static_cast<std::uint64_t>(a_index) << 32U;
    value ^= static_cast<std::uint64_t>(a_generation);
    value ^= static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(this));
    value ^= static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(m_identitySource));
    value ^= value >> 30U;
    value *= 0xBF58476D1CE4E5B9ULL;
    value ^= value >> 27U;
    value *= 0x94D049BB133111EBULL;
    value ^= value >> 31U;
    return value == 0U ? 0x9E3779B97F4A7C15ULL : value;
}

[[noreturn]] void World::terminate_allocation() const noexcept
{
    m_assertContext->fatal_handler().terminate(
        "Cue.GameCore component storage allocation failed");
}

[[noreturn]] void World::terminate_exception() const noexcept
{
    m_assertContext->fatal_handler().terminate(
        "Cue.GameCore boundary caught an unexpected exception");
}
} // namespace cue::game_core

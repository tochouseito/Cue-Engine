#include <Cue/GameCore/CommandBuffer.h>

#include <Cue/Foundation/Fatal.h>

#include <exception>

namespace cue::game_core
{
StructuralCommandBuffer::StructuralCommandBuffer(World &a_world) noexcept
    : m_world(&a_world), m_assertContext(a_world.m_assertContext),
      m_ownerThread(std::this_thread::get_id())
{
    m_world->assert_owner_thread();
    m_world->assert_active();
}

StructuralCommandBuffer::~StructuralCommandBuffer() noexcept
{
    assert_recordable();
    discard_commands();
}

Result<PendingEntityId> StructuralCommandBuffer::create_entity() noexcept
{
    assert_recordable();

    if (m_generation == 0U ||
        m_nextPendingId == std::numeric_limits<std::uint64_t>::max())
    {
        return Result<PendingEntityId>::failure(make_game_core_error(
            *m_assertContext, GameCoreError::CapacityExceeded,
            "Command buffer pending entity space is exhausted"));
    }

    const PendingEntityId pendingId(this, m_generation, m_nextPendingId);

    try
    {
        m_commands.push_back(std::make_unique<CreateCommand>(pendingId));
        ++m_nextPendingId;
        auto resultValue = pendingId;
        return Result<PendingEntityId>::success(std::move(resultValue));
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

Result<void> StructuralCommandBuffer::destroy_entity(
    EntityHandle a_entity) noexcept
{
    assert_recordable();

    try
    {
        m_commands.push_back(
            std::make_unique<DestroyCommand>(Target(a_entity)));
        return Result<void>::success();
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

Result<void> StructuralCommandBuffer::destroy_entity(
    PendingEntityId a_entity) noexcept
{
    assert_recordable();

    if (!validate_pending(a_entity))
    {
        return invalid_pending_result();
    }

    try
    {
        m_commands.push_back(
            std::make_unique<DestroyCommand>(Target(a_entity)));
        return Result<void>::success();
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

StructuralCommandResult StructuralCommandBuffer::CreateCommand::apply(
    StructuralCommandBuffer &, World &a_world,
    std::vector<std::optional<EntityHandle>> &a_pendingEntities) noexcept
{
    auto result = a_world.create_entity();

    if (!result)
    {
        return StructuralCommandResult::failure(
            StructuralCommandKind::CreateEntity,
            std::move(*result.try_error()));
    }

    const auto entity = *result.try_value();
    const auto pendingIndex = static_cast<std::size_t>(m_pendingId.m_value);
    a_pendingEntities[pendingIndex] = entity;
    return StructuralCommandResult::success(
        StructuralCommandKind::CreateEntity,
        std::optional<EntityHandle>(entity));
}

StructuralCommandResult StructuralCommandBuffer::DestroyCommand::apply(
    StructuralCommandBuffer &a_buffer, World &a_world,
    std::vector<std::optional<EntityHandle>> &a_pendingEntities) noexcept
{
    auto entity = a_buffer.resolve_target(m_target, a_pendingEntities);

    if (!entity)
    {
        return StructuralCommandResult::failure(
            StructuralCommandKind::DestroyEntity,
            std::move(*entity.try_error()));
    }

    auto result = a_world.destroy_entity(*entity.try_value());

    if (!result)
    {
        return StructuralCommandResult::failure(
            StructuralCommandKind::DestroyEntity,
            std::move(*result.try_error()));
    }

    return StructuralCommandResult::success(
        StructuralCommandKind::DestroyEntity);
}

bool StructuralCommandBuffer::validate_pending(
    PendingEntityId a_pendingId) const noexcept
{
    return a_pendingId.m_buffer == this &&
           a_pendingId.m_generation == m_generation &&
           a_pendingId.m_value > 0U &&
           a_pendingId.m_value < m_nextPendingId;
}

Result<void> StructuralCommandBuffer::invalid_pending_result() const noexcept
{
    return Result<void>::failure(make_game_core_error(
        *m_assertContext, GameCoreError::InvalidCommandBuffer,
        "Pending entity ID does not belong to the current buffer generation"));
}

Result<EntityHandle> StructuralCommandBuffer::resolve_target(
    const Target &a_target,
    const std::vector<std::optional<EntityHandle>> &a_pendingEntities) const noexcept
{
    if (const auto *entity = std::get_if<EntityHandle>(&a_target))
    {
        auto resultValue = *entity;
        return Result<EntityHandle>::success(std::move(resultValue));
    }

    const auto &pending = std::get<PendingEntityId>(a_target);
    const auto pendingIndex = static_cast<std::size_t>(pending.m_value);

    if (!validate_pending(pending) || pendingIndex >= a_pendingEntities.size() ||
        !a_pendingEntities[pendingIndex].has_value())
    {
        return Result<EntityHandle>::failure(make_game_core_error(
            *m_assertContext, GameCoreError::DependencyFailed,
            "Pending entity dependency was not created successfully"));
    }

    auto resultValue = a_pendingEntities[pendingIndex].value();
    return Result<EntityHandle>::success(std::move(resultValue));
}

void StructuralCommandBuffer::assert_recordable() const noexcept
{
    m_world->assert_owner_thread();
    m_world->assert_active();
    CUE_ASSERT(*m_assertContext,
               std::this_thread::get_id() == m_ownerThread && !m_isFlushing,
               "Cue.GameCore command buffer requires its owner thread outside flush");

    if (std::this_thread::get_id() != m_ownerThread || m_isFlushing)
    {
        m_assertContext->fatal_handler().terminate(
            "Cue.GameCore command buffer requires its owner thread outside flush");
    }
}

void StructuralCommandBuffer::discard_commands() noexcept
{
    if (m_commands.empty())
    {
        return;
    }

    World::StructuralMutationScope mutationScope(*m_world);
    m_commands.clear();
}

void StructuralCommandBuffer::terminate_allocation() const noexcept
{
    m_assertContext->fatal_handler().terminate(
        "Cue.GameCore command buffer allocation failed");
}

void StructuralCommandBuffer::terminate_exception() const noexcept
{
    m_assertContext->fatal_handler().terminate(
        "Cue.GameCore command buffer raised an unexpected exception");
}

StructuralCommandReport World::flush_commands(
    StructuralCommandBuffer &a_commandBuffer) noexcept
{
    assert_owner_thread();
    assert_active();
    CUE_ASSERT(*m_assertContext,
               a_commandBuffer.m_world == this &&
                   !a_commandBuffer.m_isFlushing && !m_isQueryActive,
               "Cue.GameCore command flush requires its world safe point");

    if (a_commandBuffer.m_world != this || a_commandBuffer.m_isFlushing ||
        m_isQueryActive)
    {
        m_assertContext->fatal_handler().terminate(
            "Cue.GameCore command flush requires its world safe point");
    }

    try
    {
        StructuralCommandReport report;
        report.m_results.reserve(a_commandBuffer.m_commands.size());
        std::vector<std::optional<EntityHandle>> pendingEntities;
        pendingEntities.resize(
            static_cast<std::size_t>(a_commandBuffer.m_nextPendingId));
        a_commandBuffer.m_isFlushing = true;

        for (auto &command : a_commandBuffer.m_commands)
        {
            report.m_results.push_back(
                command->apply(a_commandBuffer, *this, pendingEntities));
        }

        a_commandBuffer.discard_commands();
        a_commandBuffer.m_isFlushing = false;
        a_commandBuffer.m_nextPendingId = 1U;
        a_commandBuffer.m_generation =
            a_commandBuffer.m_generation ==
                    std::numeric_limits<std::uint64_t>::max()
                ? 0U
                : a_commandBuffer.m_generation + 1U;
        return report;
    }
    catch (const std::bad_alloc &)
    {
        a_commandBuffer.m_isFlushing = false;
        terminate_allocation();
    }
    catch (...)
    {
        a_commandBuffer.m_isFlushing = false;
        terminate_exception();
    }
}
} // namespace cue::game_core

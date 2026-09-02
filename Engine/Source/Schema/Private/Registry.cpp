#include <Cue/Schema/Registry.h>

#include <Cue/Foundation/Assert.h>
#include <Cue/Schema/Error.h>

#include <algorithm>
#include <limits>
#include <new>

namespace
{
/// @brief Registry構築中のAllocation失敗をEmergency終了へ変換する
[[noreturn]] void terminate_registry_allocation(
    const cue::AssertContext &a_assertContext) noexcept
{
    a_assertContext.fatal_handler().terminate("Cue.Schema registry allocation failed");
}

/// @brief Registry構築中の予期しない例外をEmergency終了へ変換する
[[noreturn]] void terminate_registry_exception(
    const cue::AssertContext &a_assertContext) noexcept
{
    a_assertContext.fatal_handler().terminate("Cue.Schema registry unexpected exception");
}
} // namespace

namespace cue::schema
{
SchemaRegistry::SchemaRegistry(std::vector<TypeDescriptor> &&a_descriptors,
                               std::vector<TypeId> &&a_tombstones) noexcept
    : m_descriptors(std::move(a_descriptors)), m_tombstones(std::move(a_tombstones))
{
}

std::size_t SchemaRegistry::size() const noexcept
{
    return m_descriptors.size();
}

const TypeDescriptor *SchemaRegistry::find(TypeId a_id) const noexcept
{
    const auto iterator = std::lower_bound(
        m_descriptors.begin(), m_descriptors.end(), a_id,
        /// @brief DescriptorのStable TypeIdが検索値より小さいか判定する
        [](const TypeDescriptor &a_descriptor, TypeId a_value) noexcept
        {
            return a_descriptor.id() < a_value;
        });
    return iterator != m_descriptors.end() && iterator->id() == a_id
               ? &*iterator
               : nullptr;
}

const TypeDescriptor *SchemaRegistry::find(DenseTypeIndex a_index) const noexcept
{
    const auto value = a_index.value();

    if (value == 0U || value > m_descriptors.size())
    {
        return nullptr;
    }

    return &m_descriptors[value - 1U];
}

std::optional<DenseTypeIndex> SchemaRegistry::dense_index(TypeId a_id) const noexcept
{
    const auto iterator = std::lower_bound(
        m_descriptors.begin(), m_descriptors.end(), a_id,
        /// @brief DescriptorのStable TypeIdが検索値より小さいか判定する
        [](const TypeDescriptor &a_descriptor, TypeId a_value) noexcept
        {
            return a_descriptor.id() < a_value;
        });

    if (iterator == m_descriptors.end() || iterator->id() != a_id)
    {
        return std::nullopt;
    }

    const auto offset = static_cast<std::size_t>(iterator - m_descriptors.begin());
    return DenseTypeIndex(static_cast<std::uint32_t>(offset + 1U));
}

bool SchemaRegistry::is_tombstoned(TypeId a_id) const noexcept
{
    return std::binary_search(m_tombstones.begin(), m_tombstones.end(), a_id);
}

SchemaRegistryBuilder::SchemaRegistryBuilder(
    const AssertContext &a_assertContext) noexcept
    : m_assertContext(&a_assertContext), m_ownerThread(std::this_thread::get_id())
{
}

Result<void> SchemaRegistryBuilder::add_type(TypeDescriptor &&a_descriptor) noexcept
{
    CUE_ASSERT(*m_assertContext, std::this_thread::get_id() == m_ownerThread,
               "SchemaRegistryBuilder must be used on its owner thread");
    CUE_ASSERT(*m_assertContext, !m_isSealed,
               "SchemaRegistryBuilder cannot register a type after seal");

    if (std::any_of(m_descriptors.begin(), m_descriptors.end(),
                    /// @brief 既存Descriptorが追加対象と同じTypeIdを持つか判定する
                    [&a_descriptor](const TypeDescriptor &a_existing) noexcept
                    {
                        return a_existing.id() == a_descriptor.id();
                    }))
    {
        return Result<void>::failure(make_schema_error(
            *m_assertContext, SchemaError::DuplicateTypeId,
            "Schema registry contains a duplicate TypeId"));
    }

    if (std::any_of(m_descriptors.begin(), m_descriptors.end(),
                    /// @brief 既存Descriptorが追加対象と同じ診断名を持つか判定する
                    [&a_descriptor](const TypeDescriptor &a_existing) noexcept
                    {
                        return a_existing.name() == a_descriptor.name();
                    }))
    {
        return Result<void>::failure(make_schema_error(
            *m_assertContext, SchemaError::DuplicateTypeName,
            "Schema registry contains a duplicate type name"));
    }

    if (std::find(m_tombstones.begin(), m_tombstones.end(), a_descriptor.id()) !=
        m_tombstones.end())
    {
        return Result<void>::failure(make_schema_error(
            *m_assertContext, SchemaError::TombstonedTypeId,
            "Active TypeId cannot reuse a tombstone"));
    }

    try
    {
        m_descriptors.push_back(std::move(a_descriptor));
    }
    catch (const std::bad_alloc &)
    {
        terminate_registry_allocation(*m_assertContext);
    }
    catch (...)
    {
        terminate_registry_exception(*m_assertContext);
    }

    return Result<void>::success();
}

Result<void> SchemaRegistryBuilder::add_tombstone(TypeId a_id) noexcept
{
    CUE_ASSERT(*m_assertContext, std::this_thread::get_id() == m_ownerThread,
               "SchemaRegistryBuilder must be used on its owner thread");
    CUE_ASSERT(*m_assertContext, !m_isSealed,
               "SchemaRegistryBuilder cannot register a tombstone after seal");

    if (std::find(m_tombstones.begin(), m_tombstones.end(), a_id) !=
        m_tombstones.end())
    {
        return Result<void>::failure(make_schema_error(
            *m_assertContext, SchemaError::DuplicateTombstone,
            "Schema registry contains a duplicate TypeId tombstone"));
    }

    if (std::any_of(m_descriptors.begin(), m_descriptors.end(),
                    /// @brief Active Descriptorが追加対象Tombstoneと同じTypeIdを持つか判定する
                    [a_id](const TypeDescriptor &a_descriptor) noexcept
                    {
                        return a_descriptor.id() == a_id;
                    }))
    {
        return Result<void>::failure(make_schema_error(
            *m_assertContext, SchemaError::TombstonedTypeId,
            "TypeId cannot be active and tombstoned"));
    }

    try
    {
        m_tombstones.push_back(a_id);
    }
    catch (const std::bad_alloc &)
    {
        terminate_registry_allocation(*m_assertContext);
    }
    catch (...)
    {
        terminate_registry_exception(*m_assertContext);
    }

    return Result<void>::success();
}

Result<SchemaRegistry> SchemaRegistryBuilder::seal() noexcept
{
    CUE_ASSERT(*m_assertContext, std::this_thread::get_id() == m_ownerThread,
               "SchemaRegistryBuilder must be sealed on its owner thread");
    CUE_ASSERT(*m_assertContext, !m_isSealed,
               "SchemaRegistryBuilder can be sealed only once");

    constexpr auto maximumTypeCount =
        static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) - 1U;

    if (m_descriptors.size() > maximumTypeCount)
    {
        return Result<SchemaRegistry>::failure(make_schema_error(
            *m_assertContext, SchemaError::CapacityExceeded,
            "Schema registry exceeds the DenseTypeIndex capacity"));
    }

    std::sort(m_descriptors.begin(), m_descriptors.end(),
              /// @brief DescriptorをStable TypeId順へ並べDense Indexを決定する
              [](const TypeDescriptor &a_left, const TypeDescriptor &a_right) noexcept
              {
                  return a_left.id() < a_right.id();
              });
    std::sort(m_tombstones.begin(), m_tombstones.end());
    m_isSealed = true;
    return Result<SchemaRegistry>::success(
        SchemaRegistry(std::move(m_descriptors), std::move(m_tombstones)));
}
} // namespace cue::schema

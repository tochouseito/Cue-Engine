#include <Cue/Schema/Registry.h>

#include <Cue/Foundation/Assert.h>
#include <Cue/Schema/Error.h>

#include <algorithm>
#include <atomic>
#include <limits>
#include <new>
#include <string>

namespace
{
std::atomic_uint64_t g_nextRegistryGeneration = 1U;

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

/// @brief TypeIdをAllocationなしのlowercase canonical UUIDとして既存文字列へ追記する
void append_type_id(std::string &a_destination, cue::schema::TypeId a_id)
{
    constexpr char hexDigits[] = "0123456789abcdef";
    std::size_t byteIndex = 0U;

    for (const auto byte : a_id.bytes())
    {
        if (byteIndex == 4U || byteIndex == 6U || byteIndex == 8U || byteIndex == 10U)
        {
            a_destination.push_back('-');
        }

        a_destination.push_back(hexDigits[(byte >> 4U) & 0x0FU]);
        a_destination.push_back(hexDigits[byte & 0x0FU]);
        ++byteIndex;
    }
}

/// @brief TypeId衝突の規則と両方の診断名を一つのErrorへ保持する
[[nodiscard]] cue::Error make_type_id_collision_error(
    const cue::AssertContext &a_assertContext, cue::schema::SchemaError a_code,
    std::string_view a_rule, cue::schema::TypeId a_id,
    std::string_view a_existingName, std::string_view a_incomingName) noexcept
{
    try
    {
        std::string summary(a_rule);
        summary.append(" TypeId=");
        append_type_id(summary, a_id);
        summary.append(" ExistingName=");
        summary.append(a_existingName);
        summary.append(" IncomingName=");
        summary.append(a_incomingName);
        return cue::schema::make_schema_error(a_assertContext, a_code, summary);
    }
    catch (const std::bad_alloc &)
    {
        terminate_registry_allocation(a_assertContext);
    }
    catch (...)
    {
        terminate_registry_exception(a_assertContext);
    }
}

/// @brief Canonical Name衝突の規則と両方のTypeIdを一つのErrorへ保持する
[[nodiscard]] cue::Error make_type_name_collision_error(
    const cue::AssertContext &a_assertContext, std::string_view a_name,
    cue::schema::TypeId a_existingId, cue::schema::TypeId a_incomingId) noexcept
{
    try
    {
        std::string summary("DuplicateTypeName CanonicalName=");
        summary.append(a_name);
        summary.append(" ExistingTypeId=");
        append_type_id(summary, a_existingId);
        summary.append(" IncomingTypeId=");
        append_type_id(summary, a_incomingId);
        return cue::schema::make_schema_error(
            a_assertContext, cue::schema::SchemaError::DuplicateTypeName, summary);
    }
    catch (const std::bad_alloc &)
    {
        terminate_registry_allocation(a_assertContext);
    }
    catch (...)
    {
        terminate_registry_exception(a_assertContext);
    }
}

/// @brief Tombstone衝突の規則とTypeIdおよび追加元診断名を一つのErrorへ保持する
[[nodiscard]] cue::Error make_tombstone_collision_error(
    const cue::AssertContext &a_assertContext, cue::schema::SchemaError a_code,
    std::string_view a_rule, cue::schema::TypeId a_id,
    std::string_view a_incomingName) noexcept
{
    try
    {
        std::string summary(a_rule);
        summary.append(" TypeId=");
        append_type_id(summary, a_id);

        if (!a_incomingName.empty())
        {
            summary.append(" IncomingName=");
            summary.append(a_incomingName);
        }

        return cue::schema::make_schema_error(a_assertContext, a_code, summary);
    }
    catch (const std::bad_alloc &)
    {
        terminate_registry_allocation(a_assertContext);
    }
    catch (...)
    {
        terminate_registry_exception(a_assertContext);
    }
}

/// @brief Process内で再利用しないRegistry Generationを予約する
[[nodiscard]] std::optional<std::uint64_t> acquire_registry_generation() noexcept
{
    auto current = g_nextRegistryGeneration.load(std::memory_order_relaxed);
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();

    while (current != maximum)
    {
        if (g_nextRegistryGeneration.compare_exchange_weak(
                current, current + 1U, std::memory_order_relaxed,
                std::memory_order_relaxed))
        {
            return current;
        }
    }

    return std::nullopt;
}
} // namespace

namespace cue::schema
{
SchemaRegistry::SchemaRegistry(std::vector<TypeDescriptor> &&a_descriptors,
                               std::vector<TypeId> &&a_tombstones,
                               std::uint64_t a_generation) noexcept
    : m_descriptors(std::move(a_descriptors)), m_tombstones(std::move(a_tombstones)),
      m_generation(a_generation)
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

    if (a_index.m_registryGeneration != m_generation || value == 0U ||
        value > m_descriptors.size())
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
    return DenseTypeIndex(static_cast<std::uint32_t>(offset + 1U), m_generation);
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

    if (m_hasFailed)
    {
        return Result<void>::failure(make_schema_error(
            *m_assertContext, SchemaError::BuilderFailed,
            "Schema registry builder previously rejected a registration"));
    }

    const auto duplicateType = std::find_if(
        m_descriptors.begin(), m_descriptors.end(),
        /// @brief 既存Descriptorが追加対象と同じTypeIdを持つか判定する
        [&a_descriptor](const TypeDescriptor &a_existing) noexcept
        {
            return a_existing.id() == a_descriptor.id();
        });

    if (duplicateType != m_descriptors.end())
    {
        m_hasFailed = true;
        return Result<void>::failure(make_type_id_collision_error(
            *m_assertContext, SchemaError::DuplicateTypeId, "DuplicateTypeId",
            a_descriptor.id(), duplicateType->name(), a_descriptor.name()));
    }

    const auto duplicateName = std::find_if(
        m_descriptors.begin(), m_descriptors.end(),
        /// @brief 既存Descriptorが追加対象と同じ診断名を持つか判定する
        [&a_descriptor](const TypeDescriptor &a_existing) noexcept
        {
            return a_existing.name() == a_descriptor.name();
        });

    if (duplicateName != m_descriptors.end())
    {
        m_hasFailed = true;
        return Result<void>::failure(make_type_name_collision_error(
            *m_assertContext, a_descriptor.name(), duplicateName->id(),
            a_descriptor.id()));
    }

    if (std::find(m_tombstones.begin(), m_tombstones.end(), a_descriptor.id()) !=
        m_tombstones.end())
    {
        m_hasFailed = true;
        return Result<void>::failure(make_tombstone_collision_error(
            *m_assertContext, SchemaError::TombstonedTypeId,
            "ActiveTypeIdReusesTombstone", a_descriptor.id(), a_descriptor.name()));
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

    if (m_hasFailed)
    {
        return Result<void>::failure(make_schema_error(
            *m_assertContext, SchemaError::BuilderFailed,
            "Schema registry builder previously rejected a registration"));
    }

    if (std::find(m_tombstones.begin(), m_tombstones.end(), a_id) !=
        m_tombstones.end())
    {
        m_hasFailed = true;
        return Result<void>::failure(make_tombstone_collision_error(
            *m_assertContext, SchemaError::DuplicateTombstone,
            "DuplicateTombstone", a_id, {}));
    }

    const auto activeType = std::find_if(
        m_descriptors.begin(), m_descriptors.end(),
        /// @brief Active Descriptorが追加対象Tombstoneと同じTypeIdを持つか判定する
        [a_id](const TypeDescriptor &a_descriptor) noexcept
        {
            return a_descriptor.id() == a_id;
        });

    if (activeType != m_descriptors.end())
    {
        m_hasFailed = true;
        return Result<void>::failure(make_tombstone_collision_error(
            *m_assertContext, SchemaError::TombstonedTypeId,
            "TombstoneConflictsWithActiveType", a_id, activeType->name()));
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

    if (m_hasFailed)
    {
        return Result<SchemaRegistry>::failure(make_schema_error(
            *m_assertContext, SchemaError::BuilderFailed,
            "Schema registry builder previously rejected a registration and cannot seal"));
    }

    constexpr auto maximumTypeCount =
        static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) - 1U;

    if (m_descriptors.size() > maximumTypeCount)
    {
        m_hasFailed = true;
        return Result<SchemaRegistry>::failure(make_schema_error(
            *m_assertContext, SchemaError::CapacityExceeded,
            "Schema registry exceeds the DenseTypeIndex capacity"));
    }

    const auto generation = acquire_registry_generation();

    if (!generation.has_value())
    {
        m_hasFailed = true;
        return Result<SchemaRegistry>::failure(make_schema_error(
            *m_assertContext, SchemaError::CapacityExceeded,
            "Schema registry generation capacity is exhausted"));
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
        SchemaRegistry(std::move(m_descriptors), std::move(m_tombstones),
                       *generation));
}
} // namespace cue::schema

#include <Cue/Schema/Registry.h>

#include <Cue/Foundation/Assert.h>
#include <Cue/Schema/Error.h>

#include <algorithm>
#include <limits>
#include <new>
#include <string>

namespace
{
/// @brief Registry 構築中の Allocation 失敗を Emergency 終了へ変換する
[[noreturn]] void terminate_registry_allocation(
    const cue::AssertContext &a_assertContext) noexcept
{
    a_assertContext.fatal_handler().terminate("Cue.Schema registry allocation failed");
}

/// @brief Registry 構築中の予期しない例外を Emergency 終了へ変換する
[[noreturn]] void terminate_registry_exception(
    const cue::AssertContext &a_assertContext) noexcept
{
    a_assertContext.fatal_handler().terminate("Cue.Schema registry unexpected exception");
}

/// @brief TypeId を Allocation なしの lowercase canonical UUID として既存文字列へ追記する
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

/// @brief TypeId 衝突の規則と両方の診断名を一つの Error へ保持する
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

/// @brief Canonical Name 衝突の規則と両方の TypeId を一つの Error へ保持する
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

/// @brief Tombstone 衝突の規則と TypeId および追加元診断名を一つの Error へ保持する
[[nodiscard]] cue::Error make_tombstone_collision_error(
    const cue::AssertContext &a_assertContext, cue::schema::SchemaError a_code,
    std::string_view a_rule, cue::schema::TypeId a_id,
    std::string_view a_existingName, std::string_view a_incomingName) noexcept
{
    try
    {
        std::string summary(a_rule);
        summary.append(" TypeId=");
        append_type_id(summary, a_id);

        summary.append(" ExistingSource=");
        summary.append(a_existingName);
        summary.append(" IncomingSource=");
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

/// @brief Tombstone 登録元名が安定した ASCII 診断 Token か検証する
[[nodiscard]] bool is_valid_registration_source(std::string_view a_sourceName) noexcept
{
    if (a_sourceName.empty() || a_sourceName.size() > 128U)
    {
        return false;
    }

    for (const char character : a_sourceName)
    {
        const bool isLetter = (character >= 'A' && character <= 'Z') ||
                              (character >= 'a' && character <= 'z');
        const bool isDigit = character >= '0' && character <= '9';
        const bool isSeparator = character == '.' || character == '_' ||
                                 character == ':' || character == '-';

        if (!isLetter && !isDigit && !isSeparator)
        {
            return false;
        }
    }

    return true;
}

} // namespace

namespace cue::schema
{
std::optional<std::uint64_t> SchemaRegistryIdentitySource::acquire_generation() noexcept
{
    auto current = m_nextGeneration.load(std::memory_order_relaxed);
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();

    while (current != maximum)
    {
        if (m_nextGeneration.compare_exchange_weak(
                current, current + 1U, std::memory_order_relaxed,
                std::memory_order_relaxed))
        {
            return current;
        }
    }

    return std::nullopt;
}

SchemaRegistry::SchemaRegistry(ConstructionKey,
                               std::vector<TypeDescriptor> &&a_descriptors,
                               std::vector<TypeId> &&a_tombstones,
                               const SchemaRegistryIdentitySource &a_identitySource,
                               std::uint64_t a_generation) noexcept
    : m_descriptors(std::move(a_descriptors)), m_tombstones(std::move(a_tombstones)),
      m_identitySource(&a_identitySource), m_generation(a_generation)
{
}

std::size_t SchemaRegistry::size() const noexcept
{
    return m_descriptors.size();
}

Result<const TypeDescriptor *> SchemaRegistry::find(
    TypeId a_id, const AssertContext &a_assertContext) const noexcept
{
    const auto iterator = std::lower_bound(
        m_descriptors.begin(), m_descriptors.end(), a_id,
        /// @brief Descriptor の Stable TypeId が検索値より小さいか判定する
        [](const TypeDescriptor &a_descriptor, TypeId a_value) noexcept
        {
            return a_descriptor.id() < a_value;
        });
    if (iterator != m_descriptors.end() && iterator->id() == a_id)
    {
        return Result<const TypeDescriptor *>::success(&*iterator);
    }

    return Result<const TypeDescriptor *>::failure(make_schema_error(
        a_assertContext, SchemaError::NotFound,
        "Schema TypeId is not registered in this registry"));
}

const TypeDescriptor *SchemaRegistry::find(DenseTypeIndex a_index) const noexcept
{
    const auto value = a_index.value();

    if (a_index.m_identitySource != m_identitySource ||
        a_index.m_registryGeneration != m_generation || value == 0U ||
        value > m_descriptors.size())
    {
        return nullptr;
    }

    return &m_descriptors[value - 1U];
}

Result<DenseTypeIndex> SchemaRegistry::dense_index(
    TypeId a_id, const AssertContext &a_assertContext) const noexcept
{
    const auto iterator = std::lower_bound(
        m_descriptors.begin(), m_descriptors.end(), a_id,
        /// @brief Descriptor の Stable TypeId が検索値より小さいか判定する
        [](const TypeDescriptor &a_descriptor, TypeId a_value) noexcept
        {
            return a_descriptor.id() < a_value;
        });

    if (iterator == m_descriptors.end() || iterator->id() != a_id)
    {
        return Result<DenseTypeIndex>::failure(make_schema_error(
            a_assertContext, SchemaError::NotFound,
            "Schema TypeId is not registered in this registry"));
    }

    const auto offset = static_cast<std::size_t>(iterator - m_descriptors.begin());
    return Result<DenseTypeIndex>::success(
        DenseTypeIndex(static_cast<std::uint32_t>(offset + 1U),
                       *m_identitySource, m_generation));
}

bool SchemaRegistry::is_tombstoned(TypeId a_id) const noexcept
{
    return std::binary_search(m_tombstones.begin(), m_tombstones.end(), a_id);
}

SchemaRegistryBuilder::SchemaRegistryBuilder(
    SchemaRegistryIdentitySource &a_identitySource,
    const AssertContext &a_assertContext) noexcept
    : m_assertContext(&a_assertContext), m_identitySource(&a_identitySource),
      m_ownerThread(std::this_thread::get_id())
{
}

Result<void> SchemaRegistryBuilder::add_type(TypeDescriptor &&a_descriptor) noexcept
{
    CUE_ASSERT(*m_assertContext, std::this_thread::get_id() == m_ownerThread,
               "SchemaRegistryBuilder must be used on its owner thread");

    if (m_isSealed)
    {
        return Result<void>::failure(make_schema_error(
            *m_assertContext, SchemaError::BuilderSealed,
            "Schema registry builder cannot register a type after seal"));
    }

    if (m_hasFailed)
    {
        return Result<void>::failure(make_schema_error(
            *m_assertContext, SchemaError::BuilderFailed,
            "Schema registry builder previously rejected a registration"));
    }

    auto descriptorValidation =
        validate_type_descriptor(a_descriptor, *m_assertContext);

    if (!descriptorValidation)
    {
        m_hasFailed = true;
        return descriptorValidation;
    }

    const auto duplicateType = std::find_if(
        m_descriptors.begin(), m_descriptors.end(),
        /// @brief 既存 Descriptor が追加対象と同じ TypeId を持つか判定する
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
        /// @brief 既存 Descriptor が追加対象と同じ診断名を持つか判定する
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

    const auto tombstone =
        std::find(m_tombstones.begin(), m_tombstones.end(), a_descriptor.id());

    if (tombstone != m_tombstones.end())
    {
        const auto offset = static_cast<std::size_t>(tombstone - m_tombstones.begin());
        m_hasFailed = true;
        return Result<void>::failure(make_tombstone_collision_error(
            *m_assertContext, SchemaError::TombstonedTypeId,
            "ActiveTypeIdReusesTombstone", a_descriptor.id(),
            m_tombstoneSources[offset], a_descriptor.name()));
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

Result<void> SchemaRegistryBuilder::add_tombstone(
    TypeId a_id, std::string_view a_sourceName) noexcept
{
    CUE_ASSERT(*m_assertContext, std::this_thread::get_id() == m_ownerThread,
               "SchemaRegistryBuilder must be used on its owner thread");

    if (m_isSealed)
    {
        return Result<void>::failure(make_schema_error(
            *m_assertContext, SchemaError::BuilderSealed,
            "Schema registry builder cannot register a tombstone after seal"));
    }

    if (m_hasFailed)
    {
        return Result<void>::failure(make_schema_error(
            *m_assertContext, SchemaError::BuilderFailed,
            "Schema registry builder previously rejected a registration"));
    }

    if (!is_valid_registration_source(a_sourceName))
    {
        m_hasFailed = true;
        return Result<void>::failure(make_schema_error(
            *m_assertContext, SchemaError::InvalidName,
            "Tombstone source must be a 1 to 128 byte ASCII diagnostic token"));
    }

    const auto duplicateTombstone =
        std::find(m_tombstones.begin(), m_tombstones.end(), a_id);

    if (duplicateTombstone != m_tombstones.end())
    {
        const auto offset = static_cast<std::size_t>(
            duplicateTombstone - m_tombstones.begin());
        m_hasFailed = true;
        return Result<void>::failure(make_tombstone_collision_error(
            *m_assertContext, SchemaError::DuplicateTombstone,
            "DuplicateTombstone", a_id, m_tombstoneSources[offset], a_sourceName));
    }

    const auto activeType = std::find_if(
        m_descriptors.begin(), m_descriptors.end(),
        /// @brief Active Descriptor が追加対象 Tombstone と同じ TypeId を持つか判定する
        [a_id](const TypeDescriptor &a_descriptor) noexcept
        {
            return a_descriptor.id() == a_id;
        });

    if (activeType != m_descriptors.end())
    {
        m_hasFailed = true;
        return Result<void>::failure(make_tombstone_collision_error(
            *m_assertContext, SchemaError::TombstonedTypeId,
            "TombstoneConflictsWithActiveType", a_id, activeType->name(),
            a_sourceName));
    }

    try
    {
        m_tombstones.push_back(a_id);
        m_tombstoneSources.emplace_back(a_sourceName);
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

Result<std::unique_ptr<SchemaRegistry>> SchemaRegistryBuilder::seal() noexcept
{
    CUE_ASSERT(*m_assertContext, std::this_thread::get_id() == m_ownerThread,
               "SchemaRegistryBuilder must be sealed on its owner thread");

    if (m_isSealed)
    {
        return Result<std::unique_ptr<SchemaRegistry>>::failure(make_schema_error(
            *m_assertContext, SchemaError::BuilderSealed,
            "Schema registry builder can be sealed only once"));
    }

    if (m_hasFailed)
    {
        return Result<std::unique_ptr<SchemaRegistry>>::failure(make_schema_error(
            *m_assertContext, SchemaError::BuilderFailed,
            "Schema registry builder previously rejected a registration and cannot seal"));
    }

    constexpr auto maximumTypeCount =
        static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) - 1U;

    if (m_descriptors.size() > maximumTypeCount)
    {
        m_hasFailed = true;
        return Result<std::unique_ptr<SchemaRegistry>>::failure(make_schema_error(
            *m_assertContext, SchemaError::CapacityExceeded,
            "Schema registry exceeds the DenseTypeIndex capacity"));
    }

    const auto generation = m_identitySource->acquire_generation();

    if (!generation.has_value())
    {
        m_hasFailed = true;
        return Result<std::unique_ptr<SchemaRegistry>>::failure(make_schema_error(
            *m_assertContext, SchemaError::CapacityExceeded,
            "Schema registry generation capacity is exhausted"));
    }

    std::sort(m_descriptors.begin(), m_descriptors.end(),
              /// @brief Descriptor を Stable TypeId 順へ並べ Dense Index を決定する
              [](const TypeDescriptor &a_left, const TypeDescriptor &a_right) noexcept
              {
                  return a_left.id() < a_right.id();
              });
    std::sort(m_tombstones.begin(), m_tombstones.end());
    std::unique_ptr<SchemaRegistry> registry;

    try
    {
        registry = std::make_unique<SchemaRegistry>(
            SchemaRegistry::ConstructionKey{}, std::move(m_descriptors),
            std::move(m_tombstones), *m_identitySource, *generation);
    }
    catch (const std::bad_alloc &)
    {
        terminate_registry_allocation(*m_assertContext);
    }
    catch (...)
    {
        terminate_registry_exception(*m_assertContext);
    }

    m_isSealed = true;
    return Result<std::unique_ptr<SchemaRegistry>>::success(std::move(registry));
}
} // namespace cue::schema

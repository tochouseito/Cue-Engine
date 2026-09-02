#include <Cue/Foundation/Assert.h>
#include <Cue/Foundation/Fatal.h>
#include <Cue/Foundation/Log.h>
#include <Cue/Schema/Descriptor.h>
#include <Cue/Schema/Error.h>
#include <Cue/Schema/Registry.h>
#include <Cue/Schema/Types.h>

#include <atomic>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
static_assert(std::is_move_constructible_v<cue::schema::SchemaRegistry>);
static_assert(!std::is_move_assignable_v<cue::schema::SchemaRegistry>);

class TestFatalHandler final : public cue::FatalHandler
{
  public:
    /// @brief Test中の通常FatalをProcess失敗へ変換する
    [[noreturn]] void terminate() noexcept override
    {
        std::abort();
    }

    /// @brief Test中のEmergency FatalをProcess失敗へ変換する
    [[noreturn]] void terminate(std::string_view) noexcept override
    {
        std::abort();
    }
};

/// @brief Resultが指定したCue.Schema Errorを保持するか判定する
template <typename T>
[[nodiscard]] bool has_schema_error(const cue::Result<T> &a_result,
                                    cue::schema::SchemaError a_expected) noexcept
{
    const cue::Error *error = a_result.try_error();
    return error != nullptr && error->code().domain() == "Cue.Schema" &&
           error->code().value() == static_cast<std::int64_t>(a_expected);
}

/// @brief Test Fixture用の検証済みTypeIdを生成する
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

/// @brief Test Fixture用の検証済みFieldIdを生成する
[[nodiscard]] cue::schema::FieldId make_field_id(
    std::uint32_t a_value, const cue::AssertContext &a_assertContext)
{
    auto result = cue::schema::FieldId::create(a_value, a_assertContext);

    if (!result)
    {
        std::abort();
    }

    return std::move(*result.try_value());
}

/// @brief Test Fixture用の検証済みSchemaVersionを生成する
[[nodiscard]] cue::schema::SchemaVersion make_version(
    const cue::AssertContext &a_assertContext)
{
    auto result = cue::schema::SchemaVersion::create(1U, a_assertContext);

    if (!result)
    {
        std::abort();
    }

    return std::move(*result.try_value());
}

/// @brief Test Fixture用のField Descriptorを生成する
[[nodiscard]] cue::schema::FieldDescriptor make_field(
    std::uint32_t a_id, std::string_view a_name,
    const cue::AssertContext &a_assertContext)
{
    auto result = cue::schema::create_field_descriptor(
        make_field_id(a_id, a_assertContext), a_name, a_assertContext);

    if (!result)
    {
        std::abort();
    }

    return std::move(*result.try_value());
}

/// @brief Test Fixture用の単一Field Type Descriptorを生成する
[[nodiscard]] cue::schema::TypeDescriptor make_type(
    std::string_view a_typeId, std::string_view a_name,
    const cue::AssertContext &a_assertContext)
{
    std::vector<cue::schema::FieldDescriptor> fields;
    fields.push_back(make_field(1U, "value", a_assertContext));
    std::vector<cue::schema::FieldId> reservedFieldIds;
    auto result = cue::schema::create_type_descriptor(
        make_type_id(a_typeId, a_assertContext), a_name,
        make_version(a_assertContext), std::move(fields),
        std::move(reservedFieldIds), a_assertContext);

    if (!result)
    {
        std::abort();
    }

    return std::move(*result.try_value());
}

/// @brief Stable Identity Valueが不正入力を拒否することを検証する
[[nodiscard]] bool test_stable_identity_validation(
    const cue::AssertContext &a_assertContext)
{
    auto valid = cue::schema::TypeId::parse(
        "10000000-0000-4000-8000-000000000001", a_assertContext);
    auto uppercase = cue::schema::TypeId::parse(
        "A0000000-0000-4000-8000-000000000001", a_assertContext);
    auto wrongVersion = cue::schema::TypeId::parse(
        "10000000-0000-3000-8000-000000000001", a_assertContext);
    auto wrongVariant = cue::schema::TypeId::parse(
        "10000000-0000-4000-4000-000000000001", a_assertContext);
    auto zeroField = cue::schema::FieldId::create(0U, a_assertContext);
    auto zeroVersion = cue::schema::SchemaVersion::create(0U, a_assertContext);

    return valid.has_value() &&
           has_schema_error(uppercase, cue::schema::SchemaError::InvalidTypeId) &&
           has_schema_error(wrongVersion, cue::schema::SchemaError::InvalidTypeId) &&
           has_schema_error(wrongVariant, cue::schema::SchemaError::InvalidTypeId) &&
           has_schema_error(zeroField, cue::schema::SchemaError::InvalidFieldId) &&
           has_schema_error(zeroVersion,
                            cue::schema::SchemaError::InvalidSchemaVersion);
}

/// @brief DescriptorがField順序とStable ID不変条件を検証することを確認する
[[nodiscard]] bool test_descriptor_validation(
    const cue::AssertContext &a_assertContext)
{
    auto invalidName = cue::schema::create_field_descriptor(
        make_field_id(1U, a_assertContext), "invalid\nname", a_assertContext);

    std::vector<cue::schema::FieldDescriptor> orderedFields;
    orderedFields.push_back(make_field(2U, "second", a_assertContext));
    orderedFields.push_back(make_field(1U, "first", a_assertContext));
    std::vector<cue::schema::FieldId> reservedIds;
    reservedIds.push_back(make_field_id(3U, a_assertContext));
    auto orderedType = cue::schema::create_type_descriptor(
        make_type_id("10000000-0000-4000-8000-000000000001", a_assertContext),
        "Cue.Test.Ordered", make_version(a_assertContext),
        std::move(orderedFields), std::move(reservedIds), a_assertContext);

    std::vector<cue::schema::FieldDescriptor> duplicateFields;
    duplicateFields.push_back(make_field(1U, "first", a_assertContext));
    duplicateFields.push_back(make_field(1U, "second", a_assertContext));
    std::vector<cue::schema::FieldId> noReservedIds;
    auto duplicateField = cue::schema::create_type_descriptor(
        make_type_id("20000000-0000-4000-8000-000000000002", a_assertContext),
        "Cue.Test.Duplicate", make_version(a_assertContext),
        std::move(duplicateFields), std::move(noReservedIds), a_assertContext);

    std::vector<cue::schema::FieldDescriptor> reusedFields;
    reusedFields.push_back(make_field(4U, "active", a_assertContext));
    std::vector<cue::schema::FieldId> reusedReservedIds;
    reusedReservedIds.push_back(make_field_id(4U, a_assertContext));
    auto reusedField = cue::schema::create_type_descriptor(
        make_type_id("30000000-0000-4000-8000-000000000003", a_assertContext),
        "Cue.Test.Reused", make_version(a_assertContext),
        std::move(reusedFields), std::move(reusedReservedIds), a_assertContext);

    const auto *ordered = orderedType.try_value();
    return has_schema_error(invalidName, cue::schema::SchemaError::InvalidName) &&
           ordered != nullptr && ordered->fields().size() == 2U &&
           ordered->fields()[0].id().value() == 1U &&
           ordered->fields()[1].id().value() == 2U &&
           ordered->reserved_field_ids().size() == 1U &&
           has_schema_error(duplicateField,
                            cue::schema::SchemaError::DuplicateFieldId) &&
           has_schema_error(reusedField, cue::schema::SchemaError::ReservedFieldId);
}

/// @brief 登録順に依存せず同じTypeIdへ同じDense Indexを割り当てることを検証する
[[nodiscard]] bool test_registration_order_independence(
    const cue::AssertContext &a_assertContext)
{
    constexpr std::string_view firstId =
        "10000000-0000-4000-8000-000000000001";
    constexpr std::string_view secondId =
        "20000000-0000-4000-8000-000000000002";
    cue::schema::SchemaRegistryIdentitySource identitySource;

    cue::schema::SchemaRegistryBuilder firstBuilder(identitySource, a_assertContext);
    auto firstAddSecond = firstBuilder.add_type(
        make_type(secondId, "Cue.Test.Second", a_assertContext));
    auto firstAddFirst = firstBuilder.add_type(
        make_type(firstId, "Cue.Test.First", a_assertContext));
    auto firstRegistryResult = firstBuilder.seal();

    cue::schema::SchemaRegistryBuilder secondBuilder(identitySource, a_assertContext);
    auto secondAddFirst = secondBuilder.add_type(
        make_type(firstId, "Cue.Test.First", a_assertContext));
    auto secondAddSecond = secondBuilder.add_type(
        make_type(secondId, "Cue.Test.Second", a_assertContext));
    auto secondRegistryResult = secondBuilder.seal();

    cue::schema::SchemaRegistryIdentitySource otherIdentitySource;
    cue::schema::SchemaRegistryBuilder otherBuilder(otherIdentitySource,
                                                    a_assertContext);
    auto otherAddFirst = otherBuilder.add_type(
        make_type(firstId, "Cue.Test.First", a_assertContext));
    auto otherRegistryResult = otherBuilder.seal();

    const auto *firstRegistry = firstRegistryResult.try_value();
    const auto *secondRegistry = secondRegistryResult.try_value();
    const auto *otherRegistry = otherRegistryResult.try_value();

    if (!firstAddSecond || !firstAddFirst || !secondAddFirst || !secondAddSecond ||
        !otherAddFirst || firstRegistry == nullptr || secondRegistry == nullptr ||
        otherRegistry == nullptr)
    {
        return false;
    }

    const auto firstTypeId = make_type_id(firstId, a_assertContext);
    const auto secondTypeId = make_type_id(secondId, a_assertContext);
    const auto firstIndex = firstRegistry->dense_index(firstTypeId);
    const auto mirroredFirstIndex = secondRegistry->dense_index(firstTypeId);
    const auto secondIndex = firstRegistry->dense_index(secondTypeId);
    const auto mirroredSecondIndex = secondRegistry->dense_index(secondTypeId);

    return firstIndex.has_value() && mirroredFirstIndex.has_value() &&
           secondIndex.has_value() && mirroredSecondIndex.has_value() &&
           firstIndex->value() == mirroredFirstIndex->value() &&
           secondIndex->value() == mirroredSecondIndex->value() &&
           firstIndex->value() == 1U && secondIndex->value() == 2U &&
           firstRegistry->find(*firstIndex)->id() == firstTypeId &&
           secondRegistry->find(*firstIndex) == nullptr &&
           otherRegistry->find(*firstIndex) == nullptr;
}

/// @brief RegistryがType・名前・Tombstoneの衝突を診断付きで拒否することを検証する
[[nodiscard]] bool test_registry_collision_validation(
    const cue::AssertContext &a_assertContext)
{
    constexpr std::string_view firstId =
        "10000000-0000-4000-8000-000000000001";
    constexpr std::string_view secondId =
        "20000000-0000-4000-8000-000000000002";
    cue::schema::SchemaRegistryIdentitySource identitySource;

    cue::schema::SchemaRegistryBuilder duplicateTypeBuilder(identitySource,
                                                            a_assertContext);
    auto firstType = duplicateTypeBuilder.add_type(
        make_type(firstId, "Cue.Test.First", a_assertContext));
    auto duplicateType = duplicateTypeBuilder.add_type(
        make_type(firstId, "Cue.Test.Other", a_assertContext));
    auto addAfterFailure = duplicateTypeBuilder.add_tombstone(
        make_type_id(secondId, a_assertContext), "Cue.Test.ModuleB");
    auto sealAfterFailure = duplicateTypeBuilder.seal();

    cue::schema::SchemaRegistryBuilder duplicateNameBuilder(identitySource,
                                                            a_assertContext);
    auto firstName = duplicateNameBuilder.add_type(
        make_type(firstId, "Cue.Test.Shared", a_assertContext));
    auto duplicateName = duplicateNameBuilder.add_type(
        make_type(secondId, "Cue.Test.Shared", a_assertContext));

    cue::schema::SchemaRegistryBuilder tombstoneBuilder(identitySource,
                                                        a_assertContext);
    const auto tombstoneId = make_type_id(secondId, a_assertContext);
    auto firstTombstone = tombstoneBuilder.add_tombstone(
        tombstoneId, "Cue.Test.ModuleA");
    auto duplicateTombstone = tombstoneBuilder.add_tombstone(
        tombstoneId, "Cue.Test.ModuleB");

    cue::schema::SchemaRegistryBuilder tombstoneConflictBuilder(identitySource,
                                                                a_assertContext);
    auto conflictTombstone = tombstoneConflictBuilder.add_tombstone(
        tombstoneId, "Cue.Test.ModuleA");
    auto tombstonedType = tombstoneConflictBuilder.add_type(
        make_type(secondId, "Cue.Test.Tombstoned", a_assertContext));

    const cue::Error *duplicateTypeError = duplicateType.try_error();
    const bool duplicateTypeDiagnostic = duplicateTypeError != nullptr &&
        duplicateTypeError->summary().find(firstId) != std::string_view::npos &&
        duplicateTypeError->summary().find("Cue.Test.First") != std::string_view::npos &&
        duplicateTypeError->summary().find("Cue.Test.Other") != std::string_view::npos;
    const cue::Error *duplicateTombstoneError = duplicateTombstone.try_error();
    const bool tombstoneDiagnostic = duplicateTombstoneError != nullptr &&
        duplicateTombstoneError->summary().find("Cue.Test.ModuleA") !=
            std::string_view::npos &&
        duplicateTombstoneError->summary().find("Cue.Test.ModuleB") !=
            std::string_view::npos;

    return firstType.has_value() && firstName.has_value() &&
           firstTombstone.has_value() && conflictTombstone.has_value() &&
           duplicateTypeDiagnostic && tombstoneDiagnostic &&
           has_schema_error(duplicateType,
                            cue::schema::SchemaError::DuplicateTypeId) &&
           has_schema_error(addAfterFailure,
                            cue::schema::SchemaError::BuilderFailed) &&
           has_schema_error(sealAfterFailure,
                            cue::schema::SchemaError::BuilderFailed) &&
           has_schema_error(duplicateName,
                            cue::schema::SchemaError::DuplicateTypeName) &&
           has_schema_error(duplicateTombstone,
                            cue::schema::SchemaError::DuplicateTombstone) &&
           has_schema_error(tombstonedType,
                            cue::schema::SchemaError::TombstonedTypeId);
}

/// @brief Seal済みBuilderが全Build構成で追加登録と再Sealを拒否することを検証する
[[nodiscard]] bool test_sealed_builder_rejection(
    const cue::AssertContext &a_assertContext)
{
    constexpr std::string_view firstId =
        "10000000-0000-4000-8000-000000000001";
    constexpr std::string_view secondId =
        "20000000-0000-4000-8000-000000000002";
    cue::schema::SchemaRegistryIdentitySource identitySource;
    cue::schema::SchemaRegistryBuilder builder(identitySource, a_assertContext);
    auto addType = builder.add_type(
        make_type(firstId, "Cue.Test.First", a_assertContext));
    auto registry = builder.seal();
    auto addAfterSeal = builder.add_tombstone(
        make_type_id(secondId, a_assertContext), "Cue.Test.Module");
    auto secondSeal = builder.seal();

    return addType.has_value() && registry.has_value() &&
           has_schema_error(addAfterSeal, cue::schema::SchemaError::BuilderSealed) &&
           has_schema_error(secondSeal, cue::schema::SchemaError::BuilderSealed);
}

/// @brief Seal済みRegistryを複数Reader Threadから変更なしで参照できることを検証する
[[nodiscard]] bool test_immutable_concurrent_read(
    const cue::AssertContext &a_assertContext)
{
    constexpr std::string_view activeIdText =
        "10000000-0000-4000-8000-000000000001";
    constexpr std::string_view tombstoneIdText =
        "30000000-0000-4000-8000-000000000003";
    const auto activeId = make_type_id(activeIdText, a_assertContext);
    const auto tombstoneId = make_type_id(tombstoneIdText, a_assertContext);
    cue::schema::SchemaRegistryIdentitySource identitySource;
    cue::schema::SchemaRegistryBuilder builder(identitySource, a_assertContext);
    auto addType = builder.add_type(
        make_type(activeIdText, "Cue.Test.Active", a_assertContext));
    auto addTombstone = builder.add_tombstone(tombstoneId, "Cue.Test.Module");
    auto registryResult = builder.seal();
    const auto *registry = registryResult.try_value();

    if (!addType || !addTombstone || registry == nullptr)
    {
        return false;
    }

    std::atomic_bool readsSucceeded = true;
    std::vector<std::thread> readers;

    for (std::size_t index = 0U; index < 4U; ++index)
    {
        readers.emplace_back(
            /// @brief Immutable Registryの検索結果が全Readerで一致するか繰り返し検証する
            [registry, activeId, tombstoneId, &readsSucceeded]() noexcept
        {
            for (std::size_t iteration = 0U; iteration < 1000U; ++iteration)
            {
                const auto denseIndex = registry->dense_index(activeId);

                if (registry->find(activeId) == nullptr || !denseIndex.has_value() ||
                    registry->find(*denseIndex) == nullptr ||
                    !registry->is_tombstoned(tombstoneId))
                {
                    readsSucceeded.store(false, std::memory_order_relaxed);
                    return;
                }
            }
        });
    }

    for (auto &reader : readers)
    {
        reader.join();
    }

    return readsSucceeded.load(std::memory_order_relaxed);
}
} // namespace

static_assert(!std::is_copy_constructible_v<cue::schema::SchemaRegistry>);
static_assert(!std::is_copy_constructible_v<cue::schema::TypeDescriptor>);
static_assert(!std::is_move_constructible_v<cue::schema::SchemaRegistryIdentitySource>);
static_assert(std::is_same_v<
              decltype(std::declval<const cue::schema::SchemaRegistry &>().find(
                  std::declval<cue::schema::TypeId>())),
              const cue::schema::TypeDescriptor *>);

/// @brief Cue.SchemaのStable IdentityとImmutable Registry契約を実行時に検証する
int main()
{
    TestFatalHandler fatalHandler;
    std::vector<std::unique_ptr<cue::LogSink>> sinks;
    cue::Logger logger(fatalHandler, std::move(sinks));
    cue::AssertContext assertContext(logger, fatalHandler);

    if (!test_stable_identity_validation(assertContext))
    {
        return 1;
    }

    if (!test_descriptor_validation(assertContext))
    {
        return 2;
    }

    if (!test_registration_order_independence(assertContext))
    {
        return 3;
    }

    if (!test_registry_collision_validation(assertContext))
    {
        return 4;
    }

    if (!test_immutable_concurrent_read(assertContext))
    {
        return 5;
    }

    if (!test_sealed_builder_rejection(assertContext))
    {
        return 6;
    }

    return 0;
}

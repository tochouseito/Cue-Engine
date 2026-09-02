#pragma once

#include <Cue/Schema/Descriptor.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <thread>
#include <vector>

namespace cue::schema
{
/// @brief Stable TypeId順でSealされるImmutable Schema Registry
class SchemaRegistry final
{
  public:
    /// @brief SealされていないRegistryを作らせないため既定構築を禁止する
    SchemaRegistry() = delete;
    /// @brief 所有Descriptorの暗黙Allocationを避けるためCopy構築を禁止する
    SchemaRegistry(const SchemaRegistry &) = delete;
    /// @brief 所有Descriptorの暗黙Allocationを避けるためCopy代入を禁止する
    SchemaRegistry &operator=(const SchemaRegistry &) = delete;
    /// @brief Immutable Registryの所有権を移動する
    SchemaRegistry(SchemaRegistry &&) noexcept = default;
    /// @brief Immutable Registryの所有権を移動代入する
    SchemaRegistry &operator=(SchemaRegistry &&) noexcept = default;
    /// @brief Registryが所有するDescriptorとTombstoneを破棄する
    ~SchemaRegistry() = default;

    /// @brief 登録済みType数を返す
    [[nodiscard]] std::size_t size() const noexcept;
    /// @brief Stable TypeIdに対応するDescriptorへの非所有Pointerを返す
    [[nodiscard]] const TypeDescriptor *find(TypeId a_id) const noexcept;
    /// @brief Runtime Dense Indexに対応するDescriptorへの非所有Pointerを返す
    [[nodiscard]] const TypeDescriptor *find(DenseTypeIndex a_index) const noexcept;
    /// @brief Stable TypeIdに対応するRegistry-local Indexを返す
    [[nodiscard]] std::optional<DenseTypeIndex> dense_index(TypeId a_id) const noexcept;
    /// @brief TypeIdが削除済みTombstoneとして予約されているか返す
    [[nodiscard]] bool is_tombstoned(TypeId a_id) const noexcept;

  private:
    friend class SchemaRegistryBuilder;

    /// @brief Stable ID順へ検証済みのDescriptorとTombstoneを所有する
    SchemaRegistry(std::vector<TypeDescriptor> &&a_descriptors,
                   std::vector<TypeId> &&a_tombstones,
                   std::uint64_t a_generation) noexcept;

    std::vector<TypeDescriptor> m_descriptors;
    std::vector<TypeId> m_tombstones;
    std::uint64_t m_generation;
};

/// @brief Owner Thread上でSchema登録を収集してImmutable Registryを構築する
class SchemaRegistryBuilder final
{
  public:
    /// @brief 診断依存と現在Owner Threadを記録して空Builderを構築する
    /// @param a_assertContext Builderより長く生存する非所有診断Context
    explicit SchemaRegistryBuilder(const AssertContext &a_assertContext) noexcept;
    /// @brief Builderの一意所有を保つためCopy構築を禁止する
    SchemaRegistryBuilder(const SchemaRegistryBuilder &) = delete;
    /// @brief Builderの一意所有を保つためCopy代入を禁止する
    SchemaRegistryBuilder &operator=(const SchemaRegistryBuilder &) = delete;
    /// @brief Owner Thread契約を別Objectへ移さないためMove構築を禁止する
    SchemaRegistryBuilder(SchemaRegistryBuilder &&) = delete;
    /// @brief Owner Thread契約を別Objectへ移さないためMove代入を禁止する
    SchemaRegistryBuilder &operator=(SchemaRegistryBuilder &&) = delete;
    /// @brief 未Sealの登録値を所有権規則に従って破棄する
    ~SchemaRegistryBuilder() = default;

    /// @brief Active Type DescriptorをBuilderへ一意登録する
    [[nodiscard]] Result<void> add_type(TypeDescriptor &&a_descriptor) noexcept;
    /// @brief 削除済みTypeId TombstoneをBuilderへ一意登録する
    [[nodiscard]] Result<void> add_tombstone(TypeId a_id) noexcept;
    /// @brief 全衝突を検証しTypeId順のImmutable Registryへ所有権を移す
    [[nodiscard]] Result<SchemaRegistry> seal() noexcept;

  private:
    const AssertContext *m_assertContext;
    std::thread::id m_ownerThread;
    std::vector<TypeDescriptor> m_descriptors;
    std::vector<TypeId> m_tombstones;
    bool m_isSealed = false;
    bool m_hasFailed = false;
};
} // namespace cue::schema

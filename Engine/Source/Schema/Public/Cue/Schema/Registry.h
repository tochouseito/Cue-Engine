#pragma once

#include <Cue/Schema/Descriptor.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace cue::schema
{
/// @brief DLL境界を含むProcess全体でRegistry Generationを一意発行する明示所有Source
class SchemaRegistryIdentitySource final
{
  public:
    /// @brief 未発行のGeneration 1からProcess-local Identity Sourceを開始する
    SchemaRegistryIdentitySource() noexcept = default;
    /// @brief 発行状態を一意所有するためCopy構築を禁止する
    SchemaRegistryIdentitySource(const SchemaRegistryIdentitySource &) = delete;
    /// @brief 発行状態を一意所有するためCopy代入を禁止する
    SchemaRegistryIdentitySource &operator=(const SchemaRegistryIdentitySource &) = delete;
    /// @brief 発行元Addressを安定させるためMove構築を禁止する
    SchemaRegistryIdentitySource(SchemaRegistryIdentitySource &&) = delete;
    /// @brief 発行元Addressを安定させるためMove代入を禁止する
    SchemaRegistryIdentitySource &operator=(SchemaRegistryIdentitySource &&) = delete;
    /// @brief 全RegistryとDense Indexの破棄後に発行状態を破棄する
    ~SchemaRegistryIdentitySource() = default;

  private:
    friend class SchemaRegistryBuilder;

    /// @brief Wrapせず次のProcess-local Registry Generationを予約する
    [[nodiscard]] std::optional<std::uint64_t> acquire_generation() noexcept;

    std::atomic_uint64_t m_nextGeneration = 1U;
};

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
    /// @brief 発行済み参照の寿命をObjectへ固定するためMove構築を禁止する
    SchemaRegistry(SchemaRegistry &&) noexcept = delete;
    /// @brief 発行済み参照とIndexを保護するためRegistryの置換を禁止する
    SchemaRegistry &operator=(SchemaRegistry &&) noexcept = delete;
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
                   const SchemaRegistryIdentitySource &a_identitySource,
                   std::uint64_t a_generation) noexcept;

    std::vector<TypeDescriptor> m_descriptors;
    std::vector<TypeId> m_tombstones;
    const SchemaRegistryIdentitySource *m_identitySource;
    std::uint64_t m_generation;
};

/// @brief Owner Thread上でSchema登録を収集してImmutable Registryを構築する
class SchemaRegistryBuilder final
{
  public:
    /// @brief Process共有Identity Source、診断依存、現在Owner Threadを記録する
    /// @param a_identitySource 全Moduleで共有し全RegistryとDense Indexより長く生存する発行元
    /// @param a_assertContext Builderより長く生存する非所有診断Context
    SchemaRegistryBuilder(SchemaRegistryIdentitySource &a_identitySource,
                          const AssertContext &a_assertContext) noexcept;
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
    /// @brief 削除済みTypeId Tombstoneを登録元診断名と共にBuilderへ一意登録する
    [[nodiscard]] Result<void> add_tombstone(TypeId a_id,
                                            std::string_view a_sourceName) noexcept;
    /// @brief 全衝突を検証しTypeId順のImmutable Registryへ所有権を移す
    [[nodiscard]] Result<std::unique_ptr<SchemaRegistry>> seal() noexcept;

  private:
    const AssertContext *m_assertContext;
    SchemaRegistryIdentitySource *m_identitySource;
    std::thread::id m_ownerThread;
    std::vector<TypeDescriptor> m_descriptors;
    std::vector<TypeId> m_tombstones;
    std::vector<std::string> m_tombstoneSources;
    bool m_isSealed = false;
    bool m_hasFailed = false;
};
} // namespace cue::schema

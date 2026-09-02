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
/// @brief DLL 境界を含む Process 全体で Registry Generation を一意発行する明示所有 Source
class SchemaRegistryIdentitySource final
{
  public:
    /// @brief 未発行の Generation 1 から Process-local Identity Source を開始する
    SchemaRegistryIdentitySource() noexcept = default;
    /// @brief 発行状態を一意所有するため Copy 構築を禁止する
    SchemaRegistryIdentitySource(const SchemaRegistryIdentitySource &) = delete;
    /// @brief 発行状態を一意所有するため Copy 代入を禁止する
    SchemaRegistryIdentitySource &operator=(const SchemaRegistryIdentitySource &) = delete;
    /// @brief 発行元 Address を安定させるため Move 構築を禁止する
    SchemaRegistryIdentitySource(SchemaRegistryIdentitySource &&) = delete;
    /// @brief 発行元 Address を安定させるため Move 代入を禁止する
    SchemaRegistryIdentitySource &operator=(SchemaRegistryIdentitySource &&) = delete;
    /// @brief 全 Registry と Dense Index の破棄後に発行状態を破棄する
    ~SchemaRegistryIdentitySource() = default;

  private:
    friend class SchemaRegistryBuilder;

    /// @brief Wrap せず次の Process-local Registry Generation を予約する
    [[nodiscard]] std::optional<std::uint64_t> acquire_generation() noexcept;

    std::atomic_uint64_t m_nextGeneration = 1U;
};

/// @brief Stable TypeId 順で Seal される Immutable Schema Registry
class SchemaRegistry final
{
  public:
    /// @brief Builder だけが Registry Constructor へ渡せる生成権限
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
        friend class SchemaRegistryBuilder;

        /// @brief SchemaRegistryBuilder だけに生成権限を発行する
        ConstructionKey() noexcept = default;
    };

    /// @brief Seal されていない Registry を作らせないため既定構築を禁止する
    SchemaRegistry() = delete;
    /// @brief 所有 Descriptor の暗黙 Allocation を避けるため Copy 構築を禁止する
    SchemaRegistry(const SchemaRegistry &) = delete;
    /// @brief 所有 Descriptor の暗黙 Allocation を避けるため Copy 代入を禁止する
    SchemaRegistry &operator=(const SchemaRegistry &) = delete;
    /// @brief 発行済み参照の寿命を Object へ固定するため Move 構築を禁止する
    SchemaRegistry(SchemaRegistry &&) noexcept = delete;
    /// @brief 発行済み参照と Index を保護するため Registry の置換を禁止する
    SchemaRegistry &operator=(SchemaRegistry &&) noexcept = delete;
    /// @brief Registry が所有する Descriptor と Tombstone を破棄する
    ~SchemaRegistry() = default;

    /// @brief Builder が検証済み Descriptor と Tombstone の所有権を束ねる
    SchemaRegistry(ConstructionKey, std::vector<TypeDescriptor> &&a_descriptors,
                   std::vector<TypeId> &&a_tombstones,
                   const SchemaRegistryIdentitySource &a_identitySource,
                   std::uint64_t a_generation) noexcept;

    /// @brief 登録済み Type 数を返す
    [[nodiscard]] std::size_t size() const noexcept;
    /// @brief Stable TypeId に対応する Descriptor への非所有 Pointer または NotFound を返す
    [[nodiscard]] Result<const TypeDescriptor *> find(
        TypeId a_id, const AssertContext &a_assertContext) const noexcept;
    /// @brief Runtime Dense Index に対応する Descriptor への非所有 Pointer を返す
    [[nodiscard]] const TypeDescriptor *find(DenseTypeIndex a_index) const noexcept;
    /// @brief Stable TypeId に対応する Registry-local Index または NotFound を返す
    [[nodiscard]] Result<DenseTypeIndex> dense_index(
        TypeId a_id, const AssertContext &a_assertContext) const noexcept;
    /// @brief TypeId が削除済み Tombstone として予約されているか返す
    [[nodiscard]] bool is_tombstoned(TypeId a_id) const noexcept;

  private:
    friend class SchemaRegistryBuilder;

    std::vector<TypeDescriptor> m_descriptors;
    std::vector<TypeId> m_tombstones;
    const SchemaRegistryIdentitySource *m_identitySource;
    std::uint64_t m_generation;
};

/// @brief Owner Thread 上で Schema 登録を収集して Immutable Registry を構築する
class SchemaRegistryBuilder final
{
  public:
    /// @brief Process 共有 Identity Source、診断依存、現在 Owner Thread を記録する
    /// @param a_identitySource 全 Module で共有し全 Registry と Dense Index より長く生存する発行元
    /// @param a_assertContext Builder より長く生存する非所有診断 Context
    SchemaRegistryBuilder(SchemaRegistryIdentitySource &a_identitySource,
                          const AssertContext &a_assertContext) noexcept;
    /// @brief Builder の一意所有を保つため Copy 構築を禁止する
    SchemaRegistryBuilder(const SchemaRegistryBuilder &) = delete;
    /// @brief Builder の一意所有を保つため Copy 代入を禁止する
    SchemaRegistryBuilder &operator=(const SchemaRegistryBuilder &) = delete;
    /// @brief Owner Thread 契約を別 Object へ移さないため Move 構築を禁止する
    SchemaRegistryBuilder(SchemaRegistryBuilder &&) = delete;
    /// @brief Owner Thread 契約を別 Object へ移さないため Move 代入を禁止する
    SchemaRegistryBuilder &operator=(SchemaRegistryBuilder &&) = delete;
    /// @brief 未 Seal の登録値を所有権規則に従って破棄する
    ~SchemaRegistryBuilder() = default;

    /// @brief Active Type Descriptor を Builder へ一意登録する
    [[nodiscard]] Result<void> add_type(TypeDescriptor &&a_descriptor) noexcept;
    /// @brief 削除済み TypeId Tombstone を登録元診断名と共に Builder へ一意登録する
    [[nodiscard]] Result<void> add_tombstone(TypeId a_id,
                                            std::string_view a_sourceName) noexcept;
    /// @brief 全衝突を検証し TypeId 順の Immutable Registry へ所有権を移す
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

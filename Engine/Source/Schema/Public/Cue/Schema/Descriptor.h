#pragma once

#include <Cue/Schema/Types.h>

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cue::schema
{
/// @brief Type内でStable Field Identityと診断名を所有する最小Descriptor
class FieldDescriptor final
{
  public:
    /// @brief 必須値を欠くField Descriptorを作らせないため既定構築を禁止する
    FieldDescriptor() = delete;
    /// @brief 所有文字列の暗黙Allocationを避けるためCopy構築を禁止する
    FieldDescriptor(const FieldDescriptor &) = delete;
    /// @brief 所有文字列の暗黙Allocationを避けるためCopy代入を禁止する
    FieldDescriptor &operator=(const FieldDescriptor &) = delete;
    /// @brief Field Descriptorの所有権を移動する
    FieldDescriptor(FieldDescriptor &&) noexcept = default;
    /// @brief Field Descriptorの所有権を移動代入する
    FieldDescriptor &operator=(FieldDescriptor &&) noexcept = default;
    /// @brief Field Descriptorの所有値を破棄する
    ~FieldDescriptor() = default;

    /// @brief Stable Field Identityを返す
    [[nodiscard]] FieldId id() const noexcept;
    /// @brief Identityには使用しない診断名を返す
    [[nodiscard]] std::string_view name() const noexcept;

  private:
    friend Result<FieldDescriptor> create_field_descriptor(
        FieldId, std::string_view, const AssertContext &) noexcept;

    /// @brief 検証済みField値の所有権を束ねる
    FieldDescriptor(FieldId a_id, std::string &&a_name) noexcept;

    FieldId m_id;
    std::string m_name;
};

/// @brief Stable Type Identityと現在Schemaの最小Metadataを所有するDescriptor
class TypeDescriptor final
{
  public:
    /// @brief 必須値を欠くType Descriptorを作らせないため既定構築を禁止する
    TypeDescriptor() = delete;
    /// @brief 所有Collectionの暗黙Allocationを避けるためCopy構築を禁止する
    TypeDescriptor(const TypeDescriptor &) = delete;
    /// @brief 所有Collectionの暗黙Allocationを避けるためCopy代入を禁止する
    TypeDescriptor &operator=(const TypeDescriptor &) = delete;
    /// @brief Type Descriptorの所有権を移動する
    TypeDescriptor(TypeDescriptor &&) noexcept = default;
    /// @brief Type Descriptorの所有権を移動代入する
    TypeDescriptor &operator=(TypeDescriptor &&) noexcept = default;
    /// @brief Type Descriptorの所有値を破棄する
    ~TypeDescriptor() = default;

    /// @brief Stable Type Identityを返す
    [[nodiscard]] TypeId id() const noexcept;
    /// @brief Identityには使用しないCanonical診断名を返す
    [[nodiscard]] std::string_view name() const noexcept;
    /// @brief 現在の連続Schema Versionを返す
    [[nodiscard]] SchemaVersion version() const noexcept;
    /// @brief Field Descriptorを登録時の意味に依存しないStable ID順で返す
    [[nodiscard]] std::span<const FieldDescriptor> fields() const noexcept;
    /// @brief Stable FieldIdに対応するDescriptorへの非所有PointerまたはNotFoundを返す
    ///
    /// Seal 前の Pointer は、この TypeDescriptor の次の Move 構築、Move 代入、破棄まで有効とする
    /// Builder へ所有権を移した時点で以前の Pointer は無効となり、Seal 後に Registry が返した
    /// TypeDescriptor から取得した Pointer は所有 Registry Object の Lifetime 中有効とする
    [[nodiscard]] Result<const FieldDescriptor *> find_field(
        FieldId a_id, const AssertContext &a_assertContext) const noexcept;
    /// @brief 再利用禁止Field IDをunsigned値順で返す
    [[nodiscard]] std::span<const FieldId> reserved_field_ids() const noexcept;

  private:
    friend Result<TypeDescriptor> create_type_descriptor(
        TypeId, std::string_view, SchemaVersion, std::vector<FieldDescriptor> &&,
        std::vector<FieldId> &&, const AssertContext &) noexcept;

    /// @brief 検証済みType Metadataの所有権を束ねる
    TypeDescriptor(TypeId a_id, std::string &&a_name, SchemaVersion a_version,
                   std::vector<FieldDescriptor> &&a_fields,
                   std::vector<FieldId> &&a_reservedFieldIds) noexcept;

    TypeId m_id;
    std::string m_name;
    SchemaVersion m_version;
    std::vector<FieldDescriptor> m_fields;
    std::vector<FieldId> m_reservedFieldIds;
};

/// @brief Field IDとUTF-8診断名を検証してDescriptorを構築する
[[nodiscard]] Result<FieldDescriptor> create_field_descriptor(
    FieldId a_id, std::string_view a_name, const AssertContext &a_assertContext) noexcept;

/// @brief Type MetadataとField集合の不変条件を検証してDescriptorを構築する
[[nodiscard]] Result<TypeDescriptor> create_type_descriptor(
    TypeId a_id, std::string_view a_name, SchemaVersion a_version,
    std::vector<FieldDescriptor> &&a_fields, std::vector<FieldId> &&a_reservedFieldIds,
    const AssertContext &a_assertContext) noexcept;
} // namespace cue::schema

#pragma once

#include <Cue/Schema/Types.h>

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cue::schema
{
/// @brief Type 内で Stable Field Identity と診断名を所有する最小 Descriptor
class FieldDescriptor final
{
  public:
    /// @brief 必須値を欠く Field Descriptor を作らせないため既定構築を禁止する
    FieldDescriptor() = delete;
    /// @brief 所有文字列の暗黙 Allocation を避けるため Copy 構築を禁止する
    FieldDescriptor(const FieldDescriptor &) = delete;
    /// @brief 所有文字列の暗黙 Allocation を避けるため Copy 代入を禁止する
    FieldDescriptor &operator=(const FieldDescriptor &) = delete;
    /// @brief Field Descriptor の所有権を移動する
    FieldDescriptor(FieldDescriptor &&a_other) noexcept;
    /// @brief Field Descriptor の所有権を移動代入する
    FieldDescriptor &operator=(FieldDescriptor &&a_other) noexcept;
    /// @brief Field Descriptor の所有値を破棄する
    ~FieldDescriptor() = default;

    /// @brief Stable Field Identity を返す
    [[nodiscard]] FieldId id() const noexcept;
    /// @brief Identity には使用しない診断名を返す
    [[nodiscard]] std::string_view name() const noexcept;

  private:
    friend Result<FieldDescriptor> create_field_descriptor(
        FieldId, std::string_view, const AssertContext &) noexcept;

    /// @brief 検証済み Field 値の所有権を束ねる
    FieldDescriptor(FieldId a_id, std::string &&a_name) noexcept;

    FieldId m_id;
    std::string m_name;
};

/// @brief Stable Type Identity と現在 Schema の最小 Metadata を所有する Descriptor
class TypeDescriptor final
{
  public:
    /// @brief 必須値を欠く Type Descriptor を作らせないため既定構築を禁止する
    TypeDescriptor() = delete;
    /// @brief 所有 Collection の暗黙 Allocation を避けるため Copy 構築を禁止する
    TypeDescriptor(const TypeDescriptor &) = delete;
    /// @brief 所有 Collection の暗黙 Allocation を避けるため Copy 代入を禁止する
    TypeDescriptor &operator=(const TypeDescriptor &) = delete;
    /// @brief Type Descriptor の所有権を移動する
    TypeDescriptor(TypeDescriptor &&a_other) noexcept;
    /// @brief Type Descriptor の所有権を移動代入する
    TypeDescriptor &operator=(TypeDescriptor &&a_other) noexcept;
    /// @brief Type Descriptor の所有値を破棄する
    ~TypeDescriptor() = default;

    /// @brief Stable Type Identity を返す
    [[nodiscard]] TypeId id() const noexcept;
    /// @brief Identity には使用しない Canonical 診断名を返す
    [[nodiscard]] std::string_view name() const noexcept;
    /// @brief 現在の連続 Schema Version を返す
    [[nodiscard]] SchemaVersion version() const noexcept;
    /// @brief Field Descriptor を登録時の意味に依存しない Stable ID 順で返す
    [[nodiscard]] std::span<const FieldDescriptor> fields() const noexcept;
    /// @brief Stable FieldId に対応する Descriptor への非所有 Pointer または NotFound を返す
    ///
    /// Seal 前の Pointer は、この TypeDescriptor の次の Move 構築、Move 代入、破棄まで有効とする
    /// Builder へ所有権を移した時点で以前の Pointer は無効となり、Seal 後に Registry が返した
    /// TypeDescriptor から取得した Pointer は所有 Registry Object の Lifetime 中有効とする
    [[nodiscard]] Result<const FieldDescriptor *> find_field(
        FieldId a_id, const AssertContext &a_assertContext) const noexcept;
    /// @brief 再利用禁止 Field ID を unsigned 値順で返す
    [[nodiscard]] std::span<const FieldId> reserved_field_ids() const noexcept;

  private:
    friend Result<TypeDescriptor> create_type_descriptor(
        TypeId, std::string_view, SchemaVersion, std::vector<FieldDescriptor> &&,
        std::vector<FieldId> &&, const AssertContext &) noexcept;

    /// @brief 検証済み Type Metadata の所有権を束ねる
    TypeDescriptor(TypeId a_id, std::string &&a_name, SchemaVersion a_version,
                   std::vector<FieldDescriptor> &&a_fields,
                   std::vector<FieldId> &&a_reservedFieldIds) noexcept;

    TypeId m_id;
    std::string m_name;
    SchemaVersion m_version;
    std::vector<FieldDescriptor> m_fields;
    std::vector<FieldId> m_reservedFieldIds;
};

/// @brief Field ID と UTF-8 診断名を検証して Descriptor を構築する
[[nodiscard]] Result<FieldDescriptor> create_field_descriptor(
    FieldId a_id, std::string_view a_name, const AssertContext &a_assertContext) noexcept;

/// @brief Type Metadata と Field 集合の不変条件を検証して Descriptor を構築する
[[nodiscard]] Result<TypeDescriptor> create_type_descriptor(
    TypeId a_id, std::string_view a_name, SchemaVersion a_version,
    std::vector<FieldDescriptor> &&a_fields, std::vector<FieldId> &&a_reservedFieldIds,
    const AssertContext &a_assertContext) noexcept;

/// @brief Registry 登録前に Type と Field の診断名不変条件を再検証する
[[nodiscard]] Result<void> validate_type_descriptor(
    const TypeDescriptor &a_descriptor,
    const AssertContext &a_assertContext) noexcept;
} // namespace cue::schema

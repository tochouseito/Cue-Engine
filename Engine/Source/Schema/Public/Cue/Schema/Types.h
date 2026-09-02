#pragma once

#include <Cue/Foundation/Result.h>

#include <array>
#include <compare>
#include <cstdint>
#include <span>
#include <string_view>

namespace cue
{
class AssertContext;
}

namespace cue::schema
{
class SchemaRegistryIdentitySource;

/// @brief Compiler 型名や登録順から独立した UUID Version 4 の Schema Type Identity
class TypeId final
{
  public:
    /// @brief 無効な Stable Identity を作らせないため既定構築を禁止する
    TypeId() = delete;
    /// @brief 固定長 Identity を複製する
    TypeId(const TypeId &) noexcept = default;
    /// @brief 固定長 Identity を複製代入する
    TypeId &operator=(const TypeId &) noexcept = default;
    /// @brief 固定長 Identity を移動する
    TypeId(TypeId &&) noexcept = default;
    /// @brief 固定長 Identity を移動代入する
    TypeId &operator=(TypeId &&) noexcept = default;
    /// @brief 固定長 Identity の値を破棄する
    ~TypeId() = default;

    /// @brief lowercase の canonical UUID Version 4 を検証して Identity を返す
    [[nodiscard]] static Result<TypeId> parse(std::string_view a_text,
                                              const AssertContext &a_assertContext) noexcept;

    /// @brief 永続 Binary 表現と同じ Network Byte Order の 16 byte を返す
    [[nodiscard]] std::span<const std::uint8_t, 16> bytes() const noexcept;

    /// @brief 128-bit 値が一致するか比較する
    [[nodiscard]] bool operator==(const TypeId &) const noexcept = default;
    /// @brief Network Byte Order の unsigned 辞書順で比較する
    [[nodiscard]] auto operator<=>(const TypeId &) const noexcept = default;

  private:
    /// @brief 検証済みの 16 byte だけから Stable Identity を構築する
    explicit TypeId(std::array<std::uint8_t, 16> a_bytes) noexcept;

    std::array<std::uint8_t, 16> m_bytes;
};

/// @brief 一つの TypeId 内で Field の意味を識別する non-zero 32-bit 値
class FieldId final
{
  public:
    /// @brief 0 の Invalid ID を作らせないため既定構築を禁止する
    FieldId() = delete;
    /// @brief Field Identity を複製する
    FieldId(const FieldId &) noexcept = default;
    /// @brief Field Identity を複製代入する
    FieldId &operator=(const FieldId &) noexcept = default;
    /// @brief Field Identity を移動する
    FieldId(FieldId &&) noexcept = default;
    /// @brief Field Identity を移動代入する
    FieldId &operator=(FieldId &&) noexcept = default;
    /// @brief Field Identity の値を破棄する
    ~FieldId() = default;

    /// @brief non-zero 値を検証して Field Identity を返す
    [[nodiscard]] static Result<FieldId> create(std::uint32_t a_value,
                                                const AssertContext &a_assertContext) noexcept;

    /// @brief 検証済みの 32-bit 値を返す
    [[nodiscard]] std::uint32_t value() const noexcept;

    /// @brief Field Identity の値が一致するか比較する
    [[nodiscard]] bool operator==(const FieldId &) const noexcept = default;
    /// @brief Field Identity を unsigned 値として比較する
    [[nodiscard]] auto operator<=>(const FieldId &) const noexcept = default;

  private:
    /// @brief 検証済みの non-zero 値から Field Identity を構築する
    explicit FieldId(std::uint32_t a_value) noexcept;

    std::uint32_t m_value;
};

/// @brief 一つの TypeId に属する連続した non-zero Schema 世代
class SchemaVersion final
{
  public:
    /// @brief 0 の Invalid Version を作らせないため既定構築を禁止する
    SchemaVersion() = delete;
    /// @brief Schema 世代を複製する
    SchemaVersion(const SchemaVersion &) noexcept = default;
    /// @brief Schema 世代を複製代入する
    SchemaVersion &operator=(const SchemaVersion &) noexcept = default;
    /// @brief Schema 世代を移動する
    SchemaVersion(SchemaVersion &&) noexcept = default;
    /// @brief Schema 世代を移動代入する
    SchemaVersion &operator=(SchemaVersion &&) noexcept = default;
    /// @brief Schema 世代の値を破棄する
    ~SchemaVersion() = default;

    /// @brief non-zero 値を検証して Schema Version を返す
    [[nodiscard]] static Result<SchemaVersion> create(
        std::uint32_t a_value, const AssertContext &a_assertContext) noexcept;

    /// @brief 検証済みの Schema Version 値を返す
    [[nodiscard]] std::uint32_t value() const noexcept;

    /// @brief Schema Version 値が一致するか比較する
    [[nodiscard]] bool operator==(const SchemaVersion &) const noexcept = default;
    /// @brief Schema Version を unsigned 値として比較する
    [[nodiscard]] auto operator<=>(const SchemaVersion &) const noexcept = default;

  private:
    /// @brief 検証済みの non-zero 値から Schema Version を構築する
    explicit SchemaVersion(std::uint32_t a_value) noexcept;

    std::uint32_t m_value;
};

/// @brief 一つの Seal 済み Registry 内だけで有効な non-zero Runtime Type Index
class DenseTypeIndex final
{
  public:
    /// @brief Registry 外で Index を生成させないため既定構築を禁止する
    DenseTypeIndex() = delete;
    /// @brief Runtime Index を複製する
    DenseTypeIndex(const DenseTypeIndex &) noexcept = default;
    /// @brief Runtime Index を複製代入する
    DenseTypeIndex &operator=(const DenseTypeIndex &) noexcept = default;
    /// @brief Runtime Index を移動する
    DenseTypeIndex(DenseTypeIndex &&) noexcept = default;
    /// @brief Runtime Index を移動代入する
    DenseTypeIndex &operator=(DenseTypeIndex &&) noexcept = default;
    /// @brief Runtime Index の値を破棄する
    ~DenseTypeIndex() = default;

    /// @brief Registry 内の 1 始まり Index を返す
    [[nodiscard]] std::uint32_t value() const noexcept;

    /// @brief Runtime Index 値が一致するか比較する
    [[nodiscard]] bool operator==(const DenseTypeIndex &) const noexcept = default;
    /// @brief Runtime Index を unsigned 値として比較する
    [[nodiscard]] auto operator<=>(const DenseTypeIndex &) const noexcept = default;

  private:
    friend class SchemaRegistry;

    /// @brief Seal 済み Registry が割り当てた世代付き non-zero 値から Index を構築する
    DenseTypeIndex(std::uint32_t a_value,
                   const SchemaRegistryIdentitySource &a_identitySource,
                   std::uint64_t a_registryGeneration) noexcept;

    std::uint32_t m_value;
    const SchemaRegistryIdentitySource *m_identitySource;
    std::uint64_t m_registryGeneration;
};
} // namespace cue::schema

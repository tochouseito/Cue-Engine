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
/// @brief Compiler型名や登録順から独立したUUID Version 4のSchema Type Identity
class TypeId final
{
  public:
    /// @brief 無効なStable Identityを作らせないため既定構築を禁止する
    TypeId() = delete;
    /// @brief 固定長Identityを複製する
    TypeId(const TypeId &) noexcept = default;
    /// @brief 固定長Identityを複製代入する
    TypeId &operator=(const TypeId &) noexcept = default;
    /// @brief 固定長Identityを移動する
    TypeId(TypeId &&) noexcept = default;
    /// @brief 固定長Identityを移動代入する
    TypeId &operator=(TypeId &&) noexcept = default;
    /// @brief 固定長Identityの値を破棄する
    ~TypeId() = default;

    /// @brief lowercaseのcanonical UUID Version 4を検証してIdentityを返す
    [[nodiscard]] static Result<TypeId> parse(std::string_view a_text,
                                              const AssertContext &a_assertContext) noexcept;

    /// @brief 永続Binary表現と同じNetwork Byte Orderの16 byteを返す
    [[nodiscard]] std::span<const std::uint8_t, 16> bytes() const noexcept;

    /// @brief 128-bit値が一致するか比較する
    [[nodiscard]] bool operator==(const TypeId &) const noexcept = default;
    /// @brief Network Byte Orderのunsigned辞書順で比較する
    [[nodiscard]] auto operator<=>(const TypeId &) const noexcept = default;

  private:
    /// @brief 検証済みの16 byteだけからStable Identityを構築する
    explicit TypeId(std::array<std::uint8_t, 16> a_bytes) noexcept;

    std::array<std::uint8_t, 16> m_bytes;
};

/// @brief 一つのTypeId内でFieldの意味を識別するnon-zero 32-bit値
class FieldId final
{
  public:
    /// @brief 0のInvalid IDを作らせないため既定構築を禁止する
    FieldId() = delete;
    /// @brief Field Identityを複製する
    FieldId(const FieldId &) noexcept = default;
    /// @brief Field Identityを複製代入する
    FieldId &operator=(const FieldId &) noexcept = default;
    /// @brief Field Identityを移動する
    FieldId(FieldId &&) noexcept = default;
    /// @brief Field Identityを移動代入する
    FieldId &operator=(FieldId &&) noexcept = default;
    /// @brief Field Identityの値を破棄する
    ~FieldId() = default;

    /// @brief non-zero値を検証してField Identityを返す
    [[nodiscard]] static Result<FieldId> create(std::uint32_t a_value,
                                                const AssertContext &a_assertContext) noexcept;

    /// @brief 検証済みの32-bit値を返す
    [[nodiscard]] std::uint32_t value() const noexcept;

    /// @brief Field Identityの値が一致するか比較する
    [[nodiscard]] bool operator==(const FieldId &) const noexcept = default;
    /// @brief Field Identityをunsigned値として比較する
    [[nodiscard]] auto operator<=>(const FieldId &) const noexcept = default;

  private:
    /// @brief 検証済みのnon-zero値からField Identityを構築する
    explicit FieldId(std::uint32_t a_value) noexcept;

    std::uint32_t m_value;
};

/// @brief 一つのTypeIdに属する連続したnon-zero Schema世代
class SchemaVersion final
{
  public:
    /// @brief 0のInvalid Versionを作らせないため既定構築を禁止する
    SchemaVersion() = delete;
    /// @brief Schema世代を複製する
    SchemaVersion(const SchemaVersion &) noexcept = default;
    /// @brief Schema世代を複製代入する
    SchemaVersion &operator=(const SchemaVersion &) noexcept = default;
    /// @brief Schema世代を移動する
    SchemaVersion(SchemaVersion &&) noexcept = default;
    /// @brief Schema世代を移動代入する
    SchemaVersion &operator=(SchemaVersion &&) noexcept = default;
    /// @brief Schema世代の値を破棄する
    ~SchemaVersion() = default;

    /// @brief non-zero値を検証してSchema Versionを返す
    [[nodiscard]] static Result<SchemaVersion> create(
        std::uint32_t a_value, const AssertContext &a_assertContext) noexcept;

    /// @brief 検証済みのSchema Version値を返す
    [[nodiscard]] std::uint32_t value() const noexcept;

    /// @brief Schema Version値が一致するか比較する
    [[nodiscard]] bool operator==(const SchemaVersion &) const noexcept = default;
    /// @brief Schema Versionをunsigned値として比較する
    [[nodiscard]] auto operator<=>(const SchemaVersion &) const noexcept = default;

  private:
    /// @brief 検証済みのnon-zero値からSchema Versionを構築する
    explicit SchemaVersion(std::uint32_t a_value) noexcept;

    std::uint32_t m_value;
};

/// @brief 一つのSeal済みRegistry内だけで有効なnon-zero Runtime Type Index
class DenseTypeIndex final
{
  public:
    /// @brief Registry外でIndexを生成させないため既定構築を禁止する
    DenseTypeIndex() = delete;
    /// @brief Runtime Indexを複製する
    DenseTypeIndex(const DenseTypeIndex &) noexcept = default;
    /// @brief Runtime Indexを複製代入する
    DenseTypeIndex &operator=(const DenseTypeIndex &) noexcept = default;
    /// @brief Runtime Indexを移動する
    DenseTypeIndex(DenseTypeIndex &&) noexcept = default;
    /// @brief Runtime Indexを移動代入する
    DenseTypeIndex &operator=(DenseTypeIndex &&) noexcept = default;
    /// @brief Runtime Indexの値を破棄する
    ~DenseTypeIndex() = default;

    /// @brief Registry内の1始まりIndexを返す
    [[nodiscard]] std::uint32_t value() const noexcept;

    /// @brief Runtime Index値が一致するか比較する
    [[nodiscard]] bool operator==(const DenseTypeIndex &) const noexcept = default;
    /// @brief Runtime Indexをunsigned値として比較する
    [[nodiscard]] auto operator<=>(const DenseTypeIndex &) const noexcept = default;

  private:
    friend class SchemaRegistry;

    /// @brief Seal済みRegistryが割り当てたnon-zero値からIndexを構築する
    explicit DenseTypeIndex(std::uint32_t a_value) noexcept;

    std::uint32_t m_value;
};
} // namespace cue::schema

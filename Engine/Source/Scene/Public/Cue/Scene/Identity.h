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

namespace cue::scene
{
using IdentityBytes = std::array<std::uint8_t, 16>;
using IdentityText = std::array<char, 36>;

/// @brief Global可変状態を使わずScene Stable ID候補を供給する注入境界
class SceneIdentitySource
{
  public:
    /// @brief 派生Identity Sourceを基底Pointerから正しく破棄する
    virtual ~SceneIdentitySource() = default;

    /// @brief RFC 4122 UUID Version 4候補の16 byteを返す
    [[nodiscard]] virtual IdentityBytes next_identity() noexcept = 0;
};

/// @brief Project内のAuthoring Sceneを識別する永続UUID Version 4
class SceneAssetId final
{
  public:
    /// @brief 無効なScene Identityを作らせないため既定構築を禁止する
    SceneAssetId() = delete;
    /// @brief Scene Identityを複製する
    SceneAssetId(const SceneAssetId &) noexcept = default;
    /// @brief Scene Identityを複製代入する
    SceneAssetId &operator=(const SceneAssetId &) noexcept = default;
    /// @brief Scene Identityを移動する
    SceneAssetId(SceneAssetId &&) noexcept = default;
    /// @brief Scene Identityを移動代入する
    SceneAssetId &operator=(SceneAssetId &&) noexcept = default;
    /// @brief Scene Identity値を破棄する
    ~SceneAssetId() noexcept;

    /// @brief 注入Sourceが返した有効なUUID Version 4からScene Identityを生成する
    [[nodiscard]] static Result<SceneAssetId> generate(
        SceneIdentitySource &a_source,
        const AssertContext &a_assertContext) noexcept;
    /// @brief lowercase canonical UUID Version 4を検証してScene Identityを返す
    [[nodiscard]] static Result<SceneAssetId> parse(
        std::string_view a_text,
        const AssertContext &a_assertContext) noexcept;

    /// @brief Network Byte Orderの永続16 byte表現を返す
    [[nodiscard]] std::span<const std::uint8_t, 16> bytes() const noexcept;
    /// @brief lowercase 8-4-4-4-12の固定長文字列表現を返す
    [[nodiscard]] IdentityText canonical_text() const noexcept;

    /// @brief Scene Identity値が一致するか比較する
    [[nodiscard]] bool operator==(const SceneAssetId &) const noexcept = default;
    /// @brief Network Byte Orderのunsigned辞書順で比較する
    [[nodiscard]] auto operator<=>(const SceneAssetId &) const noexcept = default;

  private:
    /// @brief 検証済み16 byteからScene Identityを構築する
    explicit SceneAssetId(IdentityBytes a_bytes) noexcept;

    IdentityBytes m_bytes;
};

/// @brief 一つのSceneDocument内でObjectを識別する永続UUID Version 4
class ObjectId final
{
  public:
    /// @brief 無効なObject Identityを作らせないため既定構築を禁止する
    ObjectId() = delete;
    /// @brief Object Identityを複製する
    ObjectId(const ObjectId &) noexcept = default;
    /// @brief Object Identityを複製代入する
    ObjectId &operator=(const ObjectId &) noexcept = default;
    /// @brief Object Identityを移動する
    ObjectId(ObjectId &&) noexcept = default;
    /// @brief Object Identityを移動代入する
    ObjectId &operator=(ObjectId &&) noexcept = default;
    /// @brief Object Identity値を破棄する
    ~ObjectId() noexcept;

    /// @brief 注入Sourceが返した有効なUUID Version 4からObject Identityを生成する
    [[nodiscard]] static Result<ObjectId> generate(
        SceneIdentitySource &a_source,
        const AssertContext &a_assertContext) noexcept;
    /// @brief lowercase canonical UUID Version 4を検証してObject Identityを返す
    [[nodiscard]] static Result<ObjectId> parse(
        std::string_view a_text,
        const AssertContext &a_assertContext) noexcept;

    /// @brief Network Byte Orderの永続16 byte表現を返す
    [[nodiscard]] std::span<const std::uint8_t, 16> bytes() const noexcept;
    /// @brief lowercase 8-4-4-4-12の固定長文字列表現を返す
    [[nodiscard]] IdentityText canonical_text() const noexcept;

    /// @brief Object Identity値が一致するか比較する
    [[nodiscard]] bool operator==(const ObjectId &) const noexcept = default;
    /// @brief Network Byte Orderのunsigned辞書順で比較する
    [[nodiscard]] auto operator<=>(const ObjectId &) const noexcept = default;

  private:
    /// @brief 検証済み16 byteからObject Identityを構築する
    explicit ObjectId(IdentityBytes a_bytes) noexcept;

    IdentityBytes m_bytes;
};

/// @brief 一つのSceneDocument内でComponent Instanceを識別する永続UUID Version 4
class ComponentInstanceId final
{
  public:
    /// @brief 無効なComponent Identityを作らせないため既定構築を禁止する
    ComponentInstanceId() = delete;
    /// @brief Component Identityを複製する
    ComponentInstanceId(const ComponentInstanceId &) noexcept = default;
    /// @brief Component Identityを複製代入する
    ComponentInstanceId &operator=(const ComponentInstanceId &) noexcept = default;
    /// @brief Component Identityを移動する
    ComponentInstanceId(ComponentInstanceId &&) noexcept = default;
    /// @brief Component Identityを移動代入する
    ComponentInstanceId &operator=(ComponentInstanceId &&) noexcept = default;
    /// @brief Component Identity値を破棄する
    ~ComponentInstanceId() noexcept;

    /// @brief 注入Sourceが返した有効なUUID Version 4からComponent Identityを生成する
    [[nodiscard]] static Result<ComponentInstanceId> generate(
        SceneIdentitySource &a_source,
        const AssertContext &a_assertContext) noexcept;
    /// @brief lowercase canonical UUID Version 4を検証してComponent Identityを返す
    [[nodiscard]] static Result<ComponentInstanceId> parse(
        std::string_view a_text,
        const AssertContext &a_assertContext) noexcept;

    /// @brief Network Byte Orderの永続16 byte表現を返す
    [[nodiscard]] std::span<const std::uint8_t, 16> bytes() const noexcept;
    /// @brief lowercase 8-4-4-4-12の固定長文字列表現を返す
    [[nodiscard]] IdentityText canonical_text() const noexcept;

    /// @brief Component Identity値が一致するか比較する
    [[nodiscard]] bool operator==(const ComponentInstanceId &) const noexcept = default;
    /// @brief Network Byte Orderのunsigned辞書順で比較する
    [[nodiscard]] auto operator<=>(const ComponentInstanceId &) const noexcept = default;

  private:
    /// @brief 検証済み16 byteからComponent Identityを構築する
    explicit ComponentInstanceId(IdentityBytes a_bytes) noexcept;

    IdentityBytes m_bytes;
};
} // namespace cue::scene

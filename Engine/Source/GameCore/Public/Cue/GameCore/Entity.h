#pragma once

#include <cstdint>

namespace cue::game_core
{
class World;

/// @brief 一つの Runtime World 内で Entity Slot を識別する世代付き Handle
///
/// Runtime 専用の参照であり Scene、Project、Asset、Save Data へ永続化しない
class EntityHandle final
{
  public:
    /// @brief 無効な Entity Handle を作らせないため既定構築を禁止する
    EntityHandle() = delete;
    /// @brief Entity Handle の Runtime Identity を複製する
    EntityHandle(const EntityHandle &) noexcept = default;
    /// @brief Entity Handle の Runtime Identity を複製代入する
    EntityHandle &operator=(const EntityHandle &) noexcept = default;
    /// @brief Entity Handle の Runtime Identity を移動する
    EntityHandle(EntityHandle &&) noexcept = default;
    /// @brief Entity Handle の Runtime Identity を移動代入する
    EntityHandle &operator=(EntityHandle &&) noexcept = default;
    /// @brief Entity Handle の値を破棄する
    ~EntityHandle() noexcept;

    /// @brief World Incarnation を識別する non-zero 値を返す
    [[nodiscard]] std::uint64_t world_id() const noexcept;
    /// @brief World Slot Table 内の Index を返す
    [[nodiscard]] std::uint32_t index() const noexcept;
    /// @brief Slot 再利用を識別する non-zero 世代を返す
    [[nodiscard]] std::uint32_t generation() const noexcept;

    /// @brief World、Slot、Generation がすべて一致するか比較する
    [[nodiscard]] bool operator==(const EntityHandle &) const noexcept = default;

  private:
    friend class World;

    /// @brief World が検証用 Token を含む Entity Handle を発行する
    EntityHandle(std::uint64_t a_worldId, std::uint32_t a_index,
                 std::uint32_t a_generation, std::uint64_t a_validationToken,
                 const void *a_identitySource) noexcept;

    std::uint64_t m_worldId;
    std::uint32_t m_index;
    std::uint32_t m_generation;
    std::uint64_t m_validationToken;
    const void *m_identitySource;
};
} // namespace cue::game_core

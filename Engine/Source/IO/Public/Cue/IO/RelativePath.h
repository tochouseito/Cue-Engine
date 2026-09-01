#pragma once

#include <Cue/Foundation/Result.h>

#include <string>
#include <string_view>

namespace cue
{
class AssertContext;

/// @brief Root-bound Filesystem だけで使用できる検証済み Portable Relative Path
///
/// ASCII の安全な Segment 規則を満たす所有値であり、未検証 String から暗黙変換しない
class RelativePath final
{
  public:
    /// @brief 検証前の空 Path を作らせないため既定構築を禁止する
    RelativePath() = delete;
    /// @brief 検証済み Path を複製する
    RelativePath(const RelativePath &) = default;
    /// @brief 検証済み Path を複製代入する
    RelativePath &operator=(const RelativePath &) = default;
    /// @brief 検証済み Path の所有権を移動する
    RelativePath(RelativePath &&) noexcept = default;
    /// @brief 検証済み Path を移動代入する
    RelativePath &operator=(RelativePath &&) noexcept = default;
    /// @brief 所有する Path Storage を解放する
    ~RelativePath() = default;

    /// @brief Portable 規則を全て検証し、Root を含まない Path 値を返す
    [[nodiscard]] static Result<RelativePath> parse(std::string_view a_text,
                                                    const AssertContext &a_assertContext) noexcept;

    /// @brief `/` 区切りの検証済み Path 表現を返す
    [[nodiscard]] std::string_view text() const noexcept;

    /// @brief 大小文字を区別しない Portable 比較に使用する ASCII lowercase Key を返す
    [[nodiscard]] std::string comparison_key(const AssertContext &a_assertContext) const noexcept;

  private:
    /// @brief 検証を完了した所有 String だけから Path を構築する
    explicit RelativePath(std::string &&a_text) noexcept;

    std::string m_text;
};
} // namespace cue

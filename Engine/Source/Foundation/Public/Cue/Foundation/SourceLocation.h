#pragma once

#include <cstdint>
#include <source_location>
#include <string_view>

namespace cue
{
/**
 * @brief Platform非依存なSource位置
 */
class SourceLocation final
{
  public:
    /**
     * @brief 呼び出し元のSource位置を取得する
     */
    [[nodiscard]] static constexpr SourceLocation current(
        std::source_location a_location = std::source_location::current()) noexcept
    {
        return SourceLocation(a_location);
    }

    /** @brief 標準Source位置から変換する */
    [[nodiscard]] static constexpr SourceLocation from(std::source_location a_location) noexcept
    {
        return SourceLocation(a_location);
    }

    /** @brief File名を返す */
    [[nodiscard]] constexpr std::string_view file_name() const noexcept
    {
        return m_location.file_name();
    }

    /** @brief Function名を返す */
    [[nodiscard]] constexpr std::string_view function_name() const noexcept
    {
        return m_location.function_name();
    }

    /** @brief 1始まりの行番号を返す */
    [[nodiscard]] constexpr std::uint_least32_t line() const noexcept
    {
        return m_location.line();
    }

    /** @brief 1始まりの列番号を返す */
    [[nodiscard]] constexpr std::uint_least32_t column() const noexcept
    {
        return m_location.column();
    }

  private:
    explicit constexpr SourceLocation(std::source_location a_location) noexcept : m_location(a_location)
    {
    }

    std::source_location m_location;
};
} // namespace cue

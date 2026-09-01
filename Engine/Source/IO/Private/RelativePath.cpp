#include <Cue/IO/RelativePath.h>

#include <Cue/Foundation/Assert.h>
#include <Cue/IO/Error.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <utility>

namespace
{
constexpr std::size_t k_maxPathLength = 255;
constexpr std::size_t k_maxSegmentLength = 64;
constexpr std::size_t k_maxSegmentCount = 16;

/// @brief Allocation 失敗を追加 Allocation なしで Fatal 境界へ渡す
[[noreturn]] void terminate_allocation(const cue::AssertContext &a_context) noexcept
{
    a_context.fatal_handler().terminate("Relative path allocation failed");
    std::abort();
}

/// @brief Portable Path が許可する ASCII 文字だけか判定する
[[nodiscard]] bool is_allowed_character(char a_character) noexcept
{
    const unsigned char value = static_cast<unsigned char>(a_character);
    return std::isalnum(value) != 0 || a_character == '_' || a_character == '-' || a_character == '.';
}

/// @brief ASCII 文字列を大小文字を区別せず比較する
[[nodiscard]] bool equals_ascii_case_insensitive(std::string_view a_left, std::string_view a_right) noexcept
{
    if (a_left.size() != a_right.size())
    {
        return false;
    }

    for (std::size_t index = 0; index < a_left.size(); ++index)
    {
        if (std::tolower(static_cast<unsigned char>(a_left[index])) !=
            std::tolower(static_cast<unsigned char>(a_right[index])))
        {
            return false;
        }
    }
    return true;
}

/// @brief Windows が拡張子付きでも Device と解釈する予約名か判定する
[[nodiscard]] bool is_reserved_device(std::string_view a_segment) noexcept
{
    const std::size_t dot = a_segment.find('.');
    const std::string_view stem = a_segment.substr(0, dot);

    if (equals_ascii_case_insensitive(stem, "con") || equals_ascii_case_insensitive(stem, "prn") ||
        equals_ascii_case_insensitive(stem, "aux") || equals_ascii_case_insensitive(stem, "nul"))
    {
        return true;
    }

    if (stem.size() == 4 && stem[3] >= '1' && stem[3] <= '9')
    {
        return equals_ascii_case_insensitive(stem.substr(0, 3), "com") ||
               equals_ascii_case_insensitive(stem.substr(0, 3), "lpt");
    }

    return false;
}

/// @brief 一つの Segment が Portable Rule を満たすか判定する
[[nodiscard]] bool is_valid_segment(std::string_view a_segment) noexcept
{
    if (a_segment.empty() || a_segment.size() > k_maxSegmentLength || a_segment.front() == '.' ||
        a_segment.back() == '.')
    {
        return false;
    }

    if (!std::ranges::all_of(a_segment, is_allowed_character))
    {
        return false;
    }

    return !is_reserved_device(a_segment);
}
} // namespace

namespace cue
{
RelativePath::RelativePath(std::string &&a_text) noexcept : m_text(std::move(a_text))
{
}

Result<RelativePath> RelativePath::parse(std::string_view a_text, const AssertContext &a_assertContext) noexcept
{
    if (a_text.empty() || a_text.size() > k_maxPathLength || a_text.front() == '/' || a_text.back() == '/')
    {
        return Result<RelativePath>::failure(
            make_io_error(a_assertContext, IoError::InvalidPath, "Relative path length or root form is invalid"));
    }

    std::size_t segmentCount = 0;
    std::size_t begin = 0;

    while (begin < a_text.size())
    {
        const std::size_t end = a_text.find('/', begin);
        const std::size_t count = end == std::string_view::npos ? a_text.size() - begin : end - begin;
        const std::string_view segment = a_text.substr(begin, count);
        ++segmentCount;

        if (segmentCount > k_maxSegmentCount || !is_valid_segment(segment))
        {
            return Result<RelativePath>::failure(
                make_io_error(a_assertContext, IoError::InvalidPath, "Relative path segment is invalid"));
        }

        if (end == std::string_view::npos)
        {
            break;
        }

        begin = end + 1;
    }

    try
    {
        return Result<RelativePath>::success(RelativePath(std::string(a_text)));
    }
    catch (...)
    {
        terminate_allocation(a_assertContext);
    }
}

std::string_view RelativePath::text() const noexcept
{
    return m_text;
}

std::string RelativePath::comparison_key(const AssertContext &a_assertContext) const noexcept
{
    std::string key;

    try
    {
        key.reserve(m_text.size());
        for (const char character : m_text)
        {
            key.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
        }
    }
    catch (...)
    {
        terminate_allocation(a_assertContext);
    }

    return key;
}
} // namespace cue

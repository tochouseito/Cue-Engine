#include <Cue/IO/Filesystem.h>

#include <utility>

namespace cue
{
StagingArea::StagingArea(RelativePath &&a_path, std::uint64_t a_token) noexcept
    : m_path(std::move(a_path)), m_token(a_token)
{
}

StagingArea::StagingArea(StagingArea &&a_other) noexcept : m_path(std::move(a_other.m_path)), m_token(a_other.m_token)
{
    a_other.m_token = 0;
}

StagingArea &StagingArea::operator=(StagingArea &&a_other) noexcept
{
    if (this != &a_other)
    {
        std::swap(m_path, a_other.m_path);
        std::swap(m_token, a_other.m_token);
    }

    return *this;
}

const RelativePath &StagingArea::path() const noexcept
{
    return m_path;
}

StagingArea FilesystemRoot::make_staging_area(RelativePath &&a_path, std::uint64_t a_token) noexcept
{
    return StagingArea(std::move(a_path), a_token);
}

std::uint64_t FilesystemRoot::staging_token(const StagingArea &a_staging) noexcept
{
    return a_staging.m_token;
}

void FilesystemRoot::invalidate_staging(StagingArea &a_staging) noexcept
{
    a_staging.m_token = 0;
}
} // namespace cue

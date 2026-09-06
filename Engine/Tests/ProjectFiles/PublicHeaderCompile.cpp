#if defined(_WIN32)
#include <Windows.h>
#endif

#include <Cue/ProjectFiles/Error.h>
#include <Cue/ProjectFiles/Service.h>

#include <type_traits>

static_assert(!std::is_copy_constructible_v<cue::project_files::ProjectFileService>);
static_assert(std::is_nothrow_move_constructible_v<cue::project_files::ProjectFileService>);
static_assert(!std::is_copy_constructible_v<cue::project_files::ProjectFileOperationResult>);

int main()
{
    return 0;
}

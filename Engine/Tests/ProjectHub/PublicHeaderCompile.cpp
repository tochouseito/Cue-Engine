#include <Cue/ProjectHub/Error.h>
#include <Cue/ProjectHub/Service.h>

#include <memory>
#include <type_traits>

static_assert(!std::is_copy_constructible_v<cue::project_hub::ProjectHubService>);
static_assert(!std::is_move_constructible_v<cue::project_hub::ProjectHubService>);
static_assert(!std::is_copy_constructible_v<cue::project_hub::EditorLaunchRequest>);
static_assert(std::is_nothrow_move_constructible_v<cue::project_hub::EditorLaunchRequest>);
static_assert(std::is_nothrow_move_constructible_v<std::unique_ptr<cue::project_hub::ProjectHubService>>);

int main()
{
    return 0;
}

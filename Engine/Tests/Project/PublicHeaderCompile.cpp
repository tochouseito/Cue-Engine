#include <Cue/Project/Descriptor.h>
#include <Cue/Project/Error.h>

#include <type_traits>

static_assert(!std::is_copy_constructible_v<cue::ProjectId>);
static_assert(!std::is_copy_constructible_v<cue::ProjectRoots>);
static_assert(!std::is_copy_constructible_v<cue::ProjectDescriptor>);
static_assert(std::is_nothrow_move_constructible_v<cue::ProjectId>);
static_assert(std::is_nothrow_move_constructible_v<cue::ProjectRoots>);
static_assert(std::is_nothrow_move_constructible_v<cue::ProjectDescriptor>);

/// @brief Project Public Header が単独 Target で Compile 可能か検証する
int main()
{
    return 0;
}

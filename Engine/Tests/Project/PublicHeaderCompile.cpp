#include <Cue/Project/Compatibility.h>
#include <Cue/Project/Descriptor.h>
#include <Cue/Project/Error.h>
#include <Cue/Project/Generator.h>
#include <Cue/Project/Registry.h>

#include <type_traits>
#include <utility>

static_assert(!std::is_copy_constructible_v<cue::ProjectId>);
static_assert(!std::is_copy_constructible_v<cue::ProjectRoots>);
static_assert(!std::is_copy_constructible_v<cue::ProjectDescriptor>);
static_assert(std::is_nothrow_move_constructible_v<cue::ProjectId>);
static_assert(std::is_nothrow_move_constructible_v<cue::ProjectRoots>);
static_assert(std::is_nothrow_move_constructible_v<cue::ProjectDescriptor>);
static_assert(!std::is_copy_constructible_v<cue::RecentProject>);
static_assert(!std::is_copy_constructible_v<cue::RecentProjectRegistry>);
static_assert(std::is_nothrow_move_constructible_v<cue::RecentProject>);
static_assert(std::is_nothrow_move_constructible_v<cue::RecentProjectRegistry>);
static_assert(!std::is_copy_constructible_v<cue::ProjectCapabilityProfile>);
static_assert(!std::is_copy_constructible_v<cue::ProjectCapabilitySnapshot>);
static_assert(!std::is_copy_constructible_v<cue::ProjectCompatibilityReport>);
static_assert(std::is_nothrow_move_constructible_v<cue::ProjectCapabilityProfile>);
static_assert(std::is_nothrow_move_constructible_v<cue::ProjectCapabilitySnapshot>);
static_assert(std::is_nothrow_move_constructible_v<cue::ProjectCompatibilityReport>);
static_assert(std::is_same_v<decltype(std::declval<const cue::ProjectRoots &>().source_assets()),
                             const cue::RelativePath &>);
static_assert(std::is_same_v<decltype(std::declval<const cue::ProjectRoots &>().runtime_assets()),
                             const cue::RelativePath &>);
static_assert(
    std::is_same_v<decltype(std::declval<const cue::ProjectRoots &>().generated()), const cue::RelativePath &>);
static_assert(std::is_same_v<decltype(std::declval<const cue::ProjectRoots &>().saved()), const cue::RelativePath &>);

/// @brief Project Public Header が単独 Target で Compile 可能か検証する
int main()
{
    return 0;
}

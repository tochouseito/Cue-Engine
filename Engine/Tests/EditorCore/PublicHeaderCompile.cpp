#include <Cue/EditorCore/EditorController.h>
#include <Cue/EditorCore/EditorDocument.h>
#include <Cue/EditorCore/Error.h>

#include <utility>

namespace
{
/// @brief 匿名Braced InitでFactory Passkeyを回避できるか検査する
template <typename T>
concept SupportsAnonymousConstructionKey =
    requires(cue::ProjectDescriptor &&a_descriptor, const cue::AssertContext &a_assertContext) {
        T({}, std::move(a_descriptor), a_assertContext);
    };

static_assert(!SupportsAnonymousConstructionKey<cue::editor_core::EditorController>);
} // namespace

/// @brief Cue.EditorCore Public Headerが自己完結してCompileできることを検証する
int main()
{
    return 0;
}

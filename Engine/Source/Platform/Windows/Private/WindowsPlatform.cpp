#include <Cue/Platform/Windows/WindowsPlatform.h>

#include "WindowsWindow.h"
#include "WindowsUtilities.h"

#include <Cue/Foundation/Assert.h>
#include <Cue/Foundation/Error.h>

#include <Windows.h>

#include <cstdlib>
#include <memory>
#include <utility>

namespace
{
using cue::windows_private::make_native_error;

constexpr std::int64_t k_moduleHandleUnavailable = 1;

/// @brief Allocation 失敗を追加 Allocation なしで Fatal 終了境界へ渡し、復帰時も Process を停止する
[[noreturn]] void terminate_allocation(const cue::AssertContext &a_context) noexcept
{
    a_context.fatal_handler().terminate("Windows Window System allocation failed");
    std::abort();
}

} // namespace

namespace cue
{
Result<std::unique_ptr<WindowSystem>> create_windows_window_system(const AssertContext &a_assertContext) noexcept
{
    // Window Class の登録と Native Window 作成を同じ実行 Module に結び付けるため Handle を取得する
    HINSTANCE instance = GetModuleHandleW(nullptr);

    if (instance == nullptr)
    {
        DWORD nativeCode = GetLastError();
        return Result<std::unique_ptr<WindowSystem>>::failure(make_native_error(
            a_assertContext, k_moduleHandleUnavailable, "Windows module handle could not be acquired", nativeCode));
    }

    try
    {
        // 呼出側には Platform 非依存の所有権だけを返し、Win32 型の伝播をこの生成境界で止める
        std::unique_ptr<WindowSystem> system = std::make_unique<WindowsWindowSystem>(a_assertContext, instance);
        return Result<std::unique_ptr<WindowSystem>>::success(std::move(system));
    }
    catch (...)
    {
        terminate_allocation(a_assertContext);
    }
}
} // namespace cue

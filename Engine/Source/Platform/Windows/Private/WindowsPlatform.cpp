#include <Cue/Platform/Windows/WindowsPlatform.h>

#include "WindowsWindow.h"

#include <Cue/Foundation/Assert.h>
#include <Cue/Foundation/Error.h>

#include <Windows.h>

#include <cstdlib>
#include <memory>
#include <utility>

namespace
{
constexpr std::int64_t k_moduleHandleUnavailable = 1;

[[noreturn]] void terminate_allocation(const cue::AssertContext &a_context) noexcept
{
    a_context.fatal_handler().terminate("Windows Window System allocation failed");
    std::abort();
}

[[nodiscard]] cue::Error make_native_error(const cue::AssertContext &a_context, std::int64_t a_code,
                                           std::string_view a_summary, DWORD a_nativeCode) noexcept
{
    cue::ErrorCode code = cue::ErrorCode::create(a_context.fatal_handler(), "Cue.Platform.Windows", a_code);
    cue::NativeError nativeError = cue::NativeError::create(a_context.fatal_handler(), "Win32", a_nativeCode);
    return cue::Error::create(a_context.fatal_handler(), std::move(code), a_summary, std::move(nativeError));
}
} // namespace

namespace cue
{
Result<std::unique_ptr<WindowSystem>> create_windows_window_system(const AssertContext &a_assertContext) noexcept
{
    HINSTANCE instance = GetModuleHandleW(nullptr);

    if (instance == nullptr)
    {
        DWORD nativeCode = GetLastError();
        return Result<std::unique_ptr<WindowSystem>>::failure(make_native_error(
            a_assertContext, k_moduleHandleUnavailable, "Windows module handle could not be acquired", nativeCode));
    }

    try
    {
        std::unique_ptr<WindowSystem> system = std::make_unique<WindowsWindowSystem>(a_assertContext, instance);
        return Result<std::unique_ptr<WindowSystem>>::success(std::move(system));
    }
    catch (...)
    {
        terminate_allocation(a_assertContext);
    }
}
} // namespace cue

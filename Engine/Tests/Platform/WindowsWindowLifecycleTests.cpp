#include <Cue/Foundation/Assert.h>
#include <Cue/Foundation/Fatal.h>
#include <Cue/Foundation/Log.h>
#include <Cue/Platform/Windows/WindowsPlatform.h>
#include <Cue/Platform/Windows/WindowsWindowInterop.h>

#include <Windows.h>

#include <cstdlib>
#include <iterator>
#include <memory>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace
{
constexpr wchar_t k_windowClassName[] = L"CueEngine.Window";

class TestFatalHandler final : public cue::FatalHandler
{
  public:
    [[noreturn]] void terminate() noexcept override
    {
        std::_Exit(75);
    }

    [[noreturn]] void terminate(std::string_view) noexcept override
    {
        std::_Exit(76);
    }
};

[[nodiscard]] std::unique_ptr<cue::Logger> create_logger(TestFatalHandler &a_handler)
{
    std::vector<std::unique_ptr<cue::LogSink>> sinks;
    return std::make_unique<cue::Logger>(a_handler, std::move(sinks));
}

[[nodiscard]] bool has_expected_native_error(const cue::Error &a_error, DWORD a_nativeCode)
{
    const cue::NativeError *nativeError = a_error.try_native_error();
    return a_error.code().domain() == "Cue.Platform.Windows" && nativeError != nullptr &&
           nativeError->domain() == "Win32" && nativeError->value() == a_nativeCode;
}

[[nodiscard]] bool is_window_class_unregistered();

[[nodiscard]] bool test_descriptor_failures(cue::WindowSystem &a_system)
{
    cue::WindowDescriptor zeroSize = {"zero", {0, 360}};
    cue::Result<std::unique_ptr<cue::Window>> zeroResult = a_system.create_window(zeroSize);

    if (zeroResult || zeroResult.try_error() == nullptr || zeroResult.try_error()->try_native_error() != nullptr)
    {
        return false;
    }

    const char invalidTitle[] = {static_cast<char>(0xc3), static_cast<char>(0x28)};
    cue::WindowDescriptor invalidUtf8 = {std::string_view(invalidTitle, sizeof(invalidTitle)), {640, 360}};
    cue::Result<std::unique_ptr<cue::Window>> utf8Result = a_system.create_window(invalidUtf8);

    if (utf8Result || utf8Result.try_error() == nullptr || utf8Result.try_error()->try_native_error() == nullptr)
    {
        return false;
    }

    cue::WindowDescriptor oversized = {"oversized", {UINT32_MAX, 360}};
    cue::Result<std::unique_ptr<cue::Window>> oversizedResult = a_system.create_window(oversized);
    return !oversizedResult && oversizedResult.try_error() != nullptr &&
           oversizedResult.try_error()->try_native_error() == nullptr;
}

[[nodiscard]] bool test_class_registration_rollback(cue::WindowSystem &a_system)
{
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW conflictingClass = {};
    conflictingClass.cbSize = sizeof(conflictingClass);
    conflictingClass.lpfnWndProc = DefWindowProcW;
    conflictingClass.hInstance = instance;
    conflictingClass.lpszClassName = k_windowClassName;

    if (RegisterClassExW(&conflictingClass) == 0)
    {
        return false;
    }

    cue::WindowDescriptor descriptor = {"class conflict", {640, 360}};
    cue::Result<std::unique_ptr<cue::Window>> result = a_system.create_window(descriptor);
    bool didFailAsExpected = !result && result.try_error() != nullptr &&
                             has_expected_native_error(*result.try_error(), ERROR_CLASS_ALREADY_EXISTS);
    bool didUnregister = UnregisterClassW(k_windowClassName, instance) != FALSE;
    return didFailAsExpected && didUnregister;
}

[[nodiscard]] bool test_shared_window_class(cue::WindowSystem &a_firstSystem, cue::AssertContext &a_context)
{
    cue::WindowDescriptor firstDescriptor = {"first system", {320, 180}};
    cue::Result<std::unique_ptr<cue::Window>> firstResult = a_firstSystem.create_window(firstDescriptor);
    cue::Result<std::unique_ptr<cue::WindowSystem>> secondSystemResult = cue::create_windows_window_system(a_context);

    if (!firstResult || !secondSystemResult)
    {
        return false;
    }

    std::unique_ptr<cue::Window> firstWindow = std::move(*firstResult.try_value());
    std::unique_ptr<cue::WindowSystem> secondSystem = std::move(*secondSystemResult.try_value());
    cue::WindowDescriptor secondDescriptor = {"second system", {320, 180}};
    cue::Result<std::unique_ptr<cue::Window>> secondResult = secondSystem->create_window(secondDescriptor);

    if (!secondResult)
    {
        return false;
    }

    std::unique_ptr<cue::Window> secondWindow = std::move(*secondResult.try_value());

    if (!firstWindow->destroy() || !secondWindow->destroy())
    {
        return false;
    }

    firstWindow.reset();
    secondWindow.reset();
    secondSystem.reset();
    return is_window_class_unregistered();
}

[[nodiscard]] bool test_window_lifecycle(cue::WindowSystem &a_system)
{
    constexpr std::uint32_t k_width = 640;
    constexpr std::uint32_t k_height = 360;
    constexpr char k_title[] = "CueEngine Window Lifecycle \xe6\xa4\x9c\xe8\xa8\xbc";
    constexpr wchar_t k_nativeTitle[] = L"CueEngine Window Lifecycle \u691c\u8a3c";
    cue::WindowDescriptor descriptor = {k_title, {k_width, k_height}};
    cue::Result<std::unique_ptr<cue::Window>> createResult = a_system.create_window(descriptor);

    if (!createResult)
    {
        return false;
    }

    std::unique_ptr<cue::Window> window = std::move(*createResult.try_value());
    cue::Result<cue::NativeWindowView> nativeViewResult = cue::get_native_window_view(*window);

    if (!nativeViewResult)
    {
        return false;
    }

    const cue::NativeWindowView *nativeView = nativeViewResult.try_value();
    HWND nativeWindow = static_cast<HWND>(const_cast<void *>(nativeView->value()));

    if (nativeView->kind() != cue::NativeWindowKind::Win32 || nativeWindow == nullptr)
    {
        return false;
    }

    wchar_t actualTitle[64] = {};
    int actualTitleLength = GetWindowTextW(nativeWindow, actualTitle, 64);

    if (actualTitleLength != static_cast<int>(std::size(k_nativeTitle) - 1) ||
        std::wstring_view(actualTitle, static_cast<std::size_t>(actualTitleLength)) != k_nativeTitle ||
        window->state() != cue::WindowState::Created || window->client_size().width != k_width ||
        window->client_size().height != k_height)
    {
        return false;
    }

    RECT clientRectangle = {};

    if (GetClientRect(nativeWindow, &clientRectangle) == FALSE ||
        static_cast<std::uint32_t>(clientRectangle.right - clientRectangle.left) != k_width ||
        static_cast<std::uint32_t>(clientRectangle.bottom - clientRectangle.top) != k_height)
    {
        return false;
    }

    cue::Result<std::unique_ptr<cue::Window>> duplicateResult = a_system.create_window(descriptor);

    if (duplicateResult || duplicateResult.try_error() == nullptr ||
        duplicateResult.try_error()->try_native_error() != nullptr)
    {
        return false;
    }

    cue::Result<void> showResult = window->show();

    if (!showResult || window->state() != cue::WindowState::Visible || IsWindowVisible(nativeWindow) == FALSE)
    {
        return false;
    }

    cue::Result<void> destroyResult = window->destroy();
    cue::Result<void> repeatedDestroyResult = window->destroy();

    if (!destroyResult || !repeatedDestroyResult || window->state() != cue::WindowState::Destroyed ||
        IsWindow(nativeWindow) != FALSE)
    {
        return false;
    }

    window.reset();
    cue::WindowDescriptor replacementDescriptor = {"replacement", {320, 180}};
    cue::Result<std::unique_ptr<cue::Window>> replacementResult = a_system.create_window(replacementDescriptor);

    if (!replacementResult)
    {
        return false;
    }

    std::unique_ptr<cue::Window> replacement = std::move(*replacementResult.try_value());
    cue::Result<cue::NativeWindowView> replacementViewResult = cue::get_native_window_view(*replacement);

    if (!replacementViewResult)
    {
        return false;
    }

    HWND replacementWindow = static_cast<HWND>(const_cast<void *>(replacementViewResult.try_value()->value()));
    replacement.reset();
    return IsWindow(replacementWindow) == FALSE;
}

[[nodiscard]] bool is_window_class_unregistered()
{
    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    SetLastError(ERROR_SUCCESS);
    BOOL didFindClass = GetClassInfoExW(GetModuleHandleW(nullptr), k_windowClassName, &windowClass);
    return didFindClass == FALSE && GetLastError() == ERROR_CLASS_DOES_NOT_EXIST;
}

[[nodiscard]] int run_lifecycle()
{
    TestFatalHandler handler;
    std::unique_ptr<cue::Logger> logger = create_logger(handler);
    cue::AssertContext context(*logger, handler);
    cue::Result<std::unique_ptr<cue::WindowSystem>> systemResult = cue::create_windows_window_system(context);

    if (!systemResult)
    {
        return 1;
    }

    std::unique_ptr<cue::WindowSystem> system = std::move(*systemResult.try_value());

    if (!test_descriptor_failures(*system))
    {
        return 2;
    }

    if (!test_class_registration_rollback(*system))
    {
        return 3;
    }

    if (!test_shared_window_class(*system, context))
    {
        return 4;
    }

    if (!test_window_lifecycle(*system))
    {
        return 5;
    }

    if (!is_window_class_unregistered())
    {
        return 6;
    }

    system.reset();
    return is_window_class_unregistered() ? 0 : 7;
}

[[nodiscard]] int run_invalid_show()
{
    TestFatalHandler handler;
    std::unique_ptr<cue::Logger> logger = create_logger(handler);
    cue::AssertContext context(*logger, handler);
    cue::Result<std::unique_ptr<cue::WindowSystem>> systemResult = cue::create_windows_window_system(context);

    if (!systemResult)
    {
        return 10;
    }

    std::unique_ptr<cue::WindowSystem> system = std::move(*systemResult.try_value());
    cue::WindowDescriptor descriptor = {"invalid show", {320, 180}};
    cue::Result<std::unique_ptr<cue::Window>> windowResult = system->create_window(descriptor);

    if (!windowResult)
    {
        return 11;
    }

    std::unique_ptr<cue::Window> window = std::move(*windowResult.try_value());

    if (!window->show())
    {
        return 12;
    }

#if CUE_ENABLE_ASSERTS
    static_cast<void>(window->show());
    return 13;
#else
    static_cast<void>(window->destroy());
    return 0;
#endif
}

[[nodiscard]] int run_invalid_native_view()
{
    TestFatalHandler handler;
    std::unique_ptr<cue::Logger> logger = create_logger(handler);
    cue::AssertContext context(*logger, handler);
    cue::Result<std::unique_ptr<cue::WindowSystem>> systemResult = cue::create_windows_window_system(context);

    if (!systemResult)
    {
        return 14;
    }

    std::unique_ptr<cue::WindowSystem> system = std::move(*systemResult.try_value());
    cue::WindowDescriptor descriptor = {"invalid native view", {320, 180}};
    cue::Result<std::unique_ptr<cue::Window>> windowResult = system->create_window(descriptor);

    if (!windowResult)
    {
        return 15;
    }

    std::unique_ptr<cue::Window> window = std::move(*windowResult.try_value());

    if (!window->destroy())
    {
        return 16;
    }

#if CUE_ENABLE_ASSERTS
    static_cast<void>(cue::get_native_window_view(*window));
    return 17;
#else
    return 0;
#endif
}

[[nodiscard]] int run_thread_violation()
{
    TestFatalHandler handler;
    std::unique_ptr<cue::Logger> logger = create_logger(handler);
    cue::AssertContext context(*logger, handler);
    cue::Result<std::unique_ptr<cue::WindowSystem>> systemResult = cue::create_windows_window_system(context);

    if (!systemResult)
    {
        return 18;
    }

    std::unique_ptr<cue::WindowSystem> system = std::move(*systemResult.try_value());
    cue::WindowDescriptor descriptor = {"thread violation", {320, 180}};
    cue::Result<std::unique_ptr<cue::Window>> windowResult = system->create_window(descriptor);

    if (!windowResult)
    {
        return 19;
    }

    std::unique_ptr<cue::Window> window = std::move(*windowResult.try_value());

#if CUE_ENABLE_ASSERTS
    std::thread invalidThread([&window]() { static_cast<void>(window->state()); });
    invalidThread.join();
    return 22;
#else
    static_cast<void>(window->destroy());
    return 0;
#endif
}
} // namespace

int main(int a_argumentCount, char **a_arguments)
{
    if (a_argumentCount != 2)
    {
        return 20;
    }

    std::string_view mode = a_arguments[1];

    if (mode == "Lifecycle")
    {
        return run_lifecycle();
    }

    if (mode == "InvalidShow")
    {
        return run_invalid_show();
    }

    if (mode == "InvalidNativeView")
    {
        return run_invalid_native_view();
    }

    if (mode == "ThreadViolation")
    {
        return run_thread_violation();
    }

    return 21;
}

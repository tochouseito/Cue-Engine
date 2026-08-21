#include "WindowsWindow.h"

#include "UtfConversion.h"

#include <Cue/Foundation/Assert.h>
#include <Cue/Foundation/Error.h>

#include <Windows.h>

#include <cstdlib>
#include <limits>
#include <memory>
#include <string>
#include <utility>

namespace
{
constexpr wchar_t k_windowClassName[] = L"CueEngine.Window";
constexpr DWORD k_windowStyle = WS_OVERLAPPEDWINDOW;

constexpr std::int64_t k_invalidDescriptor = 3;
constexpr std::int64_t k_windowRectangleFailed = 4;
constexpr std::int64_t k_classRegistrationFailed = 5;
constexpr std::int64_t k_windowCreationFailed = 6;
constexpr std::int64_t k_clientSizeQueryFailed = 7;
constexpr std::int64_t k_windowDestroyFailed = 8;
constexpr std::int64_t k_windowAlreadyExists = 9;
constexpr std::int64_t k_classUnregistrationFailed = 10;

[[noreturn]] void terminate_allocation(const cue::AssertContext &a_context) noexcept
{
    a_context.fatal_handler().terminate("Windows Window allocation failed");
    std::abort();
}

[[nodiscard]] cue::Error make_error(const cue::AssertContext &a_context, std::int64_t a_code,
                                    std::string_view a_summary) noexcept
{
    cue::ErrorCode code = cue::ErrorCode::create(a_context.fatal_handler(), "Cue.Platform.Windows", a_code);
    return cue::Error::create(a_context.fatal_handler(), std::move(code), a_summary);
}

[[nodiscard]] cue::Error make_native_error(const cue::AssertContext &a_context, std::int64_t a_code,
                                           std::string_view a_summary, DWORD a_nativeCode) noexcept
{
    cue::ErrorCode code = cue::ErrorCode::create(a_context.fatal_handler(), "Cue.Platform.Windows", a_code);
    cue::NativeError nativeError = cue::NativeError::create(a_context.fatal_handler(), "Win32", a_nativeCode);
    return cue::Error::create(a_context.fatal_handler(), std::move(code), a_summary, std::move(nativeError));
}

class WindowClassRegistry final
{
  public:
    [[nodiscard]] cue::Result<void> acquire(const cue::AssertContext &a_context, HINSTANCE a_instance) noexcept
    {
        AcquireSRWLockExclusive(&m_lock);

        if (m_referenceCount > 0)
        {
            CUE_ASSERT(a_context, m_instance == a_instance,
                       "Window Class Registry can only manage one Module instance");
            ++m_referenceCount;
            ReleaseSRWLockExclusive(&m_lock);
            return cue::Result<void>::success();
        }

        WNDCLASSEXW windowClass = {};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.style = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc = cue::WindowsWindow::window_procedure;
        windowClass.hInstance = a_instance;
        windowClass.lpszClassName = k_windowClassName;

        if (RegisterClassExW(&windowClass) == 0)
        {
            DWORD nativeCode = GetLastError();
            WNDCLASSEXW existingClass = {};
            existingClass.cbSize = sizeof(existingClass);
            bool canAcquireExisting = nativeCode == ERROR_CLASS_ALREADY_EXISTS &&
                                      GetClassInfoExW(a_instance, k_windowClassName, &existingClass) != FALSE &&
                                      existingClass.lpfnWndProc == cue::WindowsWindow::window_procedure;

            if (!canAcquireExisting)
            {
                ReleaseSRWLockExclusive(&m_lock);
                return cue::Result<void>::failure(make_native_error(
                    a_context, k_classRegistrationFailed, "Windows Window Class registration failed", nativeCode));
            }
        }

        m_instance = a_instance;
        m_referenceCount = 1;
        ReleaseSRWLockExclusive(&m_lock);
        return cue::Result<void>::success();
    }

    void release(const cue::AssertContext &a_context, HINSTANCE a_instance) noexcept
    {
        AcquireSRWLockExclusive(&m_lock);
        CUE_ASSERT(a_context, m_referenceCount > 0, "Window Class reference count must not underflow");
        CUE_ASSERT(a_context, m_instance == a_instance,
                   "Window Class reference must be released by its Module instance");
        --m_referenceCount;

        if (m_referenceCount > 0)
        {
            ReleaseSRWLockExclusive(&m_lock);
            return;
        }

        BOOL didUnregister = UnregisterClassW(k_windowClassName, a_instance);
        DWORD nativeCode = didUnregister != FALSE ? ERROR_SUCCESS : GetLastError();
        m_instance = nullptr;
        ReleaseSRWLockExclusive(&m_lock);

        if (didUnregister != FALSE || nativeCode == ERROR_CLASS_DOES_NOT_EXIST)
        {
            return;
        }

        cue::Error error = make_native_error(a_context, k_classUnregistrationFailed,
                                             "Windows Window Class unregistration failed", nativeCode);
        [[maybe_unused]] cue::LogResult logResult = a_context.logger().log(
            cue::LogLevel::Warning, "Windows Window Class could not be unregistered", std::move(error));
    }

  private:
    SRWLOCK m_lock = SRWLOCK_INIT;
    HINSTANCE m_instance = nullptr;
    std::uint32_t m_referenceCount = 0;
};

[[nodiscard]] WindowClassRegistry &window_class_registry() noexcept
{
    // Win32 Window ClassはModule単位Resourceのため、Window System間で参照数を共有する
    static WindowClassRegistry registry;
    return registry;
}

[[nodiscard]] cue::Result<void> validate_descriptor(const cue::WindowDescriptor &a_descriptor,
                                                    const cue::AssertContext &a_context) noexcept
{
    if (a_descriptor.clientSize.width == 0 || a_descriptor.clientSize.height == 0 ||
        a_descriptor.title.find('\0') != std::string_view::npos)
    {
        return cue::Result<void>::failure(make_error(a_context, k_invalidDescriptor, "Window descriptor is invalid"));
    }

    constexpr std::uint32_t k_maxSignedSize = static_cast<std::uint32_t>(std::numeric_limits<int>::max());

    if (a_descriptor.clientSize.width > k_maxSignedSize || a_descriptor.clientSize.height > k_maxSignedSize)
    {
        return cue::Result<void>::failure(
            make_error(a_context, k_invalidDescriptor, "Window client size is out of range"));
    }

    return cue::Result<void>::success();
}

[[nodiscard]] cue::Result<RECT> calculate_window_rectangle(const cue::WindowDescriptor &a_descriptor,
                                                           const cue::AssertContext &a_context) noexcept
{
    RECT rectangle = {0, 0, static_cast<LONG>(a_descriptor.clientSize.width),
                      static_cast<LONG>(a_descriptor.clientSize.height)};

    if (AdjustWindowRectEx(&rectangle, k_windowStyle, FALSE, 0) == FALSE)
    {
        DWORD nativeCode = GetLastError();
        return cue::Result<RECT>::failure(
            make_native_error(a_context, k_windowRectangleFailed, "Window rectangle calculation failed", nativeCode));
    }

    std::int64_t width = static_cast<std::int64_t>(rectangle.right) - rectangle.left;
    std::int64_t height = static_cast<std::int64_t>(rectangle.bottom) - rectangle.top;

    if (width <= 0 || height <= 0 || width > std::numeric_limits<int>::max() ||
        height > std::numeric_limits<int>::max())
    {
        return cue::Result<RECT>::failure(
            make_error(a_context, k_invalidDescriptor, "Window outer size is out of range"));
    }

    rectangle.left = 0;
    rectangle.top = 0;
    rectangle.right = static_cast<LONG>(width);
    rectangle.bottom = static_cast<LONG>(height);
    return cue::Result<RECT>::success(std::move(rectangle));
}
} // namespace

namespace cue
{
WindowsWindowSystem::WindowsWindowSystem(const AssertContext &a_assertContext, HINSTANCE a_instance) noexcept
    : m_assertContext(&a_assertContext), m_instance(a_instance), m_threadId(GetCurrentThreadId())
{
}

WindowsWindowSystem::~WindowsWindowSystem() noexcept
{
    CUE_ASSERT(*m_assertContext, GetCurrentThreadId() == m_threadId,
               "Window System must be destroyed on its creation thread");
    CUE_ASSERT(*m_assertContext, m_window == nullptr, "Window System must outlive all Windows Window owners");
}

Result<std::unique_ptr<Window>> WindowsWindowSystem::create_window(const WindowDescriptor &a_descriptor) noexcept
{
    CUE_ASSERT(*m_assertContext, GetCurrentThreadId() == m_threadId,
               "Window must be created on the Window System thread");

    const AssertContext &context = *m_assertContext;

    if (m_window != nullptr)
    {
        return Result<std::unique_ptr<Window>>::failure(
            make_error(context, k_windowAlreadyExists, "Window System already has a Main Window"));
    }

    Result<void> descriptorResult = validate_descriptor(a_descriptor, context);

    if (!descriptorResult)
    {
        return Result<std::unique_ptr<Window>>::failure(std::move(*descriptorResult.try_error()));
    }

    Result<std::wstring> titleResult = utf8_to_utf16(a_descriptor.title, context);

    if (!titleResult)
    {
        return Result<std::unique_ptr<Window>>::failure(std::move(*titleResult.try_error()));
    }

    Result<RECT> rectangleResult = calculate_window_rectangle(a_descriptor, context);

    if (!rectangleResult)
    {
        return Result<std::unique_ptr<Window>>::failure(std::move(*rectangleResult.try_error()));
    }

    try
    {
        std::unique_ptr<WindowsWindow> window = std::make_unique<WindowsWindow>(*this);
        Result<void> classResult = register_window_class();

        if (!classResult)
        {
            return Result<std::unique_ptr<Window>>::failure(std::move(*classResult.try_error()));
        }

        window->acquire_class_reference();
        RECT *rectangle = rectangleResult.try_value();
        std::wstring *title = titleResult.try_value();
        Result<void> createResult =
            window->create_native(*title, static_cast<int>(rectangle->right), static_cast<int>(rectangle->bottom));

        if (!createResult)
        {
            return Result<std::unique_ptr<Window>>::failure(std::move(*createResult.try_error()));
        }

        window->publish();
        std::unique_ptr<Window> result = std::move(window);
        return Result<std::unique_ptr<Window>>::success(std::move(result));
    }
    catch (...)
    {
        terminate_allocation(context);
    }
}

Result<PumpStatus> WindowsWindowSystem::pump_events() noexcept
{
    CUE_ASSERT(*m_assertContext, GetCurrentThreadId() == m_threadId,
               "Window events must be pumped on the Window System thread");
    return Result<PumpStatus>::success(PumpStatus::Running);
}

const AssertContext &WindowsWindowSystem::assert_context() const noexcept
{
    return *m_assertContext;
}

HINSTANCE WindowsWindowSystem::instance() const noexcept
{
    return m_instance;
}

DWORD WindowsWindowSystem::thread_id() const noexcept
{
    return m_threadId;
}

void WindowsWindowSystem::publish_window(WindowsWindow &a_window) noexcept
{
    CUE_ASSERT(*m_assertContext, GetCurrentThreadId() == m_threadId, "Window must be published on the Window thread");
    CUE_ASSERT(*m_assertContext, m_window == nullptr, "Window System can publish only one Main Window");
    m_window = &a_window;
}

void WindowsWindowSystem::release_window(WindowsWindow &a_window) noexcept
{
    CUE_ASSERT(*m_assertContext, GetCurrentThreadId() == m_threadId,
               "Window must release its System reference on the Window thread");
    CUE_ASSERT(*m_assertContext, m_window == nullptr || m_window == &a_window,
               "Window System can only release its current Main Window");

    if (m_window == &a_window)
    {
        m_window = nullptr;
    }

    unregister_window_class();
}

Result<void> WindowsWindowSystem::register_window_class() noexcept
{
    return window_class_registry().acquire(*m_assertContext, m_instance);
}

void WindowsWindowSystem::unregister_window_class() noexcept
{
    window_class_registry().release(*m_assertContext, m_instance);
}

WindowsWindow::WindowsWindow(WindowsWindowSystem &a_system) noexcept : m_system(&a_system)
{
}

WindowsWindow::~WindowsWindow() noexcept
{
    verify_thread();

    if (m_window != nullptr)
    {
        static_cast<void>(DestroyWindow(m_window));
    }

    release_system_reference();
}

Result<void> WindowsWindow::show() noexcept
{
    verify_thread();
    CUE_ASSERT(m_system->assert_context(), m_state == WindowState::Created,
               "Window can only be shown from Created state");
    ShowWindow(m_window, SW_SHOW);
    m_state = WindowState::Visible;
    return Result<void>::success();
}

Result<void> WindowsWindow::destroy() noexcept
{
    verify_thread();

    if (m_state == WindowState::Destroyed)
    {
        return Result<void>::success();
    }

    if (DestroyWindow(m_window) == FALSE)
    {
        DWORD nativeCode = GetLastError();
        return Result<void>::failure(make_native_error(m_system->assert_context(), k_windowDestroyFailed,
                                                       "Windows Window destruction failed", nativeCode));
    }

    CUE_ASSERT(m_system->assert_context(), m_window == nullptr,
               "DestroyWindow must synchronously detach the Window owner");
    release_system_reference();
    return Result<void>::success();
}

WindowState WindowsWindow::state() const noexcept
{
    verify_thread();
    return m_state;
}

WindowSize WindowsWindow::client_size() const noexcept
{
    verify_thread();
    return m_clientSize;
}

bool WindowsWindow::try_pop_event(WindowEvent &a_event) noexcept
{
    verify_thread();
    static_cast<void>(a_event);
    return false;
}

const void *WindowsWindow::native_view_value() const noexcept
{
    verify_thread();
    CUE_ASSERT(m_system->assert_context(), m_state != WindowState::Destroyed,
               "Destroyed Window does not have a Native View");
    return m_window;
}

void WindowsWindow::acquire_class_reference() noexcept
{
    verify_thread();
    CUE_ASSERT(m_system->assert_context(), !m_hasClassReference, "Window Class reference must only be acquired once");
    m_hasClassReference = true;
}

Result<void> WindowsWindow::create_native(std::wstring_view a_title, int a_width, int a_height) noexcept
{
    verify_thread();
    CUE_ASSERT(m_system->assert_context(), m_hasClassReference,
               "Window Class must be registered before native creation");
    CUE_ASSERT(m_system->assert_context(), m_window == nullptr, "Native Window must only be created once");

    HWND window = CreateWindowExW(0, k_windowClassName, a_title.data(), k_windowStyle, CW_USEDEFAULT, CW_USEDEFAULT,
                                  a_width, a_height, nullptr, nullptr, m_system->instance(), this);

    if (window == nullptr)
    {
        DWORD nativeCode = GetLastError();
        return Result<void>::failure(make_native_error(m_system->assert_context(), k_windowCreationFailed,
                                                       "Windows Window creation failed", nativeCode));
    }

    CUE_ASSERT(m_system->assert_context(), m_window == window,
               "Window Procedure must attach the native handle during creation");

    RECT clientRectangle = {};

    if (GetClientRect(window, &clientRectangle) == FALSE)
    {
        DWORD nativeCode = GetLastError();
        static_cast<void>(DestroyWindow(window));
        return Result<void>::failure(make_native_error(m_system->assert_context(), k_clientSizeQueryFailed,
                                                       "Windows Window client size query failed", nativeCode));
    }

    m_clientSize = {static_cast<std::uint32_t>(clientRectangle.right - clientRectangle.left),
                    static_cast<std::uint32_t>(clientRectangle.bottom - clientRectangle.top)};
    m_state = WindowState::Created;
    return Result<void>::success();
}

void WindowsWindow::publish() noexcept
{
    verify_thread();
    CUE_ASSERT(m_system->assert_context(), m_window != nullptr, "Native Window must exist before publication");
    m_isPublished = true;
    m_system->publish_window(*this);
}

LRESULT CALLBACK WindowsWindow::window_procedure(HWND a_window, UINT a_message, WPARAM a_wParam,
                                                 LPARAM a_lParam) noexcept
{
    WindowsWindow *owner = reinterpret_cast<WindowsWindow *>(GetWindowLongPtrW(a_window, GWLP_USERDATA));

    if (a_message == WM_NCCREATE)
    {
        CREATESTRUCTW *createData = reinterpret_cast<CREATESTRUCTW *>(a_lParam);
        owner = static_cast<WindowsWindow *>(createData->lpCreateParams);

        SetLastError(ERROR_SUCCESS);
        LONG_PTR previousOwner = SetWindowLongPtrW(a_window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(owner));

        if (previousOwner == 0 && GetLastError() != ERROR_SUCCESS)
        {
            return FALSE;
        }

        owner->m_window = a_window;
    }

    if (owner == nullptr)
    {
        return DefWindowProcW(a_window, a_message, a_wParam, a_lParam);
    }

    return owner->process_message(a_message, a_wParam, a_lParam);
}

LRESULT WindowsWindow::process_message(UINT a_message, WPARAM a_wParam, LPARAM a_lParam) noexcept
{
    if (a_message == WM_CLOSE)
    {
        m_state = WindowState::CloseRequested;
        return 0;
    }

    if (a_message == WM_DESTROY)
    {
        m_state = WindowState::Destroyed;
        return 0;
    }

    if (a_message == WM_NCDESTROY)
    {
        HWND window = m_window;
        LRESULT result = DefWindowProcW(window, a_message, a_wParam, a_lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        m_window = nullptr;
        return result;
    }

    return DefWindowProcW(m_window, a_message, a_wParam, a_lParam);
}

void WindowsWindow::release_system_reference() noexcept
{
    if (!m_hasClassReference)
    {
        return;
    }

    m_hasClassReference = false;
    m_isPublished = false;
    m_system->release_window(*this);
}

void WindowsWindow::verify_thread() const noexcept
{
    CUE_ASSERT(m_system->assert_context(), GetCurrentThreadId() == m_system->thread_id(),
               "Windows Window operation must run on the Window thread");
}
} // namespace cue

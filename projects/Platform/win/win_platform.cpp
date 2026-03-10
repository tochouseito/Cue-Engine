#include "win_pch.h"
#include "win_platform.h"
#include "win_native.h"
#include "private/WinApp.h"
#include "private/WinQpcClock.h"
#include "private/WinWaiter.h"
#include "private/WinThreadFactory.h"
#include "private/WinFileSystem.h"

namespace Cue::Platform
{
    std::unique_ptr<IPlatform> create_platform()
    {
        return std::make_unique<Win::WinPlatform>();
    }
}

namespace Cue::Platform::Win
{
    struct WinPlatform::Impl
    {
        WinApp app;
        std::unique_ptr<WinQpcClock> clock = std::make_unique<WinQpcClock>();
        std::unique_ptr<WinWaiter> waiter = std::make_unique<WinWaiter>(clock.get());
        std::unique_ptr<WinThreadFactory> threadFactory = std::make_unique<WinThreadFactory>();
        std::unique_ptr<WinFileSystem> fileSystem = std::make_unique<WinFileSystem>();
    };
    
    WinPlatform::WinPlatform()
        : impl(std::make_unique<Impl>())
    {
    }
    WinPlatform::~WinPlatform()
    {
    }
    Result WinPlatform::setup()
    {
        return impl->app.create_window(1200, 720, L"CueWindowClass", L"Cue Engine");
    }
    Result WinPlatform::start()
    {
        return impl->app.show_window(false);
    }
    bool WinPlatform::poll_message()
    {
        return impl->app.pump_messages();
    }
    Result WinPlatform::shutdown()
    {
        return impl->app.destroy_window();
    }

    Core::Threading::IThreadFactory& WinPlatform::get_thread_factory()
    {
        return *impl->threadFactory;
    }
    Core::Time::IClock& WinPlatform::get_clock()
    {
        return *impl->clock;
    }
    Core::Time::IWaiter& WinPlatform::get_waiter()
    {
        return *impl->waiter;
    }
    Core::IO::IFileSystem& WinPlatform::get_file_system()
    {
        return *impl->fileSystem;
    }
    NativeWindowHandle WinPlatform::get_native_window_handle() const noexcept
    {
        // 1) Win32 実体型は WinApp 側へ閉じ込め、透過ハンドルとして公開する
        return impl->app.get_native_window_handle();
    }
    uint64_t WinPlatform::register_message_handler(MessageHandler handler)
    {
        // 1) WinPlatform では型変換のみを担当し、実際の登録管理は WinApp へ委譲する。
        if (!handler)
        {
            return 0;
        }

        return impl->app.register_message_handler(
            [handler = std::move(handler)](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& outResult)
            {
                std::intptr_t nativeResult = 0;
                const bool handled = handler(
                    reinterpret_cast<NativeWindowHandle>(hwnd),
                    static_cast<uint32_t>(msg),
                    static_cast<std::uintptr_t>(wParam),
                    static_cast<std::intptr_t>(lParam),
                    nativeResult);
                outResult = static_cast<LRESULT>(nativeResult);
                return handled;
            });
    }
    bool WinPlatform::unregister_message_handler(uint64_t handlerId)
    {
        // 1) WinApp の解除結果をそのまま返す。
        return impl->app.unregister_message_handler(handlerId);
    }
    uint32_t WinPlatform::window_width() const noexcept
    {
        return impl->app.get_window_width();
    }
    uint32_t WinPlatform::window_height() const noexcept
    {
        return impl->app.get_window_height();
    }
} // namespace Cue

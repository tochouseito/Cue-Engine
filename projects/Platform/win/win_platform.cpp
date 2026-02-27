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
        return impl->app.create_window(800, 600, L"CueWindowClass", L"Cue Engine");
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
    uint32_t WinPlatform::window_width() const noexcept
    {
        return impl->app.get_window_width();
    }
    uint32_t WinPlatform::window_height() const noexcept
    {
        return impl->app.get_window_height();
    }
} // namespace Cue

#include "win_pch.h"
#include "win_platform.h"
#include "private/WinApp.h"

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
} // namespace Cue

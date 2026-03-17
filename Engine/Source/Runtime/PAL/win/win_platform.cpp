#include "win_platform.h"

namespace Cue::PAL
{
    std::unique_ptr<IPlatform> create_platform()
    {
        return std::make_unique<Win::WinPlatform>();
    }
}

namespace Cue::PAL::Win
{
    WinPlatform::WinPlatform()
    {}
    WinPlatform::~WinPlatform()
    {}
    Result WinPlatform::initialize(const platform_setup_info & info)
    {
        info;
        return Result();
    }
    Result WinPlatform::start()
    {
        return Result();
    }
    Result WinPlatform::shutdown()
    {
        return Result();
    }
    Result WinPlatform::begin_frame()
    {
        return Result();
    }
    Result WinPlatform::end_frame()
    {
        return Result();
    }
    Result WinPlatform::poll_message()
    {
        return Result();
    }
}

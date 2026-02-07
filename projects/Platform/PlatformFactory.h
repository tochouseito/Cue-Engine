#pragma once
#include <memory>

namespace Cue::Platform
{
    class IPlatform;

    std::unique_ptr<IPlatform> create_platform();
}

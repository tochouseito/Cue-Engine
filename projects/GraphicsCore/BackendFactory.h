#pragma once
#include <memory>

namespace Cue::Graphics
{
    class Backend;

    std::unique_ptr<Backend> create_backend();
}

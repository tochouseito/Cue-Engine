#pragma once
#include <memory>

namespace Cue::GraphicsCore
{
    class Backend;

    std::unique_ptr<Backend> create_backend();
}

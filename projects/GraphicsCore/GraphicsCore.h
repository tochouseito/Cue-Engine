#pragma once

namespace Cue::Graphics
{
    class Backend
    {
    public:
        Backend() = default;
        virtual ~Backend() = default;
        virtual void initialize() = 0;
        virtual void shutdown() = 0;
    };
} // namespace Cue

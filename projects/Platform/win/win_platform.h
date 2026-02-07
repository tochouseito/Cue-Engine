#pragma once
#include <Platform.h>
#include <memory>

namespace Cue::Platform::Win
{
    class WinPlatform : public IPlatform
    {
    public:
        WinPlatform();
        ~WinPlatform() override;

        Core::Result setup() override;
        Core::Result start() override;
        bool poll_message() override;
        Core::Result shutdown() override;
    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}

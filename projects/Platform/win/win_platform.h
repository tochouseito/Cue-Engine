#pragma once
#include <Platform.h>
#include <cstdint>
#include <functional>
#include <memory>
#include "PlatformFactory.h"
#include "win_native.h"

namespace Cue::Platform::Win
{
    class WinPlatform : public IPlatform
    {
    public:
        using MessageHandler = std::function<bool(NativeWindowHandle, uint32_t, std::uintptr_t, std::intptr_t, std::intptr_t&)>;

        WinPlatform();
        ~WinPlatform() override;

        Result setup() override;
        Result start() override;
        void begin_frame() override {}
        void end_frame() override {}
        bool poll_message() override;
        Result shutdown() override;

        Core::Threading::IThreadFactory& get_thread_factory() override;
        Core::Time::IClock& get_clock() override;
        Core::Time::IWaiter& get_waiter() override;
        Core::IO::IFileSystem& get_file_system() override;
        [[nodiscard]] NativeWindowHandle get_native_window_handle() const noexcept;
        [[nodiscard]] uint64_t register_message_handler(MessageHandler handler);
        bool unregister_message_handler(uint64_t handlerId);
        uint32_t window_width() const noexcept;
        uint32_t window_height() const noexcept;
    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}

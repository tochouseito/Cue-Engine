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

        /// @brief 生成
        WinPlatform();
        /// @brief 破棄
        ~WinPlatform() override;

        /// @brief 初期化
        Result setup() override;
        /// @brief 表示開始
        Result start() override;
        /// @brief フレーム開始
        void begin_frame() override {}
        /// @brief フレーム終了
        void end_frame() override {}
        /// @brief メッセージポンプ
        bool poll_message() override;
        /// @brief 終了
        Result shutdown() override;

        /// @brief スレッド factory 取得
        Core::Threading::IThreadFactory& get_thread_factory() override;
        /// @brief clock 取得
        Core::Time::IClock& get_clock() override;
        /// @brief waiter 取得
        Core::Time::IWaiter& get_waiter() override;
        /// @brief file system 取得
        Core::IO::IFileSystem& get_file_system() override;
        /// @brief native window handle 取得
        [[nodiscard]] NativeWindowHandle get_native_window_handle() const noexcept;
        /// @brief メッセージハンドラ登録
        [[nodiscard]] uint64_t register_message_handler(MessageHandler handler);
        /// @brief メッセージハンドラ解除
        bool unregister_message_handler(uint64_t handlerId);
        /// @brief ウィンドウ幅取得
        uint32_t window_width() const noexcept;
        /// @brief ウィンドウ高さ取得
        uint32_t window_height() const noexcept;
    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}

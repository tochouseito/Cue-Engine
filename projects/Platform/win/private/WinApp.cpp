#include "win_pch.h"
#include "WinApp.h"

#include <timeapi.h>
#include <wrl.h>
#ifdef CUE_DEBUG
#include <debugapi.h>
#endif

#pragma comment(lib, "winmm.lib") // timebeginperiod と timeendperiod 用

namespace Cue::Platform::Win
{
    struct WinApp::Impl
    {
        struct MessageHandlerEntry final
        {
            uint64_t m_id = 0;
            WinApp::MessageHandler m_handler{};
        };

        // windows アプリ実体
        HWND m_hwnd = nullptr;
        uint32_t m_width = 0;
        uint32_t m_height = 0;
        bool m_isComInitialized = false;
        bool m_isTimePeriodSet = false;
        bool m_shouldClose = false;
        uint64_t m_nextMessageHandlerId = 1;
        std::vector<MessageHandlerEntry> m_messageHandlers{};

        LRESULT on_message(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
        {
            // 1) 外部登録ハンドラを先に評価
            for (const MessageHandlerEntry& entry : m_messageHandlers)
            {
                if (!entry.m_handler)
                {
                    continue;
                }

                LRESULT handledResult = 0;
                const bool isHandled = entry.m_handler(hwnd, msg, wParam, lParam, handledResult);
                if (isHandled)
                {
                    return handledResult;
                }
            }

            // 2) 未処理メッセージを既定処理へ移譲
            switch (msg)
            {
            case WM_CLOSE:
                // 1) 破棄は engine 終了手順へ移譲
                // 2) メインループ終了フラグ設定
                m_shouldClose = true;
                return 0;

            case WM_SIZE:
                // 1) クライアントサイズ更新
                m_width = static_cast<uint32_t>(LOWORD(lParam));
                m_height = static_cast<uint32_t>(HIWORD(lParam));
                return 0;

            case WM_DESTROY:
                // 1) quit メッセージ送出
                ::PostQuitMessage(0);
                return 0;
            }
            return ::DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
        {
#ifndef CUE_RELEASE
#endif // !cue_release
            WinApp* self = nullptr;

            if (msg == WM_NCCREATE)
            {
                auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
                self = static_cast<WinApp*>(cs->lpCreateParams);

                ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
                self->m_impl->m_hwnd = hwnd;

                // wm_nccreate は既定処理へ移譲
                return ::DefWindowProcW(hwnd, msg, wParam, lParam);
            }

            self = reinterpret_cast<WinApp*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (self && self->m_impl)
            {
                return self->m_impl->on_message(hwnd, msg, wParam, lParam);
            }

            return ::DefWindowProcW(hwnd, msg, wParam, lParam);
        }
    };
    WinApp::WinApp()
        : m_impl(std::make_unique<Impl>())
    {
        // 1) com 初期化
        const HRESULT hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        m_impl->m_isComInitialized = SUCCEEDED(hr);
    }
    WinApp::~WinApp()
    {
        if (m_impl && m_impl->m_isComInitialized)
        {
            // 1) com 終了
            ::CoUninitialize();
        }
    }
    Result WinApp::create_window(uint32_t w, uint32_t h, const wchar_t* className, const wchar_t* titleName)
    {
        // 1) スレッド前提の初期化に失敗している場合は継続できない
        if (!m_impl->m_isComInitialized)
        {
            return Result::fail(
                Facility::Platform,
                Code::CreationFailed,
                Severity::Fatal,
                0, "Failed to initialize COM library for WinApp.");
        }

        // 2) タイムスライスを 1ms に設定する
        if (!m_impl->m_isTimePeriodSet)
        {
            const MMRESULT timeResult = ::timeBeginPeriod(1);
            if (timeResult != TIMERR_NOERROR)
            {
                return Result::fail(
                    Facility::Platform,
                    Code::CreationFailed,
                    Severity::Fatal,
                    0, "Failed to set time period for high precision timer.");
            }
            m_impl->m_isTimePeriodSet = true;
        }

        // 3) 失敗時の time period 復旧処理を用意する
        auto rollbackTimePeriod = [this]()
            {
                if (m_impl->m_isTimePeriodSet)
                {
                    ::timeEndPeriod(1);
                    m_impl->m_isTimePeriodSet = false;
                }
            };

        // 4) 作成要求サイズと module handle を保持する
        m_impl->m_width = w;
        m_impl->m_height = h;
        HINSTANCE hInstance = ::GetModuleHandleW(nullptr);

        // 5) ウィンドウクラスを登録する
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &Impl::window_proc;
        wc.lpszClassName = className;
        wc.hInstance = hInstance;
        wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);

        if (!::RegisterClassExW(&wc))
        {
            const DWORD err = ::GetLastError();
            if (err != ERROR_CLASS_ALREADY_EXISTS)
            {
                rollbackTimePeriod();
                return Result::fail(
                    Facility::Platform,
                    Code::CreationFailed,
                    Severity::Fatal,
                    0, "Failed to register window class.");
            }
        }

        // 6) クライアントサイズ維持でウィンドウを生成する
        RECT rc{ 0, 0, static_cast<LONG>(w), static_cast<LONG>(h) };
        ::AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

        HWND hwnd = ::CreateWindowExW(
            0,
            className,
            titleName,
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT,
            rc.right - rc.left,
            rc.bottom - rc.top,
            nullptr, nullptr,
            hInstance,
            this // wm_nccreate 受け渡し
        );

        if (!hwnd)
        {
            rollbackTimePeriod();
            return Result::fail(
                Facility::Platform,
                Code::CreationFailed,
                Severity::Fatal,
                0, "Failed to create window.");
        }

        // 7) 作成済み hwnd を保持する
        m_impl->m_hwnd = hwnd;

        return Result::ok();
    }
    Result WinApp::destroy_window()
    {
        // 1) 高精度 timer 設定を戻す
        if (m_impl->m_isTimePeriodSet)
        {
            ::timeEndPeriod(1);
            m_impl->m_isTimePeriodSet = false;
        }

        // 2) 生成済み hwnd を破棄する
        if (m_impl && m_impl->m_hwnd)
        {
            ::DestroyWindow(m_impl->m_hwnd);
            m_impl->m_hwnd = nullptr;
        }
        return Result::ok();
    }
    Result WinApp::show_window(bool isMaximized)
    {
        // 1) 表示状態に応じて show window を呼ぶ
        ::ShowWindow(m_impl->m_hwnd, isMaximized ? SW_MAXIMIZE : SW_SHOW);
        return Result::ok();
    }
    bool WinApp::pump_messages()
    {
        MSG msg{};
        // 1) キューを掃き出して終了メッセージを検知する
        // 2) 閉じる要求が来ていればループを終了する
        if (m_impl->m_shouldClose)
        {
            return false;
        }
        while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                return false; // 終了メッセージ検出
            }
            // 3) メッセージ変換と配送
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
        return true;
    }
    NativeWindowHandle WinApp::get_native_window_handle() const noexcept
    {
        // 1) hwnd を透過ハンドルへ変換
        return reinterpret_cast<NativeWindowHandle>(m_impl->m_hwnd);
    }
    uint32_t WinApp::get_window_width() const noexcept
    {
        // 1) 現在幅を返す
        return m_impl->m_width;
    }
    uint32_t WinApp::get_window_height() const noexcept
    {
        // 1) 現在高さを返す
        return m_impl->m_height;
    }
    uint64_t WinApp::register_message_handler(MessageHandler handler)
    {
        // 1) 空ハンドラは無効 id を返却
        if (!handler)
        {
            return 0;
        }

        // 2) 一意 id を採番して保持
        const uint64_t handlerId = m_impl->m_nextMessageHandlerId++;
        m_impl->m_messageHandlers.push_back(Impl::MessageHandlerEntry{ handlerId, std::move(handler) });
        return handlerId;
    }
    bool WinApp::unregister_message_handler(uint64_t handlerId)
    {
        // 1) 無効 id は即時失敗
        if (handlerId == 0)
        {
            return false;
        }

        // 2) 該当ハンドラだけ削除
        for (auto it = m_impl->m_messageHandlers.begin(); it != m_impl->m_messageHandlers.end(); ++it)
        {
            if (it->m_id != handlerId)
            {
                continue;
            }

            m_impl->m_messageHandlers.erase(it);
            return true;
        }

        return false;
    }
} // 名前空間 cue::platform::win

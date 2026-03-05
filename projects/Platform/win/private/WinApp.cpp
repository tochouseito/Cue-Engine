#include "win_pch.h"
#include "WinApp.h"

#include <timeapi.h>
#include <wrl.h>
#ifdef CUE_DEBUG
#include <debugapi.h>
#endif

#pragma comment(lib, "winmm.lib") // timeBeginPeriod, timeEndPeriod 用

namespace Cue::Platform::Win
{
    struct WinApp::Impl
    {
        struct MessageHandlerEntry final
        {
            uint64_t m_id = 0;
            WinApp::MessageHandler m_handler{};
        };

        // Windowsアプリケーションに関する実装の詳細をここに記述
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
            // 1) 外部登録ハンドラを先に評価し、処理済みなら標準処理へ流さない。
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

            // 2) 未処理メッセージだけ WinApp 既定処理へ渡す。
            switch (msg)
            {
            case WM_CLOSE:
                // 1) 破棄はエンジン側の終了手順に委譲する
                // 2) メインループ終了フラグを立てる
                m_shouldClose = true;
                return 0;

            case WM_SIZE:
                m_width = static_cast<uint32_t>(LOWORD(lParam));
                m_height = static_cast<uint32_t>(HIWORD(lParam));
                return 0;

            case WM_DESTROY:
                ::PostQuitMessage(0);
                return 0;
            }
            return ::DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
        {
#ifndef CUE_RELEASE
#endif // !CUE_RELEASE
            WinApp* self = nullptr;

            if (msg == WM_NCCREATE)
            {
                auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
                self = static_cast<WinApp*>(cs->lpCreateParams);

                ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
                self->m_impl->m_hwnd = hwnd;

                // WM_NCCREATE は継続可否に影響するので、ここは素直に DefWindowProc を返す
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
        // コンストラクタの実装
        // COM初期化
        const HRESULT hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        m_impl->m_isComInitialized = SUCCEEDED(hr);
    }
    WinApp::~WinApp()
    {
        if (m_impl && m_impl->m_isComInitialized)
        {
            // COM終了処理
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

        auto rollbackTimePeriod = [this]()
            {
                if (m_impl->m_isTimePeriodSet)
                {
                    ::timeEndPeriod(1);
                    m_impl->m_isTimePeriodSet = false;
                }
            };

        m_impl->m_width = w;
        m_impl->m_height = h;
        HINSTANCE hInstance = ::GetModuleHandleW(nullptr);

        // 3) ウィンドウクラスを登録する
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

        // 4) クライアントサイズを維持するようウィンドウ矩形を調整して生成する
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
            this // WM_NCCREATEで拾う
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

        m_impl->m_hwnd = hwnd;

        return Result::ok();
    }
    Result WinApp::destroy_window()
    {
        // ウィンドウを破棄

        if (m_impl->m_isTimePeriodSet)
        {
            ::timeEndPeriod(1);
            m_impl->m_isTimePeriodSet = false;
        }

        if (m_impl && m_impl->m_hwnd)
        {
            ::DestroyWindow(m_impl->m_hwnd);
            m_impl->m_hwnd = nullptr;
        }
        return Result::ok();
    }
    Result WinApp::show_window(bool isMaximized)
    {
        // ウィンドウを表示
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
                return false; // 終了メッセージが来たらfalseを返す
            }
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
        return true;
    }
    NativeWindowHandle WinApp::get_native_window_handle() const noexcept
    {
        // 1) HWND を透過ハンドルへ変換して返す
        return reinterpret_cast<NativeWindowHandle>(m_impl->m_hwnd);
    }
    uint32_t WinApp::get_window_width() const noexcept
    {
        return m_impl->m_width;
    }
    uint32_t WinApp::get_window_height() const noexcept
    {
        return m_impl->m_height;
    }
    uint64_t WinApp::register_message_handler(MessageHandler handler)
    {
        // 1) 空ハンドラは登録せず無効IDを返して呼び出し側へ通知する。
        if (!handler)
        {
            return 0;
        }

        // 2) 以後の解除で参照できる一意IDを採番して保持する。
        const uint64_t handlerId = m_impl->m_nextMessageHandlerId++;
        m_impl->m_messageHandlers.push_back(Impl::MessageHandlerEntry{ handlerId, std::move(handler) });
        return handlerId;
    }
    bool WinApp::unregister_message_handler(uint64_t handlerId)
    {
        // 1) 無効IDは探索せず false を返して誤使用を早期に返す。
        if (handlerId == 0)
        {
            return false;
        }

        // 2) 該当エントリだけ削除し、残りのハンドラ順序は維持する。
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
} // namespace Cue::Platform::Win

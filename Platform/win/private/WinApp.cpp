#include "pch.h"
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
        // Windowsアプリケーションに関する実装の詳細をここに記述
        HWND m_hwnd = nullptr;
        uint32_t m_width = 0;
        uint32_t m_height = 0;
        bool m_isComInitialized = false;
        bool m_isTimePeriodSet = false;
        bool m_shouldClose = false;

        LRESULT on_message(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
        {
            switch (msg)
            {
            case WM_CLOSE:
                // 1) 破棄はエンジン側の終了手順に委譲する
                // 2) メインループを抜けるためのフラグを立てる
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
        if (m_impl && (m_impl->m_hwnd || m_impl->m_isTimePeriodSet))
        {
            destroy_window();
        }
        if (m_impl && m_impl->m_isComInitialized)
        {
            // COM終了処理
            ::CoUninitialize();
        }
    }
    Core::Result WinApp::create_window(uint32_t w, uint32_t h)
    {
        // 1) スレッド前提の初期化に失敗している場合は継続できない
        if (!m_impl->m_isComInitialized)
        {
            return Core::Result::fail(
                Core::Facility::Platform,
                Core::Code::CreationFailed,
                Core::Severity::Fatal,
                0, "Failed to initialize COM library for WinApp.");
        }

        // 2) 高精度タイマが必要なのでタイムスライスを要求する
        if (!m_impl->m_isTimePeriodSet)
        {
            const MMRESULT timeResult = ::timeBeginPeriod(1);
            if (timeResult != TIMERR_NOERROR)
            {
                return Core::Result::fail(
                    Core::Facility::Platform,
                    Core::Code::CreationFailed,
                    Core::Severity::Fatal,
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

        constexpr wchar_t k_className[] = L"CueWindowClass";
        HINSTANCE hInstance = ::GetModuleHandleW(nullptr);

        // 3) ウィンドウクラスを登録する
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &Impl::window_proc;
        wc.lpszClassName = k_className;
        wc.hInstance = hInstance;
        wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);

        if (!::RegisterClassExW(&wc))
        {
            const DWORD err = ::GetLastError();
            if (err != ERROR_CLASS_ALREADY_EXISTS)
            {
                rollbackTimePeriod();
                return Core::Result::fail(
                    Core::Facility::Platform,
                    Core::Code::CreationFailed,
                    Core::Severity::Fatal,
                    0, "Failed to register window class.");
            }
        }

        // 4) クライアントサイズを維持するために調整して生成する
        RECT rc{ 0, 0, static_cast<LONG>(w), static_cast<LONG>(h) };
        ::AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

        HWND hwnd = ::CreateWindowExW(
            0,
            k_className,
            L"Cue Engine",
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
            return Core::Result::fail(
                Core::Facility::Platform,
                Core::Code::CreationFailed,
                Core::Severity::Fatal,
                0, "Failed to create window.");
        }

        m_impl->m_hwnd = hwnd;

        return Core::Result::ok();
    }
    Core::Result WinApp::destroy_window()
    {
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
    }
    Core::Result WinApp::show_window(bool isMaximized)
    {
        // ウィンドウを表示
        ::ShowWindow(m_impl->m_hwnd, isMaximized ? SW_MAXIMIZE : SW_SHOW);
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
} // namespace Cue::Platform::Win

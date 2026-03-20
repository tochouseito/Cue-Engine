#include "WinApp.h"

namespace
{
    constexpr uint32_t minimum_window_width = 100;
    constexpr uint32_t minimum_window_height = 100;
}

namespace Cue::PAL::Win
{
    WinApp::WinApp()
    {}
    WinApp::~WinApp()
    {}
    Result WinApp::create_window(uint32_t width, uint32_t height, const wchar_t* className, const wchar_t* titleName)
    {
        // 引数チェック
        if (!className || !titleName)
        {
            return Result::fail(
                Code::InvalidArgument, Severity::Error,
                "Class name and title name must not be null.");
        }

        // サイズチェック
        if( width < minimum_window_width || height < minimum_window_height)
        {
            return Result::fail(
                Code::InvalidArgument, Severity::Error,
                "Window size is too small.");
        }

        // module handle を取得
        HINSTANCE hInstance = ::GetModuleHandleW(nullptr);

        // ウィンドウクラスの登録
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &window_proc;
        wc.lpszClassName = className;
        wc.hInstance = hInstance;
        wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);

        if (!::RegisterClassExW(&wc))
        {
            return Result::fail(
                Code::CreateFailed, Severity::Fatal,
                "Failed to register window class.");
        }

        // クライアントサイズ維持でウィンドウの作成
        RECT rc = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
        ::AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
        m_hwnd = ::CreateWindowExW(
            0, className, titleName,
            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
            rc.right - rc.left, rc.bottom - rc.top,
            nullptr, nullptr, hInstance, this);

        if(!m_hwnd)
        {
            return Result::fail(
                Code::CreateFailed, Severity::Fatal,
                "Failed to create window.");
        }

        return Result::ok();
    }
    void WinApp::destroy_window()
    {
        // ウィンドウが存在する場合は破棄する
        if(m_hwnd)
        {
            ::DestroyWindow(m_hwnd);
            m_hwnd = nullptr;
        }
    }
    Result WinApp::show_window(ShowWindowFlag flag)
    {
        if (!m_hwnd)
        {
            return Result::fail(
                Code::InvalidArgument, Severity::Error,
                "Window handle is null.");
        }

        switch (flag)
        {
        case Cue::PAL::Win::ShowWindowFlag::Normal:
            ::ShowWindow(m_hwnd, SW_SHOW);
            return Result::ok();
            break;
        case Cue::PAL::Win::ShowWindowFlag::Maximized:
            ::ShowWindow(m_hwnd, SW_MAXIMIZE);
            return Result::ok();
            break;
        default:
            ::ShowWindow(m_hwnd, SW_SHOW);
            return Result::ok();
            break;
        }
    }
    PlatformMessage WinApp::pump_message()
    {
        MSG msg{};
        // キューを掃き出して終了メッセージを検知する
        // 閉じる要求が来ていればループを終了する
        if (m_shouldClose)
        {
            return PlatformMessage::Quit;
        }
        while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                return PlatformMessage::Quit; // 終了メッセージ検出
            }
            // メッセージ変換と配送
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
        return PlatformMessage::None;
    }

    // クライアントサイズの取得
    [[nodiscard]]
    WinApp::WindowSize WinApp::get_client_size() const noexcept
    {
        WindowSize size{};

        // 1) RECT を取得
        RECT rc{};
        if (::GetClientRect(m_hwnd, &rc) == FALSE)
        {
            return size;
        }

        // 2) 幅と高さを計算
        size.width = static_cast<uint32_t>(rc.right - rc.left);
        size.height = static_cast<uint32_t>(rc.bottom - rc.top);

        return size;
    }

    // ウィンドウサイズの取得
    [[nodiscard]]
    WinApp::WindowSize WinApp::get_window_size() const noexcept
    {
        WindowSize size{};
        // 1) RECT を取得
        RECT rc{};
        if (::GetWindowRect(m_hwnd, &rc) == FALSE)
        {
            return size;
        }
        // 2) 幅と高さを計算
        size.width = static_cast<uint32_t>(rc.right - rc.left);
        size.height = static_cast<uint32_t>(rc.bottom - rc.top);
        return size;
    }

    // メッセージハンドラ
    LRESULT WinApp::on_message(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
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
            return 0;

        case WM_DESTROY:
            // 1) quit メッセージ送出
            ::PostQuitMessage(0);
            return 0;
        }
        return ::DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    // ウィンドウプロシージャ
    LRESULT WinApp::window_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        WinApp* self = nullptr;

        if (msg == WM_NCCREATE)
        {
            auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<WinApp*>(cs->lpCreateParams);

            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->m_hwnd = hwnd;

            // wm_nccreate は既定処理へ移譲
            return ::DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        self = reinterpret_cast<WinApp*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (self)
        {
            return self->on_message(hwnd, msg, wParam, lParam);
        }

        return ::DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

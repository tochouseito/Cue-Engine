#include "WinApp.h"

namespace
{
    constexpr uint32_t k_minimumWindowWidth = 100;
    constexpr uint32_t k_minimumWindowHeight = 100;
}

namespace Cue::PAL::Win
{
    WinApp::WinApp()
    {}

    WinApp::~WinApp()
    {}

    Result WinApp::create_window(uint32_t a_width, uint32_t a_height, const wchar_t* a_className, const wchar_t* a_titleName)
    {
        // 引数チェック
        if (!a_className || !a_titleName)
        {
            return Result::fail(
                Code::InvalidArgument, Severity::Error,
                "Class name and title name must not be null.");
        }

        // サイズチェック
        if ((a_width < k_minimumWindowWidth) || (a_height < k_minimumWindowHeight))
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
        wc.lpszClassName = a_className;
        wc.hInstance = hInstance;
        wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);

        if (!::RegisterClassExW(&wc))
        {
            return Result::fail(
                Code::CreateFailed, Severity::Fatal,
                "Failed to register window class.");
        }

        // クライアントサイズ維持でウィンドウの作成
        RECT rc = { 0, 0, static_cast<LONG>(a_width), static_cast<LONG>(a_height) };
        ::AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
        m_hwnd = ::CreateWindowExW(
            0, a_className, a_titleName,
            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
            rc.right - rc.left, rc.bottom - rc.top,
            nullptr, nullptr, hInstance, this);

        if (!m_hwnd)
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
        if (m_hwnd)
        {
            ::DestroyWindow(m_hwnd);
            m_hwnd = nullptr;
        }
    }

    Result WinApp::show_window(ShowWindowFlag a_flag)
    {
        if (!m_hwnd)
        {
            return Result::fail(
                Code::InvalidArgument, Severity::Error,
                "Window handle is null.");
        }

        switch (a_flag)
        {
        case Cue::PAL::Win::ShowWindowFlag::Normal:
            ::ShowWindow(m_hwnd, SW_SHOW);
            return Result::ok();
        case Cue::PAL::Win::ShowWindowFlag::Maximized:
            ::ShowWindow(m_hwnd, SW_MAXIMIZE);
            return Result::ok();
        default:
            ::ShowWindow(m_hwnd, SW_SHOW);
            return Result::ok();
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

    uint64_t WinApp::register_message_handler(messageHandler a_handler)
    {
        // 空ハンドラは無効 id を返却
        if (!a_handler)
        {
            return 0;
        }

        // 一意 id を採番して保持
        const uint64_t handlerId = m_nextMessageHandlerId++;
        m_messageHandlers.push_back(MessageHandlerEntry{ handlerId, std::move(a_handler) });
        return handlerId;
    }

    bool WinApp::unregister_message_handler(uint64_t handlerId)
    {
        // 無効 id は即時失敗
        if (handlerId == 0)
        {
            return false;
        }

        // 該当ハンドラだけ削除
        for (auto it = m_messageHandlers.begin(); it != m_messageHandlers.end(); ++it)
        {
            if (it->m_id != handlerId)
            {
                continue;
            }

            m_messageHandlers.erase(it);
            return true;
        }

        return false;
    }

    // メッセージハンドラ
    LRESULT WinApp::on_message(HWND a_hwnd, UINT a_message, WPARAM a_wParam, LPARAM a_lParam)
    {
        // 1) 外部登録ハンドラを先に評価
        for (const MessageHandlerEntry& entry : m_messageHandlers)
        {
            if (!entry.m_handler)
            {
                continue;
            }

            LRESULT handledResult = 0;
            const bool isHandled = entry.m_handler(a_hwnd, a_message, a_wParam, a_lParam, handledResult);
            if (isHandled)
            {
                return handledResult;
            }
        }

        // 2) 未処理メッセージを既定処理へ移譲
        switch (a_message)
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
        return ::DefWindowProcW(a_hwnd, a_message, a_wParam, a_lParam);
    }

    // ウィンドウプロシージャ
    LRESULT WinApp::window_proc(HWND a_hwnd, UINT a_message, WPARAM a_wParam, LPARAM a_lParam)
    {
        WinApp* self = nullptr;

        if (a_message == WM_NCCREATE)
        {
            auto cs = reinterpret_cast<CREATESTRUCTW*>(a_lParam);
            self = static_cast<WinApp*>(cs->lpCreateParams);

            ::SetWindowLongPtrW(a_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->m_hwnd = a_hwnd;

            // wm_nccreate は既定処理へ移譲
            return ::DefWindowProcW(a_hwnd, a_message, a_wParam, a_lParam);
        }

        self = reinterpret_cast<WinApp*>(::GetWindowLongPtrW(a_hwnd, GWLP_USERDATA));
        if (self)
        {
            return self->on_message(a_hwnd, a_message, a_wParam, a_lParam);
        }

        return ::DefWindowProcW(a_hwnd, a_message, a_wParam, a_lParam);
    }
}

#include "WinApp.h"
#include <CueAssert.h>
#include <PlatformCommands.h>

// === PAL includes ===
#include "../ConvertUTF.h"

// === Windows API includes ===
#include <shellapi.h>

namespace
{
    constexpr uint32_t k_minimumWindowWidth = 100;
    constexpr uint32_t k_minimumWindowHeight = 100;
    constexpr int k_defaultAppIconResourceId = 101;
    constexpr DWORD k_windowStyle = WS_OVERLAPPEDWINDOW;
    constexpr DWORD k_windowExStyle = 0;

    using adjustWindowRectExForDpiFunc =
        BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
    using getDpiForSystemFunc = UINT(WINAPI*)();

    HICON load_app_icon(HINSTANCE a_instance, int a_width, int a_height) noexcept
    {
        HICON icon = reinterpret_cast<HICON>(::LoadImageW(
            a_instance,
            MAKEINTRESOURCEW(k_defaultAppIconResourceId),
            IMAGE_ICON,
            a_width,
            a_height,
            LR_DEFAULTCOLOR | LR_SHARED));
        if (icon != nullptr)
        {
            return icon;
        }

        return ::LoadIconW(nullptr, IDI_APPLICATION);
    }

    UINT system_dpi() noexcept
    {
        HMODULE user32Module = ::GetModuleHandleW(L"user32.dll");
        if (user32Module == nullptr)
        {
            return USER_DEFAULT_SCREEN_DPI;
        }

        auto getDpiForSystem =
            reinterpret_cast<getDpiForSystemFunc>(
                ::GetProcAddress(user32Module, "GetDpiForSystem"));
        if (getDpiForSystem == nullptr)
        {
            return USER_DEFAULT_SCREEN_DPI;
        }

        return getDpiForSystem();
    }

    void adjust_window_rect_for_client_size(RECT& a_rect) noexcept
    {
        HMODULE user32Module = ::GetModuleHandleW(L"user32.dll");
        if (user32Module != nullptr)
        {
            auto adjustWindowRectExForDpi =
                reinterpret_cast<adjustWindowRectExForDpiFunc>(
                    ::GetProcAddress(
                        user32Module,
                        "AdjustWindowRectExForDpi"));
            if (adjustWindowRectExForDpi != nullptr)
            {
                if (adjustWindowRectExForDpi(
                        &a_rect,
                        k_windowStyle,
                        FALSE,
                        k_windowExStyle,
                        system_dpi()))
                {
                    return;
                }
            }
        }

        (void)::AdjustWindowRectEx(
            &a_rect,
            k_windowStyle,
            FALSE,
            k_windowExStyle);
    }
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
        wc.hIcon = load_app_icon(hInstance, ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON));
        wc.hIconSm = load_app_icon(hInstance, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON));

        if (!::RegisterClassExW(&wc))
        {
            return Result::fail(
                Code::CreateFailed, Severity::Fatal,
                "Failed to register window class.");
        }

        // クライアントサイズ維持でウィンドウの作成
        RECT rc = { 0, 0, static_cast<LONG>(a_width), static_cast<LONG>(a_height) };
        adjust_window_rect_for_client_size(rc);
        m_hwnd = ::CreateWindowExW(
            k_windowExStyle, a_className, a_titleName,
            k_windowStyle, CW_USEDEFAULT, CW_USEDEFAULT,
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
            ::DragAcceptFiles(m_hwnd, FALSE);
            m_isDragDropEnabled = false;
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
            if (it->id != handlerId)
            {
                continue;
            }

            m_messageHandlers.erase(it);
            return true;
        }

        return false;
    }

    bool WinApp::is_window_focused() const noexcept
    {
        if (m_hwnd == nullptr)
        {
            return false;
        }

        return ::GetForegroundWindow() == m_hwnd;
    }

    Result WinApp::set_drag_drop_enabled(bool a_isEnabled)
    {
        if (m_hwnd == nullptr)
        {
            return Result::fail(
                Code::InvalidState,
                Severity::Error,
                "Window handle is null.");
        }

        ::DragAcceptFiles(m_hwnd, a_isEnabled ? TRUE : FALSE);
        m_isDragDropEnabled = a_isEnabled;
        return Result::ok();
    }

    bool WinApp::consume_dropped_files(
        std::vector<std::string>& a_outPaths) noexcept
    {
        if (m_droppedFiles.empty())
        {
            return false;
        }

        for (std::string& path : m_droppedFiles)
        {
            a_outPaths.push_back(std::move(path));
        }
        m_droppedFiles.clear();
        return true;
    }

    // メッセージハンドラ
    LRESULT WinApp::on_message(HWND a_hwnd, UINT a_message, WPARAM a_wParam, LPARAM a_lParam)
    {
        // Platform のサイズ変更は描画解像度の基準なので、外部ハンドラより先に確定する。
        switch (a_message)
        {
        case WM_SIZE:
        {
            if (m_platformBridge == nullptr || a_wParam == SIZE_MINIMIZED)
            {
                return 0;
            }

            const uint32_t resizedWidth = static_cast<uint32_t>(LOWORD(a_lParam));
            const uint32_t resizedHeight = static_cast<uint32_t>(HIWORD(a_lParam));
            if (resizedWidth == 0 || resizedHeight == 0)
            {
                return 0;
            }

            Result result = m_platformBridge->submit_command(
                std::make_unique<PAL::ResizeWindowCommand>(
                    resizedWidth, resizedHeight));
            if (!result)
            {
                CUE_ASSERTF(false,
                    "Failed to submit resize command: %s (code: %s, severity: %s) at "
                    "%s:%u in function %s",
                    result.message.data(), Cue::to_string(result.code),
                    Cue::to_string(result.severity), result.file, result.line,
                    result.function);
            }

            return 0;
        }

        case WM_DPICHANGED:
        {
            RECT* suggestedRect = reinterpret_cast<RECT*>(a_lParam);
            if (suggestedRect != nullptr)
            {
                ::SetWindowPos(
                    a_hwnd,
                    nullptr,
                    suggestedRect->left,
                    suggestedRect->top,
                    suggestedRect->right - suggestedRect->left,
                    suggestedRect->bottom - suggestedRect->top,
                    SWP_NOZORDER | SWP_NOACTIVATE);
            }

            if (m_platformBridge == nullptr)
            {
                return 0;
            }

            const WindowSize clientSize = get_client_size();
            if (clientSize.width == 0 || clientSize.height == 0)
            {
                return 0;
            }

            Result result = m_platformBridge->submit_command(
                std::make_unique<PAL::ResizeWindowCommand>(
                    clientSize.width, clientSize.height));
            if (!result)
            {
                CUE_ASSERTF(false,
                    "Failed to submit dpi resize command: %s (code: %s, severity: %s) at "
                    "%s:%u in function %s",
                    result.message.data(), Cue::to_string(result.code),
                    Cue::to_string(result.severity), result.file, result.line,
                    result.function);
            }

            return 0;
        }

        default:
            break;
        }

        // 外部登録ハンドラを先に評価
        for (const MessageHandlerEntry& entry : m_messageHandlers)
        {
            if (!entry.handler)
            {
                continue;
            }

            LRESULT handledResult = 0;
            const bool isHandled = entry.handler(a_hwnd, a_message, a_wParam, a_lParam, handledResult);
            if (isHandled)
            {
                return handledResult;
            }
        }

        // 未処理メッセージを既定処理へ移譲
        switch (a_message)
        {
        case WM_CLOSE:
            // 破棄は engine 終了手順へ移譲
            // メインループ終了フラグ設定
            m_shouldClose = true;
            return 0;

        case WM_GETMINMAXINFO:
        {
            // クライアントサイズ変更直前
            MINMAXINFO* pMinMaxInfo = reinterpret_cast<MINMAXINFO*>(a_lParam);
            pMinMaxInfo->ptMinTrackSize.x = k_minimumWindowWidth;
            pMinMaxInfo->ptMinTrackSize.y = k_minimumWindowHeight;
            return 0;
        }

        case WM_DROPFILES:
        {
            HDROP dropHandle = reinterpret_cast<HDROP>(a_wParam);
            if (!m_isDragDropEnabled)
            {
                ::DragFinish(dropHandle);
                return 0;
            }

            const UINT droppedFileCount =
                ::DragQueryFileW(dropHandle, 0xFFFFFFFF, nullptr, 0);
            for (UINT fileIndex = 0; fileIndex < droppedFileCount; ++fileIndex)
            {
                const UINT pathLength =
                    ::DragQueryFileW(dropHandle, fileIndex, nullptr, 0);
                if (pathLength == 0)
                {
                    continue;
                }

                std::wstring widePath(
                    static_cast<size_t>(pathLength) + 1u,
                    L'\0');
                const UINT written = ::DragQueryFileW(
                    dropHandle,
                    fileIndex,
                    widePath.data(),
                    pathLength + 1);
                if (written == 0)
                {
                    continue;
                }
                widePath.resize(written);

                std::string path{};
                const Result convertResult = wide_to_utf8(widePath, &path);
                if (convertResult)
                {
                    m_droppedFiles.push_back(std::move(path));
                }
            }

            ::DragFinish(dropHandle);
            return 0;
        }

        case WM_DESTROY:
            // quit メッセージ送出
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

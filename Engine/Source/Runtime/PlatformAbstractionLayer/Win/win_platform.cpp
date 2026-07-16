#include "win_platform.h"

// === win_platform includes ===
#include "ConvertHresult.h"
#include "ConvertUTF.h"

namespace
{
    void dpi_awareness()
    {
        HMODULE user32Module = ::GetModuleHandleW(L"user32.dll");
        if (user32Module == nullptr)
        {
            return;
        }

        using setProcessDpiAwarenessContextFunc =
            BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
        auto setProcessDpiAwarenessContext =
            reinterpret_cast<setProcessDpiAwarenessContextFunc>(
                ::GetProcAddress(
                    user32Module,
                    "SetProcessDpiAwarenessContext"));

        //  可能なら Per - Monitor DPI Aware v2 を使い、だめなら
        //  旧 Per - Monitor、最後に Vista 以降の SetProcessDPIAware() に フォールバックする
        if (setProcessDpiAwarenessContext != nullptr)
        {
            if (setProcessDpiAwarenessContext(
                DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
            {
                return;
            }
            if (setProcessDpiAwarenessContext(
                DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE))
            {
                return;
            }
        }

        (void)::SetProcessDPIAware();
    }
}

namespace Cue::PAL::Win
{
    WinPlatform::WinPlatform()
    {
        m_app = std::make_unique<WinApp>();
        m_fileSystem = std::make_unique<WinFileSystem>();
        m_threadFactory = std::make_unique<WinThreadFactory>();
        m_clock = std::make_unique<WinQpcClock>();
        m_waiter = std::make_unique<WinWaiter>(*m_clock.get());
        m_cpuProfiler = std::make_unique<CPUProfiler>();
    }
    WinPlatform::~WinPlatform()
    {
    }
    Result WinPlatform::initialize(const PlatformSetupInfo& a_info)
    {
        // 高 DPI 環境や複数モニタ環境で UI / ウィンドウサイズ / マウス座標など
        // が OS に勝手に拡大縮小されるのを避けるため、アプリケーション単位で DPI 対応する
        dpi_awareness();

        // COM を初期化する
        const HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (success(convert_hresult_code(result)))
        {
            m_isComInitialized = true;
        }
        else
        {
            return Result::fail(
                convert_hresult_code(result), Severity::Fatal,
                "Failed to initialize COM.");
        }

        // タイムスライスの精度を上げる
        ::timeBeginPeriod(1);

        // ウィンドウクラス名とタイトルを UTF-8 で受け取るため、Windows API で使うワイド文字列へ変換する
        std::wstring wideClassName;
        // UTF-8 からワイド文字への変換
        Result resultValue = utf8_to_wide(a_info.className, &wideClassName);
        if (!resultValue)
        {
            return Result::fail(
                resultValue.code, Severity::Fatal,
                "Failed to convert window class name from UTF-8 to wide char.");
        }
        std::wstring wideTitle;
        resultValue = utf8_to_wide(a_info.title, &wideTitle);
        if (!resultValue)
        {
            return Result::fail(
                resultValue.code, Severity::Fatal,
                "Failed to convert window title from UTF-8 to wide char.");
        }

        // ウィンドウの作成
        resultValue = m_app->create_window(a_info.width, a_info.height, wideClassName.c_str(), wideTitle.c_str());
        if (!resultValue)
        {
            return Result::fail(
                resultValue.code, Severity::Fatal,
                "Failed to create window.");
        }

        m_app->set_command_bridge(m_commandBridge);

        return Result::ok();
    }

    Result WinPlatform::start()
    {
        // ウィンドウを表示する
        return m_app->show_window();
    }

    Result WinPlatform::shutdown()
    {
        // ウィンドウを破棄する
        m_app->destroy_window();
        // COM をクリーンアップする
        if (m_isComInitialized)
        {
            ::CoUninitialize();
            m_isComInitialized = false;
        }
        // タイムスライスの精度を元に戻す
        ::timeEndPeriod(1);

        return Result::ok();
    }

    Result WinPlatform::begin_frame()
    {
        return Result::ok();
    }

    Result WinPlatform::end_frame()
    {
        return Result::ok();
    }

    PlatformMessage WinPlatform::poll_message()
    {
        return m_app->pump_message();
    }
}

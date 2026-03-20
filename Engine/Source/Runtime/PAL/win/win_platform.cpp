#include "win_platform.h"
#include "ConvertHresult.h"
#include "ConvertUTF.h"

namespace Cue::PAL
{
    std::unique_ptr<IPlatform> create_platform()
    {
        return std::make_unique<Win::WinPlatform>();
    }
}

namespace Cue::PAL::Win
{
    WinPlatform::WinPlatform()
    {
        m_app = std::make_unique<WinApp>();
    }
    WinPlatform::~WinPlatform()
    {
        
    }
    Result WinPlatform::initialize(const platform_setup_info & info)
    {
        // COM を初期化する
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (success(convert_hresult_code(hr)))
        {
            m_isComInitialized = true;
        }
        else
        {
            return Result::fail(
                convert_hresult_code(hr), Severity::Fatal,
                "Failed to initialize COM.");
        }

        // タイムスライスの精度を上げる
        ::timeBeginPeriod(1);

        // ウィンドウを作成する
        std::wstring wideClassName;
        Result r = utf8_to_wide(info.className, &wideClassName);
        if(!r)
        {
            return Result::fail(
                r.code, Severity::Fatal,
                "Failed to convert window class name from UTF-8 to wide char.");
        }
        std::wstring wideTitle;
        r = utf8_to_wide(info.title, &wideTitle);
        if(!r)
        {
            return Result::fail(
                r.code, Severity::Fatal,
                "Failed to convert window title from UTF-8 to wide char.");
        }
        r = m_app->create_window(info.width, info.height, wideClassName.c_str(), wideTitle.c_str());
        if(!r)
        {
            return Result::fail(
                r.code, Severity::Fatal,
                "Failed to create window.");
        }

        return Result::ok();
    }
    Result WinPlatform::start()
    {
        return m_app->show_window();
    }
    Result WinPlatform::shutdown()
    {
        // COM を終了する
        if (m_isComInitialized)
        {
            CoUninitialize();
            m_isComInitialized = false;
        }

        // タイムスライスの精度を元に戻す
        ::timeEndPeriod(1);

        return Result();
    }
    Result WinPlatform::begin_frame()
    {
        return Result();
    }
    Result WinPlatform::end_frame()
    {
        return Result();
    }
    PlatformMessage WinPlatform::poll_message()
    {
        return m_app->pump_message();
    }
}

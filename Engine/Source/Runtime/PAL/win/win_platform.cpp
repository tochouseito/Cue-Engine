#include "win_platform.h"
#include "ConvertHresult.h"

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

        return Result::ok();
    }
    Result WinPlatform::start()
    {
        return Result();
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
        return PlatformMessage::None;
    }
}

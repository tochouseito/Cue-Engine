#include "win_platform.h"

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

    Result WinPlatform::initialize(const PlatformSetupInfo& a_info)
    {
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

        // ウィンドウを作成する
        std::wstring wideClassName;
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
        resultValue = m_app->create_window(a_info.width, a_info.height, wideClassName.c_str(), wideTitle.c_str());
        if (!resultValue)
        {
            return Result::fail(
                resultValue.code, Severity::Fatal,
                "Failed to create window.");
        }

        // スレッドファクトリ、クロック、ウェイタを生成する
        m_threadFactory = std::make_unique<WinThreadFactory>();
        m_clock = std::make_unique<WinQpcClock>();
        m_waiter = std::make_unique<WinWaiter>(*m_clock.get());

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
            ::CoUninitialize();
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
    uint64_t WinPlatform::register_message_handler(MessageHandler handler)
    {
        // 型変換だけ行って win app へ移譲
        if (!handler)
        {
            return 0;
        }

        return m_app->register_message_handler(handler);
    }
    bool WinPlatform::unregister_message_handler(uint64_t handlerId)
    {
        // win app の解除結果を返却
        return m_app->unregister_message_handler(handlerId);
    }
}

#include "WinPlatform.h"

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
        m_keyboard = std::make_unique<WinKeyboard>();
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

        resultValue = m_keyboard->initialize(::GetModuleHandleW(nullptr),
            m_app->get_window_handle());
        if (!resultValue)
        {
            return Result::fail(
                resultValue.code, Severity::Fatal,
                "Failed to initialize keyboard input.");
        }

        resultValue = m_inputManager.initialize(m_keyboard.get());
        if (!resultValue)
        {
            return Result::fail(
                resultValue.code, Severity::Fatal,
                "Failed to initialize input manager.");
        }

        // スレッドファクトリ、クロック、ウェイタを生成する
        m_fileSystem = std::make_unique<WinFileSystem>();
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
        m_inputManager.shutdown();

        if (m_keyboard != nullptr)
        {
            m_keyboard->shutdown();
        }

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
        return m_inputManager.begin_frame();
    }

    Result WinPlatform::end_frame()
    {
        return Result();
    }

    PlatformMessage WinPlatform::poll_message()
    {
        return m_app->pump_message();
    }

    bool WinPlatform::is_window_focused() const noexcept
    {
        return m_app != nullptr && m_app->is_window_focused();
    }

    Result WinPlatform::set_drag_drop_enabled(bool a_isEnabled)
    {
        if (m_app == nullptr)
        {
            return Result::fail(
                Code::InvalidState,
                Severity::Error,
                "WinApp is not initialized.");
        }

        return m_app->set_drag_drop_enabled(a_isEnabled);
    }

    bool WinPlatform::is_drag_drop_enabled() const noexcept
    {
        return m_app != nullptr && m_app->is_drag_drop_enabled();
    }

    bool WinPlatform::consume_dropped_files(
        std::vector<std::string>& a_outPaths) noexcept
    {
        return m_app != nullptr && m_app->consume_dropped_files(a_outPaths);
    }

    uint64_t WinPlatform::register_message_handler(WinApp::messageHandler a_handler)
    {
        // WinApp と同じ契約で受け取るため、そのまま移譲
        if (!a_handler)
        {
            return 0;
        }

        return m_app->register_message_handler(std::move(a_handler));
    }
    bool WinPlatform::unregister_message_handler(uint64_t handlerId)
    {
        // win app の解除結果を返却
        return m_app->unregister_message_handler(handlerId);
    }
    void WinPlatform::set_platform_bridge(Core::CQRS::Bridge* a_bridge) noexcept
    {
        if (m_app == nullptr)
        {
            return;
        }

        m_app->set_platform_bridge(a_bridge);
    }
}

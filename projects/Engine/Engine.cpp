#include "Engine.h"

// === Core includes ===
#include <Logger.h>
#include <IO/IFileSystem.h>
#include <nlohmann/json.hpp>

// === C++ Standard Library ===
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace
{
    [[nodiscard]] bool try_parse_controller_mode(uint32_t numericMode, Cue::ControllerMode& outMode) noexcept
    {
        // 1) 保存形式を固定するため数値指定のみを列挙変換する
        switch (numericMode)
        {
        case 0:
            outMode = Cue::ControllerMode::Fixed;
            return true;
        case 1:
            outMode = Cue::ControllerMode::Mailbox;
            return true;
        case 2:
            outMode = Cue::ControllerMode::Backpressure;
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] bool try_get_u32(const nlohmann::json& object, const char* key, uint32_t& outValue) noexcept
    {
        // 1) 設定ファイル破損時に安全に失敗できるように型と範囲を明示チェックする
        const auto it = object.find(key);
        if (it == object.end())
        {
            return false;
        }

        if (it->is_number_unsigned())
        {
            const uint64_t value = it->get<uint64_t>();
            if (value > static_cast<uint64_t>((std::numeric_limits<uint32_t>::max)()))
            {
                return false;
            }

            outValue = static_cast<uint32_t>(value);
            return true;
        }

        if (it->is_number_integer())
        {
            const int64_t value = it->get<int64_t>();
            if (value < 0 || value > static_cast<int64_t>((std::numeric_limits<uint32_t>::max)()))
            {
                return false;
            }

            outValue = static_cast<uint32_t>(value);
            return true;
        }

        return false;
    }

    [[nodiscard]] uint32_t to_numeric(Cue::ControllerMode mode) noexcept
    {
        // 1) 保存形式を安定させるため数値表現を固定する
        switch (mode)
        {
        case Cue::ControllerMode::Fixed:
            return 0;
        case Cue::ControllerMode::Mailbox:
            return 1;
        case Cue::ControllerMode::Backpressure:
            return 2;
        default:
            return 0;
        }
    }
}

namespace Cue
{
    Engine::Engine()
    {
    }

    Engine::~Engine()
    {
    }
    void Engine::initialize(EngineInitInfo& initInfo)
    {
        m_platform = initInfo.platform;

        // 1) Platform の初期化と関連リソースの取得を行う
        m_platform->setup();

        // 2) EngineConfig の読み込み
        load_engine_config(Core::IO::Path{ k_engineConfigPath });

        FrameControllerDesc frameControllerDesc{};
        m_frameController = std::make_unique<FrameController>(
            frameControllerDesc,
            m_platform->get_thread_factory(),
            m_platform->get_clock(),
            m_platform->get_waiter(),
            update(),
            render(),
            present());

        m_platform->start();

        Core::Logger::log(Core::LogSink::debugConsole, "Engine initialized successfully.");
    }
    void Engine::tick()
    {
        m_platform->begin_frame();

        m_frameController->step();
        double fps = m_frameController->frame_counter().fps();
        uint32_t updateIndex = m_frameController->update_index();
        uint32_t renderIndex = m_frameController->render_index();
        uint32_t presentIndex = m_frameController->present_index();
        uint64_t totalFrame = m_frameController->total_frame();
        Core::Logger::log(Core::LogSink::debugConsole, "Frame: {}, FPS: {:.2f}, UpdateIndex: {}, RenderIndex: {}, PresentIndex: {}",
            totalFrame, fps, updateIndex, renderIndex, presentIndex);

        m_platform->end_frame();
    }
    void Engine::shutdown()
    {
        m_frameController.reset();

        // 設定ファイルの保存
        save_engine_config(Core::IO::Path{ k_engineConfigPath });

        m_platform->shutdown();
        Core::Logger::log(Core::LogSink::debugConsole, "Engine shutdown completed.");
    }
    std::function<void(uint64_t, uint32_t)> Engine::update()
    {
        // 1) 更新処理のエントリポイントを返す
        // 2) 実装確定前でもパイプラインを動かすため仮実装にする
        return [this](uint64_t frameNo, uint32_t index)
            {
                (void)frameNo;
                (void)index;
                (void)this;
            };
    }
    std::function<void(uint64_t, uint32_t)> Engine::render()
    {
        // 2) 実装確定前でもパイプラインを動かすため仮実装にする
        return [this](uint64_t frameNo, uint32_t index)
            {
                (void)frameNo;
                (void)index;
                (void)this;
            };
    }
    std::function<void(uint64_t, uint32_t)> Engine::present()
    {
        // 1) Present 処理のエントリポイントを返す
        // 2) 実装確定前でもパイプラインを動かすため仮実装にする
        return [this](uint64_t frameNo, uint32_t index)
            {
                (void)frameNo;
                (void)index;
                (void)this;
            };
    }

    Result Engine::load_engine_config(const Core::IO::Path& configPath)
    {
        // 1) 呼び出し前提を検証して不正状態でのI/Oを防ぐ
        if (m_platform == nullptr)
        {
            return Result::fail(
                Facility::Core,
                Code::InvalidState,
                Severity::Error,
                0,
                "Platform is not set before loading engine config.");
        }
        if (configPath.is_empty())
        {
            return Result::fail(
                Facility::Core,
                Code::InvalidArg,
                Severity::Error,
                0,
                "Engine config path is empty.");
        }

        // 2) ファイルを読み込み、未作成ならデフォルト値を保存して継続する
        Core::IO::IFileSystem& fileSystem = m_platform->get_file_system();
        std::vector<std::byte> bytes{};
        Result readResult = fileSystem.read_all(configPath, &bytes);
        if (!readResult)
        {
            if (readResult.code == Code::NotFound)
            {
                const Result saveResult = save_engine_config(configPath);
                if (!saveResult)
                {
                    return saveResult;
                }

                Core::Logger::log(
                    Core::LogSink::debugConsole,
                    "Engine config not found. Created default config: {}",
                    configPath.utf8());
                return Result::ok();
            }
            return readResult;
        }

        // 3) JSON を解析してから検証済みの値だけを本体へ反映する
        std::string text{};
        if (!bytes.empty())
        {
            text.assign(
                reinterpret_cast<const char*>(bytes.data()),
                reinterpret_cast<const char*>(bytes.data()) + bytes.size());
        }
        const nlohmann::json jsonRoot = nlohmann::json::parse(text, nullptr, false);
        if (jsonRoot.is_discarded() || !jsonRoot.is_object())
        {
            return Result::fail(
                Facility::Core,
                Code::InvalidArg,
                Severity::Error,
                0,
                "Engine config JSON is invalid.");
        }

        EngineConfig parsedConfig = m_engineConfig;

        uint32_t bufferCount = 0;
        if (!try_get_u32(jsonRoot, "buffer_count", bufferCount) || bufferCount == 0)
        {
            return Result::fail(
                Facility::Core,
                Code::InvalidArg,
                Severity::Error,
                0,
                "Engine config buffer_count is invalid.");
        }
        parsedConfig.m_bufferCount = bufferCount;

        uint32_t maxFps = 0;
        if (!try_get_u32(jsonRoot, "max_fps", maxFps) || maxFps == 0)
        {
            return Result::fail(
                Facility::Core,
                Code::InvalidArg,
                Severity::Error,
                0,
                "Engine config max_fps is invalid.");
        }
        parsedConfig.m_maxFps = maxFps;

        uint32_t numericMode = 0;
        if (!try_get_u32(jsonRoot, "mode", numericMode))
        {
            return Result::fail(
                Facility::Core,
                Code::InvalidArg,
                Severity::Error,
                0,
                "Engine config mode is invalid.");
        }
        if (!try_parse_controller_mode(numericMode, parsedConfig.m_mode))
        {
            return Result::fail(
                Facility::Core,
                Code::InvalidArg,
                Severity::Error,
                0,
                "Engine config mode is invalid.");
        }

        // 4) すべて解釈できた時だけ反映して中途半端な状態を避ける
        m_engineConfig = parsedConfig;
        return Result::ok();
    }

    Result Engine::save_engine_config(const Core::IO::Path& configPath)
    {
        // 1) 呼び出し前提を検証して不正状態でのI/Oを防ぐ
        if (m_platform == nullptr)
        {
            return Result::fail(
                Facility::Core,
                Code::InvalidState,
                Severity::Error,
                0,
                "Platform is not set before saving engine config.");
        }
        if (configPath.is_empty())
        {
            return Result::fail(
                Facility::Core,
                Code::InvalidArg,
                Severity::Error,
                0,
                "Engine config path is empty.");
        }

        // 2) JSON で保存してロード側と形式を一致させる
        nlohmann::json jsonRoot = nlohmann::json::object();
        jsonRoot["buffer_count"] = m_engineConfig.m_bufferCount;
        jsonRoot["max_fps"] = m_engineConfig.m_maxFps;
        jsonRoot["mode"] = to_numeric(m_engineConfig.m_mode);
        const std::string text = jsonRoot.dump(4);

        const std::span<const char> chars{ text.data(), text.size() };
        const std::span<const std::byte> bytes = std::as_bytes(chars);
        Core::IO::IFileSystem& fileSystem = m_platform->get_file_system();
        return fileSystem.write_all(configPath, bytes, true);
    }
} // namespace Cue

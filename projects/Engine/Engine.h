#pragma once
#include <Platform.h>
#include <GraphicsCore.h>
#include <memory>
#include <functional>

#include "Configuration.h"

namespace Cue
{
    struct EngineInitInfo
    {
        // 初期化情報をここに追加
        Platform::IPlatform* platform = nullptr;
        GraphicsCore::Backend* graphicsBackend = nullptr;
    };

    class Engine
    {
    public:
        Engine();
        ~Engine();
        void initialize(EngineInitInfo& initInfo);
        void tick();
        void shutdown();

        FrameController& frame_controller() noexcept
        {
            return *m_frameController;
        }
    private:
        /// @brief 更新処理
        std::function<void(uint64_t, uint32_t)> update();
        /// @brief 描画処理
        std::function<void(uint64_t, uint32_t)> render();
        /// @brief フリップ処理
        std::function<void(uint64_t, uint32_t)> present();
    private:
        Result load_engine_config(const Core::IO::Path& configPath);
        Result save_engine_config(const Core::IO::Path& configPath);
    private:
        Platform::IPlatform* m_platform = nullptr;
        GraphicsCore::Backend* m_graphicsBackend = nullptr;

        EngineConfig m_engineConfig{};

        std::unique_ptr<FrameController> m_frameController = nullptr;
    };
} // namespace Cue

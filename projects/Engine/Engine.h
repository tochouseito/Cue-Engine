#pragma once
#include <Platform.h>
#include <GraphicsCore.h>
#include <memory>

namespace Cue
{
    struct EngineInitInfo
    {
        // 初期化情報をここに追加
        Platform::IPlatform* platform = nullptr;
        Graphics::Backend* graphicsBackend = nullptr;
    };

    class Engine
    {
    public:
        Engine();
        ~Engine();
        void initialize(EngineInitInfo& initInfo);
        void tick();
        void shutdown();
    private:
        Platform::IPlatform* m_platform = nullptr;
        Graphics::Backend* m_graphicsBackend = nullptr;
    };
} // namespace Cue

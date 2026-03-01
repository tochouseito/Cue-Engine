#pragma once
#include "FrameGraph.h"

#include <array>
#include <functional>
#include <string>

namespace Cue::GraphicsCore::Pass
{
    class BackBufferClearPass final : public FrameGraphPass
    {
    public:
        using ExecuteCallback = std::function<void(ICommandContext& cmd, TextureHandle backBufferHandle, const std::array<float, 4>& clearColor)>;

        BackBufferClearPass(
            TextureHandle backBufferHandle,
            std::array<float, 4> clearColor = { 0.0f, 0.0f, 0.0f, 1.0f },
            ExecuteCallback executeCallback = {},
            std::string resourceName = "BackBuffer")
            : m_backBufferHandle(backBufferHandle)
            , m_clearColor(clearColor)
            , m_executeCallback(std::move(executeCallback))
            , m_resourceName(std::move(resourceName))
        {
        }

        ~BackBufferClearPass() override = default;

        [[nodiscard]] const char* name() const override
        {
            return "BackBufferClearPass";
        }

        void setup(FrameGraphBuilder& builder) override
        {
            // 1) スワップチェインのバックバッファを外部テクスチャとして取り込み、初期状態を Present として扱う。
            m_importedBackBufferHandle = builder.import_texture(
                ImportedTextureDesc{
                    m_resourceName,
                    m_backBufferHandle,
                    CommandListType::Graphics,
                    ResourceState::Present,
                    false,
                    ResourceState::Common,
                    false,
                    {} });

            // 2) このPassがレンダーターゲット書き込みを担当し、終了時には Present へ戻す。
            builder.write(m_importedBackBufferHandle, ResourceState::RenderTarget, ResourceState::Present);
        }

        void execute(ICommandContext& cmd) const override
        {
            // 1) 実際のクリア命令は backend 側の具体 API が必要なため、呼び出し側から注入された処理へ委譲する。
            if (m_executeCallback)
            {
                m_executeCallback(cmd, m_backBufferHandle, m_clearColor);
            }
        }

    private:
        TextureHandle m_backBufferHandle{};
        TextureHandle m_importedBackBufferHandle{};
        std::array<float, 4> m_clearColor{};
        ExecuteCallback m_executeCallback;
        std::string m_resourceName;
    };
}

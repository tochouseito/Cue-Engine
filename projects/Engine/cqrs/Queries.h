#pragma once
#include "cqrs.h"
#include <GraphicsCore.h>

namespace Cue::CQRS::Queries
{
    class EngineQueryContext final : public IQueryContext
    {
    public:
        void set_graphics_backend(GraphicsCore::Backend* graphicsBackend) noexcept
        {
            // 1) query が Engine 所有の backend 協力者を使えるよう、初期化時に注入する。
            m_graphicsBackend = graphicsBackend;
        }

        Result resolve_texture_shader_resource_descriptor(
            std::string_view resourceName,
            GraphicsCore::DescriptorHandle& outHandle) const
        {
            // 1) backend 未設定を先に検出し、Editor からの query を明確な失敗として返す。
            outHandle = {};
            if (m_graphicsBackend == nullptr)
            {
                return Result::fail(Facility::Core, Code::InvalidState, Severity::Error, 0, "Graphics backend is not set.");
            }

            // 2) 実際の descriptor 解決は backend 協力者へ委譲し、Editor から実装差分を隠蔽する。
            return m_graphicsBackend->get_texture_shader_resource_descriptor(resourceName, outHandle);
        }

    private:
        GraphicsCore::Backend* m_graphicsBackend = nullptr;
    };

    struct TexturePreviewQueryResult final : public IQueryResult
    {
        GraphicsCore::DescriptorHandle descriptorHandle = {};
    };

    class FinalColorPreviewQuery final : public IQuery
    {
    public:
        FinalColorPreviewQuery() = default;

        Result execute(const IQueryContext& queryContext, IQueryResult& outResult) const override
        {
            // 1) Engine 専用 context 以外を弾き、query の実行相手を固定する。
            const auto* engineQueryContext = dynamic_cast<const EngineQueryContext*>(&queryContext);
            if (engineQueryContext == nullptr)
            {
                return Result::fail(Facility::Core, Code::InvalidArg, Severity::Error, 0, "Query context type is invalid.");
            }

            // 2) 結果型を検証し、呼び出し側の受け皿ミスで descriptor を破壊しないようにする。
            auto* texturePreviewResult = dynamic_cast<TexturePreviewQueryResult*>(&outResult);
            if (texturePreviewResult == nullptr)
            {
                return Result::fail(Facility::Core, Code::InvalidArg, Severity::Error, 0, "Query result type is invalid.");
            }

            // 3) FinalColor の descriptor 解決だけを query に閉じ込め、Editor 側から backend 直呼びをなくす。
            return engineQueryContext->resolve_texture_shader_resource_descriptor(
                "FinalColor",
                texturePreviewResult->descriptorHandle);
        }
    };
}

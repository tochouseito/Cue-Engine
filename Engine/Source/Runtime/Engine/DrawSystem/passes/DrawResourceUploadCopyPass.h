#pragma once

/// ************************************************************************************
/// DrawResources の upload heap 内容を default heap へコピーするパス
/// ************************************************************************************

// === RHI includes ===
#include "FrameGraph.h"

// === Engine includes ===
#include "DrawSystem/DrawResources.h"

namespace Cue::DrawSystem
{
    /// @brief CPU が更新した frame upload buffer を描画前に GPU default buffer へ反映する
    class DrawResourceUploadCopyPass final : public RHI::FrameGraphPass
    {
    public:
        explicit DrawResourceUploadCopyPass(DrawResources& a_drawResources);
        ~DrawResourceUploadCopyPass() override;

        DrawResourceUploadCopyPass(const DrawResourceUploadCopyPass&) = delete;
        DrawResourceUploadCopyPass& operator=(const DrawResourceUploadCopyPass&) = delete;
        DrawResourceUploadCopyPass(DrawResourceUploadCopyPass&&) = delete;
        DrawResourceUploadCopyPass& operator=(DrawResourceUploadCopyPass&&) = delete;

        /// @brief FrameGraph 上の pass 名
        [[nodiscard]] const char* name() const noexcept override;

        /// @brief upload heap から default heap への転送なので copy queue で実行する
        [[nodiscard]] RHI::CommandListType type() const noexcept override;

        /// @brief 追加 RHI object は不要
        [[nodiscard]] Result setup(RHI::FrameGraphBuilder& a_builder) override;

        /// @brief コピー先 default buffer を CopyDest として宣言する
        [[nodiscard]] Result describe_resources(RHI::FrameGraphBuilder& a_builder) override;

        /// @brief 現在 frame の upload heap から default heap へコピーする
        void execute(RHI::FrameGraphContext& a_context) override;

    private:
        DrawResources& m_drawResources; // コピー対象の DrawSystem 共有 buffer
    };
} // namespace Cue::DrawSystem

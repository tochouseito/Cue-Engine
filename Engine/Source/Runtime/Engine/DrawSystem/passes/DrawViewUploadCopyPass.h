#pragma once

/// ****************************************************************************
/// View 固有 upload buffer を default heap へコピーする pass
/// ****************************************************************************

// === RHI includes ===
#include "FrameGraph.h"

// === Engine includes ===
#include "DrawSystem/DrawViewResources.h"

namespace Cue::DrawSystem
{
    /// @brief camera ごとの ViewProjectionBuffer を描画前に反映する copy pass
    class DrawViewUploadCopyPass final : public RHI::FrameGraphPass
    {
    public:
        explicit DrawViewUploadCopyPass(DrawViewResources& a_viewResources);
        ~DrawViewUploadCopyPass() override;
        DrawViewUploadCopyPass(const DrawViewUploadCopyPass&) = delete;
        DrawViewUploadCopyPass& operator=(const DrawViewUploadCopyPass&) = delete;
        DrawViewUploadCopyPass(DrawViewUploadCopyPass&&) = delete;
        DrawViewUploadCopyPass& operator=(DrawViewUploadCopyPass&&) = delete;

        /// @brief FrameGraph 上の pass 名
        [[nodiscard]] const char* name() const noexcept override;

        /// @brief upload heap から default heap への転送なので copy queue で実行する
        [[nodiscard]] RHI::CommandListType type() const noexcept override;

        /// @brief 追加 RHI object は不要
        [[nodiscard]] Result setup(RHI::FrameGraphBuilder& a_builder) override;

        /// @brief ViewProjection buffer を CopyDest として宣言する
        [[nodiscard]] Result describe_resources(RHI::FrameGraphBuilder& a_builder) override;

        /// @brief 現在 frame の ViewProjection upload data をコピーする
        void execute(RHI::FrameGraphContext& a_context) override;

    private:
        DrawViewResources& m_viewResources;
    };
} // namespace Cue::DrawSystem

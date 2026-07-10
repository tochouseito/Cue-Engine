#pragma once

/// ****************************************************************************
/// View 固有 visibility upload buffer を default heap へコピーする pass
/// ****************************************************************************

// === RHI includes ===
#include "FrameGraph.h"

// === Engine includes ===
#include "DrawSystem/DrawVisibilityResources.h"

namespace Cue::DrawSystem
{
    /// @brief camera ごとの indirect draw 入力を描画前に反映する copy pass
    class DrawVisibilityUploadCopyPass final : public RHI::FrameGraphPass
    {
    public:
        explicit DrawVisibilityUploadCopyPass(DrawVisibilityResources& a_visibilityResources);
        ~DrawVisibilityUploadCopyPass() override;
        DrawVisibilityUploadCopyPass(const DrawVisibilityUploadCopyPass&) = delete;
        DrawVisibilityUploadCopyPass& operator=(const DrawVisibilityUploadCopyPass&) = delete;
        DrawVisibilityUploadCopyPass(DrawVisibilityUploadCopyPass&&) = delete;
        DrawVisibilityUploadCopyPass& operator=(DrawVisibilityUploadCopyPass&&) = delete;

        /// @brief FrameGraph 上の pass 名
        [[nodiscard]] const char* name() const noexcept override;

        /// @brief upload heap から default heap への転送なので copy queue で実行する
        [[nodiscard]] RHI::CommandListType type() const noexcept override;

        /// @brief 追加 RHI object は不要
        [[nodiscard]] Result setup(RHI::FrameGraphBuilder& a_builder) override;

        /// @brief visibility buffer を CopyDest として宣言する
        [[nodiscard]] Result describe_resources(RHI::FrameGraphBuilder& a_builder) override;

        /// @brief 現在 frame の visibility upload data をコピーする
        void execute(RHI::FrameGraphContext& a_context) override;

    private:
        DrawVisibilityResources& m_visibilityResources;
    };
} // namespace Cue::DrawSystem

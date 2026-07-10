#pragma once

/// ****************************************************************************
/// shared DrawScene upload buffer を default heap へコピーする pass
/// ****************************************************************************

// === RHI includes ===
#include "FrameGraph.h"

// === Engine includes ===
#include "DrawSystem/DrawSceneResources.h"

namespace Cue::DrawSystem
{
    /// @brief Main と Debug が共有する Scene 入力を描画前に反映する copy pass
    class DrawSceneUploadCopyPass final : public RHI::FrameGraphPass
    {
    public:
        explicit DrawSceneUploadCopyPass(DrawSceneResources& a_sceneResources);
        ~DrawSceneUploadCopyPass() override;
        DrawSceneUploadCopyPass(const DrawSceneUploadCopyPass&) = delete;
        DrawSceneUploadCopyPass& operator=(const DrawSceneUploadCopyPass&) = delete;
        DrawSceneUploadCopyPass(DrawSceneUploadCopyPass&&) = delete;
        DrawSceneUploadCopyPass& operator=(DrawSceneUploadCopyPass&&) = delete;

        /// @brief FrameGraph 上の pass 名
        [[nodiscard]] const char* name() const noexcept override;

        /// @brief upload heap から default heap への転送なので copy queue で実行する
        [[nodiscard]] RHI::CommandListType type() const noexcept override;

        /// @brief 追加 RHI object は不要
        [[nodiscard]] Result setup(RHI::FrameGraphBuilder& a_builder) override;

        /// @brief Scene buffer を CopyDest として宣言する
        [[nodiscard]] Result describe_resources(RHI::FrameGraphBuilder& a_builder) override;

        /// @brief 現在 frame の Scene upload data をコピーする
        void execute(RHI::FrameGraphContext& a_context) override;

    private:
        DrawSceneResources& m_sceneResources;
    };
} // namespace Cue::DrawSystem

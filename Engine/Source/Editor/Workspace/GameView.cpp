#include "GameView.h"

// === C++ includes ===
#include <algorithm>

namespace Cue::Editor
{
ImVec2 GameView::calculate_fit_size(ImVec2 a_availableRegion, uint32_t a_textureWidth,
                                    uint32_t a_textureHeight) noexcept
{
    if (a_availableRegion.x <= 0.0f || a_availableRegion.y <= 0.0f || a_textureWidth == 0 || a_textureHeight == 0)
    {
        return ImVec2(0.0f, 0.0f);
    }

    const float textureAspect = static_cast<float>(a_textureWidth) / static_cast<float>(a_textureHeight);
    float width = a_availableRegion.x;
    float height = width / textureAspect;
    if (height > a_availableRegion.y)
    {
        height = a_availableRegion.y;
        width = height * textureAspect;
    }

    return ImVec2(std::max(width, 0.0f), std::max(height, 0.0f));
}

void GameView::update()
{
    if (m_backend == nullptr || m_backend->get_view_manager() == nullptr)
    {
        CUE_ASSERT_MSG(false, "GameView: Backend is null");
        return;
    }

    // resize 後は FinalColorSRV が再作成されるため、handle を毎フレーム解決する。
    Result viewResult = m_backend->get_view_manager()->get_view("FinalColorSRV", m_finalColorSrvHandle);
    CUE_ASSERT_FORMAT(success(viewResult), "Failed to get FinalColorSRV: {}", viewResult.message.data());

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    const bool isVisible = ImGui::Begin("GameView", nullptr, windowFlags);

    if (!isVisible)
    {
        ImGui::End();
        ImGui::PopStyleVar();
        return;
    }

    const uint32_t finalColorWidth = m_backend->width();
    const uint32_t finalColorHeight = m_backend->height();
    const ImVec2 availableRegion = ImGui::GetContentRegionAvail();
    const ImVec2 imageSize = calculate_fit_size(availableRegion, finalColorWidth, finalColorHeight);

    D3D12_GPU_DESCRIPTOR_HANDLE finalColorSrvGpuDescHandle = m_backend->get_gpu_descriptor_handle(
        m_finalColorSrvHandle, m_backend->current_back_buffer_index(), m_backend->buffer_count());

    if (finalColorSrvGpuDescHandle.ptr != 0 && imageSize.x > 0.0f && imageSize.y > 0.0f)
    {
        // Dock 配置で余った領域がある場合、GameView 内で中央に配置する。
        const float offsetX = std::max((availableRegion.x - imageSize.x) * 0.5f, 0.0f);
        const float offsetY = std::max((availableRegion.y - imageSize.y) * 0.5f, 0.0f);
        ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + offsetX, ImGui::GetCursorPosY() + offsetY));
        ImGui::Image(static_cast<ImTextureID>(finalColorSrvGpuDescHandle.ptr), imageSize);
    }

    ImGui::End();
    ImGui::PopStyleVar();
}
} // namespace Cue::Editor
